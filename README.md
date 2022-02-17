grab - simple, but very fast grep
=================================


![greppin](https://github.com/stealth/grab/blob/greppin/pic/greppin.jpg)


This is my own, experimental, parallel version of _grep_ so I can test
various strategies to speed up access to large directory trees.
On Flash storage or SSDs, you can easily outsmart common greps by up
a factor of 8.

Options:

```
Usage: ./greppin [-rIOLlsSH] [-n <cores>] <regex> <path>

	-2	-- use PCRE2 instead of PCRE
	-O	-- print file offset of match
	-l	-- do not print the matching line (Useful if you want
		   to see _all_ offsets; if you also print the line, only
		   the first match in the line counts)
	-s	-- single match; dont search file further after first match
		   (similar to grep on a binary)
	-H	-- use hyperscan lib for scanning
	-S	-- only for hyperscan: interpret pattern as string literal instead of regex
	-L	-- machine has low mem; half chunk-size (default 2GB)
		   may be used multiple times
	-I	-- enable highlighting of matches (useful)
	-n	-- Use multiple cores in parallel (omit for single core)
	-r	-- recurse on directory
```


_grab_ uses the _pcre_ library, so basically its equivalent to a `grep -P -a`.
The `-P` is important, since Perl-Compatible Regular Expressions have different
characteristics than basic regexes.


Build
-----

There are two branches. `master` and `greppin`. Master is the 'traditional'
*grab* that should compile and run on most POSIX systems. `greppin` comes with
its own optimized and parallelized version of `nftw()` and `readdir()`, which
again doubles speed on the top of speedup that the `master` branch already
provides. The `greppin` branch runs on Linux, BSD and OSX. `greppin` also comes
with support for Intel's [hyperscan](https://www.hyperscan.io) libraries that try
to exploit CPU's SIMD instructions if possible (AVX2, AVX512 etc.) when compiling
the regex pattern into JIT code.

You will most likely want to build the `greppin` branch:

```
$ git checkout greppin
[...]
$ cd src; make
[...]
```

Make sure you have the *pcre* and *pcre2* library packages installed.
On BSD systems you need `gmake` instead of `make`.
If you want to do cutting edge tech with _greppin's_ multiple regex engine and hyperscan
support, you first need to get and build that:

```
$ git clone https://github.com/intel/hyperscan
[...]
$ cd hyperscan
$ mkdir build; cd build
$ cmake -DFAT_RUNTIME=1 -DBUILD_STATIC_AND_SHARED=1 ..
[...]
$ make
[...]
```

This will build so called *fat runtime* of the hyperscan libs which contain support
for all CPU families in order to select the right compilation pattern at runtime
for most performance. Once the build finishes, you build _greppin_ against that:

(inside grab cloned repo)
```
$ cd src
$ HYPERSCAN_BUILD=/path/to/hyperscan/build make -f Makefile.hs
[...]
```

This will produce a `greppin` binary that enables the `-H` option to load
a different engine at runtime, trying to exploit all possible performance bits.

You could link it against already installed libs, but the API just recently
added some functions in the 5.x version and most distros ship with 4.x.


Why is it faster?
-----------------

_grab_ is using `mmap(2)` and matches the whole file blob
without counting newlines (which _grep_ is doing even if there is no match
[as of a grep code review of mine in 2012; things may be different today])
which is a lot faster than `read(2)`-ing the file in small chunks and counting the
newlines. If available, _grab_ also uses the PCRE JIT feature.
However, speedups are only measurable on large file trees or fast HDDs or SSDs.
In the later case, the speedup can be really drastically (up to 3 times faster)
if matching recursively and in parallel. Since storage is the bottleneck,
parallelizing the search on HDDs makes no sense, as the seeking takes more time
than just doing stuff in linear.

Additionally, _grab_ is skipping files which are too small to contain the
regular expression. For larger regex's in a recursive search, this can
skip quite good amount of files without even opening them.

A quite new *pcre* lib is required, on some older systems the build can fail
due to a missing `PCRE_INFO_MINLENGTH` and `pcre_study()`.

Files are mmaped and matched in chunks of 1Gig. For files which are larger,
the last 4096 byte (1 page) of a chunk are overlapped, so that matches on a 1 Gig
boundary can be found. In this case, you see the match doubled (but with the
same offset).

If you measure _grep_ vs. _grab_, keep in mind to drop the dentry and page
caches between each run: `echo 3 > /proc/sys/vm/drop_caches`

Note, that _grep_ will print only a 'Binary file matches', if it detects binary
files, while _grab_ will print all matches, unless `-s` is given. So, for a
speed test you have to search for an expression that *does not* exist in the data,
in order to enforce searching of the entire files.

_grab_ was made to quickly grep through large directory trees without indexing.
The original _grep_ has by far a more complete option-set. The speedup
for a single file match is very small, if at all measureable.

For SSDs, the multicore option makes sense. For HDDs it does not, since
the head has to be positioned back and forth between the threads, potentially
destroying the locality principle and killing performance.

The `greppin` branch features its own lockfree parallel version of `nftw()`, so the time
of idling of N - 1 cores when the 1st core builds the directory tree can also
be used for working.

Whats left to note: _grab_ will traverse directories _physically_, i.e. it will not follow
symlinks.


Examples
--------

This shows the speedup on a 4-core machine with a search on a SSD:


```
root@linux:~# echo 3 > /proc/sys/vm/drop_caches
root@linux:~# time grep -r foobardoesnotexist /source/linux

real	0m34.811s
user	0m3.710s
sys	0m10.936s
root@linux:~# echo 3 > /proc/sys/vm/drop_caches
root@linux:~# time grab -r foobardoesnotexist /source/linux

real	0m31.629s
user	0m4.984s
sys	0m8.690s
root@linux:~# echo 3 > /proc/sys/vm/drop_caches
root@linux:~# time grab -n 2 -r foobardoesnotexist /source/linux

real	0m15.203s
user	0m3.689s
sys	0m4.665s
root@linux:~# echo 3 > /proc/sys/vm/drop_caches
root@linux:~# time grab -n 4 -r foobardoesnotexist /source/linux

real	0m13.135s
user	0m4.023s
sys	0m5.581s
```

With `greppin` branch:

```
root@linux:~# echo 3 > /proc/sys/vm/drop_caches
root@linux:~# time grep -a -P -r linus /source/linux/|wc -l
16918

real    1m12.470s
user    0m49.548s
sys     0m6.162s
root@linux:~# echo 3 > /proc/sys/vm/drop_caches
root@linux:~# time greppin -n 4 -r linus /source/linux/|wc -l
16918

real    0m8.773s
user    0m4.670s
sys     0m5.837s
root@linux:~#
```

Yes! ~ 9s vs. ~ 72s! Thats 8x as fast on a 4-core SSD machine as the traditional grep.

Just to proof that it resulted in the same output:

```
root@linux:~# echo 3 > /proc/sys/vm/drop_caches
root@linux:~# greppin -n 4 -r linus /source/linux/|sort|md5sum
a1f9fe635bd22575a4cce851e79d26a0  -
root@linux:~# echo 3 > /proc/sys/vm/drop_caches
root@linux:~# grep -P -a -r linus /source/linux/|sort|md5sum
a1f9fe635bd22575a4cce851e79d26a0  -
root@linux:~#
```


In the single core comparison, speedup also depends on which CPU the kernel
actually scheduls the _grep_, so a _grab_ may or may not be faster (mostly it is).
If the load is equal among the single-core tests, _grab_ will see a speedup if
searching on large file trees. On multi-core setups, _grab_ can benefit ofcorse.


ripgrep comparison
------------------

The project can be found [here](https://github.com/BurntSushi/ripgrep).

The main speedup thats inside their benchmark tables stems from the fact that _ripgrep_
ignores a lot of files (notably  dotfiles) when invoked without special options as well
as treating binary files as a single-match target (similar to _grep_). In order to have
comparable results, keep in mind to (4 is the number of cores):

* `echo 3 > /proc/sys/vm/drop_caches` between each run
* Add `-j 4 -a --no-unicode --no-pcre2-unicode -uuu --mmap` to _ripgrep_, since
  it will by default match Unicode which is 3 times slower, and tries to compensate
  the speedloss by skipping 'ignore'-based files. `-e` is faster than `-P`,
  so better choose `-e`, but thats not as powerful as a PCRE
* redirect the output to `/dev/null` to avoid tty based effects
* add `-H -n 4` to _greppin_ if you want best performance. `-H` is PCRE compatible
  with only very few exceptions (according to hyperscan docu)
* `setfattr -n user.pax.flags -v "m" /path/to/binary` if you run on grsec systems
  and require rwx JIT mappings

Then just go ahead and check the timings. Even when not using hyperscan, `greppin`
is significantly faster than `rg` when using PCRE2 expressions (PCRE2 vs. PCRE2)
and still faster when comparing the fastest expressions (-e vs. hyperscan).

