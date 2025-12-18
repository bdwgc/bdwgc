# Conservative Garbage Collector Porting Directions

The collector is designed to be relatively easy to port, but is not portable
code per se. The collector inherently has to perform operations, such
as scanning the stack(s), that are not possible in portable C code.

All of the following assumes that the collector is being ported to
a byte-addressable 32- or 64-bit machine. Currently all successful ports
to 64-bit machines involve LP64 and LLP64 targets (notably Win64). You
are hereby discouraged from attempting a port to non-byte-addressable,
or 8-bit, or 16-bit machines.

The difficulty of porting the collector varies greatly depending on the needed
functionality. In the simplest case, only some small additions are needed for
the `include/private/gcconfig.h` file. This is described in the following
section. Later sections discuss some of the optional features, which typically
involve more porting effort.

Note that the collector makes heavy use of `ifdef`s. Unlike some other
software projects, we have concluded repeatedly that this is preferable
to system-dependent files, with code duplicated between the files. However,
to keep this manageable, we do strongly believe in indenting `ifdef`s
correctly (for historical reasons usually without the leading sharp sign).
(Separate source files are of course fine if they do not result in code
duplication.)

## Adding platforms to gcconfig.h

If neither multi-threading support, nor tracing of dynamic library data is
required, these are often the only changes you will need to make.

The `include/private/gcconfig.h` file consists of three sections:

  1. A section that defines GC-internal macros that identify the architecture
     (e.g. `IA64` or `I386`) and operating system (e.g. `LINUX` or `MSWIN32`).
     This is usually done by testing predefined macros. By defining our own
     macros instead of using the predefined ones directly, we can impose a bit
     more consistency, and somewhat isolate ourselves from compiler
     differences. It is relatively straightforward to add a new entry here.
     But please try to be consistent with the existing code. In particular,
     64-bit variants of 32-bit architectures general are *not* treated as
     a new architecture. Instead we explicitly test for a 64-bit machine in
     the few places in which it matters. (The notable exception here is `I386`
     and `X86_64`. This is partially historical, and partially justified by
     the fact that there are arguably more substantial architecture and ABI
     differences here than for RISC variants.) On GNU-based systems,
     `cpp -dM -x c /dev/null` seems to generate a set of the predefined
     macros. On some other systems, the "verbose" compiler option may do so,
     or the manual page may list them.

  2. A section that defines a small number of platform-specific macros, which
     are then used directly by the collector. For simple ports, this is where
     most of the effort is required. We describe the macros below.
     This section contains a subsection for each architecture (enclosed in
     a suitable `#ifdef`). Each subsection usually contains some
     architecture-dependent defines, followed by several sets of OS-dependent
     defines, again enclosed in `#ifdef` directives.

  3. A section that fills in defaults for some macros left undefined in the
     preceding section, and defines some other macros that rarely need
     adjustment for new platforms. You will typically not have to touch these.
     If you are porting to an OS that was previously completely unsupported,
     it is likely that you will need to add another clause to the definition
     of `GET_MEM`.

The following macros must be defined correctly for each architecture and
operating system:

  * `MACH_TYPE` - Defined to a string that represents the machine
    architecture. This is just the macro name used to identify the
    architecture, but enclosed in quotes.

  * `OS_TYPE` - Defined to a string that represents the operating system name.
    This is just the macro name used to identify the operating system, but
    enclosed in quotes.

  * `CPP_WORDSZ` - The address (also referred simply as "word") size in bits
    as a constant suitable for preprocessor tests, i.e. without casts or
    `sizeof` expressions. For platforms supporting both 32- and 64-bit ABIs,
    this should be conditionally defined depending on the current ABI.

  * `CPP_PTRSZ` - Similar to `CPP_WORDSZ` but for the size of a pointer (also
    referred as "pointer-sized word") in bits. On most platforms, its value
    is equal to that of `CPP_WORDSZ`.

  * `ALIGNMENT` - Defined to be the largest `n` such that all pointers
    are guaranteed to be aligned on `n`-byte boundaries. Defining it to be
    1 will always work, but perform poorly. For all modern 32-bit platforms,
    this is 4. For all modern 64-bit platforms, this is 8. (Whether or not
    x86 qualifies as a modern architecture here is compiler- and
    OS-dependent.)

  * `DATASTART` - The beginning of the main data segment. The collector will
    trace all memory between `DATASTART` and `DATAEND` for root pointers.
    On some platforms, this can be defined to a constant address, though
    experience has shown that to be risky. Ideally the linker will define
    a symbol (e.g. `_data`) whose address is the beginning of the data
    segment. Sometimes the value can be computed using the
    `GC_SysVGetDataStart` function. Not used if either `SEARCH_FOR_DATA_START`
    macro is defined, or if dynamic loading is supported, and the dynamic
    loading support defines `GC_register_main_static_data()` which returns
    false.

  * `SEARCH_FOR_DATA_START` - If this is defined `DATASTART` will be defined
    to a dynamically computed value which is obtained by starting with the
    address of `_end` and walking backwards until non-addressable memory
    is found. This often works on POSIX-like platforms. It makes it harder
    to debug client programs, since startup involves generating and catching
    a segmentation fault, which tends to confuse users.

  * `DATAEND` - Set to the end of the main data segment. Defaults to `_end`,
    where that is declared as an array. This works in some cases, since the
    linker introduces a suitable symbol.

  * `DATASTART2`, `DATAEND2` - Some platforms have two discontiguous main data
    segments, e.g. for initialized and uninitialized data. If so, these two
    macros should be defined to the limits of the second main data segment.

  * `STACK_GROWS_UP` - Should be defined if the stack (or thread stacks) grow
    towards higher addresses. (This appears to be true only on PA-RISC.
    If your architecture has more than one stack per thread, and is not
    supported yet, you will need to do more work. Grep for `E2K` and `IA64`
    in the source code for examples.)

  * `STACKBOTTOM` - Defined to be the cold end of the stack, which is usually
    (i.e. when the stacks grow down) the highest address in the stack.
    It must bound the region of the stack that contains pointers into the
    collector heap. With multi-threading support, this must be the cold end
    of the main stack, which typically cannot be found in the same way as the
    other thread stacks. If this is not defined and none of the following
    three macros (`SPECIFIC_MAIN_STACKBOTTOM`, `HEURISTIC1`, `HEURISTIC2`)
    is defined, client code must explicitly set `GC_stackbottom` to
    an appropriate value before calling `GC_INIT` or any other `GC_` routine.

  * `SPECIFIC_MAIN_STACKBOTTOM` - May be defined instead of `STACKBOTTOM`.
    If defined, then the cold end of the main stack will be determined in some
    OS-specific way, e.g. by reading it from `/proc` in case of Linux.

  * `HEURISTIC1` - May be defined instead of `STACKBOTTOM`. `STACK_GRAN`
    should be defined too in this case. The cold end of the stack is
    determined by taking an address inside `GC_init`s frame, and rounding it
    up to the next multiple of `STACK_GRAN`. This works well if the stack
    bottom is always aligned to a large power of two. (Note: defining
    `STACK_GRAN` to 0x1000000 is rarely optimal.)

  * `HEURISTIC2` - May be defined instead of `STACKBOTTOM`. The cold end
    of the stack is determined by taking an address inside `GC_init`s frame,
    incrementing it repeatedly in small steps (decrement if `STACK_GROWS_UP`),
    and reading the value at each location. We remember the value when the
    first Segmentation violation or Bus error is signaled, round that to the
    nearest plausible page boundary, and use that as the stack bottom.

  * `DYNAMIC_LOADING` - Should be defined if `dyn_load.c` file has been
    updated for this platform and tracing of dynamic library roots is
    supported.

  * `GWW_VDB`, `MPROTECT_VDB`, `PROC_VDB`, `SOFT_VDB` - May be defined if the
    corresponding _virtual dirty bit_ implementation in `os_dep.c` file is
    usable on this platform. This allows incremental/generational garbage
    collection. (`GWW_VDB` uses the Win32 `GetWriteWatch` function to read
    dirty bits, `MPROTECT_VDB` identifies modified pages by write-protecting
    the heap and catching faults. `PROC_VDB` and `SOFT_VDB` use the `/proc`
    pseudo-files to read dirty bits.)

  * `PREFETCH`, `GC_PREFETCH_FOR_WRITE` - The collector uses `PREFETCH(x)`
    to preload the cache with the data at `x` address. This defaults to
    a no-op.

  * `CLEAR_DOUBLE` - If it is defined, then `CLEAR_DOUBLE(x)` is used as
    a fast way to clear two "pointer-sized" words at `GC_malloc`-aligned
    address `x`. By default, `NULL` pointers storing is used instead.

  * `HEAP_START` - May be defined as the initial address hint for
    `mmap`-based allocation.

## Additional requirements for a basic port

In some cases, you may have to add additional platform-specific code to other
files. A likely candidate is the implementation of
`GC_with_callee_saves_pushed` in `mach_dep.c` file. This ensure that register
contents that the collector must trace from are copied to the stack. Typically
this can be done portably, but on some platforms it may require assembly code,
or just tweaking of conditional compilation tests.

If your platform supports `getcontext` then defining the macro `UNIX_LIKE` for
your OS in `include/private/gcconfig.h` file (if it is not defined there yet)
is likely to solve the problem. Otherwise, if you are using gcc,
`_builtin_unwind_init` will be used, and should work fine. If that is not
applicable either, the implementation will try to use `setjmp`. This will work
if your `setjmp` implementation saves all possibly pointer-valued registers
into the buffer, as opposed to trying to unwind the stack at `longjmp` time.
The `setjmp_test` test tries to determine this, but often does not get it
right. Registers tracing handled with an assembly code is generally to be
avoided.

Most commonly `os_dep.c` file will not require attention, but see below.

## Multi-threading support

Supporting threads requires that the collector be able to find and suspend all
threads potentially accessing the garbage-collected heap, and locate any state
associated with each thread that must be traced.

The functionality needed for multi-threading support is generally implemented
in one or more files specific to the particular thread interface. E.g.,
somewhat portable support of `pthreads` is implemented in `pthread_support.c`
and `pthread_stop_world.c` files. The essential functionality consists of:

  * `GC_stop_world` - Stops all threads which may access the garbage-collected
  heap, other than the caller;
  * `GC_start_world` - Restart other threads;
  * `GC_push_all_stacks` - Push the contents of all thread stacks (or,
  at least, of pointer-containing regions in the thread stacks) onto the mark
  stack.

These very often require that the garbage collector maintain its own data
structures to track active threads.

In addition, `LOCK` and `UNLOCK` must be implemented in
`include/private/gc_locks.h` file.

The easiest case is probably a new `pthreads` platform on which threads can be
stopped with signals. In this case, the changes involve:

  1. Ensuring that if `GC_THREADS` macro is defined by the build scripts, then
     it results in a definition of `GC_PTHREADS` in
     `include/private/gcconfig.h` file.

  2. Ensuring that the `libatomic_ops` package at least minimally supports
     the platform. If incremental GC is needed, or if `pthreads` locks do not
     perform adequately as the allocator lock, you will probably need to
     ensure that a sufficient `libatomic_ops` port exists for the platform to
     provided an atomic test-and-set operation. Alternatively, the collector
     can use the GCC atomic intrinsics instead of `libatomic_ops` package (see
     `include/private/gc_atomic_ops.h` file).

  3. Making any needed adjustments to `pthread_stop_world.c` and
     `pthread_support.c` files. Ideally none should be needed. In fact, not
     all of this is as well standardized as one would like, and outright bugs
     requiring workarounds are common.  Non-preemptive threads packages will
     probably require further work. Similarly thread-local allocation and
     parallel marking requires further work in `pthread_support.c` file, and
     may require better `libatomic_ops` support for the designed platform.

## Dynamic library support

So long as `DATASTART` and `DATAEND` are defined correctly, the collector will
trace memory reachable from file scope or `static` variables defined as part
of the main executable. This is sufficient if either the program is statically
linked, or if pointers to the garbage-collected heap are never stored
in non-stack variables defined in dynamic libraries.

If dynamic library data sections must also be traced, then:

  * `DYNAMIC_LOADING` must be defined in the appropriate section of
  `include/private/gcconfig.h` file.
  * An appropriate versions of the functions `GC_register_dynamic_libraries`
  should be defined in `dyn_load.c` file. This function should invoke
  `GC_cond_add_roots(region_start, region_end, TRUE)` on each dynamic
  library data section.

Implementations that scan for writable data segments are error prone,
particularly in the presence of threads. They frequently result in race
conditions when threads exit and stacks disappear. They may also accidentally
trace large regions of graphics memory, or mapped files. On at least one
occasion they have been known to try to trace device memory that could not
safely be read in the manner the GC wanted to read it.

It is usually safer to walk the dynamic linker data structure, especially
if the linker exports an interface to do so. But beware of poorly documented
locking behavior in this case.

## Incremental GC support

For incremental and generational collection to work, `os_dep.c` file must
contain a suitable _virtual dirty bit_ implementation, which allows the
collector to track which heap pages (assumed to be a multiple of the
collector's block size) have been written during a certain time interval.
The collector provides several implementations, which might be adapted.
The default (`DEFAULT_VDB`) is a placeholder which treats all pages as
having been written. This ensures correctness, but renders incremental and
generational collection essentially useless.

## Stack traces for debug support

If stack traces in objects are needed for debug support, `GC_save_callers` and
`GC_print_callers` must be implemented.

## Disclaimer

This is an initial pass at porting guidelines. Some things have no doubt been
overlooked.
