/*
 * Copyright (c) 1996-1998 by Silicon Graphics.  All rights reserved.
 * Copyright (c) 2018-2025 Ivan Maidanski
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

/* This file is kept for a binary compatibility purpose only. */

#ifndef GC_ALLOC_PTRS_H
#define GC_ALLOC_PTRS_H

#include "gc/gc.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef GC_API_PRIV
#  define GC_API_PRIV GC_API
#endif

/*
 * Some compilers do not accept `const` together with the `dllimport`
 * attribute, so the symbols below are exported as non-constant ones.
 */
#ifndef GC_APIVAR_CONST
#  if defined(GC_BUILD) || !defined(GC_DLL)
#    define GC_APIVAR_CONST const
#  else
#    define GC_APIVAR_CONST /*< empty */
#  endif
#endif

GC_API_PRIV void **GC_APIVAR_CONST GC_objfreelist_ptr;
GC_API_PRIV void **GC_APIVAR_CONST GC_aobjfreelist_ptr;
GC_API_PRIV void **GC_APIVAR_CONST GC_uobjfreelist_ptr;

#ifdef GC_ATOMIC_UNCOLLECTABLE
GC_API_PRIV void **GC_APIVAR_CONST GC_auobjfreelist_ptr;
#endif

/*
 * Manually update the number of bytes allocated during the current
 * collection cycle and the number of explicitly deallocated bytes of
 * memory since the last collection, respectively.  Both functions are
 * unsynchronized, `GC_call_with_alloc_lock()` should be used to avoid
 * data race.
 */
GC_API_PRIV void GC_CALL GC_incr_bytes_allocd(size_t /* `bytes` */);
GC_API_PRIV void GC_CALL GC_incr_bytes_freed(size_t /* `bytes` */);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* GC_ALLOC_PTRS_H */
