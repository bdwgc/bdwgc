/*
 * Copyright (c) 1992-1994 by Xerox Corporation.  All rights reserved.
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

#ifdef CHECKSUMS

/*
 * This is debugging code intended to verify the results of dirty bit
 * computations.  Currently works only in a single-threaded environment.
 */

#  define NSUMS 10000
#  define OFFSET 0x10000

typedef struct {
  GC_bool new_valid;
  word old_sum;
  word new_sum;

  /*
   * Block to which this refers plus `OFFSET` to hide it from the
   * garbage collector.
   */
  struct hblk *block;
} page_entry;

/* Declare symbols externally visible for debugging purpose. */
EXTERN_C_BEGIN
extern page_entry GC_sums[NSUMS];
extern int GC_n_dirty_errors, GC_n_faulted_dirty_errors;
extern unsigned long GC_n_clean, GC_n_dirty;
EXTERN_C_END

page_entry GC_sums[NSUMS] = { { FALSE, 0, 0, NULL } };

/* Record of pages on which we saw a write fault. */
STATIC word GC_faulted[NSUMS] = { 0 };

STATIC size_t GC_n_faulted = 0;

#  ifndef GC_NO_DEINIT
void
GC_reset_check_page(void)
{
  BZERO(GC_sums, sizeof(GC_sums));
  GC_n_faulted = 0;
}
#  endif

#  ifdef MPROTECT_VDB
void
GC_record_fault(struct hblk *h)
{
  GC_ASSERT(GC_page_size != 0);
  if (GC_n_faulted >= NSUMS)
    ABORT("write fault log overflowed");
  GC_faulted[GC_n_faulted++] = ADDR(HBLK_PAGE_ALIGNED(h));
}
#  endif

STATIC GC_bool
GC_was_faulted(struct hblk *h)
{
  size_t i;
  word page = ADDR(HBLK_PAGE_ALIGNED(h));

  for (i = 0; i < GC_n_faulted; ++i) {
    if (GC_faulted[i] == page)
      return TRUE;
  }
  return FALSE;
}

STATIC word
GC_checksum(struct hblk *h)
{
  word *p;
  word *lim = (word *)(h + 1);
  word result = 0;

  for (p = (word *)h; ADDR_LT((ptr_t)p, (ptr_t)lim); p++) {
    result += *p;
  }
  return result | SIGNB; /*< does not look like pointer */
}

int GC_n_dirty_errors = 0;
int GC_n_faulted_dirty_errors = 0;
unsigned long GC_n_clean = 0;
unsigned long GC_n_dirty = 0;

STATIC void
GC_update_check_page(struct hblk *h, int index)
{
  page_entry *pe = &GC_sums[index];
  hdr *hhdr = HDR(h);

  if (pe->block != 0 && pe->block != h + OFFSET)
    ABORT("goofed");
  pe->old_sum = pe->new_sum;
  pe->new_sum = GC_checksum(h);
#  if !defined(MSWIN32) && !defined(MSWINCE)
  if (pe->new_sum != SIGNB && !GC_page_was_ever_dirty(h)) {
    GC_err_printf("GC_page_was_ever_dirty(%p) is wrong\n", (void *)h);
  }
#  endif
  if (GC_page_was_dirty(h)) {
    GC_n_dirty++;
  } else {
    GC_n_clean++;
  }
  if (hhdr != NULL) {
    (void)GC_find_starting_hblk(h, &hhdr);
    if (pe->new_valid
#  ifdef SOFT_VDB
        && !HBLK_IS_FREE(hhdr)
#  endif
        && !IS_PTRFREE(hhdr) && pe->old_sum != pe->new_sum) {
      if (!GC_page_was_dirty(h) || !GC_page_was_ever_dirty(h)) {
        GC_bool was_faulted = GC_was_faulted(h);

        GC_n_dirty_errors++; /*< set breakpoint here */
        if (was_faulted)
          GC_n_faulted_dirty_errors++;
      }
    }
  }
  pe->new_valid = TRUE;
  pe->block = h + OFFSET;
}

/* Should be called immediately after `GC_read_dirty`. */
void
GC_check_dirty(void)
{
  int index;
  size_t i;

  GC_n_dirty_errors = 0;
  GC_n_faulted_dirty_errors = 0;
  GC_n_clean = 0;
  GC_n_dirty = 0;

  index = 0;
  for (i = 0; i < GC_n_heap_sects; i++) {
    ptr_t start = GC_heap_sects[i].hs_start;
    struct hblk *h;

    for (h = (struct hblk *)start;
         ADDR_LT((ptr_t)h, start + GC_heap_sects[i].hs_bytes); h++) {
      GC_update_check_page(h, index);
      index++;
      if (index >= NSUMS) {
        i = GC_n_heap_sects;
        break;
      }
    }
  }

  GC_COND_LOG_PRINTF("Checked %lu clean and %lu dirty pages\n", GC_n_clean,
                     GC_n_dirty);
  if (GC_n_dirty_errors > 0) {
    GC_err_printf("Found %d dirty bit errors (%d were faulted)\n",
                  GC_n_dirty_errors, GC_n_faulted_dirty_errors);
  }
  if (GC_n_faulted > 0) {
    /* Do not expose block addresses to the garbage collector. */
    BZERO(GC_faulted, GC_n_faulted * sizeof(word));
    GC_n_faulted = 0;
  }
}

#endif /* CHECKSUMS */
