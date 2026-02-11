/*
 * Copyright 1988, 1989 Hans-J. Boehm, Alan J. Demers
 * Copyright (c) 1991-1994 by Xerox Corporation.  All rights reserved.
 * Copyright (c) 1996-1999 by Silicon Graphics.  All rights reserved.
 * Copyright (c) 1999-2004 Hewlett-Packard Development Company, L.P.
 * Copyright (c) 2008-2025 Ivan Maidanski
 *
 * THIS MATERIAL IS PROVIDED AS IS, WITH ABSOLUTELY NO WARRANTY EXPRESSED
 * OR IMPLIED.  ANY USE IS AT YOUR OWN RISK.
 *
 * Permission is hereby granted to use or copy this program
 * for any purpose, provided the above notices are retained on all copies.
 * Permission to modify the code and to distribute modified code is granted,
 * provided the above notices are retained, and a notice that the code was
 * modified is included with the above copyright notice.
 */

#ifndef GC_PRIVATE_H
#define GC_PRIVATE_H

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#if !defined(GC_BUILD) && !defined(NOT_GCBUILD)
#  define GC_BUILD
#endif

#if (defined(__linux__) || defined(__GLIBC__) || defined(__GNU__) \
     || defined(__CYGWIN__) || defined(HAVE_DLADDR)               \
     || (defined(__COSMOPOLITAN__) && defined(USE_MUNMAP))        \
     || defined(GC_HAVE_PTHREAD_SIGMASK)                          \
     || defined(HAVE_PTHREAD_SETNAME_NP_WITHOUT_TID)              \
     || defined(HAVE_PTHREAD_SETNAME_NP_WITH_TID_AND_ARG)         \
     || defined(HAVE_PTHREAD_SETNAME_NP_WITH_TID))                \
    && !defined(_GNU_SOURCE)
/* Cannot test `LINUX`, since this must be defined before other includes. */
#  define _GNU_SOURCE 1
#endif

#if defined(__INTERIX) && !defined(_ALL_SOURCE)
#  define _ALL_SOURCE 1
#endif

#if (defined(DGUX) && defined(GC_THREADS) || defined(DGUX386_THREADS) \
     || defined(GC_DGUX386_THREADS))                                  \
    && !defined(_USING_POSIX4A_DRAFT10)
#  define _USING_POSIX4A_DRAFT10 1
#endif

#if defined(NO_DEBUGGING) && !defined(GC_ASSERTIONS) && !defined(NDEBUG)
/* To turn off assertion checking (in `atomic_ops.h` file). */
#  define NDEBUG 1
#endif

#ifndef GC_H
#  include "gc/gc.h"
#endif

#include <stdlib.h>
#if !defined(sony_news)
#  include <stddef.h>
#endif

#ifdef DGUX
#  include <sys/resource.h>
#  include <sys/time.h>
#endif

#ifdef BSD_TIME
#  include <sys/resource.h>
#  include <sys/time.h>
#endif

#ifdef PARALLEL_MARK
#  define AO_REQUIRE_CAS
#  if !defined(__GNUC__) && !defined(AO_ASSUME_WINDOWS98)
#    define AO_ASSUME_WINDOWS98
#  endif
#endif

#include "gc/gc_mark.h"
#include "gc/gc_tiny_fl.h"

typedef GC_word word;

#ifndef PTR_T_DEFINED
/*
 * A generic pointer to which we can add byte displacements and which
 * can be used for address comparisons.
 */
typedef char *ptr_t;
#  define PTR_T_DEFINED
#endif

#ifndef SIZE_MAX
#  include <limits.h>
#endif
#if defined(SIZE_MAX) && !defined(CPPCHECK)
/*
 * A constant representing maximum value for `size_t` type.  Note: an extra
 * cast is used to workaround some buggy `SIZE_MAX` definitions.
 */
#  define GC_SIZE_MAX ((size_t)SIZE_MAX)
#else
#  define GC_SIZE_MAX (~(size_t)0)
#endif

#if (GC_GNUC_PREREQ(3, 0) || defined(__clang__)) && !defined(LINT2)
/* Equivalent to `e`, but predict that usually `e` is true (false). */
#  define LIKELY(e) __builtin_expect(e, 1 /* `TRUE` */)
#  define UNLIKELY(e) __builtin_expect(e, 0 /* `FALSE` */)
#else
#  define LIKELY(e) (e)
#  define UNLIKELY(e) (e)
#endif /* __GNUC__ */

/*
 * Saturated addition of `size_t` values.  Used to avoid value wrap around
 * on overflow.  The arguments should have no side effects.
 */
#define SIZET_SAT_ADD(a, b) \
  (LIKELY((a) < GC_SIZE_MAX - (b)) ? (a) + (b) : GC_SIZE_MAX)

#include "gcconfig.h"

#ifdef __cplusplus
typedef bool GC_bool;
#elif defined(__BORLANDC__) || defined(__WATCOMC__)
typedef int GC_bool;
#else
typedef char GC_bool;
#endif

#if defined(__cplusplus) && !defined(ANY_MSWIN)
/* Avoid macro redefinition on a Windows platform. */
#  define TRUE true
#  define FALSE false
#else
#  define TRUE 1
#  define FALSE 0
#endif

#if !defined(GC_ATOMIC_UNCOLLECTABLE) && defined(ATOMIC_UNCOLLECTABLE)
/* For compatibility with old-style naming. */
#  define GC_ATOMIC_UNCOLLECTABLE
#endif

#ifndef GC_INNER
/*
 * This tagging macro must be used at the start of every variable definition
 * which is declared with `GC_EXTERN`.  Should be also used for the GC-scope
 * function definitions and prototypes.  Must not be used in `gcconfig.h`
 * file.  Should not be used for the debugging-only functions.
 */
#  if defined(GC_DLL) && defined(__GNUC__) && !defined(ANY_MSWIN)
#    if GC_GNUC_PREREQ(4, 0) && !defined(GC_NO_VISIBILITY)
/* See the corresponding `GC_API` definition. */
#      define GC_INNER __attribute__((__visibility__("hidden")))
#    else
/* The attribute is unsupported. */
#      define GC_INNER /*< empty */
#    endif
#  else
#    define GC_INNER /*< empty */
#  endif

#  define GC_EXTERN extern GC_INNER
/*
 * Used only for the GC-scope variables (prefixed with `GC_`) declared
 * in the private header files.  Must not be used for thread-local
 * variables.  Must not be used in `gcconfig.h` file.
 * The corresponding variable definition must start with `GC_INNER`.
 * Should not be used for the debugging- or profiling-only variables.
 * As of now, there are some other exceptions, e.g. for the variables
 * that are known to be used by some popular clients.
 */
#endif /* !GC_INNER */

#ifdef __cplusplus
/* `register` storage specifier is deprecated in C++11. */
#  define REGISTER /*< empty */
#else
/*
 * Used only for several local variables in the performance-critical
 * functions.  Should not be used for new code.
 */
#  define REGISTER register
#endif

#if defined(CPPCHECK)
#  define MACRO_BLKSTMT_BEGIN {
#  define MACRO_BLKSTMT_END }
#  define LOCAL_VAR_INIT_OK = 0 /*< to avoid "uninit var" false positive */
#else
#  define MACRO_BLKSTMT_BEGIN do {
#  define MACRO_BLKSTMT_END \
    }                       \
    while (0)
#  define LOCAL_VAR_INIT_OK /*< empty */
#endif

#if defined(M68K) && defined(__GNUC__)
/*
 * By default, `__alignof__(void *)` is 2 on m68k architecture.
 * Use this attribute to have the machine-word alignment (i.e. 4-byte one
 * on the given 32-bit architecture).
 */
#  define GC_ATTR_PTRT_ALIGNED __attribute__((__aligned__(sizeof(ptr_t))))
#else
#  define GC_ATTR_PTRT_ALIGNED /*< empty */
#endif

#ifdef CHERI_PURECAP
#  include <cheriintrin.h>
#endif

EXTERN_C_BEGIN

typedef GC_uintptr_t GC_funcptr_uint;
#define FUNCPTR_IS_DATAPTR

typedef unsigned int unsigned32;

#define hblk GC_hblk_s
struct hblk;

typedef struct hblkhdr hdr;

EXTERN_C_END

#include "gc_hdrs.h"

#ifndef GC_ATTR_NO_SANITIZE_ADDR
#  ifndef ADDRESS_SANITIZER
#    define GC_ATTR_NO_SANITIZE_ADDR /*< empty */
#  elif GC_CLANG_PREREQ(3, 8)
#    define GC_ATTR_NO_SANITIZE_ADDR \
      __attribute__((__no_sanitize__("address")))
#  else
#    define GC_ATTR_NO_SANITIZE_ADDR __attribute__((__no_sanitize_address__))
#  endif
#endif /* !GC_ATTR_NO_SANITIZE_ADDR */

#ifndef GC_ATTR_NO_SANITIZE_MEMORY
#  ifndef MEMORY_SANITIZER
#    define GC_ATTR_NO_SANITIZE_MEMORY /*< empty */
#  elif GC_CLANG_PREREQ(3, 8)
#    define GC_ATTR_NO_SANITIZE_MEMORY \
      __attribute__((__no_sanitize__("memory")))
#  else
#    define GC_ATTR_NO_SANITIZE_MEMORY __attribute__((__no_sanitize_memory__))
#  endif
#endif /* !GC_ATTR_NO_SANITIZE_MEMORY */

#ifndef GC_ATTR_NO_SANITIZE_THREAD
#  ifndef THREAD_SANITIZER
#    define GC_ATTR_NO_SANITIZE_THREAD /*< empty */
#  elif GC_CLANG_PREREQ(3, 8)
#    define GC_ATTR_NO_SANITIZE_THREAD \
      __attribute__((__no_sanitize__("thread")))
#  else
/*
 * It seems that `no_sanitize_thread` attribute has no effect if the
 * function is inlined (as of gcc-11.1.0, at least).
 */
#    define GC_ATTR_NO_SANITIZE_THREAD \
      GC_ATTR_NOINLINE __attribute__((__no_sanitize_thread__))
#  endif
#endif /* !GC_ATTR_NO_SANITIZE_THREAD */

#define GC_ATTR_NO_SANITIZE_ADDR_MEM_THREAD           \
  GC_ATTR_NO_SANITIZE_ADDR GC_ATTR_NO_SANITIZE_MEMORY \
      GC_ATTR_NO_SANITIZE_THREAD

#ifndef UNUSED_ARG
#  define UNUSED_ARG(arg) ((void)(arg))
#endif

#ifdef HAVE_CONFIG_H
/* The `inline` keyword is determined by `AC_C_INLINE` of `autoconf`. */
#  define GC_INLINE static inline
#elif defined(_MSC_VER) || defined(__INTEL_COMPILER) || defined(__DMC__) \
    || (GC_GNUC_PREREQ(3, 0) && defined(__STRICT_ANSI__))                \
    || defined(__BORLANDC__) || defined(__WATCOMC__)
#  define GC_INLINE static __inline
#elif GC_GNUC_PREREQ(3, 0) || defined(__sun)
#  define GC_INLINE static inline
#else
#  define GC_INLINE static
#endif

#ifndef GC_ATTR_NOINLINE
#  if GC_GNUC_PREREQ(4, 0)
#    define GC_ATTR_NOINLINE __attribute__((__noinline__))
#  elif _MSC_VER >= 1400
#    define GC_ATTR_NOINLINE __declspec(noinline)
#  else
#    define GC_ATTR_NOINLINE /*< empty */
#  endif
#endif

#ifndef GC_API_OSCALL
/* This is used to identify GC routines called by name from OS. */
#  if defined(__GNUC__)
#    if GC_GNUC_PREREQ(4, 0) && !defined(GC_NO_VISIBILITY)
/* Same as `GC_API` macro if `GC_DLL` one is defined. */
#      define GC_API_OSCALL extern __attribute__((__visibility__("default")))
#    else
/* The attribute is unsupported. */
#      define GC_API_OSCALL extern
#    endif
#  else
#    define GC_API_OSCALL GC_API
#  endif
#endif

#ifndef GC_API_PRIV
#  define GC_API_PRIV GC_API
#endif

#ifndef GC_API_PATCHABLE
#  define GC_API_PATCHABLE GC_ATTR_NOINLINE GC_API
#endif

#if defined(THREADS) && !defined(NN_PLATFORM_CTR)
#  include "gc_atomic_ops.h"
#  ifndef AO_HAVE_compiler_barrier
#    define AO_HAVE_compiler_barrier 1
#  endif
#endif

#ifdef ANY_MSWIN
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN 1
#  endif
#  define NOSERVICE
#  include <windows.h>

/* This is included strictly after the platform `windows.h` file. */
#  include <winbase.h>
#endif /* ANY_MSWIN */

#include "gc_locks.h"

#ifdef GC_ASSERTIONS
#  define GC_ASSERT(e)                                                   \
    do {                                                                 \
      if (UNLIKELY(!(e))) {                                              \
        GC_err_printf("Assertion failure: %s:%d\n", __FILE__, __LINE__); \
        ABORT("assertion failure");                                      \
      }                                                                  \
    } while (0)
#else
#  define GC_ASSERT(e)
#endif

#include "gc/gc_inline.h"

/*
 * Prevent certain compiler warnings by making a pointer-related cast
 * through a "pointer-sized" numeric type.
 */
#define CAST_THRU_UINTPTR(t, x) ((t)(GC_uintptr_t)(x))

#define CAST_AWAY_VOLATILE_PVOID(p) \
  CAST_THRU_UINTPTR(/* no volatile */ void *, p)

/*
 * Convert an `unsigned` value to a `void` pointer.  Typically used to
 * print a numeric value using "%p" format specifier.  The pointer is not
 * supposed to be dereferenced.
 */
#define NUMERIC_TO_VPTR(v) ((void *)(GC_uintptr_t)(v))

/* Create a `ptr_t` pointer from a number (of `word` type). */
#define MAKE_CPTR(w) ((ptr_t)(GC_uintptr_t)(word)(w))

#define GC_WORD_MAX (~(word)0)

/* Convert given pointer to its address.  Result is of `word` type. */
#ifdef CHERI_PURECAP
#  define ADDR(p) cheri_address_get(p)
#else
#  define ADDR(p) ((word)(GC_uintptr_t)(p))
#endif

#define ADDR_LT(p, q) GC_ADDR_LT(p, q)
#define ADDR_GE(p, q) (!ADDR_LT(p, q))

/*
 * Check whether pointer `p` is in range [`s`, `e_p1`).
 * `p` should not have side effects.
 */
#define ADDR_INSIDE(p, s, e_p1) (ADDR_GE(p, s) && ADDR_LT(p, e_p1))

/* Handy definitions to compare and adjust pointers in a stack. */
#ifdef STACK_GROWS_UP
#  define HOTTER_THAN(p, q) ADDR_LT(q, p) /*< inverse */
#  define MAKE_COOLER(p, d) \
    (void)((p) -= ADDR(p) > (word)((d) * sizeof(*(p))) ? (d) : 0)
#  define MAKE_HOTTER(p, d) (void)((p) += (d))
#else
#  define HOTTER_THAN(p, q) ADDR_LT(p, q)
#  define MAKE_COOLER(p, d) \
    (void)((p)              \
           += ADDR(p) <= (word)(GC_WORD_MAX - (d) * sizeof(*(p))) ? (d) : 0)
#  define MAKE_HOTTER(p, d) (void)((p) -= (d))
#endif /* !STACK_GROWS_UP */

/* Clear/set flags (given by a mask) in a pointer. */
#define CPTR_CLEAR_FLAGS(p, mask) \
  (ptr_t)((GC_uintptr_t)(p) & ~(GC_uintptr_t)(word)(mask))
#define CPTR_SET_FLAGS(p, mask) (ptr_t)((GC_uintptr_t)(p) | (word)(mask))

/* Easily changeable parameters are below. */

#ifdef ALL_INTERIOR_POINTERS
/*
 * Forces all pointers into the interior of an object to be considered valid.
 * Also causes the sizes of all objects to be inflated by at least one byte.
 * This should suffice to guarantee that in the presence of a compiler that
 * does not perform garbage-collector-unsafe optimizations, all portable,
 * strictly ANSI conforming C programs should be safely usable with `malloc`
 * replaced by `GC_malloc` and `free` calls removed.  There are several
 * disadvantages:
 *   1. There are probably no interesting, portable, strictly ANSI-conforming
 *      C programs;
 *   2. This option makes it hard for the collector to allocate space that is
 *      not "pointed to" by integers, etc.  (Under SunOS 4.x with a statically
 *      linked `libc`, we empirically observed that it would be difficult to
 *      allocate individual objects larger than 100 KB; even if only smaller
 *      objects are allocated, more swap space is likely to be needed;
 *      fortunately, much of this will never be touched.)
 *
 * If you can easily avoid using this option, do.  If not, try to keep
 * individual objects small.  This is really controlled at startup, through
 * `GC_all_interior_pointers` variable.
 */
#endif

EXTERN_C_BEGIN

#ifndef GC_NO_FINALIZATION
/*
 * If `GC_finalize_on_demand` is not set, invoke eligible finalizers.
 * Otherwise: call `(*GC_finalizer_notifier)()` if there are finalizers to
 * be run, and we have not called this procedure yet this collection cycle.
 */
GC_INNER void GC_notify_or_invoke_finalizers(void);

/*
 * Perform all indicated finalization actions on unmarked objects.
 * Unreachable finalizable objects are enqueued for processing by
 * `GC_invoke_finalizers()`.  Cause disappearing links to disappear
 * and unreachable objects to be enqueued for finalization.
 * Invoked with the allocator lock held but the world is running.
 */
GC_INNER void GC_finalize(void);

#  ifndef GC_TOGGLE_REFS_NOT_NEEDED
/* Process the "toggle-refs" before GC starts. */
GC_INNER void GC_process_togglerefs(void);
#  endif
#  ifndef SMALL_CONFIG
GC_INNER void GC_print_finalization_stats(void);
#  endif
#else
#  define GC_notify_or_invoke_finalizers() (void)0
#endif /* GC_NO_FINALIZATION */

#ifdef NO_ALL_INTERIOR_POINTERS
#  define GC_all_interior_pointers 0 /*< false */
#endif

#if defined(DONT_ADD_BYTE_AT_END) || defined(NO_ALL_INTERIOR_POINTERS)
#  define MAX_EXTRA_BYTES 0
#  define EXTRA_BYTES MAX_EXTRA_BYTES
#else
#  define MAX_EXTRA_BYTES 1
#  ifdef LINT2
/*
 * Explicitly instruct the code analysis tool that `GC_all_interior_pointers`
 * is assumed to have only value of 0 or 1.
 */
#    define EXTRA_BYTES ((size_t)(GC_all_interior_pointers ? 1 : 0))
#  else
#    define EXTRA_BYTES ((size_t)GC_all_interior_pointers)
#  endif
#endif

#ifdef LARGE_CONFIG
#  define MINHINCR 64
#  define MAXHINCR 4096
#else
/*
 * Minimum heap increment, in blocks of `HBLKSIZE`.  Note: must be multiple
 * of largest page size.
 */
#  define MINHINCR 16

/* Maximum heap increment, in blocks. */
#  define MAXHINCR 2048
#endif /* !LARGE_CONFIG */

/* Stack saving for debugging. */

#ifdef NEED_CALLINFO
struct callinfo {
  GC_return_addr_t ci_pc; /*< `pc` of caller, not callee */
#  if NARGS > 0
  GC_hidden_pointer ci_arg[NARGS]; /*< hide to avoid retention */
#  endif
#  if (NFRAMES * (NARGS + 1)) % 2 == 1
  /* Likely alignment problem. */
  ptr_t ci_dummy;
#  endif
};

#  ifdef SAVE_CALL_CHAIN
/*
 * Fill in the `pc` and argument information for up to `NFRAMES` of
 * my callers.  Ignore my frame and my callers frame.
 */
GC_INNER void GC_save_callers(struct callinfo info[NFRAMES]);
#  endif

/* Print `info` to `stderr`.  We do not hold the allocator lock. */
GC_INNER void GC_print_callers(struct callinfo info[NFRAMES]);
#endif /* NEED_CALLINFO */

EXTERN_C_END

/*
 * Macros to ensure same formatting of C array/struct/union initializer
 * across multiple versions of clang-format.
 */
#define C_INITIALIZER_BEGIN {
#define C_INITIALIZER_END }

/* OS interface routines. */

#ifndef NO_CLOCK
#  ifdef BSD_TIME
#    undef CLOCK_TYPE
#    undef GET_TIME
#    undef MS_TIME_DIFF
#    define CLOCK_TYPE struct timeval
#    define CLOCK_TYPE_INITIALIZER C_INITIALIZER_BEGIN 0, 0 C_INITIALIZER_END
#    define GET_TIME(x)                  \
      do {                               \
        struct rusage rusage;            \
        getrusage(RUSAGE_SELF, &rusage); \
        x = rusage.ru_utime;             \
      } while (0)

/*
 * Compute time difference.  `a` time is expected to be not earlier
 * than `b` one; the result has `unsigned long` type.
 */
#    define MS_TIME_DIFF(a, b)                                            \
      ((unsigned long)((long)(a.tv_sec - b.tv_sec) * 1000                 \
                       + (long)(a.tv_usec - b.tv_usec) / 1000             \
                       - (a.tv_usec < b.tv_usec                           \
                                  && (long)(a.tv_usec - b.tv_usec) % 1000 \
                                         != 0                             \
                              ? 1                                         \
                              : 0)))

/*
 * The nanosecond part of the time difference.  The total time difference
 * could be computed as:
 * `MS_TIME_DIFF(a, b) * 1000000 + NS_FRAC_TIME_DIFF(a, b)`.
 */
#    define NS_FRAC_TIME_DIFF(a, b)                                          \
      ((unsigned long)((a.tv_usec < b.tv_usec                                \
                                && (long)(a.tv_usec - b.tv_usec) % 1000 != 0 \
                            ? 1000L                                          \
                            : 0)                                             \
                       + (long)(a.tv_usec - b.tv_usec) % 1000)               \
       * 1000)

#  elif defined(MSWIN32) || defined(MSWINCE) || defined(WINXP_USE_PERF_COUNTER)
#    if defined(MSWINRT_FLAVOR) || defined(WINXP_USE_PERF_COUNTER)
#      define CLOCK_TYPE ULONGLONG
/*
 * Note: two standalone `if` statements below are used to avoid MS VC
 * false warning (FP) about potentially uninitialized `tc` variable.
 */
#      define GET_TIME(x)                                              \
        do {                                                           \
          LARGE_INTEGER freq, tc;                                      \
          if (!QueryPerformanceFrequency(&freq))                       \
            ABORT("QueryPerformanceFrequency requires WinXP+");        \
          if (!QueryPerformanceCounter(&tc))                           \
            ABORT("QueryPerformanceCounter failed");                   \
          x = (CLOCK_TYPE)((double)tc.QuadPart / freq.QuadPart * 1e9); \
          /* TODO: Call QueryPerformanceFrequency once at GC init. */  \
        } while (0)
#      define MS_TIME_DIFF(a, b) ((unsigned long)(((a) - (b)) / 1000000UL))
#      define NS_FRAC_TIME_DIFF(a, b) \
        ((unsigned long)(((a) - (b)) % 1000000UL))
#    else
#      define CLOCK_TYPE DWORD
#      define GET_TIME(x) (void)(x = GetTickCount())
#      define MS_TIME_DIFF(a, b) ((unsigned long)((a) - (b)))
#      define NS_FRAC_TIME_DIFF(a, b) 0UL
#    endif /* !WINXP_USE_PERF_COUNTER */

#  elif defined(NN_PLATFORM_CTR)
#    define CLOCK_TYPE long long
EXTERN_C_BEGIN
CLOCK_TYPE n3ds_get_system_tick(void);
CLOCK_TYPE n3ds_convert_tick_to_ms(CLOCK_TYPE tick);
EXTERN_C_END
#    define GET_TIME(x) (void)(x = n3ds_get_system_tick())
#    define MS_TIME_DIFF(a, b) \
      ((unsigned long)n3ds_convert_tick_to_ms((a) - (b)))
/* TODO: Implement NS_FRAC_TIME_DIFF(). */
#    define NS_FRAC_TIME_DIFF(a, b) 0UL

#  elif defined(HAVE_CLOCK_GETTIME)
#    include <time.h>
#    define CLOCK_TYPE struct timespec
#    define CLOCK_TYPE_INITIALIZER C_INITIALIZER_BEGIN 0, 0 C_INITIALIZER_END
#    if defined(_POSIX_MONOTONIC_CLOCK) && !defined(NINTENDO_SWITCH)
#      define GET_TIME(x)                               \
        do {                                            \
          if (clock_gettime(CLOCK_MONOTONIC, &x) == -1) \
            ABORT("clock_gettime failed");              \
        } while (0)
#    else
#      define GET_TIME(x)                              \
        do {                                           \
          if (clock_gettime(CLOCK_REALTIME, &x) == -1) \
            ABORT("clock_gettime failed");             \
        } while (0)
#    endif
#    define MS_TIME_DIFF(a, b)                                            \
      /* `a.tv_nsec - b.tv_nsec` is in range -1e9 to 1e9, exclusively. */ \
      ((unsigned long)((a).tv_nsec + (1000000L * 1000 - (b).tv_nsec))     \
           / 1000000UL                                                    \
       + ((unsigned long)((a).tv_sec - (b).tv_sec) * 1000UL) - 1000UL)
#    define NS_FRAC_TIME_DIFF(a, b)                                   \
      ((unsigned long)((a).tv_nsec + (1000000L * 1000 - (b).tv_nsec)) \
       % 1000000UL)

#  else /* !BSD_TIME && !LINUX && !NN_PLATFORM_CTR && !MSWIN32 */
#    include <time.h>
#    if defined(FREEBSD) && !defined(CLOCKS_PER_SEC)
#      include <machine/limits.h>
#      define CLOCKS_PER_SEC CLK_TCK
#    endif
#    if !defined(CLOCKS_PER_SEC)
/*
 * This is technically a bug in the implementation.
 * ANSI requires that `CLOCKS_PER_SEC` be defined.  But at least under
 * SunOS 4.1.1, it is not.  Also note that the combination of ANSI C
 * and POSIX is incredibly gross here.  The type `clock_t` is used by
 * both `clock()` and `times()`.  But on some machines these use
 * different notions of a clock tick, `CLOCKS_PER_SEC` seems to apply
 * only to `clock()`.  Hence we use it here.  On many machines,
 * including SunOS, `clock()` actually uses units of microseconds (that
 * are not really clock ticks).
 */
#      define CLOCKS_PER_SEC 1000000
#    endif
#    define CLOCK_TYPE clock_t
#    define GET_TIME(x) (void)(x = clock())
#    define MS_TIME_DIFF(a, b)                            \
      (CLOCKS_PER_SEC % 1000 == 0                         \
           ? (unsigned long)((a) - (b))                   \
                 / (unsigned long)(CLOCKS_PER_SEC / 1000) \
           : ((unsigned long)((a) - (b)) * 1000)          \
                 / (unsigned long)CLOCKS_PER_SEC)
/*
 * Avoid using `double` type since some targets (like ARM) might
 * require `-lm` option for `double`-to-`long` conversion.
 */
#    define NS_FRAC_TIME_DIFF(a, b)                                         \
      (CLOCKS_PER_SEC <= 1000                                               \
           ? 0UL                                                            \
           : (unsigned long)(CLOCKS_PER_SEC <= (clock_t)1000000UL           \
                                 ? (((a) - (b))                             \
                                    * ((clock_t)1000000UL / CLOCKS_PER_SEC) \
                                    % 1000)                                 \
                                       * 1000                               \
                                 : (CLOCKS_PER_SEC                          \
                                            <= (clock_t)1000000UL * 1000    \
                                        ? ((a) - (b))                       \
                                              * ((clock_t)1000000UL * 1000  \
                                                 / CLOCKS_PER_SEC)          \
                                        : (((a) - (b)) * (clock_t)1000000UL \
                                           * 1000)                          \
                                              / CLOCKS_PER_SEC)             \
                                       % (clock_t)1000000UL))
#  endif /* !BSD_TIME && !MSWIN32 */
#  ifndef CLOCK_TYPE_INITIALIZER
/*
 * This is used to initialize `CLOCK_TYPE` variables (to some value)
 * to avoid "variable might be uninitialized" compiler warnings.
 */
#    define CLOCK_TYPE_INITIALIZER 0
#  endif
#endif /* !NO_CLOCK */

/* We use `bzero()` and `bcopy()` internally.  They may not be available. */
#if defined(M68K) && defined(NEXT) || defined(VAX)
#  define BCOPY_EXISTS
#elif defined(DARWIN)
#  include <string.h>
#  define BCOPY_EXISTS
#endif

#if !defined(BCOPY_EXISTS) || defined(CPPCHECK)
#  include <string.h>
#  define BCOPY(x, y, n) memcpy(y, x, (size_t)(n))
#  define BZERO(x, n) memset(x, 0, (size_t)(n))
#else
#  define BCOPY(x, y, n) bcopy((void *)(x), (void *)(y), (size_t)(n))
#  define BZERO(x, n) bzero((void *)(x), (size_t)(n))
#endif

EXTERN_C_BEGIN

#if defined(CPPCHECK) && defined(ANY_MSWIN)
#  undef TEXT
#  ifdef UNICODE
#    define TEXT(s) L##s
#  else
#    define TEXT(s) s
#  endif
#endif /* CPPCHECK && ANY_MSWIN */

/* Stop and restart mutator threads. */
#if defined(NN_PLATFORM_CTR) || defined(NINTENDO_SWITCH) \
    || defined(GC_WIN32_THREADS) || defined(GC_PTHREADS)
GC_INNER void GC_stop_world(void);
GC_INNER void GC_start_world(void);
#  define STOP_WORLD() GC_stop_world()
#  define START_WORLD() GC_start_world()
#else
/* Just do a sanity check: we are not inside `GC_do_blocking()`. */
#  define STOP_WORLD() GC_ASSERT(GC_blocked_sp == NULL)
#  define START_WORLD()
#endif

/* Abandon ship. */
#ifdef SMALL_CONFIG
#  define GC_on_abort(msg) (void)0 /*< be silent on abort */
#else
GC_API_PRIV GC_abort_func GC_on_abort;
#endif
#if defined(CPPCHECK)
#  define ABORT(msg)    \
    {                   \
      GC_on_abort(msg); \
      abort();          \
    }
#else
#  if defined(MSWIN_XBOX1) && !defined(DebugBreak)
#    define DebugBreak() __debugbreak()
#  elif defined(MSWINCE) && !defined(DebugBreak) \
      && (!defined(UNDER_CE) || (defined(__MINGW32CE__) && !defined(ARM32)))
/*
 * This simplifies linking for WinCE (and, probably, does not
 * hurt debugging much); use `-D DebugBreak=DebugBreak` to override
 * this behavior if really needed.  This is also a workaround for
 * x86mingw32ce toolchain (if it is still declaring `DebugBreak()`
 * instead of defining it as a macro).
 */
#    define DebugBreak() _exit(-1) /*< there is no `abort()` in WinCE */
#  endif
#  if defined(MSWIN32) && (defined(NO_DEBUGGING) || defined(LINT2))
/*
 * A more user-friendly abort after showing fatal message.
 * Exit on error without running "at-exit" callbacks.
 */
#    define ABORT(msg) (GC_on_abort(msg), _exit(-1))
#  elif defined(MSWINCE) && defined(NO_DEBUGGING)
#    define ABORT(msg) (GC_on_abort(msg), ExitProcess(-1))
#  elif defined(MSWIN32) || defined(MSWINCE)
#    if defined(_CrtDbgBreak) && defined(_DEBUG) && defined(_MSC_VER)
#      define ABORT(msg)                          \
        {                                         \
          GC_on_abort(msg);                       \
          _CrtDbgBreak() /*< `__debugbreak()` */; \
        }
#    else
#      define ABORT(msg)    \
        {                   \
          GC_on_abort(msg); \
          DebugBreak();     \
        }
/*
 * Note: on a WinCE box, this could be silently ignored (i.e., the program
 * is not aborted); `DebugBreak()` is a statement in some toolchains.
 */
#    endif
#  else /* !MSWIN32 */
#    define ABORT(msg) (GC_on_abort(msg), abort())
#  endif
#endif /* !CPPCHECK */

/*
 * For the abort message with 1 .. 3 arguments.  `C_msg` and `C_fmt`
 * should be literals.  `C_msg` should not contain format specifiers.
 * Arguments should match their format specifiers.
 */
#define ABORT_ARG1(C_msg, C_fmt, arg1)               \
  MACRO_BLKSTMT_BEGIN                                \
  GC_ERRINFO_PRINTF(C_msg /* + */ C_fmt "\n", arg1); \
  ABORT(C_msg);                                      \
  MACRO_BLKSTMT_END
#define ABORT_ARG2(C_msg, C_fmt, arg1, arg2)               \
  MACRO_BLKSTMT_BEGIN                                      \
  GC_ERRINFO_PRINTF(C_msg /* + */ C_fmt "\n", arg1, arg2); \
  ABORT(C_msg);                                            \
  MACRO_BLKSTMT_END
#define ABORT_ARG3(C_msg, C_fmt, arg1, arg2, arg3)               \
  MACRO_BLKSTMT_BEGIN                                            \
  GC_ERRINFO_PRINTF(C_msg /* + */ C_fmt "\n", arg1, arg2, arg3); \
  ABORT(C_msg);                                                  \
  MACRO_BLKSTMT_END

/*
 * Same as `ABORT` but does not have a `noreturn` attribute.
 * I.e. `ABORT` on a dummy condition (which is always true).
 */
#define ABORT_RET(msg)                                                \
  if ((GC_funcptr_uint)GC_current_warn_proc == ~(GC_funcptr_uint)0) { \
  } else                                                              \
    ABORT(msg)

/*
 * Exit process abnormally, but without making a mess (e.g. out of memory).
 * Note: some compilers (e.g. `lcc-e2k`) report a warning if a function with
 * a `noreturn` attribute (`exit()` in this case) is used in an expression,
 * thus we use `do { ... } while (0)` instead of `(GC_on_abort(0), exit(1))`.
 */
#define EXIT()                    \
  do {                            \
    GC_on_abort(NULL);            \
    exit(1 /* `EXIT_FAILURE` */); \
  } while (0)

/*
 * Print warning message, e.g. almost out of memory.  The argument (if any)
 * format specifier should be: "%s", "%p", "%"`WARN_PRIdPTR` or
 * "%"`WARN_PRIuPTR`.
 */
#define WARN(msg, arg) \
  GC_current_warn_proc("GC Warning: " msg, (GC_uintptr_t)(arg))
GC_EXTERN GC_warn_proc GC_current_warn_proc;

/*
 * Print format type macro for decimal `GC_signed_word` value passed to
 * `WARN()`.  This could be redefined for Win64 or LLP64, but typically
 * should not be done as the `WARN` format string is, possibly,
 * processed on the client side, so non-standard print type modifiers
 * (like MS "I64d") should be avoided here if possible.
 * TODO: Assuming `sizeof(void *)` is equal to `sizeof(long)` or this
 * is a little-endian machine.
 */
#ifndef WARN_PRIdPTR
#  define WARN_PRIdPTR "ld"
#  define WARN_PRIuPTR "lu"
#endif

/*
 * A tagging macro (for a code static analyzer) to indicate that the
 * string obtained from an untrusted source (e.g., `argv[]`, `getenv`)
 * is safe to use in a vulnerable operation (e.g., `open`, `exec`).
 */
#define TRUSTED_STRING(s) COVERT_DATAFLOW_P(s)

#ifdef GC_READ_ENV_FILE
/*
 * This routine scans `GC_envfile_content` for the specified environment
 * variable (and returns its value if found).
 */
GC_INNER char *GC_envfile_getenv(const char *name);

/* Get the process environment entry. */
#  define GETENV(name) GC_envfile_getenv(name)
#elif defined(NO_GETENV) && !defined(CPPCHECK)
#  define GETENV(name) NULL
#elif defined(EMPTY_GETENV_RESULTS) && !defined(CPPCHECK)
/* Workaround for a reputed Wine bug. */
GC_INLINE char *
fixed_getenv(const char *name)
{
  char *value = getenv(name);
  return value != NULL && *value != '\0' ? value : NULL;
}
#  define GETENV(name) fixed_getenv(name)
#else
#  define GETENV(name) getenv(name)
#endif

EXTERN_C_END

#if defined(DARWIN)
#  include <mach/thread_status.h>
#  ifndef MAC_OS_X_VERSION_MAX_ALLOWED
/* Include this header just to import the above macro. */
#    include <AvailabilityMacros.h>
#  endif
#  if defined(POWERPC)
#    if CPP_WORDSZ == 32
#      define GC_THREAD_STATE_T ppc_thread_state_t
#    else
#      define GC_THREAD_STATE_T ppc_thread_state64_t
#      define GC_MACH_THREAD_STATE PPC_THREAD_STATE64
#      define GC_MACH_THREAD_STATE_COUNT PPC_THREAD_STATE64_COUNT
#    endif
#  elif defined(I386) || defined(X86_64)
#    if CPP_WORDSZ == 32
#      if defined(i386_THREAD_STATE_COUNT) \
          && !defined(x86_THREAD_STATE32_COUNT)
/* Use old naming convention for i686. */
#        define GC_THREAD_STATE_T i386_thread_state_t
#        define GC_MACH_THREAD_STATE i386_THREAD_STATE
#        define GC_MACH_THREAD_STATE_COUNT i386_THREAD_STATE_COUNT
#      else
#        define GC_THREAD_STATE_T x86_thread_state32_t
#        define GC_MACH_THREAD_STATE x86_THREAD_STATE32
#        define GC_MACH_THREAD_STATE_COUNT x86_THREAD_STATE32_COUNT
#      endif
#    else
#      define GC_THREAD_STATE_T x86_thread_state64_t
#      define GC_MACH_THREAD_STATE x86_THREAD_STATE64
#      define GC_MACH_THREAD_STATE_COUNT x86_THREAD_STATE64_COUNT
#    endif
#  elif defined(ARM32) && defined(ARM_UNIFIED_THREAD_STATE) \
      && !defined(CPPCHECK)
#    define GC_THREAD_STATE_T arm_unified_thread_state_t
#    define GC_MACH_THREAD_STATE ARM_UNIFIED_THREAD_STATE
#    define GC_MACH_THREAD_STATE_COUNT ARM_UNIFIED_THREAD_STATE_COUNT
#  elif defined(ARM32)
#    define GC_THREAD_STATE_T arm_thread_state_t
#    ifdef ARM_MACHINE_THREAD_STATE_COUNT
#      define GC_MACH_THREAD_STATE ARM_MACHINE_THREAD_STATE
#      define GC_MACH_THREAD_STATE_COUNT ARM_MACHINE_THREAD_STATE_COUNT
#    endif
#  elif defined(AARCH64)
#    define GC_THREAD_STATE_T arm_thread_state64_t
#    define GC_MACH_THREAD_STATE ARM_THREAD_STATE64
#    define GC_MACH_THREAD_STATE_COUNT ARM_THREAD_STATE64_COUNT
#  elif !defined(CPPCHECK)
#    error define GC_THREAD_STATE_T
#  endif
#  ifndef GC_MACH_THREAD_STATE
#    define GC_MACH_THREAD_STATE MACHINE_THREAD_STATE
#    define GC_MACH_THREAD_STATE_COUNT MACHINE_THREAD_STATE_COUNT
#  endif

/*
 * Try to work out the right way to access thread state structure members.
 * The structure has different definition in different Darwin versions.
 * This now defaults to the (older) names without `__`, thus hopefully
 * not breaking any existing `Makefile.direct` builds.
 */
#  if __DARWIN_UNIX03
#    define THREAD_FLD_NAME(x) __##x
#  else
#    define THREAD_FLD_NAME(x) x
#  endif
#  if defined(ARM32) && defined(ARM_UNIFIED_THREAD_STATE)
#    define THREAD_FLD(x) ts_32.THREAD_FLD_NAME(x)
#  else
#    define THREAD_FLD(x) THREAD_FLD_NAME(x)
#  endif
#endif /* DARWIN */

#ifndef WASI
#  include <setjmp.h>
#endif

#include <stdio.h>

#if defined(CAN_HANDLE_FORK) && defined(GC_PTHREADS)
#  include <pthread.h> /*< for `pthread_t` */
#endif

#if defined(MPROTECT_VDB) && defined(DARWIN)
#  include <mach/mach.h> /*< for `mach_port_t`, at least */
#endif

#if __STDC_VERSION__ >= 201112L
#  include <assert.h> /*< for `static_assert` */
#endif

EXTERN_C_BEGIN

/* Definitions depending on `word` size. */

#define modWORDSZ(n) ((n) & (CPP_WORDSZ - 1)) /*< `n mod size_of_word` */
#define divWORDSZ(n) ((n) / CPP_WORDSZ)

#define SIGNB ((word)1 << (CPP_WORDSZ - 1))
#define SIZET_SIGNB (GC_SIZE_MAX ^ (GC_SIZE_MAX >> 1))

#if CPP_PTRSZ / 8 != ALIGNMENT
#  define UNALIGNED_PTRS
#endif

#define BYTES_TO_GRANULES(lb) ((lb) / GC_GRANULE_BYTES)
#define GRANULES_TO_BYTES(lg) (GC_GRANULE_BYTES * (lg))
#define BYTES_TO_PTRS(lb) ((lb) / sizeof(ptr_t))
#define PTRS_TO_BYTES(lpw) ((lpw) * sizeof(ptr_t))
#define GRANULES_TO_PTRS(lg) (GC_GRANULE_PTRS * (lg))

/*
 * Convert size in bytes to that in pointers rounding up (but not adding
 * extra byte at end).
 */
#define BYTES_TO_PTRS_ROUNDUP(lb) BYTES_TO_PTRS((lb) + sizeof(ptr_t) - 1)

/* Size parameters. */

/*
 * Heap block size, in bytes.  Should be a power of two.
 * Incremental collection with `MPROTECT_VDB` currently requires the
 * page size to be a multiple of `HBLKSIZE`.  Since most modern
 * architectures support variable page sizes down to 4 KB, and i686 and
 * x86_64 are generally 4 KB, we now default to 4 KB, except for:
 *   - Alpha: seems to be used with 8 KB pages;
 *   - `SMALL_CONFIG`: want less block-level fragmentation.
 */
#ifndef HBLKSIZE
#  if defined(SMALL_CONFIG) && !defined(LARGE_CONFIG)
#    define CPP_LOG_HBLKSIZE 10
#  elif defined(ALPHA)
#    define CPP_LOG_HBLKSIZE 13
#  else
#    define CPP_LOG_HBLKSIZE 12
#  endif
#else
#  if HBLKSIZE == 512
#    define CPP_LOG_HBLKSIZE 9
#  elif HBLKSIZE == 1024
#    define CPP_LOG_HBLKSIZE 10
#  elif HBLKSIZE == 2048
#    define CPP_LOG_HBLKSIZE 11
#  elif HBLKSIZE == 4096
#    define CPP_LOG_HBLKSIZE 12
#  elif HBLKSIZE == 8192
#    define CPP_LOG_HBLKSIZE 13
#  elif HBLKSIZE == 16384
#    define CPP_LOG_HBLKSIZE 14
#  elif HBLKSIZE == 32768
#    define CPP_LOG_HBLKSIZE 15
#  elif HBLKSIZE == 65536
#    define CPP_LOG_HBLKSIZE 16
#  elif !defined(CPPCHECK)
#    error Bad HBLKSIZE value
#  endif
#  undef HBLKSIZE
#endif

#define LOG_HBLKSIZE ((size_t)CPP_LOG_HBLKSIZE)
#define HBLKSIZE ((size_t)1 << CPP_LOG_HBLKSIZE)

#define GC_SQRT_SIZE_MAX ((((size_t)1) << (sizeof(size_t) * 8 / 2)) - 1)

/*
 * Maximum size of objects supported by free list (larger objects are
 * allocated directly with `GC_alloc_large()`, by rounding to the next
 * multiple of `HBLKSIZE`).
 */
#define MAXOBJBYTES (HBLKSIZE >> 1)
#define MAXOBJGRANULES BYTES_TO_GRANULES(MAXOBJBYTES)

#define divHBLKSZ(n) ((n) >> LOG_HBLKSIZE)

/*
 * Equivalent to subtracting one `hblk` pointer from another.  We do it
 * this way because a compiler should find it hard to use an integer
 * division instead of a shift.  The bundled SunOS 4.1 otherwise sometimes
 * pessimizes the subtraction to involve a call to `.div`.
 */
#define HBLK_PTR_DIFF(p, q) divHBLKSZ((ptr_t)p - (ptr_t)q)

#define modHBLKSZ(n) ((n) & (HBLKSIZE - 1))

#define HBLKPTR(objptr) \
  ((struct hblk *)PTR_ALIGN_DOWN((ptr_t)(objptr), HBLKSIZE))
#define HBLKDISPL(objptr) modHBLKSZ((size_t)ADDR(objptr))

/* Same as `HBLKPTR` but points to the first block in the page. */
#define HBLK_PAGE_ALIGNED(objptr) \
  ((struct hblk *)PTR_ALIGN_DOWN((ptr_t)(objptr), GC_page_size))

/* Round up allocation size (in bytes) to a multiple of a granule. */
#define ROUNDUP_GRANULE_SIZE(lb) /*< `lb` should have no side-effect */ \
  (SIZET_SAT_ADD(lb, GC_GRANULE_BYTES - 1) & ~(size_t)(GC_GRANULE_BYTES - 1))

/*
 * Round up byte allocation request (after adding `EXTRA_BYTES`) to
 * a multiple of a granule, then convert it to granules.
 */
#define ALLOC_REQUEST_GRANS(lb) /*< `lb` should have no side-effect */ \
  BYTES_TO_GRANULES(SIZET_SAT_ADD(lb, GC_GRANULE_BYTES - 1 + EXTRA_BYTES))

#if MAX_EXTRA_BYTES == 0
#  define ADD_EXTRA_BYTES(lb) (lb)
#  define SMALL_OBJ(lb) LIKELY((lb) <= MAXOBJBYTES)
#else
#  define ADD_EXTRA_BYTES(lb) /*< `lb` should have no side-effect */ \
    SIZET_SAT_ADD(lb, EXTRA_BYTES)

/*
 * This really just tests that `lb` is not greater than
 * `MAXOBJBYTES - EXTRA_BYTES`, but we try to avoid looking up `EXTRA_BYTES`.
 */
#  define SMALL_OBJ(lb) /*< `lb` should have no side-effect */ \
    (LIKELY((lb) <= MAXOBJBYTES - MAX_EXTRA_BYTES)             \
     || (lb) <= MAXOBJBYTES - EXTRA_BYTES)
#endif

/*
 * Hash table representation of sets of pages.  Implements a map from
 * `HBLKSIZE`-aligned chunks of the address space to one bit each.
 * This assumes it is OK to spuriously set bits, e.g. because multiple
 * addresses are represented by a single location.  Used by
 * black-listing code, and perhaps by dirty bit maintenance code.
 */
#ifndef LOG_PHT_ENTRIES
#  ifdef LARGE_CONFIG
#    if CPP_WORDSZ == 32
/*
 * Collisions are impossible (because of a 4 GB space limit).
 * Each table takes 128 KB, some of which may never be touched.
 */
#      define LOG_PHT_ENTRIES 20
#    else
/*
 * Collisions likely at 2M blocks, which is greater than 8 GB.
 * Each table takes 256 KB, some of which may never be touched.
 */
#      define LOG_PHT_ENTRIES 21
#    endif
#  elif !defined(SMALL_CONFIG)
/*
 * Collisions are likely if heap grows to more than 256K blocks,
 * which is greater than 1 GB.  Each hash table occupies 32 KB.
 * Even for somewhat smaller heaps, say half of that, collisions may
 * be an issue because we blacklist addresses outside the heap.
 */
#    define LOG_PHT_ENTRIES 18
#  else
/*
 * Collisions are likely if heap grows to more than 32K blocks,
 * which is 128 MB.  Each hash table occupies 4 KB.
 */
#    define LOG_PHT_ENTRIES 15
#  endif
#endif /* !LOG_PHT_ENTRIES */

#define PHT_ENTRIES (1 << LOG_PHT_ENTRIES)
#define PHT_SIZE (PHT_ENTRIES > CPP_WORDSZ ? PHT_ENTRIES / CPP_WORDSZ : 1)
typedef word page_hash_table[PHT_SIZE];

#define PHT_HASH(p) ((size_t)((ADDR(p) >> LOG_HBLKSIZE) & (PHT_ENTRIES - 1)))

#define get_pht_entry_from_index(bl, index) \
  (((bl)[divWORDSZ(index)] >> modWORDSZ(index)) & 1)
#define set_pht_entry_from_index(bl, index) \
  (void)((bl)[divWORDSZ(index)] |= (word)1 << modWORDSZ(index))

#if defined(THREADS) && defined(AO_HAVE_or)
/*
 * And, one more variant for `GC_add_to_black_list_normal` and
 *`GC_add_to_black_list_stack` (invoked indirectly by `GC_do_local_mark()`)
 * and `async_set_pht_entry_from_index()` (invoked by `GC_dirty()` or the
 * write fault handler).
 */
#  define set_pht_entry_from_index_concurrent(bl, index) \
    AO_or((volatile AO_t *)&(bl)[divWORDSZ(index)],      \
          (AO_t)1 << modWORDSZ(index))
#  ifdef MPROTECT_VDB
#    define set_pht_entry_from_index_concurrent_volatile(bl, index) \
      set_pht_entry_from_index_concurrent(bl, index)
#  endif
#else
#  define set_pht_entry_from_index_concurrent(bl, index) \
    set_pht_entry_from_index(bl, index)
#  ifdef MPROTECT_VDB
/*
 * Same as `set_pht_entry_from_index` but avoiding the compound assignment
 * for a `volatile` array.
 */
#    define set_pht_entry_from_index_concurrent_volatile(bl, index) \
      (void)((bl)[divWORDSZ(index)]                                 \
             = (bl)[divWORDSZ(index)] | ((word)1 << modWORDSZ(index)))
#  endif
#endif

/* Heap blocks. */

/*
 * The upper bound.  We allocate 1 bit per allocation granule.
 * If `MARK_BIT_PER_OBJ` is not defined, we use every `n`-th bit, where
 * `n` is the number of allocation granules per object.  Otherwise, we only
 * use the initial group of mark bits, and it is safe to allocate smaller
 * header for large objects.
 */
#define MARK_BITS_PER_HBLK (HBLKSIZE / GC_GRANULE_BYTES)

#ifndef MARK_BIT_PER_OBJ
/*
 * We maintain layout maps for heap blocks containing objects of
 * a given size.  Each entry in this map describes a byte offset
 * (displacement) and has the following type.
 */
#  if (1 << (CPP_LOG_HBLKSIZE - 1)) / GC_GRANULE_BYTES <= 0x100
typedef unsigned char hb_map_entry_t;
#  else
typedef unsigned short hb_map_entry_t;
#  endif
#endif /* !MARK_BIT_PER_OBJ */

struct hblkhdr {
  /*
   * Link field for `hblk` free list and for lists of chunks waiting to
   * be reclaimed.
   */
  struct hblk *hb_next;

  struct hblk *hb_prev; /*< backwards link for free list */

  struct hblk *hb_block; /*< the corresponding block */

  /*
   * Kind of objects in the block.  Each kind identifies a mark
   * procedure and a set of list headers.  Sometimes called regions.
   */
  unsigned char hb_obj_kind;

  unsigned char hb_flags;

  /* Ignore pointers that do not point to the first `hblk` of this object. */
#define IGNORE_OFF_PAGE 1

  /*
   * This is a free block, which has been unmapped from the address space.
   * `GC_remap()` must be invoked on it before it can be reallocated.
   * Set only if `USE_MUNMAP` macro is defined.
   */
#define WAS_UNMAPPED 2

  /* Block is free, i.e. not in use. */
#define FREE_BLK 4

#ifdef ENABLE_DISCLAIM
  /* This kind has a callback on reclaim. */
#  define HAS_DISCLAIM 8

  /*
   * Mark from all objects, marked or not.  Used to mark objects needed
   * by reclaim notifier.
   */
#  define MARK_UNCONDITIONALLY 0x10
#endif

#ifndef MARK_BIT_PER_OBJ
#  define LARGE_BLOCK 0x20
#endif

  /*
   * Value of `GC_gc_no` when block was last allocated or swept.
   * May wrap.  For a free block, this is maintained only for `USE_MUNMAP`,
   * and indicates when the header was allocated, or when the size of the
   * block last changed.
   */
  unsigned short hb_last_reclaimed;

#ifdef MARK_BIT_PER_OBJ
#  define LARGE_INV_SZ ((unsigned32)1 << 16)

  /*
   * A good upper bound for `2**32 / hb_sz`.
   * For large objects, we use `LARGE_INV_SZ`.
   */
  unsigned32 hb_inv_sz;
#endif

  /*
   * If in use, size in bytes, of objects in the block.
   * Otherwise, the size of the whole free block.  We assume that this is
   * convertible to `GC_signed_word` without generating a negative result.
   * We avoid generating free blocks larger than that.
   */
  size_t hb_sz;

  /* Object descriptor for marking.  See `gc_mark.h` file. */
  word hb_descr;

#ifndef MARK_BIT_PER_OBJ
  /*
   * A table of remainders `mod BYTES_TO_GRANULES(hb_sz)` essentially,
   * except for large blocks.  See `GC_obj_map`.
   */
  hb_map_entry_t *hb_map;
#endif

#ifdef PARALLEL_MARK
  /*
   * Number of set mark bits, excluding the one always set at the end.
   * Currently it is updated concurrently and hence only approximate.
   * But a zero value does guarantee that the block contains
   * no marked objects.  Ensuring this property means that we never
   * decrement it to zero during a collection, and hence the count may
   * be one too high.  Due to concurrent updates, an arbitrary number
   * of increments, but not all of them (!) may be lost, hence it may,
   * in theory, be much too low.  The count may also be too high
   * if multiple mark threads mark the same object due to a race.
   */
  volatile AO_t hb_n_marks;
#else
  /* Without parallel marking, the count is accurate. */
  size_t hb_n_marks;
#endif

#ifdef USE_MARK_BYTES
  /*
   * Unlike the other case, this is in units of bytes.  Since we force
   * certain alignment, we need at most one mark bit per a granule.
   * But we do allocate and set one extra mark bit to avoid
   * an explicit check for the partial object at the end of each block.
   */
#  define HB_MARKS_SZ (MARK_BITS_PER_HBLK + 1)
  union {
    /*
     * The `i`-th byte is 1 if the object starting at granule `i`
     * or object `i` is marked, 0 otherwise.  The mark bit for the
     * "one past the end" object is always set to avoid a special case
     * test in the marker.
     */
    char _hb_marks[HB_MARKS_SZ];
    word dummy; /*< force word alignment of mark bytes */
  } _mark_byte_union;
#  define hb_marks _mark_byte_union._hb_marks
#else
#  define HB_MARKS_SZ (MARK_BITS_PER_HBLK / CPP_WORDSZ + 1)

#  if defined(PARALLEL_MARK) || (defined(THREAD_SANITIZER) && defined(THREADS))
  volatile AO_t hb_marks[HB_MARKS_SZ];
#  else
  word hb_marks[HB_MARKS_SZ];
#  endif
#endif /* !USE_MARK_BYTES */
};

/* A "random" mark bit index for assertions. */
#define ANY_INDEX 23

/* Heap block body. */

#define HBLK_WORDS (HBLKSIZE / sizeof(word))
#define HBLK_GRANULES (HBLKSIZE / GC_GRANULE_BYTES)

/*
 * The number of objects in a block dedicated to a certain size.
 * May erroneously yield zero (instead of one) for large objects.
 */
#define HBLK_OBJS(sz_in_bytes) (HBLKSIZE / (sz_in_bytes))

struct hblk {
  char hb_body[HBLKSIZE];
};

#define HBLK_IS_FREE(hhdr) (((hhdr)->hb_flags & FREE_BLK) != 0)

#define OBJ_SZ_TO_BLOCKS(lb) divHBLKSZ((lb) + HBLKSIZE - 1)

/*
 * Size of block (in units of `HBLKSIZE`) needed to hold objects of
 * given `lb` (in bytes).  The checked variant prevents wrap around.
 */
#define OBJ_SZ_TO_BLOCKS_CHECKED(lb) /*< `lb` should have no side-effect */ \
  divHBLKSZ(SIZET_SAT_ADD(lb, HBLKSIZE - 1))

/* The object free-list link. */
#define obj_link(p) (*(void **)(p))

/*
 * Root sets.  Logically private to `mark_rts.c` file.  But we do not
 * want the tables scanned, so we put them here.
 */

/* The maximum number of ranges that can be registered as static roots. */
#ifdef LARGE_CONFIG
#  define MAX_ROOT_SETS 8192
#elif !defined(SMALL_CONFIG)
#  define MAX_ROOT_SETS 2048
#else
#  define MAX_ROOT_SETS 512
#endif

/* Maximum number of segments that can be excluded from root sets. */
#define MAX_EXCLUSIONS (MAX_ROOT_SETS / 4)

/* A data structure for excluded static roots. */
struct exclusion {
  ptr_t e_start;
  ptr_t e_end;
};

/*
 * A data structure for list of root sets.  We keep a hash table, so that
 * we can filter out duplicate additions.  Under Win32, we need to do
 * a better job of filtering overlaps, so we resort to sequential search,
 * and pay the price.
 */
struct roots {
  ptr_t r_start; /*< multiple of pointer size */
  ptr_t r_end;   /*< multiple of pointer size and greater than `r_start` */
#ifndef ANY_MSWIN
  struct roots *r_next;
#endif
  /* Delete before registering new dynamic libraries if set. */
  GC_bool r_tmp;
};

#ifndef ANY_MSWIN
/* Size of hash table index to roots. */
#  define LOG_RT_SIZE 6

/* `RT_SIZE` should be a power of 2, may be not equal to `MAX_ROOT_SETS`. */
#  define RT_SIZE (1 << LOG_RT_SIZE)
#endif

#if (!defined(MAX_HEAP_SECTS) || defined(CPPCHECK)) \
    && (defined(ANY_MSWIN) || defined(USE_PROC_FOR_LIBRARIES))
#  ifdef LARGE_CONFIG
#    if CPP_WORDSZ > 32
#      define MAX_HEAP_SECTS 81920
#    else
#      define MAX_HEAP_SECTS 7680
#    endif
#  elif defined(SMALL_CONFIG) && !defined(USE_PROC_FOR_LIBRARIES)
#    if defined(PARALLEL_MARK) && (defined(CYGWIN) || defined(MSWIN32))
#      define MAX_HEAP_SECTS 384
#    else
#      define MAX_HEAP_SECTS 128 /*< roughly 256 MB (`128 * 2048 * 1024`) */
#    endif
#  elif CPP_WORDSZ > 32
#    define MAX_HEAP_SECTS 1024 /*< roughly 8 GB */
#  else
#    define MAX_HEAP_SECTS 512 /*< roughly 4 GB */
#  endif
#endif /* !MAX_HEAP_SECTS */

#ifdef GWW_VDB
/*
 * Note: this is still susceptible to overflow, if there are very large
 * allocations, and everything is dirty.
 */
#  define GC_GWW_BUF_LEN (MAXHINCR * HBLKSIZE / 4096 /* x86 page size */)
#endif

#if !defined(NO_FIND_LEAK) && !defined(MAX_LEAKED)
#  define MAX_LEAKED 40
#endif

#if !defined(SHORT_DBG_HDRS) && !defined(MAX_SMASHED)
#  define MAX_SMASHED 20
#endif

typedef struct GC_ms_entry {
  ptr_t mse_start; /*< beginning of object, pointer-aligned one */
#ifdef PARALLEL_MARK
  volatile AO_t mse_descr;
#else
  /*
   * The descriptor; the low-order two bits are tags, as described
   * in `gc_mark.h` file.
   */
  word mse_descr;
#endif
} mse;

/* This is used by `GC_call_with_gc_active`, `GC_push_all_stack_sections`. */
struct GC_traced_stack_sect_s {
  ptr_t saved_stack_ptr;
#ifdef IA64
  ptr_t saved_backing_store_ptr;
  ptr_t backing_store_end;
#endif
  struct GC_traced_stack_sect_s *prev;
};

/*
 * Current state of marking.  Used to remember where we are during the
 * concurrent marking.
 */
typedef int mark_state_t;

struct disappearing_link;
struct finalizable_object;

struct dl_hashtbl_s {
  struct disappearing_link **head;
  size_t entries;
  unsigned log_size;
};

struct fnlz_roots_s {
  struct finalizable_object **fo_head;
  /* List of objects that should be finalized now. */
  struct finalizable_object *finalize_now;
};

union toggle_ref_u {
  /* The least significant bit is used to distinguish between choices. */
  void *strong_ref;
  GC_hidden_pointer weak_ref;
};

/*
 * Extended descriptors.  `GC_typed_mark_proc` understands these.
 * These are used for simple objects that are larger than what can
 * be described by a `BITMAP_BITS`-sized bitmap.
 */
typedef struct {
  /*
   * The least significant bit corresponds to the first "pointer-sized"
   * word.
   */
  word ed_bitmap;
  GC_bool ed_continued; /*< next entry is continuation */
} typed_ext_descr_t;

struct HeapSect {
  ptr_t hs_start;
  size_t hs_bytes;
};

#if defined(USE_WINALLOC) && !defined(REDIRECT_MALLOC)
/* In the long run, a better data structure would also be nice... */
struct GC_malloc_heap_list {
  void *allocation_base;
  struct GC_malloc_heap_list *next;
};
#endif

#if defined(DYNAMIC_LOADING) && !defined(ANY_MSWIN) \
    && !defined(USE_PROC_FOR_LIBRARIES) && defined(HAVE_DL_ITERATE_PHDR)
struct load_segment_s {
  ptr_t start;
  ptr_t end;
  /*
   * The room for the second segment if we remove a `PT_GNU_RELRO` segment
   * from the middle.
   */
  ptr_t start2;
  ptr_t end2;
};
#  define MAX_LOAD_SEGS MAX_ROOT_SETS
#endif

#ifdef MAKE_BACK_GRAPH
/* The maximum in-degree we handle directly. */
#  define BACKGRAPH_MAX_IN 10

struct back_edges_s {
  /* Number of edges, including those in continuation structures. */
  word n_edges;

  unsigned short flags;

  /*
   * If `height` is greater than zero, then keeps the `GC_gc_no` value
   * when it was computed.  If it was computed this cycle, then it is
   * current.  If it was computed during the last cycle, then it belongs
   * to the old height, which is only saved for live objects referenced by
   * dead ones.  This may grow due to references from newly dead objects.
   */
  unsigned short height_gc_no;

  /*
   * Longest path through unreachable nodes to this node that we found
   * using depth first search.
   */
  GC_signed_word height;

  ptr_t edges[BACKGRAPH_MAX_IN];

  /*
   * Pointer to continuation structure; we use only the edges field in
   * the continuation.  Also used as a free-list link.
   */
  struct back_edges_s *cont;
};
#endif

/*
 * Lists of all heap blocks and free lists as well as other random data
 * structures that should not be scanned by the collector.  These are
 * grouped together in a structure so that they can be easily skipped by
 * `GC_push_conditional_with_exclusions()`.  The ordering is weird to
 * make `GC_malloc` faster by keeping the important fields sufficiently
 * close together that a single load of a base register will do.
 * Scalars that could easily appear to be pointers are also put here.
 * The main fields should precede any conditionally included fields, where
 * possible.
 */
struct _GC_arrays {
#define GC_heapsize GC_arrays._heapsize
  word _heapsize; /*< heap size in bytes (value never goes down) */

#define GC_requested_heapsize GC_arrays._requested_heapsize
  word _requested_heapsize; /*< heap size due to explicit expansion */

#ifndef NO_WARN_HEAP_GROW_WHEN_GC_DISABLED
#  define GC_heapsize_on_gc_disable GC_arrays._heapsize_on_gc_disable
  word _heapsize_on_gc_disable;
#endif

#define GC_heapsize_at_forced_unmap GC_arrays._heapsize_at_forced_unmap
  word _heapsize_at_forced_unmap; /*< accessed with the allocator lock held */

#define GC_last_heap_addr GC_arrays._last_heap_addr
  word _last_heap_addr;

  /*
   * Total bytes contained in blocks on the free list of large objects.
   * (A large object is the one that occupies a block of at least
   * two `HBLKSIZE`.)
   */
#define GC_large_free_bytes GC_arrays._large_free_bytes
  word _large_free_bytes;

  /* Total number of bytes in allocated large objects blocks. */
#define GC_large_allocd_bytes GC_arrays._large_allocd_bytes
  word _large_allocd_bytes;

  /*
   * Maximum number of bytes that were ever allocated in large object blocks.
   * This is used to help decide when it is safe to split up a large block.
   */
#define GC_max_large_allocd_bytes GC_arrays._max_large_allocd_bytes
  word _max_large_allocd_bytes;

  /* Number of bytes allocated before this collection cycle. */
#define GC_bytes_allocd_before_gc GC_arrays._bytes_allocd_before_gc
  word _bytes_allocd_before_gc;

#define GC_our_mem_bytes GC_arrays._our_mem_bytes
  word _our_mem_bytes;

  /* Number of bytes allocated during this collection cycle. */
#define GC_bytes_allocd GC_arrays._bytes_allocd
  word _bytes_allocd;

  /*
   * Number of black-listed bytes dropped during GC cycle as a result
   * of repeated scanning during allocation attempts.  These are treated
   * largely as allocated, even though they are not useful to the client.
   */
#define GC_bytes_dropped GC_arrays._bytes_dropped
  word _bytes_dropped;

  /*
   * Approximate number of bytes in objects (and headers) that became
   * ready for finalization in the last collection.
   */
#define GC_bytes_finalized GC_arrays._bytes_finalized
  word _bytes_finalized;

  /*
   * Number of explicitly deallocated bytes of memory since last
   * collection.
   */
#define GC_bytes_freed GC_arrays._bytes_freed
  word _bytes_freed;

  /*
   * Bytes of memory explicitly deallocated while finalizers were running.
   * Used to approximate size of memory explicitly deallocated by finalizers.
   */
#define GC_finalizer_bytes_freed GC_arrays._finalizer_bytes_freed
  word _finalizer_bytes_freed;

  /*
   * Pointer to the first (lowest address) `bottom_index` entity;
   * assumes the allocator lock is held.
   */
#define GC_all_bottom_indices GC_arrays._all_bottom_indices
  bottom_index *_all_bottom_indices;

  /*
   * Pointer to the last (highest address) `bottom_index` entity;
   * assumes the allocator lock is held.
   */
#define GC_all_bottom_indices_end GC_arrays._all_bottom_indices_end
  bottom_index *_all_bottom_indices_end;

#define GC_scratch_free_ptr GC_arrays._scratch_free_ptr
  ptr_t _scratch_free_ptr;

#define GC_hdr_free_list GC_arrays._hdr_free_list
  hdr *_hdr_free_list;

#define GC_scratch_end_addr GC_arrays._scratch_end_addr
  word _scratch_end_addr; /*< the end point of the current scratch area */

#if defined(IRIX5) || (defined(USE_PROC_FOR_LIBRARIES) && !defined(LINUX))
#  define USE_SCRATCH_LAST_END_PTR
  /*
   * The address of the end point of the last obtained scratch area.
   * Used by `GC_register_dynamic_libraries()`.
   */
#  define GC_scratch_last_end_addr GC_arrays._scratch_last_end_addr
  word _scratch_last_end_addr;
#endif

#if defined(GC_ASSERTIONS) || defined(MAKE_BACK_GRAPH) \
    || defined(INCLUDE_LINUX_THREAD_DESCR)             \
    || (defined(KEEP_BACK_PTRS) && ALIGNMENT == 1)
#  define SET_REAL_HEAP_BOUNDS

  /*
   * Similar to `GC_least_plausible_heap_addr` and
   * `GC_greatest_plausible_heap_addr` but do not include future
   * (potential) heap expansion.  Both variables are zero initially.
   */
#  define GC_least_real_heap_addr GC_arrays._least_real_heap_addr
#  define GC_greatest_real_heap_addr GC_arrays._greatest_real_heap_addr
  word _least_real_heap_addr;
  word _greatest_real_heap_addr;
#endif

#define GC_used_heap_size_after_full GC_arrays._used_heap_size_after_full
  word _used_heap_size_after_full;

  /* Number of explicitly managed bytes of storage at last collection. */
#define GC_non_gc_bytes_at_gc GC_arrays._non_gc_bytes_at_gc
  word _non_gc_bytes_at_gc;

#ifndef NO_BLACK_LISTING
  /*
   * Average number of bytes between black-listed blocks.  Approximate.
   * Counts only blocks that are "stack black-listed", i.e. that are
   * problematic in the interior of an object.
   */
#  define GC_black_list_spacing GC_arrays._black_list_spacing
  word _black_list_spacing;
#endif

#if !defined(ALWAYS_SMALL_CLEAR_STACK) && !defined(STACK_NOT_SCANNED) \
    && !defined(THREADS)
#  define GC_bytes_allocd_at_reset GC_arrays._bytes_allocd_at_reset
  word _bytes_allocd_at_reset;

  /* `GC_gc_no` value when we last did this. */
#  define GC_stack_last_cleared GC_arrays._stack_last_cleared
  word _stack_last_cleared;
#endif

  /* The number of extra calls to `GC_mark_some` that we have made. */
#define GC_mark_deficit GC_arrays._mark_deficit
  size_t _mark_deficit;

/* Number of attempts at finishing collection within `GC_time_limit`. */
#define GC_n_attempts GC_arrays._n_attempts
  int _n_attempts;

#define GC_n_partial_gcs GC_arrays._n_partial_gcs
  int _n_partial_gcs;

#ifndef NO_BLACK_LISTING
  /*
   * Counter of the cases when found block by `GC_allochblk_nth` is
   * black-listed completely.
   */
#  define GC_drop_blacklisted_count GC_arrays._drop_blacklisted_count
  unsigned _drop_blacklisted_count;

  /* Number of warnings suppressed so far. */
#  define GC_large_alloc_warn_suppressed GC_arrays._large_alloc_warn_suppressed
  long _large_alloc_warn_suppressed;

  /* Non-stack false references seen at last full collection. */
#  define GC_old_normal_bl GC_arrays._old_normal_bl
  word *_old_normal_bl;
  /* Non-stack false references seen since last full collection. */
#  define GC_incomplete_normal_bl GC_arrays._incomplete_normal_bl
  word *_incomplete_normal_bl;

#  define GC_old_stack_bl GC_arrays._old_stack_bl
#  define GC_incomplete_stack_bl GC_arrays._incomplete_stack_bl
  word *_old_stack_bl;
  word *_incomplete_stack_bl;
#endif

  /* The limits of stack for `GC_mark_some()` and friends. */
#define GC_mark_stack GC_arrays._mark_stack
#define GC_mark_stack_limit GC_arrays._mark_stack_limit
  mse *_mark_stack;
  mse *_mark_stack_limit;

  /*
   * All ranges between `GC_mark_stack` (incl.) and `GC_mark_stack_top`
   * (incl.) still need to be marked from.
   */
#define GC_mark_stack_top GC_arrays._mark_stack_top
#ifdef PARALLEL_MARK
  /* Updated only with the mark lock held, but read asynchronously. */
  mse *volatile _mark_stack_top;

#  define GC_mark_no GC_arrays._mark_no
  word _mark_no; /*< protected by the mark lock */

  /*
   * Number of bytes of memory allocated since we released the allocator lock.
   * Instead of reacquiring the allocator lock just to add this in, we add it
   * in the next time we reacquire the allocator lock.  (Atomically adding it
   * does not work, since we would have to atomically update it in
   * `GC_malloc`, which is too expensive.)
   */
#  define GC_bytes_allocd_tmp GC_arrays._bytes_allocd_tmp
  volatile AO_t _bytes_allocd_tmp;
#else
  mse *_mark_stack_top;
#endif

#if defined(USE_WINALLOC) && !defined(REDIRECT_MALLOC)
#  define GC_malloc_heap_l GC_arrays._malloc_heap_l
  struct GC_malloc_heap_list *_malloc_heap_l;
#endif

#ifdef GWW_VDB
#  define GC_gww_last_warned GC_arrays._gww_last_warned
  const struct hblk *_gww_last_warned;
#endif

#ifdef PROC_VDB
#  define GC_proc_buf GC_arrays._proc_buf
  char *_proc_buf;
#endif

#ifdef SOFT_VDB
#  define GC_soft_vdb_buf GC_arrays._soft_vdb_buf
#  define GC_pagemap_buf_len GC_arrays._pagemap_buf_len
#  define GC_pagemap_buf_fpos GC_arrays._pagemap_buf_fpos
#  define GC_pagemap_fd GC_arrays._pagemap_fd
  ptr_t _soft_vdb_buf;
  size_t _pagemap_buf_len;
  off_t _pagemap_buf_fpos; /*< valid only if `_pagemap_buf_len > 0` */
  int _pagemap_fd;
#endif

#if !defined(THREADS) && (defined(PROC_VDB) || defined(SOFT_VDB))
  /* `pid` used to compose `/proc` file names. */
#  define GC_saved_proc_pid GC_arrays._saved_proc_pid
  pid_t _saved_proc_pid;
#endif

#ifdef DYNAMIC_POINTER_MASK
  /*
   * Both mask and shift are zeros by default; if mask is zero, then
   * correct it to ~0 at the collector initialization.
   */
#  define GC_pointer_mask GC_arrays._pointer_mask
#  define GC_pointer_shift GC_arrays._pointer_shift
  word _pointer_mask;
  unsigned char _pointer_shift;
#endif

#ifdef THREADS
#  ifdef USE_SPIN_LOCK
#    define GC_allocate_lock GC_arrays._allocate_lock
  volatile AO_TS_t _allocate_lock;
#  endif
#  if !defined(HAVE_LOCKFREE_AO_OR) && defined(AO_HAVE_test_and_set_acquire) \
      && (!defined(NO_MANUAL_VDB) || defined(MPROTECT_VDB))
#    define NEED_FAULT_HANDLER_LOCK
#    define GC_fault_handler_lock GC_arrays._fault_handler_lock
  volatile AO_TS_t _fault_handler_lock;
#  endif
#  if !defined(GC_NO_THREADS_DISCOVERY) && defined(GC_WIN32_THREADS)
#    define GC_dll_main_detach_thread_lock \
      GC_arrays._dll_main_detach_thread_lock
  volatile AO_TS_t _dll_main_detach_thread_lock;
#  endif

#  define GC_need_to_lock_real GC_arrays._need_to_lock_real
  GC_bool _need_to_lock_real;

#  ifdef GC_ASSERTIONS
#    define GC_thr_initialized GC_arrays._thr_initialized
  GC_bool _thr_initialized;
#  endif
#  if defined(GC_USE_DLOPEN_WRAP) && !defined(GC_WIN32_THREADS) \
      && !defined(PLATFORM_THREADS) && !defined(SN_TARGET_PSP2)
#    define GC_syms_wrap_initialized GC_arrays._syms_wrap_initialized
  GC_bool _syms_wrap_initialized;
#  endif

#  define GC_roots_were_cleared GC_arrays._roots_were_cleared
  GC_bool _roots_were_cleared;
#else
#  ifndef GC_NO_FINALIZATION
/*
 * The variables to minimize the level of recursion when a client
 * finalizer allocates memory.
 */
#    define GC_finalizer_nested GC_arrays._finalizer_nested
#    define GC_finalizer_skipped GC_arrays._finalizer_skipped
  unsigned char _finalizer_nested;
  unsigned short _finalizer_skipped;
#  endif
#endif

  /* Has `GC_init()` been run? */
#define GC_is_initialized GC_arrays._is_initialized
  GC_bool _is_initialized;

  /*
   * Do we need a larger mark stack?  May be set by client-supplied
   * mark routines.
   */
#define GC_mark_stack_too_small GC_arrays._mark_stack_too_small
  GC_bool _mark_stack_too_small;

  /* Are there collectible marked objects in the heap? */
#define GC_objects_are_marked GC_arrays._objects_are_marked
  GC_bool _objects_are_marked;

#define GC_is_full_gc GC_arrays._is_full_gc
  GC_bool _is_full_gc;

#ifdef GC_DISABLE_INCREMENTAL
#  define GC_incremental FALSE
#else
  /*
   * Using incremental/generational collection.  Assumes dirty bits are
   * being maintained.
   */
#  define GC_incremental GC_arrays._incremental
  GC_bool _incremental;

#  define GC_should_start_incremental_collection \
    GC_arrays._should_start_incremental_collection
  GC_bool _should_start_incremental_collection;
#endif

#ifdef GWW_VDB
#  define GC_incremental_at_stack_alloc GC_arrays._incremental_at_stack_alloc
  GC_bool _incremental_at_stack_alloc;
#endif

#ifndef NO_MANUAL_VDB
  /*
   * The incremental collection is in the manual VDB mode.
   * Assumes `GC_incremental` is `TRUE`.  Should not be modified once
   * `GC_incremental` is set to `TRUE`.
   */
#  define GC_manual_vdb GC_arrays._manual_vdb
  GC_bool _manual_vdb;
#endif

#ifndef GC_NO_FINALIZATION
  /* Avoid the work if unreachable finalizable objects are not used. */
  /* TODO: Turn this variable into a counter. */
#  define GC_need_unreachable_finalization \
    GC_arrays._need_unreachable_finalization
  GC_bool _need_unreachable_finalization;
#endif

#ifndef DONT_USE_ATEXIT
#  ifdef SMALL_CONFIG
#    define GC_skip_collect_atexit FALSE
#  else
  /*
   * A dedicated variable to avoid a garbage collection on abort.
   * `GC_find_leak` cannot be used for this purpose as otherwise
   * TSan finds a data race (between `GC_default_on_abort` and, e.g.,
   * `GC_finish_collection`).
   */
#    define GC_skip_collect_atexit GC_arrays._skip_collect_atexit
  GC_bool _skip_collect_atexit;
#  endif
#endif

#if defined(NO_FIND_LEAK) && defined(SHORT_DBG_HDRS)
#  define GC_debugging_initialized GC_arrays._debugging_initialized
  GC_bool _debugging_initialized;
#  define GC_debugging_started FALSE
#else
#  define GC_debugging_initialized GC_debugging_started
  /* `GC_debug_malloc()` has been called, once at least. */
#  define GC_debugging_started GC_arrays._debugging_started
  GC_bool _debugging_started;

#  define GC_have_errors GC_arrays._have_errors
#  ifdef AO_HAVE_store
  volatile AO_t _have_errors;
#  else
  GC_bool _have_errors;
#  endif
#endif

#define GC_explicit_typing_initialized GC_arrays._explicit_typing_initialized
#ifdef AO_HAVE_load_acquire
  volatile AO_t _explicit_typing_initialized;
#else
  GC_bool _explicit_typing_initialized;
#endif

  /* Indicate whether a full collection due to heap growth is needed. */
#define GC_need_full_gc GC_arrays._need_full_gc
  GC_bool _need_full_gc;

#ifndef NO_CLOCK
  /*
   * Do performance measurements if set to true (e.g., accumulation of the
   * total time of full collections).
   */
#  define GC_measure_performance GC_arrays._measure_performance
  GC_bool _measure_performance;

  /*
   * Variables for world-stop average delay time statistic computation.
   * `GC_world_stopped_total_divisor` is incremented every world stop and
   * halved when reached its maximum (or upon `GC_world_stopped_total_time`
   * overflow).  In milliseconds.
   */
  /* TODO: Store the nanosecond part. */
#  define GC_world_stopped_total_time GC_arrays._world_stopped_total_time
#  define GC_world_stopped_total_divisor GC_arrays._world_stopped_total_divisor
  unsigned _world_stopped_total_time;
  unsigned _world_stopped_total_divisor;
#endif

#ifndef NO_FIND_LEAK
#  define GC_n_leaked GC_arrays._n_leaked
  unsigned _n_leaked;
#endif
#ifndef SHORT_DBG_HDRS
#  define GC_n_smashed GC_arrays._n_smashed
  unsigned _n_smashed;
#endif

  /* Number of bytes in the accessible composite objects. */
#define GC_composite_in_use GC_arrays._composite_in_use
  word _composite_in_use;

  /* Number of bytes in the accessible atomic objects. */
#define GC_atomic_in_use GC_arrays._atomic_in_use
  word _atomic_in_use;

  /* GC number of latest successful `GC_expand_hp_inner()` call. */
#define GC_last_heap_growth_gc_no GC_arrays._last_heap_growth_gc_no
  word _last_heap_growth_gc_no;

  /*
   * Number of bytes of memory reclaimed minus the number of bytes originally
   * on free lists that we had to drop.  Protected by the allocator lock.
   */
#define GC_bytes_found GC_arrays._bytes_found
  GC_signed_word _bytes_found;

#ifndef GC_GET_HEAP_USAGE_NOT_NEEDED
  /*
   * Number of bytes reclaimed before this collection cycle; used for
   * statistics only.
   */
#  define GC_reclaimed_bytes_before_gc GC_arrays._reclaimed_bytes_before_gc
  word _reclaimed_bytes_before_gc;
#endif

#ifdef USE_MUNMAP
#  define GC_unmapped_bytes GC_arrays._unmapped_bytes
  word _unmapped_bytes;
#else
#  define GC_unmapped_bytes 0
#endif

#if defined(COUNT_UNMAPPED_REGIONS) && defined(USE_MUNMAP)
#  define GC_num_unmapped_regions GC_arrays._num_unmapped_regions
  GC_signed_word _num_unmapped_regions;
#else
#  define GC_num_unmapped_regions 0
#endif

#define GC_all_nils GC_arrays._all_nils
  bottom_index *_all_nils;

#define GC_scan_ptr GC_arrays._scan_ptr
  struct hblk *_scan_ptr;

#ifdef PARALLEL_MARK
#  define GC_main_local_mark_stack GC_arrays._main_local_mark_stack
  mse *_main_local_mark_stack;

  /*
   * The lowest entry on mark stack that may not be empty.
   * Updated only by the initiating thread.
   */
#  define GC_first_nonempty GC_arrays._first_nonempty
  volatile ptr_t _first_nonempty;
#endif

#ifndef THREADS
  /* Note: `NULL` value means we are not inside `GC_do_blocking()` call. */
#  define GC_blocked_sp GC_arrays._blocked_sp
  ptr_t _blocked_sp;

#  ifdef IA64
#    define GC_blocked_register_sp GC_arrays._blocked_register_sp
  ptr_t _blocked_register_sp;

#    ifndef STACK_NOT_SCANNED
  /* Value returned from register flushing routine (`ar.bsp`). */
#      define GC_save_regs_ret_val GC_arrays._save_regs_ret_val
  ptr_t _save_regs_ret_val;
#    endif
#  endif

  /*
   * Points to the "frame" data held in stack by the innermost
   * `GC_call_with_gc_active()`.  `NULL` if no such "frame" active.
   */
#  define GC_traced_stack_sect GC_arrays._traced_stack_sect
  struct GC_traced_stack_sect_s *_traced_stack_sect;
#endif

#if defined(E2K) && defined(THREADS) || defined(IA64)
  /*
   * The bottom of the register stack of the primordial thread.
   * E2K: holds the offset (`ps_ofs`) instead of a pointer.
   */
#  define GC_register_stackbottom GC_arrays._register_stackbottom
  ptr_t _register_stackbottom;
#endif

#ifdef ENABLE_TRACE
#  define GC_trace_ptr GC_arrays._trace_ptr
  ptr_t _trace_ptr;
#endif

#if !defined(ALWAYS_SMALL_CLEAR_STACK) && !defined(STACK_NOT_SCANNED) \
    && !defined(THREADS)
  /*
   * Coolest stack pointer value from which we have already cleared
   * the stack.
   */
#  define GC_min_sp GC_arrays._min_sp
  ptr_t _min_sp;

  /*
   * The "hottest" stack pointer value we have seen recently.
   * Degrades over time.
   */
#  define GC_high_water GC_arrays._high_water
  ptr_t _high_water;
#endif

#if CPP_PTRSZ > CPP_WORDSZ
#  define GC_noop_sink_ptr GC_arrays._noop_sink_ptr
  volatile ptr_t _noop_sink_ptr;
#endif

#define GC_noop_sink GC_arrays._noop_sink
#if defined(AO_HAVE_store) && defined(THREAD_SANITIZER)
  volatile AO_t _noop_sink;
#else
  volatile word _noop_sink;
#endif

#if !defined(SMALL_CONFIG) && !defined(GC_NO_FINALIZATION)
  /* Saved number of disappearing links for stats printing. */
#  define GC_old_dl_entries GC_arrays._old_dl_entries
  size_t _old_dl_entries;
#  ifndef GC_LONG_REFS_NOT_NEEDED
#    define GC_old_ll_entries GC_arrays._old_ll_entries
  size_t _old_ll_entries;
#  endif
#endif

#define GC_mark_stack_size GC_arrays._mark_stack_size
  size_t _mark_stack_size;

#define GC_mark_state GC_arrays._mark_state
  mark_state_t _mark_state; /*< initialized to `MS_NONE` (0) */

#define GC_capacity_heap_sects GC_arrays._capacity_heap_sects
  size_t _capacity_heap_sects;

#define GC_n_heap_sects GC_arrays._n_heap_sects
  size_t _n_heap_sects; /*< number of separately added heap sections */

#ifdef ANY_MSWIN
#  define GC_n_heap_bases GC_arrays._n_heap_bases
  size_t _n_heap_bases; /*< see `GC_heap_bases[]` */
#endif

#ifdef USE_PROC_FOR_LIBRARIES
  /* Number of `GET_MEM`-allocated memory sections. */
#  define GC_n_memory GC_arrays._n_memory
  word _n_memory;
#endif

#ifdef GC_GCJ_SUPPORT
#  define GC_last_finalized_no GC_arrays._last_finalized_no
#  define GC_gcjobjfreelist GC_arrays._gcjobjfreelist
  word _last_finalized_no;
  ptr_t *_gcjobjfreelist;
#endif

#ifdef NEED_PROC_MAPS
#  define GC_maps_buf GC_arrays._maps_buf
#  define GC_maps_buf_sz_m1 GC_arrays._maps_buf_sz_m1
  char *_maps_buf;
  size_t _maps_buf_sz_m1;
#endif

#define GC_fo_entries GC_arrays._fo_entries
  size_t _fo_entries;

#ifndef NO_DEBUGGING
#  define GC_last_root_set GC_arrays._last_root_set
#  ifndef HAS_REAL_READER_LOCK
  size_t _last_root_set; /*< no shared access */
#  elif defined(AO_HAVE_load) || defined(AO_HAVE_store)
  volatile AO_t _last_root_set;
#  else
  /* Note: a race is acceptable, it is just a cached index. */
  volatile size_t _last_root_set;
#  endif
#endif

#ifndef GC_NO_FINALIZATION
#  define GC_last_finalizer_notification GC_arrays._last_finalizer_notification
#  define GC_dl_hashtbl GC_arrays._dl_hashtbl
#  define GC_fnlz_roots GC_arrays._fnlz_roots
#  define GC_log_fo_table_size GC_arrays._log_fo_table_size
  word _last_finalizer_notification;
#  if defined(KEEP_BACK_PTRS) || defined(MAKE_BACK_GRAPH)
#    define GC_last_back_trace_gc_no GC_arrays._last_back_trace_gc_no
  word _last_back_trace_gc_no;
#  endif
#  ifndef GC_LONG_REFS_NOT_NEEDED
#    define GC_ll_hashtbl GC_arrays._ll_hashtbl
  struct dl_hashtbl_s _ll_hashtbl;
#  endif
  struct dl_hashtbl_s _dl_hashtbl;
  struct fnlz_roots_s _fnlz_roots;
  unsigned _log_fo_table_size;

#  ifndef GC_TOGGLE_REFS_NOT_NEEDED
#    define GC_toggleref_arr GC_arrays._toggleref_arr
#    define GC_toggleref_array_size GC_arrays._toggleref_array_size
#    define GC_toggleref_array_capacity GC_arrays._toggleref_array_capacity
  union toggle_ref_u *_toggleref_arr;
  size_t _toggleref_array_size;
  size_t _toggleref_array_capacity;
#  endif
#endif

#ifdef TRACE_BUF
#  define GC_trace_buf_pos GC_arrays._trace_buf_pos
  size_t _trace_buf_pos; /*< an index in the circular buffer */
#endif

#if defined(MPROTECT_VDB) && defined(DARWIN)
#  define GC_task_self GC_arrays._task_self
#  define GC_exception_port GC_arrays._exception_port
  mach_port_t _task_self;
  mach_port_t _exception_port;
#  ifdef BROKEN_EXCEPTION_HANDLING
#    define GC_darwin_last_fault_addr GC_arrays._darwin_last_fault_addr
#    define GC_darwin_fault_count GC_arrays._darwin_fault_count
  static const char *_darwin_last_fault_addr;
  static int _darwin_fault_count;
#  endif
#endif

  /*
   * How many consecutive collection/expansion failures?
   * Reset by `GC_allochblk()`.
   */
#define GC_alloc_fail_count GC_arrays._alloc_fail_count
  unsigned _alloc_fail_count;

#ifdef ENABLE_DISCLAIM
#  define GC_finalized_kind GC_arrays._finalized_kind
  unsigned _finalized_kind;
#endif

#ifndef NO_CLOCK
#  define GC_full_gc_total_time GC_arrays._full_gc_total_time
#  define GC_stopped_mark_total_time GC_arrays._stopped_mark_total_time
#  define GC_full_gc_total_ns_frac GC_arrays._full_gc_total_ns_frac
#  define GC_stopped_mark_total_ns_frac GC_arrays._stopped_mark_total_ns_frac
  unsigned long _full_gc_total_time; /*< in ms, may wrap */
  unsigned long _stopped_mark_total_time;
  unsigned32 _full_gc_total_ns_frac; /*< fraction of 1 ms */
  unsigned32 _stopped_mark_total_ns_frac;
#endif

#if !defined(GC_NO_THREADS_DISCOVERY) && defined(GC_WIN32_THREADS)
  /* Largest index in `dll_thread_table` that was ever used. */
#  define GC_max_thread_index GC_arrays._max_thread_index
  volatile LONG _max_thread_index;
#endif

  /* Total size of registered root sections. */
#define GC_root_size GC_arrays._root_size
  word _root_size;

  /* `GC_static_roots[0..n_root_sets-1]` contains the valid root sets. */
#define n_root_sets GC_arrays._n_root_sets
  size_t _n_root_sets;

#define GC_excl_table_entries GC_arrays._excl_table_entries
  size_t _excl_table_entries; /*< number of entries in use */

#define GC_ed_size GC_arrays._ed_size
  size_t _ed_size; /*< current size of above arrays */

#define GC_avail_descr GC_arrays._avail_descr
  size_t _avail_descr; /*< next available slot */

#if defined(CAN_HANDLE_FORK) && defined(GC_PTHREADS)
  /* Value of `pthread_self()` of the thread which called `fork()`. */
#  define GC_parent_pthread_self GC_arrays._parent_pthread_self
  pthread_t _parent_pthread_self;
#endif

  /* Points to array of extended descriptors. */
#define GC_ext_descriptors GC_arrays._ext_descriptors
  typed_ext_descr_t *_ext_descriptors;

  /*
   * Table of user-defined mark procedures.  There is a small number
   * of these, which can be referenced by `DS_PROC` mark descriptors.
   * See `gc_mark.h` file.
   */
#define GC_mark_procs GC_arrays._mark_procs
  GC_mark_proc _mark_procs[GC_MAX_MARK_PROCS];

  /*
   * `GC_valid_offsets[i]` implies
   * `GC_modws_valid_offsets[i % sizeof(ptr_t)]`.
   */
#define GC_modws_valid_offsets GC_arrays._modws_valid_offsets
  char _modws_valid_offsets[sizeof(ptr_t)];

#ifdef MAKE_BACK_GRAPH
  /* Points to never-used `back_edges` space. */
#  define GC_n_back_edge_structs GC_arrays._n_back_edge_structs
  int _n_back_edge_structs;

#  define GC_back_edge_space GC_arrays._back_edge_space
  struct back_edges_s *_back_edge_space;

  /* Pointer to free list of deallocated `back_edges` structures. */
#  define GC_avail_back_edges GC_arrays._avail_back_edges
  struct back_edges_s *_avail_back_edges;

  /*
   * Table of objects that are currently on the depth-first search stack.
   * Only objects with in-degree one are in this table.  Other objects are
   * identified using `HEIGHT_IN_PROGRESS`.
   */
  /* FIXME: This data structure needs improvement. */
#  define GC_backgraph_in_progress_space GC_arrays._backgraph_in_progress_space
#  define GC_backgraph_in_progress_size GC_arrays._backgraph_in_progress_size
#  define GC_backgraph_n_in_progress GC_arrays._backgraph_n_in_progress
  ptr_t *_backgraph_in_progress_space;
  size_t _backgraph_in_progress_size;
  size_t _backgraph_n_in_progress;

#  define GC_backgraph_max_deepest_h GC_arrays._backgraph_max_deepest_h
#  define GC_backgraph_deepest_height GC_arrays._backgraph_deepest_height
#  define GC_backgraph_deepest_obj GC_arrays._backgraph_deepest_obj
  word _backgraph_max_deepest_h;
  word _backgraph_deepest_height;
  ptr_t _backgraph_deepest_obj;
#endif

#ifdef GC_READ_ENV_FILE
  /*
   * The contents of the `.gc.env` file with CR and LF replaced to '\0'.
   * `NULL` if the file is missing or empty.  Otherwise, always ends
   * with '\0' (designating the end of the file).
   */
#  define GC_envfile_content GC_arrays._envfile_content
  char *_envfile_content;

  /* Length of `GC_envfile_content` (if non-`NULL`). */
#  define GC_envfile_length GC_arrays._envfile_length
  size_t _envfile_length;
#endif

#ifndef NO_CLOCK
#  ifndef NO_DEBUGGING
  /* The time that the collector was initialized at. */
#    define GC_init_time GC_arrays._init_time
  CLOCK_TYPE _init_time;
#  endif
  /* Time at which we stopped world.  Used only by `GC_timeout_stop_func()`. */
#  define GC_start_time GC_arrays._start_time
  CLOCK_TYPE _start_time;
#endif

#ifndef ANY_MSWIN
  /*
   * The hash table header.  Used only to check whether a range
   * is already present.
   */
#  define GC_root_index GC_arrays._root_index
  struct roots *_root_index[RT_SIZE];
#endif

#ifndef NO_FIND_LEAK
#  define GC_leaked GC_arrays._leaked
  ptr_t _leaked[MAX_LEAKED];
#endif

#ifndef SHORT_DBG_HDRS
  /*
   * List of smashed (clobbered) locations.  We defer printing these,
   * since we cannot always print them nicely with the allocator lock held.
   * We put them here instead of in `GC_arrays`, since it may be useful to
   * be able to look at them with the debugger.
   */
#  define GC_smashed GC_arrays._smashed
  ptr_t _smashed[MAX_SMASHED];
#endif

#if defined(SAVE_CALL_CHAIN) && !defined(DONT_SAVE_TO_LAST_STACK) \
    && (!defined(REDIRECT_MALLOC) || !defined(GC_HAVE_BUILTIN_BACKTRACE))
  /*
   * Stack at last garbage collection.  Useful for debugging mysterious
   * object disappearances.  In the multi-threaded case, we currently only
   * save the calling stack.  Not supported in case of `malloc` redirection
   * because `backtrace()` may call `malloc()`.
   */
  struct callinfo _last_stack[NFRAMES];
#  define SAVE_CALLERS_TO_LAST_STACK() GC_save_callers(GC_arrays._last_stack)
#else
#  define SAVE_CALLERS_TO_LAST_STACK() (void)0
#endif

  /* Free list for `NORMAL` objects. */
#define GC_objfreelist GC_arrays._objfreelist
  void *_objfreelist[MAXOBJGRANULES + 1];

  /* Free list for atomic objects. */
#define GC_aobjfreelist GC_arrays._aobjfreelist
  void *_aobjfreelist[MAXOBJGRANULES + 1];

  /*
   * Uncollectible but traced objects.  Objects on this and `_auobjfreelist`
   * are always marked, except during garbage collections.
   */
#define GC_uobjfreelist GC_arrays._uobjfreelist
  void *_uobjfreelist[MAXOBJGRANULES + 1];

#ifdef GC_ATOMIC_UNCOLLECTABLE
  /* Atomic uncollectible but traced objects. */
#  define GC_auobjfreelist GC_arrays._auobjfreelist
  void *_auobjfreelist[MAXOBJGRANULES + 1];
#endif

  /*
   * Number of granules to allocate when asked for a certain number of bytes
   * (plus `EXTRA_BYTES`).  Should be accessed with the allocator lock held.
   */
#define GC_size_map GC_arrays._size_map
  size_t _size_map[MAXOBJBYTES + 1];

#ifndef MARK_BIT_PER_OBJ
  /*
   * If the element is not `NULL`, then it points to a map of valid object
   * addresses.  `GC_obj_map[lg][i]` is `i % lg`.  This is now used purely
   * to replace a division in the marker by a table lookup.
   * `GC_obj_map[0]` is used for large objects and contains all nonzero
   * entries.  This gets us out of the marker fast path without an extra test.
   */
#  define GC_obj_map GC_arrays._obj_map
  hb_map_entry_t *_obj_map[MAXOBJGRANULES + 1];

#  define OBJ_MAP_LEN BYTES_TO_GRANULES(HBLKSIZE)
#endif

#define VALID_OFFSET_SZ HBLKSIZE
  /*
   * A nonzero `GC_valid_offsets[i]` means `i` is registered as
   * a displacement.
   */
#define GC_valid_offsets GC_arrays._valid_offsets
  char _valid_offsets[VALID_OFFSET_SZ];

#ifndef GC_DISABLE_INCREMENTAL
  /* Pages that were dirty at last `GC_read_dirty()` call. */
#  define GC_grungy_pages GC_arrays._grungy_pages
  page_hash_table _grungy_pages;

  /* Pages dirtied since last `GC_read_dirty()` call. */
#  define GC_dirty_pages GC_arrays._dirty_pages
#  ifdef MPROTECT_VDB
  volatile
#  endif
      page_hash_table _dirty_pages;
#endif

#if (defined(CHECKSUMS) && (defined(GWW_VDB) || defined(SOFT_VDB))) \
    || defined(PROC_VDB)
  /* A table to indicate the pages ever dirtied. */
#  define GC_written_pages GC_arrays._written_pages
  page_hash_table _written_pages;
#endif

  /* Heap segments potentially containing client objects. */
#define GC_heap_sects GC_arrays._heap_sects
  struct HeapSect *_heap_sects;

#if defined(USE_PROC_FOR_LIBRARIES)
  /* All `GET_MEM`-allocated memory.  Includes block headers and the like. */
#  define GC_our_memory GC_arrays._our_memory
  struct HeapSect _our_memory[MAX_HEAP_SECTS];
#endif

#ifdef ANY_MSWIN
  /* Start address of memory regions obtained from OS. */
#  define GC_heap_bases GC_arrays._heap_bases
  ptr_t _heap_bases[MAX_HEAP_SECTS];
#endif

#ifdef MSWINCE
  /* Committed lengths of memory regions obtained from OS. */
#  define GC_heap_lengths GC_arrays._heap_lengths
  word _heap_lengths[MAX_HEAP_SECTS];
#endif

#define GC_static_roots GC_arrays._static_roots
  struct roots _static_roots[MAX_ROOT_SETS];

  /* Array of exclusions, ascending address order. */
#define GC_excl_table GC_arrays._excl_table
  struct exclusion _excl_table[MAX_EXCLUSIONS];

  /*
   * The block header index.  Each entry points to a `bottom_index` entity.
   * On a 32-bit machine, it points to the index for a set of the high-order
   * bits equal to the index.  For longer addresses, we hash the high-order
   * bits to compute the index in `GC_top_index`, and each entry points to
   * a hash chain.  The last entry in each chain is `GC_all_nils`.
   */
#define GC_top_index GC_arrays._top_index
  bottom_index *_top_index[TOP_SZ];

#if defined(DYNAMIC_LOADING) && !defined(ANY_MSWIN) \
    && !defined(USE_PROC_FOR_LIBRARIES) && defined(HAVE_DL_ITERATE_PHDR)
/*
 * Instead of registering `PT_LOAD` sections directly, we keep them in
 * a temporary list, and filter them by excluding `PT_GNU_RELRO` segments.
 * Processing `PT_GNU_RELRO` sections with `GC_exclude_static_roots` instead
 * would be superficially cleaner.  But it runs into trouble if a client
 * registers an overlapping segment, which unfortunately seems quite
 * possible.
 */
#  define GC_n_load_segs GC_arrays._n_load_segs
#  define GC_load_segs_overflow_warned GC_arrays._load_segs_overflow_warned
#  define GC_load_segs GC_arrays._load_segs
  size_t _n_load_segs;
  GC_bool _load_segs_overflow_warned;
  struct load_segment_s _load_segs[MAX_LOAD_SEGS];
#endif

#ifdef GWW_VDB
#  define GC_gww_buf GC_arrays._gww_buf
  void *_gww_buf[GC_GWW_BUF_LEN];
#endif

#ifdef ECOS
#  ifndef ECOS_GC_MEMORY_SIZE
#    define ECOS_GC_MEMORY_SIZE (448 * 1024)
#  endif
#  define GC_ecos_brk_idx GC_arrays._ecos_brk_idx
#  define GC_ecos_memory GC_arrays._ecos_memory
  size_t _ecos_brk_idx;
  char _ecos_memory[ECOS_GC_MEMORY_SIZE];
#endif
};

GC_API_PRIV struct _GC_arrays GC_arrays;
#define beginGC_arrays ((ptr_t)(&GC_arrays))
#define endGC_arrays (beginGC_arrays + sizeof(GC_arrays))

/* Object kinds. */
#ifndef MAXOBJKINDS
#  ifdef GC_DEBUG
#    define MAXOBJKINDS 32
#  elif !defined(SMALL_CONFIG)
#    define MAXOBJKINDS 24
#  else
#    define MAXOBJKINDS 16
#  endif
#endif
GC_EXTERN struct obj_kind {
  /*
   * Array of free-list headers for this kind of object.  Point either
   * to `GC_arrays` or to storage allocated with `GC_scratch_alloc()`.
   */
  void **ok_freelist;

  /*
   * List headers for lists of blocks waiting to be swept.
   * Indexed by object size in granules.
   */
  struct hblk **ok_reclaim_list;

  /* Descriptor template for objects in this block. */
  word ok_descriptor;

  /*
   * Add object size in bytes to descriptor template to obtain descriptor.
   * Otherwise the template is used as is.
   */
  GC_bool ok_relocate_descr;

  /* Clear objects before putting them on the free list. */
  GC_bool ok_init;

#ifdef ENABLE_DISCLAIM
  /*
   * Mark from all, including unmarked, objects in block.
   * Used to protect objects reachable from reclaim notifiers.
   */
  GC_bool ok_mark_unconditionally;

  /*
   * The disclaim procedure is called before `obj` is reclaimed, but
   * must also tolerate being called with object from free list.
   * A nonzero exit prevents object from being reclaimed.
   */
  int(GC_CALLBACK *ok_disclaim_proc)(void * /* `obj` */);

#  define OK_DISCLAIM_INITZ /* comma */ , FALSE, 0
#else
#  define OK_DISCLAIM_INITZ /*< empty */
#endif
} GC_obj_kinds[MAXOBJKINDS];

#define beginGC_obj_kinds ((ptr_t)(&GC_obj_kinds[0]))
#define endGC_obj_kinds (beginGC_obj_kinds + sizeof(GC_obj_kinds))

/* The predefined kinds. */
#define PTRFREE GC_I_PTRFREE
#define NORMAL GC_I_NORMAL
#define UNCOLLECTABLE 2
#ifdef GC_ATOMIC_UNCOLLECTABLE
#  define AUNCOLLECTABLE 3
#  define IS_UNCOLLECTABLE(k) (((k) & ~1) == UNCOLLECTABLE)
#  define GC_N_KINDS_INITIAL_VALUE 4
#else
#  define IS_UNCOLLECTABLE(k) ((k) == UNCOLLECTABLE)
#  define GC_N_KINDS_INITIAL_VALUE 3
#endif

GC_EXTERN unsigned GC_n_kinds;

/* May mean the allocation granularity size, not page size. */
GC_EXTERN size_t GC_page_size;

#ifdef REAL_PAGESIZE_NEEDED
GC_EXTERN size_t GC_real_page_size;
#else
#  define GC_real_page_size GC_page_size
#endif

/*
 * Get heap memory from the OS.
 * Note that `sbrk`-like allocation is preferred, since it usually
 * makes it possible to merge consecutively allocated chunks.
 * It also avoids unintended recursion with `REDIRECT_MALLOC` macro
 * defined.  `GET_MEM()` argument should be of `size_t` type and
 * have no side-effect.  `GET_MEM()` returns `HBLKSIZE`-aligned chunk
 * (`NULL` means a failure).  In case of `MMAP_SUPPORTED`, the argument
 * must also be a multiple of a physical page size.
 * `GET_MEM` is currently not assumed to retrieve zero-filled space.
 */
/* TODO: Take advantage of `GET_MEM()` returning a zero-filled space. */
#if defined(ANY_MSWIN) || defined(MSWIN_XBOX1) || defined(OS2)
GC_INNER void *GC_get_mem(size_t lb);
#  define GET_MEM(lb) GC_get_mem(lb)
#  if defined(CYGWIN) && !defined(USE_WINALLOC)
#    define NEED_UNIX_GET_MEM
#  endif
#elif defined(DOS4GW) || defined(EMBOX) || defined(KOS) || defined(NEXT) \
    || defined(NONSTOP) || defined(RTEMS) || defined(__CC_ARM)           \
    || (defined(SOLARIS) && !defined(USE_MMAP))
/* TODO: Use `page_alloc()` directly on Embox. */
#  if defined(REDIRECT_MALLOC) && !defined(CPPCHECK)
#    error Malloc redirection is unsupported
#  endif
#  define GET_MEM(lb)                                                  \
    ((void *)HBLKPTR((ptr_t)calloc(1, SIZET_SAT_ADD(lb, GC_page_size)) \
                     + GC_page_size - 1))
#elif !defined(GET_MEM)
GC_INNER void *GC_unix_get_mem(size_t lb);
#  define GET_MEM(lb) GC_unix_get_mem(lb)
#  define NEED_UNIX_GET_MEM
#endif

/*
 * Round up allocation size to a multiple of a page size.
 * `GC_setpagesize()` is assumed to be already invoked.
 */
#define ROUNDUP_PAGESIZE(lb) /*< `lb` should have no side-effect */ \
  (SIZET_SAT_ADD(lb, GC_page_size - 1) & ~(GC_page_size - 1))

/*
 * Same as `ROUNDUP_PAGESIZE` but is used to make `GET_MEM()` argument
 * safe.
 */
#ifdef MMAP_SUPPORTED
#  define ROUNDUP_PAGESIZE_IF_MMAP(lb) ROUNDUP_PAGESIZE(lb)
#else
#  define ROUNDUP_PAGESIZE_IF_MMAP(lb) (lb)
#endif

#ifdef ANY_MSWIN
GC_EXTERN SYSTEM_INFO GC_sysinfo;

/*
 * Is `p` the start of either the `malloc` heap, or of one of the collector
 * heap sections?
 */
GC_INNER GC_bool GC_is_heap_base(const void *p);
#endif

#ifdef GC_GCJ_SUPPORT
/* Note: `GC_hblkfreelist` and `GC_free_bytes` remain visible to GNU `gcj`. */
extern struct hblk *GC_hblkfreelist[];
extern word GC_free_bytes[];
#endif

/* This is used by `GC_do_blocking()`. */
struct blocking_data {
  GC_fn_type fn;
  void *client_data; /*< and result */
};

#ifdef THREADS
/*
 * Process all "traced stack sections" - scan entire stack except for
 * frames belonging to the user functions invoked by `GC_do_blocking`.
 */
GC_INNER void
GC_push_all_stack_sections(ptr_t lo, ptr_t hi,
                           struct GC_traced_stack_sect_s *traced_stack_sect);

/*
 * The total size, in bytes, of all stacks.
 * Updated on every `GC_push_all_stacks()` call.
 */
GC_EXTERN word GC_total_stacksize;
#endif

#ifdef IA64
/* Similar to `GC_push_all_stack_sections` but for IA-64 registers store. */
GC_INNER void GC_push_all_register_sections(
    ptr_t bs_lo, ptr_t bs_hi, GC_bool eager,
    struct GC_traced_stack_sect_s *traced_stack_sect);
#endif

/*
 * Mark bit operations.
 *
 * The marks are in a reserved area of each heap block.
 * Each object or granule has one mark bit associated with it.
 * Only those corresponding to the beginning of an object are used.
 */

/*
 * Retrieve, set, clear the `n`-th mark bit in a given heap block.
 * (Recall that bit `n` corresponds to `n`-th object or allocation granule
 * relative to the beginning of the block, including unused space.)
 */

#ifdef USE_MARK_BYTES
#  define mark_bit_from_hdr(hhdr, n) ((hhdr)->hb_marks[n])
#  define set_mark_bit_from_hdr(hhdr, n) (void)((hhdr)->hb_marks[n] = 1)
#  define clear_mark_bit_from_hdr(hhdr, n) (void)((hhdr)->hb_marks[n] = 0)
#else
/* Set mark bit correctly, even if mark bits may be concurrently accessed. */
#  if defined(PARALLEL_MARK) || (defined(THREAD_SANITIZER) && defined(THREADS))
/*
 * Workaround TSan false positive: there is no race between
 * `mark_bit_from_hdr` and `set_mark_bit_from_hdr` when `n` is different
 * (alternatively, `USE_MARK_BYTES` could be used).  If TSan is off, then
 * `AO_or()` is used only if we define `USE_MARK_BITS` macro explicitly.
 */
#    define OR_WORD(addr, bits) AO_or(addr, bits)
#  else
#    define OR_WORD(addr, bits) (void)(*(addr) |= (bits))
#  endif
#  define mark_bit_from_hdr(hhdr, n) \
    (((hhdr)->hb_marks[divWORDSZ(n)] >> modWORDSZ(n)) & (word)1)
#  define set_mark_bit_from_hdr(hhdr, n) \
    OR_WORD((hhdr)->hb_marks + divWORDSZ(n), (word)1 << modWORDSZ(n))
#  define clear_mark_bit_from_hdr(hhdr, n)                                    \
    (void)(((word *)CAST_AWAY_VOLATILE_PVOID((hhdr)->hb_marks))[divWORDSZ(n)] \
           &= ~((word)1 << modWORDSZ(n)))
#endif /* !USE_MARK_BYTES */

#ifdef MARK_BIT_PER_OBJ
/*
 * Get the mark bit index corresponding to the given byte offset and
 * size (in bytes).
 */
#  define MARK_BIT_NO(offset, sz) ((offset) / (sz))

/* Spacing between useful mark bits. */
#  define MARK_BIT_OFFSET(sz) 1

/* Position of final, always set, mark bit. */
#  define FINAL_MARK_BIT(sz) ((sz) > MAXOBJBYTES ? 1 : HBLK_OBJS(sz))
#else
#  define MARK_BIT_NO(offset, sz) BYTES_TO_GRANULES(offset)
#  define MARK_BIT_OFFSET(sz) BYTES_TO_GRANULES(sz)
#  define FINAL_MARK_BIT(sz)                 \
    ((sz) > MAXOBJBYTES ? MARK_BITS_PER_HBLK \
                        : BYTES_TO_GRANULES(HBLK_OBJS(sz) * (sz)))
#endif /* !MARK_BIT_PER_OBJ */

/* Important internal collector routines. */

/* Return the current stack pointer, approximately. */
GC_INNER ptr_t GC_approx_sp(void);

/*
 * Same as `GC_approx_sp` but a macro.  `sp` should be a local variable
 * of `volatile` `ptr_t` type.
 */
#if (defined(E2K) && defined(__clang__)         \
     || (defined(S390) && __clang_major__ < 8)) \
    && !defined(CPPCHECK)
/*
 * Workaround some bugs in clang:
 *   - "undefined reference to llvm.frameaddress" error (clang-9/e2k);
 *   - a crash in SystemZTargetLowering of libLLVM-3.8 (s390).
 */
#  define STORE_APPROX_SP_TO(sp) (void)(sp = (ptr_t)(&sp))
#elif defined(CPPCHECK)                              \
    || ((__GNUC__ >= 4 /* `GC_GNUC_PREREQ(4, 0)` */) \
        && !defined(STACK_NOT_SCANNED))
/* TODO: Use `GC_GNUC_PREREQ` after fixing a bug in cppcheck. */
/* Note: l-value is passed instead of pointer to `sp` (because of cppcheck). */
#  define STORE_APPROX_SP_TO(sp) (void)(sp = (ptr_t)__builtin_frame_address(0))
#else
#  define STORE_APPROX_SP_TO(sp) (void)(sp = (ptr_t)(&sp))
#endif

/* Have we allocated enough to amortize a collection? */
GC_INNER GC_bool GC_should_collect(void);

/*
 * Get the next block whose address is at least `h`.  Returned block
 * is managed by the collector.  The block must be in use unless
 * `allow_free` is TRUE.  Return `NULL` if there is no such block.
 */
GC_INNER struct hblk *GC_next_block(struct hblk *h, GC_bool allow_free);

/*
 * Get the last (highest address) block whose address is at most `h`.
 * Returned block is managed by the collector, but may or may not be in use.
 * Return `NULL` if there is no such block.
 */
GC_INNER struct hblk *GC_prev_block(struct hblk *h);

GC_INNER void GC_mark_init(void);

/*
 * Clear mark bits in all allocated heap blocks (i.e. for all heap objects).
 * This invalidates the marker invariant, and sets `GC_mark_state` to
 * reflect this.  (This implicitly starts marking to reestablish the
 * invariant.)
 */
GC_INNER void GC_clear_marks(void);

/*
 * Tell the marker that marked objects may point to unmarked ones, and
 * roots may point to unmarked objects.  Reset mark stack.
 */
GC_INNER void GC_invalidate_mark_state(void);

/*
 * Perform a small amount of marking.  We try to touch roughly a page
 * of memory.  Returns quickly if no collection is in progress.
 * Returns `TRUE` if we just finished a mark phase.
 * `cold_gc_frame` argument is an address inside a frame of the
 * collector that remains valid until all marking is complete;
 * `NULL` value indicates that it is OK to miss some register values.
 * In the case of an incremental collection, the world may be running.
 */
GC_INNER GC_bool GC_mark_some(ptr_t cold_gc_frame);

/*
 * Initiate a garbage collection.  Initiates a full collection if the
 * mark state is invalid; otherwise it is a partial one.
 */
GC_INNER void GC_initiate_gc(void);

/*
 * Is a collection in progress?  Note that this can return `TRUE` in
 * the non-incremental case, if a collection has been abandoned and
 * the mark state is now `MS_INVALID`.
 */
GC_INNER GC_bool GC_collection_in_progress(void);

/*
 * Push contents of the symbol residing in the static roots area excluded
 * from scanning by the collector for a reason.  Note: it should be used only
 * for symbols of relatively small size (containing one or several pointers).
 */
#define GC_PUSH_ALL_SYM(sym) GC_push_all_eager(&(sym), &(sym) + 1)

#if defined(NEED_FIXUP_POINTER) || defined(NO_ALL_INTERIOR_POINTERS)
#  define GC_push_all_stack(b, t) GC_push_all_eager(b, t)
#else
/* Same as `GC_push_all` but consider interior pointers as valid. */
GC_INNER void GC_push_all_stack(void *b, void *t);
#endif

#ifdef NO_VDB_FOR_STATIC_ROOTS
#  define GC_push_conditional_static(b, t, all) \
    ((void)(all), GC_push_all(b, t))
#else
/*
 * Same as `GC_push_conditional` (does either of `GC_push_all` or
 * `GC_push_selected` depending on the third argument) but the caller
 * guarantees the region belongs to the registered static roots.
 */
GC_INNER void GC_push_conditional_static(void *b, void *t, GC_bool all);
#endif

#if defined(WRAP_MARK_SOME) && defined(PARALLEL_MARK)
/*
 * Similar to `GC_push_conditional` but scans the whole region immediately.
 * `GC_mark_local` does not handle memory protection faults yet.
 * So, the static data regions are scanned immediately by `GC_push_roots`.
 */
GC_INNER void GC_push_conditional_eager(void *bottom, void *top, GC_bool all);
#endif

/*
 * In the multi-threaded case, we push part of the current thread stack
 * with `GC_push_all_eager` when we push the registers.  This gets the
 * callee-save registers that may disappear.  The remainder of the stacks
 * are scheduled for scanning in `(*GC_push_other_roots)()`, which is
 * thread-package-specific.
 */

/*
 * Push all or dirty roots.  Call the mark routine (`GC_push_one` for
 * a single pointer, `GC_push_conditional` on groups of pointers) on every
 * top level accessible pointer.  If not `all`, then arrange to push only
 * possibly altered values.  `cold_gc_frame` is an address inside
 * a collector frame that remains valid until all marking is complete;
 * a `NULL` pointer indicates that it is OK to miss some register values.
 */
GC_INNER void GC_push_roots(GC_bool all, ptr_t cold_gc_frame);

/*
 * Push system or application specific roots onto the mark stack.
 * In some environments (e.g. a multi-threaded one) this is predefined
 * to be nonzero.  A client-supplied replacement should also call the
 * original function.  Remains externally visible as used by some
 * well-known 3rd-party software (e.g., ECL) currently.
 */
GC_API_PRIV GC_push_other_roots_proc GC_push_other_roots;

#ifdef THREADS
GC_INNER void GC_push_thread_structures(void);
#endif

GC_INNER void GC_push_reclaim_structures(void);

/*
 * A pointer set to `GC_push_typed_structures_proc` lazily so that we can
 * avoid linking in the typed allocation support if the latter is unused.
 */
GC_EXTERN void (*GC_push_typed_structures)(void);

typedef void (*GC_with_callee_saves_func)(ptr_t arg, void *context);

/*
 * Ensure that either registers are pushed, or callee-save registers are
 * somewhere on the stack, and then call `fn(arg, ctxt)`.  `ctxt` is either
 * a pointer to a `ucontext_t` entity we generated, or `NULL`.  Could be
 * called with or w/o the allocator lock held; could be called from a signal
 * handler as well.
 */
GC_INNER void GC_with_callee_saves_pushed(GC_with_callee_saves_func fn,
                                          ptr_t arg);

#if defined(IA64) || defined(SPARC)
/*
 * Cause all stacked registers to be saved in memory.  Return a pointer to
 * the top of the corresponding memory stack.
 */
ptr_t GC_save_regs_in_stack(void);
#endif

#ifdef E2K
#  include <asm/e2k_syswork.h>
#  include <errno.h>
#  include <sys/syscall.h>

#  if defined(CPPCHECK)
/*
 * Workaround "Uninitialized bs_lo" and "obsolete alloca() called"
 * false positive (FP) warnings.
 */
#    define PS_ALLOCA_BUF(pbuf, sz) \
      (void)(GC_noop1_ptr(pbuf), *(pbuf) = (ptr_t)__builtin_alloca(sz))
#  else
#    define PS_ALLOCA_BUF(pbuf, sz) (void)(*(pbuf) = (ptr_t)alloca(sz))
#  endif

/*
 * Approximate size (in bytes) of the obtained procedure stack part
 * belonging to `syscall()` itself.
 */
#  define PS_SYSCALL_TAIL_BYTES 0x100

/*
 * Determine the current size of the whole procedure stack.  The size
 * is valid only within the current function.
 */
#  define GET_PROCEDURE_STACK_SIZE_INNER(psz_ull)                            \
    do {                                                                     \
      *(psz_ull) = 0; /*< might be redundant */                              \
      if (syscall(__NR_access_hw_stacks, E2K_GET_PROCEDURE_STACK_SIZE, NULL, \
                  NULL, 0, psz_ull)                                          \
          == -1)                                                             \
        ABORT_ARG1("Cannot get size of procedure stack", ": errno= %d",      \
                   errno);                                                   \
      GC_ASSERT(*(psz_ull) > 0 && *(psz_ull) % sizeof(ptr_t) == 0);          \
    } while (0)

#  ifdef THREADS
#    define PS_COMPUTE_ADJUSTED_OFS(padj_ps_ofs, ps_ofs, ofs_sz_ull)      \
      do {                                                                \
        if ((ofs_sz_ull) <= (ps_ofs) /* `&& ofs_sz_ull > 0` */)           \
          ABORT_ARG2("Incorrect size of procedure stack",                 \
                     ": ofs= %lu, size= %lu", (unsigned long)(ps_ofs),    \
                     (unsigned long)(ofs_sz_ull));                        \
        *(padj_ps_ofs) = (ps_ofs) > (unsigned)PS_SYSCALL_TAIL_BYTES       \
                             ? (ps_ofs) - (unsigned)PS_SYSCALL_TAIL_BYTES \
                             : 0;                                         \
      } while (0)
#  else
/* A simplified variant of the above assuming `ps_ofs` is a zero const. */
#    define PS_COMPUTE_ADJUSTED_OFS(padj_ps_ofs, ps_ofs, ofs_sz_ull) \
      do {                                                           \
        GC_STATIC_ASSERT((ps_ofs) == 0);                             \
        (void)(ofs_sz_ull);                                          \
        *(padj_ps_ofs) = 0;                                          \
      } while (0)
#  endif /* !THREADS */

/*
 * Copy procedure (register) stack to a stack-allocated buffer.
 * Usable from a signal handler.  The buffer (`*pbuf`) is valid only
 * within the current function.  `ps_ofs` designates the offset in the
 * procedure stack to copy the contents from.  Note: this macro cannot
 * be changed to a function because `alloca()` and both `syscall()`
 * should be called in the context of the caller.
 */
#  define GET_PROCEDURE_STACK_LOCAL(ps_ofs, pbuf, psz)                      \
    do {                                                                    \
      unsigned long long ofs_sz_ull;                                        \
      size_t adj_ps_ofs;                                                    \
                                                                            \
      GET_PROCEDURE_STACK_SIZE_INNER(&ofs_sz_ull);                          \
      PS_COMPUTE_ADJUSTED_OFS(&adj_ps_ofs, ps_ofs, ofs_sz_ull);             \
      *(psz) = (size_t)ofs_sz_ull - adj_ps_ofs;                             \
      /* Allocate buffer on the stack; cannot return `NULL`. */             \
      PS_ALLOCA_BUF(pbuf, *(psz));                                          \
      /* Copy the procedure stack at the given offset to the buffer. */     \
      for (;;) {                                                            \
        ofs_sz_ull = adj_ps_ofs;                                            \
        if (syscall(__NR_access_hw_stacks, E2K_READ_PROCEDURE_STACK_EX,     \
                    &ofs_sz_ull, *(pbuf), *(psz), NULL)                     \
            != -1)                                                          \
          break;                                                            \
        if (errno != EAGAIN)                                                \
          ABORT_ARG2("Cannot read procedure stack", ": sz= %lu, errno= %d", \
                     (unsigned long)(*(psz)), errno);                       \
      }                                                                     \
    } while (0)
#endif /* E2K */

#if defined(E2K) && defined(USE_PTR_HWTAG)
/* Load value and get tag of the target memory. */
#  if defined(__ptr64__)
#    define LOAD_TAGGED_VALUE(v, tag, p)                        \
      do {                                                      \
        ptr_t val;                                              \
        __asm__ __volatile__("ldd, sm %[adr], 0x0, %[val]\n\t"  \
                             "gettagd %[val], %[tag]\n"         \
                             : [val] "=r"(val), [tag] "=r"(tag) \
                             : [adr] "r"(p));                   \
        v = val;                                                \
      } while (0)
#  elif !defined(CPPCHECK)
#    error Unsupported -march for e2k target
#  endif

#  define LOAD_PTR_OR_CONTINUE(v, p) \
    {                                \
      int tag LOCAL_VAR_INIT_OK;     \
      LOAD_TAGGED_VALUE(v, tag, p);  \
      if (tag != 0)                  \
        continue;                    \
    }
#elif defined(CHERI_PURECAP)
#  define HAS_TAG_AND_PERM_LOAD(cap) \
    (cheri_tag_get(cap) != 0 && (cheri_perms_get(cap) & CHERI_PERM_LOAD) != 0)

#  define LOAD_PTR_OR_CONTINUE(v, p)                                         \
    {                                                                        \
      word base_addr;                                                        \
      v = *(ptr_t *)(p);                                                     \
      if (!HAS_TAG_AND_PERM_LOAD(v))                                         \
        continue;                                                            \
      base_addr = cheri_base_get(v);                                         \
      if (ADDR(v) < base_addr || ADDR(v) >= base_addr + cheri_length_get(v)) \
        continue;                                                            \
    }

#  define CAPABILITY_COVERS_RANGE(cap, b_addr, e_addr) \
    (cheri_base_get(cap) <= (b_addr)                   \
     && cheri_base_get(cap) + cheri_length_get(cap) >= (e_addr))
#  define SPANNING_CAPABILITY(cap, b_addr, e_addr)                       \
    (cheri_tag_get(cap) && CAPABILITY_COVERS_RANGE(cap, b_addr, e_addr)  \
     && (cheri_perms_get(cap) & (CHERI_PERM_LOAD | CHERI_PERM_LOAD_CAP)) \
            != 0)
#else
#  define LOAD_PTR_OR_CONTINUE(v, p) (void)(v = *(ptr_t *)(p))
#endif /* !CHERI_PURECAP */

#if defined(DARWIN) && defined(THREADS)
/*
 * If `p` points to an object, mark it and push contents on the mark stack.
 * Pointer recognition test always accepts interior pointers, i.e. this is
 * appropriate for pointers found on the thread stack.
 */
void GC_push_one(word p);
#endif

/*
 * Mark and push (i.e. gray) a single object `p` onto the main mark stack.
 * Consider `p` to be valid if it is an interior pointer.  The object `p`
 * has passed a preliminary pointer validity test, but we do not definitely
 * know whether it is valid.  Mark bits are not atomically updated; thus
 * this must be the only thread setting them.
 */
#if defined(PRINT_BLACK_LIST) || defined(KEEP_BACK_PTRS)
GC_INNER void GC_mark_and_push_stack(ptr_t p, ptr_t source);
#else
GC_INNER void GC_mark_and_push_stack(ptr_t p);
#endif

/* Is the block with the given header containing no pointers? */
#define IS_PTRFREE(hhdr) (0 == (hhdr)->hb_descr)

/* Clear all mark bits in the header. */
GC_INNER void GC_clear_hdr_marks(hdr *hhdr);

/* Set all mark bits in the header.  Used for uncollectible blocks. */
GC_INNER void GC_set_hdr_marks(hdr *hhdr);

/* Set all mark bits associated with a free list. */
GC_INNER void GC_set_fl_marks(ptr_t);

#if defined(GC_ASSERTIONS) && defined(THREAD_LOCAL_ALLOC)
/*
 * Check that all mark bits associated with a free list are set.
 * Abort if not.
 */
void GC_check_fl_marks(void **);
#endif

/*
 * Add [`b`,`e`) to the root set.  Adding the same interval a second
 * time is a moderately fast no-op, and hence benign.  We do not handle
 * different but overlapping intervals efficiently.  (But we do handle
 * them correctly.)  `tmp` specifies that the interval may be deleted
 * before re-registering dynamic libraries.
 */
GC_INNER void GC_add_roots_inner(ptr_t b, ptr_t e, GC_bool tmp);

#if defined(USE_PROC_FOR_LIBRARIES) && defined(LINUX)
/*
 * Remove given range from every static root which intersects with the range.
 * `GC_remove_tmp_roots` is assumed to be called before this function is
 * called (repeatedly) by `GC_register_map_entries`.
 */
GC_INNER void GC_remove_roots_subregion(ptr_t b, ptr_t e);
#endif

/*
 * Inform the collector that a certain section of statically allocated
 * memory contains no pointers to garbage-collected memory.
 * The range boundaries should be properly aligned and valid.
 */
GC_INNER void GC_exclude_static_roots_inner(ptr_t start, ptr_t finish);

#if defined(ANY_MSWIN) || defined(DYNAMIC_LOADING)
/* Add dynamic library data sections to the root set. */
GC_INNER void GC_register_dynamic_libraries(void);
#endif

/*
 * Remove and re-register dynamic libraries if we are configured to do
 * that at each collection.
 */
GC_INNER void GC_cond_register_dynamic_libraries(void);

/* Machine-dependent startup routines. */

/*
 * Get the cold end of the stack of the primordial thread.  This is always
 * called from the main (primordial) thread.
 */
GC_INNER ptr_t GC_get_main_stack_base(void);

#ifdef IA64
/* Get the cold end of register stack. */
GC_INNER ptr_t GC_get_register_stack_base(void);
#endif

GC_INNER void GC_register_data_segments(void);

#ifdef THREADS
/* Both are invoked from `GC_init()` only. */
GC_INNER void GC_thr_init(void);

/*
 * Perform all initializations, including those that may require allocation,
 * e.g. initialize thread-local free lists if used.  Called by `GC_init()`.
 */
GC_INNER void GC_init_parallel(void);

#  ifndef DONT_USE_ATEXIT
GC_INNER GC_bool GC_is_main_thread(void);
#  endif
#else
#  ifdef TRACE_BUF
void GC_add_trace_entry(const char *caller_fn_name, ptr_t arg1, ptr_t arg2);
#  endif
#endif /* !THREADS */

#ifdef NO_BLACK_LISTING
#  define GC_bl_init() (void)0
/* Do not define `GC_bl_init_no_interiors()`. */
#  define GC_ADD_TO_BLACK_LIST_NORMAL(p, source) ((void)(p))
#  define GC_ADD_TO_BLACK_LIST_STACK(p, source) ((void)(p))
#  define GC_promote_black_lists() (void)0
#  define GC_unpromote_black_lists() (void)0
#else

/*
 * If we need a block of `n` bytes, and we have a block of `n + BL_LIMIT`
 * bytes available, and `n` is greater than `BL_LIMIT`, but all possible
 * positions in it are black-listed, we just use it anyway (and print
 * a warning, if warnings are enabled).  This risks subsequently leaking
 * the block due to a false reference.  But not using the block risks
 * unreasonable immediate heap growth.
 */
#  define BL_LIMIT GC_black_list_spacing

/*
 * The interval between unsuppressed warnings about repeated allocation
 * of a very large block.
 */
GC_EXTERN long GC_large_alloc_warn_interval;

/* Initialize the black listing mechanism. */
GC_INNER void GC_bl_init(void);
GC_INNER void GC_bl_init_no_interiors(void);

#  ifdef PRINT_BLACK_LIST
/*
 * Register bits as a possible future false reference from the heap
 * or static data.  The argument `p` is not a valid pointer reference,
 * but it falls inside the plausible heap bounds.
 */
GC_INNER void GC_add_to_black_list_normal(ptr_t p, ptr_t source);
#    define GC_ADD_TO_BLACK_LIST_NORMAL(p, source) \
      if (GC_all_interior_pointers) {              \
        GC_add_to_black_list_stack(p, source);     \
      } else                                       \
        GC_add_to_black_list_normal(p, source)
GC_INNER void GC_add_to_black_list_stack(ptr_t p, ptr_t source);
#    define GC_ADD_TO_BLACK_LIST_STACK(p, source) \
      GC_add_to_black_list_stack(p, source)
#  else
GC_INNER void GC_add_to_black_list_normal(ptr_t p);
#    define GC_ADD_TO_BLACK_LIST_NORMAL(p, source) \
      if (GC_all_interior_pointers) {              \
        GC_add_to_black_list_stack(p);             \
      } else                                       \
        GC_add_to_black_list_normal(p)
GC_INNER void GC_add_to_black_list_stack(ptr_t p);
#    define GC_ADD_TO_BLACK_LIST_STACK(p, source) GC_add_to_black_list_stack(p)
#  endif /* PRINT_BLACK_LIST */

/*
 * Declare an end to a black listing phase.  (I.e. signal the completion of
 * a collection.)  Turn the incomplete black lists into new black lists, etc.
 */
GC_INNER void GC_promote_black_lists(void);

/*
 * Approximately undo the effect of `GC_promote_black_lists()`.
 * This actually loses some information, but only in a reasonably safe way.
 */
GC_INNER void GC_unpromote_black_lists(void);
#endif

/*
 * The collector internal memory allocation for small objects.
 * Deallocation is not possible.  May return `NULL`.
 */
GC_INNER ptr_t GC_scratch_alloc(size_t bytes);

#ifdef GWW_VDB
/* `GC_scratch_recycle_no_gww()` is not used. */
#else
#  define GC_scratch_recycle_no_gww GC_scratch_recycle_inner
#endif
/* Reuse the memory region by the heap. */
GC_INNER void GC_scratch_recycle_inner(void *ptr, size_t sz);

#ifndef MARK_BIT_PER_OBJ
/*
 * Add a heap block map for objects of a size in granules to `GC_obj_map`.
 * A size of zero is used for large objects.  Returns `FALSE` on failure.
 */
GC_INNER GC_bool GC_add_map_entry(size_t lg);
#endif

/*
 * Same as `GC_register_displacement` but assuming the allocator lock
 * is already held.
 */
GC_INNER void GC_register_displacement_inner(size_t offset);

/*
 * Allocate a new heap block for small objects of size `lg` (in granules)
 * and `kind`.  Add all of the block's objects to the free list for objects
 * of that size.  Set all mark bits if objects are uncollectible.
 * Will fail to do anything if out of memory.
 */
GC_INNER void GC_new_hblk(size_t lg, int kind);

/*
 * Build a free list for objects of size `lg` (in granules) inside heap
 * block `h`.  Clear objects inside `h` if `clear` argument is set.
 * Add `list` to the end of the free list we build.  Return the new
 * free list.  Normally called by `GC_new_hblk()`, but this could also
 * be called without the allocator lock, if we ensure that there is no
 * concurrent collection which might reclaim objects that we have not
 * yet allocated.
 */
GC_INNER ptr_t GC_build_fl(struct hblk *h, ptr_t list, size_t lg,
                           GC_bool clear);

/*
 * Allocate (and return pointer to) a heap block for objects of the
 * given size and alignment (in bytes), searching over the appropriate
 * free block lists; inform the marker that the found block is valid
 * for objects of the indicated size.  Assumes (as implied by the argument
 * name) that `EXTRA_BYTES` value is already added to the size, if needed.
 * The client is responsible for clearing the block, if needed.
 * Note: we set `GC_obj_map` field in the header correctly; the caller
 * is responsible for building an object's free list in the block.
 */
GC_INNER struct hblk *GC_allochblk(size_t lb_adjusted, int kind,
                                   unsigned flags, size_t align_m1);

/*
 * Deallocate (free) a heap block and mark it as invalid.  Coalesce it
 * with its neighbors if possible.  All mark words are assumed to be cleared.
 */
GC_INNER void GC_freehblk(struct hblk *p);

/*  Miscellaneous GC routines. */

/*
 * This explicitly increases the size of the heap.  It is used internally,
 * but may also be invoked from `GC_expand_hp` by client.  The argument is
 * in units of `HBLKSIZE`.  (An argument of zero is treated as 1.)
 * Returns `FALSE` on failure.
 */
GC_INNER GC_bool GC_expand_hp_inner(word n);

/*
 * Restore unmarked objects to free lists, or (if `abort_if_found` is `TRUE`)
 * report them.  (I.e. perform `GC_reclaim_block()` on the entire heap,
 * after first clearing small-object free lists if we are not just looking
 * for leaks.)  Sweeping of small object pages is largely deferred.
 */
GC_INNER void GC_start_reclaim(GC_bool abort_if_found);

/*
 * Sweep blocks of the indicated object size (in granules) and kind
 * until either the appropriate nonempty free list is found, or there
 * are no more blocks to sweep.
 */
GC_INNER void GC_continue_reclaim(size_t lg, int kind);

/*
 * Reclaim all small blocks waiting to be reclaimed.  Abort and return
 * `FALSE` when/if `(*stop_func)()` returns `TRUE`.  If this returns `TRUE`,
 * then it is safe to restart the world with incorrectly cleared mark bits.
 * If `ignore_old`, then reclaim only blocks that have been reclaimed
 * recently, and discard the rest.  `stop_func` may be 0.
 */
GC_INNER GC_bool GC_reclaim_all(GC_stop_func stop_func, GC_bool ignore_old);

/*
 * Generic procedure to rebuild a free list in `hbp` with header `hhdr`,
 * with objects of size `sz` bytes.  Add `list` to the end of the free list.
 * Add the number of reclaimed bytes to `*pcount`.  Note: it could be called
 * directly from `GC_malloc_many`.
 */
GC_INNER ptr_t GC_reclaim_generic(struct hblk *hbp, hdr *hhdr, size_t sz,
                                  GC_bool init, ptr_t list, word *pcount);

/*
 * Is given heap block completely unmarked (i.e. contains no marked objects)?
 * This does not require the block to be in physical memory.
 */
GC_INNER GC_bool GC_block_empty(const hdr *hhdr);

/* Always returns 0 (`FALSE`). */
GC_INNER int GC_CALLBACK GC_never_stop_func(void);

/*
 * Stop-the-world garbage collection.  The caller must have acquired
 * the allocator lock.  If `stop_func` is not `GC_never_stop_func`, then
 * abort if `stop_func` returns `TRUE`.  Return `TRUE` if we successfully
 * completed the collection (otherwise the collection is aborted).
 */
GC_INNER GC_bool GC_try_to_collect_inner(GC_stop_func stop_func);

#define GC_gcollect_inner() (void)GC_try_to_collect_inner(GC_never_stop_func)

#ifdef THREADS
/*
 * We may currently be in thread creation or destruction.  Only set to `TRUE`
 * while the allocator lock is held.  When set, it is OK to run the garbage
 * collection from an unknown thread.  Protected by the allocator lock.
 */
GC_EXTERN GC_bool GC_in_thread_creation;
#endif

/*
 * Do `n_blocks` units of a garbage collection work, if appropriate.
 * A unit is an amount appropriate for `HBLKSIZE` bytes of allocation.
 */
GC_INNER void GC_collect_a_little_inner(size_t n_blocks);

GC_INNER void *GC_malloc_kind_aligned_global(size_t lb, int kind,
                                             size_t align_m1);

GC_INNER void *GC_generic_malloc_aligned(size_t lb, int kind, unsigned flags,
                                         size_t align_m1);

/*
 * Allocate an object of the given `kind` but assuming the allocator
 * lock is already held.  Should not be used to directly allocate
 * objects requiring special handling on allocation.  `flags` argument
 * should be 0 or `IGNORE_OFF_PAGE`; in the latter case the client
 * guarantees there will always be a pointer to the beginning (i.e.
 * within the first `hblk`) of the object while it is live.
 */
GC_INNER void *GC_generic_malloc_inner(size_t lb, int kind, unsigned flags);

/*
 * Collect or expand heap in an attempt make the indicated number of
 * free blocks available.  Should be called until the blocks are
 * available (setting `retry` value to `TRUE` unless this is the first
 * call in a loop) or until it fails by returning `FALSE`.  The `flags`
 * argument should be `IGNORE_OFF_PAGE` or 0.
 */
GC_INNER GC_bool GC_collect_or_expand(word needed_blocks, unsigned flags,
                                      GC_bool retry);

/*
 * Make the indicated object free list nonempty, and return its head (the
 * first object on the free list).  The object must be removed from the free
 * list by the caller.  The size is in granules.
 */
GC_INNER ptr_t GC_allocobj(size_t lg, int kind);

#ifdef GC_ADD_CALLER
/*
 * `GC_DBG_EXTRAS` is used by the collector debug API functions (unlike
 * `GC_EXTRAS` used by the debug API macros) thus `GC_RETURN_ADDR_PARENT`
 * (pointing to client caller) should be used if possible.
 */
#  ifdef GC_HAVE_RETURN_ADDR_PARENT
#    define GC_DBG_EXTRAS GC_RETURN_ADDR_PARENT, NULL, 0
#  else
#    define GC_DBG_EXTRAS GC_RETURN_ADDR, NULL, 0
#  endif
#else
#  define GC_DBG_EXTRAS "unknown", 0
#endif /* !GC_ADD_CALLER */

#ifdef GC_COLLECT_AT_MALLOC
/*
 * Parameter to force collection at every `malloc` of size greater or
 * equal to the given value.  This might be handy during debugging.
 * Note: this variable is visible outside for debugging purpose.
 */
extern size_t GC_dbg_collect_at_malloc_min_lb;

#  define GC_DBG_COLLECT_AT_MALLOC(lb) \
    (void)((lb) >= GC_dbg_collect_at_malloc_min_lb ? (GC_gcollect(), 0) : 0)
#else
#  define GC_DBG_COLLECT_AT_MALLOC(lb) (void)0
#endif /* !GC_COLLECT_AT_MALLOC */

/* Allocation routines that bypass the thread-local cache. */

#if defined(THREAD_LOCAL_ALLOC) && defined(GC_GCJ_SUPPORT)
/*
 * Allocate an object, clear it, and store the pointer to the type
 * structure ("vtable" in `gcj`).  This adds a byte at the end of the
 * object if `GC_malloc` would.
 */
GC_INNER void *GC_core_gcj_malloc(size_t lb, const void *vtable_ptr,
                                  unsigned flags);
#endif

GC_INNER void GC_init_headers(void);

/*
 * Install a header for block `h`.  Return `NULL` on failure, or the
 * uninitialized header otherwise.
 */
GC_INNER hdr *GC_install_header(struct hblk *h);

/*
 * Set up forwarding counts for block `h` of size `sz`.  Return `FALSE`
 * on failure.
 */
GC_INNER GC_bool GC_install_counts(struct hblk *h, size_t sz);

/* Remove the header for block `h`. */
GC_INNER void GC_remove_header(struct hblk *h);

/* Remove forwarding counts for `h`. */
GC_INNER void GC_remove_counts(struct hblk *h, size_t sz);

/* A non-macro variant of the header location routine. */
GC_INNER hdr *GC_find_header(const void *h);

/*
 * Get `HBLKSIZE`-aligned heap memory chunk from the OS and add the
 * chunk to `GC_our_memory`.  Return `NULL` if out of memory.
 */
GC_INNER ptr_t GC_os_get_mem(size_t bytes);

#if defined(NO_FIND_LEAK) && defined(SHORT_DBG_HDRS)
#  define GC_print_all_errors() (void)0
#  define GC_check_heap() (void)0
#  define GC_print_all_smashed() (void)0
#else
/*
 * Print smashed and leaked objects, if any.  Clear the lists of such
 * objects.  Called without the allocator lock held.
 */
GC_INNER void GC_print_all_errors(void);

/*
 * Check that all objects in the heap with debugging info are intact.
 * Add any that are not to `GC_smashed` list.
 */
GC_EXTERN void (*GC_check_heap)(void);

/* Print `GC_smashed` list if it is not empty.  Then clear the list. */
GC_EXTERN void (*GC_print_all_smashed)(void);
#endif

/*
 * If possible, print (using `GC_err_printf()`) a more detailed
 * description (terminated with "\n") of the object referred to by `p`.
 */
GC_EXTERN void (*GC_print_heap_obj)(ptr_t p);

GC_INNER void GC_default_print_heap_obj_proc(ptr_t p);

#if defined(LINUX) && defined(__ELF__) && !defined(SMALL_CONFIG)
/*
 * Print an address map of the process.  The caller should hold the
 * allocator lock.
 */
void GC_print_address_map(void);
#endif

#ifdef NO_FIND_LEAK
#  define GC_find_leak_inner FALSE
#else
#  define GC_find_leak_inner GC_find_leak
#  ifndef SHORT_DBG_HDRS
/*
 * Do not immediately deallocate object on `free()` in the find-leak mode,
 * just mark it as freed (and deallocate it after collection).
 */
GC_EXTERN GC_bool GC_findleak_delay_free;
#  endif
#endif /* !NO_FIND_LEAK */

#if defined(NO_FIND_LEAK) && defined(SHORT_DBG_HDRS)
#  define get_have_errors() FALSE
#elif defined(AO_HAVE_store)
#  define GC_SET_HAVE_ERRORS() AO_store(&GC_have_errors, (AO_t)TRUE)
#  define get_have_errors() \
    ((GC_bool)AO_load(&GC_have_errors)) /*< no barrier */
#else
#  define GC_SET_HAVE_ERRORS() (void)(GC_have_errors = TRUE)
/*
 * We saw a smashed or leaked object.  Call error printing routine
 * occasionally.  It is OK to read it not acquiring the allocator lock.
 * Once set to `TRUE`, it is never cleared.
 */
#  define get_have_errors() GC_have_errors
#endif /* !AO_HAVE_store */

#define VERBOSE 2
#if !defined(NO_CLOCK) || !defined(SMALL_CONFIG)
/*
 * Value of 1 generates basic collector log; `VERBOSE` generates additional
 * messages.
 */
GC_EXTERN int GC_print_stats;
#else /* SMALL_CONFIG */
/*
 * Defined as a macro to aid the compiler to remove the relevant message
 * character strings from the executable (with a particular level of
 * optimizations).
 */
#  define GC_print_stats 0
#endif

#ifdef KEEP_BACK_PTRS
/* Number of random backtraces to generate for each collection. */
GC_EXTERN long GC_backtraces;
#endif

/*
 * A trivial (linear congruential) pseudo-random numbers generator,
 * safe for the concurrent usage.
 */
#define GC_RAND_MAX ((int)(~0U >> 1))
#if defined(AO_HAVE_store) && defined(THREAD_SANITIZER)
#  define GC_RAND_STATE_T volatile AO_t
#  define GC_RAND_NEXT(pseed) GC_rand_next(pseed)
GC_INLINE int
GC_rand_next(GC_RAND_STATE_T *pseed)
{
  AO_t next = (AO_t)((AO_load(pseed) * (unsigned32)1103515245UL + 12345)
                     & (unsigned32)((unsigned)GC_RAND_MAX));
  AO_store(pseed, next);
  return (int)next;
}
#else
#  define GC_RAND_STATE_T unsigned32
#  define GC_RAND_NEXT(pseed) /*< overflow and race are OK */      \
    (int)(*(pseed) = (*(pseed) * (unsigned32)1103515245UL + 12345) \
                     & (unsigned32)((unsigned)GC_RAND_MAX))
#endif

#ifdef MAKE_BACK_GRAPH
GC_EXTERN GC_bool GC_print_back_height;
void GC_print_back_graph_stats(void);
#endif

/*
 * Explicitly deallocate the object.  `hhdr` should correspond to `base`.
 * Optionally clears (does zero-filling) whole or part of the object before
 * the deallocation.  Assumes the allocator lock is held.
 */
GC_INNER void GC_free_internal(void *base, const hdr *hhdr, size_t clear_ofs,
                               size_t clear_lb);

#ifdef VALGRIND_TRACKING
#  define FREE_PROFILER_HOOK(p) GC_free_profiler_hook(p)
#else
#  define FREE_PROFILER_HOOK(p) (void)(p)
#endif

/*
 * Macros used for collector internal allocation.  These assume the
 * allocator lock is held.
 */
#ifdef DBG_HDRS_ALL

/*
 * An allocation function for internal use.  Normally internally allocated
 * objects do not have debug information.  But in this case, we need to make
 * sure that all objects have debug headers.
 */
GC_INNER void *GC_debug_generic_malloc_inner(size_t lb, int kind,
                                             unsigned flags);

#  define GC_INTERNAL_MALLOC(lb, k) GC_debug_generic_malloc_inner(lb, k, 0)
#  define GC_INTERNAL_MALLOC_IGNORE_OFF_PAGE(lb, k) \
    GC_debug_generic_malloc_inner(lb, k, IGNORE_OFF_PAGE)
#  ifdef THREADS
/* Used internally; we assume it is called correctly. */
GC_INNER void GC_debug_free_inner(void *p);

#    define GC_INTERNAL_FREE(p) GC_debug_free_inner(p)
#  else
#    define GC_INTERNAL_FREE(p) GC_debug_free(p)
#  endif

#else
#  define GC_INTERNAL_MALLOC(lb, k) GC_generic_malloc_inner(lb, k, 0)
#  define GC_INTERNAL_MALLOC_IGNORE_OFF_PAGE(lb, k) \
    GC_generic_malloc_inner(lb, k, IGNORE_OFF_PAGE)

/* Note: the argument should have no side effects. */
#  define GC_INTERNAL_FREE(p) GC_free_internal(p, HDR(p), 0, 0)
#endif /* !DBG_HDRS_ALL */

/* Memory unmapping routines. */
#ifdef USE_MUNMAP

/*
 * Unmap blocks that have not been recently touched.  This is the only
 * way blocks are ever unmapped.
 */
GC_INNER void GC_unmap_old(unsigned threshold);

/*
 * Merge all unmapped blocks that are adjacent to other free blocks.
 * This may involve remapping, since all blocks are either fully mapped
 * or fully unmapped.  Returns `TRUE` if at least one block was merged.
 */
GC_INNER GC_bool GC_merge_unmapped(void);

GC_INNER void GC_unmap(ptr_t start, size_t bytes);
GC_INNER void GC_remap(ptr_t start, size_t bytes);

/*
 * Two adjacent blocks have already been unmapped and are about to be merged.
 * Unmap the whole block.  This typically requires that we unmap a small
 * section in the middle that was not previously unmapped due to alignment
 * constraints.
 */
GC_INNER void GC_unmap_gap(ptr_t start1, size_t bytes1, ptr_t start2,
                           size_t bytes2);
#endif

#ifdef CAN_HANDLE_FORK
/*
 * Fork-handling mode:
 *   - 0 means no `fork` handling is requested (but client could anyway
 *     call `fork()` provided it is surrounded with `GC_atfork_prepare`,
 *     `GC_atfork_parent`, `GC_atfork_child` calls);
 *   - (-1) means the collector tries to use `pthread_at_fork()` if it is
 *     available (if it succeeds, then `GC_handle_fork` value is changed to
 *     one), a portable client should nonetheless surround `fork()` with
 *     `GC_atfork_prepare()` and the accompanying routines (for the case
 *     of `pthread_at_fork()` failure or absence);
 *   - 1 (or other values) means client fully relies on `pthread_at_fork`
 *     (so if it is missing or failed, then `abort` occurs in `GC_init()`),
 *     `GC_atfork_prepare` and the accompanying routines are no-op in such
 *     a case.
 *
 * Note: the value is examined by `GC_thr_init`.
 */
GC_EXTERN int GC_handle_fork;

#  ifdef THREADS
#    if defined(SOLARIS) && !defined(_STRICT_STDC)
/* Update `pthreads` id in the child process right after `fork`. */
GC_INNER void GC_stackbase_info_update_after_fork(void);
#    else
#      define GC_stackbase_info_update_after_fork() (void)0
#    endif
#  endif
#endif /* CAN_HANDLE_FORK */

#ifdef NO_MANUAL_VDB
#  define GC_manual_vdb FALSE
#  define GC_auto_incremental GC_incremental
#  define GC_dirty(p) (void)(p)
#  define REACHABLE_AFTER_DIRTY(p) (void)(p)
#else

#  define GC_auto_incremental (GC_incremental && !GC_manual_vdb)

/*
 * Manually mark the page containing `p` as dirty.  Logically, this
 * dirties the entire object.  Does not require locking.
 * Exported and marked as `noinline` for the purpose of some clients that
 * need to patch the symbol when using write barrier validation.
 */
GC_API_PATCHABLE void GC_dirty_inner(const void *p);

#  define GC_dirty(p) (GC_manual_vdb ? GC_dirty_inner(p) : (void)0)
#  define REACHABLE_AFTER_DIRTY(p) GC_reachable_here(p)
#endif /* !NO_MANUAL_VDB */

#ifndef GC_DISABLE_INCREMENTAL
/* Virtual dirty bit (VDB) implementations; each one exports the following. */

/*
 * Initialize the virtual dirty bit implementation.  Returns `TRUE` if
 * virtual dirty bits are maintained (otherwise it is OK to be called again
 * if the client calls `GC_enable_incremental()` once more).
 */
GC_INNER GC_bool GC_dirty_init(void);

/*
 * Retrieve system dirty bits for the heap to a local buffer (unless
 * `output_unneeded`).  The caller should set `output_unneeded` to indicate
 * that reading of the retrieved dirty bits is not planned till the next
 * retrieval.  Restore the systems notion of which pages are dirty.
 * We assume that either the world is stopped or it is OK to lose dirty bits
 * while it is happening (`GC_enable_incremental()` is the caller and
 * `output_unneeded` is `TRUE` at least if the multi-threading support is on).
 */
GC_INNER void GC_read_dirty(GC_bool output_unneeded);

/*
 * Is the `HBLKSIZE`-sized page at `h` marked dirty in the local buffer?
 * If the actual page size is different, this returns `TRUE` if any of
 * the pages overlapping `h` are dirty.  This routine may err on the side
 * of labeling pages as dirty (and this implementation does).
 */
GC_INNER GC_bool GC_page_was_dirty(struct hblk *h);

/*
 * Block `h` is about to be written or allocated shortly.  Ensure that
 * all pages containing any part of the `nblocks` `hblk` entities starting
 * at `h` are no longer write-protected (by the virtual dirty bit
 * implementation).  I.e., this is a call that:
 *   - hints that [`h`, `h + nblocks`) is about to be written;
 *   - guarantees that protection is removed;
 *   - may speed up some virtual dirty bit implementations;
 *   - may be essential if we need to ensure that pointer-free system
 *     call buffers in the heap are not protected.
 */
GC_INNER void GC_remove_protection(struct hblk *h, size_t nblocks,
                                   GC_bool is_ptrfree);

#  if !defined(NO_VDB_FOR_STATIC_ROOTS) && !defined(PROC_VDB)
/* Is VDB working for static roots? */
GC_INNER GC_bool GC_is_vdb_for_static_roots(void);
#  endif

#  ifdef CAN_HANDLE_FORK
#    if defined(PROC_VDB) || defined(SOFT_VDB) \
        || (defined(MPROTECT_VDB) && defined(DARWIN) && defined(THREADS))
/*
 * Update pid-specific resources (like `/proc` file descriptors) needed
 * by the dirty bits implementation after `fork` in the child process.
 */
GC_INNER void GC_dirty_update_child(void);
#    else
#      define GC_dirty_update_child() (void)0
#    endif
#  endif /* CAN_HANDLE_FORK */

#  if defined(MPROTECT_VDB) && defined(DARWIN)
EXTERN_C_END
#    include <pthread.h>
EXTERN_C_BEGIN
#    ifdef THREADS
GC_INNER int GC_inner_pthread_create(pthread_t *t,
                                     GC_PTHREAD_CREATE_CONST pthread_attr_t *a,
                                     void *(*fn)(void *), void *arg);
#    else
#      define GC_inner_pthread_create pthread_create
#    endif
#  endif
#endif /* !GC_DISABLE_INCREMENTAL */

#if defined(COUNT_PROTECTED_REGIONS) && defined(MPROTECT_VDB)
/*
 * Do actions on heap growth, if needed, to prevent hitting the OS kernel
 * limit on the VM map regions.
 */
GC_INNER void GC_handle_protected_regions_limit(void);
#else
#  define GC_handle_protected_regions_limit() (void)0
#endif

/* Same as `GC_base` but accepts and returns a pointer to `const` object. */
#define GC_base_C(p) ((const void *)GC_base(GC_CAST_AWAY_CONST_PVOID(p)))

/* Some debugging print routines. */
void GC_print_block_list(void);
void GC_print_hblkfreelist(void);
void GC_print_heap_sects(void);
void GC_print_static_roots(void);

#ifdef KEEP_BACK_PTRS
/*
 * Store back pointer to `source` in `dest`, if that appears to be
 * possible.  This is not completely safe, since we may mistakenly
 * conclude that `dest` has a debugging wrapper.  But the error
 * probability is very small, and this should not be used in
 * production code.  We assume that `dest` is the real base pointer.
 * `source` should usually be a pointer to the interior of an object.
 */
GC_INNER void GC_store_back_pointer(ptr_t source, ptr_t dest);

GC_INNER void GC_marked_for_finalization(ptr_t dest);
#  define GC_STORE_BACK_PTR(source, dest) GC_store_back_pointer(source, dest)
#  define GC_MARKED_FOR_FINALIZATION(dest) GC_marked_for_finalization(dest)
#else
#  define GC_STORE_BACK_PTR(source, dest) (void)(source)
#  define GC_MARKED_FOR_FINALIZATION(dest)
#endif /* !KEEP_BACK_PTRS */

/* Make arguments appear live to compiler. */
void GC_noop6(word, word, word, word, word, word);

#ifndef GC_ATTR_FORMAT_PRINTF
#  if GC_GNUC_PREREQ(3, 0)
#    define GC_ATTR_FORMAT_PRINTF(spec_argnum, first_checked) \
      __attribute__((__format__(__printf__, spec_argnum, first_checked)))
#  else
#    define GC_ATTR_FORMAT_PRINTF(spec_argnum, first_checked)
#  endif
#endif

/* Logging and diagnostic output. */

/*
 * `GC_printf` is used typically on client explicit print requests.
 * A variant of `printf` that does not allocate, 1 KB total output length.
 * (It uses `sprintf()` internally; hopefully the latter does not allocate
 * memory for `long` arguments.)  For all `GC_*_printf` routines,
 * it is recommended to put "\n" at the end of `format` string (for the
 * output atomicity).
 */
GC_API_PRIV void GC_printf(const char *format, ...)
    GC_ATTR_FORMAT_PRINTF(1, 2);
GC_API_PRIV void GC_err_printf(const char *format, ...)
    GC_ATTR_FORMAT_PRINTF(1, 2);

/*
 * The basic logging routine.  Typically, it is called directly only inside
 * various `DEBUG_*` blocks.
 */
GC_API_PRIV void GC_log_printf(const char *format, ...)
    GC_ATTR_FORMAT_PRINTF(1, 2);

extern int GC_quiet; /*< visible outside currently */

#ifndef GC_ANDROID_LOG
#  define GC_PRINT_STATS_FLAG (GC_print_stats != 0)
#  define GC_INFOLOG_PRINTF GC_COND_LOG_PRINTF
/*
 * The "verbose" logging routine which is called only if `GC_print_stats`
 * is `VERBOSE`.
 */
#  define GC_verbose_log_printf GC_log_printf
#else
#  define GC_PRINT_STATS_FLAG (!GC_quiet)
/* `INFO`/`DBG` loggers are enabled even if `GC_print_stats` is off. */
#  ifndef GC_INFOLOG_PRINTF
#    define GC_INFOLOG_PRINTF \
      if (GC_quiet) {         \
      } else                  \
        GC_info_log_printf
#  endif
GC_INNER void GC_info_log_printf(const char *format, ...)
    GC_ATTR_FORMAT_PRINTF(1, 2);
GC_INNER void GC_verbose_log_printf(const char *format, ...)
    GC_ATTR_FORMAT_PRINTF(1, 2);
#endif /* GC_ANDROID_LOG */

#if defined(SMALL_CONFIG) || defined(GC_ANDROID_LOG)
#  define GC_ERRINFO_PRINTF GC_INFOLOG_PRINTF
#else
#  define GC_ERRINFO_PRINTF GC_log_printf
#endif

/*
 * Convenient wrapper macros over `GC_log_printf()` and
 * `GC_verbose_log_printf()`.
 */
#define GC_COND_LOG_PRINTF       \
  if (LIKELY(!GC_print_stats)) { \
  } else                         \
    GC_log_printf
#define GC_VERBOSE_LOG_PRINTF              \
  if (LIKELY(GC_print_stats != VERBOSE)) { \
  } else                                   \
    GC_verbose_log_printf
#ifndef GC_DBGLOG_PRINTF
#  define GC_DBGLOG_PRINTF      \
    if (!GC_PRINT_STATS_FLAG) { \
    } else                      \
      GC_log_printf
#endif

/* Write `s` to `stderr`, but do not buffer, do not add newlines, do not... */
void GC_err_puts(const char *s);

/*
 * A handy macro for logging size values (of `word` type) in KiB, rounding
 * to nearest value.
 */
#define TO_KiB_UL(v) ((unsigned long)(((v) + ((1 << 9) - 1)) >> 10))

#ifdef USE_MUNMAP
GC_EXTERN unsigned GC_unmap_threshold; /*< defined in `alloc.c` file */

/*
 * Force memory unmapping on every collection.  Has no effect on
 * implicitly-initiated collections.
 */
GC_EXTERN GC_bool GC_force_unmap_on_gcollect;
#endif

#ifdef MSWIN32
#  if (defined(_MSC_VER) && _MSC_VER >= 1800 || defined(_WIN64) \
       || defined(MSWINRT_FLAVOR))                              \
      && !defined(GC_WINNT)
/*
 * MS Visual Studio 2013 deprecates `GetVersion`, but on the other hand
 * it cannot be used to target pre-Win2K.
 */
#    define GC_WINNT
#  endif
#  ifdef GC_WINNT
#    define GC_no_win32_dlls FALSE
#    define GC_wnt TRUE
#  else
GC_EXTERN GC_bool GC_no_win32_dlls;

/* Is this a Windows NT derivative (i.e. NT, Win2K, XP or later)? */
GC_EXTERN GC_bool GC_wnt;

GC_INNER void GC_init_win32(void);
#  endif
#endif

#ifdef THREADS

#  if (defined(MSWIN32) && !defined(CONSOLE_LOG)) || defined(MSWINCE)
GC_EXTERN CRITICAL_SECTION GC_write_cs;
#    ifdef GC_ASSERTIONS
/*
 * Set to `TRUE` only if `GC_stop_world()` has acquired `GC_write_cs`.
 * Protected by `GC_write_cs`.
 */
GC_EXTERN GC_bool GC_write_disabled;
#    endif
#  endif /* MSWIN32 || MSWINCE */

#  ifdef NEED_FAULT_HANDLER_LOCK
/*
 * Acquire the spin lock we use to update dirty bits.  Threads should
 * not get stopped holding it.  But we may acquire and release it during
 * `GC_remove_protection()` call.
 */
#    define GC_acquire_dirty_lock() \
      do { /* Empty. */             \
      } while (AO_test_and_set_acquire(&GC_fault_handler_lock) == AO_TS_SET)
#    define GC_release_dirty_lock() AO_CLEAR(&GC_fault_handler_lock)
#  else
#    define GC_acquire_dirty_lock() (void)0
#    define GC_release_dirty_lock() (void)0
#  endif

#  ifdef GC_WIN32_THREADS
#    ifdef GC_NO_THREADS_DISCOVERY
#      define GC_win32_dll_threads FALSE
#    elif defined(GC_DISCOVER_TASK_THREADS)
#      define GC_win32_dll_threads TRUE
#    else
/*
 * `GC_win32_dll_threads` must be set (if needed) at the application
 * initialization time, i.e. before any collector or thread calls.
 * We make it a "dynamic" option only to avoid multiple library versions.
 */
GC_EXTERN GC_bool GC_win32_dll_threads;
#    endif
#  endif

/* This returns `FALSE` until a thread is created (or attached), at least. */
#  if !defined(GC_NO_THREADS_DISCOVERY) && defined(GC_WIN32_THREADS)
#    define GC_has_running_threads() \
      (GC_win32_dll_threads ? GC_max_thread_index > 1 : GC_need_to_lock_real)
#  else
#    define GC_has_running_threads() GC_need_to_lock_real
#  endif

#  ifdef MSWINCE
GC_EXTERN GC_bool GC_dont_query_stack_min;
#  endif

#endif /* THREADS */

#ifdef THREAD_LOCAL_ALLOC
GC_EXTERN GC_bool GC_world_stopped; /*< defined in `alloc.c` file */

/*
 * We must explicitly mark `ptrfree` and `gcj` free lists, since the
 * free list links would not otherwise be found.  We also set them in
 * the normal free lists, since that involves touching less memory than
 * if we scanned them normally.
 */
GC_INNER void GC_mark_thread_local_free_lists(void);
#endif

#if defined(GLIBC_2_19_TSX_BUG) && defined(GC_PTHREADS_PARAMARK)
/* Parse string like `<major>[.<minor>[<tail>]]` and return `major` value. */
GC_INNER int GC_parse_version(int *pminor, const char *pverstr);
#endif

#if defined(MPROTECT_VDB) && defined(GWW_VDB)
/*
 * Returns `TRUE` if `GetWriteWatch()` is available.  May be called
 * repeatedly.  May be called with or without the allocator lock held.
 */
GC_INNER GC_bool GC_gww_dirty_init(void);
#endif

#if defined(CHECKSUMS) || defined(PROC_VDB)
/* Could any valid GC heap pointer ever have been written to this page? */
GC_INNER GC_bool GC_page_was_ever_dirty(struct hblk *h);
#endif

#ifndef GC_NO_DEINIT
GC_INNER void GC_reset_freelist(void);
GC_INNER void GC_reset_obj_kinds(void);
#  ifdef CHECKSUMS
void GC_reset_check_page(void);
#  endif
#  ifdef THREADS
GC_INNER void GC_reset_threads(void);
#  endif
#  ifdef THREAD_LOCAL_ALLOC
GC_INNER void GC_reset_thread_local_initialization(void);
#  endif
#endif

#ifdef CHECKSUMS
#  ifdef MPROTECT_VDB
void GC_record_fault(struct hblk *h);
#  endif
void GC_check_dirty(void);
#endif

GC_INNER void GC_setpagesize(void);

#ifndef NO_ALL_INTERIOR_POINTERS
GC_INNER void GC_initialize_offsets(void);
#endif

#if defined(REDIR_MALLOC_AND_LINUX_THREADS)                    \
    && !defined(REDIRECT_MALLOC_IN_HEADER)                     \
    && (defined(IGNORE_FREE) || defined(REDIRECT_MALLOC_DEBUG) \
        || !defined(REDIRECT_MALLOC_UNCOLLECTABLE))
GC_INNER void GC_init_lib_bounds(void);
#else
#  define GC_init_lib_bounds() (void)0
#endif

#ifdef REDIR_MALLOC_AND_LINUX_THREADS
/*
 * Find the text (code) mapping for the library whose name, after
 * stripping the directory part, starts with `nm`.
 */
GC_INNER GC_bool GC_text_mapping(const char *nm, ptr_t *startp, ptr_t *endp);
#endif

#if defined(USE_WINALLOC) && !defined(REDIRECT_MALLOC)
GC_INNER void GC_add_current_malloc_heap(void);
#endif

#ifdef MAKE_BACK_GRAPH
/*
 * Rebuild the representation of the backward reachability graph.
 * Does not examine mark bits.  Could be called before collection.
 */
GC_INNER void GC_build_back_graph(void);

GC_INNER void GC_traverse_back_graph(void);
#endif

#ifndef ANY_MSWIN
/*
 * Is a particular static root (with the given start) registered?
 * If so, then return a pointer to it, else `NULL`.  The type is a lie,
 * since the real type does not make sense here, and we only test for `NULL`.
 */
GC_INNER void *GC_roots_present(ptr_t);
#endif

#if defined(GC_WIN32_THREADS)
/* Same as `GC_push_one` but for a sequence of registers. */
GC_INNER void GC_push_many_regs(const word *regs, unsigned count);

/*
 * Find stack with the lowest address which overlaps the interval
 * [`start`, `limit`).  Return stack bounds in `*plo` and `*phi`.
 * If no such stack is found, both `*phi` and `*plo` will be set to an
 * address higher than `limit`.
 */
GC_INNER void GC_get_next_stack(ptr_t start, ptr_t limit, ptr_t *plo,
                                ptr_t *phi);

#  if defined(MPROTECT_VDB) && !defined(CYGWIN)
GC_INNER void GC_set_write_fault_handler(void);
#  endif
#  if defined(WRAP_MARK_SOME) && !defined(GC_PTHREADS)
/*
 * Did we invalidate mark phase with an unexpected thread start?
 * Return `TRUE` if a thread was attached since we last asked or since
 * `GC_attached_thread` was explicitly reset.
 */
GC_INNER GC_bool GC_started_thread_while_stopped(void);
#  endif
#endif /* GC_WIN32_THREADS */

#if defined(MPROTECT_VDB) && defined(DARWIN) && defined(THREADS)
GC_INNER void GC_mprotect_stop(void);
GC_INNER void GC_mprotect_resume(void);
#  ifndef GC_NO_THREADS_DISCOVERY
GC_INNER void GC_darwin_register_self_mach_handler(void);
#  endif
#endif

#ifndef NOT_GCBUILD
/*
 * Iterate over forwarding addresses, if any, to get the beginning of
 * the block and its header.  Assumes `*phhdr` is non-`NULL` on entry,
 * and guarantees `*phhdr` is non-`NULL` on return.
 */
GC_INLINE struct hblk *
GC_find_starting_hblk(struct hblk *h, hdr **phhdr)
{
  hdr *hhdr = *phhdr;

  GC_ASSERT(HDR(h) == hhdr);
  for (; IS_FORWARDING_ADDR_OR_NIL(hhdr); hhdr = HDR(h)) {
    GC_ASSERT(hhdr != NULL);
    h = FORWARDED_ADDR(h, hhdr);
  }
  *phhdr = hhdr;
  return h;
}
#endif /* !NOT_GCBUILD */

#if (defined(PARALLEL_MARK)                                      \
     && !defined(HAVE_PTHREAD_SETNAME_NP_WITH_TID_AND_ARG)       \
     && (defined(HAVE_PTHREAD_SETNAME_NP_WITH_TID)               \
         || defined(HAVE_PTHREAD_SETNAME_NP_WITHOUT_TID)         \
         || defined(HAVE_PTHREAD_SET_NAME_NP)))                  \
    || (defined(DYNAMIC_LOADING)                                 \
        && ((defined(USE_PROC_FOR_LIBRARIES) && !defined(LINUX)) \
            || defined(DARWIN) || defined(IRIX5)))               \
    || defined(PROC_VDB) || defined(SOFT_VDB)
/*
 * A function to convert a long integer value `lv` to a string adding
 * the `prefix` and optional `suffix`.  The resulting string is put to
 * `buf` of the designated size (`buf_sz`).  Guaranteed to append
 * a trailing "\0" and not to exceed the buffer size.  (Note that it is
 * recommended to reserve at least 20 characters for the number part of
 * the string in `buf` to avoid a compiler warning about potential
 * number truncation.)
 */
#  ifndef GC_DISABLE_SNPRINTF
#    define GC_snprintf_s_ld_s(buf, buf_sz, prefix, lv, suffix)    \
      (void)(snprintf(buf, buf_sz, "%s%ld%s", prefix, lv, suffix), \
             (buf)[(buf_sz) - (size_t)1] = '\0')
#  else
#    define NEED_SNPRINTF_SLDS
GC_INNER void GC_snprintf_s_ld_s(char *buf, size_t buf_sz, const char *prefix,
                                 long lv, const char *suffix);
#  endif
#endif

#ifdef THREADS
#  ifndef GC_NO_FINALIZATION
/* Called by `GC_finalize()` (in case of an allocation failure observed). */
GC_INNER void GC_reset_finalizer_nested(void);

/*
 * Check and update the thread-local level of finalizers recursion.
 * Returns `NULL` if `GC_invoke_finalizers()` should not be called by
 * the collector (to minimize the risk of a deep finalizers recursion),
 * otherwise returns a pointer to the thread-local `finalizer_nested`.
 * Called by `GC_notify_or_invoke_finalizers()` only.
 */
GC_INNER unsigned char *GC_check_finalizer_nested(void);
#  endif

GC_INNER void GC_do_blocking_inner(ptr_t data, void *context);

/*
 * Should do exactly the right thing if the world is stopped; should
 * not fail if it is not stopped.
 */
GC_INNER void GC_push_all_stacks(void);

#  if defined(USE_PROC_FOR_LIBRARIES) && defined(LINUX)
GC_INNER GC_bool GC_segment_is_thread_stack(ptr_t lo, ptr_t hi);
#  endif
#  if (defined(HAVE_PTHREAD_ATTR_GET_NP) || defined(HAVE_PTHREAD_GETATTR_NP)) \
      && defined(IA64)
/*
 * Find the largest stack base smaller than `bound`.  May be used to find
 * the boundary between a register stack and adjacent immediately preceding
 * memory stack.
 */
GC_INNER ptr_t GC_greatest_stack_base_below(ptr_t bound);
#  endif
#endif /* THREADS */

#ifdef DYNAMIC_LOADING
/* Do we need to separately register the main static data segment? */
GC_INNER GC_bool GC_register_main_static_data(void);

#  ifdef DARWIN
GC_INNER void GC_init_dyld(void);
#  endif
#endif /* DYNAMIC_LOADING */

#ifdef SEARCH_FOR_DATA_START
GC_INNER void GC_init_linux_data_start(void);
#endif

#ifdef NEED_PROC_MAPS
#  if defined(DYNAMIC_LOADING) && defined(USE_PROC_FOR_LIBRARIES) \
      || defined(IA64) || defined(INCLUDE_LINUX_THREAD_DESCR)     \
      || (defined(CHECK_SOFT_VDB) && defined(MPROTECT_VDB))       \
      || defined(REDIR_MALLOC_AND_LINUX_THREADS)
/*
 * Assign various fields of the first line in `maps_ptr` to `*p_start`,
 * `*p_end`, `*p_prot`, `*p_maj_dev` and `*p_mapping_name`.
 * `p_mapping_name` may be `NULL`. `*p_prot` and `*p_mapping_name` are
 * assigned pointers into the original buffer.
 */
GC_INNER const char *GC_parse_map_entry(const char *maps_ptr, ptr_t *p_start,
                                        ptr_t *p_end, const char **p_prot,
                                        unsigned *p_maj_dev,
                                        const char **p_mapping_name);
#  endif
#  if defined(IA64) || defined(INCLUDE_LINUX_THREAD_DESCR) \
      || (defined(CHECK_SOFT_VDB) && defined(MPROTECT_VDB))
/*
 * Try to read the backing store base from `/proc/self/maps` file.
 * Return the bounds of the writable mapping with a zero major device,
 * which includes the address passed as data.  Return `FALSE` if there
 * is no such mapping.
 */
GC_INNER GC_bool GC_enclosing_writable_mapping(ptr_t addr, ptr_t *startp,
                                               ptr_t *endp);
#  endif

/*
 * Copy the contents of `/proc/self/maps` file to a buffer in our
 * address space.  Return the address of the buffer.
 */
GC_INNER const char *GC_get_maps(void);
#endif /* NEED_PROC_MAPS */

#ifdef GC_ASSERTIONS
/* Should return the same value as `GC_large_free_bytes`. */
GC_INNER word GC_compute_large_free_bytes(void);

/* Should return the same value as `GC_root_size`. */
GC_INNER word GC_compute_root_size(void);
#endif

/* Check a compile time assertion at compile time. */
#if defined(_MSC_VER) && (_MSC_VER >= 1700)
#  define GC_STATIC_ASSERT(e) static_assert(e, "static assertion failed: " #e)
#elif defined(static_assert) && !defined(CPPCHECK) \
    && (__STDC_VERSION__ >= 201112L)
#  define GC_STATIC_ASSERT(e)                                               \
    do { /* placed in `do`-`while` for proper formatting by clang-format */ \
      static_assert(e, #e);                                                 \
    } while (0)
#elif defined(mips) && !defined(__GNUC__) && !defined(CPPCHECK)
/*
 * DOB: MIPSPro C gets an internal error taking the `sizeof` an array type.
 * This code works correctly (ugliness is to avoid "unused var" warnings).
 */
#  define GC_STATIC_ASSERT(e) \
    do {                      \
      if (0) {                \
        char j[(e) ? 1 : -1]; \
        j[0] = '\0';          \
        j[0] = j[0];          \
      }                       \
    } while (0)
#else
/* The error message for failure is a bit baroque, but... */
#  define GC_STATIC_ASSERT(e) (void)sizeof(char[(e) ? 1 : -1])
#endif

/*
 * Runtime check for an argument declared as non-`NULL` is actually
 * not `NULL`.
 */
#if GC_GNUC_PREREQ(4, 0)
/* Workaround tautological-pointer-compare Clang warning. */
#  define NONNULL_ARG_NOT_NULL(arg) \
    (*CAST_THRU_UINTPTR(volatile void **, &(arg)) != NULL)
#else
#  define NONNULL_ARG_NOT_NULL(arg) ((arg) != NULL)
#endif

#define COND_DUMP_CHECKS                                             \
  do {                                                               \
    GC_ASSERT(I_HOLD_LOCK());                                        \
    GC_ASSERT(GC_compute_large_free_bytes() == GC_large_free_bytes); \
    GC_ASSERT(GC_compute_root_size() == GC_root_size);               \
  } while (0)

#ifndef NO_DEBUGGING
/* A flag to generate regular debugging dumps. */
GC_EXTERN GC_bool GC_dump_regularly;
#  define COND_DUMP                    \
    if (UNLIKELY(GC_dump_regularly)) { \
      GC_dump_named(NULL);             \
    } else                             \
      COND_DUMP_CHECKS
#else
#  define COND_DUMP COND_DUMP_CHECKS
#endif

/*
 * We need additional synchronization facilities from the multi-threading
 * support.  We believe these are less performance critical than the allocator
 * lock; standard `pthreads`-based implementations should be sufficient.
 */
#ifdef PARALLEL_MARK

/*
 * Number of mark threads we would like to have excluding the initiating
 * thread.
 */
#  define GC_markers_m1 GC_parallel

/* A flag to temporarily avoid parallel marking. */
GC_EXTERN GC_bool GC_parallel_mark_disabled;

/*
 * The routines to deal with the mark lock and condition variables.
 * If the allocator lock is also acquired, it must be done first.
 * The mark lock is used to both protect some variables used by the
 * parallel marker, and to protect `GC_fl_builder_count`.
 * `GC_notify_all_marker()` is called when the state of the parallel marker
 * changes in some significant way (see `gc_mark.h` file for details).
 * The latter set of events includes incrementing `GC_mark_no`.
 * `GC_notify_all_builder()` is called when `GC_fl_builder_count` reaches
 * zero.
 */

/*
 * Wait all markers to finish initialization (i.e. store `marker_sp`,
 * `marker_bsp`, `marker_mach_threads`, `GC_marker_Id`).
 */
GC_INNER void GC_wait_for_markers_init(void);

GC_INNER void GC_acquire_mark_lock(void);
GC_INNER void GC_release_mark_lock(void);
GC_INNER void GC_notify_all_builder(void);
GC_INNER void GC_wait_for_reclaim(void);

/*
 * Number of threads currently building free lists without holding
 * the allocator lock.  It is not safe to collect if this is nonzero.
 * Also, together with the mark lock, it is used as a semaphore during
 * marker threads startup.  Protected by the mark lock.
 */
GC_EXTERN GC_signed_word GC_fl_builder_count;

GC_INNER void GC_notify_all_marker(void);
GC_INNER void GC_wait_marker(void);

/*
 * Try to help out parallel marker, if it is running, for mark cycle
 * `my_mark_no`.  Returns if the mark cycle finishes or was already
 * done, or there was nothing to do for some other reason.  We hold the
 * mark lock only, the initiating thread holds the allocator lock.
 */
GC_INNER void GC_help_marker(word my_mark_no);

GC_INNER void GC_start_mark_threads_inner(void);

#  define INCR_MARKS(hhdr) \
    AO_store(&(hhdr)->hb_n_marks, AO_load(&(hhdr)->hb_n_marks) + 1)
#else
#  define INCR_MARKS(hhdr) (void)(++(hhdr)->hb_n_marks)
#endif /* !PARALLEL_MARK */

#if defined(SIGNAL_BASED_STOP_WORLD) && !defined(SIG_SUSPEND)
/*
 * We define the thread suspension signal here, so that we can refer
 * to it in the virtual dirty bit (VDB) implementation, if necessary.
 * Ideally we would allocate a (real-time?) signal using the standard
 * mechanism.  Unfortunately, there is no such one.  (There is one in
 * Linux `glibc`, but it is not exported.)  Thus we continue to use
 * the same hard-coded signals we have always used.
 */
#  ifdef THREAD_SANITIZER
/*
 * Unfortunately, use of an asynchronous signal to suspend threads leads to
 * the situation when the signal is not delivered (it is stored to
 * `pending_signals` in TSan runtime actually) while the destination thread
 * is blocked in `pthread_mutex_lock()`.  Thus, we use some synchronous one
 * instead (which is again unlikely to be used by clients directly).
 */
#    define SIG_SUSPEND SIGSYS
#  elif (defined(DGUX) || defined(LINUX)) && !defined(GC_USESIGRT_SIGNALS)
#    if defined(SPARC) && !defined(SIGPWR)
/*
 * Linux/SPARC does not properly define `SIGPWR` in platform `signal.h` file.
 * It is aliased to `SIGLOST` in platform `asm/signal.h` file, though.
 */
#      define SIG_SUSPEND SIGLOST
#    else
/* LinuxThreads itself uses `SIGUSR1` and `SIGUSR2`. */
#      define SIG_SUSPEND SIGPWR
#    endif
#  elif defined(FREEBSD) && defined(__GLIBC__) && !defined(GC_USESIGRT_SIGNALS)
#    define SIG_SUSPEND (32 + 6)
#  elif (defined(FREEBSD) || defined(HURD) || defined(RTEMS)) \
      && !defined(GC_USESIGRT_SIGNALS)
#    define SIG_SUSPEND SIGUSR1
/* `SIGTSTP` and `SIGCONT` could be used alternatively on FreeBSD. */
#  elif (defined(OPENBSD) && !defined(GC_USESIGRT_SIGNALS)) \
      || defined(SERENITY)
#    define SIG_SUSPEND SIGXFSZ
#  elif defined(_SIGRTMIN) && !defined(CPPCHECK)
#    define SIG_SUSPEND _SIGRTMIN + 6
#  else
#    define SIG_SUSPEND SIGRTMIN + 6
#  endif
#endif /* GC_PTHREADS && !SIG_SUSPEND */

#if defined(GC_PTHREADS) && !defined(GC_SEM_INIT_PSHARED)
#  define GC_SEM_INIT_PSHARED 0
#endif

/*
 * Some macros for `setjmp()` working across signal handlers, where
 * possible.
 */
#if (defined(UNIX_LIKE) || (defined(NEED_FIND_LIMIT) && defined(CYGWIN))) \
    && !defined(GC_NO_SIGSETJMP)
#  if defined(SUNOS5SIGS) && !defined(FREEBSD) && !defined(LINUX)
EXTERN_C_END
#    include <sys/siginfo.h>
EXTERN_C_BEGIN
#  endif
/*
 * Define `SETJMP()` and friends to be the variant restoring the signal
 * mask.
 */
#  define SETJMP(env) sigsetjmp(env, 1)
#  define LONGJMP(env, val) siglongjmp(env, val)
#  define JMP_BUF sigjmp_buf
#else
#  ifdef ECOS
#    define SETJMP(env) hal_setjmp(env)
#  else
#    define SETJMP(env) setjmp(env)
#  endif
#  define LONGJMP(env, val) longjmp(env, val)
#  define JMP_BUF jmp_buf
#endif /* !UNIX_LIKE || GC_NO_SIGSETJMP */

#ifdef DATASTART_USES_XGETDATASTART
#  ifdef FREEBSD
EXTERN_C_END
#    include <machine/trap.h>
EXTERN_C_BEGIN
#  endif
GC_INNER ptr_t GC_SysVGetDataStart(size_t, ptr_t);
#endif /* DATASTART_USES_XGETDATASTART */

#if defined(USE_PROC_FOR_LIBRARIES) && defined(THREADS) \
    || defined(NEED_FIND_LIMIT) || defined(SEARCH_FOR_DATA_START)
#  if (defined(HOST_ANDROID) || defined(__ANDROID__)) \
      && defined(IGNORE_DYNAMIC_LOADING)
/* Declared as public one in `gc.h` file. */
#  else
void *GC_find_limit(void *p, int up);
#  endif
#endif

#if defined(NEED_FIND_LIMIT)                                 \
    || (defined(UNIX_LIKE) && !defined(NO_DEBUGGING))        \
    || (defined(USE_PROC_FOR_LIBRARIES) && defined(THREADS)) \
    || (defined(WRAP_MARK_SOME) && defined(NO_SEH_AVAILABLE))
typedef void (*GC_fault_handler_t)(int);
GC_INNER void GC_set_and_save_fault_handler(GC_fault_handler_t);
#endif

#if defined(NEED_FIND_LIMIT)                                 \
    || (defined(USE_PROC_FOR_LIBRARIES) && defined(THREADS)) \
    || (defined(WRAP_MARK_SOME) && defined(NO_SEH_AVAILABLE))
GC_EXTERN JMP_BUF GC_jmp_buf;

/*
 * Set up a handler for address faults which will `longjmp`
 * to `GC_jmp_buf`.
 */
GC_INNER void GC_setup_temporary_fault_handler(void);

/* Undo the effect of `GC_setup_temporary_fault_handler`. */
GC_INNER void GC_reset_fault_handler(void);
#endif /* NEED_FIND_LIMIT || USE_PROC_FOR_LIBRARIES || WRAP_MARK_SOME */

/* Some convenience macros for cancellation support. */
#ifdef CANCEL_SAFE
#  if defined(GC_ASSERTIONS)                                            \
      && (defined(USE_COMPILER_TLS)                                     \
          || (defined(LINUX) && !defined(ARM32) && GC_GNUC_PREREQ(3, 3) \
              || defined(HPUX) /* and probably others... */))
extern __thread unsigned char GC_cancel_disable_count;
#    define NEED_CANCEL_DISABLE_COUNT
#    define INCR_CANCEL_DISABLE() ++GC_cancel_disable_count
#    define DECR_CANCEL_DISABLE() --GC_cancel_disable_count
#    define ASSERT_CANCEL_DISABLED() GC_ASSERT(GC_cancel_disable_count > 0)
#  else
#    define INCR_CANCEL_DISABLE()
#    define DECR_CANCEL_DISABLE()
#    define ASSERT_CANCEL_DISABLED() (void)0
#  endif /* !GC_ASSERTIONS */
#  define DISABLE_CANCEL(state)                               \
    do {                                                      \
      pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &state); \
      INCR_CANCEL_DISABLE();                                  \
    } while (0)
#  define RESTORE_CANCEL(state)            \
    do {                                   \
      ASSERT_CANCEL_DISABLED();            \
      pthread_setcancelstate(state, NULL); \
      DECR_CANCEL_DISABLE();               \
    } while (0)
#else
#  define DISABLE_CANCEL(state) (void)0
#  define RESTORE_CANCEL(state) (void)0
#  define ASSERT_CANCEL_DISABLED() (void)0
#endif /* !CANCEL_SAFE */

/* Multiply 32-bit unsigned values (used by `GC_push_contents_hdr()`). */
#ifdef NO_LONGLONG64
#  define LONG_MULT(hprod, lprod, x, y)                                 \
    do {                                                                \
      unsigned32 lx = (x) & (0xffffU);                                  \
      unsigned32 ly = (y) & (0xffffU);                                  \
      unsigned32 hx = (x) >> 16;                                        \
      unsigned32 hy = (y) >> 16;                                        \
      unsigned32 lxhy = lx * hy;                                        \
      unsigned32 mid = hx * ly + lxhy; /*< may overflow */              \
      unsigned32 lxly = lx * ly;                                        \
                                                                        \
      lprod = (mid << 16) + lxly; /*< may overflow */                   \
      hprod = hx * hy + ((lprod) < lxly ? 1U : 0)                       \
              + (mid < lxhy ? (unsigned32)0x10000UL : 0) + (mid >> 16); \
    } while (0)
#elif defined(I386) && defined(__GNUC__) && !defined(NACL)
#  define LONG_MULT(hprod, lprod, x, y) \
    __asm__ __volatile__("mull %2" : "=a"(lprod), "=d"(hprod) : "r"(y), "0"(x))
#else
#  if (defined(__int64) && !defined(__GNUC__) || defined(__BORLANDC__)) \
      && !defined(CPPCHECK)
#    define ULONG_MULT_T unsigned __int64
#  else
#    define ULONG_MULT_T unsigned long long
#  endif
#  define LONG_MULT(hprod, lprod, x, y)                          \
    do {                                                         \
      ULONG_MULT_T prod = (ULONG_MULT_T)(x) * (ULONG_MULT_T)(y); \
                                                                 \
      GC_STATIC_ASSERT(sizeof(x) + sizeof(y) <= sizeof(prod));   \
      hprod = (unsigned32)(prod >> 32);                          \
      lprod = (unsigned32)prod;                                  \
    } while (0)
#endif /* !I386 && !NO_LONGLONG64 */

EXTERN_C_END

#endif /* GC_PRIVATE_H */
