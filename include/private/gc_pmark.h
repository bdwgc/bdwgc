/*
 * Copyright (c) 1991-1994 by Xerox Corporation.  All rights reserved.
 * Copyright (c) 2001 by Hewlett-Packard Company. All rights reserved.
 * Copyright (c) 2008-2023 Ivan Maidanski
 *
 * THIS MATERIAL IS PROVIDED AS IS, WITH ABSOLUTELY NO WARRANTY EXPRESSED
 * OR IMPLIED.  ANY USE IS AT YOUR OWN RISK.
 *
 * Permission is hereby granted to use or copy this program
 * for any purpose,  provided the above notices are retained on all copies.
 * Permission to modify the code and to distribute modified code is granted,
 * provided the above notices are retained, and a notice that the code was
 * modified is included with the above copyright notice.
 *
 */

/* Private declarations of GC marker data structures and macros */

/*
 * Declarations of mark stack.  Needed by marker and client supplied mark
 * routines.  Transitively include gc_priv.h.
 */
#ifndef GC_PMARK_H
#define GC_PMARK_H

#if defined(HAVE_CONFIG_H) && !defined(GC_PRIVATE_H)
  /* When gc_pmark.h is included from gc_priv.h, some of macros might   */
  /* be undefined in gcconfig.h, so skip config.h in this case.         */
# include "config.h"
#endif

#ifndef GC_BUILD
# define GC_BUILD
#endif

#if (defined(__linux__) || defined(__GLIBC__) || defined(__GNU__)) \
    && !defined(_GNU_SOURCE) && defined(GC_PTHREADS) \
    && !defined(GC_NO_PTHREAD_SIGMASK)
# define _GNU_SOURCE 1
#endif

#if defined(KEEP_BACK_PTRS) || defined(PRINT_BLACK_LIST)
# include "dbg_mlc.h"
#endif

#include "../gc_mark.h"
#include "gc_priv.h"

EXTERN_C_BEGIN

/* The real declarations of the following is in gc_priv.h, so that      */
/* we can avoid scanning the following table.                           */
/*
mark_proc GC_mark_procs[MAX_MARK_PROCS];
*/

#ifndef MARK_DESCR_OFFSET
# define MARK_DESCR_OFFSET sizeof(word)
#endif

/*
 * Mark descriptor stuff that should remain private for now, mostly
 * because it's hard to export WORDSZ without including gcconfig.h.
 */
#define BITMAP_BITS (WORDSZ - GC_DS_TAG_BITS)
#define PROC(descr) \
      (GC_mark_procs[((descr) >> GC_DS_TAG_BITS) & (GC_MAX_MARK_PROCS-1)])
#define ENV(descr) \
      ((descr) >> (GC_DS_TAG_BITS + GC_LOG_MAX_MARK_PROCS))
#define MAX_ENV \
      (((word)1 << (WORDSZ - GC_DS_TAG_BITS - GC_LOG_MAX_MARK_PROCS)) - 1)

GC_EXTERN unsigned GC_n_mark_procs;

/* Number of mark stack entries to discard on overflow. */
#define GC_MARK_STACK_DISCARDS (INITIAL_MARK_STACK_SIZE/8)

#ifdef PARALLEL_MARK
    /*
     * Allow multiple threads to participate in the marking process.
     * This works roughly as follows:
     *  The main mark stack never shrinks, but it can grow.
     *
     *  The initiating threads holds the GC lock, and sets GC_help_wanted.
     *
     *  Other threads:
     *     1) update helper_count (while holding the mark lock).
     *     2) allocate a local mark stack
     *     repeatedly:
     *          3) Steal a global mark stack entry by atomically replacing
     *             its descriptor with 0.
     *          4) Copy it to the local stack.
     *          5) Mark on the local stack until it is empty, or
     *             it may be profitable to copy it back.
     *          6) If necessary, copy local stack to global one,
     *             holding mark lock.
     *    7) Stop when the global mark stack is empty.
     *    8) decrement helper_count (holding the mark lock).
     *
     * This is an experiment to see if we can do something along the lines
     * of the University of Tokyo SGC in a less intrusive, though probably
     * also less performant, way.
     */

    /* GC_mark_stack_top is protected by mark lock.     */

    /*
     * GC_notify_all_marker() is used when GC_help_wanted is first set,
     * when the last helper becomes inactive,
     * when something is added to the global mark stack, and just after
     * GC_mark_no is incremented.
     * This could be split into multiple CVs (and probably should be to
     * scale to really large numbers of processors.)
     */
#endif /* PARALLEL_MARK */

GC_INNER mse * GC_signal_mark_stack_overflow(mse *msp);

/* Push the object obj with corresponding heap block header hhdr onto   */
/* the mark stack.  Returns the updated mark_stack_top value.           */
GC_INLINE mse * GC_push_obj(ptr_t obj, hdr * hhdr,  mse * mark_stack_top,
                            mse * mark_stack_limit)
{
  word descr = hhdr -> hb_descr;

  GC_ASSERT(!HBLK_IS_FREE(hhdr));
  if (descr != 0) {
    mark_stack_top++;
    if ((word)mark_stack_top >= (word)mark_stack_limit) {
      mark_stack_top = GC_signal_mark_stack_overflow(mark_stack_top);
    }
    mark_stack_top -> mse_start = obj;
    mark_stack_top -> mse_descr.w = descr;
  }
  return mark_stack_top;
}

/* Push the contents of current onto the mark stack if it is a valid    */
/* ptr to a currently unmarked object.  Mark it.                        */
#define PUSH_CONTENTS(current, mark_stack_top, mark_stack_limit, source) \
  do { \
    hdr * my_hhdr; \
    HC_GET_HDR(current, my_hhdr, source); /* contains "break" */ \
    mark_stack_top = GC_push_contents_hdr(current, mark_stack_top, \
                                          mark_stack_limit, \
                                          source, my_hhdr, TRUE); \
  } while (0)

/* Set mark bit, exit (using "break" statement) if it is already set.   */
#ifdef USE_MARK_BYTES
# if defined(PARALLEL_MARK) && defined(AO_HAVE_char_store) \
     && !defined(BASE_ATOMIC_OPS_EMULATED)
    /* There is a race here, and we may set the bit twice in the        */
    /* concurrent case.  This can result in the object being pushed     */
    /* twice.  But that is only a performance issue.                    */
#   define SET_MARK_BIT_EXIT_IF_SET(hhdr, bit_no) \
      { /* cannot use do-while(0) here */ \
        volatile unsigned char * mark_byte_addr = \
                        (unsigned char *)(hhdr)->hb_marks + (bit_no); \
        /* Unordered atomic load and store are sufficient here. */ \
        if (AO_char_load(mark_byte_addr) != 0) \
          break; /* go to the enclosing loop end */ \
        AO_char_store(mark_byte_addr, 1); \
      }
# else
#   define SET_MARK_BIT_EXIT_IF_SET(hhdr, bit_no) \
      { /* cannot use do-while(0) here */ \
        char * mark_byte_addr = (char *)(hhdr)->hb_marks + (bit_no); \
        if (*mark_byte_addr != 0) break; /* go to the enclosing loop end */ \
        *mark_byte_addr = 1; \
      }
# endif /* !PARALLEL_MARK */
#else
# ifdef PARALLEL_MARK
    /* This is used only if we explicitly set USE_MARK_BITS.            */
    /* The following may fail to exit even if the bit was already set.  */
    /* For our uses, that's benign:                                     */
#   ifdef THREAD_SANITIZER
#     define OR_WORD_EXIT_IF_SET(addr, bits) \
        { /* cannot use do-while(0) here */ \
          if (!((word)AO_load((volatile AO_t *)(addr)) & (bits))) { \
                /* Atomic load is just to avoid TSan false positive. */ \
            AO_or((volatile AO_t *)(addr), (AO_t)(bits)); \
          } else { \
            break; /* go to the enclosing loop end */ \
          } \
        }
#   else
#     define OR_WORD_EXIT_IF_SET(addr, bits) \
        { /* cannot use do-while(0) here */ \
          if (!(*(addr) & (bits))) { \
            AO_or((volatile AO_t *)(addr), (AO_t)(bits)); \
          } else { \
            break; /* go to the enclosing loop end */ \
          } \
        }
#   endif /* !THREAD_SANITIZER */
# else
#   define OR_WORD_EXIT_IF_SET(addr, bits) \
        { /* cannot use do-while(0) here */ \
           word old = *(addr); \
           word my_bits = (bits); \
           if ((old & my_bits) != 0) \
             break; /* go to the enclosing loop end */ \
           *(addr) = old | my_bits; \
        }
# endif /* !PARALLEL_MARK */
# define SET_MARK_BIT_EXIT_IF_SET(hhdr, bit_no) \
    { /* cannot use do-while(0) here */ \
        word * mark_word_addr = (hhdr)->hb_marks + divWORDSZ(bit_no); \
        OR_WORD_EXIT_IF_SET(mark_word_addr, \
                (word)1 << modWORDSZ(bit_no)); /* contains "break" */ \
    }
#endif /* !USE_MARK_BYTES */

#ifdef PARALLEL_MARK
# define INCR_MARKS(hhdr) \
                AO_store(&hhdr->hb_n_marks, AO_load(&hhdr->hb_n_marks) + 1)
#else
# define INCR_MARKS(hhdr) (void)(++hhdr->hb_n_marks)
#endif

#ifdef ENABLE_TRACE
# define TRACE(source, cmd) \
        if (GC_trace_addr != 0 && (ptr_t)(source) == GC_trace_addr) cmd
# define TRACE_TARGET(target, cmd) \
        if (GC_trace_addr != NULL && GC_is_heap_ptr(GC_trace_addr) \
            && (target) == *(ptr_t *)GC_trace_addr) cmd
#else
# define TRACE(source, cmd)
# define TRACE_TARGET(source, cmd)
#endif

#if defined(I386) && defined(__GNUC__) && !defined(NACL)
# define LONG_MULT(hprod, lprod, x, y) \
    do { \
        __asm__ __volatile__("mull %2" : "=a"(lprod), "=d"(hprod) \
                             : "r"(y), "0"(x)); \
    } while (0)
#else
# if defined(__int64) && !defined(__GNUC__) && !defined(CPPCHECK)
#   define ULONG_MULT_T unsigned __int64
# else
#   define ULONG_MULT_T unsigned long long
# endif
# define LONG_MULT(hprod, lprod, x, y) \
    do { \
        ULONG_MULT_T prod = (ULONG_MULT_T)(x) * (ULONG_MULT_T)(y); \
        GC_STATIC_ASSERT(sizeof(x) + sizeof(y) <= sizeof(prod)); \
        hprod = (unsigned32)(prod >> 32); \
        lprod = (unsigned32)prod; \
    } while (0)
#endif /* !I386 */

/* If the mark bit corresponding to current is not set, set it, and     */
/* push the contents of the object on the mark stack.  Current points   */
/* to the beginning of the object.  We rely on the fact that the        */
/* preceding header calculation will succeed for a pointer past the     */
/* first page of an object, only if it is in fact a valid pointer       */
/* to the object.  Thus we can omit the otherwise necessary tests       */
/* here.  Note in particular that the "displ" value is the displacement */
/* from the beginning of the heap block, which may itself be in the     */
/* interior of a large object.                                          */
GC_INLINE mse * GC_push_contents_hdr(ptr_t current, mse * mark_stack_top,
                                     mse * mark_stack_limit, ptr_t source,
                                     hdr * hhdr, GC_bool do_offset_check)
{
  do {
    size_t displ = HBLKDISPL(current); /* Displacement in block; in bytes. */
    /* displ is always within range.  If current doesn't point to the   */
    /* first block, then we are in the all_interior_pointers case, and  */
    /* it is safe to use any displacement value.                        */
    ptr_t base = current;
#   ifdef MARK_BIT_PER_GRANULE
      size_t gran_displ = BYTES_TO_GRANULES(displ);
      size_t gran_offset = hhdr -> hb_map[gran_displ];
      size_t byte_offset = displ & (GRANULE_BYTES - 1);

      /* The following always fails for large block references.         */
      if (EXPECT((gran_offset | byte_offset) != 0, FALSE))
#   else
      unsigned32 gran_displ; /* high_prod */
      unsigned32 inv_sz = hhdr -> hb_inv_sz;
#   endif /* MARK_BIT_PER_OBJ */

    {
#     ifdef MARK_BIT_PER_GRANULE
        if ((hhdr -> hb_flags & LARGE_BLOCK) != 0)
#     else
        if (EXPECT(inv_sz == LARGE_INV_SZ, FALSE))
#     endif /* MARK_BIT_PER_OBJ */
      {
        /* gran_offset is bogus.        */
        size_t obj_displ;

        base = (ptr_t)hhdr->hb_block;
        obj_displ = current - base;
        if (obj_displ != displ) {
          GC_ASSERT(obj_displ < hhdr -> hb_sz);
          /* Must be in all_interior_pointer case, not first block      */
          /* already did validity check on cache miss.                  */
        } else if (do_offset_check && !GC_valid_offsets[obj_displ]) {
          GC_ADD_TO_BLACK_LIST_NORMAL(current, source);
          break;
        }
        GC_ASSERT(hhdr -> hb_sz > HBLKSIZE
                  || hhdr -> hb_block == HBLKPTR(current));
        GC_ASSERT((word)hhdr->hb_block <= (word)current);
        gran_displ = 0;
      } else {
#       ifdef MARK_BIT_PER_GRANULE
          size_t obj_displ = GRANULES_TO_BYTES(gran_offset) + byte_offset;
#       else
          unsigned32 low_prod;

          LONG_MULT(gran_displ, low_prod, (unsigned32)displ, inv_sz);
          if ((low_prod >> 16) != 0)
#       endif /* MARK_BIT_PER_OBJ */
        {
#         if defined(MARK_BIT_PER_OBJ) \
             && !defined(MARK_BIT_PER_GRANULE) /* for cppcheck */
            size_t obj_displ;

            /* Accurate enough if HBLKSIZE <= 2**15.    */
            GC_STATIC_ASSERT(HBLKSIZE <= (1 << 15));
            obj_displ = (((low_prod >> 16) + 1) * (size_t)hhdr->hb_sz) >> 16;
#         endif
          if (do_offset_check && !GC_valid_offsets[obj_displ]) {
            GC_ADD_TO_BLACK_LIST_NORMAL(current, source);
            break;
          }
#         ifdef MARK_BIT_PER_GRANULE
            gran_displ -= gran_offset;
#         endif
          base -= obj_displ;
        }
      }
    }
#   ifdef MARK_BIT_PER_GRANULE
      GC_ASSERT(hhdr == GC_find_header(base));
      GC_ASSERT(gran_displ % BYTES_TO_GRANULES(hhdr -> hb_sz) == 0);
#   else
      /* May get here for pointer to start of block not at the          */
      /* beginning of object.  If so, it is valid, and we are fine.     */
      GC_ASSERT(gran_displ <= HBLK_OBJS(hhdr -> hb_sz));
#   endif /* MARK_BIT_PER_OBJ */
    TRACE(source, GC_log_printf("GC #%lu: passed validity tests\n",
                                (unsigned long)GC_gc_no));
    SET_MARK_BIT_EXIT_IF_SET(hhdr, gran_displ); /* contains "break" */
    TRACE(source, GC_log_printf("GC #%lu: previously unmarked\n",
                                (unsigned long)GC_gc_no));
    TRACE_TARGET(base, GC_log_printf("GC #%lu: marking %p from %p instead\n",
                                     (unsigned long)GC_gc_no, (void *)base,
                                     (void *)source));
    INCR_MARKS(hhdr);
    GC_STORE_BACK_PTR(source, base);
    mark_stack_top = GC_push_obj(base, hhdr, mark_stack_top,
                                 mark_stack_limit);
  } while (0);
  return mark_stack_top;
}

#if defined(PRINT_BLACK_LIST) || defined(KEEP_BACK_PTRS)
# define PUSH_ONE_CHECKED_STACK(p, source) \
        GC_mark_and_push_stack((ptr_t)(p), (ptr_t)(source))
#else
# define PUSH_ONE_CHECKED_STACK(p, source) \
        GC_mark_and_push_stack((ptr_t)(p))
#endif

/*
 * Push a single value onto mark stack. Mark from the object pointed to by p.
 * Invoke FIXUP_POINTER() before any further processing.
 * P is considered valid even if it is an interior pointer.
 * Previously marked objects are not pushed.  Hence we make progress even
 * if the mark stack overflows.
 */

#ifdef NEED_FIXUP_POINTER
    /* Try both the raw version and the fixed up one.   */
# define GC_PUSH_ONE_STACK(p, source) \
    do { \
      word pp = (word)(p); \
      \
      if ((word)(p) >= (word)GC_least_plausible_heap_addr \
          && (word)(p) < (word)GC_greatest_plausible_heap_addr) { \
         PUSH_ONE_CHECKED_STACK(p, source); \
      } \
      FIXUP_POINTER(pp); \
      if (pp >= (word)GC_least_plausible_heap_addr \
          && pp < (word)GC_greatest_plausible_heap_addr) { \
         PUSH_ONE_CHECKED_STACK(pp, source); \
      } \
    } while (0)
#else /* !NEED_FIXUP_POINTER */
# define GC_PUSH_ONE_STACK(p, source) \
    do { \
      if ((word)(p) >= (word)GC_least_plausible_heap_addr \
          && (word)(p) < (word)GC_greatest_plausible_heap_addr) { \
         PUSH_ONE_CHECKED_STACK(p, source); \
      } \
    } while (0)
#endif

/* As above, but interior pointer recognition as for normal heap pointers. */
#define GC_PUSH_ONE_HEAP(p,source,mark_stack_top) \
    do { \
      FIXUP_POINTER(p); \
      if ((word)(p) >= (word)GC_least_plausible_heap_addr \
          && (word)(p) < (word)GC_greatest_plausible_heap_addr) \
        mark_stack_top = GC_mark_and_push((void *)(p), mark_stack_top, \
                                GC_mark_stack_limit, (void * *)(source)); \
    } while (0)

/* Mark starting at mark stack entry top (incl.) down to        */
/* mark stack entry bottom (incl.).  Stop after performing      */
/* about one page worth of work.  Return the new mark stack     */
/* top entry.                                                   */
GC_INNER mse * GC_mark_from(mse * top, mse * bottom, mse *limit);

#define MARK_FROM_MARK_STACK() \
        GC_mark_stack_top = GC_mark_from(GC_mark_stack_top, \
                                         GC_mark_stack, \
                                         GC_mark_stack + GC_mark_stack_size);

#define GC_mark_stack_empty() ((word)GC_mark_stack_top < (word)GC_mark_stack)

/*
 * Mark from one finalizable object using the specified
 * mark proc. May not mark the object pointed to by
 * real_ptr. That is the job of the caller, if appropriate.
 * Note that this is called with the mutator running, but
 * with us holding the allocation lock.  This is safe only if the
 * mutator needs the allocation lock to reveal hidden pointers.
 * FIXME: Why do we need the GC_mark_state test below?
 */
#define GC_MARK_FO(real_ptr, mark_proc) \
  do { \
    (*(mark_proc))(real_ptr); \
    while (!GC_mark_stack_empty()) MARK_FROM_MARK_STACK(); \
    if (GC_mark_state != MS_NONE) { \
        GC_set_mark_bit(real_ptr); \
        while (!GC_mark_some((ptr_t)0)) { /* empty */ } \
    } \
  } while (0)

                                /* Current state of marking, as follows.*/

                                /* We say something is dirty if it was  */
                                /* written since the last time we       */
                                /* retrieved dirty bits.  We say it's   */
                                /* grungy if it was marked dirty in the */
                                /* last set of bits we retrieved.       */

                                /* Invariant "I": all roots and marked  */
                                /* objects p are either dirty, or point */
                                /* to objects q that are either marked  */
                                /* or a pointer to q appears in a range */
                                /* on the mark stack.                   */

#define MS_NONE 0               /* No marking in progress. "I" holds.   */
                                /* Mark stack is empty.                 */

#define MS_PUSH_RESCUERS 1      /* Rescuing objects are currently       */
                                /* being pushed.  "I" holds, except     */
                                /* that grungy roots may point to       */
                                /* unmarked objects, as may marked      */
                                /* grungy objects above GC_scan_ptr.    */

#define MS_PUSH_UNCOLLECTABLE 2 /* "I" holds, except that marked        */
                                /* uncollectible objects above          */
                                /* GC_scan_ptr may point to unmarked    */
                                /* objects.  Roots may point to         */
                                /* unmarked objects.                    */

#define MS_ROOTS_PUSHED 3       /* "I" holds, mark stack may be nonempty. */

#define MS_PARTIALLY_INVALID 4  /* "I" may not hold, e.g. because of    */
                                /* the mark stack overflow.  However,   */
                                /* marked heap objects below            */
                                /* GC_scan_ptr point to marked or       */
                                /* stacked objects.                     */

#define MS_INVALID 5            /* "I" may not hold.                    */

EXTERN_C_END

#endif  /* GC_PMARK_H */
