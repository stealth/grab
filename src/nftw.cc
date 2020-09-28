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
#include <vector>
#include <map>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <ftw.h>
#include <fcntl.h>
#include <unistd.h>
#include <atomic>
#include <sys/types.h>
#include <sys/stat.h>

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <sys/syscall.h>
#include <pthread.h>

#include "nftw.h"


using namespace std;


namespace grab {


// aligned to 64bit for ->data to be passed to
// getdents' dirent struct
struct DIR {
	int32_t fd;
	int32_t align;

	uint64_t size;
	uint64_t offset;

	char data[0x10000 - 3*8];
};

// Removed d_type, so it matches linux_dirent struct and we
// can pass it right away to getdents() syscall
struct dirent {
	ino_t d_ino;
	off_t d_off;
	unsigned short d_reclen;
	char d_name[256];
};

// all the directory handles currently handled at this depth
// So to say, this is our recursion stack, since nftw_once()
// is leaving recursion early after first fn() call and saving the track
// into the vector to be popped from on re-entry.
static vector<DIR *> dirvec;

// We need to keep the directory pathname per DIR, so we can re-establish state upon re-entry
// into nftw_once()
static map<DIR *, string> dirmap;

static atomic<int> finished{0}, inflight{0}, initial{1};

static pthread_mutex_t lck = PTHREAD_MUTEX_INITIALIZER;


static DIR *opendir(const char *path)
{
	DIR *dp = new (nothrow) DIR;
	if (!dp)
		return nullptr;

	memset(dp, 0, sizeof(DIR));

	if ((dp->fd = open(path, O_RDONLY|O_DIRECTORY)) < 0) {
		delete dp;
		return nullptr;
	}

	return dp;
}


static dirent *readdir(DIR *dp)
{
	dirent *de = nullptr;
	ssize_t n = 0;

	do {
		if (dp->offset >= dp->size) {
			if ((n = syscall(SYS_getdents, dp->fd, dp->data, sizeof(dp->data))) <= 0) {
				de = nullptr;
				break;
			}
			dp->size = static_cast<uint64_t>(n);
			dp->offset = 0;
		}

		de = reinterpret_cast<dirent *>(&dp->data[dp->offset]);
		dp->offset += de->d_reclen;

	} while (de->d_ino == 0);

	return de;
}


static int closedir(DIR *dp)
{
	if (dp->fd >= 0) {
		close(dp->fd);
		dp->fd = -1;
	}
	delete dp;
	return 0;
}


static int nftw_once(const char *dir, int (*fn) (const char *fpath, const struct stat *sb, int typeflag, void *ftwbuf), bool recursed)
{
	DIR *dfd = nullptr;
	string pathname = "";
	struct dirent *de = nullptr;
	struct stat lst;

	// retval: 0 -> end of entries, 1 -> can be called once more

	if (finished.load() == 1)
		return 0;

	pthread_mutex_lock(&lck);

	++inflight;

	// The end condition: we are not called from a recursion (but by a thread iteratively)
	// and except ourselfes, there is noone else inside this function. We are also already
	// through at least once (initial == 0) and we have no DIR entries left to check.
	// This end condition is guaranteed to happen: At some point T, all DIRs are read (recursively)
	// and therefore dirvec will be empty at T and no calls with recursed == 1 are made anymore.
	// inflights will descend to the amount of parallel threads at T and will eventually be 1
	// due to above lck and the "--inflight" after the "if (!recursed && initial.load() == 0)" below.
	if (!recursed && inflight.load() == 1 && initial.load() == 0 && dirvec.empty()) {
		--inflight;
		finished.store(1);
		// dirvec already empty
		dirmap.clear();
		pthread_mutex_unlock(&lck);
		return 0;
	}

	if (dirvec.empty() || recursed) {

		// This might happen if we are here iteratively
		// and dirvec has not yet been filled by a recursion (dirvec.empty())
		// of another thread. Do not re-fill dirvec with the initial dir again.
		if (!recursed && initial.load() == 0) {
			--inflight;
			pthread_mutex_unlock(&lck);
			return 1;
		}
		if (dirvec.empty())
			dirvec.reserve(1024);

		if ((dfd = opendir(dir)) == nullptr) {
			--inflight;
			pthread_mutex_unlock(&lck);
			return 1;
		}
		dirvec.push_back(dfd);

		// must use operator[] and can't use insert(), because insert() would not overwrite
		// existing entries, and since we might closedir/opendir/... and therefore re-cycle 'dfd' allocs,
		// and do not erase() map elements when we remove it from dirvec, we would end up with
		// wrong mappings
		dirmap[dfd] = dir;
		pathname = dir;
	} else {
		dfd = dirvec.back();
		pathname = dirmap[dfd];
	}

	// No longer uninited; we filled dirvec at least once at this point
	initial.store(0);

	// lock is still held on entry, and the loop is ment to execute just
	// once in most cases, so take care to lock in the "continue" case
	for (;;) {

		if ((de = readdir(dfd)) == nullptr) {

			// This is thhe only place where its allowed to remove DIRs from dirvec:
			// All entries have been read. dfd if the last entry, so it can just be popped.
			// This is since we continously locked dirvec and no other thread could
			// have pushed or popped something in between.
			dirvec.pop_back();
			closedir(dfd);
			dfd = nullptr;
			pthread_mutex_unlock(&lck);
			break;
		}

		// still locked, no need to lock again in continue
		if (de->d_name[0] == '.' && (de->d_name[1] == 0 || (de->d_name[1] == '.' && de->d_name[2] == 0)))
			continue;

		string d_name = de->d_name;

		pthread_mutex_unlock(&lck);

		string p = pathname;
		if (p[p.size() - 1] != '/')
			p += "/";
		p += d_name;

		if (lstat(p.c_str(), &lst) < 0)
			break;

		// don't follow symlinks into directories
		if (S_ISDIR(lst.st_mode)) {
			nftw_once(p.c_str(), fn, 1);
		} else if (S_ISREG(lst.st_mode)) {
			fn(p.c_str(), &lst, FTW_F, nullptr);
		}
		// ignore symlinks and other files

		break;
	}

	--inflight;
	return 1;
}


// Parallel + racursive nftw() version for multicore. nopenfd is ignored
int nftw_multi(const char *dir, int (*fn) (const char *fpath, const struct stat *sb, int typeflag, void *ftwbuf), int nopenfd, int flags)
{
	return nftw_once(dir, fn, 0);
}


// Single threaded version, no locking required. nopenfd is ignored
int nftw_single(const char *dir, int (*fn) (const char *fpath, const struct stat *sb, int typeflag, void *ftwbuf), int nopenfd, int flags)
{
	DIR *dfd = nullptr;
	struct dirent *de = nullptr;
	struct stat lst;

	if ((dfd = opendir(dir)) == nullptr)
		return -1;

	for (;;) {

		if ((de = readdir(dfd)) == nullptr) {
			closedir(dfd);
			return 0;
		}

		if (de->d_name[0] == '.' && (de->d_name[1] == 0 || (de->d_name[1] == '.' && de->d_name[2] == 0)))
			continue;

		string p = dir;
		if (p[p.size() - 1] != '/')
			p += "/";
		p += de->d_name;

		if (lstat(p.c_str(), &lst) < 0)
			continue;

		// don't follow symlinks into directories
		if (S_ISDIR(lst.st_mode)) {
			nftw_single(p.c_str(), fn, nopenfd, flags);
		} else if (S_ISREG(lst.st_mode)) {
			fn(p.c_str(), &lst, FTW_F, nullptr);
		}
		// ignore symlinks and other files

	}

	return 0;
}



}


