/*
 * Copyright (c) 2000-2005 by Hewlett-Packard Company.  All rights reserved.
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

#include "private/gc_priv.h"

#if defined(THREAD_LOCAL_ALLOC)

#  if !defined(THREADS) && !defined(CPPCHECK)
#    error Invalid config - THREAD_LOCAL_ALLOC requires GC_THREADS
#  endif

#  include "private/thread_local_alloc.h"

#  if defined(USE_COMPILER_TLS)
__thread GC_ATTR_TLS_FAST
#  elif defined(USE_WIN32_COMPILER_TLS)
__declspec(thread) GC_ATTR_TLS_FAST
#  endif
    GC_key_t GC_thread_key;

static GC_bool keys_initialized;

/*
 * Return a single nonempty free list `fl` to the global one pointed to
 * by `gfl`.
 */
static void
return_single_freelist(void *fl, void **gfl)
{
  if (NULL == *gfl) {
    *gfl = fl;
  } else {
    void *q = fl;
    void **qptr;

    GC_ASSERT(GC_size(fl) == GC_size(*gfl));
    /* Concatenate. */
    do {
      qptr = &obj_link(q);
      q = *qptr;
    } while (ADDR(q) >= HBLKSIZE);
    GC_ASSERT(NULL == q);
    *qptr = *gfl;
    *gfl = fl;
  }
}

/*
 * Recover the contents of the free-list array `fl` into the global one
 * `gfl`.
 */
static void
return_freelists(void **fl, void **gfl)
{
  int i;

  for (i = 1; i < GC_TINY_FREELISTS; ++i) {
    if (ADDR(fl[i]) >= HBLKSIZE) {
      return_single_freelist(fl[i], &gfl[i]);
    }
    /*
     * Clear `fl[i]`, since the thread structure may hang around.
     * Do it in a way that is likely to trap if we access it.
     */
    fl[i] = (ptr_t)NUMERIC_TO_VPTR(HBLKSIZE);
  }
  /* The 0 granule free list really contains 1 granule objects. */
  if (ADDR(fl[0]) >= HBLKSIZE
#  ifdef GC_GCJ_SUPPORT
      && ADDR(fl[0]) != ERROR_FL
#  endif
  ) {
    return_single_freelist(fl[0], &gfl[1]);
  }
}

#  ifdef USE_PTHREAD_SPECIFIC
/*
 * Re-set the TLS value on thread cleanup to allow thread-local allocations
 * to happen in the TLS destructors.  `GC_unregister_my_thread()` (and
 * similar routines) will finally set the `GC_thread_key` to `NULL`
 * preventing this destructor from being called repeatedly.
 */
static void
reset_thread_key(void *v)
{
  pthread_setspecific(GC_thread_key, v);
}
#  else
#    define reset_thread_key 0
#  endif

GC_INNER void
GC_init_thread_local(GC_tlfs p)
{
  int kind, j, res;

  GC_ASSERT(I_HOLD_LOCK());
  if (UNLIKELY(!keys_initialized)) {
#  ifdef USE_CUSTOM_SPECIFIC
    /* Ensure proper alignment of a "pushed" GC symbol. */
    GC_ASSERT(ADDR(&GC_thread_key) % ALIGNMENT == 0);
#  endif
    res = GC_key_create(&GC_thread_key, reset_thread_key);
    if (COVERT_DATAFLOW(res) != 0) {
      ABORT("Failed to create key for local allocator");
    }
    keys_initialized = TRUE;
  }
  res = GC_setspecific(GC_thread_key, p);
  if (COVERT_DATAFLOW(res) != 0) {
    ABORT("Failed to set thread specific allocation pointers");
  }
  for (j = 0; j < GC_TINY_FREELISTS; ++j) {
    for (kind = 0; kind < THREAD_FREELISTS_KINDS; ++kind) {
      p->_freelists[kind][j] = NUMERIC_TO_VPTR(1);
    }
#  ifdef GC_GCJ_SUPPORT
    p->gcj_freelists[j] = NUMERIC_TO_VPTR(1);
#  endif
  }
  /*
   * The zero-sized free list is handled like the regular free list, to
   * ensure that the explicit deallocation works.  However, an allocation
   * of a `gcj` object with the zero size is always an error.
   */
#  ifdef GC_GCJ_SUPPORT
  p->gcj_freelists[0] = MAKE_CPTR(ERROR_FL);
#  endif
}

GC_INNER void
GC_destroy_thread_local(GC_tlfs p)
{
  int kind;

  GC_ASSERT(I_HOLD_LOCK());
  GC_ASSERT(GC_getspecific(GC_thread_key) == p);
  /* We currently only do this from the thread itself. */
  GC_STATIC_ASSERT(THREAD_FREELISTS_KINDS <= MAXOBJKINDS);
  for (kind = 0; kind < THREAD_FREELISTS_KINDS; ++kind) {
    if (kind == (int)GC_n_kinds) {
      /* The kind is not created. */
      break;
    }
    return_freelists(p->_freelists[kind], GC_obj_kinds[kind].ok_freelist);
  }
#  ifdef GC_GCJ_SUPPORT
  return_freelists(p->gcj_freelists, (void **)GC_gcjobjfreelist);
#  endif
}

STATIC void *
GC_get_tlfs(void)
{
#  if !defined(USE_PTHREAD_SPECIFIC) && !defined(USE_WIN32_SPECIFIC)
  GC_key_t k = GC_thread_key;

  if (UNLIKELY(0 == k)) {
    /*
     * We have not yet run `GC_init_parallel()`.  That means we also
     * are not locking, so `GC_malloc_kind_global()` is fairly cheap.
     */
    return NULL;
  }
  return GC_getspecific(k);
#  else
  if (UNLIKELY(!keys_initialized))
    return NULL;

  return GC_getspecific(GC_thread_key);
#  endif
}

GC_API GC_ATTR_MALLOC void *GC_CALL
GC_malloc_kind(size_t lb, int kind)
{
  size_t lg;
  void *tsd;
  void *result;

#  if MAXOBJKINDS > THREAD_FREELISTS_KINDS
  if (UNLIKELY(kind >= THREAD_FREELISTS_KINDS))
    return GC_malloc_kind_global(lb, kind);
#  endif
  tsd = GC_get_tlfs();
  if (UNLIKELY(NULL == tsd))
    return GC_malloc_kind_global(lb, kind);

  GC_ASSERT(GC_is_initialized);
  GC_ASSERT(GC_is_thread_tsd_valid(tsd));
  lg = ALLOC_REQUEST_GRANS(lb);
#  if defined(CPPCHECK)
#    define MALLOC_KIND_PTRFREE_INIT (void *)1
#  else
#    define MALLOC_KIND_PTRFREE_INIT NULL
#  endif
  GC_FAST_MALLOC_GRANS(result, lg, ((GC_tlfs)tsd)->_freelists[kind],
                       DIRECT_GRANULES, kind, GC_malloc_kind_global(lb, kind),
                       (void)(kind == PTRFREE ? MALLOC_KIND_PTRFREE_INIT
                                              : (obj_link(result) = 0)));
#  ifdef LOG_ALLOCS
  GC_log_printf("GC_malloc_kind(%lu, %d) returned %p, recent GC #%lu\n",
                (unsigned long)lb, kind, result, (unsigned long)GC_gc_no);
#  endif
  return result;
}

#  ifdef GC_GCJ_SUPPORT

#    include "gc/gc_gcj.h"

GC_API GC_ATTR_MALLOC void *GC_CALL
GC_gcj_malloc(size_t lb, const void *vtable_ptr)
{
  void *result;
  void **tiny_fl;
  size_t lg;

  /*
   * Unlike the other thread-local allocation calls, we assume that the
   * collector has been explicitly initialized.
   */
  GC_ASSERT(GC_gcjobjfreelist != NULL);
#    if defined(USE_PTHREAD_SPECIFIC) || defined(USE_WIN32_SPECIFIC)
  GC_ASSERT(keys_initialized);
#    else
  GC_ASSERT(GC_thread_key != 0);
#    endif

  /*
   * `gcj`-style allocation without locks is extremely tricky.
   * The fundamental issue is that we may end up marking a free list,
   * which has free-list links instead of "vtable" pointers.
   * That is usually OK, since the next object on the free list will be
   * cleared, and will thus be interpreted as containing a zero descriptor.
   * That is fine if the object has not yet been initialized.  But there
   * are interesting potential races.  In the case of incremental
   * collection, this seems hopeless, since the marker may run
   * asynchronously, and may pick up the pointer to the next free-list
   * entry (which it thinks is a "vtable" pointer), get suspended for
   * a while, and then see an allocated object instead of the "vtable".
   * This may be avoidable with either a handshake with the collector or,
   * probably more easily, by moving the free list links to the second
   * "pointer-sized" word of each object.  The latter is not a universal
   * win, since on architecture like Itanium, nonzero offsets are not
   * necessarily free.  And there may be cache fill order issues.
   * For now, we punt with the incremental collection.  This probably means
   * that the incremental collection should be enabled before we create
   * a second thread.
   */
  if (UNLIKELY(GC_incremental))
    return GC_core_gcj_malloc(lb, vtable_ptr, 0 /* `flags` */);

  tiny_fl = ((GC_tlfs)GC_getspecific(GC_thread_key))->gcj_freelists;
  lg = ALLOC_REQUEST_GRANS(lb);

  /*
   * The provided `default_expr` below forces the initialization of the
   * "vtable" pointer.  This is necessary to ensure some very subtle
   * properties required if a garbage collection is run in the middle of
   * such an allocation.  Here we implicitly also assume atomicity for the
   * free list and method pointer assignments.  We must update the free list
   * before we store the pointer.  Otherwise a collection at this point
   * would see a corrupted free list.  A real memory barrier is not needed,
   * since the action of stopping this thread will cause prior writes
   * to complete.  We assert that any concurrent marker will stop us.
   * Thus it is impossible for a mark procedure to see the allocation of the
   * next object, but to see this object still containing a free-list pointer.
   * Otherwise the marker, by misinterpreting the free-list link as a "vtable"
   * pointer, might find a random "mark descriptor" in the next object.
   */
  GC_FAST_MALLOC_GRANS(
      result, lg, tiny_fl, DIRECT_GRANULES, GC_gcj_kind,
      GC_core_gcj_malloc(lb, vtable_ptr, 0 /* `flags` */), do {
        AO_compiler_barrier();
        *(const void **)result = vtable_ptr;
      } while (0));
  return result;
}

#  endif /* GC_GCJ_SUPPORT */

GC_INNER void
GC_mark_thread_local_fls_for(GC_tlfs p)
{
  ptr_t q;
  int kind, j;

  for (j = 0; j < GC_TINY_FREELISTS; ++j) {
    for (kind = 0; kind < THREAD_FREELISTS_KINDS; ++kind) {
      /*
       * Load the pointer atomically as it might be updated concurrently
       * by `GC_FAST_MALLOC_GRANS()`.
       */
      q = GC_cptr_load((volatile ptr_t *)&p->_freelists[kind][j]);
      if (ADDR(q) > HBLKSIZE)
        GC_set_fl_marks(q);
    }
#  ifdef GC_GCJ_SUPPORT
    if (LIKELY(j > 0)) {
      q = GC_cptr_load((volatile ptr_t *)&p->gcj_freelists[j]);
      if (ADDR(q) > HBLKSIZE)
        GC_set_fl_marks(q);
    }
#  endif
  }
}

#  if defined(GC_ASSERTIONS)
/* Check that all thread-local free-lists in `p` are completely marked. */
void
GC_check_tls_for(GC_tlfs p)
{
  int kind, j;

  for (j = 1; j < GC_TINY_FREELISTS; ++j) {
    for (kind = 0; kind < THREAD_FREELISTS_KINDS; ++kind) {
      GC_check_fl_marks(&p->_freelists[kind][j]);
    }
#    ifdef GC_GCJ_SUPPORT
    GC_check_fl_marks(&p->gcj_freelists[j]);
#    endif
  }
}
#  endif

#endif /* THREAD_LOCAL_ALLOC */
