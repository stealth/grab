#ifndef nftw_h
#define nftw_h

#include <sys/types.h>
#include <sys/stat.h>

namespace grab {

int t_nftw(const char *, int (*fn) (const char *, const struct stat *, int, void *), int, int);

}

#endif

