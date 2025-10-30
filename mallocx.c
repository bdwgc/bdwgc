/*
 * Copyright 1988, 1989 Hans-J. Boehm, Alan J. Demers
 * Copyright (c) 1991-1994 by Xerox Corporation.  All rights reserved.
 * Copyright (c) 1996 by Silicon Graphics.  All rights reserved.
 * Copyright (c) 2000 by Hewlett-Packard Company.  All rights reserved.
 * Copyright (c) 2009-2025 Ivan Maidanski
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
 * These are extra allocation routines that are likely to be less
 * frequently used than those in `malloc.c` file.  They are separate in
 * the hope that the `.o` file will be excluded from statically linked
 * executables.  We should probably break this up further.
 */

#include <string.h>

#ifndef MSWINCE
#  include <errno.h>
#endif

/*
 * Some externally visible but unadvertised variables to allow access
 * to free lists from inlined allocators without include `gc_priv.h` file
 * or introducing dependencies on internal data structure layouts.
 */
#include "private/gc_alloc_ptrs.h"
void **const GC_objfreelist_ptr = GC_objfreelist;
void **const GC_aobjfreelist_ptr = GC_aobjfreelist;
void **const GC_uobjfreelist_ptr = GC_uobjfreelist;
#ifdef GC_ATOMIC_UNCOLLECTABLE
void **const GC_auobjfreelist_ptr = GC_auobjfreelist;
#endif

GC_API int GC_CALL
GC_get_kind_and_size(const void *p, size_t *psize)
{
  const hdr *hhdr = HDR(p);

  if (psize != NULL) {
    *psize = hhdr->hb_sz;
  }
  return hhdr->hb_obj_kind;
}

GC_API GC_ATTR_MALLOC void *GC_CALL
GC_generic_or_special_malloc(size_t lb, int kind)
{
  switch (kind) {
  case PTRFREE:
  case NORMAL:
    return GC_malloc_kind(lb, kind);
  case UNCOLLECTABLE:
#ifdef GC_ATOMIC_UNCOLLECTABLE
  case AUNCOLLECTABLE:
#endif
    return GC_generic_malloc_uncollectable(lb, kind);
  default:
    return GC_generic_malloc_aligned(lb, kind, 0 /* `flags` */, 0);
  }
}

GC_API void *GC_CALL
GC_realloc(void *p, size_t lb)
{
  hdr *hhdr;
  void *result;
#if defined(_FORTIFY_SOURCE) && defined(__GNUC__) && !defined(__clang__)
  /*
   * Use `cleared_p` instead of `p` as a workaround to avoid passing
   * `alloc_size(lb)` attribute associated with `p` to `memset`
   * (including a `memset` call inside `GC_free`).
   */
  volatile GC_uintptr_t cleared_p = (GC_uintptr_t)p;
#else
#  define cleared_p p
#endif
  size_t sz;      /*< current size in bytes */
  size_t orig_sz; /*< original `sz` (in bytes) */
  int obj_kind;

  if (NULL == p) {
    /* Required by ANSI. */
    return GC_malloc(lb);
  }
  if (0 == lb) /* `&& p != NULL` */ {
#ifndef IGNORE_FREE
    GC_free(p);
#endif
    return NULL;
  }
  hhdr = HDR(HBLKPTR(p));
  sz = hhdr->hb_sz;
  obj_kind = hhdr->hb_obj_kind;
  orig_sz = sz;

  if (sz > MAXOBJBYTES) {
    const struct obj_kind *ok = &GC_obj_kinds[obj_kind];
    word descr = ok->ok_descriptor;

    /* Round it up to the next whole heap block. */
    sz = (sz + HBLKSIZE - 1) & ~(HBLKSIZE - 1);
#if ALIGNMENT > GC_DS_TAGS
    /*
     * An extra byte is not added in case of ignore-off-page allocated
     * objects not smaller than `HBLKSIZE`.
     */
    GC_ASSERT(sz >= HBLKSIZE);
    if (EXTRA_BYTES != 0 && (hhdr->hb_flags & IGNORE_OFF_PAGE) != 0
        && obj_kind == NORMAL)
      descr += ALIGNMENT; /*< or set to 0 */
#endif
    if (ok->ok_relocate_descr) {
      descr += sz;
    }

    /*
     * `GC_realloc` might be changing the block size while
     * `GC_reclaim_block` or `GC_clear_hdr_marks` is examining it.
     * The change to the size field is benign, in that `GC_reclaim`
     * (and `GC_clear_hdr_marks`) would work correctly with either
     * value, since we are not changing the number of objects in
     * the block.  But seeing a half-updated value (though unlikely
     * to occur in practice) could be probably bad.
     * Using unordered atomic accesses on `hb_sz` and `hb_descr`
     * fields would solve the issue.  (The alternate solution might
     * be to initially overallocate large objects, so we do not
     * have to adjust the size in `GC_realloc`, if they still fit.
     * But that is probably more expensive, since we may end up
     * scanning a bunch of zeros during the collection.)
     */
#ifdef AO_HAVE_store
    AO_store(&hhdr->hb_sz, sz);
    AO_store((AO_t *)&hhdr->hb_descr, descr);
#else
    {
      LOCK();
      hhdr->hb_sz = sz;
      hhdr->hb_descr = descr;
      UNLOCK();
    }
#endif

#ifdef MARK_BIT_PER_OBJ
    GC_ASSERT(hhdr->hb_inv_sz == LARGE_INV_SZ);
#else
    GC_ASSERT((hhdr->hb_flags & LARGE_BLOCK) != 0
              && hhdr->hb_map[ANY_INDEX] == 1);
#endif
    if (IS_UNCOLLECTABLE(obj_kind))
      GC_non_gc_bytes += (sz - orig_sz);
    /* Extra area is already cleared by `GC_alloc_large_and_clear`. */
  }
  if (ADD_EXTRA_BYTES(lb) <= sz) {
    if (lb >= (sz >> 1)) {
      if (orig_sz > lb) {
        /* Clear unneeded part of object to avoid bogus pointer tracing. */
        BZERO((ptr_t)cleared_p + lb, orig_sz - lb);
      }
      return p;
    }
    /*
     * Shrink it.  Note: shrinking of large blocks is not implemented
     * efficiently.
     */
    sz = lb;
  }
  result = GC_generic_or_special_malloc((word)lb, obj_kind);
  if (LIKELY(result != NULL)) {
    /*
     * In case of shrink, it could also return original object.
     * But this gives the client warning of imminent disaster.
     */
    BCOPY(p, result, sz);
#ifndef IGNORE_FREE
    GC_free((ptr_t)cleared_p);
#endif
  }
  return result;
#undef cleared_p
}

#if defined(REDIRECT_MALLOC) && !defined(REDIRECT_REALLOC)
#  define REDIRECT_REALLOC GC_realloc
#endif

#ifdef REDIRECT_REALLOC

/* As with `malloc`, avoid two levels of extra calls here. */
#  define GC_debug_realloc_replacement(p, lb) \
    GC_debug_realloc(p, lb, GC_DBG_EXTRAS)

#  if !defined(REDIRECT_MALLOC_IN_HEADER)
void *
realloc(void *p, size_t lb)
{
  return REDIRECT_REALLOC(p, lb);
}
#  endif

#  undef GC_debug_realloc_replacement
#endif /* REDIRECT_REALLOC */

GC_API GC_ATTR_MALLOC void *GC_CALL
GC_generic_malloc_ignore_off_page(size_t lb, int kind)
{
  return GC_generic_malloc_aligned(lb, kind, IGNORE_OFF_PAGE,
                                   0 /* `align_m1` */);
}

GC_API GC_ATTR_MALLOC void *GC_CALL
GC_malloc_ignore_off_page(size_t lb)
{
  return GC_generic_malloc_aligned(lb, NORMAL, IGNORE_OFF_PAGE, 0);
}

GC_API GC_ATTR_MALLOC void *GC_CALL
GC_malloc_atomic_ignore_off_page(size_t lb)
{
  return GC_generic_malloc_aligned(lb, PTRFREE, IGNORE_OFF_PAGE, 0);
}

/*
 * Increment `GC_bytes_allocd` from code that does not have direct access
 * to `GC_arrays`.
 */
void GC_CALL
GC_incr_bytes_allocd(size_t n)
{
  GC_bytes_allocd += n;
}

/* The same as `GC_incr_bytes_allocd` but for `GC_bytes_freed`. */
void GC_CALL
GC_incr_bytes_freed(size_t n)
{
  GC_bytes_freed += n;
}

GC_API size_t GC_CALL
GC_get_expl_freed_bytes_since_gc(void)
{
  return (size_t)GC_bytes_freed;
}

#ifdef PARALLEL_MARK
static void
acquire_mark_lock_notify_builders(void)
{
  GC_acquire_mark_lock();
  --GC_fl_builder_count;
  if (0 == GC_fl_builder_count)
    GC_notify_all_builder();
  GC_release_mark_lock();
}
#endif

GC_API void GC_CALL
GC_generic_malloc_many(size_t lb_adjusted, int kind, void **result)
{
  void *op;
  void *p;
  void **opp;
  /* The value of `lb_adjusted` converted to granules. */
  size_t lg;
  word my_bytes_allocd = 0;
  struct obj_kind *ok;
  struct hblk **rlh;

  GC_ASSERT(lb_adjusted != 0 && (lb_adjusted & (GC_GRANULE_BYTES - 1)) == 0);
  /* Currently a single object is always allocated if manual VDB. */
  /*
   * TODO: `GC_dirty` should be called for each linked object (but the
   * last one) to support multiple objects allocation.
   */
  if (UNLIKELY(lb_adjusted > MAXOBJBYTES) || GC_manual_vdb) {
    op = GC_generic_malloc_aligned(lb_adjusted - EXTRA_BYTES, kind,
                                   0 /* `flags` */, 0 /* `align_m1` */);
    if (LIKELY(op != NULL))
      obj_link(op) = NULL;
    *result = op;
#ifndef NO_MANUAL_VDB
    if (GC_manual_vdb && GC_is_heap_ptr(result)) {
      GC_dirty_inner(result);
      REACHABLE_AFTER_DIRTY(op);
    }
#endif
    return;
  }

  GC_ASSERT(kind < MAXOBJKINDS);
  lg = BYTES_TO_GRANULES(lb_adjusted);
  if (UNLIKELY(get_have_errors()))
    GC_print_all_errors();
  GC_notify_or_invoke_finalizers();
  GC_DBG_COLLECT_AT_MALLOC(lb_adjusted - EXTRA_BYTES);
  if (UNLIKELY(!GC_is_initialized))
    GC_init();

  LOCK();
  /* Do our share of marking work. */
  if (GC_incremental && !GC_dont_gc) {
    GC_collect_a_little_inner(1);
  }

  /* First see if we can reclaim a page of objects waiting to be reclaimed. */
  ok = &GC_obj_kinds[kind];
  rlh = ok->ok_reclaim_list;
  if (rlh != NULL) {
    struct hblk *hbp;
    hdr *hhdr;

    while ((hbp = rlh[lg]) != NULL) {
      hhdr = HDR(hbp);
      rlh[lg] = hhdr->hb_next;
      GC_ASSERT(hhdr->hb_sz == lb_adjusted);
      hhdr->hb_last_reclaimed = (unsigned short)GC_gc_no;
#ifdef PARALLEL_MARK
      if (GC_parallel) {
        GC_signed_word my_bytes_allocd_tmp
            = (GC_signed_word)AO_load(&GC_bytes_allocd_tmp);
        GC_ASSERT(my_bytes_allocd_tmp >= 0);
        /*
         * We only decrement it while holding the allocator lock.
         * Thus, we cannot accidentally adjust it down in more than
         * one thread simultaneously.
         */
        if (my_bytes_allocd_tmp != 0) {
          (void)AO_fetch_and_add(&GC_bytes_allocd_tmp,
                                 (AO_t)(-my_bytes_allocd_tmp));
          GC_bytes_allocd += (word)my_bytes_allocd_tmp;
        }
        GC_acquire_mark_lock();
        ++GC_fl_builder_count;
        UNLOCK();
        GC_release_mark_lock();

        op = GC_reclaim_generic(hbp, hhdr, lb_adjusted, ok->ok_init, NULL,
                                &my_bytes_allocd);
        if (op != NULL) {
          *result = op;
          (void)AO_fetch_and_add(&GC_bytes_allocd_tmp, (AO_t)my_bytes_allocd);
          GC_acquire_mark_lock();
          --GC_fl_builder_count;
          if (0 == GC_fl_builder_count)
            GC_notify_all_builder();
#  ifdef THREAD_SANITIZER
          GC_release_mark_lock();
          LOCK();
          GC_bytes_found += (GC_signed_word)my_bytes_allocd;
          UNLOCK();
#  else
          /* The resulting `GC_bytes_found` may be inaccurate. */
          GC_bytes_found += (GC_signed_word)my_bytes_allocd;
          GC_release_mark_lock();
#  endif
          (void)GC_clear_stack(NULL);
          return;
        }

        acquire_mark_lock_notify_builders();

        /*
         * The allocator lock is needed for access to the reclaim list.
         * We must decrement `GC_fl_builder_count` before reacquiring
         * the allocator lock.  Hopefully this path is rare.
         */
        LOCK();
        rlh = ok->ok_reclaim_list; /*< reload `rlh` after locking */
        if (UNLIKELY(NULL == rlh))
          break;
        continue;
      }
#endif

      op = GC_reclaim_generic(hbp, hhdr, lb_adjusted, ok->ok_init, NULL,
                              &my_bytes_allocd);
      if (op != NULL) {
        /* We also reclaimed memory, so we need to adjust that count. */
        GC_bytes_found += (GC_signed_word)my_bytes_allocd;
        GC_bytes_allocd += my_bytes_allocd;
        *result = op;
        UNLOCK();
        (void)GC_clear_stack(NULL);
        return;
      }
    }
  }

  /*
   * Next try to use prefix of global free list if there is one.
   * We do not refill it, but we need to use it up before allocating
   * a new block ourselves.
   */
  opp = &ok->ok_freelist[lg];
  op = *opp;
  if (op != NULL) {
    *opp = NULL;
    my_bytes_allocd = 0;
    for (p = op; p != NULL; p = obj_link(p)) {
      my_bytes_allocd += lb_adjusted;
      if ((word)my_bytes_allocd >= HBLKSIZE) {
        *opp = obj_link(p);
        obj_link(p) = NULL;
        break;
      }
    }
    GC_bytes_allocd += my_bytes_allocd;

  } else {
    /* Next try to allocate a new block worth of objects of this size. */
    struct hblk *h
        = GC_allochblk(lb_adjusted, kind, 0 /* `flags` */, 0 /* `align_m1` */);

    if (h /* `!= NULL` */) { /*< CPPCHECK */
      if (IS_UNCOLLECTABLE(kind))
        GC_set_hdr_marks(HDR(h));
      GC_bytes_allocd += HBLKSIZE - (HBLKSIZE % lb_adjusted);
#ifdef PARALLEL_MARK
      if (GC_parallel) {
        GC_acquire_mark_lock();
        ++GC_fl_builder_count;
        UNLOCK();
        GC_release_mark_lock();

        op = GC_build_fl(h, NULL, lg, ok->ok_init || GC_debugging_started);
        *result = op;

        acquire_mark_lock_notify_builders();
        (void)GC_clear_stack(NULL);
        return;
      }
#endif

      op = GC_build_fl(h, NULL, lg, ok->ok_init || GC_debugging_started);
    } else {
      /*
       * As a last attempt, try allocating a single object.
       * Note that this may trigger a collection or expand the heap.
       */
      op = GC_generic_malloc_inner(lb_adjusted - EXTRA_BYTES, kind,
                                   0 /* `flags` */);
      if (op != NULL)
        obj_link(op) = NULL;
    }
  }

  *result = op;
  UNLOCK();
  (void)GC_clear_stack(NULL);
}

GC_API GC_ATTR_MALLOC void *GC_CALL
GC_malloc_many(size_t lb)
{
  void *result;
  size_t lg, lb_adjusted;

  if (UNLIKELY(0 == lb))
    lb = 1;
  lg = ALLOC_REQUEST_GRANS(lb);
  lb_adjusted = GRANULES_TO_BYTES(lg);
  GC_generic_malloc_many(lb_adjusted, NORMAL, &result);
  return result;
}

/*
 * TODO: The debugging variant of `GC_memalign` and friends is tricky
 * and currently missing.  The major difficulty is: `store_debug_info`
 * should return the pointer of the object with the requested alignment
 * (unlike the object header).
 */

GC_API GC_ATTR_MALLOC void *GC_CALL
GC_memalign(size_t align, size_t lb)
{
  size_t align_m1 = align - 1;

  /* Check the alignment argument. */
  if (UNLIKELY(0 == align || (align & align_m1) != 0))
    return NULL;

  /* TODO: Use thread-local allocation. */
  if (align <= GC_GRANULE_BYTES)
    return GC_malloc(lb);
  return GC_malloc_kind_aligned_global(lb, NORMAL, align_m1);
}

GC_API int GC_CALL
GC_posix_memalign(void **memptr, size_t align, size_t lb)
{
  void *p;
  size_t align_minus_one = align - 1; /*< to workaround a cppcheck warning */

  /* Check alignment properly. */
  if (UNLIKELY(align < sizeof(void *) || (align_minus_one & align) != 0)) {
#ifdef MSWINCE
    return ERROR_INVALID_PARAMETER;
#else
    return EINVAL;
#endif
  }

  p = GC_memalign(align, lb);
  if (UNLIKELY(NULL == p)) {
#ifdef MSWINCE
    return ERROR_NOT_ENOUGH_MEMORY;
#else
    return ENOMEM;
#endif
  }
  *memptr = p;
  return 0; /*< success */
}

#ifndef GC_NO_VALLOC
GC_API GC_ATTR_MALLOC void *GC_CALL
GC_valloc(size_t lb)
{
  if (UNLIKELY(!GC_is_initialized))
    GC_init();
  GC_ASSERT(GC_real_page_size != 0);
  return GC_memalign(GC_real_page_size, lb);
}

GC_API GC_ATTR_MALLOC void *GC_CALL
GC_pvalloc(size_t lb)
{
  if (UNLIKELY(!GC_is_initialized))
    GC_init();
  GC_ASSERT(GC_real_page_size != 0);
  lb = SIZET_SAT_ADD(lb, GC_real_page_size - 1) & ~(GC_real_page_size - 1);
  return GC_memalign(GC_real_page_size, lb);
}
#endif /* !GC_NO_VALLOC */

GC_API GC_ATTR_MALLOC char *GC_CALL
GC_strdup(const char *s)
{
  /*
   * Implementation of a variant of `strdup()` that uses the collector
   * to allocate a copy of the string.
   */
  char *copy;
  size_t lb;
  if (s == NULL)
    return NULL;
  lb = strlen(s) + 1;
  copy = (char *)GC_malloc_atomic(lb);
  if (UNLIKELY(NULL == copy)) {
#ifndef MSWINCE
    errno = ENOMEM;
#endif
    return NULL;
  }
  BCOPY(s, copy, lb);
  return copy;
}

GC_API GC_ATTR_MALLOC char *GC_CALL
GC_strndup(const char *str, size_t size)
{
  char *copy;
  /* Note: `str` is expected to be non-`NULL`. */
  size_t len = strlen(str);
  if (UNLIKELY(len > size))
    len = size;
  copy = (char *)GC_malloc_atomic(len + 1);
  if (UNLIKELY(NULL == copy)) {
#ifndef MSWINCE
    errno = ENOMEM;
#endif
    return NULL;
  }
  if (LIKELY(len > 0))
    BCOPY(str, copy, len);
  copy[len] = '\0';
  return copy;
}

#ifdef GC_REQUIRE_WCSDUP
#  include <wchar.h> /*< for `wcslen()` */

GC_API GC_ATTR_MALLOC wchar_t *GC_CALL
GC_wcsdup(const wchar_t *str)
{
  size_t lb = (wcslen(str) + 1) * sizeof(wchar_t);
  wchar_t *copy = (wchar_t *)GC_malloc_atomic(lb);

  if (UNLIKELY(NULL == copy)) {
#  ifndef MSWINCE
    errno = ENOMEM;
#  endif
    return NULL;
  }
  BCOPY(str, copy, lb);
  return copy;
}

#  if !defined(wcsdup) && defined(REDIRECT_MALLOC) \
      && !defined(REDIRECT_MALLOC_IN_HEADER)
wchar_t *
wcsdup(const wchar_t *str)
{
  return GC_wcsdup(str);
}
#  endif
#endif /* GC_REQUIRE_WCSDUP */

#ifndef CPPCHECK
GC_API void *GC_CALL
GC_malloc_stubborn(size_t lb)
{
  return GC_malloc(lb);
}

GC_API void GC_CALL
GC_change_stubborn(const void *p)
{
  UNUSED_ARG(p);
}
#endif /* !CPPCHECK */

GC_API void GC_CALL
GC_end_stubborn_change(const void *p)
{
  GC_dirty(p); /*< entire object */
}

GC_API void GC_CALL
GC_ptr_store_and_dirty(void *p, const void *q)
{
  *(const void **)p = q;
  GC_dirty(p);
  REACHABLE_AFTER_DIRTY(q);
}
