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
#include <cstring>
#include <vector>
#include <map>
#include <cstdio>
#include <cstdint>
#include <fcntl.h>
#include <unistd.h>
#include <atomic>
#include <sys/types.h>
#include <sys/stat.h>

#include <sys/syscall.h>

#ifdef __APPLE__
#include <sys/dirent.h>
#endif

#include "nftw.h"


using namespace std;


extern uint32_t min_file_size;


namespace grab {


dir_cache *dirvec = nullptr;

// OSX requires to save seek offsets across getdirentries() calls
#ifdef __APPLE__
static map<int, long> fd2seek;
#endif

static struct {
	alignas(64) atomic<int> finished{0};
	alignas(64) atomic<int> inflight{0};
	alignas(64) atomic<int> inited{0};
	alignas(64) atomic<int> first{1};
} atomics;


static int getdents(int fd, char *buf, int nbytes)
{
#ifdef __linux__
	return syscall(SYS_getdents, fd, buf, nbytes);

#elif (defined __FreeBSD__ || defined __NetBSD__ || defined __OpenBSD__)
	return ::getdents(fd, buf, nbytes);

#elif defined __APPLE__

	// getdents() is called by readdir() which is called when lck
	// is held, so access to this map is safe.
	long seek = fd2seek[fd];

#ifdef _DARWIN_FEATURE_64_BIT_INODE
	int r = syscall(SYS_getdirentries64, fd, buf, nbytes, &seek);
#else
	int r = syscall(SYS_getdirentries, fd, buf, nbytes, &seek);
#endif
	fd2seek[fd] = seek;

	return r;
#endif
}


static inline DIR *opendir(const char *path)
{
	DIR *dp = new (nothrow) DIR;
	if (!dp)
		return nullptr;

	// no memzero of ->data necessary: ->data is filled
	// by getdents() and the d_name[] array is guaranteed
	// to be NUL-terminated by the syscall. Other members are initialized
	// in DIR constructor.

	if ((dp->fd = open(path, O_RDONLY|O_DIRECTORY)) < 0) {
		delete dp;
		return nullptr;
	}

	dp->dirname = strdup(path);

#ifdef __APPLE__

	// Must use operator[] to overwrite potential existing seek offsets
	// after a close()/open() cycle. Access to global map is safe, because
	// caller of opendir() holds lck.
	fd2seek[dp->fd.load()] = 0;
#endif

	// Atomic insert. the `use` counter is already been set to 1 by the DIR constructor
	if (dirvec)
		dirvec->insert(dp);

	return dp;
}


static inline dirent *readdir(DIR *dp)
{
	dirent *de = nullptr;
	size_t offset = 0, lck_offset = static_cast<size_t>(-1);

	do {
		while ((offset = dp->offset.exchange(lck_offset)) == lck_offset)
			;

		if (offset >= dp->size) {

			if (dp->finished) {
				dp->offset.store(offset);
				break;
			}

			if ((dp->size = static_cast<uint64_t>(getdents(dp->fd, dp->data, sizeof(dp->data) - 1))) <= 0) {
				dp->size = 0;
				dp->finished = 1;
				dp->offset.store(0);
				break;
			}
			offset = 0;

			// if less data was read than there is space for one more dirent,
			// it could only mean that we read all entries of that DIR
			if (dp->size < (sizeof(dp->data) - sizeof(dirent)))
				dp->finished = 1;
		}

		de = reinterpret_cast<dirent *>(&dp->data[offset]);
		dp->offset.store(offset + de->d_reclen);

	} while (de->d_ino == 0);

	return de;
}


static inline int closedir(DIR *dp)
{
	// might race with other closedir() of same DIR*, so make erase() atomic and once
	if (dp->erase_lck.exchange(1) == 0) {
		if (dirvec)
			dirvec->erase(dp->fd.load());
	}

	// only the last one on this DIR* may close() and free resources
	if (dp->use.fetch_sub(1) == 1) {
		close(dp->fd.load());
		free(dp->dirname);
		delete dp;
	}

	return 0;
}


static inline void abs_path(char *dst, size_t dstlen, const char *dir, const char *basename)
{
	while (--dstlen > 2 && *dir)
		*dst++ = *dir++;
	*dst++ = '/'; --dstlen;
	while (--dstlen > 2 && *basename)
		*dst++ = *basename++;
	*dst = 0;
}


static int nftw_once(const char *dir, int (*fn) (int dfd, const char *dirname, const char *basename, const struct stat *sb, int typeflag, void *ftwbuf), bool recursed)
{
	DIR *dp = nullptr;
	struct dirent *de = nullptr;
	struct stat lst;


	// retval: 0 -> end of entries, 1 -> can be called once more

	if (atomics.finished)
		return 0;

	if (dirvec->empty() || recursed) {

		if (!recursed) {

			if (atomics.inflight == 0 && atomics.inited == 1) {
				atomics.finished = 1;
				return 0;
			}

			// This might happen if we are here iteratively
			// and dirvec has not yet been filled by a recursion (dirvec.empty())
			// of another thread. Do not re-fill dirvec with the initial dir again.
			if (atomics.first.exchange(0) == 0)
				return 1;
		}

		++atomics.inflight;

		if ((dp = opendir(dir)) == nullptr) {
			--atomics.inflight;
			return -1;
		}

		atomics.inited = 1;

		// dp is emplaced on the back of dir_vec by opendir() - no store necessary.
		// it will have ref_cnt of 1, for the storage in dir_cache
		//dirvec.emplace_back(dp);

	} else {

		++atomics.inflight;

		if ((dp = dirvec->fetch1()) == nullptr) {
			--atomics.inflight;
			return 1;
		}
	}

	for (;;) {

		if ((de = readdir(dp)) == nullptr)
			break;

		if (de->d_name[0] == '.' && (de->d_name[1] == 0 || (de->d_name[1] == '.' && de->d_name[2] == 0)))
			continue;

		if (fstatat(dp->fd.load(), de->d_name, &lst, AT_SYMLINK_NOFOLLOW) < 0)
			continue;

		// don't follow symlinks into directories
		if (S_ISDIR(lst.st_mode)) {
			char fullp[4096];

			// double slashes in pathnames do not matter (in case initial dir had prepended /)
			abs_path(fullp, sizeof(fullp), dp->dirname, de->d_name);
			nftw_once(fullp, fn, 1);
		} else if (S_ISREG(lst.st_mode)) {
			if (!min_file_size || lst.st_size >= min_file_size)
				fn(dp->fd.load(), dp->dirname, de->d_name, &lst, G_FTW_F, nullptr);
		}
		// ignore symlinks and other files
	}

	closedir(dp);

	--atomics.inflight;
	return 1;
}


// Parallel + racursive nftw() version for multicore. nopenfd is ignored
int nftw_multi(const char *dir, int (*fn)(int dfd, const char *dirname, const char *basename, const struct stat *sb, int typeflag, void *ftwbuf), int nopenfd, int flags)
{
	return nftw_once(dir, fn, 0);
}


// Single threaded version, no locking required. nopenfd is ignored
int nftw_single(const char *dir, int (*fn)(int dfd, const char *dirname, const char *basename, const struct stat *sb, int typeflag, void *ftwbuf), int nopenfd, int flags)
{
	DIR *dp = nullptr;
	struct dirent *de = nullptr;
	struct stat lst;

	if ((dp = opendir(dir)) == nullptr)
		return -1;

	for (;;) {

		if ((de = readdir(dp)) == nullptr) {
			closedir(dp);
			return 0;
		}

		if (de->d_name[0] == '.' && (de->d_name[1] == 0 || (de->d_name[1] == '.' && de->d_name[2] == 0)))
			continue;

		if (fstatat(dp->fd.load(), de->d_name, &lst, AT_SYMLINK_NOFOLLOW) < 0)
			continue;

		// don't follow symlinks into directories
		if (S_ISDIR(lst.st_mode)) {
			char fullp[4096];

			abs_path(fullp, sizeof(fullp), dir, de->d_name);
			nftw_single(fullp, fn, nopenfd, flags);
		} else if (S_ISREG(lst.st_mode)) {
			if (!min_file_size || lst.st_size >= min_file_size)
				fn(dp->fd.load(), dir, de->d_name, &lst, G_FTW_F, nullptr);
		}
		// ignore symlinks and other files
	}

	return 0;
}


}


