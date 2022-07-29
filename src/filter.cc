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
#include <cstring>
#include <cstdio>
#include <fnmatch.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "filter.h"

namespace grab  {

using namespace std;


int Filter::find(int dfd, const char *dirname, const char *basename, const struct stat *st, int flags)
{

	if (d_flags & FILTER_UID) {
		if (st->st_uid != d_uid)
			return 0;
	}

	if (d_flags & FILTER_GID) {
		if (st->st_uid != d_gid)
			return 0;
	}

	if (d_flags & FILTER_TYPE) {
		if ((st->st_mode & S_IFMT) != d_type)
			return 0;
	}

	if (d_flags & FILTER_PERM_EXACT) {
		if ((st->st_mode & ~S_IFMT) != d_perm)
			return 0;
	}

	if (d_flags & FILTER_PERM_ANY) {
		if (!(((st->st_mode & ~S_IFMT) & d_perm) != 0))
			return 0;
	}

	if (d_flags & FILTER_PERM_MORE) {
		if (!(((st->st_mode & ~S_IFMT) & d_perm) == d_perm))
			return 0;
	}

	if (d_flags & FILTER_SIZE) {
		if (st->st_size < d_size)
			return 0;
	}

	if (d_flags & FILTER_NAME) {
		if (fnmatch(d_name.c_str(), basename, 0) != 0)
			return 0;
	}

	return 1;
}


void Filter::add_uid(uid_t u)
{
	d_uid = u;
	d_flags |= FILTER_UID;
}


void Filter::add_gid(gid_t g)
{
	d_gid = g;
	d_flags |= FILTER_GID;
}


void Filter::add_size(off_t s)
{
	d_size = s;
	d_flags |= FILTER_SIZE;
}


void Filter::add_perm(const string &p)
{
	if (p.empty())
		return;

	int idx = 0;
	if (p[0] == '/') {
		idx = 1;
		d_flags |= FILTER_PERM_ANY;
	} else if (p[0] == '-') {
		idx = 1;
		d_flags |= FILTER_PERM_MORE;
	} else
		d_flags |= FILTER_PERM_EXACT;

	sscanf(p.c_str() + idx, "%od", &d_perm);
}


void Filter::add_name(const string &n)
{
	d_name = n;
	d_flags |= FILTER_NAME;
}


void Filter::add_type(char c)
{

	switch (c) {
	case 'b':
		d_type = S_IFBLK;
		break;
	case 'c':
		d_type = S_IFCHR;
		break;
	case 'd':
		d_type = S_IFDIR;
		break;
	case 'p':
		d_type = S_IFIFO;
		break;
	case 'f':
		d_type = S_IFREG;
		break;
	case 'l':
		d_type = S_IFLNK;
		break;
	case 's':
		d_type = S_IFSOCK;
		break;
	default:
		return;
	}

	d_flags |= FILTER_TYPE;
}


}

