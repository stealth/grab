/*
 * Copyright (C) 2020-2022 Sebastian Krahmer.
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
#include <pcre2.h>
#include "engine-pcre2.h"


using namespace std;


namespace grab {


pcre2_engine::pcre2_engine()
{
}


pcre2_engine::~pcre2_engine()
{
	if (d_pcre_match)
		pcre2_match_data_free(d_pcre_match);
	if (d_pcreh)
		pcre2_code_free(d_pcreh);
}


int pcre2_engine::prepare(const map<string, size_t> &conf)
{
	if (conf.count("literal") > 0) {
		d_err = "pcre2_engine::prepare: No literal support in PCRE2 engine.";
		return -1;
	}
	return 0;
}


int pcre2_engine::compile(const string &regex, uint32_t &min)
{
	PCRE2_SIZE erroff = 0;
	int errcode = 0;

	if ((d_pcreh = pcre2_compile(reinterpret_cast<PCRE2_SPTR>(regex.c_str()),
	                             PCRE2_ZERO_TERMINATED|PCRE2_NEVER_UCP|PCRE2_NEVER_UTF|PCRE2_NO_AUTO_CAPTURE,
	                             0, &errcode, &erroff, nullptr)) == nullptr) {
		d_err = "pcre2_engine::prepare::pcre2_compile error";
		return -1;
	}
	if (pcre2_jit_compile(d_pcreh, PCRE2_JIT_COMPLETE) < 0) {
		d_err = "pcre2_engine::prepare::pcre2_jit_compile error";
		return -1;
	}

	if ((d_pcre_match = pcre2_match_data_create(1, nullptr)) == nullptr) {
		d_err = "pcre2_engine::prepare::pcre2_match_data_create error";
		return -1;
	}

	pcre2_pattern_info(d_pcreh, PCRE2_INFO_MINLENGTH, &d_minlen);
	min = d_minlen;

	return 0;
}


int pcre2_engine::match(const char *start, const char *match_start, uint64_t len, int ovector[3])
{
	int n = 0;
	if ((n = pcre2_jit_match(d_pcreh, reinterpret_cast<PCRE2_SPTR>(match_start), len, 0, 0, d_pcre_match, nullptr)) < 0) {
		if (n == PCRE2_ERROR_NOMATCH)
			return 0;
		char ebuf[256] = {0};
		pcre2_get_error_message(n, reinterpret_cast<PCRE2_UCHAR*>(ebuf), sizeof(ebuf) - 1);
		d_err = "pcre2_engine::match::pcre2_jit_match:";
		d_err += ebuf;
		return -1;
	}

	if (auto ovec = pcre2_get_ovector_pointer(d_pcre_match)) {
		ovector[0] = static_cast<int>(ovec[0]);
		ovector[1] = static_cast<int>(ovec[1]);
		return 1;
	}

	return 0;
}

}

