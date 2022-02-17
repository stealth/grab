/*
 * Copyright (C) 2012-2022 Sebastian Krahmer.
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
#include <thread>
#include <cstdio>
#include <stdint.h>
#include <iostream>
#include <sstream>
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>
#include <cstring>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/resource.h>
#include "grab.h"
#include "nftw.h"


using namespace std;
using namespace grab;


FileGrep *grep = nullptr;

uint32_t min_file_size = 0;

#ifdef __APPLE__
__thread FileGrep *tgrep = nullptr;
#else
thread_local FileGrep *tgrep = nullptr;
#endif

struct thread_arg {
	string path{""};
	FileGrep *grep{nullptr};

	int idx{0}, nthreads{0};

public:

	virtual ~thread_arg()
	{
		delete grep;
	}
};


int thread_grep_once(int dfd, const char *dirname, const char *basename, const struct stat *st, int typeflag, void *ftwbuf)
{
	// since we use our own dedicated nftw() impl, only typeglag == FTW_F
	// and S_ISREG() files will reach us, so we do not need to check again
	return tgrep->find(dfd, dirname, basename, st, G_FTW_F);
}


void *find_iterative(void *vp)
{
	thread_arg *ta = static_cast<thread_arg *>(vp);

	const char *c_str = ta->path.c_str();

	// thread_local
	tgrep = ta->grep;

	while (nftw_multi(c_str, thread_grep_once, 1024, G_FTW_PHYS) == 1)
		;

	return nullptr;
}


void usage(const string &p)
{
	cout<<"\nParallel grep (C) Sebastian Krahmer -- https://github.com/stealth/grab\n\n"
	    <<"Usage: "<<p<<" [-rIOLlsSH] [-n <cores>] <regex> <path>\n\n"
	    <<"\t-2\t-- use PCRE2 instead of PCRE\n"
	    <<"\t-O\t-- print file offset of match\n"
	    <<"\t-l\t-- do not print the matching line (Useful if you want\n"
	    <<"\t\t   to see _all_ offsets; if you also print the line, only\n"
	    <<"\t\t   the first match in the line counts)\n"
	    <<"\t-s\t-- single match; dont search file further after first match\n"
	    <<"\t\t   (similar to grep on a binary)\n"
#ifdef WITH_HYPERSCAN
	    <<"\t-H\t-- use hyperscan lib for scanning\n"
	    <<"\t-S\t-- only for hyperscan: interpret pattern as string literal instead of regex\n"
#else
	    <<"\t-H -S\t-- support not compiled in (hyperscan lib)\n"
#endif
	    <<"\t-L\t-- machine has low mem; half chunk-size (default 2GB)\n"
	    <<"\t\t   may be used multiple times\n"
	    <<"\t-I\t-- enable highlighting of matches (useful)\n"
	    <<"\t-n\t-- Use multiple cores in parallel (omit for single core)\n"
	    <<"\t-r\t-- recurse on directory\n\n";

	exit(1);
}


int main(int argc, char **argv)
{
	int c = 0;
	map<string, size_t> config;
	size_t chunk_size = 1<<30;

	while ((c = getopt(argc, argv, "2Rrn:IOlsSLH")) != -1) {
		switch (c) {
		case '2':
			config["pcre2"] = 1;
			break;
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

		rlimit rl{0, 0};
		getrlimit(RLIMIT_NOFILE, &rl);
		dirvec = new (nothrow) dir_cache(rl.rlim_cur);

		if (config.count("recursive") == 0) {
			cerr<<"Multicore support only for recursive grabs.\n";
			return -1;
		}

		chunk_size >>= 2;
		config["chunk_size"] = chunk_size;

		FileGrep *tgrep = nullptr;
		thread_arg *ta = new (nothrow) thread_arg[cores];

		if (!ta) {
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

		vector<thread> tds;

		for (int i = 0; i < cores; ++i)
			tds.emplace_back(find_iterative, ta + i);

		for (auto it = tds.begin(); it != tds.end(); ++it)
			it->join();

		for (int i = 0; i < cores; ++i)
			ta[i].grep->flush_ostream();

		delete [] ta;
		delete dirvec;
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

	grep->flush_ostream();

	delete grep;

	return 0;
}


