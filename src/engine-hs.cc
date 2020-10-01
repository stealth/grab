/*
 * Copyright (C) 2020 Sebastian Krahmer.
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
#include <list>
#include <map>
#include <hs.h>
#include "engine-hs.h"


using namespace std;


namespace grab {


hs_engine::hs_engine()
{
}


hs_engine::~hs_engine()
{
	if (d_scratch)
		hs_free_scratch(d_scratch);
	if (d_db)
		hs_free_database(d_db);
}


int hs_engine::prepare(const map<string, size_t> &conf)
{
	if (conf.count("literal") > 0)
		d_literal = 1;

	return 0;
}


int hs_engine::compile(const string &regex, uint32_t &min)
{

	hs_compile_error_t *e = nullptr;
	hs_expr_info_t *inf = nullptr;
	hs_platform_info_t plat;

	if (!d_literal) {
		if (hs_expression_ext_info(regex.c_str(), 0, nullptr, &inf, &e) != HS_SUCCESS) {
			d_err = "hs_engine::compile::hs_expression_ext_info:";
			d_err += e->message;
			return -1;
		}

		d_minlen = inf->min_width;
		min = d_minlen;
		free(inf);
	} else {
		d_minlen = regex.size();
		min = d_minlen;
	}

	if (hs_populate_platform(&plat) != HS_SUCCESS) {
		d_err = "hs_engine::compile::hs_populate_platform:";
		d_err += e->message;
		return -1;
	}
	if (!d_literal) {
		if (hs_compile(regex.c_str(), HS_FLAG_SOM_LEFTMOST, HS_MODE_BLOCK, &plat, &d_db, &e) != HS_SUCCESS) {
			d_err = "hs_engine::compile::hs_compile:";
			d_err += e->message;
			return -1;
		}
	} else {
		if (hs_compile_lit(regex.c_str(), HS_FLAG_SOM_LEFTMOST, regex.size(), HS_MODE_BLOCK, &plat, &d_db, &e) != HS_SUCCESS) {
			d_err = "hs_engine::compile::hs_compile_lit:";
			d_err += e->message;
			return -1;
		}
	}

	if (hs_alloc_scratch(d_db, &d_scratch) != HS_SUCCESS) {
		d_err = "hs_engine::compile::hs_alloc_scratch:";
		d_err += e->message;
		return -1;
	}
	return 0;
}


thread_local static bool haz_match = 0;


// odd hyperscan API: hs_scan() takes "unsigned int" as len param for the data, but returns
// "unsigned long long" as matching offsets??
static int hs_cb(unsigned int id, unsigned long long from, unsigned long long to, unsigned int flags, void *ctx)
{

	int *ovec = reinterpret_cast<int *>(ctx);
	ovec[0] = static_cast<int>(from);
	ovec[1] = static_cast<int>(to);
	haz_match = 1;

	// 1 indicates "return from scanning"
	return 1;
}


int hs_engine::match(const char *block_start, const char *match_start, uint64_t len, int ovector[3])
{
	haz_match = 0;
	hs_scan(d_db, match_start, static_cast<unsigned int>(len), 0, d_scratch, hs_cb, ovector);
	return haz_match;
}


}

