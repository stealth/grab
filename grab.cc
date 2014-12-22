/*
 * Copyright (C) 2012-2014 Sebastian Krahmer.
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
#include <sys/mman.h>
#include <ftw.h>
#include <pcre.h>

#ifdef BUILD_WITH_PARALLELISM

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <pthread.h>

pthread_mutex_t stdout_lock = PTHREAD_MUTEX_INITIALIZER;

#endif


using namespace std;


char *const fail_addr = (char *)-1;
size_t chunk_size = 1<<30;


class FileGrep {

	std::string err;
	static std::string start_inv, stop_inv;
	int minlen;
	char *mmap_buf;
	size_t clen;
	bool print_line, print_offset, recursive, colored, print_path;


	pcre *pcreh;
	pcre_extra *extra;
public:

	FileGrep();

	~FileGrep();

	const char *why()
	{
		return err.c_str();
	}

	void recurse()
	{
		recursive = 1;
	}

	void show_path(bool b)
	{
		print_path = b;
	}

	int prepare(const std::string &);

	void config(const std::map<std::string, int> &);

	int find(const std::string &);

	int find(const char *path, const struct stat *st, int typeflag);

	int find_recursive(const std::string &);
};


string FileGrep::start_inv = "\33[7m";
string FileGrep::stop_inv = "\33[27m";

FileGrep *grep = NULL;


struct thread_arg {
	int idx, nthreads;
	FileGrep *grep;
};

vector<string> files;
vector<struct stat> stats;


int walk(const char *path, const struct stat *st, int typeflag, struct FTW *ftwbuf)
{
	if (typeflag == FTW_F) {
		if (S_ISREG(st->st_mode)) {
			if (grep->find(path, st, typeflag) < 0)
				cerr<<path<<": "<<grep->why()<<endl;
		}
	}
	return 0;
}


int thread_walk(const char *path, const struct stat *st, int typeflag, struct FTW *ftwbuf)
{
	if (typeflag == FTW_F) {
		if (S_ISREG(st->st_mode)) {
			files.push_back(path);
			stats.push_back(*st);
		}
	}
	return 0;
}



FileGrep::FileGrep()
	: err(""), minlen(1), mmap_buf(fail_addr), clen(0),
	  print_line(1), print_offset(0), recursive(0), colored(0), print_path(0), pcreh(NULL), extra(NULL)
{
}


FileGrep::~FileGrep()
{
	if (extra)
		pcre_free_study(extra);
}


void FileGrep::config(const std::map<std::string, int> &config)
{
	if (config.count("color") > 0)
		colored = 1;
	if (config.count("noline") > 0)
		print_line = 0;
	if (config.count("offsets") > 0)
		print_offset = 1;
}


int FileGrep::prepare(const string &regex)
{
	const char *errptr = NULL;
	int erroff = 0;

	if ((pcreh = pcre_compile(regex.c_str(), 0, &errptr, &erroff, pcre_maketables())) == NULL) {
		err = "FileGrep::prepare::pcre_compile error";
		return -1;
	}

#ifndef PCRE_STUDY_JIT_COMPILE
#define PCRE_STUDY_JIT_COMPILE 0
#endif

	if ((extra = pcre_study(pcreh, PCRE_STUDY_JIT_COMPILE, &errptr)) == NULL) {
		err = "FileGrep::prepare::pcre_study error" ;
		return -1;
	}

	pcre_fullinfo(pcreh, extra, PCRE_INFO_MINLENGTH, &minlen);

	return 0;
}


int FileGrep::find(const char *path, const struct stat *st, int typeflag)
{
	size_t clen = st->st_size;
	if ((size_t)minlen > clen)
		return 0;

	int fd;
	if ((fd = open(path, O_RDONLY|O_NOCTTY)) < 0) {
		err = "FileGrep::find::open: " + string(strerror(errno));
		return -1;
	}

	char *content = NULL;
	int overlap = 0x1000;
	stringstream str;

	for (off_t off = 0; off < st->st_size; off += (chunk_size - overlap)) {
		if (st->st_size - off < (off_t)chunk_size)
			clen = st->st_size - off;
		else
			clen = chunk_size;

		if ((content = (char *)mmap(NULL, clen,
		                            PROT_READ, MAP_PRIVATE||MAP_NORESERVE|MAP_POPULATE,
		                            fd, off)) == fail_addr) {
			err = "FileGrep::find::mmap: " + string(strerror(errno));
			close(fd);
			return -1;
		}

		int ovector[3] = {-1}, rc = 0;
		const char *start = content, *end = content + clen;
		char before[256], after[256];
		bool print_results = false;

		for (;;) {
			memset(ovector, 0, sizeof(ovector));
			rc = pcre_exec(pcreh, extra, start, end - start, 0, 0, ovector, 3);
			if (rc <= 0)
				break;

			print_results = true;
			if (recursive || print_path)
				str<<path<<": ";

			if (print_offset)
				str<<"Match at offset "<<off + start - content + (int)ovector[0]<<endl;

			uint8_t a = 0, b = sizeof(before) - 1;
			if (print_line) {
				const char *ptr = start + ovector[0] - 1;

				while (ptr >= start && *ptr != '\n' && b > 0)
					before[b--] = *ptr--;
				before[b] = 0;
				ptr = start + ovector[1];
				while (ptr < end && *ptr != '\n' && a < sizeof(after) - 1)
					after[a++] = *ptr++;
				after[a] = 0;
				str<<string(before + b, sizeof(before) - b);
				if (colored)
					str<<start_inv;
				str<<string(start + ovector[0], ovector[1] - ovector[0]);
				if (colored)
					str<<stop_inv;
				str<<string(after, a)<<endl;
			} else if (!print_offset) {
				str<<"matches\n";
				break;
			}

			start += ovector[1] + a;
		}

		if (print_results) {
#ifdef BUILD_WITH_PARALLELISM
		pthread_mutex_lock(&stdout_lock);
#endif

		cout<<str.str();

#ifdef BUILD_WITH_PARALLELISM
		pthread_mutex_unlock(&stdout_lock);
#endif

		str.flush();
		}

		munmap(content, clen);
	}

	close(fd);
	return 0;
}


int FileGrep::find(const string &path)
{
	struct stat st;
	if (stat(path.c_str(), &st) < 0) {
		err = "FileGrep::find::stat: " + string(strerror(errno));
		return -1;
	}
	int r = 0;

	if (S_ISREG(st.st_mode))
		r = find(path.c_str(), &st, FTW_F);
	else if (S_ISDIR(st.st_mode))
		cerr<<"Clever boy! Want recursion? Add -R!\n";

	return r;
}


int FileGrep::find_recursive(const string &path)
{
	recursive = 1;
	return nftw(path.c_str(), walk, 1024, FTW_PHYS);
}


void *find_iterative(void *vp)
{
	string path = "";
	struct stat st;
	thread_arg *ta = static_cast<thread_arg *>(vp);
	FileGrep *grep = ta->grep;

	int vsize = files.size();
	for (int i = ta->idx; i < vsize; i += ta->nthreads) {
		path = files[i];
		st = stats[i];
		grep->find(path.c_str(), &st, FTW_F);
	}
	return NULL;
}


void usage(const string &p)
{
	cout<<"Usage: "<<p<<" [-rR] [-I] [-O] [-l] [-n <cores>] <regex> <path>\n";
	exit(1);
}


int main(int argc, char **argv)
{
	int c = 0;
	map<string, int> config;

	while ((c = getopt(argc, argv, "Rrn:IOl")) != -1) {
		switch (c) {
		case 'r':
		case 'R':
			config["recursive"] = 1;
			break;
		case 'O':
			config["offsets"] = 1;
			break;
		case 'l':
			config["noline"] = 1;
			break;
		case 'I':
			if (isatty(1))
				config["color"] = 1;
			break;
		case 'n':
#ifndef BUILD_WITH_PARALLELISM
			cerr<<"Built without multicore support! Ignoring.\n";
#else
			config["cores"] = atoi(optarg);
#endif
			break;
		default:
			usage(argv[0]);
		}
	}

	string path = "", regex = "";

	if (argc < optind + 2)
		usage(argv[0]);

	regex = argv[optind++];
	path = argv[optind++];

#ifdef BUILD_WITH_PARALLELISM
	int cores = config["cores"];
	if (cores > 1) {
		if (config.count("recursive") == 0) {
			cerr<<"Multicore support only for recursive grabs.\n";
			return -1;
		}

		chunk_size >>= 2;

		files.reserve(1<<20);
		stats.reserve(1<<20);

		nftw(path.c_str(), thread_walk, 1024, FTW_PHYS);

		FileGrep *tgrep = NULL;
		cpu_set_t cpuset;
		thread_arg ta[cores];
		pthread_t tids[cores];
		int r = 0;

		for (int i = 0; i < cores; ++i) {
			tgrep = new FileGrep;
			tgrep->config(config);
			tgrep->prepare(regex);
			tgrep->recurse();
			CPU_ZERO(&cpuset);
			CPU_SET(i, &cpuset);

			ta[i].grep = tgrep;
			ta[i].idx = i;
			ta[i].nthreads = cores;

			if ((r = pthread_create(tids + i, NULL, find_iterative, ta + i)) != 0) {
				cerr<<"pthread_create: "<<strerror(r)<<endl;
				exit(-1);
			}
			if ((r = pthread_setaffinity_np(tids[i], sizeof(cpuset), &cpuset)) != 0) {
				cerr<<"pthread_setaffinity_np:"<<strerror(r)
				    <<" (more threads than cores?)"<<endl;
				exit(-1);
			}
		}

		for (int i = 0; i < cores; ++i) {
			pthread_join(tids[i], NULL);
			delete ta[i].grep;
		}

		exit(0);

	}

#endif
	grep = new FileGrep;

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


