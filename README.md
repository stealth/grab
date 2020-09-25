grab - simple, but very fast grep
=================================


![greppin](https://github.com/stealth/grab/blob/greppin/pic/greppin.jpg)


This is my own, experimental, parallel version of _grep_ so I can test
various strategies to speed up access to large directory trees.
On Flash storage or SSDs, you can easily outsmart common greps by up
a factor of 8.

Options:

```

 -O     -- print file offset of match
 -l     -- do not print the matching line (Useful if you want
           to see _all_ offsets; if you also print the line, only
           the first match in the line counts)
 -s     -- single match; dont search file further after first match
           (similar to grep on a binary)
 -L     -- machine has low mem; half chunk-size (default 1GB)
           may be used multiple times
 -I     -- enable highlighting of matches (useful)
 -n <n> -- Use n cores in parallel (recommended for flash/SSD)
           n <= 1 uses single-core
 -r     -- recurse on directory
 -R     -- same as -r

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
provides. However, the `greppin` branch only runs on Linux.

```
$ make
```

or

```
$ git checkout greppin; make clean; make
```


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

A quite new pcre lib is required, on some older systems the build can fail
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

The `greppin` branch features its own parallel version of `nftw()`, so the time
of idling of N - 1 cores when the 1st core builds the directory tree can also
be used for working. Additional to that, since locking is required in the
threads anyway, it also comes with its own faster and lockfree `readdir()` implementation
to save quite some `futex()` calls.

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


What about ripgrep?
-------------------

I was asked several times how _grab_ would compare against [ripgrep](https://github.com/BurntSushi/ripgrep)
so I downloaded their official build `ripgrep-12.1.1-x86_64-unknown-linux-musl.tar.gz`
and run it on the same 4-core SSD machine as in the above tests. _ripgrep_ has
by far more options and compares much better to the traditional _grep_ than _grab_
does. Its written in **Rust** and is also using a parallelized directory iterator
(as I learned from their README). Still, _greppin_ is __faster by a factor of 3__.
Since _ripgrep_ will ignore lot of files (for good reason) when running against
a git repo, I also run it against `/usr`. It can be seen that _greppin_
is still faster, and 'equally fast' when scanning the _entire git repo_ (printing
16918 matches vs. 247) versus _ripgrep_ with skipping a lot of files in between.
Between the runs, the `echo 3 > /proc/sys/vm/drop_caches` has been made in order
to minimize caching effects. Also note that due to JIT engines inside most regex
implementations, memory `rwx` mappings have to be allowed for maximum performance.
On grsec for example, you would need to

```
setfattr -n user.pax.flags -v "m" /path/to/rg
```

Since I was using an untrusted binary download, I didnt run the tests as root
this time, which is perfectly OK. Above tests were run as root for the vm-dropping
shortcut without the need to setup sudo rules. It requires redirecting `stderr` to
`/dev/zero` though, in order to eliminate junk output:

```
source@linux:~$ time rg -a -P linus /source/linux|wc -l
247

real    0m7.123s
user    0m8.092s
sys     0m3.284s
source@linux:~$ time greppin -n 4 -r linus /source/linux|wc -l
16918

real    0m8.756s
user    0m4.319s
sys     0m5.454s
source@lnux:~$ time rg -a -P linus /usr 2>/dev/zero|wc -l
192

real    0m51.108s
user    2m23.315s
sys     0m10.981s
source@linux:~$ time rg -j 4 -a -P linus /usr 2>/dev/zero|wc -l
192

real    0m48.952s
user    2m15.765s
sys     0m10.679s

source@linux:~$ time greppin -n 4 -r linus /usr 2>/dev/zero|wc -l
192

real    0m17.476s
user    0m7.445s
sys     0m10.202s
source@linux:~$
```

When using non-Perl regexes in _ripgrep_ such as with `-a -N -e linus` both runs are
equally fast, but thats an odd comparison, since PCRE's are much more powerful and
potentially slower to match than basic regular expressions.


