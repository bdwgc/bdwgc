/*
 * Copyright (c) 1991-1994 by Xerox Corporation.  All rights reserved.
 * Copyright (c) 2001 by Hewlett-Packard Company. All rights reserved.
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

/*
 * Private declarations of the collector marker data structures (like the
 * mark stack) and macros.  Needed by the marker and the client-supplied
 * mark routines.  Transitively includes `gc_priv.h` file.
 */

#ifndef GC_PMARK_H
#define GC_PMARK_H

#if defined(HAVE_CONFIG_H) && !defined(GC_PRIVATE_H)
/*
 * When `gc_pmark.h` file is included from `gc_priv.h` file, some of
 * macros might be undefined in `gcconfig.h` file, so skip `config.h`
 * file in this case.
 */
#  include "config.h"
#endif

#ifndef GC_BUILD
#  define GC_BUILD
#endif

#if (defined(__linux__) || defined(__GLIBC__) || defined(__GNU__)) \
    && !defined(_GNU_SOURCE) && defined(GC_PTHREADS)               \
    && !defined(GC_NO_PTHREAD_SIGMASK)
#  define _GNU_SOURCE 1
#endif

#if defined(KEEP_BACK_PTRS) || defined(PRINT_BLACK_LIST)
#  include "dbg_mlc.h"
#endif

#include "gc/gc_mark.h"
#include "gc_priv.h"

EXTERN_C_BEGIN

/*
 * The real declarations of the following is in `gc_priv.h` file, so
 * that we can avoid scanning `GC_mark_procs` table.
 */

/*
 * Mark descriptor stuff that should remain private for now, mostly
 * because it is hard to export `CPP_WORDSZ` macro without include
 * `gcconfig.h` file.
 */
#define BITMAP_BITS (CPP_WORDSZ - GC_DS_TAG_BITS)
#define PROC(descr) \
  (GC_mark_procs[((descr) >> GC_DS_TAG_BITS) & (GC_MAX_MARK_PROCS - 1)])
#define ENV(descr) ((descr) >> (GC_DS_TAG_BITS + GC_LOG_MAX_MARK_PROCS))
#define MAX_ENV (((word)1 << (BITMAP_BITS - GC_LOG_MAX_MARK_PROCS)) - 1)

GC_EXTERN unsigned GC_n_mark_procs;

/* Number of mark stack entries to discard on overflow. */
#define GC_MARK_STACK_DISCARDS (INITIAL_MARK_STACK_SIZE / 8)

#ifdef PARALLEL_MARK
/*
 * Allow multiple threads to participate in the marking process.
 * This works roughly as follows:
 *   - The main mark stack never shrinks, but it can grow.
 *   - The initiating threads holds the allocator lock, sets
 *     `GC_help_wanted`.
 *   - Other threads:
 *     1. Update `GC_helper_count` (while holding the mark lock).
 *     2. Allocate a local mark stack repeatedly:
 *        2.1. Steal a global mark stack entry by atomically replacing
 *             its descriptor with 0;
 *        2.2. Copy it to the local stack;
 *        2.3. Mark on the local stack until it is empty, or it may be
 *             profitable to copy it back;
 *        2.4. If necessary, copy local stack to global one, holding the
 *             mark lock;
 *        2.5. Stop when the global mark stack is empty.
 *     3. Decrement `GC_helper_count` (holding the mark lock).
 *
 * This is an experiment to see if we can do something along the lines
 * of the University of Tokyo SGC in a less intrusive, though probably
 * also less performant, way.
 */

/* `GC_mark_stack_top` is protected by the mark lock. */

/*
 * `GC_notify_all_marker()` is used when `GC_help_wanted` is first set,
 * when the last helper becomes inactive, when something is added to the
 * global mark stack, and just after `GC_mark_no` is incremented.
 * This could be split into multiple conditional variables (and probably
 * should be) to scale to really large numbers of processors.
 */
#endif /* PARALLEL_MARK */

/*
 * Push the object `obj` with corresponding heap block header `hhdr`
 * onto the mark stack.  Returns the updated `mark_stack_top` value.
 */
GC_INLINE mse *
GC_ms_push_obj_hdr(ptr_t obj, const hdr *hhdr, mse *mark_stack_top,
                   mse *mark_stack_limit)
{
  GC_ASSERT(!HBLK_IS_FREE(hhdr));
  if (!IS_PTRFREE(hhdr)) {
    mark_stack_top = GC_custom_push_proc(hhdr->hb_descr, obj, mark_stack_top,
                                         mark_stack_limit);
  }
  return mark_stack_top;
}

/*
 * Push the contents of `current` onto the mark stack if it is a valid
 * pointer to a currently unmarked object.  Mark it.
 */
#define PUSH_CONTENTS(current, mark_stack_top, mark_stack_limit, source)   \
  do {                                                                     \
    hdr *my_hhdr;                                                          \
    HC_GET_HDR(current, my_hhdr, source); /*< contains `break` */          \
    mark_stack_top = GC_ms_push_contents_hdr(                              \
        current, my_hhdr, mark_stack_top, mark_stack_limit, source, TRUE); \
  } while (0)

/* Set mark bit, exit (using `break` statement) if it is already set. */
#ifdef USE_MARK_BYTES
#  if defined(PARALLEL_MARK) && defined(AO_HAVE_char_store) \
      && !defined(BASE_ATOMIC_OPS_EMULATED)
/*
 * There is a race here, and we may set the bit twice in the concurrent
 * case.  This can result in the object being pushed twice.  But that is
 * only a performance issue.
 */
#    define SET_MARK_BIT_EXIT_IF_SET(hhdr, bit_no)                 \
      { /*< cannot use `do ... while (0)` here */                  \
        volatile unsigned char *mark_byte_addr                     \
            = (unsigned char *)(hhdr)->hb_marks + (bit_no);        \
        /* Unordered atomic load and store are sufficient here. */ \
        if (AO_char_load(mark_byte_addr) != 0)                     \
          break; /*< go to the enclosing loop end */               \
        AO_char_store(mark_byte_addr, 1);                          \
      }
#  else
#    define SET_MARK_BIT_EXIT_IF_SET(hhdr, bit_no)                 \
      { /*< cannot use `do ... while (0)` here */                  \
        ptr_t mark_byte_addr = (ptr_t)(hhdr)->hb_marks + (bit_no); \
                                                                   \
        if (*mark_byte_addr != 0)                                  \
          break; /*< go to the enclosing loop end */               \
        *mark_byte_addr = 1;                                       \
      }
#  endif /* !PARALLEL_MARK */
#else
#  if defined(PARALLEL_MARK) || (defined(THREAD_SANITIZER) && defined(THREADS))
#    ifdef THREAD_SANITIZER
#      define MARK_WORD_READ(addr) AO_load(addr)
#    else
#      define MARK_WORD_READ(addr) (*(addr))
#    endif
/*
 * This is used only if we explicitly define `USE_MARK_BITS` macro.
 * The following may fail to exit even if the bit was already set.
 * For our uses, that is benign.
 */
#    define SET_MARK_BIT_EXIT_IF_SET(hhdr, bit_no)                            \
      { /*< cannot use `do ... while (0)` here */                             \
        volatile AO_t *mark_word_addr = (hhdr)->hb_marks + divWORDSZ(bit_no); \
        word my_bits = (word)1 << modWORDSZ(bit_no);                          \
                                                                              \
        if ((MARK_WORD_READ(mark_word_addr) & my_bits) != 0)                  \
          break; /*< go to the enclosing loop end */                          \
        AO_or(mark_word_addr, my_bits);                                       \
      }
#  else /* !PARALLEL_MARK */
#    define SET_MARK_BIT_EXIT_IF_SET(hhdr, bit_no)                   \
      { /*< cannot use `do ... while (0)` here */                    \
        word *mark_word_addr = (hhdr)->hb_marks + divWORDSZ(bit_no); \
        word old = *mark_word_addr;                                  \
        word my_bits = (word)1 << modWORDSZ(bit_no);                 \
                                                                     \
        if ((old & my_bits) != 0)                                    \
          break; /*< go to the enclosing loop end */                 \
        *(mark_word_addr) = old | my_bits;                           \
      }
#  endif
#endif /* !USE_MARK_BYTES */

#ifdef ENABLE_TRACE
#  define TRACE(source, cmd)                                     \
    if (GC_trace_ptr != NULL && (ptr_t)(source) == GC_trace_ptr) \
    cmd
#  define TRACE_TARGET(target, cmd)                          \
    if (GC_trace_ptr != NULL && GC_is_heap_ptr(GC_trace_ptr) \
        && (target) == *(ptr_t *)GC_trace_ptr)               \
    cmd
#else
#  define TRACE(source, cmd)
#  define TRACE_TARGET(source, cmd)
#endif

/*
 * If the mark bit corresponding to `current` is not set, set it, and
 * push the contents of the object on the mark stack.  `current` points
 * to the beginning of the object.  We rely on the fact that the
 * preceding header calculation will succeed for a pointer past the
 * first page of an object, only if it is in fact a valid pointer
 * to the object.  Thus we can omit the otherwise necessary tests here.
 */
GC_INLINE mse *
GC_ms_push_contents_hdr(ptr_t current, hdr *hhdr, mse *mark_stack_top,
                        mse *mark_stack_limit, ptr_t source,
                        GC_bool do_offset_check)
{
  do {
    /*
     * Displacement in the block, in bytes; always within range.
     * Note, in particular, that this value is the displacement from the
     * beginning of the heap block, which may itself be in the interior
     * of a large object.  If `current` does not point to the first block,
     * then we are in the all-interior-pointers mode, and it is safe to
     * use any displacement value.
     */
    size_t displ = HBLKDISPL(current);
    ptr_t base = current;
#ifdef MARK_BIT_PER_OBJ
    unsigned32 gran_displ; /*< `high_prod` */
    unsigned32 inv_sz = hhdr->hb_inv_sz;

#else
    size_t gran_displ = BYTES_TO_GRANULES(displ);
    size_t gran_offset = hhdr->hb_map[gran_displ];
    size_t byte_offset = displ & (GC_GRANULE_BYTES - 1);

    /* The following always fails for large block references. */
    if (UNLIKELY((gran_offset | byte_offset) != 0))
#endif
    {
#ifdef MARK_BIT_PER_OBJ
      if (UNLIKELY(inv_sz == LARGE_INV_SZ))
#else
      if ((hhdr->hb_flags & LARGE_BLOCK) != 0)
#endif
      {
        /* `gran_offset` is bogus. */
        size_t obj_displ;

        base = (ptr_t)hhdr->hb_block;
        obj_displ = (size_t)(current - base);
        if (obj_displ != displ) {
          GC_ASSERT(obj_displ < hhdr->hb_sz);
          /*
           * Must be in the all-interior-pointers mode, non-first block
           * already did validity check on cache miss.
           */
        } else if (do_offset_check && !GC_valid_offsets[obj_displ]) {
          GC_ADD_TO_BLACK_LIST_NORMAL(current, source);
          break;
        }
        GC_ASSERT(hhdr->hb_sz > HBLKSIZE
                  || hhdr->hb_block == HBLKPTR(current));
        GC_ASSERT(ADDR_GE(current, (ptr_t)hhdr->hb_block));
        gran_displ = 0;
      } else {
#ifdef MARK_BIT_PER_OBJ
        unsigned32 low_prod;

        LONG_MULT(gran_displ, low_prod, (unsigned32)displ, inv_sz);
        if ((low_prod >> 16) != 0)
#endif
        {
          size_t obj_displ;

#ifdef MARK_BIT_PER_OBJ
          /* Accurate enough if `HBLKSIZE` is not greater than `2**15`. */
          GC_STATIC_ASSERT(HBLKSIZE <= (1 << 15));
          obj_displ = (((low_prod >> 16) + 1) * hhdr->hb_sz) >> 16;
#else
          obj_displ = GRANULES_TO_BYTES(gran_offset) + byte_offset;
#endif

          if (do_offset_check && !GC_valid_offsets[obj_displ]) {
            GC_ADD_TO_BLACK_LIST_NORMAL(current, source);
            break;
          }
#ifndef MARK_BIT_PER_OBJ
          gran_displ -= gran_offset;
#endif
          base -= obj_displ;
        }
      }
    }
#ifdef MARK_BIT_PER_OBJ
    /*
     * May get here for pointer to start of block not at the beginning
     * of object.  If so, it is valid, and we are fine.
     */
    GC_ASSERT(gran_displ <= HBLK_OBJS(hhdr->hb_sz));
#else
    GC_ASSERT(hhdr == GC_find_header(base));
    GC_ASSERT(gran_displ % BYTES_TO_GRANULES(hhdr->hb_sz) == 0);
#endif
    TRACE(source, GC_log_printf("GC #%lu: passed validity tests\n",
                                (unsigned long)GC_gc_no));
    SET_MARK_BIT_EXIT_IF_SET(hhdr, gran_displ); /*< contains `break` */
    TRACE(source, GC_log_printf("GC #%lu: previously unmarked\n",
                                (unsigned long)GC_gc_no));
    TRACE_TARGET(base, GC_log_printf("GC #%lu: marking %p from %p instead\n",
                                     (unsigned long)GC_gc_no, (void *)base,
                                     (void *)source));
    INCR_MARKS(hhdr);
    GC_STORE_BACK_PTR(source, base);
    mark_stack_top
        = GC_ms_push_obj_hdr(base, hhdr, mark_stack_top, mark_stack_limit);
  } while (0);
  return mark_stack_top;
}

#if defined(PRINT_BLACK_LIST) || defined(KEEP_BACK_PTRS)
#  define PUSH_ONE_CHECKED_STACK(p, source) \
    GC_mark_and_push_stack(p, (ptr_t)(source))
#else
#  define PUSH_ONE_CHECKED_STACK(p, source) GC_mark_and_push_stack(p)
#endif

/*
 * Push a single value onto mark stack.  Mark from the object
 * pointed to by `p`.  The argument should be of `ptr_t` type.
 * Invoke `FIXUP_POINTER()` before any further processing.
 * p` is considered valid even if it is an interior pointer.
 * Previously marked objects are not pushed.  Hence we make progress
 * even if the mark stack overflows.
 */
#ifdef NEED_FIXUP_POINTER
/* Try both the raw variant and the fixed up one. */
#  define GC_PUSH_ONE_STACK(p, source)                              \
    do {                                                            \
      ptr_t pp = (p);                                               \
                                                                    \
      if (ADDR_LT((ptr_t)GC_least_plausible_heap_addr, p)           \
          && ADDR_LT(p, (ptr_t)GC_greatest_plausible_heap_addr)) {  \
        PUSH_ONE_CHECKED_STACK(p, source);                          \
      }                                                             \
      FIXUP_POINTER(pp);                                            \
      if (ADDR_LT((ptr_t)GC_least_plausible_heap_addr, pp)          \
          && ADDR_LT(pp, (ptr_t)GC_greatest_plausible_heap_addr)) { \
        PUSH_ONE_CHECKED_STACK(pp, source);                         \
      }                                                             \
    } while (0)
#else /* !NEED_FIXUP_POINTER */
#  define GC_PUSH_ONE_STACK(p, source)                             \
    do {                                                           \
      if (ADDR_LT((ptr_t)GC_least_plausible_heap_addr, p)          \
          && ADDR_LT(p, (ptr_t)GC_greatest_plausible_heap_addr)) { \
        PUSH_ONE_CHECKED_STACK(p, source);                         \
      }                                                            \
    } while (0)
#endif

/*
 * Same as `GC_PUSH_ONE_STACK`, but the interior pointers recognition as
 * for normal heap pointers.
 */
#define GC_PUSH_ONE_HEAP(p, source, mark_stack_top)                   \
  do {                                                                \
    FIXUP_POINTER(p);                                                 \
    if (ADDR_LT((ptr_t)GC_least_plausible_heap_addr, p)               \
        && ADDR_LT(p, (ptr_t)GC_greatest_plausible_heap_addr))        \
      mark_stack_top = GC_mark_and_push(                              \
          p, mark_stack_top, GC_mark_stack_limit, (void **)(source)); \
  } while (0)

/*
 * Mark objects pointed to by the regions described by mark stack entries
 * between `mark_stack` and `mark_stack_top`, inclusive.  Assumes the upper
 * limit of a mark stack entry is never `NULL`.  A mark stack entry never
 * has zero size.  Return the new value of `mark_stack_top`.
 * We try to traverse on the order of a `hblk` of memory before we return.
 * Caller is responsible for calling this until the mark stack is empty.
 * Note that this is the most performance critical routine in the collector.
 * Hence it contains all sorts of ugly hacks to speed things up.
 * In particular, we avoid procedure calls on the common path, we take
 * advantage of peculiarities of the mark descriptor encoding, we optionally
 * maintain a cache for the block address to header mapping, we prefetch
 * when an object is "grayed", etc.
 */
GC_INNER mse *GC_mark_from(mse *mark_stack_top, mse *mark_stack,
                           mse *mark_stack_limit);

#define MARK_FROM_MARK_STACK()                                       \
  GC_mark_stack_top = GC_mark_from(GC_mark_stack_top, GC_mark_stack, \
                                   GC_mark_stack + GC_mark_stack_size);

#define GC_mark_stack_empty() \
  ADDR_LT((ptr_t)GC_mark_stack_top, (ptr_t)GC_mark_stack)

/*
 * The current state of marking, as follows.  We say something is dirty
 * if it was written since the last time we retrieved dirty bits.
 * We say it is grungy if it was marked dirty in the last set of bits
 * we retrieved.  Invariant "I": all roots and marked objects `p` are
 * either dirty, or point to objects `q` that are either marked or
 * a pointer to `q` appears in a range on the mark stack.
 */

/* No marking in progress.  "I" holds.  Mark stack is empty. */
#define MS_NONE 0

/*
 * Rescuing objects are currently being pushed.  "I" holds, except that
 * grungy roots may point to unmarked objects, as may marked grungy objects
 * above `GC_scan_ptr`.
 */
#define MS_PUSH_RESCUERS 1

/*
 * Uncollectible objects are currently being pushed.  "I" holds, except
 * that marked uncollectible objects above `GC_scan_ptr` may point to
 * unmarked objects.  Roots may point to unmarked objects too.
 */
#define MS_PUSH_UNCOLLECTABLE 2

/* "I" holds, mark stack may be nonempty. */
#define MS_ROOTS_PUSHED 3

/*
 * "I" may not hold, e.g. because of the mark stack overflow.  However,
 * marked heap objects below `GC_scan_ptr` point to marked or stacked
 * objects.
 */
#define MS_PARTIALLY_INVALID 4

/* "I" may not hold. */
#define MS_INVALID 5

EXTERN_C_END

#endif /* GC_PMARK_H */
