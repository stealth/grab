#ifndef nftw_h
#define nftw_h

#include <stdint.h>
#include <sys/types.h>
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
	int32_t fd;
	int32_t align;

	uint64_t size;
	uint64_t offset;

	char data[0x10000 - 3*8];
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
	char d_name[MAXNAMELEN + 1];
};

#elif (defined __OpenBSD__ || defined __NetBSD__)

struct dirent {
	uint32_t d_ino;			// d_fileno
	off_t d_off;
	uint16_t d_reclen;
	uint8_t d_type, d_namelen;
	char d_name[MAXNAMELEN + 1];
};


#elif defined __APPLE__

#ifdef _DARWIN_FEATURE_64_BIT_INODE

struct dirent {
	ino_t d_ino;			// d_fileno
	uint64_t d_seekof;
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


int nftw_multi(const char *, int (*fn) (const char *, const struct stat *, int, void *), int, int);

int nftw_single(const char *, int (*fn) (const char *, const struct stat *, int, void *), int, int);

}

#endif

