grab - a simple but very fast grep implementation
=================================================

This is my own, experimental, version of _grep_ so I can test
various strategies to speedy access large directory trees.
On SSD's you can smart out common greps by up to 100%.

Options:

```

 -O     -- print file offset of match
 -l     -- do not print the matching line (Useful if you want
           to see _all_ offsets; if you also print the line, only
           the first match in the line counts)
 -I     -- enable highlighting of matches
 -c <n> -- Use n cores in parallel (useless and even slower in most situations)
           n <= 1 uses single-core
 -r     -- recurse on directory
 -R     -- same as -r

```


grab uses the _pcre_ library, so basically its equivalent to a `grep -P -a`


Why is it faster?
-----------------

_grab_ is using `mmap(2)` with `MAP_POPULATE` and matches the whole file blob
without counting newlines (which _grep_ is doing even if there is no match)
which is a lot faster than reading the file in chunks and counting the
newlines. If available, _grab_ also uses the PCRE JIT feature.
However, speedups are only measurable on fast HDD's or SSD's. In the later
case, the speedup can be really drastically (even up to 100%) if matching
recursively. So clearly, the storage is the bottleneck, and parallelizing
the search is in most cases even slower, as the seeking takes more time
than just doing stuff in linear; even on SSD's.

Additionally, grab is skipping files which are too small to contain the
regular expression. For larger regex's in a recursive search, this can
skip quite good amount of files without even opening them.

A quite new pcre lib is required, on some older systems the build can fail
due to `PCRE_INFO_MINLENGTH` and `pcre_study()`.

Files are mmaped and matched in chunks of 1Gig. For files which are larger,
the last 4096 byte (1 page) of a chunk are overlapped, so that matches on a 1 Gig
boundary can be found. In this case, you see the match doubled (but with the
same offset).

If you measure _grep_ vs. _grab_, keep in mind to drop the dentry and page
caches between each run: `echo 3 > /proc/sys/vm/drop_caches`

_grab_ was made to quickly grep through large directory trees. The original grep
has by far a more complete option-set. _grab_ is therefore not pipe-able; the speedup
for a single file match is very small, if at all (stdin cannot be
mmapped and I am too lazy to add a pread() workaround just for this
useless case)


