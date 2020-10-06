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
#include "grab.h"
#include "nftw.h"
#include "engine.h"
#include "engine-pcre.h"

#ifdef WITH_HYPERSCAN
#include "engine-hs.h"
#endif

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <pthread.h>


using namespace std;


extern grab::FileGrep *grep;


namespace grab {

pthread_mutex_t stdout_lock = PTHREAD_MUTEX_INITIALIZER;

char *const fail_addr = (char *)-1;

string FileGrep::start_inv = "\33[7m";
string FileGrep::stop_inv = "\33[27m";


FileGrep::FileGrep()
{
	d_my_uid = geteuid();
}


FileGrep::~FileGrep()
{
	delete d_engine;
}


int FileGrep::config(const map<string, size_t> &config)
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

	if (config.count("hyperscan") > 0) {
#ifdef WITH_HYPERSCAN
		d_engine = new (nothrow) hs_engine;
#else
		d_err = "No hyperscan support built in. Use Makefile.hs for building.\n";
		return -1;
#endif
	} else
		d_engine = new (nothrow) pcre_engine;

	return d_engine->prepare(config);
}


int FileGrep::compile(const string &regex, uint32_t &min_size)
{
	int r = d_engine->compile(regex, d_minlen);
	min_size = d_minlen;
	return r;
}


enum {
#if (defined __linux__ || defined __APPLE__)
	mmap_flags = MAP_PRIVATE|MAP_NORESERVE
#else
	mmap_flags = MAP_PRIVATE
#endif
};


int FileGrep::find(const char *path, const struct stat *st, int typeflag)
{
	size_t clen = st->st_size;

	// Not needed here anymore due to min_file_size global
	//if ((size_t)d_minlen > clen)
	//	return 0;

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
	const int overlap = 0x1000;	//d_engine->overlap();
	ostringstream str;

	char before[512] = {0}, after[512] = {0};
	int ovector[3] = {0}, rc = -1;

	// no impls yet
	//d_engine->pre_match();

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

		const char *start = content, *end = content + clen;

		for (;start + d_minlen < end;) {

			if ((rc = d_engine->match(content, start, end - start, ovector)) <= 0)
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

			pthread_mutex_lock(&stdout_lock);
			cout<<str.str();
			pthread_mutex_unlock(&stdout_lock);

			str.flush();
			str.clear();
			str.str("");

			if (d_single_match)
				break;
		}
	}

	//d_engine->post_match();

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
		r = find(path.c_str(), &st, G_FTW_F);
	else if (S_ISDIR(st.st_mode))
		cerr<<"Clever boy! Want recursion? Add -R!\n";

	return r;
}


int walk(const char *path, const struct stat *st, int typeflag, void *ftwbuf)
{
	// Since we use our own dedicated nftw() impl, only FTW_F and S_ISREG()
	// files will reach this callback, so no need to check again for it
	if (grep->find(path, st, typeflag) < 0)
		cerr<<path<<": "<<grep->why()<<endl;
	return 0;
}


int FileGrep::find_recursive(const string &path)
{
	d_recursive = 1;
	return nftw_single(path.c_str(), walk, 1024, G_FTW_PHYS);
}


}

