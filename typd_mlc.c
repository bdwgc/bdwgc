/*
 * Copyright (c) 1991-1994 by Xerox Corporation.  All rights reserved.
 * Copyright (c) 1999-2000 by Hewlett-Packard Company.  All rights reserved.
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

#include "private/gc_pmark.h"

/*
 * Some simple primitives for allocation with explicit type information.
 * Simple objects are allocated such that they contain a `GC_descr` at the
 * end (in the last allocated word).  This descriptor may be a procedure
 * which then examines an extended descriptor passed as its environment.
 *
 * Arrays are treated as simple objects if they have sufficiently simple
 * structure.  Otherwise they are allocated from an array kind that supplies
 * a special mark procedure.  These arrays contain a pointer to a
 * `complex_descriptor` as their last "pointer-sized" word.
 * This is done because the environment field is too small, and the collector
 * must trace the `complex_descriptor`.
 *
 * Note that descriptors inside objects may appear cleared, if we encounter
 * a false reference to an object on a free list.  In the case of a simple
 * object, this is OK, since a zero descriptor corresponds to examining no
 * fields.  In the `complex_descriptor` case, we explicitly check for that
 * case.
 *
 * Note: major parts of this code have not been tested at all and are not
 * testable, since they are not accessible through the current interface.
 */

#include "gc/gc_typed.h"

/* Object kind for objects with indirect (possibly extended) descriptors. */
STATIC int GC_explicit_kind = 0;

/*
 * Object kind for objects with complex descriptors and
 * `GC_array_mark_proc`.
 */
STATIC int GC_array_kind = 0;

#define ED_INITIAL_SIZE 100

/* Indices of the typed mark procedures. */
STATIC unsigned GC_typed_mark_proc_index = 0;
STATIC unsigned GC_array_mark_proc_index = 0;

STATIC void
GC_push_typed_structures_proc(void)
{
  GC_PUSH_ALL_SYM(GC_ext_descriptors);
}

/*
 * Add a multi-word bitmap to `GC_ext_descriptors` arrays.
 * Returns starting index on success, -1 otherwise.
 */
STATIC GC_signed_word
GC_add_ext_descriptor(const word *bm, size_t nbits)
{
  GC_signed_word result;
  size_t i;
  size_t nwords = divWORDSZ(nbits + CPP_WORDSZ - 1);

  LOCK();
  while (UNLIKELY(GC_avail_descr + nwords >= GC_ed_size)) {
    typed_ext_descr_t *newExtD;
    size_t new_size;
    size_t ed_size = GC_ed_size;

    if (0 == ed_size) {
      GC_ASSERT(ADDR(&GC_ext_descriptors) % ALIGNMENT == 0);
      GC_push_typed_structures = GC_push_typed_structures_proc;
      UNLOCK();
      new_size = ED_INITIAL_SIZE;
    } else {
      UNLOCK();
      new_size = 2 * ed_size;
      if (new_size > MAX_ENV)
        return -1;
    }
    newExtD = (typed_ext_descr_t *)GC_malloc_atomic(
        new_size * sizeof(typed_ext_descr_t));
    if (NULL == newExtD)
      return -1;
    LOCK();
    if (ed_size == GC_ed_size) {
      if (GC_avail_descr != 0) {
        BCOPY(GC_ext_descriptors, newExtD,
              GC_avail_descr * sizeof(typed_ext_descr_t));
      }
      GC_ed_size = new_size;
      GC_ext_descriptors = newExtD;
    } else {
      /* Another thread is already resized it in the meantime. */
    }
  }
  result = (GC_signed_word)GC_avail_descr;
  for (i = 0; i < nwords - 1; i++) {
    GC_ext_descriptors[(size_t)result + i].ed_bitmap = bm[i];
    GC_ext_descriptors[(size_t)result + i].ed_continued = TRUE;
  }
  /* Clear irrelevant (highest) bits for the last element. */
  GC_ext_descriptors[(size_t)result + i].ed_bitmap
      = bm[i] & (GC_WORD_MAX >> (nwords * CPP_WORDSZ - nbits));
  GC_ext_descriptors[(size_t)result + i].ed_continued = FALSE;
  GC_avail_descr += nwords;
  GC_ASSERT(result >= 0);
  UNLOCK();
  return result;
}

/* Table of bitmap descriptors for `n` pointer-long all-pointer objects. */
STATIC GC_descr GC_bm_table[CPP_WORDSZ / 2] = { 0 };

/*
 * Return a descriptor for the concatenation of 2 objects, each one is
 * `lpw` pointers long and described by descriptor `d`.  The result is
 * known to be short enough to fit into a bitmap descriptor.
 * `d` is a `GC_DS_LENGTH` or `GC_DS_BITMAP` descriptor.
 */
STATIC GC_descr
GC_double_descr(GC_descr d, size_t lpw)
{
  GC_ASSERT(GC_bm_table[0] == GC_DS_BITMAP); /*< `bm` table is initialized */
  if ((d & GC_DS_TAGS) == GC_DS_LENGTH) {
    d = GC_bm_table[BYTES_TO_PTRS(d)];
  }
  d |= (d & ~(GC_descr)GC_DS_TAGS) >> lpw;
  return d;
}

STATIC mse *GC_CALLBACK GC_typed_mark_proc(word *addr, mse *mark_stack_top,
                                           mse *mark_stack_limit, word env);

STATIC mse *GC_CALLBACK GC_array_mark_proc(word *addr, mse *mark_stack_top,
                                           mse *mark_stack_limit, word env);

STATIC void
GC_init_explicit_typing(void)
{
  unsigned i;

  /*
   * Set up object kind with simple indirect descriptor.
   * Descriptor is in the last `word` of the object.
   */
  GC_typed_mark_proc_index = GC_new_proc_inner(GC_typed_mark_proc);
  GC_explicit_kind = (int)GC_new_kind_inner(
      GC_new_free_list_inner(),
      (PTRS_TO_BYTES(GC_WORD_MAX) | GC_DS_PER_OBJECT), TRUE, TRUE);

  /* Set up object kind with array descriptor. */
  GC_array_mark_proc_index = GC_new_proc_inner(GC_array_mark_proc);
  GC_array_kind = (int)GC_new_kind_inner(
      GC_new_free_list_inner(), GC_MAKE_PROC(GC_array_mark_proc_index, 0),
      FALSE, TRUE);

  GC_bm_table[0] = GC_DS_BITMAP;
  for (i = 1; i < CPP_WORDSZ / 2; i++) {
    GC_bm_table[i] = (GC_WORD_MAX << (CPP_WORDSZ - i)) | GC_DS_BITMAP;
  }
}

STATIC mse *GC_CALLBACK
GC_typed_mark_proc(word *addr, mse *mark_stack_top, mse *mark_stack_limit,
                   word env)
{
  word bm;
  ptr_t current_p = (ptr_t)addr;
  ptr_t greatest_ha = (ptr_t)GC_greatest_plausible_heap_addr;
  ptr_t least_ha = (ptr_t)GC_least_plausible_heap_addr;
  DECLARE_HDR_CACHE;

  /* The allocator lock is held by the collection initiating thread. */
  GC_ASSERT(GC_get_parallel() || I_HOLD_LOCK());
  bm = GC_ext_descriptors[env].ed_bitmap;

  INIT_HDR_CACHE;
  for (; bm != 0; bm >>= 1, current_p += sizeof(ptr_t)) {
    if (bm & 1) {
      ptr_t q;

      LOAD_PTR_OR_CONTINUE(q, current_p);
      FIXUP_POINTER(q);
      if (ADDR_LT(least_ha, q) && ADDR_LT(q, greatest_ha)) {
        PUSH_CONTENTS(q, mark_stack_top, mark_stack_limit, current_p);
      }
    }
  }
  if (GC_ext_descriptors[env].ed_continued) {
    /*
     * Push an entry with the rest of the descriptor back onto the stack.
     * Thus we never do too much work at once.  Note that we also cannot
     * overflow the mark stack unless we actually mark something.
     */
    mark_stack_top = GC_custom_push_proc(
        GC_MAKE_PROC(GC_typed_mark_proc_index, env + 1),
        (ptr_t *)addr + CPP_WORDSZ, mark_stack_top, mark_stack_limit);
  }
  return mark_stack_top;
}

GC_API GC_descr GC_CALL
GC_make_descriptor(const GC_word *bm, size_t len)
{
  GC_signed_word last_set_bit = (GC_signed_word)len - 1;
  GC_descr d;

#if defined(AO_HAVE_load_acquire) && defined(AO_HAVE_store_release)
  if (UNLIKELY(!AO_load_acquire(&GC_explicit_typing_initialized))) {
    LOCK();
    if (!GC_explicit_typing_initialized) {
      GC_init_explicit_typing();
      AO_store_release(&GC_explicit_typing_initialized, TRUE);
    }
    UNLOCK();
  }
#else
  LOCK();
  if (UNLIKELY(!GC_explicit_typing_initialized)) {
    GC_init_explicit_typing();
    GC_explicit_typing_initialized = TRUE;
  }
  UNLOCK();
#endif

  while (last_set_bit >= 0 && !GC_get_bit(bm, (word)last_set_bit))
    last_set_bit--;
  if (last_set_bit < 0) {
    /* No pointers. */
    return 0;
  }

#if ALIGNMENT == CPP_PTRSZ / 8
  {
    GC_signed_word i;

    for (i = 0; i < last_set_bit; i++) {
      if (!GC_get_bit(bm, (word)i))
        break;
    }
    if (i == last_set_bit) {
      /*
       * The initial section contains all pointers; use the length
       * descriptor.
       */
      return PTRS_TO_BYTES((word)last_set_bit + 1) | GC_DS_LENGTH;
    }
  }
#endif
  if (last_set_bit < BITMAP_BITS) {
    GC_signed_word i;

    /*
     * Hopefully the common case.  Build the bitmap descriptor (with the
     * bits reversed).
     */
    d = SIGNB;
    for (i = last_set_bit - 1; i >= 0; i--) {
      d >>= 1;
      if (GC_get_bit(bm, (word)i))
        d |= SIGNB;
    }
    d |= GC_DS_BITMAP;
  } else {
    GC_signed_word index = GC_add_ext_descriptor(bm, (size_t)last_set_bit + 1);

    if (UNLIKELY(index < 0)) {
      /* Out of memory: use a conservative approximation. */
      return PTRS_TO_BYTES((word)last_set_bit + 1) | GC_DS_LENGTH;
    }
#ifdef LINT2
    if ((word)index > MAX_ENV)
      ABORT("GC_add_ext_descriptor() result cannot exceed MAX_ENV");
#endif
    d = GC_MAKE_PROC(GC_typed_mark_proc_index, index);
  }
  return d;
}

static void
set_obj_descr(ptr_t op, GC_descr d)
{
  size_t sz;

  if (UNLIKELY(NULL == op))
    return;
  /*
   * It is not safe to use `GC_size_map[]` here as the table might be
   * updated asynchronously.
   */
  sz = GC_size(op);

  GC_ASSERT((sz & (GC_GRANULE_BYTES - 1)) == 0 && sz > sizeof(GC_descr));
#ifdef AO_HAVE_store_release
  AO_store_release((volatile AO_t *)&op[sz - sizeof(GC_descr)], d);
#else
  *(GC_descr *)&op[sz - sizeof(GC_descr)] = d;
#endif
}

GC_API GC_ATTR_MALLOC void *GC_CALL
GC_malloc_explicitly_typed(size_t lb, GC_descr d)
{
  ptr_t op;

  GC_ASSERT(GC_explicit_typing_initialized);
  if (UNLIKELY(lb < sizeof(ptr_t) - sizeof(GC_descr) + 1)) {
    /* Ensure the descriptor does not occupy the first pointer place. */
    lb = sizeof(ptr_t) - sizeof(GC_descr) + 1;
  }
  op = (ptr_t)GC_malloc_kind(SIZET_SAT_ADD(lb, sizeof(GC_descr) - EXTRA_BYTES),
                             GC_explicit_kind);
  set_obj_descr(op, d);
  return op;
}

GC_API GC_ATTR_MALLOC void *GC_CALL
GC_malloc_explicitly_typed_ignore_off_page(size_t lb, GC_descr d)
{
  ptr_t op;

  if (lb < HBLKSIZE - sizeof(GC_descr))
    return GC_malloc_explicitly_typed(lb, d);

  GC_ASSERT(GC_explicit_typing_initialized);
  /*
   * Note that ignore-off-page objects with the requested size of
   * at least `HBLKSIZE` do not have `EXTRA_BYTES` added by
   * `GC_generic_malloc_aligned()`.
   */
  op = (ptr_t)GC_clear_stack(
      GC_generic_malloc_aligned(SIZET_SAT_ADD(lb, sizeof(GC_descr)),
                                GC_explicit_kind, IGNORE_OFF_PAGE, 0));
  set_obj_descr(op, d);
  return op;
}

/*
 * Array descriptors.  `GC_array_mark_proc` understands these.
 * We may eventually need to add provisions for headers and trailers.
 * Hence we provide for tree structured descriptors, though we do not
 * really use them currently.
 */

/* This type describes simple array. */
struct LeafDescriptor {
  word ld_tag;
#define LEAF_TAG 1
  /* Bytes per element; nonzero, multiple of `ALIGNMENT`. */
  size_t ld_size;
  /* Number of elements. */
  size_t ld_nelements;
  /* A simple length, bitmap, or procedure descriptor. */
  GC_descr ld_descriptor;
};

struct ComplexArrayDescriptor {
  word ad_tag;
#define ARRAY_TAG 2
  size_t ad_nelements;
  union ComplexDescriptor *ad_element_descr;
};

struct SequenceDescriptor {
  word sd_tag;
#define SEQUENCE_TAG 3
  union ComplexDescriptor *sd_first;
  union ComplexDescriptor *sd_second;
};

typedef union ComplexDescriptor {
  struct LeafDescriptor ld;
  struct ComplexArrayDescriptor ad;
  struct SequenceDescriptor sd;
} complex_descriptor;

STATIC complex_descriptor *
GC_make_leaf_descriptor(size_t size, size_t nelements, GC_descr d)
{
  complex_descriptor *result
      = (complex_descriptor *)GC_malloc_atomic(sizeof(struct LeafDescriptor));

  GC_ASSERT(size != 0);
  if (UNLIKELY(NULL == result))
    return NULL;

  result->ld.ld_tag = LEAF_TAG;
  result->ld.ld_size = size;
  result->ld.ld_nelements = nelements;
  result->ld.ld_descriptor = d;
  return result;
}

STATIC complex_descriptor *
GC_make_sequence_descriptor(complex_descriptor *first,
                            complex_descriptor *second)
{
  /*
   * Note: for a reason, the sanitizer runtime complains of insufficient
   * space for `complex_descriptor` if the pointer type of `result` variable
   * is changed to.
   */
  struct SequenceDescriptor *result = (struct SequenceDescriptor *)GC_malloc(
      sizeof(struct SequenceDescriptor));

  if (UNLIKELY(NULL == result))
    return NULL;

  /*
   * Cannot result in overly conservative marking, since tags are very
   * small integers.  Probably faster than maintaining type information.
   */
  result->sd_tag = SEQUENCE_TAG;
  result->sd_first = first;
  result->sd_second = second;
  GC_dirty(result);
  REACHABLE_AFTER_DIRTY(first);
  REACHABLE_AFTER_DIRTY(second);
  return (complex_descriptor *)result;
}

#define NO_MEM (-1)
#define SIMPLE 0
#define LEAF 1
#define COMPLEX 2

/*
 * Build a descriptor for an array with `nelements` elements, each of
 * which can be described by a simple descriptor `d`.
 * We try to optimize some common cases.  If the result is `COMPLEX`,
 * a `complex_descriptor *` value is returned in `*pcomplex_d`.
 * If the result is `LEAF`, then a `LeafDescriptor` value is built in the
 * structure pointed to by `pleaf`.  The tag in the `*pleaf` structure
 * is not set.  If the result is `SIMPLE`, then a `GC_descr` value is
 * returned in `*psimple_d`.  If the result is `NO_MEM`, then we failed
 * to allocate the descriptor.  The implementation assumes `GC_DS_LENGTH`
 * is 0.  `*pleaf`, `*pcomplex_d` and `*psimple_d` may be used as
 * temporaries during the construction.
 */
STATIC int
GC_make_array_descriptor(size_t nelements, size_t size, GC_descr d,
                         GC_descr *psimple_d, complex_descriptor **pcomplex_d,
                         struct LeafDescriptor *pleaf)
{
  /*
   * For larger arrays, we try to combine descriptors of adjacent
   * descriptors to speed up marking, and to reduce the amount of space
   * needed on the mark stack.
   */
#define OPT_THRESHOLD 50

  GC_ASSERT(size != 0);
  if ((d & GC_DS_TAGS) == GC_DS_LENGTH) {
    if (d == (GC_descr)size) {
      /* Note: no overflow is guaranteed by caller. */
      *psimple_d = nelements * d;
      return SIMPLE;
    } else if (0 == d) {
      *psimple_d = 0;
      return SIMPLE;
    }
  }

  if (nelements <= OPT_THRESHOLD) {
    if (nelements <= 1) {
      *psimple_d = nelements == 1 ? d : 0;
      return SIMPLE;
    }
  } else if (size <= BITMAP_BITS / 2 && (d & GC_DS_TAGS) != GC_DS_PROC
             && (size & (sizeof(ptr_t) - 1)) == 0) {
    complex_descriptor *one_element, *beginning;
    int result = GC_make_array_descriptor(
        nelements / 2, 2 * size, GC_double_descr(d, BYTES_TO_PTRS(size)),
        psimple_d, pcomplex_d, pleaf);

    if ((nelements & 1) == 0 || UNLIKELY(NO_MEM == result))
      return result;

    one_element = GC_make_leaf_descriptor(size, 1, d);
    if (UNLIKELY(NULL == one_element))
      return NO_MEM;

    if (COMPLEX == result) {
      beginning = *pcomplex_d;
    } else {
      beginning
          = SIMPLE == result
                ? GC_make_leaf_descriptor(size, 1, *psimple_d)
                : GC_make_leaf_descriptor(pleaf->ld_size, pleaf->ld_nelements,
                                          pleaf->ld_descriptor);
      if (UNLIKELY(NULL == beginning))
        return NO_MEM;
    }
    *pcomplex_d = GC_make_sequence_descriptor(beginning, one_element);
    if (UNLIKELY(NULL == *pcomplex_d))
      return NO_MEM;

    return COMPLEX;
  }

  pleaf->ld_size = size;
  pleaf->ld_nelements = nelements;
  pleaf->ld_descriptor = d;
  return LEAF;
}

struct GC_calloc_typed_descr_s {
  complex_descriptor *complex_d; /*< the first field, the only pointer */
  struct LeafDescriptor leaf;
  GC_descr simple_d;
  word alloc_lb;             /*< of `size_t` type actually */
  GC_signed_word descr_type; /*< of `int` type actually */
};

GC_API int GC_CALL
GC_calloc_prepare_explicitly_typed(struct GC_calloc_typed_descr_s *pctd,
                                   size_t ctd_sz, size_t n, size_t lb,
                                   GC_descr d)
{
  GC_STATIC_ASSERT(sizeof(struct GC_calloc_typed_descr_opaque_s)
                   == sizeof(struct GC_calloc_typed_descr_s));
  GC_ASSERT(GC_explicit_typing_initialized);
  GC_ASSERT(sizeof(struct GC_calloc_typed_descr_s) == ctd_sz);
  (void)ctd_sz; /*< unused currently */
  if (UNLIKELY(0 == lb || 0 == n))
    lb = n = 1;
  if (UNLIKELY((lb | n) > GC_SQRT_SIZE_MAX) /*< fast initial check */
      && n > GC_SIZE_MAX / lb) {
    /* `n * lb` overflows. */
    pctd->alloc_lb = GC_SIZE_MAX;
    pctd->descr_type = NO_MEM;
    /* The rest of the fields are unset. */
    return 0; /*< failure */
  }

  pctd->descr_type = GC_make_array_descriptor(n, lb, d, &pctd->simple_d,
                                              &pctd->complex_d, &pctd->leaf);
  switch (pctd->descr_type) {
  case NO_MEM:
  case SIMPLE:
    pctd->alloc_lb = (word)lb * n;
    break;
  case LEAF:
    pctd->alloc_lb = SIZET_SAT_ADD(
        lb * n, (BYTES_TO_PTRS_ROUNDUP(sizeof(struct LeafDescriptor)) + 1)
                        * sizeof(ptr_t)
                    - EXTRA_BYTES);
    break;
  case COMPLEX:
    pctd->alloc_lb = SIZET_SAT_ADD(lb * n, sizeof(ptr_t) - EXTRA_BYTES);
    break;
  }
  return 1; /*< success */
}

GC_API GC_ATTR_MALLOC void *GC_CALL
GC_calloc_do_explicitly_typed(const struct GC_calloc_typed_descr_s *pctd,
                              size_t ctd_sz)
{
  void *op;
  size_t lpw_m1;

  GC_ASSERT(sizeof(struct GC_calloc_typed_descr_s) == ctd_sz);
  (void)ctd_sz; /*< unused currently */
  switch (pctd->descr_type) {
  case NO_MEM:
    return (*GC_get_oom_fn())((size_t)pctd->alloc_lb);
  case SIMPLE:
    return GC_malloc_explicitly_typed((size_t)pctd->alloc_lb, pctd->simple_d);
  case LEAF:
  case COMPLEX:
    break;
  default:
    ABORT_RET("Bad descriptor type");
    return NULL;
  }
  op = GC_malloc_kind((size_t)pctd->alloc_lb, GC_array_kind);
  if (UNLIKELY(NULL == op))
    return NULL;

  lpw_m1 = BYTES_TO_PTRS(GC_size(op)) - 1;
  if (pctd->descr_type == LEAF) {
    /* Set up the descriptor inside the object itself. */
    struct LeafDescriptor *lp
        = (struct LeafDescriptor *)((ptr_t *)op + lpw_m1
                                    - BYTES_TO_PTRS_ROUNDUP(
                                        sizeof(struct LeafDescriptor)));

    lp->ld_tag = LEAF_TAG;
    lp->ld_size = pctd->leaf.ld_size;
    lp->ld_nelements = pctd->leaf.ld_nelements;
    lp->ld_descriptor = pctd->leaf.ld_descriptor;
    /*
     * Hold the allocator lock (in the reader mode which should be enough)
     * while writing the descriptor `word` to the object to ensure that
     * the descriptor contents are seen by `GC_array_mark_proc` as expected.
     */

    /*
     * TODO: It should be possible to replace locking with the atomic
     * operations (with the release barrier here) but, in this case,
     * avoiding the acquire barrier in `GC_array_mark_proc` seems to
     * be tricky as `GC_mark_some` might be invoked with the world running.
     */
    READER_LOCK();
    ((struct LeafDescriptor **)op)[lpw_m1] = lp;
    READER_UNLOCK_RELEASE();
  } else {
#ifndef GC_NO_FINALIZATION
    READER_LOCK();
    ((complex_descriptor **)op)[lpw_m1] = pctd->complex_d;
    READER_UNLOCK_RELEASE();

    GC_dirty((ptr_t *)op + lpw_m1);
    REACHABLE_AFTER_DIRTY(pctd->complex_d);

    /*
     * Make sure the descriptor is cleared once there is any danger
     * it may have been collected.
     */
    if (UNLIKELY(
            GC_general_register_disappearing_link((void **)op + lpw_m1, op)
            == GC_NO_MEMORY))
#endif
    {
      /* Could not register it due to lack of memory.  Punt. */
      return (*GC_get_oom_fn())((size_t)pctd->alloc_lb);
    }
  }
  return op;
}

GC_API GC_ATTR_MALLOC void *GC_CALL
GC_calloc_explicitly_typed(size_t n, size_t lb, GC_descr d)
{
  struct GC_calloc_typed_descr_s ctd;

  (void)GC_calloc_prepare_explicitly_typed(&ctd, sizeof(ctd), n, lb, d);
  return GC_calloc_do_explicitly_typed(&ctd, sizeof(ctd));
}

/*
 * Return the size of the object described by `complex_d`.
 * It would be faster to store this directly, or to compute it as part
 * of `GC_push_complex_descriptor`, but hopefully it does not matter.
 */
STATIC size_t
GC_descr_obj_size(complex_descriptor *complex_d)
{
  switch (complex_d->ad.ad_tag) {
  case LEAF_TAG:
    return complex_d->ld.ld_nelements * complex_d->ld.ld_size;
  case ARRAY_TAG:
    return complex_d->ad.ad_nelements
           * GC_descr_obj_size(complex_d->ad.ad_element_descr);
  case SEQUENCE_TAG:
    return GC_descr_obj_size(complex_d->sd.sd_first)
           + GC_descr_obj_size(complex_d->sd.sd_second);
  default:
    ABORT_RET("Bad complex descriptor");
    return 0;
  }
}

/*
 * Push descriptors for the object with the complex descriptor onto
 * the mark stack.  Return `NULL` if the mark stack overflowed.
 */
STATIC mse *
GC_push_complex_descriptor(ptr_t current, complex_descriptor *complex_d,
                           mse *msp, mse *msl)
{
  size_t i, nelements;
  size_t sz;
  GC_descr d;
  complex_descriptor *element_descr;

  switch (complex_d->ad.ad_tag) {
  case LEAF_TAG:
    d = complex_d->ld.ld_descriptor;
    nelements = complex_d->ld.ld_nelements;
    sz = complex_d->ld.ld_size;

    if (UNLIKELY(msl - msp <= (GC_signed_word)nelements))
      return NULL;
    GC_ASSERT(sz != 0);
    for (i = 0; i < nelements; i++) {
      msp++;
      msp->mse_start = current;
      msp->mse_descr = d;
      current += sz;
    }
    break;
  case ARRAY_TAG:
    element_descr = complex_d->ad.ad_element_descr;
    nelements = complex_d->ad.ad_nelements;
    sz = GC_descr_obj_size(element_descr);
    GC_ASSERT(sz != 0 || 0 == nelements);
    for (i = 0; i < nelements; i++) {
      msp = GC_push_complex_descriptor(current, element_descr, msp, msl);
      if (UNLIKELY(NULL == msp))
        return NULL;
      current += sz;
    }
    break;
  case SEQUENCE_TAG:
    sz = GC_descr_obj_size(complex_d->sd.sd_first);
    msp = GC_push_complex_descriptor(current, complex_d->sd.sd_first, msp,
                                     msl);
    if (UNLIKELY(NULL == msp))
      return NULL;
    GC_ASSERT(sz != 0);
    current += sz;
    msp = GC_push_complex_descriptor(current, complex_d->sd.sd_second, msp,
                                     msl);
    break;
  default:
    ABORT("Bad complex descriptor");
  }
  return msp;
}

GC_ATTR_NO_SANITIZE_THREAD
static complex_descriptor *
get_complex_descr(ptr_t *p, size_t lpw)
{
  return (complex_descriptor *)p[lpw - 1];
}

/* Used by `GC_calloc_do_explicitly_typed()` via `GC_array_kind`. */
STATIC mse *GC_CALLBACK
GC_array_mark_proc(word *addr, mse *mark_stack_top, mse *mark_stack_limit,
                   word env)
{
  size_t sz = HDR(addr)->hb_sz;
  size_t lpw = BYTES_TO_PTRS(sz);
  complex_descriptor *complex_d = get_complex_descr((ptr_t *)addr, lpw);
  mse *orig_mark_stack_top = mark_stack_top;
  mse *new_mark_stack_top;

  UNUSED_ARG(env);
  if (NULL == complex_d) {
    /* Found a reference to a free-list entry.  Ignore it. */
    return orig_mark_stack_top;
  }
  /*
   * In-use counts were already updated when array descriptor was pushed.
   * Here we only replace it by subobject descriptors, so no update is
   * necessary.
   */
  new_mark_stack_top = GC_push_complex_descriptor(
      (ptr_t)addr, complex_d, mark_stack_top, mark_stack_limit - 1);
  if (NULL == new_mark_stack_top) {
    /* Explicitly instruct Clang Static Analyzer that pointer is non-`NULL`. */
    if (NULL == mark_stack_top) {
      ABORT("Bad mark_stack_top");
    }

    /*
     * Does not fit.  Conservatively push the whole array as a unit and
     * request a mark stack expansion.  This cannot cause a mark stack
     * overflow, since it replaces the original array entry.
     */
#ifdef PARALLEL_MARK
    /* We might be using a `local_mark_stack` in the parallel collection. */
    if (GC_mark_stack + GC_mark_stack_size == mark_stack_limit)
#endif
    {
      GC_mark_stack_too_small = TRUE;
    }
    new_mark_stack_top = orig_mark_stack_top + 1;
    new_mark_stack_top->mse_start = (ptr_t)addr;
    new_mark_stack_top->mse_descr = sz | GC_DS_LENGTH;
  } else {
    /* Push descriptor itself. */
    new_mark_stack_top++;
    new_mark_stack_top->mse_start = (ptr_t)((ptr_t *)addr + lpw - 1);
    new_mark_stack_top->mse_descr = sizeof(ptr_t) | GC_DS_LENGTH;
  }
  return new_mark_stack_top;
}
