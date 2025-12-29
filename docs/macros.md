# The configurable macros affecting the collector operation

The collector uses a large amount of conditional compilation in order to
deal with platform dependencies.  This violates a number of known coding
standards.  On the other hand, it seems to be the only practical way to
support this many platforms without excessive code duplication.


## Basic guidelines

A few guidelines have mostly been followed in order to keep this manageable:

  1. `#if` and `#ifdef` directives are properly indented whenever easily
     possible.  All known C compilers allow whitespace between the "#" and the
     `if` to make this possible.  ANSI C also allows white space before the
     "#", though we avoid that.  It has the known disadvantages that it
     differs from the normal GNU conventions, and that it makes patches larger
     than otherwise necessary.  It is still well worth it, for the same reason
     that we indent ordinary `if` statements.

  2. Whenever possible, tests are performed on the macros defined in
     `gcconfig.h` file instead of directly testing platform-specific
     predefined macros.  This makes it relatively easy to adapt to new
     compilers with a different set of predefined macros.  Currently these
     macros generally identify platforms instead of features.  In many cases,
     this is a mistake.

Many of the tested configuration macros are at least somewhat defined in
either `include/config.h.in` or in `Makefile.direct`.  Below is an attempt at
documenting these macros.


## Macros tested during client build

The following macros are tested by the public headers (`gc.h` file and
others). Some of them are also affecting the collector build.

`GC_DEBUG` - Tested by `gc.h` file (and by `gc_cpp.cc` file).  Causes
all-upper-case macros to expand to calls to the debug variant of collector
routines.

`GC_NAMESPACE` - Tested by `gc_cpp.h` file. Causes `gc_cpp` symbols to be
defined in `boehmgc` namespace.

`GC_NAMESPACE_ALLOCATOR` - Tested by `gc_allocator.h` file.  Causes
`gc_allocator<T>` and similar symbols to be defined in `boehmgc` namespace.

`GC_DEBUG_REPLACEMENT` - Tested by `gc.h` file.  Causes `GC_MALLOC`,
`GC_MALLOC_UNCOLLECTABLE`, `GC_REALLOC`, `GC_REALLOCF` to be defined as
`GC_debug_malloc_replacement`, `GC_debug_malloc_uncollectable_replacement`,
`GC_debug_realloc_replacement`, `GC_debug_reallocf_replacement`, respectively.

`GC_NO_THREAD_REDIRECTS` - Tested by `gc.h` file.  Prevents redirection of
thread creation routines and friends to the `GC_` variants.  Requires the
client to explicitly handle thread registration.

`GC_NO_THREAD_DECLS` - Tested by `gc.h` file.  Does not declare thread
creation (and related) routines and do not include platform `windows.h` file
from `gc.h` file.

`GC_DONT_INCLUDE_WINDOWS_H` - Tested by `gc.h` file.  Windows only.  Does not
include platform `windows.h` file from `gc.h` file (but Windows-specific
thread creation routines are declared).

`GC_UNDERSCORE_STDCALL` - Tested by `gc.h` file.  Explicitly prefixes
exported/imported `WINAPI` (`__stdcall`) symbols with "_" (underscore).  Could
be used with MinGW compiler targeting x86 (in conjunction with `GC_DLL`) to
follow MS conventions for `__stdcall` symbols naming.

`_ENABLE_ARRAYNEW` - Tested by gc_cpp.h.  Predefined by the Digital Mars C++
compiler when operator `new[]` and `delete[]` are separately overloadable.

`GC_NO_OPERATOR_NEW_ARRAY` - Tested by `gc_cpp.h` file.  Declares that the C++
compiler does not support the new syntax `operator new[]` for allocating and
deleting arrays.  This is defined implicitly in a few environments.

`GC_NO_INLINE_STD_NEW` - Tested by `gc_cpp.h` file (and by `gc_cpp.cc` file).
Windows only.  Defines the system-wide `new` and `delete` operators in
`gccpp.dll` file instead of providing an inline variant of the operators.

`_DLL` - Tested by gc.h.  Defined by Visual C++ if runtime dynamic libraries
are in use.  Used (only if none of `GC_DLL`, `GC_NOT_DLL` and `__GNUC__` are
defined) to test whether `__declspec(dllimport)` needs to be added to
declarations to support the case in which the collector is in a DLL.

`GC_DLL` - Tested by `cord.h`, `gc.h` files.  Defined by client if dynamic
libraries are being built or used.  Also the macro is set in `gc.h` file if
`_DLL` is defined (except for MinGW) when `GC_NOT_DLL` and `__GNUC__` are both
undefined.  This is the macro that is tested internally to determine whether
the collector is in its own dynamic library.  May need to be set by clients
before include `gc.h` file.  Note that inside the collector implementation it
indicates primarily that the collector should export its symbols.  But in
clients, it indicates that the collector resides in a different dynamic
(shared) library, its entry points should be referenced accordingly, and
precautions may need to be taken to properly deal with statically allocated
variables of the main program.  Used for Windows, and also by GCC v4+ (only
when the dynamic shared library is being built) to hide internally used
symbols.

`GC_NOT_DLL` - Tested by `gc.h` and `gc_cpp.h` files.  A client-settable macro
that overrides `_DLL` macro, e.g. if runtime dynamic libraries are used, but
the collector is in a static library.

`GC_NO_VALLOC` - Tested by `gc.h` and `leak_detector.h` files.  Does not
provide `GC_valloc` and `GC_pvalloc` functions, and do not redirect the
corresponding `glibc` functions in `leak_detector.h` file.

`GC_REQUIRE_WCSDUP` - Tested by `gc.h` and `leak_detector.h` files (and by
`dbg_mlc.c` and `mallocx.c` files).  Forces the collector to export
`GC_wcsdup` (the Unicode variant of `GC_strdup`); could be useful in the
find-leak mode. Clients should define it before including `gc.h` file if the
function is needed.

`GC_THREADS` - Tested by `gc.h` file.  Enables support for native threads.
Must normally be defined by the client (before including `gc.h` file), thus
redefining thread primitives to invoke the `GC_` wrappers instead.

`GC_MARKERS=<n>` - Tested by `gc.h` file.  Sets the desired number of marker
threads.  If not defined or defined to zero, then the collector decides based
on the number of total CPU cores.  Has no effect unless the collector is built
with `PARALLEL_MARK` macro defined.


## Macros tested at collector build

The following macros (`-D` arguments passed to the compiler) are tested only
in `.c`, `.cpp` files and private headers.  Some of them provide the default
configuration values, others become hard-coded values for the library.

`NO_FIND_LEAK` - Excludes the support of the find-leak mode (for smaller code
size).

`FIND_LEAK` - Causes the collector to assume that all inaccessible objects
should have been explicitly deallocated, and reports exceptions.
Finalization and some of the test programs are not usable in this mode.
Actually, `GC_set_find_leak(1)` could be called, before the collector
initialization, to enter this mode.  Should not be defined if `NO_FIND_LEAK`
is defined.

`GC_FINDLEAK_DELAY_FREE` - Turns on deferred freeing of objects in the
find-leak mode letting the collector to detect alter-object-after-free errors
as well as detect leaked objects sooner (instead of only when program
terminates).  Should not be defined if `NO_FIND_LEAK` or `SHORT_DBG_HDRS` is
defined.

`GC_ABORT_ON_LEAK` - Causes the application to be terminated once leaked or
smashed (corrupted on use-after-free) objects are found (after printing the
information about that objects).

`SUNOS5SIGS` - Solaris-like signal handling.  This is probably misnamed,
since it really does not guarantee much more than POSIX.  Currently is set
only for DRSNX, FreeBSD, HP/UX and Solaris.

`GC_WIN32_PTHREADS` (Win32 only) - Enables support for pthreads-win32 (or
other non-Cygwin `pthreads` library for Windows).  This should be specified
instead of (or in addition to) `GC_THREADS`, otherwise Win32 native threads
API will be used.

`PTW32_STATIC_LIB` - Causes the static variant of the MinGW `pthreads` library
to be used.  Requires `GC_WIN32_PTHREADS` macro defined.

`GC_PTHREADS_PARAMARK` - Causes `pthreads`-based parallel mark implementation
to be used even if `GC_WIN32_PTHREADS` macro is undefined.  (Useful for
WinCE.)

`NO_ALL_INTERIOR_POINTERS` - Excludes the support of the all-interior-pointers
mode (for smaller code size).

`ALL_INTERIOR_POINTERS` - Allows all pointers to the interior of objects to be
recognized.  (See `gc_priv.h` file for the consequences.)  Alternatively,
`GC_all_interior_pointers` variable can be set at runtime before the collector
initialization.  Has no effect if `NO_ALL_INTERIOR_POINTERS` macro is defined.

`SMALL_CONFIG` - Tries to tune the collector for small heap sizes,
usually causing it to use less space in such situations.  Incremental
collection no longer works in this case.  Also, removes some
statistic-printing code.  Turns off some optimization algorithms (like data
prefetching in the mark routine).

`NO_CLOCK` - Disables system clock usage.  Disables some statistic printing.

`GC_DISABLE_INCREMENTAL` - Turns off the incremental collection support.

`NO_INCREMENTAL` -  Causes the collector test programs to not invoke the
incremental mode of the collector.  This has no impact on the generated
library, only on the test programs.  (This is often useful for debugging
failures unrelated to the incremental collection.)

`LARGE_CONFIG` - Tunes the collector for unusually large heaps.
Necessary for heaps larger than about 4 GB on most (64-bit) machines.
Recommended for heaps larger than about 0.5 GB.  Not recommended for
embedded systems.  Could be used in conjunction with `SMALL_CONFIG` macro to
generate smaller code (by disabling incremental collection support,
statistic printing and some optimization algorithms).

`DONT_ADD_BYTE_AT_END` - Meaningful only with `ALL_INTERIOR_POINTERS` macro
defined (or `GC_all_interior_pointers` variable set to one).  Normally
`ALL_INTERIOR_POINTERS` macro causes all objects to be padded so that pointers
just past the end of an object can be recognized.  This can be expensive.
(The padding is normally more than one byte due to alignment constraints.)
`DONT_ADD_BYTE_AT_END` macro disables the padding.

`NO_EXECUTE_PERMISSION` - May cause some or all of the heap to not
have execute permission, i.e. it may be impossible to execute
code from the heap.  Affects the incremental collector and memory unmapping.
It may greatly improve the performance, since this may avoid some expensive
cache synchronization.  Instead of relying on this macro, if the execute
permission is required, portable clients should call
`GC_set_pages_executable(1)` at runtime, before the collector initialization.

`REDIRECT_MALLOC` - Causes `malloc` to be redirected to
either `GC_malloc`, `GC_malloc_uncollectable`,
`GC_debug_malloc_replacement` or `GC_debug_malloc_uncollectable_replacement`.
Also causes `realloc` (and `reallocf` similarly) to be redirected to either
`GC_realloc` or `GC_debug_realloc_replacement`, and causes `free` to be
redirected to either `GC_free` or `GC_debug_free` (`freezero` and
`freezeroall` are redirected similarly).  `calloc()`, `strdup()` and
`strndup()` are redefined in terms of the new `malloc` definition.
See also `REDIRECT_MALLOC_UNCOLLECTABLE` and `REDIRECT_MALLOC_DEBUG` macros.
Might not be supported properly on some platforms, e.g. where `malloc` or
`calloc` is used to obtain system memory.  If you build the collector with
`DBG_HDRS_ALL` macro defined, then you should also define
`REDIRECT_MALLOC_DEBUG` macro.

`REDIRECT_MALLOC_UNCOLLECTABLE` - Causes `malloc` to be redirected to
`GC_debug_malloc_uncollectable_replacement` or `GC_malloc_uncollectable`,
depending on `REDIRECT_MALLOC_DEBUG` macro.  Ensures `REDIRECT_MALLOC` macro
is defined.

`REDIRECT_MALLOC_DEBUG` - Causes `malloc` to be redirected to
`GC_debug_malloc_uncollectable_replacement` or `GC_debug_malloc_replacement`,
depending on `REDIRECT_MALLOC_UNCOLLECTABLE` macro.  Also causes `realloc`
(and `reallocf` similarly) and `free` to be redirected to
`GC_debug_realloc_replacement` and `GC_debug_free`, respectively (`freezero`
and `freezeroall` are redirected similarly).  This is to generate leak reports
with call stacks for both `malloc` and `realloc` (and `reallocf`).
Ensures `REDIRECT_MALLOC` macro is defined.  The `replacement` variant of the
above functions invokes the `debug` variant of the corresponding GC routine
with dummy source location information, but still results in properly
remembered call stacks on Linux/i686, Linux/x86_64 and Solaris/SPARC, at
least.

`IGNORE_FREE` - Turns calls to `free()` into a no-op.  `freezero` and
`freezeroall` just clear the object in this case, partly or fully,
respectively.  Useful with `REDIRECT_MALLOC` macro (or friends) defined.

`NO_DEBUGGING` - Prevents providing of `GC_count_set_marks_in_hblk`,
`GC_dump`, `GC_dump_named`, `GC_dump_finalization`, `GC_dump_regions`,
`GC_print_free_list` functions.  Reduces code size slightly at the expense of
debuggability.

`GC_DISABLE_SNPRINTF` - Prevents `snprintf()` usage by the collector itself
(except for call-stack printing code, if supported).  The formatting string
is printed by `GC_printf` (and friends) as is, ignoring the rest of the
arguments.  This might be needed for environments where static linking with
the standard `snprintf()` is not desired for the matter of code size.

`GC_DUMP_REGULARLY` - Causes generation of regular debugging dumps.

`DEBUG_THREADS` - Turns on printing of additional thread-support debugging
information.

`GC_COLLECT_AT_MALLOC=<n>` - Forces garbage collection at every
`GC_malloc_*` call when the size argument greater than the specified value.
(Might be useful for application debugging or in the find-leak mode.)

`JAVA_FINALIZATION` - Makes it somewhat safer to finalize objects out of
order by specifying a non-standard finalization mark procedure (see
`finalize.c`).  Objects reachable from finalizable objects will be marked
in a separate post-pass, and hence their memory will not be reclaimed.
Not recommended unless you are implementing a language that specifies
these semantics.  Actually, the macro determines only the initial value
of `GC_java_finalization` variable.

`FINALIZE_ON_DEMAND` - Causes finalizers to be run only in response to
explicit `GC_invoke_finalizers()` calls.  Actually, the macro only determines
the initial value of `GC_finalize_on_demand` variable.

`GC_NO_FINALIZATION` - Excludes finalization support (for smaller code size).

`GC_TOGGLE_REFS_NOT_NEEDED` - Excludes toggle-refs support (for smaller code
size).  Has no effect if `GC_NO_FINALIZATION` macro is defined.

`GC_NO_DEINIT` - Excludes `GC_deinit()` functionality.

`GC_ATOMIC_UNCOLLECTABLE` - Includes code for
`GC_malloc_atomic_uncollectable`.  This is useful if either the vendor
`malloc` implementation is poor, or if `REDIRECT_MALLOC` macro is defined.

`MARK_BIT_PER_OBJ` - Requests that a mark bit (or a byte) be allocated for
each object.  By default, a mark bit/byte is allocated per a granule - this
often improves speed, possibly at some cost in space and/or cache footprint.

`HBLKSIZE=<ddd>` - Explicitly sets the heap block size (where `ddd` is a power
of two between 512 and 65536).  Each heap block is devoted to a single size
and kind of object.  For the incremental collector it makes sense to match
the most likely page size.  Otherwise large values result in more
fragmentation, but generally better performance for large heaps.

`USE_MMAP` - Forces to use `mmap()` instead of `sbrk()` to get new memory from
the OS.  Works for Linux, FreeBSD, Cygwin, Solaris and Irix, at least.

`USE_MUNMAP` - Causes memory to be returned to the OS under the right
circumstances.  Unsupported on some platforms.  Requires `USE_MMAP` macro
defined (except for Windows).

`USE_WINALLOC` (Cygwin only) - Causes Win32 `VirtualAlloc()` to be used
(instead of `sbrk()` and `mmap()`) to get new memory.  Useful if memory
unmapping is enabled (by defining `USE_MUNMAP` macro).

`MUNMAP_THRESHOLD=<n>` - Sets the default threshold of memory blocks
unmapping (the number of sequential garbage collections during those
a candidate block for unmapping should be marked as free).  The special
value "0" completely disables unmapping.

`GC_FORCE_UNMAP_ON_GCOLLECT` - Sets "unmap as much as possible on explicit GC"
mode on by default.  The mode could be changed at run-time.  Has no effect
unless memory unmapping is turned on.  Has no effect on implicitly-initiated
garbage collections.

`PRINT_BLACK_LIST` - Whenever a black list entry is added, i.e. whenever
the garbage collector detects a value that looks almost, but not quite,
like a pointer, print both the address containing the value, and the
value of the near-bogus-pointer.  Can be used to identify regions of
memory that are likely to contribute misidentified pointers.

`KEEP_BACK_PTRS` - Turns on saving back pointers in debugging headers
for objects allocated with the debugging allocator.  If all objects are
allocated through `GC_MALLOC()` with `GC_DEBUG` defined, this allows the
client to determine how particular or randomly chosen objects are reachable
for debugging/profiling purposes.  The `gc_backptr.h` file interface is
implemented only if this is defined.

`GC_ASSERTIONS` - Enables some internal assertion checking in the collector.
It is intended primarily for debugging of the garbage collector itself, but
could also help to identify cases of incorrect usage of the collector by
a client.

`VALGRIND_TRACKING` - Supports tracking of `GC_malloc` and friends for heap
profiling tools like Valgrind's massif and KDE heaptrack.  Such tools work by
intercepting dynamically linked allocator calls so that they can do their own
bookkeeping for tracking memory usage.  Actually, the macro turns on export
of `GC_free_profiler_hook` no-op function which could be intercepted by the
profiling tools (thus, they could see which objects were freed).

`DBG_HDRS_ALL` - Makes sure that all objects have debug headers.  Increases
the reliability (from 99.9999% to 100% mod. bugs) of some of the debugging
code (especially when `KEEP_BACK_PTRS` is defined).  Makes `SHORT_DBG_HDRS`
possible.  Assumes that all client allocation is done through debugging
allocators.

`SHORT_DBG_HDRS` - Assumes that all objects have debug headers.  Shorten
the headers to minimize object size, at the expense of checking for
writes past the end of an object.  This is intended for environments
in which most client code is written in a "safe" language, such as
Scheme or Java.  Assumes that all client allocation is done using
the `GC_debug_` functions, or through the macros that expand to these,
or by redirecting `malloc()` to `GC_debug_malloc_replacement()`.
(Also eliminates the field for the requested object size.)  It could be
useful for debugging of client code.  In combination with `NO_FIND_LEAK`
macro, this could also be used to reduce code size slightly.  Slows down
the collector somewhat (if the `GC_debug_` functions are used), but not
drastically.

`SAVE_CALL_COUNT=<n>` - Sets the number of call frames saved with objects
allocated through the debugging interface.  Affects the amount of
information generated in leak reports.  Only matters on platforms on which
we can quickly generate call stacks, currently Linux/i686, Linux/x86_64,
Linux/SPARC, Solaris/SPARC, and on platforms that provide execinfo.h.
Default is zero.  On i686, client code should not be compiled with
`-fomit-frame-pointer` option.

`SAVE_CALL_NARGS=<n>` - Sets the number of function arguments to be saved
with each call frame.  Default is zero.  Ignored if we do not know how to
retrieve arguments on the platform.

`CHECKSUMS` - Reports on erroneously clear dirty bits (at a substantial
performance cost).  Use only for debugging of the incremental collector.
Not compatible with `USE_MUNMAP` and not compatible with threads.

`GC_GCJ_SUPPORT` - Includes support for `gcj` (and possibly other systems
that include a pointer to a type descriptor in each allocated object).

`USE_I686_PREFETCH` - Causes the collector to issue Pentium III style
prefetch instructions.  Has no effect except on Linux/i686 platforms.
Empirically the code appears to still run correctly on Pentium II
processors, though with no performance benefit.  May not run on other
i686 processors probably.  In some cases this improves performance by 15%
or so.

`USE_3DNOW_PREFETCH` - Causes the collector to issue AMD 3DNow style
prefetch instructions.  Same restrictions as for `USE_I686_PREFETCH`.
Minimally tested.  Did not appear to be an obvious win on a K6-2/500.

`USE_PPC_PREFETCH` - Causes the collector to issue PowerPC style
prefetch instructions.  Has no effect except on Darwin/PowerPC platforms.
The performance impact is untested.

`GC_USE_LD_WRAP` - In combination with the old flags listed in
[README.linux](platforms/README.linux) causes the collector to handle some
system and `pthreads` calls in a more transparent fashion than the usual
macro-based approach.  Requires GNU `ld`, and currently probably works only
with Linux.

`GC_USE_DLOPEN_WRAP` - Causes the collector to redefine `malloc` and
intercepted `pthreads` routines with their real names, and causes it to use
`dlopen()` and `dlsym()` to refer to the original versions.  This makes it
possible to build an LD_PRELOAD'able `malloc` replacement library.

`USE_RWLOCK` - Uses `rwlock` for the allocator lock instead of mutex.
Thus enable usage of the reader (shared) mode of the allocator lock where
possible.

`THREAD_LOCAL_ALLOC` - Defines `GC_malloc()`, `GC_malloc_atomic()` and
`GC_gcj_malloc()` to use a per-thread set of free lists.  Then these functions
allocate in a way that usually does not involve acquisition of the allocator
(global) lock.  Recommended for multiprocessors.

`USE_COMPILER_TLS` - Causes thread-local allocation to use
the compiler-supported `__thread` thread-local variables.  This is the
default in HP/UX.  It may help performance on recent Linux installations.

`GC_ATTR_TLS_FAST` - Uses specific attributes for `GC_thread_key` like
`__attribute__((tls_model("local-exec")))`.

`PARALLEL_MARK` - Allows the marker to run in multiple threads.  Recommended
for multiprocessors.

`GC_BUILTIN_ATOMIC` - Uses GCC atomic intrinsics instead of `libatomic_ops`
primitives.

`GC_ALWAYS_MULTITHREADED` - Forces the multi-threaded mode right at the
collector initialization.

`GC_ENABLE_SUSPEND_THREAD` (Linux only) - Turns on thread suspend/resume API
support.

`GC_REUSE_SIG_SUSPEND` (Linux only) - Uses same signal number to suspend and
resume threads.  Alternatively, the same effect could be achieved at
runtime by calling `GC_set_suspend_signal(GC_get_thr_restart_signal())`
before the collector initialization.  However, use of same signal
might slightly increase the overall time of resuming threads on some
architectures (because of an extra context save when resuming a thread).

`GC_WINMAIN_REDIRECT` (Win32 only) - Redirects (renames) an application
`WinMain` to `GC_WinMain`; implements the "real" `WinMain` which starts a new
thread to call `GC_WinMain()` after the collector initialization.  Useful for
WinCE.  Incompatible with `GC_DLL` macro.

`GC_INSIDE_DLL` (Win32 only) - Enables DllMain-based approach of threads
registering even in case `GC_DLL` macro is not defined.

`GC_REGISTER_MEM_PRIVATE` (Win32 only) - Forces to register `MEM_PRIVATE` R/W
sections as data roots.  Might be needed for some WinCE 6.0+ custom builds.
(May result in numerous "Data Abort" messages logged to WinCE debugging
console.)  Incompatible with any known GCC toolchain for WinCE.

`NO_GETENV` - Prevents the collector from looking at environment variables.
These may otherwise alter its configuration, or turn off garbage collection
altogether.  Possibly worth defining the macro if the resulting process runs
as a privileged user or to reduce code size.  Defined implicitly for WinCE.

`EMPTY_GETENV_RESULTS` - Workarounds a reputed Wine bug in `getenv`
(`getenv()` may return an empty string instead of `NULL` for a missing entry).

`GC_READ_ENV_FILE` (Win32 only) - Reads environment variables from the
collector "env" file (named as the program name plus ".gc.env" extension).
Useful for WinCE (which has no `getenv`).  In the file, every variable is
specified in a separate line and the format is as `<name>=<value>` (without
spaces).  A comment line may start with any character except for the Latin
letters, the digits and the underscore ("_").  The file encoding is expected
to be "Latin-1".

`USE_GLOBAL_ALLOC` (Win32 only) - Instructs the collector to use
`GlobalAlloc()` instead of `VirtualAlloc()` to allocate the heap.  May be
needed to workaround a Windows NT/2000 issue.  Incompatible with `USE_MUNMAP`
macro.  See [README.win32](platforms/README.win32) for details.

`MAKE_BACK_GRAPH` - Enables `GC_PRINT_BACK_HEIGHT` environment variable.  See
[environment.md](environment.md) for details.  Experimental.  Limited platform
support.  Implies `DBG_HDRS_ALL` macro is defined.  All allocation should be
done using the collector debug interface.

`GC_PRINT_BACK_HEIGHT` - Permanently turns on the back-height printing mode
(useful when `NO_GETENV` macro is defined).  See the similar environment
variable description in [environment.md](environment.md).  Requires
`MAKE_BACK_GRAPH` macro to be defined.

`HANDLE_FORK` (UNIX and Cygwin only) - Attempts by default to make
`GC_malloc()` work in a child process fork'ed from a multi-threaded parent.
Not fully POSIX-compliant and could be disabled at runtime (before `GC_INIT()`
call).

`POINTER_MASK=<0x...>` - Causes candidate pointers to be AND'ed with the given
mask before being considered.  If either this or `POINTER_SHIFT` macro is
defined, it will be assumed that all pointers stored in the heap need to be
processed this way.  Stack and register pointers will be considered both
with and without processing.  These macros are normally needed only to
support systems that use high-order pointer tags.  Has no effect if
`DYNAMIC_POINTER_MASK` macro is defined.

`POINTER_SHIFT=<n>` - Causes the collector to left-shift candidate pointers
by the indicated amount (`n`) before trying to interpret them.  Applied after
`POINTER_MASK`; see also the description of the latter macro.

`DYNAMIC_POINTER_MASK` - Allows to set pointer mask and shift at runtime,
typically before `GC_INIT()` call.  Overrides (undoes definition of)
`POINTER_MASK` and `POINTER_SHIFT` macros (i.e. the default mask and shift
are assumed to be zeros regardless of the latter two macros).

`ENABLE_TRACE` - Enables the `GC_TRACE=<addr>` environment setting to do its
job.  By default this is not supported in order to keep the marker as fast as
possible.

`DARWIN_DONT_PARSE_STACK` - Causes the Darwin port to discover thread
stack bounds in the same way as other `pthreads` ports, without trying to
walk the frames on the stack.  This is recommended only as a fall-back for
applications that do not support proper stack unwinding.

`GC_NO_THREADS_DISCOVERY` (Darwin and Win32+DLL only) - Excludes DllMain-based
(on Windows) and task-threads-based (on Darwin) thread registration support.

`GC_DISCOVER_TASK_THREADS` (Darwin and Win32+DLL only) - Forces to compile
the collector with the implicitly turned on task-threads-based (on Darwin) or
DllMain-based (on Windows) approach of threads registering.  Only for
compatibility and for the case when it is not possible to call
`GC_use_threads_discovery()` early (before other garbage collector calls).

`USE_PROC_FOR_LIBRARIES` - Causes the Linux collector to treat writable
memory mappings (as reported by `/proc`) as roots, if it does not have
other information about them.  It no longer traverses dynamic loader
data structures to find dynamic library static data.  This may be required
for applications that store pointers in segments obtained by `mmap()` without
informing the collector.  But it typically performs poorly, especially
since it will scan inactive but cached NPTL thread stacks completely.

`IGNORE_DYNAMIC_LOADING` - Prevents `DYNAMIC_LOADING` macro definition even if
the feature is supported by the platform (that is, build the collector with
disabled tracing of dynamic library data roots, probably for smaller code
size).

`NO_PROC_STAT` - Causes the collector to avoid relying on Linux
`/proc/self/stat`.

`NO_GETCONTEXT` - Causes the collector to not assume the existence of the
`getcontext()` on Linux-like platforms.  This currently happens implicitly on
ARM, MIPS and some other architectures, as well as on some operating systems
like Android, Hurd, OpenBSD, OS X, etc.  It is explicitly needed for some old
versions of FreeBSD.

`STATIC=static` - Causes various `GC_` symbols that are local to a compilation
unit of the collector to be declared as static (this is the default if
`NO_DEBUGGING` macro is defined).  Reduces the number of visible symbols
(letting the optimizer do its work better), which is probably cleaner, but may
make some kinds of debugging and profiling harder.

`GC_DLL` - Tunes to build dynamic-link library (or dynamic shared object).
For UNIX this causes the exported symbols to have `default` visibility
(ignored unless GCC v4+) and the internal ones to have `hidden` visibility.

`NO_MSGBOX_ON_ERROR` (Win32 only) - Prevents showing Windows message box with
"OK" button on a collector fatal error.  Otherwise the client application is
terminated only once the user clicks "OK" button.  Useful for non-GUI (or
non-interactive) applications.

`DONT_USE_USER32_DLL` (Win32 only) - Forces not to use `user32` DLL import
library (containing `MessageBox()` entry).

`GC_PREFER_MPROTECT_VDB` - Causes `MPROTECT_VDB` to be chosen in case of
multiple virtual dirty bit strategies are implemented (at present useful on
Win32, Solaris and Linux to force `MPROTECT_VDB` strategy instead of the
default `GWW_VDB`, `PROC_VDB` or `SOFT_VDB` ones, respectively).

`DONT_PROTECT_PTRFREE` - Prevents mixing heap blocks with and without pointers
in a page, thus avoid protecting pointer-free pages (at the expense of
potentially bigger heap size).  Has no effect unless MPROTECT_VDB is defined
and the page is larger than the heap block.

`CHECK_SOFT_VDB` - Turns on both `MPROTECT_VDB` and `SOFT_VDB` virtual dirty
bit strategies to check whether they are consistent.  Use only for debugging
of the incremental collector.

`NO_MANUAL_VDB` - Turns off support of the manual VDB (virtual dirty bits)
mode.

`GC_IGNORE_GCJ_INFO` - Disables `gcj`-style type information.  This might be
useful for client debugging on WinCE (which has no `getenv`).

`GC_PRINT_VERBOSE_STATS` - Permanently turns on verbose logging (useful for
debugging and profiling on WinCE).

`GC_ONLY_LOG_TO_FILE` - Prevents `GC_stdout` and `GC_stderr` redirection to
the collector log file specified by `GC_LOG_FILE` environment variable.  Has
no effect unless the variable is set (to anything other than "0").

`GC_ANDROID_LOG` (Android only) - Forces the collector to output error/debug
information to Android log.

`CONSOLE_LOG` (Win32 only) - Forces the collector to output error/debug
information to `stdout` and `stderr`.

`GC_DONT_EXPAND` - Prevents expanding the heap unless explicitly requested or
forced to.

`GC_USE_ENTIRE_HEAP` - Causes the non-incremental collector to use the
entire heap before collecting.  This sometimes results in more large-block
fragmentation, since very large blocks will tend to get broken up during
each garbage collection cycle.  It is likely to result in a larger working
set, but lower collection frequencies, and hence fewer instructions executed
in the collector.  Actually, the macro determines only the initial value
of `GC_use_entire_heap` variable.

`GC_INITIAL_HEAP_SIZE=<value>` - Sets the desired default initial heap size,
in bytes.

`GC_FREE_SPACE_DIVISOR=<value>` - Sets the alternate default value of
`GC_free_space_divisor` variable.

`GC_ALLOCD_BYTES_PER_FINALIZER=<value>` - Sets the alternate default value of
`GC_allocd_bytes_per_finalizer` variable.

`GC_TIME_LIMIT=<milliseconds>` - Sets the alternate default value of
`GC_time_limit` variable (setting this to `GC_TIME_UNLIMITED` will essentially
disable incremental collection while leaving generational collection
enabled).

`GC_FULL_FREQ=<n>` - Sets the alternate default number of partial collections
between full collections (matters only if the incremental collection mode
is on).

`NO_CANCEL_SAFE` (POSIX platforms with threads only) - Stops bothering about
making the collector safe for thread cancellation.  Assuming cancellation is
not used by client.  (If cancellation is used anyway, threads may end up
getting canceled in unexpected places.)  Even without this option,
`PTHREAD_CANCEL_ASYNCHRONOUS` is never safe with the collector.  (We could
argue about its safety without the collector.)

`UNICODE` (Win32 only) - Forces to use the Unicode variant (with suffix "W")
of the Win32 API instead of ANSI/ASCII one (with suffix "A").  Useful for
WinCE.

`HOST_ANDROID` (or `__ANDROID__`) - Targets compilation for Android NDK
platform.

`SN_TARGET_PS3` - Targets compilation for Sony PS/3.

`USE_GET_STACKBASE_FOR_MAIN` (Linux only) - Forces to use
`pthread_attr_getstack()` instead of `__libc_stack_end` (or instead of any
hard-coded value) for getting the primordial thread stack bottom (useful if
the client modifies the program's address space).

`EMSCRIPTEN_ASYNCIFY` (Emscripten only) - Forces to use Asyncify feature.
Note this is a relatively rarely used feature of Emscripten, as most
developers do not use it, and it does not scale well to moderate-to-large
code bases.  But the feature is needed to pass `gctest`, at least.  Requires
`-sASYNCIFY` linker flag.
