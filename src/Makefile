#
# grab Makefile
#

.PHONY: all clean

CXX=c++

CFLAGS=-c -Wall -O2 -pedantic -std=c++11

INC=-I/usr/local/include

LIBS=-L/usr/local/lib
LIBS+=-lpcre

# For OSX (Darwin), just disable the parallel build, as
# it has no cpu_set_t

DEFS=-DBUILD_WITH_PARALLELISM
LIBS+=-pthread

all: grab

grab: grab.o main.o
	$(CXX) $(LIBS) grab.o main.o -o grab

main.o: main.cc
	$(CXX) $(CFLAGS) $(INC) $(DEFS) $< -o main.o

grab.o: grab.cc grab.h
	$(CXX) $(CFLAGS) $(INC) $(DEFS) $< -o grab.o

clean:
	rm -f *.o

