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

#ifndef grab_h
#define grab_h

#include <cstdint>
#include <string>
#include <sstream>
#include "engine.h"


namespace grab {

class FileGrep {

	std::string d_err{""};
	static std::string start_inv, stop_inv;

	std::ostringstream d_ostr;

	uint32_t d_minlen{1};

	size_t d_chunk_size{1UL<<31};

	re_engine *d_engine{nullptr};

	uid_t d_my_uid{0};

	bool d_print_line{1}, d_print_offset{0}, d_recursive{0}, d_colored{0}, d_print_path{0},
	     d_single_match{0}, d_low_mem{0};
public:

	FileGrep();

	~FileGrep();

	const char *why()
	{
		if (!d_engine || !d_err.empty())
			return d_err.c_str();
		return d_engine->why();
	}

	void recurse()
	{
		d_recursive = 1;
	}

	void show_path(bool b)
	{
		d_print_path = b;
	}

	void flush_ostream();

	int compile(const std::string &, uint32_t &);

	int config(const std::map<std::string, size_t> &);

	int find(const std::string &);

	int find(int, const char *, const char *, const struct stat *, int);

	int find_recursive(const std::string &);
};

}

#endif

