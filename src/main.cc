/*
 * Copyright (C) 2012-2020 Sebastian Krahmer.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by Sebastian Krahmer.
 * 4. The name Sebastian Krahmer may not be used to endorse or promote
 *    products derived from this software without specific prior written
 *    permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <string>
#include <vector>
#include <map>
#include <cstdio>
#include <stdint.h>
#include <iostream>
#include <sstream>
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>
#include <cstring>
#include <sys/stat.h>
#include <pcre.h>
#include "grab.h"
#include "nftw.h"

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <pthread.h>


using namespace std;
using namespace grab;


FileGrep *grep = nullptr;

thread_local FileGrep *tgrep = nullptr;

pthread_mutex_t start_lck = PTHREAD_MUTEX_INITIALIZER;

struct thread_arg {
	int idx, nthreads;
	string path;
	FileGrep *grep;
};


int thread_grep_once(const char *path, const struct stat *st, int typeflag, void *ftwbuf)
{
	if (typeflag == FTW_F) {
		if (S_ISREG(st->st_mode))
			return tgrep->find(path, st, FTW_F);
	}
	return 0;
}





void *find_iterative(void *vp)
{
	thread_arg *ta = static_cast<thread_arg *>(vp);

	// thread_local
	tgrep = ta->grep;

	pthread_mutex_lock(&start_lck);
	pthread_mutex_unlock(&start_lck);

	while (nftw_multi(ta->path.c_str(), thread_grep_once, 1024, FTW_PHYS) == 1)
		;

	return nullptr;
}


void usage(const string &p)
{
	cout<<"Usage: "<<p<<" [-rR] [-I] [-O] [-L] [-l] [-s] [-n <cores>] <regex> <path>\n";
	exit(1);
}


int main(int argc, char **argv)
{
	int c = 0;
	map<string, size_t> config;
	size_t chunk_size = 1<<30;

	while ((c = getopt(argc, argv, "Rrn:IOlsL")) != -1) {
		switch (c) {
		case 'r':
		case 'R':
			config["recursive"] = 1;
			break;
		case 's':
			config["single"] = 1;
			break;
		case 'O':
			config["offsets"] = 1;
			break;
		case 'l':
			config["noline"] = 1;
			break;
		case 'L':
			config["low_mem"] = 1;
			chunk_size >>= 1;
			if (chunk_size < (1<<25))
				chunk_size = 1<<25;
			break;
		case 'I':
			if (isatty(1))
				config["color"] = 1;
			break;
		case 'n':
			config["cores"] = atoi(optarg);
			break;
		default:
			usage(argv[0]);
		}
	}

	config["chunk_size"] = chunk_size;

	string path = "", regex = "";

	if (argc < optind + 2)
		usage(argv[0]);

	regex = argv[optind++];
	path = argv[optind++];

	int cores = config["cores"];
	if (cores > 1) {

		if (config.count("recursive") == 0) {
			cerr<<"Multicore support only for recursive grabs.\n";
			return -1;
		}

		chunk_size >>= 2;
		config["chunk_size"] = chunk_size;

		FileGrep *tgrep = nullptr;
		cpu_set_t cpuset;
		thread_arg *ta = new (nothrow) thread_arg[cores];
		pthread_t *tids = new (nothrow) pthread_t[cores];

		if (!ta || !tids) {
			cerr<<"Out of memory.\n";
			return -1;
		}

		for (int i = 0; i < cores; ++i) {
			tgrep = new (nothrow) FileGrep;
			tgrep->config(config);
			tgrep->prepare(regex);
			tgrep->recurse();
			ta[i].grep = tgrep;
			ta[i].idx = i;
			ta[i].nthreads = cores;
			ta[i].path = path;
		}

		int r = 0;

		// Avoid head start of the first thread. Thats also why above and below loops
		// are split, even though they could be merged.
		pthread_mutex_lock(&start_lck);

		for (int i = 0; i < cores; ++i) {
			CPU_ZERO(&cpuset);
			CPU_SET(i, &cpuset);

			if ((r = pthread_create(tids + i, nullptr, find_iterative, ta + i)) != 0) {
				cerr<<"pthread_create: "<<strerror(r)<<endl;
				exit(-1);
			}
			if ((r = pthread_setaffinity_np(tids[i], sizeof(cpuset), &cpuset)) != 0) {
				cerr<<"pthread_setaffinity_np:"<<strerror(r)
				    <<" (more threads than cores?)"<<endl;
				exit(-1);
			}
		}

		// Go, go, go ...
		pthread_mutex_unlock(&start_lck);

		for (int i = 0; i < cores; ++i) {
			pthread_join(tids[i], nullptr);
			delete ta[i].grep;
		}

		delete [] ta;
		delete [] tids;

		exit(0);

	}

	if (!(grep = new (nothrow) FileGrep)) {
		cerr<<"Out of memory.\n";
		return -1;
	}

	grep->config(config);

	if (grep->prepare(regex) < 0) {
		cerr<<grep->why()<<endl;
		return -1;
	}

	if (config.count("recursive") > 0) {
		if (grep->find_recursive(path) < 0) {
			cerr<<grep->why()<<endl;
			return -1;
		}
	} else {
		if (argc - optind > 0)
			grep->show_path(1);
		for (;;) {
			if (grep->find(path) < 0) {
				cerr<<grep->why()<<endl;
				return -1;
			}
			if (argc > optind)
				path = argv[optind++];
			else
				break;
		}
	}

	delete grep;

	return 0;
}


