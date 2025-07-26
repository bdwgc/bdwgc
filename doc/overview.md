[Interface Overview](gcinterface.md) | [Tutorial Slides](http://www.hboehm.info/gc/04tutorial.pdf) | [FAQ](http://www.hboehm.info/gc/faq.html) | [Example](simple_example.md) | [Download](https://github.com/bdwgc/bdwgc/wiki/Download) | [License](http://www.hboehm.info/gc/license.txt)
---|---|---|---|---|---

# A garbage collector for C and C++

  * Platforms
  * Some collector details
  * Further reading
  * Information provided on the BDWGC site
  * More background information
  * Contacts and new release announcements

[ This is an updated version of the page formerly at
`www.hpl.hp.com/personal/Hans_Boehm/gc/`, before that at
`http://reality.sgi.com/boehm/gc.html` and before that at
`ftp://ftp.parc.xerox.com/pub/gc/gc.html`. ]

The
[Boehm](http://www.hboehm.info)-[Demers](http://www.cs.cornell.edu/annual_report/00-01/bios.htm#demers)-[Weiser](https://en.wikipedia.org/wiki/Mark_Weiser)
conservative Garbage Collector (**BDWGC**) can be used as a garbage collecting
replacement for C `malloc` or C++ `new`. It allows you to allocate memory
basically as you normally would, without explicitly deallocating memory that
is no longer useful. The collector automatically recycles memory when
it determines that it can no longer be otherwise accessed. A simple example
of such a use is given [here](simple_example.md).

The collector is also used by a number of programming language implementations
that either use C as intermediate code, want to facilitate easier
interoperation with C libraries, or just prefer the simple collector
interface. For a more detailed description of the interface, see
[here](gcinterface.md).

Alternatively, the garbage collector may be used as a [leak detector](leak.md)
for C or C++ programs, though that is not its primary goal.

Typically several versions are offered for
[downloading](https://github.com/bdwgc/bdwgc/wiki/Download): preview, stable,
legacy. Usually you should use the one marked as the _latest stable_ release.
Preview versions may contain additional features, platform support, but are
likely to be less well tested. The list of changes for each version
is specified on the [releases](https://github.com/bdwgc/bdwgc/releases) page.
The development version (snapshot) is available in the master branch of
[bdwgc git](https://github.com/bdwgc/bdwgc) repository on GitHub.

The arguments for and against conservative garbage collection in C and C++ are
briefly discussed [here](http://www.hboehm.info/gc/issues.html). The
beginnings of a frequently-asked-questions list are
[here](http://www.hboehm.info/gc/faq.html).

The garbage collector code is copyrighted by
[Hans-J. Boehm](http://www.hboehm.info), Alan J. Demers,
[Xerox Corporation](https://en.wikipedia.org/wiki/Xerox),
[Silicon Graphics](https://en.wikipedia.org/wiki/Silicon_Graphics),
[Hewlett-Packard Company](http://www.hp.com/),
[Ivan Maidanski](https://github.com/ivmai), and partially by some others.
It may be used and copied without payment of a fee under minimal restrictions.
See the README.md file in the distribution or the
[license](http://www.hboehm.info/gc/license.txt) for more details.
**IT IS PROVIDED AS IS, WITH ABSOLUTELY NO WARRANTY EXPRESSED OR IMPLIED.
ANY USE IS AT YOUR OWN RISK.**

Empirically, this collector works with most unmodified C programs, simply
by replacing `malloc` and `calloc` with `GC_malloc` calls, replacing `realloc`
with `GC_realloc` calls, and removing `free` calls. Exceptions are discussed
[here](http://www.hboehm.info/gc/issues.html).

## Platforms

The collector is not completely portable, but the distribution includes ports
to most standard PC and UNIX/Linux platforms. The collector should work
on Linux, Android, BSD variants, OS/2, Windows (Win32 and Win64), MacOS X,
iOS, HP/UX, Solaris, Tru64, Irix, Symbian and other operating systems. Some
platforms are more polished (better supported) than others.

Irix pthreads, Linux threads, Windows threads, Solaris threads (pthreads
only), HP/UX 11 pthreads, Tru64 pthreads, and MacOS X threads are supported.

## Some Collector Details

The collector uses a [mark-sweep](http://www.hboehm.info/gc/complexity.html)
algorithm. It provides incremental and generational collection under operating
systems which provide the right kind of virtual memory support. (Currently
this includes SunOS[45], IRIX, OSF/1, Linux, and Windows, with varying
restrictions.) It allows [finalization](finalization.md) code to be invoked
when an object is collected. It can take advantage of type information
to locate pointers if such information is provided, but it is usually used
without such information. See the README and `gc.h` files in the distribution
for more details.

For an overview of the implementation, see [here](gcdescr.md).

The garbage collector distribution includes a C string (`cord.h`) package that
provides for fast concatenation and substring operations on long strings.
A simple curses- and Windows-based editor that represents the entire file as
a cord is included as a sample application.

Performance of the non-incremental collector is typically competitive with
`malloc`/`free` implementations. Both space and time overhead are likely to be
only slightly higher for programs written for `malloc`/`free` (see Detlefs,
Dosser and Zorn's
[Memory Allocation Costs in Large C and C++ Programs](https://www.semanticscholar.org/paper/Memory-allocation-costs-in-large-C-and-C%2B%2B-programs-Detlefs-Dosser/49b2b2cec4ce52493c031d964fb3be31d1e02b77)).
For programs allocating primarily very small objects, the collector may be
faster; for programs allocating primarily large objects it will be slower.
If the collector is used in a multi-threaded environment and configured for
thread-local allocation, it may in some cases significantly outperform
`malloc`/`free` allocation in time.

We also expect that in many cases any additional overhead will be more than
compensated for by e.g. decreased copying if programs are written and tuned
for garbage collection.

## Further reading

**The beginnings of a frequently asked questions list for this collector are
[here](http://www.hboehm.info/gc/faq.html).**

**The following provide information on garbage collection in general:**

Paul Wilson's
[garbage collection ftp archive](ftp://ftp.cs.utexas.edu/pub/garbage)
and
[GC survey](https://ftpmirror.infania.net/sites/ftp.leo.org/historic/doc/programming/gcsurvey.ps.Z).

The Ravenbrook
[Memory Management Reference](http://www.memorymanagement.org/).

David Chase's [GC FAQ](http://www.iecc.com/gclist/GC-faq.html).

Richard Jones'
[Garbage Collection page](https://www.cs.kent.ac.uk/people/staff/rej/gc.html)
and his [book](http://www.cs.kent.ac.uk/people/staff/rej/gcbook/gcbook.html)
mentioned on the page.

**The following papers describe the collector algorithms we use and the
underlying design decisions at a higher level:**

(Some of the lower level details can be found [here](gcdescr.md).)

The first one is not available electronically due to copyright considerations.
Most of the others are subject to ACM copyright.

Boehm, H., Dynamic Memory Allocation and Garbage Collection,
_Computers in Physics 9_, 3, May/June 1995, pp. 297-303. This is directed
at an otherwise sophisticated audience unfamiliar with memory allocation
issues. The algorithmic details differ from those in the implementation. There
is a related letter to the editor and a minor correction in the next issue.

Boehm, H., and M. Weiser,
[Garbage Collection in an Uncooperative Environment](http://www.hboehm.info/spe_gc_paper/),
_Software Practice and Experience_, September 1988, pp. 807-820.

Boehm, H., A. Demers, and S. Shenker,
[Mostly Parallel Garbage Collection](http://www.hboehm.info/gc/papers/pldi91.ps.Z),
Proceedings of the ACM SIGPLAN '91 Conference on Programming Language Design
and Implementation, _SIGPLAN Notices 26_, 6 (June 1991), pp. 157-164.

Boehm, H.,
[Space Efficient Conservative Garbage Collection](http://www.hboehm.info/gc/papers/pldi93.ps.Z),
Proceedings of the ACM SIGPLAN '93 Conference on Programming Language Design
and Implementation, _SIGPLAN Notices 28_, 6 (June 1993), pp. 197-206.

Boehm, H., Reducing Garbage Collector Cache Misses,
_Proceedings of the 2000 International Symposium on Memory Management_.
[Official version](https://dl.acm.org/doi/10.1145/362422.362438).
Describes the prefetch strategy incorporated into the collector for
some platforms. Explains why the sweep phase of a _mark-sweep_ collector
should not really be a distinct phase.

M. Serrano, H. Boehm, Understanding Memory Allocation of Scheme Programs,
_Proceedings of the Fifth ACM SIGPLAN International Conference on Functional
Programming_, 2000, Montreal, Canada, pp. 245-256.
[Official version](https://dl.acm.org/doi/10.1145/357766.351264).
Includes some discussion of the collector debugging facilities for
identifying causes of memory retention.

Boehm, H.,
[Fast Multiprocessor Memory Allocation and Garbage Collection](https://www.researchgate.net/publication/242553754_Fast_multiprocessor_memory_allocation_and_garbage_collection).
Discusses the parallel collection algorithms, and presents some performance
results.

Boehm, H., Bounding Space Usage of Conservative Garbage Collectors,
_Proceedings of the 2002 ACM SIGPLAN-SIGACT Symposium on Principles
of Programming Languages_, Jan. 2002, pp. 93-100.
[Official version](https://dl.acm.org/doi/10.1145/565816.503282).
Includes a discussion of a collector facility to much more reliably
test for the potential of unbounded heap growth.

**The following papers discuss language and compiler restrictions necessary
to guaranteed safety of conservative garbage collection:**

We thank John Levine and JCLT for allowing us to make the second paper
available electronically, and providing PostScript for the final version.

Boehm, H.,
[Simple Garbage-Collector-Safety](http://www.hboehm.info/gc/papers/pldi96.ps.gz),
Proceedings of the ACM SIGPLAN '96 Conference on Programming Language Design
and Implementation.

Boehm, H., and D. Chase,
[A Proposal for Garbage-Collector-Safe C Compilation](http://www.hboehm.info/gc/papers/boecha.ps.gz),
_Journal of C Language Translation 4_, 2 (December 1992), pp. 126-141.

**Other related information:**

The Detlefs, Dosser and Zorn's
[Memory Allocation Costs in Large C and C++ Programs](https://www.semanticscholar.org/paper/Memory-allocation-costs-in-large-C-and-C%2B%2B-programs-Detlefs-Dosser/49b2b2cec4ce52493c031d964fb3be31d1e02b77).
This is a performance comparison of the Boehm-Demers-Weiser collector
to `malloc`/`free`, using programs written for `malloc`/`free`.

Joel Bartlett's
[Mostly Copying Conservative Garbage Collector for C++](https://ftp.zx.net.nz/pub/archive/ftp.digital.com/pub/DEC/WRL/research-reports/WRL-TN-12.ps).

John Ellis and David Detlef's
["Safe, Efficient Garbage Collection for C++"](https://dl.acm.org/doi/10.5555/1267974.1267983)
proposal.

[Henry Baker's Archive of Research Papers](https://web.archive.org/web/20200212080133/http://home.pipeline.com/~hbaker1/).

Slides for Hans Boehm's
[Allocation and GC Myths](http://www.hboehm.info/gc/myths.ps) talk.

## Information provided on the BDWGC site

[Known BDWGC users](https://github.com/bdwgc/bdwgc/wiki/Known-clients) list.

[A simple illustration of how to build and use the collector](simple_example.md).

[Description of alternate interfaces to the garbage collector](gcinterface.md).

Tutorial slides from an ISMM 2004:
[The Boehm-Demers-Weiser Conservative Garbage Collector](http://www.hboehm.info/gc/04tutorial.pdf).

[A FAQ (frequently asked questions) list](http://www.hboehm.info/gc/faq.html).

[How to use the garbage collector as a leak detector](leak.md).

[Some hints on debugging garbage collected applications](debugging.md).

[An overview of the implementation of the garbage collector](gcdescr.md).

[The data structure used for fast pointer lookups](tree.md).

[Scalability of the collector to multiprocessors](scale.md).

[Directory](http://www.hboehm.info/gc/gc_source/) containing the distribution
files of all garbage collector releases. It duplicates
[Download](https://github.com/bdwgc/bdwgc/wiki/Download) page on GitHub.

## More background information

[An attempt to establish a bound on space usage of conservative garbage collectors](http://www.hboehm.info/gc/bounds.html).

[Mark-sweep versus copying garbage collectors and their complexity](http://www.hboehm.info/gc/complexity.html).

["Why Conservative Garbage Collectors?"](http://www.hboehm.info/gc/conservative.html)
(pros and cons of conservative garbage collectors, in comparison to other
collectors).

[Advantages and Disadvantages of Conservative Garbage Collection](http://www.hboehm.info/gc/issues.html)
(issues related to garbage collection versus manual memory management in
C/C++).

An example of
[Expensive Explicit Deallocation](http://www.hboehm.info/gc/example.html)
in which the garbage collection results in a much faster implementation as
a result of reduced synchronization.

Slide set discussing
[Performance of Non-Moving Garbage Collectors](http://www.hboehm.info/gc/nonmoving/).

Slide set discussing
["Destructors, Finalizers, and Synchronization"](http://www.hboehm.info/popl03/web/),
POPL 2003 (and the corresponding
[paper](https://dl.acm.org/doi/10.1145/604131.604153)).

[A Java/Scheme/C/C++ garbage collection benchmark](http://www.hboehm.info/gc/gc_bench/).

Slides for talk on
[Memory Allocation Myths](http://www.hboehm.info/gc/myths.ps).

Slides for OOPSLA 98
[Garbage Collection Talk](http://www.hboehm.info/gc/gctalk.ps).

Some other [related papers](http://www.hboehm.info/gc/papers/).

## Contacts and new release announcements

GitHub and Stack Overflow are the major two places for communication.

Technical questions (how to, how does it work, etc.) should be posted
to [Stack Overflow](https://stackoverflow.com/questions/tagged/boehm-gc) with
_boehm-gc_ tag.

To contribute, please rebase your code to the latest
[master](https://github.com/bdwgc/bdwgc/tree/master/) and submit
a [pull request](https://github.com/bdwgc/bdwgc/pulls) to GitHub.

To report a bug, or propose (request) a new feature, create
a [GitHub issue](https://github.com/bdwgc/bdwgc/issues). Please make sure
it has not been reported yet by someone else.

To receive notifications on every release, please subscribe to
[Releases RSS feed](https://github.com/bdwgc/bdwgc/releases.atom).
Notifications on all issues and pull requests are available
by [watching](https://github.com/bdwgc/bdwgc/watchers) the project.

Mailing lists (bdwgc-announce@lists.opendylan.org, bdwgc@lists.opendylan.org,
and the former gc-announce@linux.hpl.hp.com and gc@linux.hpl.hp.com) are not
used at this moment. Their content is available
in
[bdwgc-announce](https://github.com/bdwgc/bdwgc/files/1037650/bdwgc-announce-mailing-list-archive-2014_02.tar.gz)
and
[bdwgc](https://github.com/bdwgc/bdwgc/files/1038163/bdwgc-mailing-list-archive-2017_04.tar.gz)
archive files, respectively. The gc list archive may also be read
at [Narkive](http://bdwgc.opendylan.narkive.com).

Some prior discussion of the collector has taken place on the gcc java mailing
list, whose archives appear [here](http://gcc.gnu.org/ml/java/), and also
on [gclist@iecc.com](http://lists.tunes.org/mailman/listinfo/gclist).
