#ifndef nftw_h
#define nftw_h

#include <sys/types.h>
#include <sys/stat.h>

namespace grab {

enum {
	FTW_PHYS	= 1,
	FTW_F		= 0x1000

};

int nftw_multi(const char *, int (*fn) (const char *, const struct stat *, int, void *), int, int);

int nftw_single(const char *, int (*fn) (const char *, const struct stat *, int, void *), int, int);

}

#endif

