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
#include "grab.h"

#ifdef BUILD_WITH_PARALLELISM

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <pthread.h>

pthread_mutex_t stdout_lock = PTHREAD_MUTEX_INITIALIZER;

#endif


using namespace std;


char *const fail_addr = (char *)-1;

string FileGrep::start_inv = "\33[7m";
string FileGrep::stop_inv = "\33[27m";


FileGrep::FileGrep()
{
	d_my_uid = geteuid();
}


FileGrep::~FileGrep()
{
	if (d_extra)
		pcre_free_study(d_extra);
}


void FileGrep::config(const map<string, size_t> &config)
{
	if (config.count("color") > 0)
		d_colored = 1;
	if (config.count("noline") > 0)
		d_print_line = 0;
	if (config.count("offsets") > 0)
		d_print_offset = 1;
	if (config.count("single") > 0)
		d_single_match = 1;
	if (config.count("low_mem") > 0)
		d_low_mem = 1;
	auto it = config.find("chunk_size");
	if (it != config.end())
		d_chunk_size = it->second;
}


int FileGrep::prepare(const string &regex)
{
	const char *errptr = nullptr;
	int erroff = 0;

	if ((d_pcreh = pcre_compile(regex.c_str(), 0, &errptr, &erroff, pcre_maketables())) == nullptr) {
		d_err = "FileGrep::prepare::pcre_compile error";
		return -1;
	}

#ifndef PCRE_STUDY_JIT_COMPILE
#define PCRE_STUDY_JIT_COMPILE 0
#endif

	if ((d_extra = pcre_study(d_pcreh, PCRE_STUDY_JIT_COMPILE, &errptr)) == nullptr) {
		d_err = "FileGrep::prepare::pcre_study error" ;
		return -1;
	}

	pcre_fullinfo(d_pcreh, d_extra, PCRE_INFO_MINLENGTH, &d_minlen);

	return 0;
}


enum {
	mmap_flags = MAP_PRIVATE|MAP_NORESERVE
};


int FileGrep::find(const char *path, const struct stat *st, int typeflag)
{
	size_t clen = st->st_size;
	if ((size_t)d_minlen > clen)
		return 0;

	int fd = -1, flags = O_RDONLY|O_NOCTTY;

#ifdef __linux__
	// try to avoid marking inode as dirty
	if (st->st_uid == d_my_uid || d_my_uid == 0)
		flags |= O_NOATIME;
#endif

	if ((fd = open(path, flags)) < 0) {
		d_err = "FileGrep::find::open: " + string(strerror(errno));
		return -1;
	}

	char *content = nullptr;
	const int overlap = 0x1000;
	ostringstream str;

	for (off_t off = 0; off < st->st_size; off += (d_chunk_size - overlap)) {

		if (st->st_size - off < (off_t)d_chunk_size)
			clen = st->st_size - off;
		else
			clen = d_chunk_size;

		if ((content = (char *)mmap(nullptr, clen, PROT_READ, mmap_flags, fd, off)) == fail_addr) {
			d_err = "FileGrep::find::mmap: " + string(strerror(errno));
			close(fd);
			return -1;
		}

		// ignore errors
		if (clen > 4*0x1000 && !d_single_match)
			posix_madvise(content, clen, POSIX_MADV_SEQUENTIAL);

		int ovector[3] = {-1}, rc = 0;
		const char *start = content, *end = content + clen;
		char before[512] = {0}, after[512] = {0};

		for (;start + d_minlen < end;) {

			memset(ovector, 0, sizeof(ovector));
			rc = pcre_exec(d_pcreh, d_extra, start, end - start, 0, 0, ovector, 3);
			if (rc <= 0)
				break;

			if (d_recursive || d_print_path)
				str<<path<<":";

			if (d_print_offset)
				str<<"Match at offset "<<off + start - content + (int)ovector[0]<<endl;

			uint16_t a = 0, b = sizeof(before) - 1;
			if (d_print_line) {
				const char *ptr = start + ovector[0] - 1;

				while (ptr >= start && *ptr != '\n' && b > 0)
					before[b--] = *ptr--;
				ptr = start + ovector[1];
				while (ptr < end && *ptr != '\n' && a < sizeof(after) - 1)
					after[a++] = *ptr++;
				str<<string(before + b + 1, sizeof(before) - b - 1);
				if (d_colored)
					str<<start_inv;
				str<<string(start + ovector[0], ovector[1] - ovector[0]);
				if (d_colored)
					str<<stop_inv;
				str<<string(after, a)<<endl;
			} else if (!d_print_offset) {
				str<<"matches\n";
				break;
			}

			start += ovector[1] + a;

			if (d_single_match)
				break;
		}

		munmap(content, clen);

		if (str.str().size() > 0) {
#ifdef BUILD_WITH_PARALLELISM
			pthread_mutex_lock(&stdout_lock);
#endif

			cout<<str.str();

#ifdef BUILD_WITH_PARALLELISM
			pthread_mutex_unlock(&stdout_lock);
#endif

			str.flush();
			str.clear();
			str.str("");

			if (d_single_match)
				break;
		}
	}

	close(fd);
	return 0;
}


int FileGrep::find(const string &path)
{
	struct stat st;
	if (stat(path.c_str(), &st) < 0) {
		d_err = "FileGrep::find::stat: " + string(strerror(errno));
		return -1;
	}
	int r = 0;

	if (S_ISREG(st.st_mode))
		r = find(path.c_str(), &st, FTW_F);
	else if (S_ISDIR(st.st_mode))
		cerr<<"Clever boy! Want recursion? Add -R!\n";

	return r;
}


extern FileGrep *grep;


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


int FileGrep::find_recursive(const string &path)
{
	d_recursive = 1;
	return nftw(path.c_str(), walk, 1024, FTW_PHYS);
}



