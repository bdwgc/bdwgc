/*
 * Copyright 1988, 1989 Hans-J. Boehm, Alan J. Demers
 * Copyright (c) 1991-1994 by Xerox Corporation.  All rights reserved.
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

#ifndef NO_BLACK_LISTING

/*
 * We maintain several hash tables of `hblk` entities that have had false
 * hits.  Each contains one bit per hash bucket.  If any page in the bucket
 * has had a false hit, we assume that all of them have.
 * See the definition of `page_hash_table` in `gc_priv.h` file.
 * False hits from the stack(s) are much more dangerous than false hits
 * from elsewhere, since the former can pin a large object that spans the
 * block, even though it does not start on the dangerous block.
 */

/*
 * Externally callable routines are:
 *   - `GC_add_to_black_list_normal`,
 *   - `GC_add_to_black_list_stack`,
 *   - `GC_promote_black_lists`.
 */

STATIC void
GC_clear_bl(word *bl)
{
  BZERO(bl, sizeof(page_hash_table));
}

STATIC void
GC_copy_bl(const word *old, word *dest)
{
  BCOPY(old, dest, sizeof(page_hash_table));
}

#  ifdef PRINT_BLACK_LIST
STATIC void
GC_print_blacklisted_ptr(ptr_t p, ptr_t source, const char *kind_str)
{
  ptr_t base = (ptr_t)GC_base(source);

  if (NULL == base) {
    GC_err_printf("Black listing (%s) %p referenced from %p in %s\n", kind_str,
                  (void *)p, (void *)source,
                  NULL != source ? "root set" : "register");
  } else {
    /*
     * FIXME: We cannot call the debug variant of `GC_print_heap_obj`
     * (with `PRINT_CALL_CHAIN`) here because the allocator lock is held
     * and the world is stopped.
     */
    GC_err_printf("Black listing (%s) %p referenced from %p in"
                  " object at %p of appr. %lu bytes\n",
                  kind_str, (void *)p, (void *)source, (void *)base,
                  (unsigned long)GC_size(base));
  }
}
#  endif /* PRINT_BLACK_LIST */

GC_INNER void
GC_bl_init_no_interiors(void)
{
  GC_ASSERT(I_HOLD_LOCK());
  if (NULL == GC_incomplete_normal_bl) {
    GC_old_normal_bl = (word *)GC_scratch_alloc(sizeof(page_hash_table));
    GC_incomplete_normal_bl
        = (word *)GC_scratch_alloc(sizeof(page_hash_table));
    if (NULL == GC_old_normal_bl || NULL == GC_incomplete_normal_bl) {
      GC_err_printf("Insufficient memory for black list\n");
      EXIT();
    }
    GC_clear_bl(GC_old_normal_bl);
    GC_clear_bl(GC_incomplete_normal_bl);
  }
}

GC_INNER void
GC_bl_init(void)
{
  GC_ASSERT(I_HOLD_LOCK());
  GC_black_list_spacing = MINHINCR * HBLKSIZE; /*< initial guess */
  if (!GC_all_interior_pointers) {
    GC_bl_init_no_interiors();
  }
  GC_ASSERT(NULL == GC_old_stack_bl && NULL == GC_incomplete_stack_bl);
  GC_old_stack_bl = (word *)GC_scratch_alloc(sizeof(page_hash_table));
  GC_incomplete_stack_bl = (word *)GC_scratch_alloc(sizeof(page_hash_table));
  if (NULL == GC_old_stack_bl || NULL == GC_incomplete_stack_bl) {
    GC_err_printf("Insufficient memory for black list\n");
    EXIT();
  }
  GC_clear_bl(GC_old_stack_bl);
  GC_clear_bl(GC_incomplete_stack_bl);
}

static word compute_total_stack_black_listed(void);

GC_INNER void
GC_promote_black_lists(void)
{
  word *very_old_normal_bl = GC_old_normal_bl;
  word *very_old_stack_bl = GC_old_stack_bl;
  word total_stack_black_listed; /*< number of bytes on stack blacklist */

  GC_ASSERT(I_HOLD_LOCK());
  GC_old_normal_bl = GC_incomplete_normal_bl;
  GC_old_stack_bl = GC_incomplete_stack_bl;
  if (!GC_all_interior_pointers) {
    GC_clear_bl(very_old_normal_bl);
  }
  GC_clear_bl(very_old_stack_bl);
  GC_incomplete_normal_bl = very_old_normal_bl;
  GC_incomplete_stack_bl = very_old_stack_bl;
  total_stack_black_listed = compute_total_stack_black_listed();
  GC_VERBOSE_LOG_PRINTF(
      "%lu bytes in heap blacklisted for interior pointers\n",
      (unsigned long)total_stack_black_listed);
  if (total_stack_black_listed != 0)
    GC_black_list_spacing
        = HBLKSIZE * (GC_heapsize / total_stack_black_listed);
  if (GC_black_list_spacing < 3 * HBLKSIZE)
    GC_black_list_spacing = 3 * HBLKSIZE;
  if (GC_black_list_spacing > MAXHINCR * HBLKSIZE) {
    /*
     * Make it easier to allocate really huge blocks, which otherwise may
     * have problems with nonuniform blacklist distributions.
     * This way we should always succeed immediately after growing the heap.
     */
    GC_black_list_spacing = MAXHINCR * HBLKSIZE;
  }
}

GC_INNER void
GC_unpromote_black_lists(void)
{
  if (!GC_all_interior_pointers) {
    GC_copy_bl(GC_old_normal_bl, GC_incomplete_normal_bl);
  }
  GC_copy_bl(GC_old_stack_bl, GC_incomplete_stack_bl);
}

#  if defined(PARALLEL_MARK) && defined(THREAD_SANITIZER)
#    define backlist_set_pht_entry_from_index(db, index) \
      set_pht_entry_from_index_concurrent(db, index)
#  else
/*
 * It is safe to set a bit in a blacklist even without synchronization,
 * the only drawback is that we might have to redo black-listing sometimes.
 */
#    define backlist_set_pht_entry_from_index(bl, index) \
      set_pht_entry_from_index(bl, index)
#  endif

#  ifdef PRINT_BLACK_LIST
GC_INNER void
GC_add_to_black_list_normal(ptr_t p, ptr_t source)
#  else
GC_INNER void
GC_add_to_black_list_normal(ptr_t p)
#  endif
{
#  ifndef PARALLEL_MARK
  GC_ASSERT(I_HOLD_LOCK());
#  endif
  if (GC_modws_valid_offsets[ADDR(p) & (sizeof(ptr_t) - 1)]) {
    size_t index = PHT_HASH(p);

    if (NULL == HDR(p) || get_pht_entry_from_index(GC_old_normal_bl, index)) {
#  ifdef PRINT_BLACK_LIST
      if (!get_pht_entry_from_index(GC_incomplete_normal_bl, index)) {
        GC_print_blacklisted_ptr(p, source, "normal");
      }
#  endif
      backlist_set_pht_entry_from_index(GC_incomplete_normal_bl, index);
    } else {
      /*
       * This is probably just an interior pointer to an allocated object,
       * and is not worth black listing.
       */
    }
  }
}

#  ifdef PRINT_BLACK_LIST
GC_INNER void
GC_add_to_black_list_stack(ptr_t p, ptr_t source)
#  else
GC_INNER void
GC_add_to_black_list_stack(ptr_t p)
#  endif
{
  size_t index = PHT_HASH(p);

#  ifndef PARALLEL_MARK
  GC_ASSERT(I_HOLD_LOCK());
#  endif
  if (NULL == HDR(p) || get_pht_entry_from_index(GC_old_stack_bl, index)) {
#  ifdef PRINT_BLACK_LIST
    if (!get_pht_entry_from_index(GC_incomplete_stack_bl, index)) {
      GC_print_blacklisted_ptr(p, source, "stack");
    }
#  endif
    backlist_set_pht_entry_from_index(GC_incomplete_stack_bl, index);
  }
}

#endif /* !NO_BLACK_LISTING */

GC_API struct GC_hblk_s *GC_CALL
GC_is_black_listed(struct GC_hblk_s *h, size_t len)
{
#ifdef NO_BLACK_LISTING
  UNUSED_ARG(h);
  UNUSED_ARG(len);
#else
  size_t index = PHT_HASH(h);
  size_t i, nblocks;

  if (!GC_all_interior_pointers
      && (get_pht_entry_from_index(GC_old_normal_bl, index)
          || get_pht_entry_from_index(GC_incomplete_normal_bl, index))) {
    return h + 1;
  }

  nblocks = divHBLKSZ(len);
  for (i = 0;;) {
    if (0 == GC_old_stack_bl[divWORDSZ(index)]
        && 0 == GC_incomplete_stack_bl[divWORDSZ(index)]) {
      /* An easy case. */
      i += CPP_WORDSZ - modWORDSZ(index);
    } else {
      if (get_pht_entry_from_index(GC_old_stack_bl, index)
          || get_pht_entry_from_index(GC_incomplete_stack_bl, index)) {
        return &h[i + 1];
      }
      i++;
    }
    if (i >= nblocks)
      break;
    index = PHT_HASH(h + i);
  }
#endif
  return NULL;
}

#ifndef NO_BLACK_LISTING
/*
 * Return the number of black-listed blocks in a given range.  Used only
 * for statistical purposes.  Looks only at the `GC_incomplete_stack_bl`.
 */
STATIC word
GC_number_stack_black_listed(struct hblk *start, struct hblk *endp1)
{
  struct hblk *h;
  word result = 0;

  for (h = start; ADDR_LT((ptr_t)h, (ptr_t)endp1); h++) {
    size_t index = PHT_HASH(h);

    if (get_pht_entry_from_index(GC_old_stack_bl, index))
      result++;
  }
  return result;
}

/* Return the total number of (stack) black-listed bytes. */
static word
compute_total_stack_black_listed(void)
{
  size_t i;
  word total = 0;

  for (i = 0; i < GC_n_heap_sects; i++) {
    struct hblk *start = (struct hblk *)GC_heap_sects[i].hs_start;
    struct hblk *endp1 = start + divHBLKSZ(GC_heap_sects[i].hs_bytes);

    total += GC_number_stack_black_listed(start, endp1);
  }
  return total * HBLKSIZE;
}
#endif /* !NO_BLACK_LISTING */
