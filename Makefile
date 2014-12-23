#
# grab Makefile
#

CXX=c++
CFLAGS=-Wall -O2
#CFLAGS+=-ansi -pedantic -std=c++11
INC=-I/usr/local/include
LIBS=-L/usr/local/lib
LIBS+=-lpcre

# For OSX (Darwin), just disable the parallel build, as
# it has no cpu_set_t

DEFS=-DBUILD_WITH_PARALLELISM
LIBS+=-pthread

grab: grab.cc
	$(CXX) $(CFLAGS) $(INC) $(DEFS) $(LIBS) $< -o grab

