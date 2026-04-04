/*
 * Copyright (c) 1994 by Xerox Corporation.  All rights reserved.
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

// This implementation module for `gc_cpp.h` file provides an implementation
// of the global operators `new`/`delete` that calls the memory allocator of
// the collector.  All objects allocated by this implementation will be
// uncollectible but part of the root set of the collector.
//
// You should ensure (using implementation-dependent techniques) that the
// linker finds this module before the library that defines the default
// built-in operators `new` and `delete`.

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#ifndef GC_BUILD
#  define GC_BUILD
#endif

#define GC_DONT_INCL_WINDOWS_H
#include "gc/gc.h"

#ifndef GC_INCLUDE_NEW
#  define GC_INCLUDE_NEW
#endif
#include "gc/gc_cpp.h"

#if (!defined(_MSC_VER) && !defined(__DMC__) \
     || defined(GC_NO_INLINE_STD_NEW))       \
    && !defined(GC_INLINE_STD_NEW) && !defined(SKIP_GCCPP_DEFINITIONS)

#  if defined(GC_NEW_ABORTS_ON_OOM) || defined(_LIBCPP_NO_EXCEPTIONS)
#    define GC_ALLOCATOR_THROW_OR_ABORT() GC_abort_on_oom()
#  else
// Use `bad_alloc()` directly instead of `GC_throw_bad_alloc()` call.
#    define GC_ALLOCATOR_THROW_OR_ABORT() throw std::bad_alloc()
#  endif

void *
operator new(GC_SIZE_T size) GC_DECL_NEW_THROW
{
  void *obj = GC_MALLOC_UNCOLLECTABLE(size);
  if (0 == obj)
    GC_ALLOCATOR_THROW_OR_ABORT();
  return obj;
}

#  ifdef _MSC_VER
// This operator `new` is used by VC++ in case of `Debug` builds.
void *
operator new(GC_SIZE_T size, int /* `nBlockUse` */, const char *szFileName,
             int nLine)
{
#    ifdef GC_DEBUG
  void *obj = GC_debug_malloc_uncollectable(size, szFileName, nLine);
#    else
  void *obj = GC_MALLOC_UNCOLLECTABLE(size);
  (void)szFileName;
  (void)nLine;
#    endif
  if (0 == obj)
    GC_ALLOCATOR_THROW_OR_ABORT();
  return obj;
}
#  endif // _MSC_VER

void
operator delete(void *obj) GC_NOEXCEPT
{
  GC_FREE(obj);
}

#  ifdef GC_OPERATOR_NEW_NOTHROW
void *
operator new(GC_SIZE_T size, const std::nothrow_t &) GC_NOEXCEPT
{
  return GC_MALLOC_UNCOLLECTABLE(size);
}

void
operator delete(void *obj, const std::nothrow_t &) GC_NOEXCEPT
{
  GC_FREE(obj);
}
#  endif // GC_OPERATOR_NEW_NOTHROW

#  ifdef GC_OPERATOR_NEW_ARRAY
void *
operator new[](GC_SIZE_T size) GC_DECL_NEW_THROW
{
  void *obj = GC_MALLOC_UNCOLLECTABLE(size);
  if (0 == obj)
    GC_ALLOCATOR_THROW_OR_ABORT();
  return obj;
}

#    ifdef _MSC_VER
// This operator `new` is used by VC++ 7 (or later) in `Debug` builds.
void *
operator new[](GC_SIZE_T size, int nBlockUse, const char *szFileName,
               int nLine)
{
  return operator new(size, nBlockUse, szFileName, nLine);
}
#    endif // _MSC_VER

void
operator delete[](void *obj) GC_NOEXCEPT
{
  GC_FREE(obj);
}

#    ifdef GC_OPERATOR_NEW_NOTHROW
void *
operator new[](GC_SIZE_T size, const std::nothrow_t &) GC_NOEXCEPT
{
  return GC_MALLOC_UNCOLLECTABLE(size);
}

void
operator delete[](void *obj, const std::nothrow_t &) GC_NOEXCEPT
{
  GC_FREE(obj);
}
#    endif
#  endif // GC_OPERATOR_NEW_ARRAY

#  ifdef GC_OPERATOR_SIZED_DELETE
void
operator delete(void *obj, GC_SIZE_T) GC_NOEXCEPT
{
  GC_FREE(obj);
}

#    ifdef GC_OPERATOR_NEW_ARRAY
void
operator delete[](void *obj, GC_SIZE_T) GC_NOEXCEPT
{
  GC_FREE(obj);
}
#    endif
#  endif // GC_OPERATOR_SIZED_DELETE

#endif // !_MSC_VER && !__DMC__ || GC_NO_INLINE_STD_NEW
