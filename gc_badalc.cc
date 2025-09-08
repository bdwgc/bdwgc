/*
 * Copyright (c) 2018-2020 Ivan Maidanski
 *
 * THIS MATERIAL IS PROVIDED AS IS, WITH ABSOLUTELY NO WARRANTY EXPRESSED
 * OR IMPLIED.  ANY USE IS AT YOUR OWN RISK.
 *
 * Permission is hereby granted to use or copy this program
 * for any purpose, provided the above notices are retained on all copies.
 * Permission to modify the code and to distribute modified code is granted,
 * provided the above notices are retained, and a notice that the code was
 * modified is included with the above copyright notice.
 *
 */

// This file provides the implementation of `GC_throw_bad_alloc()` which
// is invoked by the collector operator `new` in case of an out-of-memory
// event.

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#ifndef GC_BUILD
#  define GC_BUILD
#endif

#define GC_DONT_INCL_WINDOWS_H
#include "gc/gc.h"

#include <new> /*< for `bad_alloc`, precedes include of `gc_cpp.h` file */

#if defined(GC_NEW_ABORTS_ON_OOM) || defined(_LIBCPP_NO_EXCEPTIONS)
#  define GC_ALLOCATOR_THROW_OR_ABORT() GC_abort_on_oom()
#else
#  define GC_ALLOCATOR_THROW_OR_ABORT() throw std::bad_alloc()
#endif

GC_API GC_OOM_ABORT_THROW_ATTRIBUTE void GC_CALL
GC_throw_bad_alloc()
{
  GC_ALLOCATOR_THROW_OR_ABORT();
}
