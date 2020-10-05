#
# greppin Makefile
#

.PHONY: all clean

CXX=c++

CFLAGS=-c -Wall -O3 -pedantic -std=c++11

INC=-I/usr/local/include

LFLAGS=-L/usr/local/lib
LIBS+=-lpcre

DEFS=

ifeq ($(shell uname), NetBSD)
INC+=-I/usr/pkg/include
LFLAGS+=-L/usr/pkg/lib
endif

ifeq ($(shell uname), Darwin)
DEFS+=-D_DARWIN_NO_64_BIT_INODE
else
LIBS+=-pthread
endif

all: greppin

greppin: grab.o main.o nftw.o engine-pcre.o
	$(CXX) grab.o main.o nftw.o engine-pcre.o $(LFLAGS) $(LIBS) -o greppin

main.o: main.cc
	$(CXX) $(CFLAGS) $(INC) $(DEFS) $< -o main.o

nftw.o: nftw.cc
	$(CXX) $(CFLAGS) $(INC) $(DEFS) $< -o nftw.o

grab.o: grab.cc grab.h
	$(CXX) $(CFLAGS) $(INC) $(DEFS) $< -o grab.o

engine-pcre.o: engine-pcre.cc
	$(CXX) $(CFLAGS) $(INC) $(DEFS) $< -o engine-pcre.o

clean:
	rm -f *.o

