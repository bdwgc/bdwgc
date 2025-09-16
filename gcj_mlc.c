/*
 * Copyright (c) 1991-1994 by Xerox Corporation.  All rights reserved.
 * Copyright (c) 1999-2004 Hewlett-Packard Development Company, L.P.
 * Copyright (c) 2008-2022 Ivan Maidanski
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

#include "private/gc_pmark.h" /*< includes `gc_priv.h` file */

#ifdef GC_GCJ_SUPPORT

/*
 * This is an allocator interface tuned for `gcj` (the GNU static Java
 * compiler).
 *
 * Each allocated object has a pointer in its beginning to a "vtable",
 * which for our purposes is simply a structure describing the type of
 * the object.  This descriptor structure contains a GC marking
 * descriptor at offset `GC_GCJ_MARK_DESCR_OFFSET`.
 *
 * It is hoped that this interface may also be useful for other systems,
 * possibly with some tuning of the constants.  But the immediate goal
 * is to get better `gcj` performance.
 *
 * We assume: counting on explicit initialization of this interface is OK.
 */

#  include "gc/gc_gcj.h"

/* Object kind for objects with descriptors in "vtable". */
int GC_gcj_kind = 0;

/* The kind of objects that are always marked with a mark procedure call. */
int GC_gcj_debug_kind = 0;

STATIC struct GC_ms_entry *GC_CALLBACK
GC_gcj_fake_mark_proc(word *addr, struct GC_ms_entry *mark_stack_top,
                      struct GC_ms_entry *mark_stack_limit, word env)
{
  UNUSED_ARG(addr);
  UNUSED_ARG(mark_stack_limit);
  UNUSED_ARG(env);
#  if defined(FUNCPTR_IS_DATAPTR) && defined(CPPCHECK)
  GC_noop1((word)(GC_funcptr_uint)(&GC_init_gcj_malloc));
#  endif
  ABORT_RET("No client gcj mark proc is specified");
  return mark_stack_top;
}

#  ifdef FUNCPTR_IS_DATAPTR
GC_API void GC_CALL
GC_init_gcj_malloc(int mp_index, void *mp)
{
  GC_init_gcj_malloc_mp((unsigned)mp_index,
                        CAST_THRU_UINTPTR(GC_mark_proc, mp),
                        GC_GCJ_MARK_DESCR_OFFSET);
}
#  endif /* FUNCPTR_IS_DATAPTR */

GC_API void GC_CALL
GC_init_gcj_malloc_mp(unsigned mp_index, GC_mark_proc mp, size_t descr_offset)
{
#  ifndef GC_IGNORE_GCJ_INFO
  GC_bool ignore_gcj_info;
#  endif

  GC_STATIC_ASSERT(GC_GCJ_MARK_DESCR_OFFSET >= sizeof(ptr_t));
  if (0 == mp) {
    /* In case `GC_DS_PROC` is unused. */
    mp = GC_gcj_fake_mark_proc;
  }

  /* Initialize the collector just in case it is not done yet. */
  GC_init();
  if (descr_offset != GC_GCJ_MARK_DESCR_OFFSET)
    ABORT("GC_init_gcj_malloc_mp: bad offset");

  LOCK();
  if (GC_gcjobjfreelist != NULL) {
    /* Already initialized. */
    UNLOCK();
    return;
  }
#  ifdef GC_IGNORE_GCJ_INFO
  /* This is useful for debugging on platforms with missing `getenv()`. */
#    define ignore_gcj_info TRUE
#  else
  ignore_gcj_info = GETENV("GC_IGNORE_GCJ_INFO") != NULL;
#  endif
  if (ignore_gcj_info) {
    GC_COND_LOG_PRINTF("Gcj-style type information is disabled!\n");
  }
  GC_ASSERT(GC_mark_procs[mp_index] == (GC_mark_proc)0); /*< unused */
  GC_mark_procs[mp_index] = mp;
  if (mp_index >= GC_n_mark_procs)
    ABORT("GC_init_gcj_malloc_mp: bad index");
  /* Set up object kind `gcj`-style indirect descriptor. */
  GC_gcjobjfreelist = (ptr_t *)GC_new_free_list_inner();
  if (ignore_gcj_info) {
    /*
     * Use a simple length-based descriptor, thus forcing a fully
     * conservative scan.
     */
    GC_gcj_kind = (int)GC_new_kind_inner((void **)GC_gcjobjfreelist,
                                         /* 0 | */ GC_DS_LENGTH, TRUE, TRUE);
    GC_gcj_debug_kind = GC_gcj_kind;
  } else {
    GC_gcj_kind = (int)GC_new_kind_inner(
        (void **)GC_gcjobjfreelist,
        (((word)(-(GC_signed_word)GC_GCJ_MARK_DESCR_OFFSET
                 - GC_INDIR_PER_OBJ_BIAS))
         | GC_DS_PER_OBJECT),
        FALSE, TRUE);
    /* Set up object kind for objects that require mark procedure call. */
    GC_gcj_debug_kind = (int)GC_new_kind_inner(
        GC_new_free_list_inner(),
        GC_MAKE_PROC(mp_index, 1 /* allocated with debug info */), FALSE,
        TRUE);
  }
  UNLOCK();
#  undef ignore_gcj_info
}

#  ifdef THREAD_LOCAL_ALLOC
GC_INNER
#  else
STATIC
#  endif
void *
GC_core_gcj_malloc(size_t lb, const void *vtable_ptr, unsigned flags)
{
  ptr_t op;
  size_t lg;

  GC_DBG_COLLECT_AT_MALLOC(lb);
  LOCK();
  if (SMALL_OBJ(lb)
      && (op = GC_gcjobjfreelist[lg = GC_size_map[lb]], LIKELY(op != NULL))) {
    GC_gcjobjfreelist[lg] = (ptr_t)obj_link(op);
    GC_bytes_allocd += GRANULES_TO_BYTES((word)lg);
    GC_ASSERT(NULL == ((void **)op)[1]);
  } else {
    /*
     * A mechanism to release the allocator lock and invoke finalizers.
     * We do not really have an opportunity to do this on a rarely
     * executed path on which the allocator lock is not held.  Thus we
     * check at a rarely executed point at which it is safe to release
     * the allocator lock; we do this even where we could just call
     * `GC_notify_or_invoke_finalizers()`, since it is probably cheaper
     * and certainly more uniform.
     */
    /* TODO: Consider doing the same elsewhere? */
    if (GC_gc_no != GC_last_finalized_no) {
      UNLOCK();
      GC_notify_or_invoke_finalizers();
      LOCK();
      GC_last_finalized_no = GC_gc_no;
    }

    op = (ptr_t)GC_generic_malloc_inner(lb, GC_gcj_kind, flags);
    if (NULL == op) {
      GC_oom_func oom_fn = GC_oom_fn;
      UNLOCK();
      return (*oom_fn)(lb);
    }
  }
  *(const void **)op = vtable_ptr;
  UNLOCK();
  GC_dirty(op);
  REACHABLE_AFTER_DIRTY(vtable_ptr);
  return GC_clear_stack(op);
}

#  ifndef THREAD_LOCAL_ALLOC
GC_API GC_ATTR_MALLOC void *GC_CALL
GC_gcj_malloc(size_t lb, const void *vtable_ptr)
{
  return GC_core_gcj_malloc(lb, vtable_ptr, 0 /* `flags` */);
}
#  endif /* !THREAD_LOCAL_ALLOC */

GC_API GC_ATTR_MALLOC void *GC_CALL
GC_gcj_malloc_ignore_off_page(size_t lb, const void *vtable_ptr)
{
  return GC_core_gcj_malloc(lb, vtable_ptr, IGNORE_OFF_PAGE);
}

#endif /* GC_GCJ_SUPPORT */
