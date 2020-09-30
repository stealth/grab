#
# greppin Makefile
#

.PHONY: all clean

DEFS=-DWITH_HYPERSCAN

CXX=c++

CFLAGS=-c -Wall -O3 -pedantic -std=c++11

INC=-I/usr/local/include -I${HYPERSCAN_BUILD}/../src

LIBS=-L/usr/local/lib -L${HYPERSCAN_BUILD}/lib
LIBS+=-lpcre -lpthread -lhs

all: greppin

greppin: grab.o main.o nftw.o engine-pcre.o engine-hs.o
	$(CXX) grab.o main.o nftw.o engine-pcre.o engine-hs.o $(LIBS) -o greppin

main.o: main.cc
	$(CXX) $(CFLAGS) $(INC) $(DEFS) $< -o main.o

nftw.o: nftw.cc
	$(CXX) $(CFLAGS) $(INC) $(DEFS) $< -o nftw.o

grab.o: grab.cc grab.h
	$(CXX) $(CFLAGS) $(INC) $(DEFS) $< -o grab.o

engine-pcre.o: engine-pcre.cc
	$(CXX) $(CFLAGS) $(INC) $(DEFS) $< -o engine-pcre.o

engine-hs.o: engine-hs.cc
	$(CXX) $(CFLAGS) $(INC) $(DEFS) $< -o engine-hs.o

clean:
	rm -f *.o

