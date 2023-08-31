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
#include <cstdint>
#include <map>
#include <pcre.h>
#include "engine-pcre.h"


using namespace std;


namespace grab {


pcre_engine::pcre_engine()
{
}


pcre_engine::~pcre_engine()
{
	if (d_extra)
		pcre_free_study(d_extra);
}


int pcre_engine::prepare(const map<string, size_t> &conf)
{
	if (conf.count("literal") > 0) {
		d_err = "pcre_engine::prepare: No literal support in PCRE engine.";
		return -1;
	}
	return 0;
}


int pcre_engine::compile(const string &regex, uint32_t &min)
{
	const char *errptr = nullptr;
	int erroff = 0;

	if ((d_pcreh = pcre_compile(regex.c_str(), 0, &errptr, &erroff, pcre_maketables())) == nullptr) {
		d_err = "pcre_engine::prepare::pcre_compile error";
		return -1;
	}

#ifndef PCRE_STUDY_JIT_COMPILE
#define PCRE_STUDY_JIT_COMPILE 0
#endif

	if ((d_extra = pcre_study(d_pcreh, PCRE_STUDY_JIT_COMPILE, &errptr)) == nullptr) {
		d_err = "pcre_engine::prepare::pcre_study error" ;
		return -1;
	}

	pcre_fullinfo(d_pcreh, d_extra, PCRE_INFO_MINLENGTH, &d_minlen);

	min = d_minlen;

	return 0;
}


int pcre_engine::match(const char *start, const char *match_start, uint64_t len, int ovector[3])
{
	return pcre_exec(d_pcreh, d_extra, match_start, len, 0, 0, ovector, 3);
}

}

