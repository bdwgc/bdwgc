/*
 * Copyright 1988, 1989 Hans-J. Boehm, Alan J. Demers
 * Copyright (c) 1991-1996 by Xerox Corporation.  All rights reserved.
 * Copyright (c) 1998 by Silicon Graphics.  All rights reserved.
 * Copyright (c) 1999-2004 Hewlett-Packard Development Company, L.P.
 * Copyright (c) 2008-2025 Ivan Maidanski
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

#include "private/gc_priv.h"

#include <stdio.h>
#if !defined(MACOS) && !defined(MSWINCE)
# include <signal.h>
# if !defined(GC_NO_TYPES) && !defined(SN_TARGET_PSP2) \
     && !defined(__CC_ARM)
#   include <sys/types.h>
# endif
#endif

/*
 * Separate free lists are maintained for different sized objects
 * up to MAXOBJBYTES.
 * The call GC_allocobj(i,k) ensures that the freelist for
 * kind k objects of size i points to a non-empty
 * free list. It returns a pointer to the first entry on the free list.
 * In a single-threaded world, GC_allocobj may be called to allocate
 * an object of small size lb (and NORMAL kind) as follows
 * (GC_generic_malloc_inner is a wrapper over GC_allocobj which also
 * fills in GC_size_map if needed):
 *
 *   lg = GC_size_map[lb];
 *   op = GC_objfreelist[lg];
 *   if (NULL == op) {
 *     op = GC_generic_malloc_inner(lb, NORMAL);
 *   } else {
 *     GC_objfreelist[lg] = obj_link(op);
 *     GC_bytes_allocd += GRANULES_TO_BYTES((word)lg);
 *   }
 *
 * Note that this is very fast if the free list is non-empty; it should
 * only involve the execution of 4 or 5 simple instructions.
 * All composite objects on freelists are cleared, except for
 * their first word.
 */

/*
 * The allocator uses GC_allochblk to allocate large chunks of objects.
 * These chunks all start on addresses which are multiples of
 * HBLKSIZE.   Each allocated chunk has an associated header,
 * which can be located quickly based on the address of the chunk.
 * (See headers.c for details.)
 * This makes it possible to check quickly whether an
 * arbitrary address corresponds to an object administered by the
 * allocator.
 */

word GC_non_gc_bytes = 0;  /* Number of bytes not intended to be collected */

word GC_gc_no = 0;

#ifndef NO_CLOCK
  static unsigned long full_gc_total_time = 0; /* in ms, may wrap */
  static unsigned full_gc_total_ns_frac = 0; /* fraction of 1 ms */
  static GC_bool measure_performance = FALSE;
                /* Do performance measurements if set to true (e.g.,    */
                /* accumulation of the total time of full collections). */

  GC_API void GC_CALL GC_start_performance_measurement(void)
  {
    measure_performance = TRUE;
  }

  GC_API unsigned long GC_CALL GC_get_full_gc_total_time(void)
  {
    return full_gc_total_time;
  }
#endif /* !NO_CLOCK */

#ifndef GC_DISABLE_INCREMENTAL
  GC_INNER GC_bool GC_incremental = FALSE; /* By default, stop the world. */
  STATIC GC_bool GC_should_start_incremental_collection = FALSE;
#endif

GC_API int GC_CALL GC_is_incremental_mode(void)
{
  return (int)GC_incremental;
}

#ifdef THREADS
  int GC_parallel = FALSE;      /* By default, parallel GC is off.      */
#endif

#if defined(GC_FULL_FREQ) && !defined(CPPCHECK)
  int GC_full_freq = GC_FULL_FREQ;
#else
  int GC_full_freq = 19;   /* Every 20th collection is a full   */
                           /* collection, whether we need it    */
                           /* or not.                           */
#endif

STATIC GC_bool GC_need_full_gc = FALSE;
                           /* Need full GC due to heap growth.  */

#ifdef THREAD_LOCAL_ALLOC
  GC_INNER GC_bool GC_world_stopped = FALSE;
#endif

STATIC GC_bool GC_disable_automatic_collection = FALSE;

GC_API void GC_CALL GC_set_disable_automatic_collection(int value)
{
  DCL_LOCK_STATE;

  LOCK();
  GC_disable_automatic_collection = (GC_bool)value;
  UNLOCK();
}

GC_API int GC_CALL GC_get_disable_automatic_collection(void)
{
  int value;
  DCL_LOCK_STATE;

  LOCK();
  value = (int)GC_disable_automatic_collection;
  UNLOCK();
  return value;
}

STATIC word GC_used_heap_size_after_full = 0;

/* GC_copyright symbol is externally visible. */
EXTERN_C_BEGIN
extern const char * const GC_copyright[];
EXTERN_C_END
const char * const GC_copyright[] =
{"Copyright 1988, 1989 Hans-J. Boehm and Alan J. Demers ",
"Copyright (c) 1991-1995 by Xerox Corporation.  All rights reserved. ",
"Copyright (c) 1996-1998 by Silicon Graphics.  All rights reserved. ",
"Copyright (c) 1999-2009 by Hewlett-Packard Company.  All rights reserved. ",
"Copyright (c) 2008-2021 Ivan Maidanski ",
"THIS MATERIAL IS PROVIDED AS IS, WITH ABSOLUTELY NO WARRANTY",
" EXPRESSED OR IMPLIED.  ANY USE IS AT YOUR OWN RISK.",
"See source code for details." };

/* Version macros are now defined in gc_version.h, which is included by */
/* gc.h, which is included by gc_priv.h.                                */
#ifndef GC_NO_VERSION_VAR
  EXTERN_C_BEGIN
  extern const unsigned GC_version;
  EXTERN_C_END
  const unsigned GC_version = ((GC_VERSION_MAJOR << 16) |
                        (GC_VERSION_MINOR << 8) | GC_VERSION_MICRO);
#endif

GC_API unsigned GC_CALL GC_get_version(void)
{
  return (GC_VERSION_MAJOR << 16) | (GC_VERSION_MINOR << 8) |
          GC_VERSION_MICRO;
}

/* some more variables */

#ifdef GC_DONT_EXPAND
  int GC_dont_expand = TRUE;
#else
  int GC_dont_expand = FALSE;
#endif

#if defined(GC_FREE_SPACE_DIVISOR) && !defined(CPPCHECK)
  word GC_free_space_divisor = GC_FREE_SPACE_DIVISOR; /* must be > 0 */
#else
  word GC_free_space_divisor = 3;
#endif

GC_INNER int GC_CALLBACK GC_never_stop_func(void)
{
  return(0);
}

#if defined(GC_TIME_LIMIT) && !defined(CPPCHECK)
  unsigned long GC_time_limit = GC_TIME_LIMIT;
                           /* We try to keep pause times from exceeding  */
                           /* this by much. In milliseconds.             */
#elif defined(PARALLEL_MARK)
  unsigned long GC_time_limit = GC_TIME_UNLIMITED;
                        /* The parallel marker cannot be interrupted for */
                        /* now, so the time limit is absent by default.  */
#else
  unsigned long GC_time_limit = 50;
#endif

#ifndef NO_CLOCK
  STATIC unsigned long GC_time_lim_nsec = 0;
                        /* The nanoseconds add-on to GC_time_limit      */
                        /* value.  Not updated by GC_set_time_limit().  */
                        /* Ignored if the value of GC_time_limit is     */
                        /* GC_TIME_UNLIMITED.                           */

# define TV_NSEC_LIMIT (1000UL * 1000) /* amount of nanoseconds in 1 ms */

  GC_API void GC_CALL GC_set_time_limit_tv(struct GC_timeval_s tv)
  {
    GC_ASSERT(tv.tv_ms <= GC_TIME_UNLIMITED);
    GC_ASSERT(tv.tv_nsec < TV_NSEC_LIMIT);
    GC_time_limit = tv.tv_ms;
    GC_time_lim_nsec = tv.tv_nsec;
  }

  GC_API struct GC_timeval_s GC_CALL GC_get_time_limit_tv(void)
  {
    struct GC_timeval_s tv;

    tv.tv_ms = GC_time_limit;
    tv.tv_nsec = GC_time_lim_nsec;
    return tv;
  }

  STATIC CLOCK_TYPE GC_start_time = CLOCK_TYPE_INITIALIZER;
                                /* Time at which we stopped world.      */
                                /* used only in GC_timeout_stop_func.   */
#endif /* !NO_CLOCK */

STATIC int GC_n_attempts = 0;   /* Number of attempts at finishing      */
                                /* collection within GC_time_limit.     */

STATIC GC_stop_func GC_default_stop_func = GC_never_stop_func;
                                /* accessed holding the lock.           */

GC_API void GC_CALL GC_set_stop_func(GC_stop_func stop_func)
{
  DCL_LOCK_STATE;
  GC_ASSERT(NONNULL_ARG_NOT_NULL(stop_func));
  LOCK();
  GC_default_stop_func = stop_func;
  UNLOCK();
}

GC_API GC_stop_func GC_CALL GC_get_stop_func(void)
{
  GC_stop_func stop_func;
  DCL_LOCK_STATE;
  LOCK();
  stop_func = GC_default_stop_func;
  UNLOCK();
  return stop_func;
}

#if defined(GC_DISABLE_INCREMENTAL) || defined(NO_CLOCK)
# define GC_timeout_stop_func GC_default_stop_func
#else
  STATIC int GC_CALLBACK GC_timeout_stop_func (void)
  {
    CLOCK_TYPE current_time;
    static unsigned count = 0;
    unsigned long time_diff, nsec_diff;

    if ((*GC_default_stop_func)())
      return(1);

    if ((count++ & 3) != 0) return(0);
    GET_TIME(current_time);
    time_diff = MS_TIME_DIFF(current_time,GC_start_time);
    nsec_diff = NS_FRAC_TIME_DIFF(current_time, GC_start_time);
#   if defined(CPPCHECK)
      GC_noop1((word)&nsec_diff);
#   endif
    if (time_diff >= GC_time_limit
        && (time_diff > GC_time_limit || nsec_diff >= GC_time_lim_nsec)) {
      GC_COND_LOG_PRINTF("Abandoning stopped marking after %lu ms %lu ns"
                         " (attempt %d)\n",
                         time_diff, nsec_diff, GC_n_attempts);
      return 1;
    }
    return(0);
  }
#endif /* !GC_DISABLE_INCREMENTAL */

#ifdef THREADS
  GC_INNER word GC_total_stacksize = 0; /* updated on every push_all_stacks */
#endif

static size_t min_bytes_allocd_minimum = 1;
                        /* The lowest value returned by min_bytes_allocd(). */

GC_API void GC_CALL GC_set_min_bytes_allocd(size_t value)
{
    GC_ASSERT(value > 0);
    min_bytes_allocd_minimum = value;
}

GC_API size_t GC_CALL GC_get_min_bytes_allocd(void)
{
    return min_bytes_allocd_minimum;
}

/* Return the minimum number of bytes that must be allocated between    */
/* collections to amortize the collection cost.  Should be non-zero.    */
static word min_bytes_allocd(void)
{
    word result;
    word stack_size;
    word total_root_size;       /* includes double stack size,  */
                                /* since the stack is expensive */
                                /* to scan.                     */
    word scan_size;             /* Estimate of memory to be scanned     */
                                /* during normal GC.                    */

#   ifdef THREADS
      if (GC_need_to_lock) {
        /* We are multi-threaded... */
        stack_size = GC_total_stacksize;
        /* For now, we just use the value computed during the latest GC. */
#       ifdef DEBUG_THREADS
          GC_log_printf("Total stacks size: %lu\n",
                        (unsigned long)stack_size);
#       endif
      } else
#   endif
    /* else*/ {
#     ifdef STACK_NOT_SCANNED
        stack_size = 0;
#     elif defined(STACK_GROWS_UP)
        stack_size = GC_approx_sp() - GC_stackbottom;
#     else
        stack_size = GC_stackbottom - GC_approx_sp();
#     endif
    }

    total_root_size = 2 * stack_size + GC_root_size;
    scan_size = 2 * GC_composite_in_use + GC_atomic_in_use / 4
                + total_root_size;
    result = scan_size / GC_free_space_divisor;
    if (GC_incremental) {
      result /= 2;
    }
    return result > min_bytes_allocd_minimum
            ? result : min_bytes_allocd_minimum;
}

STATIC word GC_non_gc_bytes_at_gc = 0;
                /* Number of explicitly managed bytes of storage        */
                /* at last collection.                                  */

/* Return the number of bytes allocated, adjusted for explicit storage  */
/* management, etc..  This number is used in deciding when to trigger   */
/* collections.                                                         */
STATIC word GC_adj_bytes_allocd(void)
{
    signed_word result;
    signed_word expl_managed = (signed_word)GC_non_gc_bytes
                                - (signed_word)GC_non_gc_bytes_at_gc;

    /* Don't count what was explicitly freed, or newly allocated for    */
    /* explicit management.  Note that deallocating an explicitly       */
    /* managed object should not alter result, assuming the client      */
    /* is playing by the rules.                                         */
    result = (signed_word)GC_bytes_allocd
             + (signed_word)GC_bytes_dropped
             - (signed_word)GC_bytes_freed
             + (signed_word)GC_finalizer_bytes_freed
             - expl_managed;
    if (result > (signed_word)GC_bytes_allocd) {
        result = GC_bytes_allocd;
        /* probably client bug or unfortunate scheduling */
    }
    result += GC_bytes_finalized;
        /* We count objects enqueued for finalization as though they    */
        /* had been reallocated this round. Finalization is user        */
        /* visible progress.  And if we don't count this, we have       */
        /* stability problems for programs that finalize all objects.   */
    if (result < (signed_word)(GC_bytes_allocd >> 3)) {
        /* Always count at least 1/8 of the allocations.  We don't want */
        /* to collect too infrequently, since that would inhibit        */
        /* coalescing of free storage blocks.                           */
        /* This also makes us partially robust against client bugs.     */
        return(GC_bytes_allocd >> 3);
    } else {
        return(result);
    }
}


/* Clear up a few frames worth of garbage left at the top of the stack. */
/* This is used to prevent us from accidentally treating garbage left   */
/* on the stack by other parts of the collector as roots.  This         */
/* differs from the code in misc.c, which actually tries to keep the    */
/* stack clear of long-lived, client-generated garbage.                 */
STATIC void GC_clear_a_few_frames(void)
{
#   ifndef CLEAR_NWORDS
#     define CLEAR_NWORDS 64
#   endif
    volatile word frames[CLEAR_NWORDS];
    BZERO((word *)frames, CLEAR_NWORDS * sizeof(word));
}

/* Heap size at which we need a collection to avoid expanding past      */
/* limits used by blacklisting.                                         */
STATIC word GC_collect_at_heapsize = GC_WORD_MAX;

GC_API void GC_CALL GC_start_incremental_collection(void)
{
# ifndef GC_DISABLE_INCREMENTAL
    DCL_LOCK_STATE;

    if (!GC_incremental) return;
    LOCK();
    GC_should_start_incremental_collection = TRUE;
    if (!GC_dont_gc) {
      ENTER_GC();
      GC_collect_a_little_inner(1);
      EXIT_GC();
    }
    UNLOCK();
# endif
}

/* Have we allocated enough to amortize a collection? */
GC_INNER GC_bool GC_should_collect(void)
{
    static word last_min_bytes_allocd;
    static word last_gc_no;

    GC_ASSERT(I_HOLD_LOCK());
    if (last_gc_no != GC_gc_no) {
      last_min_bytes_allocd = min_bytes_allocd();
      last_gc_no = GC_gc_no;
    }
# ifndef GC_DISABLE_INCREMENTAL
    if (GC_should_start_incremental_collection) {
      GC_should_start_incremental_collection = FALSE;
      return TRUE;
    }
# endif
    if (GC_disable_automatic_collection) return FALSE;

    return(GC_adj_bytes_allocd() >= last_min_bytes_allocd
           || GC_heapsize >= GC_collect_at_heapsize);
}

/* STATIC */ GC_start_callback_proc GC_start_call_back = 0;
                        /* Called at start of full collections.         */
                        /* Not called if 0.  Called with the allocation */
                        /* lock held.  Not used by GC itself.           */

GC_API void GC_CALL GC_set_start_callback(GC_start_callback_proc fn)
{
    DCL_LOCK_STATE;
    LOCK();
    GC_start_call_back = fn;
    UNLOCK();
}

GC_API GC_start_callback_proc GC_CALL GC_get_start_callback(void)
{
    GC_start_callback_proc fn;
    DCL_LOCK_STATE;
    LOCK();
    fn = GC_start_call_back;
    UNLOCK();
    return fn;
}

GC_INLINE void GC_notify_full_gc(void)
{
    if (GC_start_call_back != 0) {
        (*GC_start_call_back)();
    }
}

STATIC GC_bool GC_is_full_gc = FALSE;

STATIC GC_bool GC_stopped_mark(GC_stop_func stop_func);
STATIC void GC_finish_collection(void);

/*
 * Initiate a garbage collection if appropriate.
 * Choose judiciously
 * between partial, full, and stop-world collections.
 */
STATIC void GC_maybe_gc(void)
{
    GC_ASSERT(I_HOLD_LOCK());
    ASSERT_CANCEL_DISABLED();
    if (GC_should_collect()) {
        static int n_partial_gcs = 0;

        if (!GC_incremental) {
            /* TODO: If possible, GC_default_stop_func should be used here */
            GC_try_to_collect_inner(GC_never_stop_func);
            n_partial_gcs = 0;
            return;
        } else {
#         ifdef PARALLEL_MARK
            if (GC_parallel)
              GC_wait_for_reclaim();
#         endif
          if (GC_need_full_gc || n_partial_gcs >= GC_full_freq) {
            GC_COND_LOG_PRINTF(
                "***>Full mark for collection #%lu after %lu allocd bytes\n",
                (unsigned long)GC_gc_no + 1, (unsigned long)GC_bytes_allocd);
            GC_promote_black_lists();
            (void)GC_reclaim_all((GC_stop_func)0, TRUE);
            GC_notify_full_gc();
            GC_clear_marks();
            n_partial_gcs = 0;
            GC_is_full_gc = TRUE;
          } else {
            n_partial_gcs++;
          }
        }
        /* We try to mark with the world stopped.       */
        /* If we run out of time, this turns into       */
        /* incremental marking.                         */
#       ifndef NO_CLOCK
          if (GC_time_limit != GC_TIME_UNLIMITED) { GET_TIME(GC_start_time); }
#       endif
        /* TODO: If possible, GC_default_stop_func should be    */
        /* used instead of GC_never_stop_func here.             */
        if (GC_stopped_mark(GC_time_limit == GC_TIME_UNLIMITED?
                            GC_never_stop_func : GC_timeout_stop_func)) {
            SAVE_CALLERS_TO_LAST_STACK();
            GC_finish_collection();
        } else {
            if (!GC_is_full_gc) {
                /* Count this as the first attempt */
                GC_n_attempts++;
            }
        }
    }
}

STATIC GC_on_collection_event_proc GC_on_collection_event = 0;

GC_API void GC_CALL GC_set_on_collection_event(GC_on_collection_event_proc fn)
{
    /* fn may be 0 (means no event notifier). */
    DCL_LOCK_STATE;
    LOCK();
    GC_on_collection_event = fn;
    UNLOCK();
}

GC_API GC_on_collection_event_proc GC_CALL GC_get_on_collection_event(void)
{
    GC_on_collection_event_proc fn;
    DCL_LOCK_STATE;
    LOCK();
    fn = GC_on_collection_event;
    UNLOCK();
    return fn;
}

/* Stop the world garbage collection.  If stop_func is not      */
/* GC_never_stop_func then abort if stop_func returns TRUE.     */
/* Return TRUE if we successfully completed the collection.     */
GC_INNER GC_bool GC_try_to_collect_inner(GC_stop_func stop_func)
{
#   ifndef NO_CLOCK
      CLOCK_TYPE start_time = CLOCK_TYPE_INITIALIZER;
      GC_bool start_time_valid;
#   endif

    ASSERT_CANCEL_DISABLED();
    GC_ASSERT(I_HOLD_LOCK());
    if (GC_dont_gc || (*stop_func)()) return FALSE;
    if (GC_on_collection_event)
      GC_on_collection_event(GC_EVENT_START);
    if (GC_incremental && GC_collection_in_progress()) {
      GC_COND_LOG_PRINTF(
            "GC_try_to_collect_inner: finishing collection in progress\n");
      /* Just finish collection already in progress.    */
        while(GC_collection_in_progress()) {
            if ((*stop_func)()) {
              /* TODO: Notify GC_EVENT_ABANDON */
              return(FALSE);
            }
            ENTER_GC();
            GC_collect_a_little_inner(1);
            EXIT_GC();
        }
    }
    GC_notify_full_gc();
#   ifndef NO_CLOCK
      start_time_valid = FALSE;
      if ((GC_print_stats | (int)measure_performance) != 0) {
        if (GC_print_stats)
          GC_log_printf("Initiating full world-stop collection!\n");
        start_time_valid = TRUE;
        GET_TIME(start_time);
      }
#   endif
    GC_promote_black_lists();
    /* Make sure all blocks have been reclaimed, so sweep routines      */
    /* don't see cleared mark bits.                                     */
    /* If we're guaranteed to finish, then this is unnecessary.         */
    /* In the find_leak case, we have to finish to guarantee that       */
    /* previously unmarked objects are not reported as leaks.           */
#       ifdef PARALLEL_MARK
          if (GC_parallel)
            GC_wait_for_reclaim();
#       endif
        if ((GC_find_leak || stop_func != GC_never_stop_func)
            && !GC_reclaim_all(stop_func, FALSE)) {
            /* Aborted.  So far everything is still consistent. */
            /* TODO: Notify GC_EVENT_ABANDON */
            return(FALSE);
        }
    GC_invalidate_mark_state();  /* Flush mark stack.   */
    GC_clear_marks();
    SAVE_CALLERS_TO_LAST_STACK();
    GC_is_full_gc = TRUE;
    if (!GC_stopped_mark(stop_func)) {
      if (!GC_incremental) {
        /* We're partially done and have no way to complete or use      */
        /* current work.  Reestablish invariants as cheaply as          */
        /* possible.                                                    */
        GC_invalidate_mark_state();
        GC_unpromote_black_lists();
      } /* else we claim the world is already still consistent.  We'll  */
        /* finish incrementally.                                        */
      /* TODO: Notify GC_EVENT_ABANDON */
      return(FALSE);
    }
    GC_finish_collection();
#   ifndef NO_CLOCK
      if (start_time_valid) {
        CLOCK_TYPE current_time;
        unsigned long time_diff, ns_frac_diff;

        GET_TIME(current_time);
        time_diff = MS_TIME_DIFF(current_time, start_time);
        ns_frac_diff = NS_FRAC_TIME_DIFF(current_time, start_time);
        if (measure_performance) {
          full_gc_total_time += time_diff; /* may wrap */
          full_gc_total_ns_frac += (unsigned)ns_frac_diff;
          if (full_gc_total_ns_frac >= 1000000U) {
            /* Overflow of the nanoseconds part. */
            full_gc_total_ns_frac -= 1000000U;
            full_gc_total_time++;
          }
        }
        if (GC_print_stats)
          GC_log_printf("Complete collection took %lu ms %lu ns\n",
                        time_diff, ns_frac_diff);
      }
#   endif
    if (GC_on_collection_event)
      GC_on_collection_event(GC_EVENT_END);
    return(TRUE);
}

/* The number of extra calls to GC_mark_some that we have made. */
STATIC int GC_deficit = 0;

/* The default value of GC_rate.        */
#ifndef GC_RATE
# define GC_RATE 10
#endif

/* When GC_collect_a_little_inner() performs n units of GC work, a unit */
/* is intended to touch roughly GC_rate pages.  (But, every once in     */
/* a while, we do more than that.)  This needs to be a fairly large     */
/* number with our current incremental GC strategy, since otherwise we  */
/* allocate too much during GC, and the cleanup gets expensive.         */
STATIC int GC_rate = GC_RATE;

GC_API void GC_CALL GC_set_rate(int value)
{
    GC_ASSERT(value > 0);
    GC_rate = value;
}

GC_API int GC_CALL GC_get_rate(void)
{
    return GC_rate;
}

/* The default maximum number of prior attempts at world stop marking.  */
#ifndef MAX_PRIOR_ATTEMPTS
# define MAX_PRIOR_ATTEMPTS 1
#endif

/* The maximum number of prior attempts at world stop marking.          */
/* A value of 1 means that we finish the second time, no matter how     */
/* long it takes.  Does not count the initial root scan for a full GC.  */
static int max_prior_attempts = MAX_PRIOR_ATTEMPTS;

GC_API void GC_CALL GC_set_max_prior_attempts(int value)
{
    GC_ASSERT(value >= 0);
    max_prior_attempts = value;
}

GC_API int GC_CALL GC_get_max_prior_attempts(void)
{
    return max_prior_attempts;
}

GC_INNER void GC_collect_a_little_inner(int n)
{
    IF_CANCEL(int cancel_state;)

    GC_ASSERT(I_HOLD_LOCK());
    DISABLE_CANCEL(cancel_state);
    if (GC_incremental && GC_collection_in_progress()) {
        int i;
        int max_deficit = GC_rate * n;

#       ifdef PARALLEL_MARK
            if (GC_time_limit != GC_TIME_UNLIMITED)
                GC_parallel_mark_disabled = TRUE;
#       endif
        for (i = GC_deficit; i < max_deficit; i++) {
            if (GC_mark_some(NULL))
                break;
        }
#       ifdef PARALLEL_MARK
            GC_parallel_mark_disabled = FALSE;
#       endif

        if (i < max_deficit && !GC_dont_gc) {
            /* Need to finish a collection.     */
            SAVE_CALLERS_TO_LAST_STACK();
#           ifdef PARALLEL_MARK
                if (GC_parallel)
                    GC_wait_for_reclaim();
#           endif
            if (GC_n_attempts < max_prior_attempts
                && GC_time_limit != GC_TIME_UNLIMITED) {
#               ifndef NO_CLOCK
                    GET_TIME(GC_start_time);
#               endif
                if (GC_stopped_mark(GC_timeout_stop_func)) {
                    GC_finish_collection();
                } else {
                    GC_n_attempts++;
                }
            } else {
                /* TODO: If possible, GC_default_stop_func should be    */
                /* used here.                                           */
                (void)GC_stopped_mark(GC_never_stop_func);
                GC_finish_collection();
            }
        }
        if (GC_deficit > 0) {
            GC_deficit -= max_deficit;
            if (GC_deficit < 0)
                GC_deficit = 0;
        }
    } else if (!GC_dont_gc) {
        GC_maybe_gc();
    }
    RESTORE_CANCEL(cancel_state);
}

GC_INNER void (*GC_check_heap)(void) = 0;
GC_INNER void (*GC_print_all_smashed)(void) = 0;

GC_API int GC_CALL GC_collect_a_little(void)
{
    int result;
    DCL_LOCK_STATE;

    LOCK();
    if (!GC_dont_gc) {
      ENTER_GC();
      GC_collect_a_little_inner(1);
      EXIT_GC();
    }
    result = (int)GC_collection_in_progress();
    UNLOCK();
    if (!result && GC_debugging_started) GC_print_all_smashed();
    return(result);
}

#ifndef NO_CLOCK
  /* Variables for world-stop average delay time statistic computation. */
  /* "divisor" is incremented every world-stop and halved when reached  */
  /* its maximum (or upon "total_time" overflow).                       */
  static unsigned world_stopped_total_time = 0;
  static unsigned world_stopped_total_divisor = 0;
# ifndef MAX_TOTAL_TIME_DIVISOR
    /* We shall not use big values here (so "outdated" delay time       */
    /* values would have less impact on "average" delay time value than */
    /* newer ones).                                                     */
#   define MAX_TOTAL_TIME_DIVISOR 1000
# endif
#endif /* !NO_CLOCK */

#ifdef USE_MUNMAP
# define IF_USE_MUNMAP(x) x
# define COMMA_IF_USE_MUNMAP(x) /* comma */, x
#else
# define IF_USE_MUNMAP(x) /* empty */
# define COMMA_IF_USE_MUNMAP(x) /* empty */
#endif

/*
 * We stop the world and mark from all roots.
 * If stop_func() ever returns TRUE, we may fail and return FALSE.
 * Increment GC_gc_no if we succeed.
 */
STATIC GC_bool GC_stopped_mark(GC_stop_func stop_func)
{
    int i;
#   ifndef NO_CLOCK
      CLOCK_TYPE start_time = CLOCK_TYPE_INITIALIZER;
#   endif

    GC_ASSERT(I_HOLD_LOCK());
#   if !defined(REDIRECT_MALLOC) && defined(USE_WINALLOC)
        GC_add_current_malloc_heap();
#   endif
#   if defined(REGISTER_LIBRARIES_EARLY)
        GC_cond_register_dynamic_libraries();
#   endif

#   ifndef NO_CLOCK
      if (GC_PRINT_STATS_FLAG)
        GET_TIME(start_time);
#   endif

#   if !defined(GC_NO_FINALIZATION) && !defined(GC_TOGGLE_REFS_NOT_NEEDED)
      GC_process_togglerefs();
#   endif
#   ifdef THREADS
      if (GC_on_collection_event)
        GC_on_collection_event(GC_EVENT_PRE_STOP_WORLD);
#   endif
    STOP_WORLD();
#   ifdef THREADS
      if (GC_on_collection_event)
        GC_on_collection_event(GC_EVENT_POST_STOP_WORLD);
#   endif

#   ifdef THREAD_LOCAL_ALLOC
      GC_world_stopped = TRUE;
#   endif
        /* Output blank line for convenience here */
    GC_COND_LOG_PRINTF(
              "\n--> Marking for collection #%lu after %lu allocated bytes\n",
              (unsigned long)GC_gc_no + 1, (unsigned long) GC_bytes_allocd);
#   ifdef MAKE_BACK_GRAPH
      if (GC_print_back_height) {
        GC_build_back_graph();
      }
#   endif

    /* Mark from all roots.  */
        if (GC_on_collection_event)
          GC_on_collection_event(GC_EVENT_MARK_START);

        /* Minimize junk left in my registers and on the stack */
            GC_clear_a_few_frames();
            GC_noop6(0,0,0,0,0,0);

        GC_initiate_gc();
#       ifdef PARALLEL_MARK
          if (stop_func != GC_never_stop_func)
            GC_parallel_mark_disabled = TRUE;
#       endif
        for (i = 0; !(*stop_func)(); i++) {
          if (GC_mark_some(GC_approx_sp())) {
#           ifdef PARALLEL_MARK
              if (GC_parallel && GC_parallel_mark_disabled) {
                GC_COND_LOG_PRINTF("Stopped marking done after %d iterations"
                                   " with disabled parallel marker\n", i);
              }
#           endif
            i = -1;
            break;
          }
        }
#       ifdef PARALLEL_MARK
          GC_parallel_mark_disabled = FALSE;
#       endif

        if (i >= 0) {
          GC_COND_LOG_PRINTF("Abandoned stopped marking after"
                             " %d iterations\n", i);
          GC_deficit = i;       /* Give the mutator a chance.   */
#         ifdef THREAD_LOCAL_ALLOC
            GC_world_stopped = FALSE;
#         endif

#         ifdef THREADS
            if (GC_on_collection_event)
              GC_on_collection_event(GC_EVENT_PRE_START_WORLD);
#         endif

          START_WORLD();

#         ifdef THREADS
            if (GC_on_collection_event)
              GC_on_collection_event(GC_EVENT_POST_START_WORLD);
#         endif

          /* TODO: Notify GC_EVENT_MARK_ABANDON */
          return FALSE;
        }

    GC_gc_no++;
#   ifdef USE_MUNMAP
      GC_ASSERT(GC_heapsize >= GC_unmapped_bytes);
#   endif
    GC_ASSERT(GC_our_mem_bytes >= GC_heapsize);
    GC_DBGLOG_PRINTF("GC #%lu freed %ld bytes, heap %lu KiB ("
                     IF_USE_MUNMAP("+ %lu KiB unmapped ")
                     "+ %lu KiB internal)\n",
                     (unsigned long)GC_gc_no, (long)GC_bytes_found,
                     TO_KiB_UL(GC_heapsize - GC_unmapped_bytes) /*, */
                     COMMA_IF_USE_MUNMAP(TO_KiB_UL(GC_unmapped_bytes)),
                     TO_KiB_UL(GC_our_mem_bytes - GC_heapsize));

    /* Check all debugged objects for consistency */
    if (GC_debugging_started) {
      (*GC_check_heap)();
    }
    if (GC_on_collection_event) {
      GC_on_collection_event(GC_EVENT_MARK_END);
#     ifdef THREADS
        GC_on_collection_event(GC_EVENT_PRE_START_WORLD);
#     endif
    }
#   ifdef THREAD_LOCAL_ALLOC
      GC_world_stopped = FALSE;
#   endif

    START_WORLD();

#   ifdef THREADS
      if (GC_on_collection_event)
        GC_on_collection_event(GC_EVENT_POST_START_WORLD);
#   endif

#   ifndef NO_CLOCK
      if (GC_PRINT_STATS_FLAG) {
        unsigned long time_diff;
        unsigned total_time, divisor;
        CLOCK_TYPE current_time;

        GET_TIME(current_time);
        time_diff = MS_TIME_DIFF(current_time,start_time);

        /* Compute new world-stop delay total time */
        total_time = world_stopped_total_time;
        divisor = world_stopped_total_divisor;
        if ((int)total_time < 0 || divisor >= MAX_TOTAL_TIME_DIVISOR) {
          /* Halve values if overflow occurs */
          total_time >>= 1;
          divisor >>= 1;
        }
        total_time += time_diff < (((unsigned)-1) >> 1) ?
                        (unsigned)time_diff : ((unsigned)-1) >> 1;
        /* Update old world_stopped_total_time and its divisor */
        world_stopped_total_time = total_time;
        world_stopped_total_divisor = ++divisor;

        GC_ASSERT(divisor != 0);
        GC_log_printf("World-stopped marking took %lu ms %lu ns"
                      " (%u ms in average)\n",
                      time_diff, NS_FRAC_TIME_DIFF(current_time, start_time),
                      total_time / divisor);
      }
#   endif
    return(TRUE);
}

/* Set all mark bits for the free list whose first entry is q   */
GC_INNER void GC_set_fl_marks(ptr_t q)
{
    if (q /* != NULL */) { /* CPPCHECK */
      struct hblk *h = HBLKPTR(q);
      struct hblk *last_h = h;
      hdr *hhdr = HDR(h);
      IF_PER_OBJ(word sz = hhdr->hb_sz;)

      for (;;) {
        word bit_no = MARK_BIT_NO((ptr_t)q - (ptr_t)h, sz);

        if (!mark_bit_from_hdr(hhdr, bit_no)) {
          set_mark_bit_from_hdr(hhdr, bit_no);
          ++hhdr -> hb_n_marks;
        }

        q = (ptr_t)obj_link(q);
        if (q == NULL)
          break;

        h = HBLKPTR(q);
        if (h != last_h) {
          last_h = h;
          hhdr = HDR(h);
          IF_PER_OBJ(sz = hhdr->hb_sz;)
        }
      }
    }
}

#if defined(GC_ASSERTIONS) && defined(THREAD_LOCAL_ALLOC)
  /* Check that all mark bits for the free list whose first entry is    */
  /* (*pfreelist) are set.  Check skipped if points to a special value. */
  void GC_check_fl_marks(void **pfreelist)
  {
    /* TODO: There is a data race with GC_FAST_MALLOC_GRANS (which does */
    /* not do atomic updates to the free-list).  The race seems to be   */
    /* harmless, and for now we just skip this check in case of TSan.   */
#   if defined(AO_HAVE_load_acquire_read) && !defined(THREAD_SANITIZER)
      AO_t *list = (AO_t *)AO_load_acquire_read((AO_t *)pfreelist);
                /* Atomic operations are used because the world is running. */
      AO_t *prev;
      AO_t *p;

      if ((word)list <= HBLKSIZE) return;

      prev = (AO_t *)pfreelist;
      for (p = list; p != NULL;) {
        AO_t *next;

        if (!GC_is_marked(p)) {
          ABORT_ARG2("Unmarked local free list entry",
                     ": object %p on list %p", (void *)p, (void *)list);
        }

        /* While traversing the free-list, it re-reads the pointer to   */
        /* the current node before accepting its next pointer and       */
        /* bails out if the latter has changed.  That way, it won't     */
        /* try to follow the pointer which might be been modified       */
        /* after the object was returned to the client.  It might       */
        /* perform the mark-check on the just allocated object but      */
        /* that should be harmless.                                     */
        next = (AO_t *)AO_load_acquire_read(p);
        if (AO_load(prev) != (AO_t)p)
          break;
        prev = p;
        p = next;
      }
#   else
      /* FIXME: Not implemented (just skipped). */
      (void)pfreelist;
#   endif
  }
#endif /* GC_ASSERTIONS && THREAD_LOCAL_ALLOC */

/* Clear all mark bits for the free list whose first entry is q */
/* Decrement GC_bytes_found by number of bytes on free list.    */
STATIC void GC_clear_fl_marks(ptr_t q)
{
      struct hblk *h = HBLKPTR(q);
      struct hblk *last_h = h;
      hdr *hhdr = HDR(h);
      word sz = hhdr->hb_sz; /* Normally set only once. */

      for (;;) {
        word bit_no = MARK_BIT_NO((ptr_t)q - (ptr_t)h, sz);

        if (mark_bit_from_hdr(hhdr, bit_no)) {
          size_t n_marks = hhdr -> hb_n_marks;

          GC_ASSERT(n_marks != 0);
          clear_mark_bit_from_hdr(hhdr, bit_no);
          n_marks--;
#         ifdef PARALLEL_MARK
            /* Appr. count, don't decrement to zero! */
            if (0 != n_marks || !GC_parallel) {
              hhdr -> hb_n_marks = n_marks;
            }
#         else
            hhdr -> hb_n_marks = n_marks;
#         endif
        }
        GC_bytes_found -= sz;

        q = (ptr_t)obj_link(q);
        if (q == NULL)
          break;

        h = HBLKPTR(q);
        if (h != last_h) {
          last_h = h;
          hhdr = HDR(h);
          sz = hhdr->hb_sz;
        }
      }
}

#if defined(GC_ASSERTIONS) && defined(THREAD_LOCAL_ALLOC)
  void GC_check_tls(void);
#endif

GC_on_heap_resize_proc GC_on_heap_resize = 0;

/* Used for logging only. */
GC_INLINE int GC_compute_heap_usage_percent(void)
{
  word used = GC_composite_in_use + GC_atomic_in_use;
  word heap_sz = GC_heapsize - GC_unmapped_bytes;
# if defined(CPPCHECK)
    word limit = (GC_WORD_MAX >> 1) / 50; /* to avoid a false positive */
# else
    const word limit = GC_WORD_MAX / 100;
# endif

  return used >= heap_sz ? 0 : used < limit ?
                (int)((used * 100) / heap_sz) : (int)(used / (heap_sz / 100));
}

/* Finish up a collection.  Assumes mark bits are consistent, lock is   */
/* held, but the world is otherwise running.                            */
STATIC void GC_finish_collection(void)
{
#   ifndef NO_CLOCK
      CLOCK_TYPE start_time = CLOCK_TYPE_INITIALIZER;
      CLOCK_TYPE finalize_time = CLOCK_TYPE_INITIALIZER;
#   endif

    GC_ASSERT(I_HOLD_LOCK());
#   if defined(GC_ASSERTIONS) \
       && defined(THREAD_LOCAL_ALLOC) && !defined(DBG_HDRS_ALL)
        /* Check that we marked some of our own data.           */
        /* TODO: Add more checks. */
        GC_check_tls();
#   endif

#   ifndef NO_CLOCK
      if (GC_print_stats)
        GET_TIME(start_time);
#   endif
    if (GC_on_collection_event)
      GC_on_collection_event(GC_EVENT_RECLAIM_START);

#   ifndef GC_GET_HEAP_USAGE_NOT_NEEDED
      if (GC_bytes_found > 0)
        GC_reclaimed_bytes_before_gc += (word)GC_bytes_found;
#   endif
    GC_bytes_found = 0;
#   if defined(LINUX) && defined(__ELF__) && !defined(SMALL_CONFIG)
        if (GETENV("GC_PRINT_ADDRESS_MAP") != 0) {
          GC_print_address_map();
        }
#   endif
    COND_DUMP;
    if (GC_find_leak) {
      /* Mark all objects on the free list.  All objects should be      */
      /* marked when we're done.                                        */
      word size;        /* current object size  */
      unsigned kind;
      ptr_t q;

      for (kind = 0; kind < GC_n_kinds; kind++) {
        for (size = 1; size <= MAXOBJGRANULES; size++) {
          q = (ptr_t)GC_obj_kinds[kind].ok_freelist[size];
          if (q != NULL)
            GC_set_fl_marks(q);
        }
      }
      GC_start_reclaim(TRUE);
        /* The above just checks; it doesn't really reclaim anything.   */
    }

#   ifndef GC_NO_FINALIZATION
      GC_finalize();
#   endif
#   ifndef NO_CLOCK
      if (GC_print_stats)
        GET_TIME(finalize_time);
#   endif

    if (GC_print_back_height) {
#     ifdef MAKE_BACK_GRAPH
        GC_traverse_back_graph();
#     elif !defined(SMALL_CONFIG)
        GC_err_printf("Back height not available: "
                      "Rebuild collector with -DMAKE_BACK_GRAPH\n");
#     endif
    }

    /* Clear free list mark bits, in case they got accidentally marked   */
    /* (or GC_find_leak is set and they were intentionally marked).      */
    /* Also subtract memory remaining from GC_bytes_found count.         */
    /* Note that composite objects on free list are cleared.             */
    /* Thus accidentally marking a free list is not a problem;  only     */
    /* objects on the list itself will be marked, and that's fixed here. */
    {
      word size;        /* current object size          */
      ptr_t q;          /* pointer to current object    */
      unsigned kind;

      for (kind = 0; kind < GC_n_kinds; kind++) {
        for (size = 1; size <= MAXOBJGRANULES; size++) {
          q = (ptr_t)GC_obj_kinds[kind].ok_freelist[size];
          if (q != NULL)
            GC_clear_fl_marks(q);
        }
      }
    }

    GC_VERBOSE_LOG_PRINTF("Bytes recovered before sweep - f.l. count = %ld\n",
                          (long)GC_bytes_found);

    /* Reconstruct free lists to contain everything not marked */
    GC_start_reclaim(FALSE);
    GC_DBGLOG_PRINTF("In-use heap: %d%% (%lu KiB pointers + %lu KiB other)\n",
                     GC_compute_heap_usage_percent(),
                     TO_KiB_UL(GC_composite_in_use),
                     TO_KiB_UL(GC_atomic_in_use));
    if (GC_is_full_gc) {
        GC_used_heap_size_after_full = USED_HEAP_SIZE;
        GC_need_full_gc = FALSE;
    } else {
        GC_need_full_gc = USED_HEAP_SIZE - GC_used_heap_size_after_full
                            > min_bytes_allocd();
    }

    GC_VERBOSE_LOG_PRINTF("Immediately reclaimed %ld bytes, heapsize:"
                          " %lu bytes" IF_USE_MUNMAP(" (%lu unmapped)") "\n",
                          (long)GC_bytes_found,
                          (unsigned long)GC_heapsize /*, */
                          COMMA_IF_USE_MUNMAP((unsigned long)
                                              GC_unmapped_bytes));

    /* Reset or increment counters for next cycle */
    GC_n_attempts = 0;
    GC_is_full_gc = FALSE;
    GC_bytes_allocd_before_gc += GC_bytes_allocd;
    GC_non_gc_bytes_at_gc = GC_non_gc_bytes;
    GC_bytes_allocd = 0;
    GC_bytes_dropped = 0;
    GC_bytes_freed = 0;
    GC_finalizer_bytes_freed = 0;

    IF_USE_MUNMAP(GC_unmap_old());

    if (GC_on_collection_event)
      GC_on_collection_event(GC_EVENT_RECLAIM_END);
#   ifndef NO_CLOCK
      if (GC_print_stats) {
        CLOCK_TYPE done_time;

        GET_TIME(done_time);
#       if !defined(SMALL_CONFIG) && !defined(GC_NO_FINALIZATION)
          /* A convenient place to output finalization statistics.      */
          GC_print_finalization_stats();
#       endif
        GC_log_printf("Finalize and initiate sweep took %lu ms %lu ns"
                      " + %lu ms %lu ns\n",
                      MS_TIME_DIFF(finalize_time, start_time),
                      NS_FRAC_TIME_DIFF(finalize_time, start_time),
                      MS_TIME_DIFF(done_time, finalize_time),
                      NS_FRAC_TIME_DIFF(done_time, finalize_time));
      }
#   elif !defined(SMALL_CONFIG) && !defined(GC_NO_FINALIZATION)
      if (GC_print_stats)
        GC_print_finalization_stats();
#   endif
}

STATIC word GC_heapsize_at_forced_unmap = 0;
                                /* accessed with the allocation lock held */

/* If stop_func == 0 then GC_default_stop_func is used instead.         */
STATIC GC_bool GC_try_to_collect_general(GC_stop_func stop_func,
                                         GC_bool force_unmap)
{
    GC_bool result;
    IF_USE_MUNMAP(int old_unmap_threshold;)
    IF_CANCEL(int cancel_state;)
    DCL_LOCK_STATE;

    if (!EXPECT(GC_is_initialized, TRUE)) GC_init();
    if (GC_debugging_started) GC_print_all_smashed();
    GC_INVOKE_FINALIZERS();
    LOCK();
    if (force_unmap) {
      /* Record current heap size to make heap growth more conservative */
      /* afterwards (as if the heap is growing from zero size again).   */
      GC_heapsize_at_forced_unmap = GC_heapsize;
    }
    DISABLE_CANCEL(cancel_state);
#   ifdef USE_MUNMAP
      old_unmap_threshold = GC_unmap_threshold;
      if (force_unmap ||
          (GC_force_unmap_on_gcollect && old_unmap_threshold > 0))
        GC_unmap_threshold = 1; /* unmap as much as possible */
#   endif
    ENTER_GC();
    /* Minimize junk left in my registers */
      GC_noop6(0,0,0,0,0,0);
    result = GC_try_to_collect_inner(stop_func != 0 ? stop_func :
                                     GC_default_stop_func);
    EXIT_GC();
    IF_USE_MUNMAP(GC_unmap_threshold = old_unmap_threshold); /* restore */
    RESTORE_CANCEL(cancel_state);
    UNLOCK();
    if (result) {
        if (GC_debugging_started) GC_print_all_smashed();
        GC_INVOKE_FINALIZERS();
    }
    return(result);
}

/* Externally callable routines to invoke full, stop-the-world collection. */

GC_API int GC_CALL GC_try_to_collect(GC_stop_func stop_func)
{
    GC_ASSERT(NONNULL_ARG_NOT_NULL(stop_func));
    return (int)GC_try_to_collect_general(stop_func, FALSE);
}

GC_API void GC_CALL GC_gcollect(void)
{
    /* 0 is passed as stop_func to get GC_default_stop_func value       */
    /* while holding the allocation lock (to prevent data races).       */
    (void)GC_try_to_collect_general(0, FALSE);
    if (get_have_errors())
      GC_print_all_errors();
}

GC_API void GC_CALL GC_gcollect_and_unmap(void)
{
    /* Collect and force memory unmapping to OS. */
    (void)GC_try_to_collect_general(GC_never_stop_func, TRUE);
}

#ifdef USE_PROC_FOR_LIBRARIES
  /* Add HBLKSIZE aligned, GET_MEM-generated block to GC_our_memory. */
  GC_INNER void GC_add_to_our_memory(ptr_t p, size_t bytes)
  {
    GC_ASSERT(p != NULL);
    if (GC_n_memory >= MAX_HEAP_SECTS)
      ABORT("Too many GC-allocated memory sections: Increase MAX_HEAP_SECTS");
    GC_our_memory[GC_n_memory].hs_start = p;
    GC_our_memory[GC_n_memory].hs_bytes = bytes;
    GC_n_memory++;
    GC_our_mem_bytes += bytes;
  }
#endif

/* Use the chunk of memory starting at p of size bytes as part of the heap. */
/* Assumes p is HBLKSIZE aligned, bytes argument is a multiple of HBLKSIZE. */
STATIC void GC_add_to_heap(struct hblk *p, size_t bytes)
{
    hdr * phdr;
    word endp;
    size_t old_capacity = 0;
    void *old_heap_sects = NULL;
#   ifdef GC_ASSERTIONS
      unsigned i;
#   endif

    GC_ASSERT((word)p % HBLKSIZE == 0);
    GC_ASSERT(bytes % HBLKSIZE == 0);
    GC_ASSERT(bytes > 0);
    GC_ASSERT(GC_all_nils != NULL);

    if (GC_n_heap_sects == GC_capacity_heap_sects) {
      /* Allocate new GC_heap_sects with sufficient capacity.   */
#     ifndef INITIAL_HEAP_SECTS
#       define INITIAL_HEAP_SECTS 32
#     endif
      size_t new_capacity = GC_n_heap_sects > 0 ?
                (size_t)GC_n_heap_sects * 2 : INITIAL_HEAP_SECTS;
      void *new_heap_sects =
                GC_scratch_alloc(new_capacity * sizeof(struct HeapSect));

      if (EXPECT(NULL == new_heap_sects, FALSE)) {
        /* Retry with smaller yet sufficient capacity.  */
        new_capacity = (size_t)GC_n_heap_sects + INITIAL_HEAP_SECTS;
        new_heap_sects =
                GC_scratch_alloc(new_capacity * sizeof(struct HeapSect));
        if (NULL == new_heap_sects)
          ABORT("Insufficient memory for heap sections");
      }
      old_capacity = GC_capacity_heap_sects;
      old_heap_sects = GC_heap_sects;
      /* Transfer GC_heap_sects contents to the newly allocated array.  */
      if (GC_n_heap_sects > 0)
        BCOPY(old_heap_sects, new_heap_sects,
              GC_n_heap_sects * sizeof(struct HeapSect));
      GC_capacity_heap_sects = new_capacity;
      GC_heap_sects = (struct HeapSect *)new_heap_sects;
      GC_COND_LOG_PRINTF("Grew heap sections array to %lu elements\n",
                         (unsigned long)new_capacity);
    }

    while ((word)p <= HBLKSIZE) {
        /* Can't handle memory near address zero. */
        ++p;
        bytes -= HBLKSIZE;
        if (0 == bytes) return;
    }
    endp = (word)p + bytes;
    while (endp <= (word)p) {
        /* Address wrapped. */
        bytes -= HBLKSIZE;
        if (0 == bytes) return;
        endp -= HBLKSIZE;
    }
    phdr = GC_install_header(p);
    if (0 == phdr) {
        /* This is extremely unlikely. Can't add it.  This will         */
        /* almost certainly result in a 0 return from the allocator,    */
        /* which is entirely appropriate.                               */
        return;
    }
    GC_ASSERT(endp > (word)p && endp == (word)p + bytes);
#   ifdef GC_ASSERTIONS
      /* Ensure no intersection between sections.       */
      for (i = 0; i < GC_n_heap_sects; i++) {
        word hs_start = (word)GC_heap_sects[i].hs_start;
        word hs_end = hs_start + GC_heap_sects[i].hs_bytes;
        word p_e = (word)p + bytes;

        GC_ASSERT(!((hs_start <= (word)p && (word)p < hs_end)
                    || (hs_start < p_e && p_e <= hs_end)
                    || ((word)p < hs_start && hs_end < p_e)));
      }
#   endif
    GC_heap_sects[GC_n_heap_sects].hs_start = (ptr_t)p;
    GC_heap_sects[GC_n_heap_sects].hs_bytes = bytes;
    GC_n_heap_sects++;
    phdr -> hb_sz = bytes;
    phdr -> hb_flags = 0;
    GC_freehblk(p);
    GC_heapsize += bytes;

    /* Normally the caller calculates a new GC_collect_at_heapsize,
     * but this is also called directly from GC_scratch_recycle_inner, so
     * adjust here. It will be recalculated when called from
     * GC_expand_hp_inner.
     */
    GC_collect_at_heapsize += bytes;
    if (GC_collect_at_heapsize < GC_heapsize /* wrapped */)
       GC_collect_at_heapsize = GC_WORD_MAX;

    if ((word)p <= (word)GC_least_plausible_heap_addr
        || GC_least_plausible_heap_addr == 0) {
        GC_least_plausible_heap_addr = (void *)((ptr_t)p - sizeof(word));
                /* Making it a little smaller than necessary prevents   */
                /* us from getting a false hit from the variable        */
                /* itself.  There's some unintentional reflection       */
                /* here.                                                */
    }
    if ((word)p + bytes >= (word)GC_greatest_plausible_heap_addr) {
        GC_greatest_plausible_heap_addr = (void *)endp;
    }
#   ifdef SET_REAL_HEAP_BOUNDS
      if ((word)p < GC_least_real_heap_addr
          || EXPECT(0 == GC_least_real_heap_addr, FALSE))
        GC_least_real_heap_addr = (word)p - sizeof(word);
      if (endp > GC_greatest_real_heap_addr) {
#       ifdef INCLUDE_LINUX_THREAD_DESCR
          /* Avoid heap intersection with the static data roots. */
          GC_exclude_static_roots_inner((void *)p, (void *)endp);
#       endif
        GC_greatest_real_heap_addr = endp;
      }
#   endif
    GC_handle_protected_regions_limit();
    if (old_capacity > 0) {
#     ifndef GWW_VDB
        /* Recycling may call GC_add_to_heap() again but should not     */
        /* cause resizing of GC_heap_sects.                             */
        GC_scratch_recycle_no_gww(old_heap_sects,
                                  old_capacity * sizeof(struct HeapSect));
#     else
        /* TODO: implement GWW-aware recycling as in alloc_mark_stack */
        GC_noop1((word)old_heap_sects);
#     endif
    }
}

#if !defined(NO_DEBUGGING)
  void GC_print_heap_sects(void)
  {
    unsigned i;

    GC_printf("Total heap size: %lu" IF_USE_MUNMAP(" (%lu unmapped)") "\n",
              (unsigned long)GC_heapsize /*, */
              COMMA_IF_USE_MUNMAP((unsigned long)GC_unmapped_bytes));

    for (i = 0; i < GC_n_heap_sects; i++) {
      ptr_t start = GC_heap_sects[i].hs_start;
      size_t len = GC_heap_sects[i].hs_bytes;
      struct hblk *h;
      unsigned nbl = 0;

      for (h = (struct hblk *)start; (word)h < (word)(start + len); h++) {
        if (GC_is_black_listed(h, HBLKSIZE)) nbl++;
      }
      GC_printf("Section %d from %p to %p %u/%lu blacklisted\n",
                i, (void *)start, (void *)&start[len],
                nbl, (unsigned long)divHBLKSZ(len));
    }
  }
#endif

void * GC_least_plausible_heap_addr = (void *)GC_WORD_MAX;
void * GC_greatest_plausible_heap_addr = 0;

GC_INLINE word GC_max(word x, word y)
{
    return(x > y? x : y);
}

GC_INLINE word GC_min(word x, word y)
{
    return(x < y? x : y);
}

STATIC word GC_max_heapsize = 0;

GC_API void GC_CALL GC_set_max_heap_size(GC_word n)
{
    GC_max_heapsize = n;
}

GC_word GC_max_retries = 0;

GC_INNER void GC_scratch_recycle_inner(void *ptr, size_t bytes)
{
  size_t page_offset;
  size_t displ = 0;
  size_t recycled_bytes;

  if (NULL == ptr) return;

  GC_ASSERT(bytes != 0);
  GC_ASSERT(GC_page_size != 0);
  /* TODO: Assert correct memory flags if GWW_VDB */
  page_offset = (word)ptr & (GC_page_size - 1);
  if (page_offset != 0)
    displ = GC_page_size - page_offset;
  recycled_bytes = bytes > displ ? (bytes - displ) & ~(GC_page_size - 1) : 0;
  GC_COND_LOG_PRINTF("Recycle %lu/%lu scratch-allocated bytes at %p\n",
                (unsigned long)recycled_bytes, (unsigned long)bytes, ptr);
  if (recycled_bytes > 0)
    GC_add_to_heap((struct hblk *)((word)ptr + displ), recycled_bytes);
}

/* This explicitly increases the size of the heap.  It is used          */
/* internally, but may also be invoked from GC_expand_hp by the user.   */
/* The argument is in units of HBLKSIZE (tiny values are rounded up).   */
/* Returns FALSE on failure.                                            */
GC_INNER GC_bool GC_expand_hp_inner(word n)
{
    size_t bytes;
    struct hblk * space;
    word expansion_slop;        /* Number of bytes by which we expect   */
                                /* the heap to expand soon.             */

    GC_ASSERT(I_HOLD_LOCK());
    GC_ASSERT(GC_page_size != 0);
    if (n < MINHINCR) n = MINHINCR;
    bytes = ROUNDUP_PAGESIZE((size_t)n * HBLKSIZE);
    if (GC_max_heapsize != 0
        && (GC_max_heapsize < (word)bytes
            || GC_heapsize > GC_max_heapsize - (word)bytes)) {
        /* Exceeded self-imposed limit */
        return(FALSE);
    }
    space = GET_MEM(bytes);
    if (EXPECT(NULL == space, FALSE)) {
        WARN("Failed to expand heap by %" WARN_PRIuPTR " KiB\n", bytes >> 10);
        return(FALSE);
    }
    GC_add_to_our_memory((ptr_t)space, bytes);
    GC_INFOLOG_PRINTF("Grow heap to %lu KiB after %lu bytes allocated\n",
                      TO_KiB_UL(GC_heapsize + bytes),
                      (unsigned long)GC_bytes_allocd);

    /* Adjust heap limits generously for blacklisting to work better.   */
    /* GC_add_to_heap performs minimal adjustment needed for            */
    /* correctness.                                                     */
    expansion_slop = min_bytes_allocd() + 4 * MAXHINCR * HBLKSIZE;
    if ((GC_last_heap_addr == 0 && !((word)space & SIGNB))
        || (GC_last_heap_addr != 0
            && (word)GC_last_heap_addr < (word)space)) {
        /* Assume the heap is growing up. */
        word new_limit = (word)space + (word)bytes + expansion_slop;
        if (new_limit > (word)space) {
          GC_greatest_plausible_heap_addr =
            (void *)GC_max((word)GC_greatest_plausible_heap_addr,
                           (word)new_limit);
        }
    } else {
        /* Heap is growing down. */
        word new_limit = (word)space - expansion_slop;
        if (new_limit < (word)space) {
          GC_least_plausible_heap_addr =
            (void *)GC_min((word)GC_least_plausible_heap_addr,
                           (word)space - expansion_slop);
        }
    }
    GC_last_heap_addr = (ptr_t)space;

    GC_add_to_heap(space, bytes);

    /* Force GC before we are likely to allocate past expansion_slop.   */
    GC_collect_at_heapsize =
        GC_heapsize + expansion_slop - 2 * MAXHINCR * HBLKSIZE;
    if (GC_collect_at_heapsize < GC_heapsize /* wrapped */)
        GC_collect_at_heapsize = GC_WORD_MAX;
    if (GC_on_heap_resize)
        (*GC_on_heap_resize)(GC_heapsize);

    return(TRUE);
}

/* Really returns a bool, but it's externally visible, so that's clumsy. */
/* The argument is in bytes.  Includes GC_init() call.                   */
GC_API int GC_CALL GC_expand_hp(size_t bytes)
{
    int result;
    DCL_LOCK_STATE;

    if (!EXPECT(GC_is_initialized, TRUE)) GC_init();
    LOCK();
    result = (int)GC_expand_hp_inner(divHBLKSZ((word)bytes));
    if (result) GC_requested_heapsize += bytes;
    UNLOCK();
    return(result);
}

GC_INNER unsigned GC_fail_count = 0;
                        /* How many consecutive GC/expansion failures?  */
                        /* Reset by GC_allochblk.                       */

/* The minimum value of the ratio of allocated bytes since the latest   */
/* GC to the amount of finalizers created since that GC which triggers  */
/* the collection instead heap expansion.  Has no effect in the         */
/* incremental mode.                                                    */
#if defined(GC_ALLOCD_BYTES_PER_FINALIZER) && !defined(CPPCHECK)
  STATIC word GC_allocd_bytes_per_finalizer = GC_ALLOCD_BYTES_PER_FINALIZER;
#else
  STATIC word GC_allocd_bytes_per_finalizer = 10000;
#endif

GC_API void GC_CALL GC_set_allocd_bytes_per_finalizer(GC_word value)
{
  GC_allocd_bytes_per_finalizer = value;
}

GC_API GC_word GC_CALL GC_get_allocd_bytes_per_finalizer(void)
{
  return GC_allocd_bytes_per_finalizer;
}

static word last_fo_entries = 0;
static word last_bytes_finalized = 0;

/* Collect or expand heap in an attempt make the indicated number of    */
/* free blocks available.  Should be called until the blocks are        */
/* available (setting retry value to TRUE unless this is the first call */
/* in a loop) or until it fails by returning FALSE.                     */
GC_INNER GC_bool GC_collect_or_expand(word needed_blocks,
                                      GC_bool ignore_off_page,
                                      GC_bool retry)
{
    GC_bool gc_not_stopped = TRUE;
    word blocks_to_get;
    IF_CANCEL(int cancel_state;)

    GC_ASSERT(I_HOLD_LOCK());
    DISABLE_CANCEL(cancel_state);
    if (!GC_incremental && !GC_dont_gc &&
        ((GC_dont_expand && GC_bytes_allocd > 0)
         || (GC_fo_entries > last_fo_entries
             && (last_bytes_finalized | GC_bytes_finalized) != 0
             && (GC_fo_entries - last_fo_entries)
                * GC_allocd_bytes_per_finalizer > GC_bytes_allocd)
         || GC_should_collect())) {
      /* Try to do a full collection using 'default' stop_func (unless  */
      /* nothing has been allocated since the latest collection or heap */
      /* expansion is disabled).                                        */
      gc_not_stopped = GC_try_to_collect_inner(
                        GC_bytes_allocd > 0 && (!GC_dont_expand || !retry) ?
                        GC_default_stop_func : GC_never_stop_func);
      if (gc_not_stopped == TRUE || !retry) {
        /* Either the collection hasn't been aborted or this is the     */
        /* first attempt (in a loop).                                   */
        last_fo_entries = GC_fo_entries;
        last_bytes_finalized = GC_bytes_finalized;
        RESTORE_CANCEL(cancel_state);
        return(TRUE);
      }
    }

    blocks_to_get = (GC_heapsize - GC_heapsize_at_forced_unmap)
                        / (HBLKSIZE * GC_free_space_divisor)
                    + needed_blocks;
    if (blocks_to_get > MAXHINCR) {
      word slop;

      /* Get the minimum required to make it likely that we can satisfy */
      /* the current request in the presence of black-listing.          */
      /* This will probably be more than MAXHINCR.                      */
      if (ignore_off_page) {
        slop = 4;
      } else {
        slop = 2 * divHBLKSZ(BL_LIMIT);
        if (slop > needed_blocks) slop = needed_blocks;
      }
      if (needed_blocks + slop > MAXHINCR) {
        blocks_to_get = needed_blocks + slop;
      } else {
        blocks_to_get = MAXHINCR;
      }
      if (blocks_to_get > divHBLKSZ(GC_WORD_MAX))
        blocks_to_get = divHBLKSZ(GC_WORD_MAX);
    }

    if (!GC_expand_hp_inner(blocks_to_get)
        && (blocks_to_get == needed_blocks
            || !GC_expand_hp_inner(needed_blocks))) {
      if (gc_not_stopped == FALSE) {
        /* Don't increment GC_fail_count here (and no warning).     */
        GC_gcollect_inner();
        GC_ASSERT(GC_bytes_allocd == 0);
      } else if (GC_fail_count++ < GC_max_retries) {
        WARN("Out of Memory!  Trying to continue...\n", 0);
        GC_gcollect_inner();
      } else {
#       if !defined(AMIGA) || !defined(GC_AMIGA_FASTALLOC)
#         ifdef USE_MUNMAP
            GC_ASSERT(GC_heapsize >= GC_unmapped_bytes);
#         endif
          WARN("Out of Memory! Heap size: %" WARN_PRIuPTR " MiB."
               " Returning NULL!\n", (GC_heapsize - GC_unmapped_bytes) >> 20);
#       endif
        RESTORE_CANCEL(cancel_state);
        return(FALSE);
      }
    } else if (GC_fail_count) {
      GC_COND_LOG_PRINTF("Memory available again...\n");
    }
    RESTORE_CANCEL(cancel_state);
    return(TRUE);
}

/*
 * Make sure the object free list for size gran (in granules) is not empty.
 * Return a pointer to the first object on the free list.
 * The object MUST BE REMOVED FROM THE FREE LIST BY THE CALLER.
 */
GC_INNER ptr_t GC_allocobj(size_t gran, int kind)
{
    void ** flh = &(GC_obj_kinds[kind].ok_freelist[gran]);
    GC_bool tried_minor = FALSE;
    GC_bool retry = FALSE;

    GC_ASSERT(I_HOLD_LOCK());
    if (gran == 0) return(0);

    while (*flh == 0) {
      ENTER_GC();
#     ifndef GC_DISABLE_INCREMENTAL
        if (GC_incremental && GC_time_limit != GC_TIME_UNLIMITED
            && !GC_dont_gc) {
          /* True incremental mode, not just generational.      */
          /* Do our share of marking work.                      */
          GC_collect_a_little_inner(1);
        }
#     endif
      /* Sweep blocks for objects of this size */
        GC_ASSERT(!GC_is_full_gc
                  || NULL == GC_obj_kinds[kind].ok_reclaim_list
                  || NULL == GC_obj_kinds[kind].ok_reclaim_list[gran]);
        GC_continue_reclaim(gran, kind);
      EXIT_GC();
#     if defined(CPPCHECK)
        GC_noop1((word)&flh);
#     endif
      if (NULL == *flh) {
        GC_new_hblk(gran, kind);
#       if defined(CPPCHECK)
          GC_noop1((word)&flh);
#       endif
        if (NULL == *flh) {
          ENTER_GC();
          if (GC_incremental && GC_time_limit == GC_TIME_UNLIMITED
              && !tried_minor && !GC_dont_gc) {
            GC_collect_a_little_inner(1);
            tried_minor = TRUE;
          } else {
            if (!GC_collect_or_expand(1, FALSE, retry)) {
              EXIT_GC();
              return(0);
            }
            retry = TRUE;
          }
          EXIT_GC();
        }
      }
    }
    /* Successful allocation; reset failure count.      */
    GC_fail_count = 0;

    return (ptr_t)(*flh);
}
