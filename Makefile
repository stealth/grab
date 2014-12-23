
LIBS=-lpcre

LIBS+=-pthread

# For OSX (Darwin), just disable the parallel build, as
# it has no cpu_set_t

DEFS=-DBUILD_WITH_PARALLELISM

grab: grab.cc
	$(CXX) -Wall -O2 grab.cc $(DEFS) $(LIBS) -o grab

