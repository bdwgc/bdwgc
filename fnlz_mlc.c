/*
 * Copyright (c) 2011 by Hewlett-Packard Company.  All rights reserved.
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

#ifdef ENABLE_DISCLAIM

#  include "gc/gc_disclaim.h"
#  include "private/dbg_mlc.h" /*< for `oh` type */

#  if defined(KEEP_BACK_PTRS) || defined(MAKE_BACK_GRAPH)
/* The first bit is already used for a debug purpose. */
#    define FINALIZER_CLOSURE_FLAG 0x2
#  else
#    define FINALIZER_CLOSURE_FLAG 0x1
#  endif

STATIC int GC_CALLBACK
GC_finalized_disclaim(void *obj)
{
#  ifdef AO_HAVE_load
  ptr_t fc_p = GC_cptr_load((volatile ptr_t *)obj);
#  else
  ptr_t fc_p = *(ptr_t *)obj;
#  endif

  if ((ADDR(fc_p) & FINALIZER_CLOSURE_FLAG) != 0) {
    /*
     * The disclaim function may be passed fragments from the free-list,
     * on which it should not run finalization.  To recognize this case,
     * we use the fact that the value of the first pointer of such
     * fragments is always, at least, multiple of a pointer size (a link
     * to the next fragment, or `NULL`).
     *
     * Note: if it is desirable to have a finalizer which does not use
     * the first pointer for storing the finalization information,
     * `GC_disclaim_and_reclaim()` must be extended to clear fragments
     * so that the assumption holds for the selected pointer location.
     */
    const struct GC_finalizer_closure *fc
        = (struct GC_finalizer_closure *)CPTR_CLEAR_FLAGS(
            fc_p, FINALIZER_CLOSURE_FLAG);

    GC_ASSERT(!GC_find_leak_inner);
    fc->proc((ptr_t *)obj + 1, fc->cd);
  }
  return 0;
}

STATIC void
GC_register_disclaim_proc_inner(unsigned kind, GC_disclaim_proc proc,
                                GC_bool mark_unconditionally)
{
  GC_ASSERT(kind < MAXOBJKINDS);
  if (UNLIKELY(GC_find_leak_inner))
    return;

  GC_obj_kinds[kind].ok_disclaim_proc = proc;
  GC_obj_kinds[kind].ok_mark_unconditionally = mark_unconditionally;
}

GC_API void GC_CALL
GC_init_finalized_malloc(void)
{
  /* Initialize the collector just in case it is not done yet. */
  GC_init();

  LOCK();
  if (GC_finalized_kind != 0) {
    UNLOCK();
    return;
  }

  /*
   * The finalizer closure is placed in the first pointer of the
   * object in order to use the lower bits to distinguish live
   * objects from objects on the free list.  The downside of this is
   * that we need one-pointer offset interior pointers, and that
   * `GC_base()` does not return the start of the user region.
   */
  GC_register_displacement_inner(sizeof(ptr_t));

  /*
   * And, the pointer to the finalizer closure object itself is displaced
   * due to baking in this indicator.
   */
  GC_register_displacement_inner(FINALIZER_CLOSURE_FLAG);
  GC_register_displacement_inner(sizeof(oh) | FINALIZER_CLOSURE_FLAG);

  GC_finalized_kind
      = GC_new_kind_inner(GC_new_free_list_inner(), GC_DS_LENGTH, TRUE, TRUE);
  GC_ASSERT(GC_finalized_kind != 0);
  GC_register_disclaim_proc_inner(GC_finalized_kind, GC_finalized_disclaim,
                                  TRUE);
  UNLOCK();
}

GC_API void GC_CALL
GC_register_disclaim_proc(int kind, GC_disclaim_proc proc,
                          int mark_unconditionally)
{
  LOCK();
  GC_register_disclaim_proc_inner((unsigned)kind, proc,
                                  (GC_bool)mark_unconditionally);
  UNLOCK();
}

GC_API GC_ATTR_MALLOC void *GC_CALL
GC_finalized_malloc(size_t lb, const struct GC_finalizer_closure *fclos)
{
  void *op;
  ptr_t fc_p;

#  ifndef LINT2
  /* Actually, there is no data race because the variable is set once. */
  GC_ASSERT(GC_finalized_kind != 0);
#  endif
  GC_ASSERT(NONNULL_ARG_NOT_NULL(fclos));
  GC_ASSERT((ADDR(fclos) & FINALIZER_CLOSURE_FLAG) == 0);
  op = GC_malloc_kind(SIZET_SAT_ADD(lb, sizeof(ptr_t)),
                      (int)GC_finalized_kind);
  if (UNLIKELY(NULL == op))
    return NULL;

  /*
   * Set the flag (w/o conversion to a numeric type) and store
   * the finalizer closure.
   */
  fc_p = CPTR_SET_FLAGS(GC_CAST_AWAY_CONST_PVOID(fclos),
                        FINALIZER_CLOSURE_FLAG);
#  ifdef AO_HAVE_store
  GC_cptr_store((volatile ptr_t *)op, fc_p);
#  else
  *(ptr_t *)op = fc_p;
#  endif
  GC_dirty(op);
  REACHABLE_AFTER_DIRTY(fc_p);
  return (ptr_t *)op + 1;
}

#endif /* ENABLE_DISCLAIM */
