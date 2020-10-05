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
#include "grab.h"
#include "nftw.h"

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <pthread.h>


using namespace std;
using namespace grab;


FileGrep *grep = nullptr;

uint32_t min_file_size = 0;

#ifdef __APPLE__
__thread FileGrep *tgrep = nullptr;
#else
thread_local FileGrep *tgrep = nullptr;
#endif

pthread_mutex_t start_lck = PTHREAD_MUTEX_INITIALIZER;

struct thread_arg {
	int idx, nthreads;
	string path;
	FileGrep *grep;
};


int thread_grep_once(const char *path, const struct stat *st, int typeflag, void *ftwbuf)
{
	// since we use our own dedicated nftw() impl, only typeglag == FTW_F
	// and S_ISREG() files will reach us, so we do not need to check again
	return tgrep->find(path, st, G_FTW_F);
}


void *find_iterative(void *vp)
{
	thread_arg *ta = static_cast<thread_arg *>(vp);

	// thread_local
	tgrep = ta->grep;

	while (nftw_multi(ta->path.c_str(), thread_grep_once, 1024, G_FTW_PHYS) == 1)
		;

	return nullptr;
}


void usage(const string &p)
{
	cout<<"\nParallel grep (C) Sebastian Krahmer -- https://github.com/stealth/grab\n\n"
	    <<"Usage: "<<p<<" [-rRIOLlsSH] [-n <cores>] <regex> <path>\n\n"
	    <<" -O     -- print file offset of match\n"
	    <<" -l     -- do not print the matching line (Useful if you want\n"
	    <<"           to see _all_ offsets; if you also print the line, only\n"
	    <<"           the first match in the line counts)\n"
	    <<" -s     -- single match; dont search file further after first match\n"
	    <<"           (similar to grep on a binary)\n"
#ifdef WITH_HYPERSCAN
	    <<" -H     -- use hyerscan lib for scanning (see build instructions)\n"
	    <<" -S     -- only for hyperscan: interpret pattern as string literal instead of regex\n"
#else
	    <<" -H -S  -- support not compiled in (hyperscan lib)\n"
#endif
	    <<" -L     -- machine has low mem; half chunk-size (default 1GB)\n"
	    <<"           may be used multiple times\n"
	    <<" -I     -- enable highlighting of matches (useful)\n"
	    <<" -n <n> -- Use n cores in parallel (recommended for flash/SSD)\n"
	    <<"           n <= 1 uses single-core\n"
	    <<" -r     -- recurse on directory\n"
	    <<" -R     -- same as -r\n\n";


	exit(1);
}


int main(int argc, char **argv)
{
	int c = 0;
	map<string, size_t> config;
	size_t chunk_size = 1<<30;

	while ((c = getopt(argc, argv, "Rrn:IOlsSLH")) != -1) {
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
		case  'H':
			config["hyperscan"] = 1;
			break;
		case 'S':
			config["literal"] = 1;
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
	uint32_t re_min_size = 0xffffffff;

	if (cores > 1) {

		if (config.count("recursive") == 0) {
			cerr<<"Multicore support only for recursive grabs.\n";
			return -1;
		}

		chunk_size >>= 2;
		config["chunk_size"] = chunk_size;

		FileGrep *tgrep = nullptr;
		thread_arg *ta = new (nothrow) thread_arg[cores];
		pthread_t *tids = new (nothrow) pthread_t[cores];

		if (!ta || !tids) {
			cerr<<"Out of memory.\n";
			return -1;
		}

		for (int i = 0; i < cores; ++i) {
			tgrep = new (nothrow) FileGrep;
			if (tgrep->config(config) < 0) {
				cerr<<tgrep->why()<<endl;
				exit(1);
			}
			tgrep->recurse();
			if (tgrep->compile(regex, re_min_size) < 0) {
				cerr<<tgrep->why()<<endl;
				exit(1);
			}

			// set global variable for our nftw() impls to early
			// ignore too small files instead of returning up the callstack
			// into a handler and then returning because of too small size
			// We are not threaded yet, so this doesn't need locking
			min_file_size = re_min_size;

			ta[i].grep = tgrep;
			ta[i].idx = i;
			ta[i].nthreads = cores;
			ta[i].path = path;
		}

		int r = 0;

		for (int i = 0; i < cores; ++i) {

			if ((r = pthread_create(tids + i, nullptr, find_iterative, ta + i)) != 0) {
				cerr<<"pthread_create: "<<strerror(r)<<endl;
				exit(-1);
			}

#ifdef __linux__
			cpu_set_t cpuset;
			CPU_ZERO(&cpuset);
			CPU_SET(i, &cpuset);

			if ((r = pthread_setaffinity_np(tids[i], sizeof(cpuset), &cpuset)) != 0) {
				cerr<<"pthread_setaffinity_np:"<<strerror(r)
				    <<" (more threads than cores?)"<<endl;
				exit(-1);
			}
#endif
		}

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

	if (grep->config(config) < 0) {
		cerr<<grep->why()<<endl;
		return -1;
	}

	if (grep->compile(regex, min_file_size) < 0) {
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


