# The environment variables affecting the collector operation

The garbage collector looks at a number of environment variables which are,
then, used to affect its operation.

`GC_INITIAL_HEAP_SIZE=<bytes>` - Sets initial heap size, in bytes.  May speed
up process start-up.  Optionally, may be specified with a multiplier ("k", "M"
or "G") suffix.

`GC_MAXIMUM_HEAP_SIZE=<bytes>` - Sets maximum heap size, in bytes.
Optionally, may be specified with a multiplier suffix.

`GC_LOOP_ON_ABORT` - Causes the collector abort routine to enter a tight loop.
This may make it easier to debug, such a process, especially for
multi-threaded platforms that do not produce usable core files, or if a core
file would be too large.  On some platforms, this also causes `SIGSEGV` to be
caught and result in an infinite loop in a handler, allowing similar debugging
techniques.

`GC_PRINT_STATS` - Turns on the collector logging.  Has no effect if the
collector is built with `SMALL_CONFIG` macro defined.

`GC_LOG_FILE` - Specifies the name of the collector log file.  Otherwise, by
default, logging is performed to `stderr`.  Has no effect if the collector is
built with `SMALL_CONFIG` macro defined.

`GC_ONLY_LOG_TO_FILE` - Turns off redirection of `GC_stdout` and `GC_stderr`
to the log file specified by `GC_LOG_FILE` environment variable.  Has no
effect unless the latter is set; has no effect if the collector is built with
`SMALL_CONFIG` macro defined.

`GC_PRINT_VERBOSE_STATS` - Turns on even more logging.  Has no effect if the
collector is built with `SMALL_CONFIG` macro defined.

`GC_DUMP_REGULARLY` - Instructs the collector to generate a debugging dump (by
`GC_dump_named`) at the collector initialization and during every collection.
Very verbose.  Useful if you have a bug to report, but please include only the
last complete dump.

`GC_COLLECT_AT_MALLOC=<n>` - Overrides the default value specified by
`GC_COLLECT_AT_MALLOC` macro.  Has no effect unless the collector is built
with the latter macro defined.

`GC_BACKTRACES=<n>` - Instructs the collector to generate `n` random
back-traces (for heap profiling) after each collection.  This will not
generate useful output unless most objects in the heap were allocated through
the debug allocators.  This is intended to be only a statistical sample;
individual traces may be erroneous due to concurrent heap mutation.  Has no
effect unless the collector is built with `KEEP_BACK_PTRS` macro defined.

`GC_PRINT_ADDRESS_MAP` (Linux only) - Instructs the collector to dump
`/proc/self/maps` (i.e. various address maps for the process) to `stderr` on
every collection.  Useful for mapping root addresses to source for deciphering
leak reports.

`GC_NPROCS=<n>` (Linux only) - Explicitly sets the number of processor cores
that the collector should expect to use.  Note that setting this to "1", when
multiple CPU cores are available, will preserve correctness, but may lead to
really horrible performance, since the allocator lock implementation will
immediately yield without first spinning.  Has no effect for a single-threaded
collector build.

`GC_MARKERS=<n>` - Sets the number of marker threads.  This is normally set to
the number of processor cores.  It is safer to adjust `GC_MARKERS` one than
`GC_NPROCS`, since the former environment variable has no impact on the
allocator lock implementation.  Has no effect unless the collector is built
with `PARALLEL_MARK` macro defined.

`GC_LARGE_ALLOC_WARN_INTERVAL=<n>` - Instructs the collector to print every
n-th warning about very large block allocations, starting with the n-th one.
Small values of `n` are generally benign, in that a bounded number of such
warnings generally indicate at most a bounded leak.  For best results it
should be set at 1 during testing.  Default is 5.  Very large numbers
effectively disable the warning.

`GC_IGNORE_GCJ_INFO` - Forces the collector to ignore the type descriptors
implicitly supplied by `GC_gcj_malloc()` and friends.  This is useful for
debugging descriptor-generation problems, and possibly for temporarily working
around such ones.  It forces a fully conservative scan of all heap objects
except those known to be pointer-free, and may thus have other adverse
effects.

`GC_PRINT_BACK_HEIGHT` - Turns on printing of maximum length of chain through
unreachable objects ending in a reachable one.  If this number remains
bounded, then the program is "GC robust".  This ensures that a fixed number
of misidentified pointers can only result in a bounded space leak.  This
currently only works if debugging allocation is used throughout.  It increases
the collector space and time requirements appreciably.  This feature is still
somewhat experimental, and requires that the collector has been built with
`MAKE_BACK_GRAPH` macro defined.

`GC_RETRY_SIGNALS` (Pthreads only) - Tries to compensate for lost thread
suspend and restart signals.  On by default for Tru64 UNIX or if the library
is sanitized, off otherwise.  Since we have previously seen similar issues on
some other operating systems, it was turned into a runtime flag to enable
last-minute workarounds.  "0" value means "do not retry signals".

`GC_USE_GETWRITEWATCH=<n>` (Win32 and Linux only) - Explicitly specifies which
strategy of keeping track of dirtied pages should be used.  If n is "0", then
fall back to protecting pages and catching memory faults strategy), else the
collector tries to use GetWriteWatch-based strategy (`GWW_VDB`) or soft-dirty
bits strategy (`SOFT_VDB`) first if available.  Has no effect unless the
collector is built with `MPROTECT_VDB` is defined, and `GWW_VDB` or `SOFT_VDB`
is defined.

`GC_DISABLE_INCREMENTAL` - Ignores runtime requests to enable the incremental
garbage collection mode.  Useful for debugging.


## Ones for program-settable runtime flags

The following ones turn on runtime flags that are also program settable.
Checked only during the collector initialization.  We expect that they will
usually be set through other means, but this may help with debugging and
testing.

`GC_ENABLE_INCREMENTAL` - Turns on incremental collection at startup.  Note
that, depending on platform and collector configuration, this may involve
write-protecting pieces of the heap to track modifications.  These pieces may
include pointer-free objects or not.  This is intended to be transparent, but
in case of the `mprotect`-based implementation it may cause unintended system
call failures (thus, use it with caution).

`GC_PAUSE_TIME_TARGET` - Sets the desired garbage collector pause time in
milliseconds (ms).  Has no effect unless the incremental collection is
enabled.  If a collection requires appreciably more time than this, the client
will be restarted, and the collector will need to do additional work to
compensate.  The special value "999999" indicates that pause time is
unlimited, and the incremental collector will behave completely like a simple
generational collector.  Any value, except for the given special one, disables
parallel marker (almost fully) for now.

`GC_FULL_FREQUENCY` - Sets the desired number of partial collections between
full collections.  Matters only if `GC_incremental` is set.  Has no effect if
the collector is built with `SMALL_CONFIG` macro defined.

`GC_FREE_SPACE_DIVISOR` - Sets `GC_free_space_divisor` to the indicated value.
Setting it to larger values decreases space consumption and increases the
garbage collection frequency.

`GC_UNMAP_THRESHOLD` - Sets the desired threshold of memory blocks unmapping
(the number of sequential garbage collections during those a candidate block
for unmapping should be marked as free).  The special value "0" completely
disables unmapping.

`GC_FORCE_UNMAP_ON_GCOLLECT` - Turns "unmap as much as possible on explicit GC"
mode on (overrides the default value).  Has no effect on implicitly-initiated
garbage collections.  Has no effect if memory unmapping is disabled (or not
compiled in) or if the unmapping threshold is 1.

`GC_FIND_LEAK` - Turns on GC_find_leak and thus leak detection.  Forces a
collection at program termination to detect leaks that would otherwise occur
after the last collection.  Has no effect if the collector is built with
`NO_FIND_LEAK` macro defined.

`GC_FINDLEAK_DELAY_FREE` - Turns on deferred freeing of objects in the
find-leak mode (see the corresponding macro description for more information).
Has no effect if the collector is built with `NO_FIND_LEAK` macro defined.

`GC_ABORT_ON_LEAK` - Causes the application to be terminated once leaked or
smashed objects are found.

`GC_ALL_INTERIOR_POINTERS` - Turns on interior pointer recognition.

`GC_DONT_GC` - Turns off garbage collection.  Use cautiously.

`GC_USE_ENTIRE_HEAP` - Overrides the default behavior whether the
non-incremental collector to use the entire heap before collecting.  See the
description of the similar macro in [macros.md](macros.md).

`GC_TRACE=<addr>` - Causes the debugger to log information about the tracing
of address ranges containing `addr`.  Intended for collector debugging.
Typically `addr` is the address that contains a pointer to an object that
mysteriously failed to get marked.  Value of `addr` must be specified as
a hexadecimal integer.  Has no effect unless the collector is built with
`ENABLE_TRACE` macro defined.
