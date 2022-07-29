/*
 * Copyright (C) 2022 Sebastian Krahmer.
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
#include <atomic>
#include <memory>
#include <map>
#include <thread>
#include <cstdio>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>
#include <cstring>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/resource.h>
#include "filter.h"
#include "nftw.h"


using namespace std;
using namespace grab;

unique_ptr<Filter> filter(nullptr);

// as needed by nftw()
uint32_t min_file_size = 0, max_recursion_depth = 0xffffffff;


struct thread_arg {
	string path{""};

	int idx{0}, nthreads{0};
};


atomic<char> stdout_lck{0};


int thread_filter_once(int dfd, const char *dirname, const char *basename, const struct stat *st, int typeflag, void *ftwbuf)
{
	if (filter->find(dfd, dirname, basename, st, G_FTW_F) == 0)
		return 0;

	while (stdout_lck.exchange(1) == 1);
	printf("%s/%s\n", dirname, basename);
	stdout_lck.store(0);

	return 0;
}


void *find_iterative(void *vp)
{
	thread_arg *ta = static_cast<thread_arg *>(vp);

	const char *c_str = ta->path.c_str();

	while (nftw_multi(c_str, thread_filter_once, 1024, G_FTW_PHYS|G_FTW_NAME_ONLY) == 1)
		;

	return nullptr;
}


void usage(const string &p)
{
	printf("\nParallel find (C) 2022 Sebastian Krahmer -- https://github.com/stealth/grab\n\n"
	       "Usage:\t%s\t[-n CORES] <directory> [-name NAME] [-size BYTES] [-uid UID]\n"
               "\t\t[-gid GID] [-perm OCTAL] [-maxdepth N] [-type TYPE]\n\n"
	       "\t-n\t\t-- use CORES CPU cores (default 1)\n"
	       "\t-name\t\t-- may be be any shell-metacharacter based name match\n"
	       "\t-size\t\t-- only print files that contain at least BYTES bytes\n"
	       "\t-uid\t\t-- only print files that are owned by UID\n"
	       "\t-gid\t\t-- only print files with group owner GID\n"
	       "\t-maxdepth\t-- do not recurse deeper than N\n"
	       "\t-type\t\t-- as you know it from find\n"
	       "\t-perm\t\t-- may be prefixed with - or / just like with `find`\n\n", p.c_str());

	exit(1);
}


int main(int argc, char **argv)
{
	int cores = 1, i = 1;

	while (argv[i]) {
		if (strcmp(argv[i], "-n") == 0 && i + 1 < argc) {
			cores = atoi(argv[++i]);
			++i;
		}
		if (argv[i] && argv[i][0] != '-')
			break;
		else
			++i;
	}

	if (argc < i + 1)
		usage(argv[0]);

	string path = argv[i++];

	filter.reset(new (nothrow) Filter);

	while (argv[i]) {
		if (strcmp(argv[i], "-name") == 0 && i + 1 < argc)
			filter->add_name(argv[++i]);
		if (strcmp(argv[i], "-uid") == 0 && i + 1 < argc)
			filter->add_uid(atoi(argv[++i]));
		if (strcmp(argv[i], "-gid") == 0 && i + 1 < argc)
			filter->add_gid(atoi(argv[++i]));
		if (strcmp(argv[i], "-type") == 0 && i + 1 < argc)
			filter->add_type(argv[++i][0]);
		if (strcmp(argv[i], "-perm") == 0 && i + 1 < argc)
			filter->add_perm(argv[++i]);
		if (strcmp(argv[i], "-size") == 0 && i + 1 < argc)
			filter->add_size(strtoull(argv[++i], nullptr, 10));
		if (strcmp(argv[i], "-maxdepth") == 0 && i + 1 < argc)
			max_recursion_depth = strtoul(argv[++i], nullptr, 10);
		++i;
	}

	rlimit rl{0, 0};
	getrlimit(RLIMIT_NOFILE, &rl);
	dirvec = new (nothrow) dir_cache(rl.rlim_cur);

	vector<thread_arg> ta(cores);

	for (int i = 0; i < cores; ++i) {
		ta[i].idx = i;
		ta[i].nthreads = cores;
		ta[i].path = path;
	}

	vector<thread> tds;

	for (int i = 0; i < cores; ++i)
		tds.emplace_back(find_iterative, &ta[i]);

	for (auto it = tds.begin(); it != tds.end(); ++it)
		it->join();

	delete dirvec;

	return 0;
}


