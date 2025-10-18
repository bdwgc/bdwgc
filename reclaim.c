/*
 * Copyright 1988, 1989 Hans-J. Boehm, Alan J. Demers
 * Copyright (c) 1991-1996 by Xerox Corporation.  All rights reserved.
 * Copyright (c) 1996-1999 by Silicon Graphics.  All rights reserved.
 * Copyright (c) 1999-2004 Hewlett-Packard Development Company, L.P.
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

#ifdef ENABLE_DISCLAIM
#  include "gc/gc_disclaim.h"
#endif

#ifdef PARALLEL_MARK
GC_INNER GC_signed_word GC_fl_builder_count = 0;
#endif

/*
 * We defer printing of leaked objects until we are done with the
 * collection cycle, since the routine for printing objects needs
 * to run outside the collector, e.g. without the allocator lock.
 */

#ifndef NO_FIND_LEAK
#  ifndef MAX_LEAKED
#    define MAX_LEAKED 40
#  endif
STATIC ptr_t GC_leaked[MAX_LEAKED] = { NULL };
#endif

#if !defined(EAGER_SWEEP) && defined(ENABLE_DISCLAIM)
STATIC void GC_reclaim_unconditionally_marked(void);
#endif

#ifndef SHORT_DBG_HDRS

#  include "private/dbg_mlc.h"

#  ifndef MAX_SMASHED
#    define MAX_SMASHED 20
#  endif

/*
 * List of smashed (clobbered) locations.  We defer printing these,
 * since we cannot always print them nicely with the allocator lock held.
 * We put them here instead of in `GC_arrays`, since it may be useful to
 * be able to look at them with the debugger.
 */
STATIC ptr_t GC_smashed[MAX_SMASHED] = { NULL };

GC_INNER void
GC_add_smashed(ptr_t smashed)
{
  GC_ASSERT(I_HOLD_LOCK());
  GC_ASSERT(GC_is_marked(GC_base(smashed)));
  /* FIXME: Prevent adding an object while printing smashed list. */
  GC_smashed[GC_n_smashed] = smashed;
  /*
   * In case of overflow, we keep the first `MAX_SMASHED - 1` entries
   * plus the last one.
   */
  if (GC_n_smashed < MAX_SMASHED - 1)
    ++GC_n_smashed;
  GC_SET_HAVE_ERRORS();
}

GC_INNER void
GC_print_smashed_obj(const char *msg, void *p, ptr_t clobbered)
{
  oh *ohdr = (oh *)GC_base(p);

  GC_ASSERT(I_DONT_HOLD_LOCK());
#  ifdef LINT2
  if (!ohdr)
    ABORT("Invalid GC_print_smashed_obj argument");
#  endif
  if (ADDR_GE((ptr_t)(&ohdr->oh_sz), clobbered) || NULL == ohdr->oh_string) {
    GC_err_printf("%s %p in or near object at %p(<smashed>, appr. sz= %lu)\n",
                  msg, (void *)clobbered, p,
                  (unsigned long)(GC_size(ohdr) - DEBUG_BYTES));
  } else {
    GC_err_printf("%s %p in or near object at %p (%s:%d, sz= %lu)\n", msg,
                  (void *)clobbered, p,
                  ADDR(ohdr->oh_string) < HBLKSIZE ? "(smashed string)"
                  : ohdr->oh_string[0] == '\0'     ? "EMPTY(smashed?)"
                                                   : ohdr->oh_string,
                  GET_OH_LINENUM(ohdr), (unsigned long)ohdr->oh_sz);
    PRINT_CALL_CHAIN(ohdr);
  }
}

GC_INNER void
GC_print_all_smashed_proc(void)
{
  unsigned i;

  GC_ASSERT(I_DONT_HOLD_LOCK());
  if (GC_n_smashed == 0)
    return;
  GC_err_printf("GC_check_heap_block: found %u smashed heap objects:\n",
                GC_n_smashed);
  for (i = 0; i < GC_n_smashed; ++i) {
    ptr_t base = (ptr_t)GC_base(GC_smashed[i]);

#  ifdef LINT2
    if (!base)
      ABORT("Invalid GC_smashed element");
#  endif
    GC_print_smashed_obj("", base + sizeof(oh), GC_smashed[i]);
    GC_smashed[i] = 0;
  }
  GC_n_smashed = 0;
}

GC_INNER int
GC_has_other_debug_info(ptr_t base)
{
  ptr_t body = (ptr_t)((oh *)base + 1);
  size_t sz = GC_size(base);

  if (HBLKPTR(base) != HBLKPTR(body) || sz < DEBUG_BYTES + EXTRA_BYTES) {
    return 0;
  }
  if (((oh *)base)->oh_sf != (START_FLAG ^ (GC_uintptr_t)body)
      && ((GC_uintptr_t *)base)[BYTES_TO_PTRS(sz) - 1]
             != (END_FLAG ^ (GC_uintptr_t)body)) {
    return 0;
  }
  if (((oh *)base)->oh_sz == (GC_uintptr_t)sz) {
    /* Object may have had debug info, but has been deallocated. */
    return -1;
  }
  return 1;
}
#endif /* !SHORT_DBG_HDRS */

GC_INNER void
GC_default_print_heap_obj_proc(ptr_t p)
{
  ptr_t base = (ptr_t)GC_base(p);
  int kind = HDR(base)->hb_obj_kind;

  GC_err_printf("object at %p of appr. %lu bytes (%s)\n", (void *)base,
                (unsigned long)GC_size(base),
                kind == PTRFREE          ? "atomic"
                : IS_UNCOLLECTABLE(kind) ? "uncollectable"
                                         : "composite");
}

GC_INNER void (*GC_print_heap_obj)(ptr_t p) = GC_default_print_heap_obj_proc;

#if !defined(NO_FIND_LEAK) || !defined(SHORT_DBG_HDRS)
GC_INNER void
GC_print_all_errors(void)
{
  static GC_bool printing_errors = FALSE;
  GC_bool have_errors;
#  ifndef NO_FIND_LEAK
  unsigned i, n_leaked;
  ptr_t leaked[MAX_LEAKED];
#  endif

  LOCK();
  if (printing_errors) {
    UNLOCK();
    return;
  }
  have_errors = get_have_errors();
  printing_errors = TRUE;
#  ifndef NO_FIND_LEAK
  n_leaked = GC_n_leaked;
  if (n_leaked > 0) {
    GC_ASSERT(n_leaked <= MAX_LEAKED);
    BCOPY(GC_leaked, leaked, n_leaked * sizeof(ptr_t));
    GC_n_leaked = 0;
    BZERO(GC_leaked, n_leaked * sizeof(ptr_t));
  }
#  endif
  UNLOCK();

  if (GC_debugging_started) {
    GC_print_all_smashed();
  } else {
    have_errors = FALSE;
  }

#  ifndef NO_FIND_LEAK
  if (n_leaked > 0) {
    GC_err_printf("Found %u leaked objects:\n", n_leaked);
    have_errors = TRUE;
  }
  for (i = 0; i < n_leaked; i++) {
    ptr_t p = leaked[i];

#    ifndef SKIP_LEAKED_OBJECTS_PRINTING
    GC_print_heap_obj(p);
#    endif
    GC_free(p);
  }
#  endif

  if (have_errors
#  ifndef GC_ABORT_ON_LEAK
      && GETENV("GC_ABORT_ON_LEAK") != NULL
#  endif
  ) {
    ABORT("Leaked or smashed objects encountered");
  }

  LOCK();
  printing_errors = FALSE;
  UNLOCK();
}
#endif

/* The reclaim phase. */

GC_INNER GC_bool
GC_block_empty(const hdr *hhdr)
{
  return 0 == hhdr->hb_n_marks;
}

STATIC GC_bool
GC_block_nearly_full(const hdr *hhdr, size_t sz)
{
  return hhdr->hb_n_marks > HBLK_OBJS(sz) * 7 / 8;
}

/*
 * TODO: This should perhaps again be specialized for `USE_MARK_BYTES`
 * and `USE_MARK_BITS` cases.
 */

GC_INLINE ptr_t
GC_clear_block(ptr_t q, size_t sz, word *pcount)
{
  ptr_t *p = (ptr_t *)q;
  ptr_t plim = q + sz;

  /* Clear object, advance `p` to next object in the process. */
#ifdef USE_MARK_BYTES
  GC_ASSERT((sz & 1) == 0);
  GC_ASSERT((ADDR(p) & (2 * sizeof(ptr_t) - 1)) == 0);
  p[1] = NULL; /*< but do not clear link field */
  for (p += 2; ADDR_LT((ptr_t)p, plim); p += 2) {
    CLEAR_DOUBLE(p);
  }
#else
  /* Skip link field. */
  p++;

  while (ADDR_LT((ptr_t)p, plim)) {
    *p++ = NULL;
  }
#endif
  *pcount += sz;
  return (ptr_t)p;
}

/*
 * Restore unmarked small objects in `h` of size `sz` (in bytes) to the
 * object free list.  Returns the new list.  Clears unmarked objects.
 */
STATIC ptr_t
GC_reclaim_clear(struct hblk *hbp, const hdr *hhdr, size_t sz, ptr_t list,
                 word *pcount)
{
  size_t bit_no;
  ptr_t p, plim;

  GC_ASSERT(hhdr == GC_find_header(hbp));
#ifndef THREADS
  GC_ASSERT(sz == hhdr->hb_sz);
#else
  /* Skip the assertion because of a potential race with `GC_realloc`. */
#endif
  GC_ASSERT((sz & (sizeof(ptr_t) - 1)) == 0);

  /* Go through all objects in the block. */
  p = hbp->hb_body;
  plim = p + HBLKSIZE - sz;
  for (bit_no = 0; ADDR_GE(plim, p); bit_no += MARK_BIT_OFFSET(sz)) {
    if (mark_bit_from_hdr(hhdr, bit_no)) {
      p += sz;
    } else {
      /* The object is available - put it on list. */
      obj_link(p) = list;
      list = p;
      FREE_PROFILER_HOOK(p);
      p = GC_clear_block(p, sz, pcount);
    }
  }
  return list;
}

/* The same thing as `GC_reclaim_clear`, but do not clear objects. */
STATIC ptr_t
GC_reclaim_uninit(struct hblk *hbp, const hdr *hhdr, size_t sz, ptr_t list,
                  word *pcount)
{
  size_t bit_no;
  word n_bytes_found = 0;
  ptr_t p, plim;

#ifndef THREADS
  GC_ASSERT(sz == hhdr->hb_sz);
#endif

  /* Go through all objects in the block. */
  p = hbp->hb_body;
  plim = (ptr_t)hbp + HBLKSIZE - sz;
  for (bit_no = 0; ADDR_GE(plim, p); bit_no += MARK_BIT_OFFSET(sz), p += sz) {
    if (!mark_bit_from_hdr(hhdr, bit_no)) {
      n_bytes_found += sz;
      /* The object is available - put it on list. */
      obj_link(p) = list;
      list = p;
      FREE_PROFILER_HOOK(p);
    }
  }
  *pcount += n_bytes_found;
  return list;
}

#ifdef ENABLE_DISCLAIM
/*
 * Call reclaim notifier for block's kind on each unmarked object in block,
 * all within a pair of corresponding enter/leave callbacks.
 */
STATIC ptr_t
GC_disclaim_and_reclaim(struct hblk *hbp, hdr *hhdr, size_t sz, ptr_t list,
                        word *pcount)
{
  size_t bit_no;
  ptr_t p, plim;
  int(GC_CALLBACK * disclaim)(void *)
      = GC_obj_kinds[hhdr->hb_obj_kind].ok_disclaim_proc;

  GC_ASSERT(disclaim != 0);
#  ifndef THREADS
  GC_ASSERT(sz == hhdr->hb_sz);
#  endif
  p = hbp->hb_body;
  plim = p + HBLKSIZE - sz;

  for (bit_no = 0; ADDR_GE(plim, p); bit_no += MARK_BIT_OFFSET(sz)) {
    if (mark_bit_from_hdr(hhdr, bit_no)) {
      p += sz;
    } else if (disclaim(p)) {
      set_mark_bit_from_hdr(hhdr, bit_no);
      INCR_MARKS(hhdr);
      p += sz;
    } else {
      obj_link(p) = list;
      list = p;
      FREE_PROFILER_HOOK(p);
      p = GC_clear_block(p, sz, pcount);
    }
  }
  return list;
}
#endif /* ENABLE_DISCLAIM */

#ifndef NO_FIND_LEAK

#  ifndef SHORT_DBG_HDRS
STATIC GC_bool
GC_check_leaked(ptr_t base)
{
  size_t i;
  size_t lpw;
  ptr_t *p;

  if (
#    if defined(KEEP_BACK_PTRS) || defined(MAKE_BACK_GRAPH)
      (*(GC_uintptr_t *)base & 1) != 0 &&
#    endif
      GC_has_other_debug_info(base) >= 0)
    return TRUE; /*< object has leaked */

  /* Validate freed object's content. */
  p = (ptr_t *)(base + sizeof(oh));
  lpw = BYTES_TO_PTRS(HDR(base)->hb_sz - sizeof(oh));
  for (i = 0; i < lpw; ++i)
    if ((GC_uintptr_t)p[i] != GC_FREED_MEM_MARKER) {
      /* Do not reclaim it in this cycle. */
      GC_set_mark_bit(base);
      /* Alter-after-free has been detected. */
      GC_add_smashed((ptr_t)(&p[i]));
      /* Do not report any other smashed locations in the object. */
      break;
    }

  return FALSE; /*< `GC_debug_free()` has been called */
}
#  endif /* !SHORT_DBG_HDRS */

GC_INLINE void
GC_add_leaked(ptr_t leaked)
{
  GC_ASSERT(I_HOLD_LOCK());
#  ifndef SHORT_DBG_HDRS
  if (GC_findleak_delay_free && !GC_check_leaked(leaked))
    return;
#  endif

  GC_SET_HAVE_ERRORS();
  if (GC_n_leaked < MAX_LEAKED) {
    GC_leaked[GC_n_leaked++] = leaked;
    /* Make sure it is not reclaimed this cycle. */
    GC_set_mark_bit(leaked);
  }
}

/* Do not really reclaim objects, just check for unmarked ones. */
STATIC void
GC_reclaim_check(struct hblk *hbp, const hdr *hhdr, size_t sz)
{
  size_t bit_no;
  ptr_t p, plim;

#  ifndef THREADS
  GC_ASSERT(sz == hhdr->hb_sz);
#  endif
  /* Go through all objects in the block. */
  p = hbp->hb_body;
  plim = p + HBLKSIZE - sz;
  for (bit_no = 0; ADDR_GE(plim, p); bit_no += MARK_BIT_OFFSET(sz), p += sz) {
    if (!mark_bit_from_hdr(hhdr, bit_no))
      GC_add_leaked(p);
  }
}

#endif /* !NO_FIND_LEAK */

/*
 * Is a pointer-free block?  Same as `IS_PTRFREE()` macro but uses
 * unordered atomic access to avoid racing with `GC_realloc`.
 */
#ifdef AO_HAVE_load
#  define IS_PTRFREE_SAFE(hhdr) (AO_load((AO_t *)&(hhdr)->hb_descr) == 0)
#else
/*
 * No race as `GC_realloc` holds the allocator lock when updating
 * `hb_descr` field.
 */
#  define IS_PTRFREE_SAFE(hhdr) IS_PTRFREE(hhdr)
#endif

GC_INNER ptr_t
GC_reclaim_generic(struct hblk *hbp, hdr *hhdr, size_t sz, GC_bool init,
                   ptr_t list, word *pcount)
{
  ptr_t result;

#ifndef PARALLEL_MARK
  GC_ASSERT(I_HOLD_LOCK());
#endif
  GC_ASSERT(GC_find_header(hbp) == hhdr);
#ifndef GC_DISABLE_INCREMENTAL
  GC_remove_protection(hbp, 1, IS_PTRFREE_SAFE(hhdr));
#endif
#ifdef ENABLE_DISCLAIM
  if ((hhdr->hb_flags & HAS_DISCLAIM) != 0) {
    result = GC_disclaim_and_reclaim(hbp, hhdr, sz, list, pcount);
  } else
#endif
  /* else */ {
    if (init || GC_debugging_started) {
      result = GC_reclaim_clear(hbp, hhdr, sz, list, pcount);
    } else {
#ifndef AO_HAVE_load
      GC_ASSERT(IS_PTRFREE(hhdr));
#endif
      result = GC_reclaim_uninit(hbp, hhdr, sz, list, pcount);
    }
  }
  if (IS_UNCOLLECTABLE(hhdr->hb_obj_kind))
    GC_set_hdr_marks(hhdr);
  return result;
}

/*
 * Restore unmarked small objects in the block pointed to by `hbp` to
 * the appropriate object free list.  If entirely empty blocks are to
 * be completely deallocated, then caller should perform that check.
 */
STATIC void
GC_reclaim_small_nonempty_block(struct hblk *hbp, size_t sz,
                                GC_bool report_if_found)
{
  hdr *hhdr;

  GC_ASSERT(I_HOLD_LOCK());
  hhdr = HDR(hbp);
  hhdr->hb_last_reclaimed = (unsigned short)GC_gc_no;
  if (report_if_found) {
#ifndef NO_FIND_LEAK
    GC_reclaim_check(hbp, hhdr, sz);
#endif
  } else {
    struct obj_kind *ok = &GC_obj_kinds[hhdr->hb_obj_kind];
    void **flh = &ok->ok_freelist[BYTES_TO_GRANULES(sz)];

    *flh = GC_reclaim_generic(hbp, hhdr, sz, ok->ok_init, (ptr_t)(*flh),
                              (/* unsigned */ word *)&GC_bytes_found);
  }
}

#ifdef ENABLE_DISCLAIM
STATIC void
GC_disclaim_and_reclaim_or_free_small_block(struct hblk *hbp)
{
  hdr *hhdr;
  size_t sz;
  struct obj_kind *ok;
  void **flh;
  void *flh_next;

  GC_ASSERT(I_HOLD_LOCK());
  hhdr = HDR(hbp);
  sz = hhdr->hb_sz;
  ok = &GC_obj_kinds[hhdr->hb_obj_kind];
  flh = &ok->ok_freelist[BYTES_TO_GRANULES(sz)];

  hhdr->hb_last_reclaimed = (unsigned short)GC_gc_no;
  flh_next = GC_reclaim_generic(hbp, hhdr, sz, ok->ok_init, (ptr_t)(*flh),
                                (/* unsigned */ word *)&GC_bytes_found);
  if (hhdr->hb_n_marks) {
    *flh = flh_next;
  } else {
    GC_ASSERT(hbp == hhdr->hb_block);
    GC_bytes_found += (GC_signed_word)HBLKSIZE;
    GC_freehblk(hbp);
  }
}
#endif /* ENABLE_DISCLAIM */

/*
 * Restore an unmarked large object or an entirely empty block of
 * small objects to the heap block free list.  Otherwise enqueue the
 * block for later processing by `GC_reclaim_small_nonempty_block()`.
 * If `report_if_found` is `TRUE`, then process any block immediately,
 * and simply report free objects; do not actually reclaim them.
 */
STATIC void GC_CALLBACK
GC_reclaim_block(struct hblk *hbp, void *report_if_found)
{
  hdr *hhdr;
  size_t sz; /*< size of objects in current block */
  struct obj_kind *ok;

  GC_ASSERT(I_HOLD_LOCK());
#if defined(CPPCHECK)
  GC_noop1_ptr(report_if_found);
#endif
  hhdr = HDR(hbp);
  ok = &GC_obj_kinds[hhdr->hb_obj_kind];
#ifdef AO_HAVE_load
  /* Atomic access is used to avoid racing with `GC_realloc`. */
  sz = AO_load((volatile AO_t *)&hhdr->hb_sz);
#else
  /*
   * No race as `GC_realloc` holds the allocator lock while
   * updating `hb_sz`.
   */
  sz = hhdr->hb_sz;
#endif
  if (sz > MAXOBJBYTES) {
    /* The case of 1 big object. */
    if (!mark_bit_from_hdr(hhdr, 0)) {
      if (report_if_found) {
        GC_ASSERT(hbp == hhdr->hb_block);
#ifndef NO_FIND_LEAK
        GC_add_leaked((ptr_t)hbp);
#endif
      } else {
#ifdef ENABLE_DISCLAIM
        if (UNLIKELY((hhdr->hb_flags & HAS_DISCLAIM) != 0)) {
          if (ok->ok_disclaim_proc(hbp)) {
            /* Not disclaimed, thus resurrect the object. */
            set_mark_bit_from_hdr(hhdr, 0);
            goto in_use;
          }
        }
#endif
        GC_ASSERT(hbp == hhdr->hb_block);
        if (sz > HBLKSIZE) {
          GC_large_allocd_bytes -= HBLKSIZE * OBJ_SZ_TO_BLOCKS(sz);
        }
        GC_bytes_found += (GC_signed_word)sz;
        GC_freehblk(hbp);
        FREE_PROFILER_HOOK(hbp);
      }
    } else {
#ifdef ENABLE_DISCLAIM
    in_use:
#endif
      if (IS_PTRFREE_SAFE(hhdr)) {
        GC_atomic_in_use += sz;
      } else {
        GC_composite_in_use += sz;
      }
    }
  } else {
    GC_bool empty = GC_block_empty(hhdr);

#ifdef PARALLEL_MARK
    /*
     * Count can be low or one too high because we sometimes have to
     * ignore decrements.  Objects can also potentially be repeatedly
     * marked by each marker.  Here we assume 3 markers at most, but
     * this is extremely unlikely to fail spuriously with more.
     * And if it does, it should be looked at.
     */
    GC_ASSERT(sz != 0
              && (GC_markers_m1 > 1 ? 3 : GC_markers_m1 + 1)
                             * (HBLKSIZE / sz + 1)
                         + 16
                     >= hhdr->hb_n_marks);
#else
    GC_ASSERT(sz * hhdr->hb_n_marks <= HBLKSIZE);
#endif
#ifdef VALGRIND_TRACKING
    /*
     * Call `GC_free_profiler_hook()` on freed objects so that
     * a profiling tool could track the allocations.
     */
    {
      ptr_t p = hbp->hb_body;
      ptr_t plim = p + HBLKSIZE - sz;
      size_t bit_no;

      for (bit_no = 0; ADDR_GE(plim, p);
           bit_no += MARK_BIT_OFFSET(sz), p += sz) {
        if (!mark_bit_from_hdr(hhdr, bit_no))
          FREE_PROFILER_HOOK(p);
      }
    }
#endif
    GC_ASSERT(hbp == hhdr->hb_block);
    if (report_if_found) {
      GC_reclaim_small_nonempty_block(hbp, sz, TRUE /* `report_if_found` */);
    } else if (empty) {
#ifdef ENABLE_DISCLAIM
      if ((hhdr->hb_flags & HAS_DISCLAIM) != 0) {
        GC_disclaim_and_reclaim_or_free_small_block(hbp);
      } else
#endif
      /* else */ {
        GC_bytes_found += (GC_signed_word)HBLKSIZE;
        GC_freehblk(hbp);
        FREE_PROFILER_HOOK(hbp);
      }
    } else if (GC_find_leak_inner || !GC_block_nearly_full(hhdr, sz)) {
      /* Group of smaller objects, enqueue the real work. */
      struct hblk **rlh = ok->ok_reclaim_list;

      if (rlh != NULL) {
        rlh += BYTES_TO_GRANULES(sz);
        hhdr->hb_next = *rlh;
        *rlh = hbp;
      }
    } else {
      /* Not worth salvaging. */
    }
    /*
     * We used to do the `GC_block_nearly_full` check later, but we
     * already have the right cache context here.  Also doing it here
     * avoids some silly lock contention in `GC_malloc_many()`.
     */
    if (IS_PTRFREE_SAFE(hhdr)) {
      GC_atomic_in_use += (word)sz * hhdr->hb_n_marks;
    } else {
      GC_composite_in_use += (word)sz * hhdr->hb_n_marks;
    }
  }
}

#if !defined(NO_DEBUGGING)
/*
 * Routines to gather and print heap block info intended for debugging.
 * Otherwise should be called with the allocator lock held.
 */

struct Print_stats {
  size_t number_of_blocks;
  size_t total_bytes;
};

EXTERN_C_BEGIN /*< to avoid "no previous prototype" clang warning */
    unsigned
    GC_n_set_marks(const hdr *);
EXTERN_C_END

#  ifdef USE_MARK_BYTES
/*
 * Return the number of set mark bits in the given header.
 * Remains externally visible as used by GNU `gcj` currently.
 * There could be a race between `GC_clear_hdr_marks` and this
 * function but the latter is for a debug purpose.
 */
GC_ATTR_NO_SANITIZE_THREAD
unsigned
GC_n_set_marks(const hdr *hhdr)
{
  unsigned result = 0;
  size_t i;
  size_t offset = MARK_BIT_OFFSET(hhdr->hb_sz);
  size_t limit = FINAL_MARK_BIT(hhdr->hb_sz);

  for (i = 0; i < limit; i += offset) {
    result += (unsigned)hhdr->hb_marks[i];
  }

  /* The one should be set past the end. */
  GC_ASSERT(hhdr->hb_marks[limit]);
  return result;
}

#  else
/* Number of set bits in a word.  Not performance critical. */
static unsigned
count_ones(word v)
{
  unsigned result = 0;

  for (; v > 0; v >>= 1) {
    if (v & 1)
      result++;
  }
  return result;
}

unsigned
GC_n_set_marks(const hdr *hhdr)
{
  unsigned result = 0;
  size_t i;
#    ifdef MARK_BIT_PER_OBJ
  size_t n_objs = HBLK_OBJS(hhdr->hb_sz);
  size_t n_mark_words = divWORDSZ(n_objs > 0 ? n_objs : 1); /*< round down */

  for (i = 0; i <= n_mark_words; i++) {
    result += count_ones(hhdr->hb_marks[i]);
  }
#    else

  for (i = 0; i < HB_MARKS_SZ; i++) {
    result += count_ones(hhdr->hb_marks[i]);
  }
#    endif
  GC_ASSERT(result > 0);
  /* Exclude the one bit set past the end. */
  result--;

#    ifndef MARK_BIT_PER_OBJ
  if (IS_UNCOLLECTABLE(hhdr->hb_obj_kind)) {
    size_t lg = BYTES_TO_GRANULES(hhdr->hb_sz);

    /*
     * As mentioned in `GC_set_hdr_marks`, all the bits are set instead of
     * every `n`-th, thus the result should be adjusted.
     */
    GC_ASSERT((unsigned)lg != 0 && result % lg == 0);
    result /= (unsigned)lg;
  }
#    endif
  return result;
}
#  endif /* !USE_MARK_BYTES */

GC_API unsigned GC_CALL
GC_count_set_marks_in_hblk(const void *p)
{
  return GC_n_set_marks(HDR(p));
}

STATIC void GC_CALLBACK
GC_print_block_descr(struct hblk *h, void *raw_ps)
{
  const hdr *hhdr = HDR(h);
  size_t sz = hhdr->hb_sz;
  struct Print_stats *ps = (struct Print_stats *)raw_ps;
  size_t n_marks = (size_t)GC_n_set_marks(hhdr);
  size_t n_objs = HBLK_OBJS(sz);

#  ifndef PARALLEL_MARK
  GC_ASSERT(hhdr->hb_n_marks == n_marks);
#  endif
#  if defined(CPPCHECK)
  GC_noop1_ptr(h);
#  endif
  GC_ASSERT((n_objs > 0 ? n_objs : 1) >= n_marks);
  GC_printf("%u,%u,%u,%u\n", hhdr->hb_obj_kind, (unsigned)sz,
            (unsigned)n_marks, (unsigned)n_objs);
  ps->number_of_blocks++;
  ps->total_bytes += (sz + HBLKSIZE - 1) & ~(HBLKSIZE - 1); /*< round up */
}

void
GC_print_block_list(void)
{
  struct Print_stats pstats;

  GC_printf("kind(0=ptrfree/1=normal/2=unc.),"
            "obj_sz,#marks_set,#objs_in_block\n");
  BZERO(&pstats, sizeof(pstats));
  GC_apply_to_all_blocks(GC_print_block_descr, &pstats);
  GC_printf("blocks= %lu, total_bytes= %lu\n",
            (unsigned long)pstats.number_of_blocks,
            (unsigned long)pstats.total_bytes);
  if (pstats.total_bytes + GC_large_free_bytes != GC_heapsize)
    GC_err_printf("LOST SOME BLOCKS!! Total bytes should be: %lu\n",
                  (unsigned long)(GC_heapsize - GC_large_free_bytes));
}

GC_API void GC_CALL
GC_print_free_list(int kind, size_t lg)
{
  void *flh_next;
  int n;

  GC_ASSERT(kind < MAXOBJKINDS);
  GC_ASSERT(lg <= MAXOBJGRANULES);
  flh_next = GC_obj_kinds[kind].ok_freelist[lg];
  for (n = 0; flh_next != NULL; n++) {
    GC_printf("Free object in heap block %p [%d]: %p\n",
              (void *)HBLKPTR(flh_next), n, flh_next);
    flh_next = obj_link(flh_next);
  }
}
#endif /* !NO_DEBUGGING */

/*
 * Clear all `obj_link` pointers in the list of free objects `*flp`.
 * Clear `*flp`.  This must be done before dropping a list of free
 * `gcj`-style objects, since may otherwise end up with dangling
 * "descriptor" pointers.  It may help for other pointer-containing
 * objects.
 */
STATIC void
GC_clear_fl_links(void **flp)
{
  void *next;

  for (next = *flp; next != NULL; next = *flp) {
    *flp = NULL;
    flp = &obj_link(next);
  }
}

GC_INNER void
GC_start_reclaim(GC_bool report_if_found)
{
  int kind;

  GC_ASSERT(I_HOLD_LOCK());
#if defined(PARALLEL_MARK)
  GC_ASSERT(0 == GC_fl_builder_count);
#endif
  /* Reset in-use counters.  `GC_reclaim_block` recomputes them. */
  GC_composite_in_use = 0;
  GC_atomic_in_use = 0;

  /* Clear reclaim- and free-lists. */
  for (kind = 0; kind < (int)GC_n_kinds; kind++) {
    struct hblk **rlist = GC_obj_kinds[kind].ok_reclaim_list;
    GC_bool should_clobber = GC_obj_kinds[kind].ok_descriptor != 0;

    if (NULL == rlist) {
      /* Means this object kind is not used. */
      continue;
    }

    if (!report_if_found) {
      void **fop;
      void **lim = &GC_obj_kinds[kind].ok_freelist[MAXOBJGRANULES + 1];

      for (fop = GC_obj_kinds[kind].ok_freelist;
           ADDR_LT((ptr_t)fop, (ptr_t)lim); fop++) {
        if (*fop != NULL) {
          if (should_clobber) {
            GC_clear_fl_links(fop);
          } else {
            *fop = NULL;
          }
        }
      }
    } else {
      /* Free-list objects are marked, and it is safe to leave them. */
    }
    BZERO(rlist, (MAXOBJGRANULES + 1) * sizeof(void *));
  }

  /*
   * Go through all heap blocks, and reclaim unmarked objects or enqueue
   * the block for later processing.
   */
  GC_apply_to_all_blocks(GC_reclaim_block, NUMERIC_TO_VPTR(report_if_found));

#ifdef EAGER_SWEEP
  /*
   * This is a very stupid thing to do.  We make it possible anyway.
   */
  GC_reclaim_all((GC_stop_func)0, FALSE);
#elif defined(ENABLE_DISCLAIM)
  /*
   * However, make sure to clear reclaimable objects of kinds with
   * unconditional marking enabled before we do any significant
   * marking work.
   */
  GC_reclaim_unconditionally_marked();
#endif
#if defined(PARALLEL_MARK)
  GC_ASSERT(0 == GC_fl_builder_count);
#endif
}

GC_INNER void
GC_continue_reclaim(size_t lg, int kind)
{
  struct hblk *hbp;
  struct obj_kind *ok = &GC_obj_kinds[kind];
  struct hblk **rlh = ok->ok_reclaim_list;
  void **flh;

  GC_ASSERT(I_HOLD_LOCK());
  if (NULL == rlh) {
    /* No blocks of this kind. */
    return;
  }

  flh = &ok->ok_freelist[lg];
  for (rlh += lg; (hbp = *rlh) != NULL;) {
    const hdr *hhdr = HDR(hbp);

    *rlh = hhdr->hb_next;
    GC_reclaim_small_nonempty_block(hbp, hhdr->hb_sz, FALSE);
    if (*flh != NULL) {
      /* The appropriate free list is nonempty. */
      break;
    }
  }
}

GC_INNER GC_bool
GC_reclaim_all(GC_stop_func stop_func, GC_bool ignore_old)
{
  size_t lg;
  int kind;
  const hdr *hhdr;
  struct hblk *hbp;
  struct hblk **rlp;
  struct hblk **rlh;
#ifndef NO_CLOCK
  CLOCK_TYPE start_time = CLOCK_TYPE_INITIALIZER;

  if (GC_print_stats == VERBOSE)
    GET_TIME(start_time);
#endif
  GC_ASSERT(I_HOLD_LOCK());

  for (kind = 0; kind < (int)GC_n_kinds; kind++) {
    rlp = GC_obj_kinds[kind].ok_reclaim_list;
    if (NULL == rlp)
      continue;

    for (lg = 1; lg <= MAXOBJGRANULES; lg++) {
      for (rlh = rlp + lg; (hbp = *rlh) != NULL;) {
        if (stop_func != (GC_stop_func)0 && (*stop_func)()) {
          return FALSE;
        }
        hhdr = HDR(hbp);
        *rlh = hhdr->hb_next;
        if (!ignore_old || (word)hhdr->hb_last_reclaimed == GC_gc_no - 1) {
          /*
           * It is likely we will need it this time, too.  It has been
           * touched recently, so this should not trigger paging.
           */
          GC_reclaim_small_nonempty_block(hbp, hhdr->hb_sz, FALSE);
        }
      }
    }
  }
#ifndef NO_CLOCK
  if (GC_print_stats == VERBOSE) {
    CLOCK_TYPE done_time;

    GET_TIME(done_time);
    GC_verbose_log_printf("Disposing of reclaim lists took %lu ms %lu ns\n",
                          MS_TIME_DIFF(done_time, start_time),
                          NS_FRAC_TIME_DIFF(done_time, start_time));
  }
#endif
  return TRUE;
}

#if !defined(EAGER_SWEEP) && defined(ENABLE_DISCLAIM)
/*
 * We do an eager sweep on heap blocks where unconditional marking has
 * been enabled, so that any reclaimable objects have been reclaimed
 * before we start marking.  This is a simplified `GC_reclaim_all`
 * restricted to kinds where `ok_mark_unconditionally` is `TRUE`.
 */
STATIC void
GC_reclaim_unconditionally_marked(void)
{
  int kind;

  GC_ASSERT(I_HOLD_LOCK());
  for (kind = 0; kind < (int)GC_n_kinds; kind++) {
    size_t lg;
    struct obj_kind *ok = &GC_obj_kinds[kind];
    struct hblk **rlp = ok->ok_reclaim_list;

    if (NULL == rlp || !ok->ok_mark_unconditionally)
      continue;

    for (lg = 1; lg <= MAXOBJGRANULES; lg++) {
      struct hblk **rlh = rlp + lg;
      struct hblk *hbp;

      while ((hbp = *rlh) != NULL) {
        const hdr *hhdr = HDR(hbp);

        *rlh = hhdr->hb_next;
        GC_reclaim_small_nonempty_block(hbp, hhdr->hb_sz, FALSE);
      }
    }
  }
}
#endif /* !EAGER_SWEEP && ENABLE_DISCLAIM */

struct enumerate_reachable_s {
  GC_reachable_object_proc proc;
  void *client_data;
};

STATIC void GC_CALLBACK
GC_do_enumerate_reachable_objects(struct hblk *hbp, void *ed_ptr)
{
  const hdr *hhdr = HDR(hbp);
  ptr_t p, plim;
  const struct enumerate_reachable_s *ped
      = (struct enumerate_reachable_s *)ed_ptr;
  size_t sz = hhdr->hb_sz;
  size_t bit_no;

  if (GC_block_empty(hhdr))
    return;

  p = hbp->hb_body;
  if (sz > MAXOBJBYTES) {
    /* The case of 1 big object. */
    plim = p;
  } else {
    plim = p + HBLKSIZE - sz;
  }
  /* Go through all objects in the block. */
  for (bit_no = 0; ADDR_GE(plim, p); bit_no += MARK_BIT_OFFSET(sz), p += sz) {
    if (mark_bit_from_hdr(hhdr, bit_no)) {
      ped->proc(p, sz, ped->client_data);
    }
  }
}

GC_API void GC_CALL
GC_enumerate_reachable_objects_inner(GC_reachable_object_proc proc,
                                     void *client_data)
{
  struct enumerate_reachable_s ed;

  GC_ASSERT(I_HOLD_READER_LOCK());
  ed.proc = proc;
  ed.client_data = client_data;
  GC_apply_to_all_blocks(GC_do_enumerate_reachable_objects, &ed);
}
