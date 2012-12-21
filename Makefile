
LIBS=-lpcre

LIBS+=-lpthread
DEFS=-DBUILD_WITH_PARALLELISM

grab: grab.cc
	$(CXX) -Wall -O2 grab.cc $(DEFS) $(LIBS) -o grab

