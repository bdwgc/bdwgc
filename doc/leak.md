# Using the Garbage Collector as Leak Detector

The garbage collector may be used as a leak detector. In this case, the
primary function of the collector is to report objects that were allocated
(typically with `GC_MALLOC`), not deallocated (normally with `GC_FREE`), but
are no longer accessible. Since the object is no longer accessible, there
is normally no way to deallocate the object at a later time; thus it can
safely be assumed that the object has been "leaked".

This is substantially different from counting leak detectors, which simply
verify that all allocated objects are eventually deallocated.
A garbage-collector based leak detector can provide somewhat more precise
information when an object was leaked. More importantly, it does not report
objects that are never deallocated because they are part of "permanent" data
structures. Thus it does not require all objects to be deallocated at process
exit time, a potentially useless activity that often triggers large amounts
of paging.

The garbage collector provides leak detection support. This includes the
following features:

  1. Leak detection mode can be initiated at run-time by `GC_set_find_leak(1)`
  call at program startup instead of building the collector with `FIND_LEAK`
  macro defined.
  2. Leaked objects should be reported and then correctly garbage collected.

To use the collector as a leak detector, do the following steps:

  1. Activate the leak detection mode as described above.
  2. Change the program so that all allocation and deallocation goes through
  the garbage collector.
  3. Arrange to call `GC_gcollect` (or `CHECK_LEAKS()`) at appropriate points
  to check for leaks. (This happens implicitly but probably not with
  a sufficient frequency for long running programs.)

The second step can usually be accomplished with the
`-DREDIRECT_MALLOC=GC_malloc` option when the collector is built, or by
defining `malloc`, `calloc`, `realloc`, `free` (as well as `strdup`,
`strndup`, `wcsdup`, `memalign`, `posix_memalign`) to call the corresponding
garbage collector functions. But this, by itself, will not yield very
informative diagnostics, since the collector does not keep track of the
information about how objects were allocated. The error reports will include
only object addresses.

For more precise error reports, as much of the program as possible should use
the all uppercase variants of these functions, after defining `GC_DEBUG`, and
then including `gc.h`. In this environment `GC_MALLOC` is a macro which causes
at least the file name and line number at the allocation point to be saved
as part of the object. Leak reports will then also include this information.

Many collector features (e.g. finalization and disappearing links) are less
useful in this context, and are not fully supported. Their use will usually
generate additional bogus leak reports, since the collector itself drops some
associated objects.

The same is generally true of thread support. However, the correct leak
reports should be generated with linuxthreads, at least.

On a few platforms (currently Solaris/SPARC, Irix, and, with
`-DSAVE_CALL_CHAIN`, Linux/x86), `GC_MALLOC` also causes some more information
about its call stack to be saved in the object. Such information is reproduced
in the error reports in very non-symbolic form, but it can be very useful with
the aid of a debugger.

## An Example

The `leak_detector.h` file is included in the "include" subdirectory of the
distribution.

Assume the collector has been built with `-DFIND_LEAK` or
`GC_set_find_leak(1)` exists as the first statement in `main`.

The program to be tested for leaks could look like `tests/leak_test.c` file
of the distribution.

On a Linux/x86 system this produces on the stderr stream:


    Found 1 leaked objects:
    0x806dff0 (tests/leak_test.c:19, sz=4, NORMAL)


(On most unmentioned operating systems, the output is similar to this. If the
collector had been built on Linux/x86 with `-DSAVE_CALL_CHAIN`, the output
would be closer to the Solaris example. For this to work, the program should
not be compiled with `-fomit_frame_pointer`.)

On Irix it reports:


    Found 1 leaked objects:
    0x10040fe0 (tests/leak_test.c:19, sz=4, NORMAL)
            Caller at allocation:
                    ##PC##= 0x10004910


and on Solaris the error report is:


    Found 1 leaked objects:
    0xef621fc8 (tests/leak_test.c:19, sz=4, NORMAL)
            Call chain at allocation:
                    args: 4 (0x4), 200656 (0x30FD0)
                    ##PC##= 0x14ADC
                    args: 1 (0x1), -268436012 (0xEFFFFDD4)
                    ##PC##= 0x14A64


In the latter two cases some additional information is given about how malloc
was called when the leaked object was allocated. For Solaris, the first line
specifies the arguments to `GC_debug_malloc` (the actual allocation routine),
The second one specifies the program counter inside `main`, the third one
specifies the arguments to `main`, and, finally, the program counter inside
the caller to `main` (i.e. in the C startup code). In the Irix case, only the
address inside the caller to `main` is given.

In many cases, a debugger is needed to interpret the additional information.
On systems supporting the `adb` debugger, the `tools/callprocs.sh` script can
be used to replace program counter values with symbolic names. The collector
tries to generate symbolic names for call stacks if it knows how to do so on
the platform. This is true on Linux/x86, but not on most other platforms.

## Simplified leak detection under Linux

It should be possible to run the collector in the leak detection mode on
a program a.out under Linux/x86 as follows:

  1. If possible, ensure that a.out is a single-threaded executable. On some
  platforms this does not work at all for the multi-threaded programs.
  2. If possible, ensure that the `addr2line` program is installed
  in `/usr/bin`. (It comes with most Linux distributions.)
  3. If possible, compile your program, which we'll call `a.out`, with full
  debug information. This will improve the quality of the leak reports.
  With this approach, it is no longer necessary to call `GC_` routines
  explicitly, though that can also improve the quality of the leak reports.
  4. Build the collector and install it in directory _foo_ as follows (it may
  be safe to omit the `--disable-threads` option on Linux, but the combination
  of thread support and `malloc` replacement is not yet rock solid):

   - `./configure --prefix=_foo_ --enable-gc-debug --enable-redirect-malloc --disable-threads`
   - `make`
   - `make install`

  5. Set environment variables as follows (the last two are optional, just to
  confirm the collector is running, and to facilitate debugging from another
  console window if something goes wrong, respectively):

   - `LD_PRELOAD=_foo_/lib/libgc.so`
   - `GC_FIND_LEAK`
   - `GC_PRINT_STATS`
   - `GC_LOOP_ON_ABORT`

  6. Simply run `a.out` as you normally would. Note that if you run anything
  else (e.g. your editor) with those environment variables set, it will also
  be leak tested. This may or may not be useful and/or embarrassing. It can
  generate mountains of leak reports if the application was not designed
  to avoid leaks, e.g. because it's always short-lived.

This has not yet been thoroughly tested on large applications, but it's known
to do the right thing on at least some small ones.
