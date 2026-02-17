/*
 * Copyright 1988, 1989 Hans-J. Boehm, Alan J. Demers
 * Copyright (c) 1991-1995 by Xerox Corporation.  All rights reserved.
 * Copyright (c) 2000 by Hewlett-Packard Company.  All rights reserved.
 * Copyright (c) 2008-2025 Ivan Maidanski
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
 * Make arguments appear live to compiler.  Put here to minimize the
 * risk of inlining.  Used to minimize junk left in registers.
 */
GC_ATTR_NOINLINE
void
GC_noop6(word arg1, word arg2, word arg3, word arg4, word arg5, word arg6)
{
  UNUSED_ARG(arg1);
  UNUSED_ARG(arg2);
  UNUSED_ARG(arg3);
  UNUSED_ARG(arg4);
  UNUSED_ARG(arg5);
  UNUSED_ARG(arg6);
  /* Avoid `GC_noop6` calls to be optimized away. */
#if defined(AO_HAVE_compiler_barrier) && !defined(BASE_ATOMIC_OPS_EMULATED)
  AO_compiler_barrier(); /*< to serve as a special side-effect */
#else
  GC_noop1(0);
#endif
}

GC_API void GC_CALL
GC_noop1(GC_word x)
{
#if defined(AO_HAVE_store) && defined(THREAD_SANITIZER)
  AO_store(&GC_noop_sink, (AO_t)x);
#else
  GC_noop_sink = x;
#endif
}

GC_API void GC_CALL
GC_noop1_ptr(volatile void *p)
{
#if CPP_PTRSZ > CPP_WORDSZ
#  if defined(AO_HAVE_store) && defined(THREAD_SANITIZER)
  GC_cptr_store(&GC_noop_sink_ptr, (ptr_t)CAST_AWAY_VOLATILE_PVOID(p));
#  else
  GC_noop_sink_ptr = (ptr_t)CAST_AWAY_VOLATILE_PVOID(p);
#  endif
#else
  GC_noop1(ADDR(p));
#endif
}

/*
 * Initialize `GC_obj_kinds` properly and standard free lists properly.
 * This must be done statically since they may be accessed before
 * `GC_init` is called.  It is done here, since we need to deal with
 * mark descriptors.  Note: `GC_obj_kinds[NORMAL].ok_descriptor` is
 * adjusted in `GC_init()` for `EXTRA_BYTES`.
 */
GC_INNER struct obj_kind GC_obj_kinds[MAXOBJKINDS] = {
  /* `PTRFREE` */
  { &GC_aobjfreelist[0], 0 /*< filled in dynamically */,
    /* `0 |` */ GC_DS_LENGTH, FALSE,
    FALSE
        /*, */ OK_DISCLAIM_INITZ },
  /* `NORMAL` */
  { &GC_objfreelist[0], 0,
    /* `0 |` */ GC_DS_LENGTH, TRUE /*< add length to descriptor template */,
    TRUE
        /*, */ OK_DISCLAIM_INITZ },
  /* `UNCOLLECTABLE` */
  { &GC_uobjfreelist[0], 0,
    /* `0 |` */ GC_DS_LENGTH, TRUE /*< add length to descriptor template */,
    TRUE
        /*, */ OK_DISCLAIM_INITZ },
#ifdef GC_ATOMIC_UNCOLLECTABLE
  /* `AUNCOLLECTABLE` */
  { &GC_auobjfreelist[0], 0,
    /* `0 |` */ GC_DS_LENGTH, FALSE,
    FALSE
        /*, */ OK_DISCLAIM_INITZ },
#endif
};

#ifndef GC_NO_DEINIT
/* Note: keep this close to `GC_obj_kinds` definition. */
GC_INNER void
GC_reset_obj_kinds(void)
{
  unsigned i;

  for (i = 0; i < GC_N_KINDS_INITIAL_VALUE; i++)
    GC_obj_kinds[i].ok_reclaim_list = NULL;
  GC_obj_kinds[PTRFREE].ok_freelist = &GC_aobjfreelist[0];
  GC_obj_kinds[NORMAL].ok_freelist = &GC_objfreelist[0];
  GC_obj_kinds[UNCOLLECTABLE].ok_freelist = &GC_uobjfreelist[0];
#  ifdef GC_ATOMIC_UNCOLLECTABLE
  GC_obj_kinds[AUNCOLLECTABLE].ok_freelist = &GC_auobjfreelist[0];
#  endif
  GC_obj_kinds[NORMAL].ok_descriptor = GC_DS_LENGTH;
  GC_n_kinds = GC_N_KINDS_INITIAL_VALUE;
}
#endif

#ifndef INITIAL_MARK_STACK_SIZE
/*
 * `INITIAL_MARK_STACK_SIZE * sizeof(mse)` should be a multiple of `HBLKSIZE`.
 * The incremental collector actually likes a larger size, since it wants to
 * push all marked dirty objects before marking anything new.
 * Currently we let it grow dynamically.
 */
#  define INITIAL_MARK_STACK_SIZE (1 * HBLKSIZE)
#endif

#if !defined(GC_DISABLE_INCREMENTAL)
/*
 * The number of dirty pages we marked from, excluding pointer-free pages,
 * etc.  Used for logging only.
 */
STATIC word GC_n_rescuing_pages = 0;
#endif

GC_API void GC_CALL
GC_set_pointer_mask(GC_word value)
{
#ifdef DYNAMIC_POINTER_MASK
  GC_ASSERT(value >= 0xff); /*< a simple sanity check */
  GC_pointer_mask = value;
#else
  if (value
#  ifdef POINTER_MASK
      != (word)(POINTER_MASK)
#  else
      != GC_WORD_MAX
#  endif
  ) {
    ABORT("Dynamic pointer mask/shift is unsupported");
  }
#endif
}

GC_API GC_word GC_CALL
GC_get_pointer_mask(void)
{
#ifdef DYNAMIC_POINTER_MASK
  GC_word value = GC_pointer_mask;

  if (0 == value) {
    GC_ASSERT(!GC_is_initialized);
    value = GC_WORD_MAX;
  }
  return value;
#elif defined(POINTER_MASK)
  return POINTER_MASK;
#else
  return GC_WORD_MAX;
#endif
}

GC_API void GC_CALL
GC_set_pointer_shift(unsigned value)
{
#ifdef DYNAMIC_POINTER_MASK
  GC_ASSERT(value < CPP_WORDSZ);
  GC_pointer_shift = (unsigned char)value;
#else
  if (value
#  ifdef POINTER_SHIFT
      != (unsigned)(POINTER_SHIFT)
#  endif
  ) {
    ABORT("Dynamic pointer mask/shift is unsupported");
  }
#endif
}

GC_API unsigned GC_CALL
GC_get_pointer_shift(void)
{
#ifdef DYNAMIC_POINTER_MASK
  return GC_pointer_shift;
#elif defined(POINTER_SHIFT)
  GC_STATIC_ASSERT((unsigned)(POINTER_SHIFT) < CPP_WORDSZ);
  return POINTER_SHIFT;
#else
  return 0;
#endif
}

GC_INNER GC_bool
GC_collection_in_progress(void)
{
  return GC_mark_state != MS_NONE;
}

GC_INNER void
GC_clear_hdr_marks(hdr *hhdr)
{
  size_t last_bit;

#ifdef AO_HAVE_load
  /* Atomic access is used to avoid racing with `GC_realloc`. */
  last_bit = FINAL_MARK_BIT(AO_load((volatile AO_t *)&hhdr->hb_sz));
#else
  /*
   * No race as `GC_realloc` holds the allocator lock while updating
   * `hb_sz` field.
   */
  last_bit = FINAL_MARK_BIT(hhdr->hb_sz);
#endif

  BZERO(CAST_AWAY_VOLATILE_PVOID(hhdr->hb_marks), sizeof(hhdr->hb_marks));
  set_mark_bit_from_hdr(hhdr, last_bit);
  hhdr->hb_n_marks = 0;
}

GC_INNER void
GC_set_hdr_marks(hdr *hhdr)
{
  size_t i;
  size_t sz = hhdr->hb_sz;
  size_t n_marks = FINAL_MARK_BIT(sz);

#ifdef USE_MARK_BYTES
  for (i = 0; i <= n_marks; i += MARK_BIT_OFFSET(sz)) {
    hhdr->hb_marks[i] = 1;
  }
#else
  /*
   * Note that all bits are set even in case of not `MARK_BIT_PER_OBJ`,
   * instead of setting every `n`-th bit where `n` is `MARK_BIT_OFFSET(sz)`.
   *  This is done for a performance reason.
   */
  for (i = 0; i < divWORDSZ(n_marks); ++i) {
    hhdr->hb_marks[i] = GC_WORD_MAX;
  }
  /* Set the remaining bits near the end (plus one bit past the end). */
  hhdr->hb_marks[i] = ((((word)1 << modWORDSZ(n_marks)) - 1) << 1) | 1;
#endif
#ifdef MARK_BIT_PER_OBJ
  hhdr->hb_n_marks = n_marks;
#else
  hhdr->hb_n_marks = HBLK_OBJS(sz);
#endif
}

/* Clear all mark bits associated with block `h`. */
static void GC_CALLBACK
clear_marks_for_block(struct hblk *h, void *dummy)
{
  hdr *hhdr = HDR(h);

  UNUSED_ARG(dummy);
  if (IS_UNCOLLECTABLE(hhdr->hb_obj_kind)) {
    /*
     * Mark bit for these is cleared only once the object is deallocated
     * explicitly.  This either frees the block, or the bit is cleared
     * once the object is on the free list.
     */
    return;
  }
  GC_clear_hdr_marks(hhdr);
#if defined(CPPCHECK)
  GC_noop1_ptr(h);
#endif
}

/* Slow but general routines for setting/clearing/getting mark bits. */

GC_API void GC_CALL
GC_set_mark_bit(const void *p)
{
  struct hblk *h = HBLKPTR(p);
  hdr *hhdr = HDR(h);
  size_t bit_no = MARK_BIT_NO((size_t)((ptr_t)p - (ptr_t)h), hhdr->hb_sz);

  if (!mark_bit_from_hdr(hhdr, bit_no)) {
    set_mark_bit_from_hdr(hhdr, bit_no);
    INCR_MARKS(hhdr);
  }
}

GC_API void GC_CALL
GC_clear_mark_bit(const void *p)
{
  struct hblk *h = HBLKPTR(p);
  hdr *hhdr = HDR(h);
  size_t bit_no = MARK_BIT_NO((size_t)((ptr_t)p - (ptr_t)h), hhdr->hb_sz);

  if (mark_bit_from_hdr(hhdr, bit_no)) {
    size_t n_marks = hhdr->hb_n_marks;

    GC_ASSERT(n_marks != 0);
    clear_mark_bit_from_hdr(hhdr, bit_no);
    n_marks--;
#ifdef PARALLEL_MARK
    /*
     * Do not decrement to zero.  The counts are approximate due to
     * concurrency issues, but we need to ensure that a count of zero
     * implies an empty block.
     */
    if (n_marks != 0 || !GC_parallel)
      hhdr->hb_n_marks = n_marks;
#else
    hhdr->hb_n_marks = n_marks;
#endif
  }
}

GC_API int GC_CALL
GC_is_marked(const void *p)
{
  struct hblk *h = HBLKPTR(p);
  hdr *hhdr = HDR(h);
  size_t bit_no = MARK_BIT_NO((size_t)((ptr_t)p - (ptr_t)h), hhdr->hb_sz);

  return (int)mark_bit_from_hdr(hhdr, bit_no); /*< 0 or 1 */
}

GC_INNER void
GC_clear_marks(void)
{
  /* The initialization is needed for `GC_push_roots()`. */
  GC_ASSERT(GC_is_initialized);

  GC_apply_to_all_blocks(clear_marks_for_block, NULL);
  GC_objects_are_marked = FALSE;
  GC_mark_state = MS_INVALID;
  GC_scan_ptr = NULL;
}

GC_INNER void
GC_initiate_gc(void)
{
  GC_ASSERT(I_HOLD_LOCK());
  GC_ASSERT(GC_is_initialized);
#ifndef GC_DISABLE_INCREMENTAL
  if (GC_incremental) {
#  ifdef CHECKSUMS
    GC_read_dirty(FALSE);
    GC_check_dirty();
#  else
    GC_read_dirty(GC_mark_state == MS_INVALID);
#  endif
  }
  GC_n_rescuing_pages = 0;
#endif
  if (GC_mark_state == MS_NONE) {
    GC_mark_state = MS_PUSH_RESCUERS;
  } else {
    /* This is really a full collection, and mark bits are invalid. */
    GC_ASSERT(GC_mark_state == MS_INVALID);
  }
  GC_scan_ptr = NULL;
}

#ifdef PARALLEL_MARK
/* Initiate parallel marking. */
STATIC void GC_do_parallel_mark(void);
#endif

#ifdef GC_DISABLE_INCREMENTAL
#  define GC_push_next_marked_dirty(h) GC_push_next_marked(h)
#else
STATIC struct hblk *GC_push_next_marked_dirty(struct hblk *h);
#endif /* !GC_DISABLE_INCREMENTAL */

STATIC struct hblk *GC_push_next_marked(struct hblk *h);
STATIC struct hblk *GC_push_next_marked_uncollectable(struct hblk *h);

static void alloc_mark_stack(size_t);

static void
push_roots_and_advance(GC_bool push_all, ptr_t cold_gc_frame)
{
  if (GC_scan_ptr != NULL) {
    /* Not ready to push. */
    return;
  }
  GC_push_roots(push_all, cold_gc_frame);
  GC_objects_are_marked = TRUE;
  if (GC_mark_state != MS_INVALID)
    GC_mark_state = MS_ROOTS_PUSHED;
}

STATIC GC_on_mark_stack_empty_proc GC_on_mark_stack_empty = 0;

GC_API void GC_CALL
GC_set_on_mark_stack_empty(GC_on_mark_stack_empty_proc fn)
{
  LOCK();
  GC_on_mark_stack_empty = fn;
  UNLOCK();
}

GC_API GC_on_mark_stack_empty_proc GC_CALL
GC_get_on_mark_stack_empty(void)
{
  GC_on_mark_stack_empty_proc fn;

  READER_LOCK();
  fn = GC_on_mark_stack_empty;
  READER_UNLOCK();
  return fn;
}

#ifdef WRAP_MARK_SOME
/*
 * For Win32, this is called after we establish a structured exception
 * (or signal) handler, in case Windows unmaps one of our root segments.
 * Note that this code should never generate an incremental GC write fault.
 */
STATIC GC_bool
GC_mark_some_inner(ptr_t cold_gc_frame)
#else
GC_INNER GC_bool
GC_mark_some(ptr_t cold_gc_frame)
#endif
{
  GC_ASSERT(I_HOLD_LOCK());
  switch (GC_mark_state) {
  case MS_NONE:
    return TRUE;

  case MS_PUSH_RESCUERS:
    if (ADDR_GE((ptr_t)GC_mark_stack_top,
                (ptr_t)(GC_mark_stack_limit - INITIAL_MARK_STACK_SIZE / 2))) {
      /*
       * Go ahead and mark, even though that might cause us to see more
       * marked dirty objects later on.  Avoid this in the future.
       */
      GC_mark_stack_too_small = TRUE;
      MARK_FROM_MARK_STACK();
    } else {
      GC_scan_ptr = GC_push_next_marked_dirty(GC_scan_ptr);
#ifndef GC_DISABLE_INCREMENTAL
      if (NULL == GC_scan_ptr) {
        GC_COND_LOG_PRINTF("Marked from %lu dirty pages\n",
                           (unsigned long)GC_n_rescuing_pages);
      }
#endif
      push_roots_and_advance(FALSE, cold_gc_frame);
    }
    GC_ASSERT(GC_mark_state == MS_PUSH_RESCUERS
              || GC_mark_state == MS_ROOTS_PUSHED
              || GC_mark_state == MS_INVALID);
    break;

  case MS_PUSH_UNCOLLECTABLE:
    if (ADDR_GE((ptr_t)GC_mark_stack_top,
                (ptr_t)(GC_mark_stack + GC_mark_stack_size / 4))) {
#ifdef PARALLEL_MARK
      /* Avoid this, since we do not parallelize the marker here. */
      if (GC_parallel)
        GC_mark_stack_too_small = TRUE;
#endif
      MARK_FROM_MARK_STACK();
    } else {
      GC_scan_ptr = GC_push_next_marked_uncollectable(GC_scan_ptr);
      push_roots_and_advance(TRUE, cold_gc_frame);
    }
    GC_ASSERT(GC_mark_state == MS_PUSH_UNCOLLECTABLE
              || GC_mark_state == MS_ROOTS_PUSHED
              || GC_mark_state == MS_INVALID);
    break;

  case MS_ROOTS_PUSHED:
#ifdef PARALLEL_MARK
    /*
     * Eventually, incremental marking should run asynchronously
     * in multiple threads, without acquiring the allocator lock.
     * For now, parallel marker is disabled if there is a chance that
     * marking could be interrupted by a client-supplied time limit
     * or custom stop function.
     */
    if (GC_parallel && !GC_parallel_mark_disabled) {
      GC_do_parallel_mark();
      GC_ASSERT(ADDR_LT((ptr_t)GC_mark_stack_top, GC_first_nonempty));
      GC_mark_stack_top = GC_mark_stack - 1;
      if (GC_mark_stack_too_small) {
        alloc_mark_stack(2 * GC_mark_stack_size);
      }
      if (GC_mark_state == MS_ROOTS_PUSHED) {
        GC_mark_state = MS_NONE;
        return TRUE;
      }
      GC_ASSERT(GC_mark_state == MS_INVALID);
      break;
    }
#endif
    if (ADDR_GE((ptr_t)GC_mark_stack_top, (ptr_t)GC_mark_stack)) {
      MARK_FROM_MARK_STACK();
    } else {
      GC_on_mark_stack_empty_proc on_ms_empty = GC_on_mark_stack_empty;

      if (on_ms_empty != 0) {
        GC_mark_stack_top
            = on_ms_empty(GC_mark_stack_top, GC_mark_stack_limit);
        /* If we pushed new items, we need to continue processing. */
        if (ADDR_GE((ptr_t)GC_mark_stack_top, (ptr_t)GC_mark_stack))
          break;
      }
      if (GC_mark_stack_too_small) {
        alloc_mark_stack(2 * GC_mark_stack_size);
      }
      GC_mark_state = MS_NONE;
      return TRUE;
    }
    GC_ASSERT(GC_mark_state == MS_ROOTS_PUSHED || GC_mark_state == MS_INVALID);
    break;

  case MS_INVALID:
  case MS_PARTIALLY_INVALID:
    if (!GC_objects_are_marked) {
      GC_mark_state = MS_PUSH_UNCOLLECTABLE;
      break;
    }
    if (ADDR_GE((ptr_t)GC_mark_stack_top, (ptr_t)GC_mark_stack)) {
      MARK_FROM_MARK_STACK();
      GC_ASSERT(GC_mark_state == MS_PARTIALLY_INVALID
                || GC_mark_state == MS_INVALID);
      break;
    }
    if (NULL == GC_scan_ptr && GC_mark_state == MS_INVALID) {
      /*
       * About to start a heap scan for marked objects.
       * Mark stack is empty.  OK to reallocate.
       */
      if (GC_mark_stack_too_small) {
        alloc_mark_stack(2 * GC_mark_stack_size);
      }
      GC_mark_state = MS_PARTIALLY_INVALID;
    }
    GC_scan_ptr = GC_push_next_marked(GC_scan_ptr);
    if (GC_mark_state == MS_PARTIALLY_INVALID)
      push_roots_and_advance(TRUE, cold_gc_frame);
    GC_ASSERT(GC_mark_state == MS_ROOTS_PUSHED
              || GC_mark_state == MS_PARTIALLY_INVALID
              || GC_mark_state == MS_INVALID);
    break;

  default:
    ABORT("GC_mark_some: bad state");
  }
  return FALSE;
}

#ifdef PARALLEL_MARK
GC_INNER GC_bool GC_parallel_mark_disabled = FALSE;
#endif

#ifdef WRAP_MARK_SOME
GC_INNER GC_bool
GC_mark_some(ptr_t cold_gc_frame)
{
  GC_bool ret_val;

  if (GC_no_dls) {
    ret_val = GC_mark_some_inner(cold_gc_frame);
  } else {
    /*
     * Windows appears to asynchronously create and remove writable
     * memory mappings, for reasons we have not yet understood.
     * Since we look for writable regions to determine the root set, we
     * may try to mark from an address range that disappeared since we
     * started the collection.  Thus we have to recover from faults here.
     * This code seems to be necessary for WinCE (at least in the case
     * we would decide to add `MEM_PRIVATE` sections to data roots in
     * `GC_register_dynamic_libraries`).  It is conceivable that this is
     * the same issue as with terminating threads that we see with Linux
     * and `USE_PROC_FOR_LIBRARIES`.
     */
#  ifndef NO_SEH_AVAILABLE
    __try {
      ret_val = GC_mark_some_inner(cold_gc_frame);
    } __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION
                    ? EXCEPTION_EXECUTE_HANDLER
                    : EXCEPTION_CONTINUE_SEARCH) {
      goto handle_ex;
    }
#  else
#    if defined(USE_PROC_FOR_LIBRARIES) && !defined(DEFAULT_VDB)
    if (GC_auto_incremental) {
      static GC_bool is_warned = FALSE;

      if (!is_warned) {
        is_warned = TRUE;
        WARN("Incremental GC incompatible with /proc roots\n", 0);
      }
      /* Unclear if this could still work... */
    }
#    endif
    /*
     * If `USE_PROC_FOR_LIBRARIES`, then we are handling the case in
     * which `/proc` is used for root finding, and we have threads.
     * We may find a stack for a thread that is in the process of
     * exiting, and disappears while we are marking it.
     * This seems extremely difficult to avoid otherwise.
     */
    GC_setup_temporary_fault_handler();
    if (SETJMP(GC_jmp_buf) != 0)
      goto handle_ex;
    ret_val = GC_mark_some_inner(cold_gc_frame);
    GC_reset_fault_handler();
#  endif
  }

#  if defined(GC_WIN32_THREADS) && !defined(GC_PTHREADS)
  /*
   * With `DllMain`-based thread tracking, a thread may have started
   * while we were marking.  This is logically equivalent to the
   * exception case; our results are invalid and we have to start over.
   * This cannot be prevented since we cannot block in `DllMain()`.
   */
  if (GC_started_thread_while_stopped())
    goto handle_thr_start;
#  endif
  return ret_val;

handle_ex:
  /* Exception handler starts here for all cases. */
#  if defined(NO_SEH_AVAILABLE)
  GC_reset_fault_handler();
#  endif
  {
    static word warned_gc_no;

    /* Report caught `ACCESS_VIOLATION`, once per collection. */
    if (warned_gc_no != GC_gc_no) {
      GC_COND_LOG_PRINTF("Memory mapping disappeared at collection #%lu\n",
                         (unsigned long)GC_gc_no + 1);
      warned_gc_no = GC_gc_no;
    }
  }
#  if defined(GC_WIN32_THREADS) && !defined(GC_PTHREADS)
handle_thr_start:
#  endif
  /*
   * We have bad roots on the mark stack - discard it.
   * Rescan from the marked objects; redetermine the roots.
   */
#  ifdef REGISTER_LIBRARIES_EARLY
  START_WORLD();
  GC_cond_register_dynamic_libraries();
  STOP_WORLD();
#  endif
  GC_invalidate_mark_state();
  GC_scan_ptr = NULL;
  return FALSE;
}
#endif /* WRAP_MARK_SOME */

GC_INNER void
GC_invalidate_mark_state(void)
{
  GC_mark_state = MS_INVALID;
  GC_mark_stack_top = GC_mark_stack - 1;
}

STATIC mse *
GC_signal_mark_stack_overflow(mse *msp)
{
  GC_mark_state = MS_INVALID;
#ifdef PARALLEL_MARK
  /*
   * We are using a `local_mark_stack` in parallel mode, so do
   * not signal the global mark stack to be resized.
   * That will be done in `GC_return_mark_stack` if required.
   */
  if (!GC_parallel)
    GC_mark_stack_too_small = TRUE;
#else
  GC_mark_stack_too_small = TRUE;
#endif
  GC_COND_LOG_PRINTF("Mark stack overflow; current size: %lu entries\n",
                     (unsigned long)GC_mark_stack_size);
#if defined(CPPCHECK)
  GC_noop1_ptr(msp);
#endif
  return msp - GC_MARK_STACK_DISCARDS;
}

GC_ATTR_NO_SANITIZE_ADDR_MEM_THREAD
GC_INNER mse *
GC_mark_from(mse *mark_stack_top, mse *mark_stack, mse *mark_stack_limit)
{
  GC_signed_word credit = HBLKSIZE; /*< remaining credit for marking work */
  word descr;
  ptr_t current_p;    /*< pointer to the current candidate pointer */
  ptr_t q;            /*< the candidate pointer itself */
  ptr_t limit = NULL; /*< the limit (incl.) of the current candidate range */
  ptr_t greatest_ha = (ptr_t)GC_greatest_plausible_heap_addr;
  ptr_t least_ha = (ptr_t)GC_least_plausible_heap_addr;
  DECLARE_HDR_CACHE;

#define SPLIT_RANGE_PTRS 128 /*< must be power of 2 */

  GC_objects_are_marked = TRUE;
  INIT_HDR_CACHE;
#if defined(OS2) || CPP_PTRSZ > CPP_WORDSZ
  /* OS/2: avoid the tweaked variant to circumvent a compiler problem. */
  while (ADDR_GE((ptr_t)mark_stack_top, (ptr_t)mark_stack) && credit >= 0)
#else
  while (((((word)mark_stack_top - (word)mark_stack) | (word)credit) & SIGNB)
         == 0)
#endif
  {
    current_p = mark_stack_top->mse_start;
    descr = mark_stack_top->mse_descr;
  retry:
    /*
     * `current_p` and `descr` describe the current object.
     * `*mark_stack_top` is vacant.
     * The following is zero only for small objects described by a simple
     * length descriptor.  For many applications this is the common case,
     * so we try to detect it quickly.
     */
    if (descr & (~(word)(PTRS_TO_BYTES(SPLIT_RANGE_PTRS) - 1) | GC_DS_TAGS)) {
      word tag = descr & GC_DS_TAGS;

      GC_STATIC_ASSERT(GC_DS_TAGS == 0x3);
      switch (tag) {
      case GC_DS_LENGTH:
        /*
         * Large length.  Process part of the range to avoid pushing
         * too much on the stack.
         */

        /* Either it is a heap object or a region outside the heap. */
        GC_ASSERT(descr < GC_greatest_real_heap_addr - GC_least_real_heap_addr
                  || GC_least_real_heap_addr + sizeof(ptr_t)
                         >= ADDR(current_p) + descr
                  || ADDR(current_p) >= GC_greatest_real_heap_addr);
#ifdef PARALLEL_MARK
#  define SHARE_BYTES 2048
        if (descr > SHARE_BYTES && GC_parallel
            && ADDR_LT((ptr_t)mark_stack_top, (ptr_t)(mark_stack_limit - 1))) {
          word new_size = (descr >> 1) & ~(word)(sizeof(ptr_t) - 1);

          mark_stack_top->mse_start = current_p;
          /* This makes sure we handle misaligned pointers. */
          mark_stack_top->mse_descr
              = (new_size + sizeof(ptr_t)) | GC_DS_LENGTH;
          mark_stack_top++;
#  ifdef ENABLE_TRACE
          if (ADDR_INSIDE(GC_trace_ptr, current_p, current_p + descr)) {
            GC_log_printf("GC #%lu: large section; start %p, len %lu,"
                          " splitting (parallel) at %p\n",
                          (unsigned long)GC_gc_no, (void *)current_p,
                          (unsigned long)descr,
                          (void *)(current_p + new_size));
          }
#  endif
          current_p += new_size;
          descr -= new_size;
          goto retry;
        }
#endif /* PARALLEL_MARK */
        limit = current_p + PTRS_TO_BYTES(SPLIT_RANGE_PTRS - 1);
        mark_stack_top->mse_start = limit;
        mark_stack_top->mse_descr
            = descr - PTRS_TO_BYTES(SPLIT_RANGE_PTRS - 1);
#ifdef ENABLE_TRACE
        if (ADDR_INSIDE(GC_trace_ptr, current_p, current_p + descr)) {
          GC_log_printf(
              "GC #%lu: large section; start %p, len %lu, splitting at %p\n",
              (unsigned long)GC_gc_no, (void *)current_p, (unsigned long)descr,
              (void *)limit);
        }
#endif
        /*
         * Make sure that pointers overlapping the two ranges are
         * considered.
         */
        limit += sizeof(ptr_t) - ALIGNMENT;
        break;
      case GC_DS_BITMAP:
        mark_stack_top--;
#ifdef ENABLE_TRACE
        if (ADDR_INSIDE(GC_trace_ptr, current_p,
                        current_p + PTRS_TO_BYTES(BITMAP_BITS))) {
          GC_log_printf("GC #%lu: tracing from %p bitmap descr 0x%lx\n",
                        (unsigned long)GC_gc_no, (void *)current_p,
                        (unsigned long)descr);
        }
#endif
        descr &= ~(word)GC_DS_TAGS;
        credit -= (GC_signed_word)PTRS_TO_BYTES(CPP_PTRSZ / 2); /*< guess */
        for (; descr != 0;
             descr <<= 1, current_p += sizeof(ptr_t)) { /*< not `ALIGNMENT` */
          if ((descr & SIGNB) == 0)
            continue;
          LOAD_PTR_OR_CONTINUE(q, current_p);
          FIXUP_POINTER(q);
          if (ADDR_LT(least_ha, q) && ADDR_LT(q, greatest_ha)) {
            PREFETCH(q);
#ifdef ENABLE_TRACE
            if (GC_trace_ptr == current_p) {
              GC_log_printf("GC #%lu: considering(3) %p -> %p\n",
                            (unsigned long)GC_gc_no, (void *)current_p,
                            (void *)q);
            }
#endif
            PUSH_CONTENTS(q, mark_stack_top, mark_stack_limit, current_p);
          }
        }
        continue;
      case GC_DS_PROC:
        mark_stack_top--;
#ifdef ENABLE_TRACE
        if (ADDR_GE(GC_trace_ptr, current_p)) {
          const void *base = GC_base(current_p);

          if (base != NULL && GC_base(GC_trace_ptr) == base) {
            GC_log_printf("GC #%lu: tracing from %p, proc descr 0x%lx\n",
                          (unsigned long)GC_gc_no, (void *)current_p,
                          (unsigned long)descr);
          }
        }
#endif
        credit -= GC_PROC_BYTES;
        mark_stack_top = (*PROC(descr))((word *)current_p, mark_stack_top,
                                        mark_stack_limit, ENV(descr));
        continue;
      case GC_DS_PER_OBJECT:
        if (!(descr & SIGNB)) {
          /* Descriptor is in the object. */
          descr = *(word *)(current_p + descr - GC_DS_PER_OBJECT);
        } else {
          /*
           * Descriptor is in the type descriptor pointed to by the first
           * "pointer-sized" word of the object.
           */
          ptr_t type_descr = *(ptr_t *)current_p;

          /*
           * `type_descr` is either a valid pointer to the descriptor
           * structure, or this object was on a free list.
           * If it was anything but the last object on the free list,
           * we will misinterpret the next object on the free list as
           * the type descriptor, and get a zero GC descriptor, which
           * is ideal.  Unfortunately, we need to check for the last
           * object case explicitly.
           */
          if (UNLIKELY(NULL == type_descr)) {
            mark_stack_top--;
            continue;
          }
          descr = *(word *)(type_descr
                            - ((GC_signed_word)descr
                               + (GC_INDIR_PER_OBJ_BIAS - GC_DS_PER_OBJECT)));
        }
        if (0 == descr) {
          /*
           * Can happen either because we generated a zero GC descriptor
           * or we saw a pointer to a free object.
           */
          mark_stack_top--;
          continue;
        }
        goto retry;
      }
    } else {
      /* Small object with length descriptor. */
      mark_stack_top--;
#ifndef SMALL_CONFIG
      if (descr < sizeof(ptr_t))
        continue;
#endif
#ifdef ENABLE_TRACE
      if (ADDR_INSIDE(GC_trace_ptr, current_p, current_p + descr)) {
        GC_log_printf("GC #%lu: small object; start %p, len %lu\n",
                      (unsigned long)GC_gc_no, (void *)current_p,
                      (unsigned long)descr);
      }
#endif
      limit = current_p + descr;
    }
    /* The simple case in which we are scanning a range. */
    GC_ASSERT((ADDR(current_p) & (ALIGNMENT - 1)) == 0);
    credit -= limit - current_p;
    limit -= sizeof(ptr_t);
    {
#define PREF_DIST 4

#if !defined(SMALL_CONFIG) && !(defined(E2K) && defined(USE_PTR_HWTAG))
      ptr_t deferred;

#  ifdef CHERI_PURECAP
      /*
       * Check each pointer for validity before dereferencing to prevent
       * capability exceptions.  Utilize the pointer meta-data to speed-up
       * the loop.  If the loop is below the pointer bounds, skip the rest
       * of marking for that chunk.  If the limit capability restricts us to
       * reading fewer than size of a pointer, then there cannot possibly be
       * a pointer at `limit`'s pointer, and reading at that location will
       * raise a capability exception.
       */
      {
        word cap_limit = cheri_base_get(limit) + cheri_length_get(limit);

        if (ADDR(limit) + sizeof(ptr_t) > cap_limit) {
          /* Decrement limit so that it to be within bounds of `current_p`. */
          GC_ASSERT(cap_limit > sizeof(ptr_t));
          limit = (ptr_t)cheri_address_set(
              current_p, (cap_limit - sizeof(ptr_t)) & ~(sizeof(ptr_t) - 1));
          goto check_limit;
        }
      }
#  endif
      /*
       * Try to prefetch the next pointer to be examined as soon as possible.
       * Empirically, this also seems to help slightly without prefetches,
       * at least on Linux/i686.  Presumably this loop ends up with less
       * register pressure, and gcc thus ends up generating slightly better
       * code.  Overall gcc code quality for this loop is still not great.
       */
      for (;;) {
        PREFETCH(limit - PREF_DIST * CACHE_LINE_SIZE);
        GC_ASSERT(ADDR_GE(limit, current_p));
#  ifdef CHERI_PURECAP
        if (ADDR(limit) < cheri_base_get(limit))
          goto next_object;
        if (!HAS_TAG_AND_PERM_LOAD(limit)) {
          limit -= ALIGNMENT;
          goto check_limit;
        }
#  endif
        deferred = *(ptr_t *)limit;
        FIXUP_POINTER(deferred);
        limit -= ALIGNMENT;
#  ifdef CHERI_PURECAP
        if (!HAS_TAG_AND_PERM_LOAD(deferred))
          goto check_limit;
#  endif
        if (ADDR_LT(least_ha, deferred) && ADDR_LT(deferred, greatest_ha)) {
          PREFETCH(deferred);
          break;
        }
#  ifndef CHERI_PURECAP
        if (ADDR_LT(limit, current_p))
          goto next_object;
        /*
         * Unroll once, so we do not do too many of the prefetches based
         * on `limit`.
         */
        deferred = *(ptr_t *)limit;
        FIXUP_POINTER(deferred);
        limit -= ALIGNMENT;
        if (ADDR_LT(least_ha, deferred) && ADDR_LT(deferred, greatest_ha)) {
          PREFETCH(deferred);
          break;
        }
#  else
      check_limit:
#  endif
        if (ADDR_LT(limit, current_p))
          goto next_object;
      }
#endif

      for (; ADDR_GE(limit, current_p); current_p += ALIGNMENT) {
        /*
         * Empirically, unrolling this loop does not help a lot.
         * Since `PUSH_CONTENTS` expands to a lot of code, we do not.
         */
        LOAD_PTR_OR_CONTINUE(q, current_p);
        FIXUP_POINTER(q);
        PREFETCH(current_p + PREF_DIST * CACHE_LINE_SIZE);
        if (ADDR_LT(least_ha, q) && ADDR_LT(q, greatest_ha)) {
          /*
           * Prefetch the contents of the object we just pushed.
           * It is likely we will need them soon.
           */
          PREFETCH(q);
#ifdef ENABLE_TRACE
          if (GC_trace_ptr == current_p) {
            GC_log_printf("GC #%lu: considering(1) %p -> %p\n",
                          (unsigned long)GC_gc_no, (void *)current_p,
                          (void *)q);
          }
#endif
          PUSH_CONTENTS(q, mark_stack_top, mark_stack_limit, current_p);
        }
      }

#if !defined(SMALL_CONFIG) && !(defined(E2K) && defined(USE_PTR_HWTAG))
      /*
       * We still need to mark the entry we previously prefetched.
       * We already know that it passes the preliminary pointer validity test.
       */
#  ifdef ENABLE_TRACE
      if (GC_trace_ptr == current_p) {
        GC_log_printf("GC #%lu: considering(2) %p -> %p\n",
                      (unsigned long)GC_gc_no, (void *)current_p,
                      (void *)deferred);
      }
#  endif
      PUSH_CONTENTS(deferred, mark_stack_top, mark_stack_limit, current_p);
    next_object:;
#endif
    }
  }
  return mark_stack_top;
}

#ifdef PARALLEL_MARK

/* Note: this is protected by the mark lock. */
STATIC GC_bool GC_help_wanted = FALSE;

/* Number of running helpers.  Protected by the mark lock. */
STATIC unsigned GC_helper_count = 0;

/*
 * Number of active helpers.  May increase and decrease within each
 * mark cycle; but once it returns to zero, it stays for the cycle.
 * Protected by the mark lock.
 */
STATIC unsigned GC_active_count = 0;

GC_INNER GC_signed_word GC_fl_builder_count = 0;

#  ifdef LINT2
#    define LOCAL_MARK_STACK_SIZE (HBLKSIZE / 8)
#  else
/*
 * Under normal circumstances, this is big enough to guarantee we do not
 * overflow half of it in a single call to `GC_mark_from`.
 */
#    define LOCAL_MARK_STACK_SIZE HBLKSIZE
#  endif

GC_INNER void
GC_wait_for_markers_init(void)
{
  GC_signed_word count;

  GC_ASSERT(I_HOLD_LOCK());
  if (0 == GC_markers_m1)
    return;

#  ifndef CAN_HANDLE_FORK
  GC_ASSERT(NULL == GC_main_local_mark_stack);
#  else
  if (NULL == GC_main_local_mark_stack)
#  endif
  {
    size_t bytes_to_get
        = ROUNDUP_PAGESIZE_IF_MMAP(LOCAL_MARK_STACK_SIZE * sizeof(mse));

    /*
     * Allocate the local mark stack for the thread that holds the
     * allocator lock.
     */
    GC_ASSERT(GC_page_size != 0);
    GC_main_local_mark_stack = (mse *)GC_os_get_mem(bytes_to_get);
    if (NULL == GC_main_local_mark_stack)
      ABORT("Insufficient memory for main local_mark_stack");
  }

  /*
   * Reuse the mark lock and builders count to synchronize marker threads
   * startup.
   */
  GC_acquire_mark_lock();
  GC_fl_builder_count += GC_markers_m1;
  count = GC_fl_builder_count;
  GC_release_mark_lock();
  if (count != 0) {
    GC_ASSERT(count > 0);
    GC_wait_for_reclaim();
  }
}

/*
 * Steal mark stack entries starting at `mse` `low` into mark stack `local`
 * until we either steal `mse` `high`, or we have `n_to_get` entries.
 * Return a pointer to the top of the local mark stack.  `*next` is replaced
 * by a pointer to the next unscanned mark stack entry.
 */
STATIC mse *
GC_steal_mark_stack(mse *low, mse *high, mse *local, size_t n_to_get,
                    mse **next)
{
  mse *p;
  mse *top = local - 1;
  size_t i = 0;

  GC_ASSERT(ADDR_GE((ptr_t)high, (ptr_t)(low - 1))
            && (word)(high - low + 1) <= GC_mark_stack_size);
  for (p = low; ADDR_GE((ptr_t)high, (ptr_t)p) && i <= n_to_get; ++p) {
    word descr = AO_load(&p->mse_descr);

    if (descr != 0) {
      /* Must be ordered after read of `mse_descr`. */
      AO_store_release_write(&p->mse_descr, 0);
      /*
       * More than one thread may get this entry, but that is only
       * a minor performance problem.
       */
      ++top;
      top->mse_start = p->mse_start;
      top->mse_descr = descr;
      GC_ASSERT((descr & GC_DS_TAGS) != GC_DS_LENGTH /* 0 */
                || descr < GC_greatest_real_heap_addr - GC_least_real_heap_addr
                || GC_least_real_heap_addr + sizeof(ptr_t)
                       >= ADDR(p->mse_start) + descr
                || ADDR(p->mse_start) >= GC_greatest_real_heap_addr);
      /* If this is a big object, count it as `descr / 256 + 1` objects. */
      ++i;
      if ((descr & GC_DS_TAGS) == GC_DS_LENGTH)
        i += (size_t)(descr >> 8);
    }
  }
  *next = p;
#  if defined(CPPCHECK)
  GC_noop1_ptr(local);
#  endif
  return top;
}

/* Copy back a local mark stack.  `low` and `high` are inclusive bounds. */
STATIC void
GC_return_mark_stack(mse *low, mse *high)
{
  mse *my_top;
  mse *my_start;
  size_t stack_size;

  if (ADDR_LT((ptr_t)high, (ptr_t)low))
    return;
  stack_size = high - low + 1;
  GC_acquire_mark_lock();
  /* Note: the concurrent modification is impossible. */
  my_top = GC_mark_stack_top;
  my_start = my_top + 1;
  if ((word)(my_start - GC_mark_stack + stack_size)
      > (word)GC_mark_stack_size) {
    GC_COND_LOG_PRINTF("No room to copy back mark stack\n");
    GC_mark_state = MS_INVALID;
    GC_mark_stack_too_small = TRUE;
    /* We drop the local mark stack.  We will fix things later. */
  } else {
    BCOPY(low, my_start, stack_size * sizeof(mse));
    GC_ASSERT((mse *)GC_cptr_load((volatile ptr_t *)&GC_mark_stack_top)
              == my_top);
    /* Ensures visibility of previously written stack contents. */
    GC_cptr_store_release_write((volatile ptr_t *)&GC_mark_stack_top,
                                (ptr_t)(my_top + stack_size));
  }
  GC_release_mark_lock();
  GC_notify_all_marker();
}

#  ifndef N_LOCAL_ITERS
#    define N_LOCAL_ITERS 1
#  endif

/*
 * Note: called only when the local and the main mark stacks are both
 * empty.
 */
static GC_bool
has_inactive_helpers(void)
{
  GC_bool res;

  GC_acquire_mark_lock();
  res = GC_active_count < GC_helper_count;
  GC_release_mark_lock();
  return res;
}

/*
 * Mark from the local mark stack.  On return, the local mark stack
 * is empty.  But this may be achieved by copying the local mark stack
 * back into the global one.  We do not hold the mark lock.
 */
STATIC void
GC_do_local_mark(mse *local_mark_stack, mse *local_top)
{
  unsigned n;

  for (;;) {
    for (n = 0; n < N_LOCAL_ITERS; ++n) {
      local_top = GC_mark_from(local_top, local_mark_stack,
                               local_mark_stack + LOCAL_MARK_STACK_SIZE);
      if (ADDR_LT((ptr_t)local_top, (ptr_t)local_mark_stack))
        return;
      if ((word)(local_top - local_mark_stack) >= LOCAL_MARK_STACK_SIZE / 2) {
        GC_return_mark_stack(local_mark_stack, local_top);
        return;
      }
    }
    if (ADDR_LT(GC_cptr_load((volatile ptr_t *)&GC_mark_stack_top),
                GC_cptr_load(&GC_first_nonempty))
        && ADDR_LT((ptr_t)(local_mark_stack + 1), (ptr_t)local_top)
        && has_inactive_helpers()) {
      /*
       * Try to share the load, since the main stack is empty, and the helper
       * threads are waiting for a refill.  The entries near the bottom of
       * the stack are likely to require more work.  Thus we return those,
       * even though it is harder.
       */
      mse *new_bottom = local_mark_stack + (local_top - local_mark_stack) / 2;

      GC_ASSERT(ADDR_LT((ptr_t)local_mark_stack, (ptr_t)new_bottom)
                && ADDR_LT((ptr_t)new_bottom, (ptr_t)local_top));
      GC_return_mark_stack(local_mark_stack, new_bottom - 1);
      memmove(local_mark_stack, new_bottom,
              (local_top - new_bottom + 1) * sizeof(mse));
      local_top -= new_bottom - local_mark_stack;
    }
  }
}

#  ifndef ENTRIES_TO_GET
#    define ENTRIES_TO_GET 5
#  endif

/*
 * Mark using the local mark stack until the global mark stack is empty and
 * there are no active workers.  Update `GC_first_nonempty` to reflect the
 * progress.  Caller holds the mark lock.  Caller has already incremented
 * `GC_helper_count`; we decrement it, and maintain `GC_active_count`.
 */
STATIC void
GC_mark_local(mse *local_mark_stack, int id)
{
  mse *my_first_nonempty;

  GC_active_count++;
  my_first_nonempty = (mse *)GC_cptr_load(&GC_first_nonempty);
  GC_ASSERT(ADDR_GE((ptr_t)my_first_nonempty, (ptr_t)GC_mark_stack));
  GC_ASSERT(
      ADDR_GE(GC_cptr_load((volatile ptr_t *)&GC_mark_stack_top) + sizeof(mse),
              (ptr_t)my_first_nonempty));
  GC_VERBOSE_LOG_PRINTF("Starting mark helper %d\n", id);
  GC_release_mark_lock();
  for (;;) {
    size_t n_on_stack, n_to_get;
    mse *my_top, *local_top;
    mse *global_first_nonempty = (mse *)GC_cptr_load(&GC_first_nonempty);

    GC_ASSERT(ADDR_GE((ptr_t)my_first_nonempty, (ptr_t)GC_mark_stack)
              && ADDR_GE(GC_cptr_load((volatile ptr_t *)&GC_mark_stack_top)
                             + sizeof(mse),
                         (ptr_t)my_first_nonempty));
    GC_ASSERT(ADDR_GE((ptr_t)global_first_nonempty, (ptr_t)GC_mark_stack));
    if (ADDR_LT((ptr_t)my_first_nonempty, (ptr_t)global_first_nonempty)) {
      my_first_nonempty = global_first_nonempty;
    } else if (ADDR_LT((ptr_t)global_first_nonempty,
                       (ptr_t)my_first_nonempty)) {
      (void)GC_cptr_compare_and_swap(&GC_first_nonempty,
                                     (ptr_t)global_first_nonempty,
                                     (ptr_t)my_first_nonempty);
      /*
       * If this fails, then we just go ahead, without updating
       * `GC_first_nonempty`.
       */
    }
    /*
     * Perhaps we should also update `GC_first_nonempty`, if it is less.
     * But that would require usage of the atomic updates.
     */
    my_top = (mse *)GC_cptr_load_acquire((volatile ptr_t *)&GC_mark_stack_top);
    if (ADDR_LT((ptr_t)my_top, (ptr_t)my_first_nonempty)) {
      GC_acquire_mark_lock();
      /*
       * Note: asynchronous modification is impossible here, since
       * we hold the mark lock.
       */
      my_top = GC_mark_stack_top;
      n_on_stack = my_top - my_first_nonempty + 1;
      if (0 == n_on_stack) {
        GC_active_count--;
        GC_ASSERT(GC_active_count <= GC_helper_count);
        /* Other markers may redeposit objects on the stack. */
        if (0 == GC_active_count)
          GC_notify_all_marker();
        while (GC_active_count > 0
               && ADDR_LT((ptr_t)GC_mark_stack_top,
                          GC_cptr_load(&GC_first_nonempty))) {
          /*
           * We will be notified if either `GC_active_count` reaches zero,
           * or if more objects are pushed on the global mark stack.
           */
          GC_wait_marker();
        }
        if (0 == GC_active_count
            && ADDR_LT((ptr_t)GC_mark_stack_top,
                       GC_cptr_load(&GC_first_nonempty))) {
          GC_bool need_to_notify = FALSE;

          /*
           * The above conditions cannot be falsified while we hold
           * the mark lock, since neither `GC_active_count` nor
           * `GC_mark_stack_top` can change.  `GC_first_nonempty` can
           * only be incremented asynchronously.  Thus we know that
           * both conditions are actually held simultaneously.
           */
          GC_helper_count--;
          if (0 == GC_helper_count)
            need_to_notify = TRUE;
          GC_VERBOSE_LOG_PRINTF("Finished mark helper %d\n", id);
          if (need_to_notify)
            GC_notify_all_marker();
          return;
        }
        /*
         * Else there is something on the stack again, or another helper
         * may push something.
         */
        GC_active_count++;
        GC_ASSERT(GC_active_count > 0);
        GC_release_mark_lock();
        continue;
      } else {
        GC_release_mark_lock();
      }
    } else {
      n_on_stack = my_top - my_first_nonempty + 1;
    }
    n_to_get = ENTRIES_TO_GET;
    if (n_on_stack < 2 * ENTRIES_TO_GET)
      n_to_get = 1;
    local_top
        = GC_steal_mark_stack(my_first_nonempty, my_top, local_mark_stack,
                              n_to_get, &my_first_nonempty);
    GC_ASSERT(ADDR_GE((ptr_t)my_first_nonempty, (ptr_t)GC_mark_stack)
              && ADDR_GE(GC_cptr_load((volatile ptr_t *)&GC_mark_stack_top)
                             + sizeof(mse),
                         (ptr_t)my_first_nonempty));
    GC_do_local_mark(local_mark_stack, local_top);
  }
}

/*
 * Perform parallel mark.  We hold the allocator lock, but not the mark lock.
 * Currently runs until the mark stack is empty.
 */
STATIC void
GC_do_parallel_mark(void)
{
  GC_ASSERT(I_HOLD_LOCK());
  GC_acquire_mark_lock();
  GC_ASSERT(!GC_help_wanted);
  GC_ASSERT(0 == GC_active_count && 0 == GC_helper_count);
  GC_VERBOSE_LOG_PRINTF("Starting marking for mark phase number %lu\n",
                        (unsigned long)GC_mark_no);

  GC_cptr_store(&GC_first_nonempty, (ptr_t)GC_mark_stack);
  GC_active_count = 0;
  GC_helper_count = 1;
  GC_help_wanted = TRUE;
  /* Wake up potential helpers. */
  GC_notify_all_marker();
  GC_mark_local(GC_main_local_mark_stack, 0);
  GC_help_wanted = FALSE;
  /* Done; clean up. */
  while (GC_helper_count > 0) {
    GC_wait_marker();
  }
  /* `GC_helper_count` cannot be incremented while not `GC_help_wanted`. */
  GC_VERBOSE_LOG_PRINTF("Finished marking for mark phase number %lu\n",
                        (unsigned long)GC_mark_no);
  GC_mark_no++;
  GC_release_mark_lock();
  GC_notify_all_marker();
}

GC_INNER void
GC_help_marker(word my_mark_no)
{
#  define my_id my_id_mse.mse_descr
  /*
   * Put `my_id` inside the structure to keep `local_mark_stack` aligned
   * explicitly.
   */
  mse my_id_mse;
  mse local_mark_stack[LOCAL_MARK_STACK_SIZE];
  /* Note: `local_mark_stack` is quite big (up to 128 KiB). */

  GC_ASSERT(I_DONT_HOLD_LOCK());
  GC_ASSERT(GC_parallel);
  while (GC_mark_no < my_mark_no
         || (!GC_help_wanted && GC_mark_no == my_mark_no)) {
    GC_wait_marker();
  }
  my_id = GC_helper_count;
  if (GC_mark_no != my_mark_no || my_id > (unsigned)GC_markers_m1) {
    /*
     * The second test is useful only if the original threads can also
     * act as helpers.  Under Linux they cannot.
     */
    return;
  }
  GC_helper_count = (unsigned)my_id + 1;
  GC_mark_local(local_mark_stack, (int)my_id);
  /* `GC_mark_local` decrements `GC_helper_count`. */
#  undef my_id
}

#endif /* PARALLEL_MARK */

/*
 * Allocate or reallocate space for mark stack of size `n` entries.
 * May silently fail.
 */
static void
alloc_mark_stack(size_t n)
{
  mse *new_stack;
#ifdef GWW_VDB
  GC_bool recycle_old;
#endif

  GC_ASSERT(I_HOLD_LOCK());
  new_stack = (mse *)GC_scratch_alloc(n * sizeof(struct GC_ms_entry));
#ifdef GWW_VDB
  /*
   * Do not recycle a stack segment obtained with the wrong flags.
   * Win32 `GetWriteWatch` requires the right kind of memory.
   */
  recycle_old = !GC_auto_incremental || GC_incremental_at_stack_alloc;
  GC_incremental_at_stack_alloc = GC_auto_incremental;
#endif

  GC_mark_stack_too_small = FALSE;
  if (GC_mark_stack != NULL) {
    if (new_stack != NULL) {
#ifdef GWW_VDB
      if (recycle_old)
#endif
      {
        /* Recycle old space. */
        GC_scratch_recycle_inner(
            GC_mark_stack, GC_mark_stack_size * sizeof(struct GC_ms_entry));
      }
      GC_mark_stack = new_stack;
      GC_mark_stack_size = n;
      /* FIXME: Do we need some way to reset `GC_mark_stack_size`? */
      GC_mark_stack_limit = new_stack + n;
      GC_COND_LOG_PRINTF("Grew mark stack to %lu frames\n",
                         (unsigned long)GC_mark_stack_size);
    } else {
      WARN("Failed to grow mark stack to %" WARN_PRIuPTR " frames\n", n);
    }
  } else if (NULL == new_stack) {
    GC_err_printf("No space for mark stack\n");
    EXIT();
  } else {
    GC_mark_stack = new_stack;
    GC_mark_stack_size = n;
    GC_mark_stack_limit = new_stack + n;
  }
  GC_mark_stack_top = GC_mark_stack - 1;
}

GC_INNER void
GC_mark_init(void)
{
  alloc_mark_stack(INITIAL_MARK_STACK_SIZE);
}

GC_API void GC_CALL
GC_push_all(void *bottom, void *top)
{
  mse *mark_stack_top;
  word length;

  bottom = PTR_ALIGN_UP((ptr_t)bottom, ALIGNMENT);
  top = PTR_ALIGN_DOWN((ptr_t)top, ALIGNMENT);
  if (ADDR_GE((ptr_t)bottom, (ptr_t)top))
    return;

  mark_stack_top = GC_mark_stack_top + 1;
  if (ADDR_GE((ptr_t)mark_stack_top, (ptr_t)GC_mark_stack_limit)) {
    mark_stack_top = GC_signal_mark_stack_overflow(mark_stack_top);
  }
  length = (word)((ptr_t)top - (ptr_t)bottom);
#if GC_DS_TAGS > ALIGNMENT - 1
  length = (length + GC_DS_TAGS) & ~(word)GC_DS_TAGS; /*< round up */
#endif
  mark_stack_top->mse_start = (ptr_t)bottom;
  mark_stack_top->mse_descr = length | GC_DS_LENGTH;
  GC_mark_stack_top = mark_stack_top;
}

GC_API struct GC_ms_entry *GC_CALL
GC_custom_push_range(void *bottom, void *top,
                     struct GC_ms_entry *mark_stack_top,
                     struct GC_ms_entry *mark_stack_limit)
{
  word length;

  bottom = PTR_ALIGN_UP((ptr_t)bottom, ALIGNMENT);
  top = PTR_ALIGN_DOWN((ptr_t)top, ALIGNMENT);
  if (ADDR_GE((ptr_t)bottom, (ptr_t)top))
    return mark_stack_top;

  length = (word)((ptr_t)top - (ptr_t)bottom);
#if GC_DS_TAGS > ALIGNMENT - 1
  length = (length + GC_DS_TAGS) & ~(word)GC_DS_TAGS; /*< round up */
#endif
  return GC_custom_push_proc(length | GC_DS_LENGTH, bottom, mark_stack_top,
                             mark_stack_limit);
}

GC_API struct GC_ms_entry *GC_CALL
GC_custom_push_proc(GC_word descr, void *obj,
                    struct GC_ms_entry *mark_stack_top,
                    struct GC_ms_entry *mark_stack_limit)
{
  mark_stack_top++;
  if (ADDR_GE((ptr_t)mark_stack_top, (ptr_t)mark_stack_limit)) {
    mark_stack_top = GC_signal_mark_stack_overflow(mark_stack_top);
  }
  mark_stack_top->mse_start = (ptr_t)obj;
  mark_stack_top->mse_descr = descr;
  return mark_stack_top;
}

GC_API void GC_CALL
GC_push_proc(GC_word descr, void *obj)
{
  GC_mark_stack_top = GC_custom_push_proc(descr, obj, GC_mark_stack_top,
                                          GC_mark_stack_limit);
}

#ifndef GC_DISABLE_INCREMENTAL

/*
 * Analogous to `GC_push_all`, but push only those pages `h` with
 * `dirty_fn(h) != 0`.  We use `GC_push_all` to actually push the block.
 * Used both to selectively push dirty pages, or to push a block in
 * a piecemeal fashion, to allow for more marking concurrency.
 * Will not overflow mark stack if `GC_push_all` pushes a small fixed
 * number of entries.  (This is invoked only if `GC_push_all` pushes
 * a single entry, or if it marks each object before pushing it, thus
 * ensuring progress in the event of a stack overflow.)
 */
STATIC void
GC_push_selected(ptr_t bottom, ptr_t top, GC_bool (*dirty_fn)(struct hblk *))
{
  struct hblk *h;

  bottom = PTR_ALIGN_UP(bottom, ALIGNMENT);
  top = PTR_ALIGN_DOWN(top, ALIGNMENT);
  if (ADDR_GE(bottom, top))
    return;

  h = HBLKPTR(bottom + HBLKSIZE);
  if (ADDR_GE((ptr_t)h, top)) {
    if ((*dirty_fn)(h - 1)) {
      GC_push_all(bottom, top);
    }
    return;
  }
  if ((*dirty_fn)(h - 1)) {
    if ((word)(GC_mark_stack_top - GC_mark_stack)
        > 3 * GC_mark_stack_size / 4) {
      GC_push_all(bottom, top);
      return;
    }
    GC_push_all(bottom, h);
  }

  while (ADDR_GE(top, (ptr_t)(h + 1))) {
    if ((*dirty_fn)(h)) {
      if ((word)(GC_mark_stack_top - GC_mark_stack)
          > 3 * GC_mark_stack_size / 4) {
        /* Danger of mark stack overflow. */
        GC_push_all(h, top);
        return;
      } else {
        GC_push_all(h, h + 1);
      }
    }
    h++;
  }

  if ((ptr_t)h != top && (*dirty_fn)(h)) {
    GC_push_all(h, top);
  }
}

GC_API void GC_CALL
GC_push_conditional(void *bottom, void *top, int all)
{
  if (!all) {
    GC_push_selected((ptr_t)bottom, (ptr_t)top, GC_page_was_dirty);
  } else {
#  ifdef PROC_VDB
    if (GC_auto_incremental) {
      /* Pages that were never dirtied cannot contain pointers. */
      GC_push_selected((ptr_t)bottom, (ptr_t)top, GC_page_was_ever_dirty);
    } else
#  endif
    /* else */ {
      GC_push_all(bottom, top);
    }
  }
}

#  ifndef NO_VDB_FOR_STATIC_ROOTS
#    ifndef PROC_VDB
/*
 * Same as `GC_page_was_dirty` but `h` is allowed to point to some page
 * in the registered static roots only.  Not used if the manual VDB is on.
 */
STATIC GC_bool
GC_static_page_was_dirty(struct hblk *h)
{
  return get_pht_entry_from_index(GC_grungy_pages, PHT_HASH(h));
}
#    endif

GC_INNER void
GC_push_conditional_static(void *bottom, void *top, GC_bool all)
{
#    ifdef PROC_VDB
  /*
   * Just redirect to the generic routine because `PROC_VDB`
   * implementation gets the dirty bits map for the whole process memory.
   */
  GC_push_conditional(bottom, top, all);
#    else
  if (all || !GC_is_vdb_for_static_roots()) {
    GC_push_all(bottom, top);
  } else {
    GC_push_selected((ptr_t)bottom, (ptr_t)top, GC_static_page_was_dirty);
  }
#    endif
}
#  endif /* !NO_VDB_FOR_STATIC_ROOTS */

#else
GC_API void GC_CALL
GC_push_conditional(void *bottom, void *top, int all)
{
  UNUSED_ARG(all);
  GC_push_all(bottom, top);
}
#endif /* GC_DISABLE_INCREMENTAL */

#if defined(DARWIN) && defined(THREADS)
void
GC_push_one(word p)
{
  GC_PUSH_ONE_STACK((ptr_t)p, MARKED_FROM_REGISTER);
}
#endif /* DARWIN && THREADS */

#if defined(GC_WIN32_THREADS)
GC_INNER void
GC_push_many_regs(const word *regs, unsigned count)
{
  unsigned i;

  for (i = 0; i < count; i++)
    GC_PUSH_ONE_STACK((ptr_t)regs[i], MARKED_FROM_REGISTER);
}
#endif /* GC_WIN32_THREADS */

GC_API struct GC_ms_entry *GC_CALL
GC_mark_and_push(void *obj, mse *mark_stack_top, mse *mark_stack_limit,
                 void **src)
{
  hdr *hhdr;

  PREFETCH(obj);
  GET_HDR(obj, hhdr);
  if ((UNLIKELY(IS_FORWARDING_ADDR_OR_NIL(hhdr))
       && (!GC_all_interior_pointers
           || NULL == (hhdr = GC_find_header(GC_base(obj)))))
      || UNLIKELY(HBLK_IS_FREE(hhdr))) {
    GC_ADD_TO_BLACK_LIST_NORMAL((ptr_t)obj, (ptr_t)src);
    return mark_stack_top;
  }
  return GC_push_contents_hdr((ptr_t)obj, mark_stack_top, mark_stack_limit,
                              (ptr_t)src, hhdr, TRUE);
}

GC_ATTR_NO_SANITIZE_ADDR
GC_INNER void
#if defined(PRINT_BLACK_LIST) || defined(KEEP_BACK_PTRS)
GC_mark_and_push_stack(ptr_t p, ptr_t source)
#else
GC_mark_and_push_stack(ptr_t p)
#  define source ((ptr_t)NULL)
#endif
{
  hdr *hhdr;
  ptr_t r = p;

  PREFETCH(p);
  GET_HDR(p, hhdr);
  if (UNLIKELY(IS_FORWARDING_ADDR_OR_NIL(hhdr))) {
    if (NULL == hhdr || (r = (ptr_t)GC_base(p)) == NULL
        || (hhdr = HDR(r)) == NULL) {
      GC_ADD_TO_BLACK_LIST_STACK(p, source);
      return;
    }
  }
  if (UNLIKELY(HBLK_IS_FREE(hhdr))) {
    GC_ADD_TO_BLACK_LIST_NORMAL(p, source);
    return;
  }
#ifdef THREADS
  /*
   * Pointer is on the stack.  We may have dirtied the object it points to,
   * but have not called `GC_dirty` yet.
   */
  GC_dirty(p); /*< entire object */
#endif
  GC_mark_stack_top = GC_push_contents_hdr(
      r, GC_mark_stack_top, GC_mark_stack_limit, source, hhdr, FALSE);
  /*
   * We silently ignore pointers to near the end of a block, which is
   * very mildly suboptimal.
   */
  /* FIXME: We should probably add a header word to address this. */
#undef source
}

#ifdef TRACE_BUF
#  ifndef TRACE_ENTRIES
#    define TRACE_ENTRIES 1000
#  endif

struct trace_entry {
  const char *caller_fn_name;
  word gc_no;
  word bytes_allocd;
  GC_hidden_pointer arg1;
  GC_hidden_pointer arg2;
} GC_trace_buf[TRACE_ENTRIES] = { { (const char *)NULL, 0, 0, 0, 0 } };

void
GC_add_trace_entry(const char *caller_fn_name, ptr_t arg1, ptr_t arg2)
{
  size_t i = GC_trace_buf_pos;

  GC_trace_buf[i].caller_fn_name = caller_fn_name;
  GC_trace_buf[i].gc_no = GC_gc_no;
  GC_trace_buf[i].bytes_allocd = GC_bytes_allocd;
  GC_trace_buf[i].arg1 = GC_HIDE_POINTER(arg1);
  GC_trace_buf[i].arg2 = GC_HIDE_POINTER(arg2);
  i++;
  if (i >= TRACE_ENTRIES)
    i = 0;
  GC_trace_buf_pos = i;
}

GC_API void GC_CALL
GC_print_trace_inner(GC_word gc_no)
{
  size_t i;

  for (i = GC_trace_buf_pos;; i--) {
    struct trace_entry *p;

    if (0 == i)
      i = TRACE_ENTRIES;
    p = &GC_trace_buf[i - 1];
    /*
     * Compare `gc_no` values (`p->gc_no` is less than given `gc_no`)
     * taking into account that the counter may overflow.
     */
    if (((p->gc_no - gc_no) & SIGNB) != 0 || NULL == p->caller_fn_name) {
      return;
    }
    GC_printf("Trace:%s (gc:%lu, bytes:%lu) %p, %p\n", p->caller_fn_name,
              (unsigned long)p->gc_no, (unsigned long)p->bytes_allocd,
              GC_REVEAL_POINTER(p->arg1), GC_REVEAL_POINTER(p->arg2));
    if (i == GC_trace_buf_pos + 1)
      break;
  }
  GC_printf("Trace incomplete\n");
}

GC_API void GC_CALL
GC_print_trace(GC_word gc_no)
{
  READER_LOCK();
  GC_print_trace_inner(gc_no);
  READER_UNLOCK();
}
#endif /* TRACE_BUF */

GC_ATTR_NO_SANITIZE_ADDR_MEM_THREAD
GC_API void GC_CALL
GC_push_all_eager(void *bottom, void *top)
{
  REGISTER ptr_t current_p;
  REGISTER word lim_addr;
  REGISTER ptr_t greatest_ha = (ptr_t)GC_greatest_plausible_heap_addr;
  REGISTER ptr_t least_ha = (ptr_t)GC_least_plausible_heap_addr;
#define GC_greatest_plausible_heap_addr greatest_ha
#define GC_least_plausible_heap_addr least_ha

  if (NULL == top)
    return;
  /* Check all pointers in range and push if they appear to be valid. */
  current_p = PTR_ALIGN_UP((ptr_t)bottom, ALIGNMENT);
  lim_addr = ADDR(PTR_ALIGN_DOWN((ptr_t)top, ALIGNMENT)) - sizeof(ptr_t);
#ifdef CHERI_PURECAP
  {
    word cap_limit = cheri_base_get(current_p) + cheri_length_get(current_p);

    if (lim_addr >= cap_limit)
      lim_addr = cap_limit - sizeof(ptr_t);
  }
#endif
  for (; ADDR(current_p) <= lim_addr; current_p += ALIGNMENT) {
    REGISTER ptr_t q;

    LOAD_PTR_OR_CONTINUE(q, current_p);
    GC_PUSH_ONE_STACK(q, current_p);
  }
#undef GC_greatest_plausible_heap_addr
#undef GC_least_plausible_heap_addr
}

#if !defined(NEED_FIXUP_POINTER) && !defined(NO_ALL_INTERIOR_POINTERS)
GC_INNER void
GC_push_all_stack(void *bottom, void *top)
{
  GC_ASSERT(I_HOLD_LOCK());
  if (GC_all_interior_pointers
#  if defined(THREADS) && defined(MPROTECT_VDB)
      && !GC_auto_incremental
#  endif
      && ADDR_LT((ptr_t)GC_mark_stack_top,
                 (ptr_t)(GC_mark_stack_limit - INITIAL_MARK_STACK_SIZE / 8))) {
    GC_push_all(bottom, top);
  } else {
    GC_push_all_eager(bottom, top);
  }
}
#endif

#if defined(WRAP_MARK_SOME) && defined(PARALLEL_MARK)
GC_ATTR_NO_SANITIZE_ADDR_MEM_THREAD
GC_INNER void
GC_push_conditional_eager(void *bottom, void *top, GC_bool all)
{
  REGISTER ptr_t current_p;
  REGISTER ptr_t lim;
  REGISTER ptr_t greatest_ha = (ptr_t)GC_greatest_plausible_heap_addr;
  REGISTER ptr_t least_ha = (ptr_t)GC_least_plausible_heap_addr;
#  define GC_greatest_plausible_heap_addr greatest_ha
#  define GC_least_plausible_heap_addr least_ha

  if (NULL == top)
    return;

  /* TODO: If not `all`, then scan only dirty pages. */
  (void)all;

  current_p = PTR_ALIGN_UP((ptr_t)bottom, ALIGNMENT);
  lim = PTR_ALIGN_DOWN((ptr_t)top, ALIGNMENT) - sizeof(ptr_t);
  for (; ADDR_GE(lim, current_p); current_p += ALIGNMENT) {
    REGISTER ptr_t q;

    LOAD_PTR_OR_CONTINUE(q, current_p);
    GC_PUSH_ONE_HEAP(q, current_p, GC_mark_stack_top);
  }
#  undef GC_greatest_plausible_heap_addr
#  undef GC_least_plausible_heap_addr
}
#endif /* WRAP_MARK_SOME && PARALLEL_MARK */

#if !defined(SMALL_CONFIG) && !defined(USE_MARK_BYTES) \
    && !defined(MARK_BIT_PER_OBJ) && GC_GRANULE_PTRS <= 4
#  define USE_PUSH_MARKED_ACCELERATORS
#  if GC_GRANULE_PTRS == 1
#    define PUSH_GRANULE(q)                                 \
      do {                                                  \
        ptr_t q_contents = (q)[0];                          \
        GC_PUSH_ONE_HEAP(q_contents, q, GC_mark_stack_top); \
      } while (0)
#  elif GC_GRANULE_PTRS == 2
#    define PUSH_GRANULE(q)                                       \
      do {                                                        \
        ptr_t q_contents = (q)[0];                                \
        GC_PUSH_ONE_HEAP(q_contents, q, GC_mark_stack_top);       \
        q_contents = (q)[1];                                      \
        GC_PUSH_ONE_HEAP(q_contents, (q) + 1, GC_mark_stack_top); \
      } while (0)
#  else
#    define PUSH_GRANULE(q)                                       \
      do {                                                        \
        ptr_t q_contents = (q)[0];                                \
        GC_PUSH_ONE_HEAP(q_contents, q, GC_mark_stack_top);       \
        q_contents = (q)[1];                                      \
        GC_PUSH_ONE_HEAP(q_contents, (q) + 1, GC_mark_stack_top); \
        q_contents = (q)[2];                                      \
        GC_PUSH_ONE_HEAP(q_contents, (q) + 2, GC_mark_stack_top); \
        q_contents = (q)[3];                                      \
        GC_PUSH_ONE_HEAP(q_contents, (q) + 3, GC_mark_stack_top); \
      } while (0)
#  endif

/*
 * Push all objects reachable from marked objects in the given block
 * containing objects of size 1 granule.
 */
GC_ATTR_NO_SANITIZE_THREAD
STATIC void
GC_push_marked1(struct hblk *h, const hdr *hhdr)
{
  const word *mark_word_addr
      = (word *)CAST_AWAY_VOLATILE_PVOID(hhdr->hb_marks);
  ptr_t *p;
  ptr_t plim;

  /*
   * Allow registers to be used for some frequently accessed global variables.
   * Otherwise aliasing issues are likely to prevent that.
   */
  ptr_t greatest_ha = (ptr_t)GC_greatest_plausible_heap_addr;
  ptr_t least_ha = (ptr_t)GC_least_plausible_heap_addr;
  mse *mark_stack_top = GC_mark_stack_top;
  mse *mark_stack_limit = GC_mark_stack_limit;

#  undef GC_mark_stack_top
#  undef GC_mark_stack_limit
#  define GC_mark_stack_top mark_stack_top
#  define GC_mark_stack_limit mark_stack_limit
#  define GC_greatest_plausible_heap_addr greatest_ha
#  define GC_least_plausible_heap_addr least_ha

  p = (ptr_t *)h->hb_body;
  plim = (ptr_t)h + HBLKSIZE;

  /* Go through all granules in block. */
  while (ADDR_LT((ptr_t)p, plim)) {
    word mark_word = *mark_word_addr++;
    ptr_t *q;

    for (q = p; mark_word != 0; mark_word >>= 1) {
      if ((mark_word & 1) != 0)
        PUSH_GRANULE(q);
      q += GC_GRANULE_PTRS;
    }
    p += CPP_WORDSZ * GC_GRANULE_PTRS;
  }

#  undef GC_greatest_plausible_heap_addr
#  undef GC_least_plausible_heap_addr
#  undef GC_mark_stack_top
#  undef GC_mark_stack_limit
#  define GC_mark_stack_limit GC_arrays._mark_stack_limit
#  define GC_mark_stack_top GC_arrays._mark_stack_top
  GC_mark_stack_top = mark_stack_top;
}

#  ifndef UNALIGNED_PTRS
/*
 * Push all objects reachable from marked objects in the given block
 * of two-granule objects.
 */
GC_ATTR_NO_SANITIZE_THREAD
STATIC void
GC_push_marked2(struct hblk *h, const hdr *hhdr)
{
  const word *mark_word_addr
      = (word *)CAST_AWAY_VOLATILE_PVOID(hhdr->hb_marks);
  ptr_t *p;
  ptr_t plim;
  ptr_t greatest_ha = (ptr_t)GC_greatest_plausible_heap_addr;
  ptr_t least_ha = (ptr_t)GC_least_plausible_heap_addr;
  mse *mark_stack_top = GC_mark_stack_top;
  mse *mark_stack_limit = GC_mark_stack_limit;

#    undef GC_mark_stack_top
#    undef GC_mark_stack_limit
#    define GC_mark_stack_top mark_stack_top
#    define GC_mark_stack_limit mark_stack_limit
#    define GC_greatest_plausible_heap_addr greatest_ha
#    define GC_least_plausible_heap_addr least_ha

  p = (ptr_t *)h->hb_body;
  plim = (ptr_t)h + HBLKSIZE;

  /* Go through all granules in block. */
  while (ADDR_LT((ptr_t)p, plim)) {
    word mark_word = *mark_word_addr++;
    ptr_t *q;

    for (q = p; mark_word != 0; mark_word >>= 2) {
      if (mark_word & 1) {
        PUSH_GRANULE(q);
        PUSH_GRANULE(q + GC_GRANULE_PTRS);
      }
      q += 2 * GC_GRANULE_PTRS;
    }
    p += CPP_WORDSZ * GC_GRANULE_PTRS;
  }

#    undef GC_greatest_plausible_heap_addr
#    undef GC_least_plausible_heap_addr
#    undef GC_mark_stack_top
#    undef GC_mark_stack_limit
#    define GC_mark_stack_limit GC_arrays._mark_stack_limit
#    define GC_mark_stack_top GC_arrays._mark_stack_top
  GC_mark_stack_top = mark_stack_top;
}

#    if GC_GRANULE_PTRS < 4
/*
 * Push all objects reachable from marked objects in the given block of
 * four-granule objects.  There is a risk of mark stack overflow here.
 * But we handle that.  And only unmarked objects get pushed, so it is
 * not very likely.
 */
GC_ATTR_NO_SANITIZE_THREAD
STATIC void
GC_push_marked4(struct hblk *h, const hdr *hhdr)
{
  const word *mark_word_addr
      = (word *)CAST_AWAY_VOLATILE_PVOID(hhdr->hb_marks);
  ptr_t *p;
  ptr_t plim;
  ptr_t greatest_ha = (ptr_t)GC_greatest_plausible_heap_addr;
  ptr_t least_ha = (ptr_t)GC_least_plausible_heap_addr;
  mse *mark_stack_top = GC_mark_stack_top;
  mse *mark_stack_limit = GC_mark_stack_limit;

#      undef GC_mark_stack_top
#      undef GC_mark_stack_limit
#      define GC_mark_stack_top mark_stack_top
#      define GC_mark_stack_limit mark_stack_limit
#      define GC_greatest_plausible_heap_addr greatest_ha
#      define GC_least_plausible_heap_addr least_ha

  p = (ptr_t *)h->hb_body;
  plim = (ptr_t)h + HBLKSIZE;

  /* Go through all granules in block. */
  while (ADDR_LT((ptr_t)p, plim)) {
    word mark_word = *mark_word_addr++;
    ptr_t *q;

    for (q = p; mark_word != 0; mark_word >>= 4) {
      if (mark_word & 1) {
        PUSH_GRANULE(q);
        PUSH_GRANULE(q + GC_GRANULE_PTRS);
        PUSH_GRANULE(q + 2 * GC_GRANULE_PTRS);
        PUSH_GRANULE(q + 3 * GC_GRANULE_PTRS);
      }
      q += 4 * GC_GRANULE_PTRS;
    }
    p += CPP_WORDSZ * GC_GRANULE_PTRS;
  }
#      undef GC_greatest_plausible_heap_addr
#      undef GC_least_plausible_heap_addr
#      undef GC_mark_stack_top
#      undef GC_mark_stack_limit
#      define GC_mark_stack_limit GC_arrays._mark_stack_limit
#      define GC_mark_stack_top GC_arrays._mark_stack_top
  GC_mark_stack_top = mark_stack_top;
}
#    endif
#  endif
#endif /* !USE_MARK_BYTES && !MARK_BIT_PER_OBJ && !SMALL_CONFIG */

/* Push all objects reachable from marked objects in the given block. */
STATIC void
GC_push_marked(struct hblk *h, const hdr *hhdr)
{
  size_t sz = hhdr->hb_sz;
  ptr_t p;
  size_t bit_no;
  ptr_t plim;
  mse *mark_stack_top;
  mse *mark_stack_limit = GC_mark_stack_limit;

  /* Some quick shortcuts: */
  if ((/* `0 |` */ GC_DS_LENGTH) == hhdr->hb_descr)
    return;
  if (GC_block_empty(hhdr))
    return; /*< nothing marked */

#if !defined(GC_DISABLE_INCREMENTAL)
  GC_n_rescuing_pages++;
#endif
  GC_objects_are_marked = TRUE;
  switch (BYTES_TO_GRANULES(sz)) {
#ifdef USE_PUSH_MARKED_ACCELERATORS
  case 1:
    GC_push_marked1(h, hhdr);
    break;
#  ifndef UNALIGNED_PTRS
  case 2:
    GC_push_marked2(h, hhdr);
    break;
#    if GC_GRANULE_PTRS < 4
  case 4:
    GC_push_marked4(h, hhdr);
    break;
#    endif
#  endif /* !UNALIGNED_PTRS */
#else
  case 1: /*< to suppress "switch statement contains no case" warning */
#endif
  default:
    plim = sz > MAXOBJBYTES ? h->hb_body
                            : CAST_THRU_UINTPTR(ptr_t, (h + 1)->hb_body) - sz;
    mark_stack_top = GC_mark_stack_top;
    for (p = h->hb_body, bit_no = 0; ADDR_GE(plim, p);
         p += sz, bit_no += MARK_BIT_OFFSET(sz)) {
      /* Mark from fields inside the object. */
      if (mark_bit_from_hdr(hhdr, bit_no)) {
        mark_stack_top
            = GC_push_obj(p, hhdr, mark_stack_top, mark_stack_limit);
      }
    }
    GC_mark_stack_top = mark_stack_top;
  }
}

#ifdef ENABLE_DISCLAIM
/*
 * Unconditionally mark from all objects that have not been reclaimed.
 * This is useful in order to retain pointers reachable from the disclaim
 * notifiers.  To determine whether an object has been reclaimed, we
 * require that any live object has a nonzero as one of the two least
 * significant bits of the first "pointer-sized" word.  On the other hand,
 * the reclaimed object is a member of free lists, and thus contains
 * a pointer-aligned next-pointer as the first "pointer-sized" word.
 */
GC_ATTR_NO_SANITIZE_THREAD
STATIC void
GC_push_unconditionally(struct hblk *h, const hdr *hhdr)
{
  size_t sz = hhdr->hb_sz;
  ptr_t p;
  ptr_t plim;
  mse *mark_stack_top;
  mse *mark_stack_limit = GC_mark_stack_limit;

  if ((/* `0 |` */ GC_DS_LENGTH) == hhdr->hb_descr)
    return;

#  if !defined(GC_DISABLE_INCREMENTAL)
  GC_n_rescuing_pages++;
#  endif
  GC_objects_are_marked = TRUE;
  plim = sz > MAXOBJBYTES ? h->hb_body
                          : CAST_THRU_UINTPTR(ptr_t, (h + 1)->hb_body) - sz;
  mark_stack_top = GC_mark_stack_top;
  for (p = h->hb_body; ADDR_GE(plim, p); p += sz) {
    if ((ADDR(*(ptr_t *)p) & 0x3) != 0) {
      mark_stack_top = GC_push_obj(p, hhdr, mark_stack_top, mark_stack_limit);
    }
  }
  GC_mark_stack_top = mark_stack_top;
}
#endif /* ENABLE_DISCLAIM */

#ifndef GC_DISABLE_INCREMENTAL
/* Test whether any page in the given block is dirty. */
STATIC GC_bool
GC_block_was_dirty(struct hblk *h, const hdr *hhdr)
{
  size_t sz;
  ptr_t p;

#  ifdef AO_HAVE_load
  /* Atomic access is used to avoid racing with `GC_realloc`. */
  sz = AO_load((volatile AO_t *)&hhdr->hb_sz);
#  else
  sz = hhdr->hb_sz;
#  endif
  if (sz <= MAXOBJBYTES) {
    return GC_page_was_dirty(h);
  }

  for (p = (ptr_t)h; ADDR_LT(p, (ptr_t)h + sz); p += HBLKSIZE) {
    if (GC_page_was_dirty((struct hblk *)p))
      return TRUE;
  }
  return FALSE;
}
#endif /* GC_DISABLE_INCREMENTAL */

/*
 * Similar to `GC_push_marked`, but skip over unallocated blocks and
 * return address of next plausible block.
 */
STATIC struct hblk *
GC_push_next_marked(struct hblk *h)
{
  hdr *hhdr = HDR(h);

  if (UNLIKELY(IS_FORWARDING_ADDR_OR_NIL(hhdr) || HBLK_IS_FREE(hhdr))) {
    h = GC_next_block(h, FALSE);
    if (NULL == h)
      return NULL;
    hhdr = GC_find_header(h);
  } else {
#ifdef LINT2
    if (NULL == h)
      ABORT("Bad HDR() definition");
#endif
  }
  GC_push_marked(h, hhdr);
  return h + OBJ_SZ_TO_BLOCKS(hhdr->hb_sz);
}

#ifndef GC_DISABLE_INCREMENTAL
/* Identical to `GC_push_next_marked`, but mark only from dirty pages. */
STATIC struct hblk *
GC_push_next_marked_dirty(struct hblk *h)
{
  hdr *hhdr;

  GC_ASSERT(I_HOLD_LOCK());
  if (!GC_incremental)
    ABORT("Dirty bits not set up");
  for (;; h += OBJ_SZ_TO_BLOCKS(hhdr->hb_sz)) {
    hhdr = HDR(h);
    if (UNLIKELY(IS_FORWARDING_ADDR_OR_NIL(hhdr) || HBLK_IS_FREE(hhdr))) {
      h = GC_next_block(h, FALSE);
      if (NULL == h)
        return NULL;
      hhdr = GC_find_header(h);
    } else {
#  ifdef LINT2
      if (NULL == h)
        ABORT("Bad HDR() definition");
#  endif
    }
    if (GC_block_was_dirty(h, hhdr))
      break;
  }
#  ifdef ENABLE_DISCLAIM
  if ((hhdr->hb_flags & MARK_UNCONDITIONALLY) != 0) {
    GC_push_unconditionally(h, hhdr);

    /*
     * Then we may ask, why not also add the `MARK_UNCONDITIONALLY`
     * case to `GC_push_next_marked`, which is also applied to
     * uncollectible blocks?  But it seems to me that the function
     * does not need to scan uncollectible (and unconditionally
     * marked) blocks since those are already handled in the
     * `MS_PUSH_UNCOLLECTABLE` phase.
     */
  } else
#  endif
  /* else */ {
    GC_push_marked(h, hhdr);
  }
  return h + OBJ_SZ_TO_BLOCKS(hhdr->hb_sz);
}
#endif /* !GC_DISABLE_INCREMENTAL */

/*
 * Similar to `GC_push_next_marked`, but for uncollectible pages.
 * Needed since we do not clear marks for such pages, even for full
 * collections.
 */
STATIC struct hblk *
GC_push_next_marked_uncollectable(struct hblk *h)
{
  hdr *hhdr = HDR(h);

  for (;;) {
    if (UNLIKELY(IS_FORWARDING_ADDR_OR_NIL(hhdr) || HBLK_IS_FREE(hhdr))) {
      h = GC_next_block(h, FALSE);
      if (NULL == h)
        return NULL;
      hhdr = GC_find_header(h);
    } else {
#ifdef LINT2
      if (NULL == h)
        ABORT("Bad HDR() definition");
#endif
    }
    if (hhdr->hb_obj_kind == UNCOLLECTABLE) {
      GC_push_marked(h, hhdr);
      break;
    }
#ifdef ENABLE_DISCLAIM
    if ((hhdr->hb_flags & MARK_UNCONDITIONALLY) != 0) {
      GC_push_unconditionally(h, hhdr);
      break;
    }
#endif
    h += OBJ_SZ_TO_BLOCKS(hhdr->hb_sz);
    hhdr = HDR(h);
  }
  return h + OBJ_SZ_TO_BLOCKS(hhdr->hb_sz);
}
