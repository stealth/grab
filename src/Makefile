#
# greppin Makefile
#

.PHONY: all clean

CXX=c++

CFLAGS=-c -Wall -O3 -pedantic -std=c++11

INC=-I/usr/local/include

LFLAGS=-L/usr/local/lib
LIBS+=-lpcre -lpcre2-8
PLIBS=

DEFS=-DPCRE2_CODE_UNIT_WIDTH=8

ifeq ($(shell uname), NetBSD)
INC+=-I/usr/pkg/include
LFLAGS+=-L/usr/pkg/lib
endif

ifeq ($(shell uname), Darwin)
DEFS+=
else
PLIBS=-pthread
endif

all: greppin spot

greppin: grab.o main.o nftw.o engine-pcre.o engine-pcre2.o
	$(CXX) grab.o main.o nftw.o engine-pcre.o engine-pcre2.o $(LFLAGS) $(LIBS) $(PLIBS) -o greppin

spot: spot.o filter.o nftw.o
	$(CXX) spot.o nftw.o filter.o $(LFLAGS) $(PLIBS) -o spot

main.o: main.cc
	$(CXX) $(CFLAGS) $(INC) $(DEFS) $< -o main.o

filter.o: filter.cc
	$(CXX) $(CFLAGS) $(INC) $< -o filter.o

spot.o: spot.cc
	$(CXX) $(CFLAGS) $(INC) $< -o spot.o

nftw.o: nftw.cc
	$(CXX) $(CFLAGS) $(INC) $(DEFS) $< -o nftw.o

grab.o: grab.cc grab.h
	$(CXX) $(CFLAGS) $(INC) $(DEFS) $< -o grab.o

engine-pcre.o: engine-pcre.cc
	$(CXX) $(CFLAGS) $(INC) $(DEFS) $< -o engine-pcre.o

engine-pcre2.o: engine-pcre2.cc
	$(CXX) $(CFLAGS) $(INC) $(DEFS) $< -o engine-pcre2.o

clean:
	rm -f *.o

