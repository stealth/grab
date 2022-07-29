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

#ifndef spot_filter_h
#define spot_filter_h

#include <string>
#include <sys/types.h>
#include <sys/stat.h>


namespace grab  {

class Filter {

	std::string d_err{""};

	std::string d_name{""};

	uid_t d_uid{0};
	gid_t d_gid{0};
	mode_t d_perm{0}, d_type{S_IFREG};

	off_t d_size{0};

	enum {
		FILTER_NAME		= 0x1,
		FILTER_TYPE		= 0x2,
		FILTER_SIZE		= 0x4,
		FILTER_UID		= 0x8,
		FILTER_GID		= 0x10,
		FILTER_PERM_EXACT	= 0x20,
		FILTER_PERM_ANY		= 0x40,
		FILTER_PERM_MORE	= 0x80,
	};

	uint32_t d_flags{0};

public:

	int find(int, const char *, const char *, const struct stat *, int);

	void add_uid(uid_t);

	void add_gid(gid_t);

	void add_size(off_t);

	void add_perm(const std::string &);

	void add_name(const std::string &);

	void add_type(char);

	const char *why()
	{
		return d_err.c_str();
	}

};

}

#endif

