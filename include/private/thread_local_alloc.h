/*
 * Copyright (c) 2000-2005 by Hewlett-Packard Company.  All rights reserved.
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

/*
 * Included indirectly from a thread-library-specific file.
 * This is the interface for thread-local allocation, whose implementation
 * is mostly thread-library-independent.  Here we describe only the interface
 * that needs to be known and invoked from the multi-threading support layer;
 * the actual implementation also exports `GC_malloc` and friends, which are
 * declared in `gc.h` file.
 */

#ifndef GC_THREAD_LOCAL_ALLOC_H
#define GC_THREAD_LOCAL_ALLOC_H

#include "gc_priv.h"

#ifdef THREAD_LOCAL_ALLOC

#  if defined(USE_HPUX_TLS)
#    error USE_HPUX_TLS macro was replaced by USE_COMPILER_TLS
#  endif

#  include <stdlib.h>

EXTERN_C_BEGIN

#  if !defined(USE_PTHREAD_SPECIFIC) && !defined(USE_WIN32_SPECIFIC)    \
      && !defined(USE_WIN32_COMPILER_TLS) && !defined(USE_COMPILER_TLS) \
      && !defined(USE_CUSTOM_SPECIFIC)
#    if defined(GC_WIN32_THREADS)
#      if defined(CYGWIN) && GC_GNUC_PREREQ(4, 0)
#        if defined(__clang__)
/* As of Cygwin clang-3.5.2, thread-local storage is unsupported. */
#          define USE_PTHREAD_SPECIFIC
#        else
#          define USE_COMPILER_TLS
#        endif
#      elif defined(__GNUC__) || defined(MSWINCE)
#        define USE_WIN32_SPECIFIC
#      else
#        define USE_WIN32_COMPILER_TLS
#      endif /* !GNU */

#    elif defined(HOST_ANDROID)
#      if defined(ARM32) \
          && (GC_GNUC_PREREQ(4, 6) || GC_CLANG_PREREQ_FULL(3, 8, 256229))
#        define USE_COMPILER_TLS
#      elif !defined(__clang__) && !defined(ARM32)
/* TODO: Support clang/arm64. */
#        define USE_COMPILER_TLS
#      else
#        define USE_PTHREAD_SPECIFIC
#      endif

#    elif defined(LINUX) && GC_GNUC_PREREQ(3, 3) /* `&& !HOST_ANDROID` */
#      if defined(ARM32) || defined(AVR32)
/* TODO: Change to USE_COMPILER_TLS on Linux/arm. */
#        define USE_PTHREAD_SPECIFIC
#      elif defined(AARCH64) && defined(__clang__) && !GC_CLANG_PREREQ(8, 0)
/* To avoid "R_AARCH64_ABS64 used with TLS symbol" linker warnings. */
#        define USE_PTHREAD_SPECIFIC
#      else
#        define USE_COMPILER_TLS
#      endif

#    elif (defined(COSMO) || defined(FREEBSD)                                 \
           || (defined(NETBSD) && __NetBSD_Version__ >= 600000000 /* 6.0 */)) \
        && (GC_GNUC_PREREQ(4, 4) || GC_CLANG_PREREQ(3, 9))
#      define USE_COMPILER_TLS

#    elif defined(HPUX)
#      ifdef __GNUC__
/* Empirically, as of gcc-3.3, USE_COMPILER_TLS does not work. */
#        define USE_PTHREAD_SPECIFIC
#      else
#        define USE_COMPILER_TLS
#      endif

#    elif defined(IRIX5) || defined(OPENBSD) || defined(SOLARIS) \
        || defined(NINTENDO_SWITCH) || defined(NN_PLATFORM_CTR)
/* Use our own. */
#      define USE_CUSTOM_SPECIFIC

#    else
#      define USE_PTHREAD_SPECIFIC
#    endif
#  endif /* !USE_x_SPECIFIC */

#  ifndef THREAD_FREELISTS_KINDS
#    ifdef ENABLE_DISCLAIM
#      define THREAD_FREELISTS_KINDS (NORMAL + 2)
#    else
#      define THREAD_FREELISTS_KINDS (NORMAL + 1)
#    endif
#  endif /* !THREAD_FREELISTS_KINDS */

/*
 * The first `GC_TINY_FREELISTS` free lists correspond to the first
 * `GC_TINY_FREELISTS` multiples of `GC_GRANULE_BYTES`, i.e. we keep
 * separate free lists for each multiple of `GC_GRANULE_BYTES` up to
 * `(GC_TINY_FREELISTS-1) * GC_GRANULE_BYTES`.  After that they may be
 * spread out further.
 */

/*
 * This should be used for the `tlfs` field in the structure pointed to
 * by a `GC_thread` entity.  Free lists contain either a pointer or
 * a small count reflecting the number of granules allocated at that size:
 *   - zero means thread-local allocation in use, free list is empty;
 *   - greater than zero but not greater than `DIRECT_GRANULES` means
 *     using global allocation, too few objects of this size have been
 *     allocated by this thread;
 *   - greater than `DIRECT_GRANULES` but less than `HBLKSIZE` means
 *     transition to local allocation, equivalent to zero;
 *   - not less than `HBLKSIZE` means pointer to nonempty free list.
 */

struct thread_local_freelists {
  /* Note: preserve `*_freelists` names for some clients. */
  void *_freelists[THREAD_FREELISTS_KINDS][GC_TINY_FREELISTS];
#  define ptrfree_freelists _freelists[PTRFREE]
#  define normal_freelists _freelists[NORMAL]
#  ifdef GC_GCJ_SUPPORT
  void *gcj_freelists[GC_TINY_FREELISTS];
  /* A value used for `gcj_freelists[-1]`; allocation is erroneous. */
#    define ERROR_FL GC_WORD_MAX
#  endif

  /* Do not use local free lists for up to this much allocation. */
#  define DIRECT_GRANULES (HBLKSIZE / GC_GRANULE_BYTES)
};
typedef struct thread_local_freelists *GC_tlfs;

#  if defined(USE_PTHREAD_SPECIFIC)
#    define GC_getspecific pthread_getspecific
#    define GC_setspecific pthread_setspecific
#    define GC_key_create pthread_key_create
/*
 * Explicitly delete the value to stop the TLS (thread-local storage)
 * destructor from being called repeatedly.
 */
#    define GC_remove_specific(key) (void)pthread_setspecific(key, NULL)
#    define GC_remove_specific_after_fork(key, t) \
      (void)0 /*< no action needed */
typedef pthread_key_t GC_key_t;
#  elif defined(USE_COMPILER_TLS) || defined(USE_WIN32_COMPILER_TLS)
#    define GC_getspecific(x) (x)
#    define GC_setspecific(key, v) ((key) = (v), 0)
#    define GC_key_create(key, d) 0
/* Just to clear the pointer to `tlfs`. */
#    define GC_remove_specific(key) (void)GC_setspecific(key, NULL)
#    define GC_remove_specific_after_fork(key, t) (void)0
typedef void *GC_key_t;
#  elif defined(USE_WIN32_SPECIFIC)
#    define GC_getspecific TlsGetValue
/* Note: we assume that zero means success, Win32 API does the opposite. */
#    define GC_setspecific(key, v) !TlsSetValue(key, v)
#    ifndef TLS_OUT_OF_INDEXES
/* This is currently missing in WinCE. */
#      define TLS_OUT_OF_INDEXES (DWORD)0xFFFFFFFF
#    endif
#    define GC_key_create(key, d) \
      ((d) != 0 || (*(key) = TlsAlloc()) == TLS_OUT_OF_INDEXES ? -1 : 0)
/* TODO: Is `TlsFree` needed on process exit/detach? */
#    define GC_remove_specific(key) (void)GC_setspecific(key, NULL)
#    define GC_remove_specific_after_fork(key, t) (void)0
typedef DWORD GC_key_t;
#  elif defined(USE_CUSTOM_SPECIFIC)
EXTERN_C_END
#    include "specific.h"
EXTERN_C_BEGIN
#  else
#    error implement me
#  endif

/*
 * Each thread structure must be initialized.  This call must be made from
 * the new thread.  Caller should hold the allocator lock.
 */
GC_INNER void GC_init_thread_local(GC_tlfs p);

/*
 * Called when a thread is unregistered, or exits.  Caller should hold
 * the allocator lock.
 */
GC_INNER void GC_destroy_thread_local(GC_tlfs p);

/*
 * The multi-threading support layer must arrange to mark thread-local free
 * lists explicitly, since the link field is often invisible to the marker.
 * It knows how to find all threads; we take care of an individual thread
 * free-list structure.
 */
GC_INNER void GC_mark_thread_local_fls_for(GC_tlfs p);

#  ifdef GC_ASSERTIONS
GC_bool GC_is_thread_tsd_valid(void *tsd);
void GC_check_tls_for(GC_tlfs p);
#    if defined(USE_CUSTOM_SPECIFIC)
void GC_check_tsd_marks(tsd *key);
#    endif
#  endif /* GC_ASSERTIONS */

#  ifndef GC_ATTR_TLS_FAST
#    define GC_ATTR_TLS_FAST /*< empty */
#  endif

/*
 * This is set up by `GC_init_thread_local()`.  No need for cleanup on
 * thread exit.  But the multi-threading support layer makes sure that
 * `GC_thread_key` is traced, if necessary.
 */
extern
#  if defined(USE_COMPILER_TLS)
    __thread GC_ATTR_TLS_FAST
#  elif defined(USE_WIN32_COMPILER_TLS)
    __declspec(thread) GC_ATTR_TLS_FAST
#  endif
        GC_key_t GC_thread_key;

EXTERN_C_END

#endif /* THREAD_LOCAL_ALLOC */

#endif /* GC_THREAD_LOCAL_ALLOC_H */
