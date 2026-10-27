/*
 * Copyright (c) 2007-2011 by Hewlett-Packard Company. All rights reserved.
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

#ifndef GC_DISCLAIM_H
#define GC_DISCLAIM_H

#include "gc.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * This API is defined only if the library has been suitably compiled
 * (i.e. with `ENABLE_DISCLAIM` macro defined).
 */

/**
 * Prepare the object kind used by `GC_finalized_malloc`.  Call it from
 * your initialization code or, at least, at some point before using
 * finalized allocations.  The function is thread-safe.
 */
GC_API void GC_CALL GC_init_finalized_malloc(void);

/** Type of a disclaim callback.  Called with the allocator lock held. */
typedef int(GC_CALLBACK *GC_disclaim_proc)(void * /* `obj` */);

/**
 * Register `proc` to be called on each object (of given `kind`) ready to be
 * reclaimed.  If `proc()` returns a nonzero value, the collector will not
 * reclaim the object on this collection cycle (`proc()` should not try to
 * resurrect the object otherwise); objects reachable from `proc()`
 * (including the referred closure object) will be protected from collection
 * if `mark_unconditionally` is nonzero, but at the expense that long chains
 * of objects will take many cycles to reclaim.  A non-zero value of
 * `mark_unconditionally` implies the client should be careful not to create
 * a pointer cycle involving the allocated object, as it would prevent
 * collection of the object.  Any call to `GC_free()` (or friends) deallocates
 * the object (pointed to by the argument) without inquiring `proc()`.
 * Acquires the allocator lock.  No-op in the find-leak mode.
 * Note: the internal `GC_disclaim_and_reclaim()` invokes the disclaim
 * callback for every slot (including free-list fragments) in a heap block
 * that has no mark bit set - client should take care not to misinterpret
 * the object's first "pointer-sized" which is also used as a link pointer to
 * the next free object.
 */
GC_API void GC_CALL
GC_register_disclaim_proc(int /* `kind` */, GC_disclaim_proc /* `proc` */,
                          int /* `mark_unconditionally` */);

/** The finalizer closure used by `GC_finalized_malloc`. */
struct GC_finalizer_closure {
  GC_finalization_proc proc;
  void *cd;
};

/**
 * Allocate an object which is to be finalized by the given closure.
 * This uses a dedicated object kind with a disclaim procedure, and
 * is more efficient than `GC_register_finalizer` and friends.
 * `GC_init_finalized_malloc` must be called before using this.
 * The collector will reclaim the object during this collection cycle
 * (thus, `fc->proc()` should not try to resurrect the object).
 * The other objects reachable from `fc->proc()` (including the closure
 * object in case it is a heap-allocated one) will be protected from
 * collection.  As a consequence, the client should guarantee no pointer
 * cycles involving the allocated object, otherwise this would prevent
 * collection of the object.  The start of the allocated object is
 * pointer-aligned but not granule-aligned (having the default granule size,
 * unlike `GC_malloc` function).  Note also that `GC_size()` (for the
 * allocated object) returns a value slightly bigger than the specified
 * allocation size, and that `GC_base()` result points to a "pointer-sized"
 * word prior to the start of the allocated object.
 * The disclaim procedure is not invoked in the find-leak mode.
 * There is no debugging variant of this allocation function.
 */
GC_API GC_ATTR_MALLOC GC_ATTR_ALLOC_SIZE(1) void *GC_CALL
    GC_finalized_malloc(size_t /* `size` */,
                        const struct GC_finalizer_closure * /* `fc` */)
        GC_ATTR_NONNULL(2);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif
