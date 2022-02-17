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

#ifndef nftw_h
#define nftw_h

#include <stdint.h>
#include <sys/types.h>
#include <atomic>
#include <vector>
#include <sys/stat.h>


#if (defined __FreeBSD__ || defined __OpenBSD__ || defined __NetBSD__ || defined __APPLE__)
#include <dirent.h>
#endif


namespace grab {

enum {
	G_FTW_PHYS	= 1,
	G_FTW_F		= 0x1000
};


// aligned to 64bit for ->data to be passed to
// getdents' dirent struct
struct DIR {
	std::atomic<int32_t> fd{-1}, use{1}, erase_lck{0};
	std::atomic<uint64_t> size{0}, offset{0};

	char *dirname{nullptr};

	std::atomic<char> finished{0};

	char data[0x30000 - (3*sizeof(std::atomic<int32_t>) +
	                     2*sizeof(std::atomic<uint64_t>) +
	                     sizeof(char *) +
	                     sizeof(std::atomic<char>))];

	// avoid default initialization by compiler, to not have costly
	// memzero of ->data member, which is unnecessary
	DIR() {}

	~DIR() {}
};


// The dirent structs are directly returned by our readdir() impl,
// but they are made to exactly fit what getdents(2) would return,
// so they can be passed right away. For this reason, some member entries
// are not defined sometimes, even if the original POSIX readdir(3)
// would require different struct members. IOW, this is not a POSIX
// impl of readdir().

#ifdef __linux__

// Removed d_type, so it matches linux_dirent struct and we
// can pass it right away to getdents() syscall
struct dirent {
	ino_t d_ino;
	off_t d_off;
	unsigned short d_reclen;
	char d_name[1024];
};

#elif defined __FreeBSD__

struct dirent {
	uint32_t d_ino;			// d_fileno
	uint16_t d_reclen;
	uint8_t d_type, d_namelen;
	char d_name[1024 + 1];
};

#elif defined __OpenBSD__

struct dirent {
	ino_t d_ino;			// d_fileno
	off_t d_off;
	uint16_t d_reclen;
	uint8_t d_type, d_namelen;
	char d_name[1024 + 1];
};


#elif defined __NetBSD__

struct dirent {
	ino_t d_ino;			// d_fileno
	uint16_t d_reclen, d_namelen;
	uint8_t d_type;
	char d_name[1024 + 1];
};


#elif defined __APPLE__

#ifdef _DARWIN_FEATURE_64_BIT_INODE

struct dirent {
	ino_t d_ino;			// d_fileno
	uint64_t d_seekoff;
	uint16_t d_reclen, d_namelen;
	uint8_t d_type;
	char d_name[1024];
};

#else
struct dirent {
	ino_t d_ino;
	uint16_t d_reclen;
	uint8_t d_type, d_namelen;
	char d_name[256];
};

#endif

#endif

class dir_cache {

	std::vector<std::atomic<DIR *>> d_vec;

	std::atomic<int32_t> d_max_fd{0}, d_entries{0}, d_cur_idx{0};

public:

	dir_cache(int nfd) : d_vec(nfd + 1)
	{
		d_max_fd.store(nfd);
	}

	virtual ~dir_cache()
	{
	}

	dir_cache(const dir_cache &) = delete;

	dir_cache &operator=(const dir_cache &) = delete;

	void insert(DIR *dp)
	{
		int fd = dp->fd.load();
		d_vec[fd].store(dp);
		d_cur_idx.store(fd);
//		if (fd > d_max_fd.load())
//			d_max_fd.store(fd);
		d_entries.fetch_add(1);
	}

	DIR *fetch1()
	{
		DIR *dp = nullptr;

		for (int i = d_cur_idx.load(); d_entries.load() > 0;) {
			i = i % (d_max_fd.load() + 1);
			if ((dp = d_vec[i].exchange(nullptr)) == nullptr) {
				++i;
				continue;
			}
			// We might fetched an DIR* that is erased from d_vec soon after, but this
			// doesn't matter, as this will have erase_lck set then (and won't be erased or appear in d_vec again) as well
			// as the finished bit set, so readdir() on it will immediately return nullptr. We leave
			// this logic to the nftw() loop.

			dp->use.fetch_add(1);
			d_vec[i].store(dp);
			d_cur_idx.store(i);
			break;
		}
		return dp;
	}

	void erase(int fd)
	{
		// Can only have been called exclusively on fd by closedir() with fd still open,
		// so this does not race with other erase() calls on the same fd, only with fetch1().
		// We assume that a DIR* with that fd has once been stored in d_vec.
		// If this loops, someone is on fetch1() on this fd and will soon replace it by DIR* again.
		while (d_vec[fd].exchange(nullptr) == nullptr)
			;
		d_entries.fetch_sub(1);
	}

	bool empty()
	{
		return d_entries == 0;
	}

};


int nftw_multi(const char *, int (*fn)(int, const char *, const char *, const struct stat *, int, void *), int, int);

int nftw_single(const char *, int (*fn)(int, const char *, const char *, const struct stat *, int, void *), int, int);

extern dir_cache *dirvec;

}

#endif

