/*
 * Copyright (c) 1991-1994 by Xerox Corporation.  All rights reserved.
 * Copyright (c) 1997 by Silicon Graphics.  All rights reserved.
 * Copyright (c) 2000 by Hewlett-Packard Company.  All rights reserved.
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

#include "private/gc_priv.h"

/*
 * This is located in this file (rather than in `dyn_load.c` file) to
 * avoid having to link against `libdl` library if the client does not
 * call `dlopen()`.  Of course, this is meaningless if the collector is
 * in a dynamic library.
 */
#if defined(GC_PTHREADS) && !defined(GC_NO_DLOPEN)

#  undef GC_MUST_RESTORE_REDEFINED_DLOPEN
#  if defined(dlopen) && !defined(GC_USE_LD_WRAP)
/*
 * To support various threads packages, `gc.h` file interposes on `dlopen`
 * by defining the latter to be `GC_dlopen`, which is implemented below.
 * However, `GC_dlopen()` itself uses the real system `dlopen()`, thus
 * we first remove `dlopen` definition by `gc.h` file and restore it later,
 * after `GC_dlopen()` definition.
 */
#    undef dlopen
#    define GC_MUST_RESTORE_REDEFINED_DLOPEN
#  endif

/*
 * Redefine `dlopen` to guarantee mutual exclusion with
 * `GC_register_dynamic_libraries()`.  Should probably happen for
 * other operating systems, too.
 */

/* This is similar to `WRAP_FUNC`/`REAL_FUNC` in `pthread_support.c` file. */
#  ifdef GC_USE_LD_WRAP
#    define WRAP_DLFUNC(f) __wrap_##f
#    define REAL_DLFUNC(f) __real_##f
void *REAL_DLFUNC(dlopen)(const char *, int);
#  else
#    define WRAP_DLFUNC(f) GC_##f
#    define REAL_DLFUNC(f) f
#  endif

#  define GC_wrap_dlopen WRAP_DLFUNC(dlopen)
GC_API void *
GC_wrap_dlopen(const char *path, int mode)
{
  void *result;

#  ifndef USE_PROC_FOR_LIBRARIES
  /*
   * Make sure we are not in the middle of a collection, then disable
   * collections.  This is invoked prior to a `dlopen` call to avoid
   * synchronization issues.  We cannot just acquire the allocator lock,
   * since startup code in `dlopen` may try to allocate.
   * This solution risks heap growth (or, even, heap overflow) in the
   * presence of many `dlopen` calls in either a multi-threaded environment,
   * or if the library initialization code allocates substantial amounts of
   * garbage-collected memory.  But there seems no better solutions.
   */
  GC_finish_and_disable();
#  endif
  result = REAL_DLFUNC(dlopen)(path, mode);
#  ifndef USE_PROC_FOR_LIBRARIES
  GC_enable();
#  endif
  return result;
}
#  undef GC_wrap_dlopen

/*
 * Define `GC_` function as an alias for the plain one, which will be
 * intercepted.  This allows files that include `gc.h` file, and hence
 * generate references to the `GC_` symbol, to see the right symbol.
 */
#  ifdef GC_USE_LD_WRAP

GC_API void *
GC_dlopen(const char *path, int mode)
{
  return dlopen(path, mode);
}
#  endif /* GC_USE_LD_WRAP */

#  ifdef GC_MUST_RESTORE_REDEFINED_DLOPEN
#    define dlopen GC_dlopen
#  endif

#endif /* GC_PTHREADS && !GC_NO_DLOPEN */
