CFLAGS+=-pthread -DBUILD_WITH_PARALLELISM
LDFLAGS+=-pthread -lpcre

grab: grab.cc

