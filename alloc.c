/*
 * Copyright (c) 1988-1989 Hans-J. Boehm, Alan J. Demers
 * Copyright (c) 1991-1996 by Xerox Corporation.  All rights reserved.
 * Copyright (c) 1996-1999 by Silicon Graphics.  All rights reserved.
 * Copyright (c) 1999-2011 Hewlett-Packard Development Company, L.P.
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

#include "private/gc_priv.h"

/*
 * Separate free lists are maintained for different sized objects up
 * to `MAXOBJBYTES`.
 * The call `GC_allocobj(lg, kind)` ensures that the free list for the given
 * kind of objects of the given size in granules is a nonempty one.
 * It returns a pointer to the first entry on the free list.
 * In a single-threaded world, `GC_allocobj` may be called to allocate
 * an object of small size `lb` (and `NORMAL` kind) as follows
 * (`GC_generic_malloc_inner` is a wrapper over `GC_allocobj` which also
 * fills in `GC_size_map` if needed):
 *
 * ```
 *   lg = GC_size_map[lb];
 *   op = GC_objfreelist[lg];
 *   if (NULL == op) {
 *     op = GC_generic_malloc_inner(lb, NORMAL, 0);
 *   } else {
 *     GC_objfreelist[lg] = obj_link(op);
 *     GC_bytes_allocd += GRANULES_TO_BYTES((word)lg);
 *   }
 * ```
 *
 * Note that this is very fast if the free list is not empty; it should
 * only involve the execution of 4 or 5 simple instructions.
 * All composite objects on freelists are cleared, except for
 * their first "pointer-sized" word.
 */

/*
 * The allocator uses `GC_allochblk` to allocate large chunks of objects.
 * These chunks all start on addresses that are multiples of `HBLKSIZE`.
 * Each allocated chunk has an associated header, which can be located
 * quickly based on the address of the chunk.  This makes it possible
 * to check quickly whether an arbitrary address corresponds to an object
 * administered by the allocator.  (See `headers.c` file for details.)
 */

/* Number of bytes not intended to be collected. */
word GC_non_gc_bytes = 0;

word GC_gc_no = 0;

#ifndef NO_CLOCK

GC_API void GC_CALL
GC_start_performance_measurement(void)
{
  GC_measure_performance = TRUE;
}

GC_API unsigned long GC_CALL
GC_get_full_gc_total_time(void)
{
  return GC_full_gc_total_time;
}

GC_API unsigned long GC_CALL
GC_get_stopped_mark_total_time(void)
{
  return GC_stopped_mark_total_time;
}

#  ifndef MAX_TOTAL_TIME_DIVISOR
/*
 * We shall not use big values here (so "outdated" delay time values would
 * have less impact on "average" delay time value than newer ones).
 */
#    define MAX_TOTAL_TIME_DIVISOR 1000
#  endif

GC_API unsigned long GC_CALL
GC_get_avg_stopped_mark_time_ns(void)
{
  unsigned long total_time;
  unsigned divisor;

  READER_LOCK();
  total_time = (unsigned long)GC_world_stopped_total_time;
  divisor = GC_world_stopped_total_divisor;
  READER_UNLOCK();
  if (0 == divisor) {
    GC_ASSERT(0 == total_time);
    /*
     * No world-stopped collection has occurred since the start of
     * performance measurements.
     */
    return 0;
  }

  /* Halve values to prevent overflow during the multiplication. */
  for (; total_time > ~0UL / (1000UL * 1000); total_time >>= 1) {
    divisor >>= 1;
    if (UNLIKELY(0 == divisor)) {
      /* The actual result is larger than representable value. */
      return ~0UL;
    }
  }

  return total_time * (1000UL * 1000) / divisor;
}

#endif /* !NO_CLOCK */

GC_API int GC_CALL
GC_is_incremental_mode(void)
{
  return (int)GC_incremental;
}

#ifdef THREADS
int GC_parallel = FALSE; /*< parallel collection is off by default */
#endif

#if defined(GC_FULL_FREQ) && !defined(CPPCHECK)
int GC_full_freq = GC_FULL_FREQ;
#else
/*
 * Every `GC_full_freq + 1` collection is a full collection, whether we
 * need it or not.
 */
int GC_full_freq = 19;
#endif

#ifdef THREAD_LOCAL_ALLOC
GC_INNER GC_bool GC_world_stopped = FALSE;
#endif

STATIC GC_bool GC_disable_automatic_collection = FALSE;

GC_API void GC_CALL
GC_set_disable_automatic_collection(int value)
{
  LOCK();
  GC_disable_automatic_collection = (GC_bool)value;
  UNLOCK();
}

GC_API int GC_CALL
GC_get_disable_automatic_collection(void)
{
  int value;

  READER_LOCK();
  value = (int)GC_disable_automatic_collection;
  READER_UNLOCK();
  return value;
}

/*
 * The version macros are now defined in `gc_version.h` file, which is
 * included by `gc.h` file, which, in turn, is included by `gc_priv.h` file.
 */
#ifndef GC_NO_VERSION_VAR
EXTERN_C_BEGIN
extern const GC_VERSION_VAL_T GC_version;
EXTERN_C_END

const GC_VERSION_VAL_T GC_version = ((GC_VERSION_VAL_T)GC_VERSION_MAJOR << 16)
                                    | (GC_VERSION_MINOR << 8)
                                    | GC_VERSION_MICRO;
#endif

GC_API GC_VERSION_VAL_T GC_CALL
GC_get_version(void)
{
  return ((GC_VERSION_VAL_T)GC_VERSION_MAJOR << 16) | (GC_VERSION_MINOR << 8)
         | GC_VERSION_MICRO;
}

GC_API int GC_CALL
GC_get_dont_add_byte_at_end(void)
{
#if MAX_EXTRA_BYTES > 0
  /* This is meaningful only if `GC_all_interior_pointers`. */
  return 0;
#else
  return 1;
#endif
}

/* Some more variables. */

#ifdef GC_DONT_EXPAND
int GC_dont_expand = TRUE;
#else
int GC_dont_expand = FALSE;
#endif

#if defined(GC_FREE_SPACE_DIVISOR) && !defined(CPPCHECK)
word GC_free_space_divisor = GC_FREE_SPACE_DIVISOR; /*< must be positive */
#else
word GC_free_space_divisor = 3;
#endif

GC_INNER int GC_CALLBACK
GC_never_stop_func(void)
{
  return FALSE;
}

#if defined(GC_TIME_LIMIT) && !defined(CPPCHECK)
/*
 * We try to keep pause times from exceeding this by much.
 * In milliseconds.
 */
unsigned long GC_time_limit = GC_TIME_LIMIT;
#elif defined(PARALLEL_MARK)
/*
 * The parallel marker cannot be interrupted for now, so the time limit
 * is absent by default.
 */
unsigned long GC_time_limit = GC_TIME_UNLIMITED;
#else
unsigned long GC_time_limit = 15;
#endif

#ifndef NO_CLOCK
/*
 * The nanoseconds add-on to `GC_time_limit` value.
 * Not updated by `GC_set_time_limit()`.
 * Ignored if the value of `GC_time_limit` is `GC_TIME_UNLIMITED`.
 */
STATIC unsigned long GC_time_lim_nsec = 0;

#  define TV_NSEC_LIMIT (1000UL * 1000) /*< amount of nanoseconds in 1 ms */

GC_API void GC_CALL
GC_set_time_limit_tv(struct GC_timeval_s tv)
{
  GC_ASSERT(tv.tv_ms <= GC_TIME_UNLIMITED);
  GC_ASSERT(tv.tv_nsec < TV_NSEC_LIMIT);
  GC_time_limit = tv.tv_ms;
  GC_time_lim_nsec = tv.tv_nsec;
}

GC_API struct GC_timeval_s GC_CALL
GC_get_time_limit_tv(void)
{
  struct GC_timeval_s tv;

  tv.tv_ms = GC_time_limit;
  tv.tv_nsec = GC_time_lim_nsec;
  return tv;
}
#endif /* !NO_CLOCK */

/* Note: accessed holding the allocator lock. */
STATIC GC_stop_func GC_default_stop_func = GC_never_stop_func;

GC_API void GC_CALL
GC_set_stop_func(GC_stop_func stop_func)
{
  GC_ASSERT(NONNULL_ARG_NOT_NULL(stop_func));
  LOCK();
  GC_default_stop_func = stop_func;
  UNLOCK();
}

GC_API GC_stop_func GC_CALL
GC_get_stop_func(void)
{
  GC_stop_func stop_func;

  READER_LOCK();
  stop_func = GC_default_stop_func;
  READER_UNLOCK();
  return stop_func;
}

#if defined(GC_DISABLE_INCREMENTAL) || defined(NO_CLOCK)
#  define GC_timeout_stop_func GC_default_stop_func
#else
STATIC int GC_CALLBACK
GC_timeout_stop_func(void)
{
  CLOCK_TYPE current_time;
  static unsigned count = 0;
  unsigned long time_diff, nsec_diff;

  GC_ASSERT(I_HOLD_LOCK());
  if (GC_default_stop_func())
    return TRUE;

  if (GC_time_limit == GC_TIME_UNLIMITED || (count++ & 3) != 0)
    return FALSE;

  GET_TIME(current_time);
  time_diff = MS_TIME_DIFF(current_time, GC_start_time);
  nsec_diff = NS_FRAC_TIME_DIFF(current_time, GC_start_time);
#  if defined(CPPCHECK)
  GC_noop1_ptr(&nsec_diff);
#  endif
  if (time_diff >= GC_time_limit
      && (time_diff > GC_time_limit || nsec_diff >= GC_time_lim_nsec)) {
    GC_COND_LOG_PRINTF(
        "Abandoning stopped marking after %lu ms %lu ns (attempt %d)\n",
        time_diff, nsec_diff, GC_n_attempts);
    return TRUE;
  }

  return FALSE;
}
#endif /* !GC_DISABLE_INCREMENTAL */

#ifdef THREADS
GC_INNER word GC_total_stacksize = 0;
#endif

/* The lowest value returned by `min_bytes_allocd()`. */
static size_t min_bytes_allocd_minimum = 1;

GC_API void GC_CALL
GC_set_min_bytes_allocd(size_t value)
{
  GC_ASSERT(value > 0);
  min_bytes_allocd_minimum = value;
}

GC_API size_t GC_CALL
GC_get_min_bytes_allocd(void)
{
  return min_bytes_allocd_minimum;
}

/*
 * Return the minimum number of bytes that must be allocated between
 * collections to amortize the cost of the latter.  Should be nonzero.
 */
static word
min_bytes_allocd(void)
{
  word result;
  word stack_size;
  /*
   * Total size of roots, it includes double stack size, since the stack
   * is expensive to scan.
   */
  word total_root_size;
  /* Estimate of memory to be scanned during normal collection. */
  word scan_size;

  GC_ASSERT(I_HOLD_LOCK());
#ifdef THREADS
  if (GC_need_to_lock) {
    /* We are multi-threaded... */
    stack_size = GC_total_stacksize;
    /*
     * For now, we just use the value computed during the latest garbage
     * collection.
     */
#  ifdef DEBUG_THREADS
    GC_log_printf("Total stacks size: %lu\n", (unsigned long)stack_size);
#  endif
  } else
#endif
  /* else */ {
#ifdef STACK_NOT_SCANNED
    stack_size = 0;
#elif defined(STACK_GROWS_UP)
    stack_size = (word)(GC_approx_sp() - GC_stackbottom);
#else
    stack_size = (word)(GC_stackbottom - GC_approx_sp());
#endif
  }

  total_root_size = 2 * stack_size + GC_root_size;
  scan_size = 2 * GC_composite_in_use + GC_atomic_in_use / 4 + total_root_size;
  result = scan_size / GC_free_space_divisor;
  if (GC_incremental) {
    result /= 2;
  }
  return result > min_bytes_allocd_minimum ? result : min_bytes_allocd_minimum;
}

/*
 * Return the number of bytes allocated, adjusted for explicit storage
 * management, etc.  This number is used in deciding when to trigger
 * collections.
 */
STATIC word
GC_adj_bytes_allocd(void)
{
  GC_signed_word result;
  GC_signed_word expl_managed = (GC_signed_word)GC_non_gc_bytes
                                - (GC_signed_word)GC_non_gc_bytes_at_gc;

  /*
   * Do not count what was explicitly freed, or newly allocated for
   * explicit management.  Note that deallocating an explicitly managed
   * object should not alter result, assuming the client is playing by
   * the rules.
   */
  result = (GC_signed_word)GC_bytes_allocd + (GC_signed_word)GC_bytes_dropped
           - (GC_signed_word)GC_bytes_freed
           + (GC_signed_word)GC_finalizer_bytes_freed - expl_managed;
  if (result > (GC_signed_word)GC_bytes_allocd) {
    /* Probably a client bug or unfortunate scheduling. */
    result = (GC_signed_word)GC_bytes_allocd;
  }
  /*
   * We count objects enqueued for finalization as though they had been
   * reallocated this round.  Finalization is visible to user.
   * And if we do not count this, we have stability problems for programs
   * that finalize all objects.
   */
  result += (GC_signed_word)GC_bytes_finalized;
  if (result < (GC_signed_word)(GC_bytes_allocd >> 3)) {
    /*
     * Always count at least 1/8 of the allocations.  We do not want to
     * collect too infrequently, since that would inhibit coalescing of
     * free storage blocks.  This also makes us partially robust against
     * client bugs.
     */
    result = (GC_signed_word)(GC_bytes_allocd >> 3);
  }
  return (word)result;
}

/*
 * Clear up a few frames worth of garbage left at the top of the stack.
 * This is used to prevent us from accidentally treating garbage left
 * on the stack by other parts of the collector as roots.
 * This differs from the code in `misc.c` file, which actually tries
 * to keep the stack clear of long-lived, client-generated garbage.
 */
STATIC void
GC_clear_a_few_frames(void)
{
#ifndef CLEAR_STACK_NPTRS
#  define CLEAR_STACK_NPTRS 64 /*< pointers */
#endif
  volatile ptr_t frames[CLEAR_STACK_NPTRS];

  BZERO(CAST_AWAY_VOLATILE_PVOID(frames), sizeof(frames));
}

GC_API void GC_CALL
GC_start_incremental_collection(void)
{
#ifndef GC_DISABLE_INCREMENTAL
  LOCK();
  if (GC_incremental) {
    GC_should_start_incremental_collection = TRUE;
    if (!GC_dont_gc) {
      GC_collect_a_little_inner(1);
    }
  }
  UNLOCK();
#endif
}

GC_INNER GC_bool
GC_should_collect(void)
{
  static word last_min_bytes_allocd, last_gc_no;

  GC_ASSERT(I_HOLD_LOCK());
  if (last_gc_no != GC_gc_no) {
    last_min_bytes_allocd = min_bytes_allocd();
    last_gc_no = GC_gc_no;
  }
#ifndef GC_DISABLE_INCREMENTAL
  if (GC_should_start_incremental_collection) {
    GC_should_start_incremental_collection = FALSE;
    return TRUE;
  }
#endif
  if (GC_disable_automatic_collection)
    return FALSE;

  if (GC_last_heap_growth_gc_no == GC_gc_no) {
    /* Avoid expanding past limits used by black-listing. */
    return TRUE;
  }

  return GC_adj_bytes_allocd() >= last_min_bytes_allocd;
}

/*
 * Called at start of full collections.  Not called if zero.
 * Called with the allocator lock held.  Not used by the collector itself.
 */
/* `STATIC` */ GC_start_callback_proc GC_start_call_back = 0;

GC_API void GC_CALL
GC_set_start_callback(GC_start_callback_proc fn)
{
  LOCK();
  GC_start_call_back = fn;
  UNLOCK();
}

GC_API GC_start_callback_proc GC_CALL
GC_get_start_callback(void)
{
  GC_start_callback_proc fn;

  READER_LOCK();
  fn = GC_start_call_back;
  READER_UNLOCK();
  return fn;
}

GC_INLINE void
GC_notify_full_gc(void)
{
  if (GC_start_call_back != 0) {
    (*GC_start_call_back)();
  }
}

STATIC GC_bool GC_stopped_mark(GC_stop_func stop_func);
STATIC void GC_finish_collection(void);

/*
 * Initiate a garbage collection if appropriate.  Choose judiciously
 * between partial, full, and stop-world collections.
 */
STATIC void
GC_maybe_gc(void)
{
  GC_ASSERT(I_HOLD_LOCK());
  ASSERT_CANCEL_DISABLED();
  if (!GC_should_collect())
    return;

  if (!GC_incremental) {
    GC_gcollect_inner();
    return;
  }

  GC_ASSERT(!GC_collection_in_progress());
#ifdef PARALLEL_MARK
  if (GC_parallel)
    GC_wait_for_reclaim();
#endif
  if (GC_need_full_gc || GC_n_partial_gcs >= GC_full_freq) {
    GC_COND_LOG_PRINTF(
        "***>Full mark for collection #%lu after %lu bytes allocated\n",
        (unsigned long)GC_gc_no + 1, (unsigned long)GC_bytes_allocd);
    GC_notify_full_gc();
    ENTER_GC();
    GC_promote_black_lists();
    (void)GC_reclaim_all((GC_stop_func)0, TRUE);
    GC_clear_marks();
    EXIT_GC();
    GC_n_partial_gcs = 0;
    GC_is_full_gc = TRUE;
  } else {
    GC_n_partial_gcs++;
  }

  /*
   * Try to mark with the world stopped.  If we run out of time, then this
   * turns into an incremental marking.
   */
#ifndef NO_CLOCK
  if (GC_time_limit != GC_TIME_UNLIMITED)
    GET_TIME(GC_start_time);
#endif
  if (GC_stopped_mark(GC_timeout_stop_func)) {
    SAVE_CALLERS_TO_LAST_STACK();
    GC_finish_collection();
  } else if (!GC_is_full_gc) {
    /* Count this as the first attempt. */
    GC_n_attempts++;
  }
}

STATIC GC_on_collection_event_proc GC_on_collection_event = 0;

GC_API void GC_CALL
GC_set_on_collection_event(GC_on_collection_event_proc fn)
{
  /* `fn` may be 0 (means no event notifier). */
  LOCK();
  GC_on_collection_event = fn;
  UNLOCK();
}

GC_API GC_on_collection_event_proc GC_CALL
GC_get_on_collection_event(void)
{
  GC_on_collection_event_proc fn;

  READER_LOCK();
  fn = GC_on_collection_event;
  READER_UNLOCK();
  return fn;
}

GC_INNER GC_bool
GC_try_to_collect_inner(GC_stop_func stop_func)
{
#ifndef NO_CLOCK
  CLOCK_TYPE start_time = CLOCK_TYPE_INITIALIZER;
  GC_bool start_time_valid;
#endif

  ASSERT_CANCEL_DISABLED();
  GC_ASSERT(I_HOLD_LOCK());
  GC_ASSERT(GC_is_initialized);
  if (GC_dont_gc || (*stop_func)())
    return FALSE;
  if (GC_on_collection_event)
    GC_on_collection_event(GC_EVENT_START);
  if (GC_incremental && GC_collection_in_progress()) {
    GC_COND_LOG_PRINTF(
        "GC_try_to_collect_inner: finishing collection in progress\n");
    /* Just finish collection already in progress. */
    do {
      if ((*stop_func)()) {
        /* TODO: Notify `GC_EVENT_ABANDON`. */
        return FALSE;
      }
      GC_collect_a_little_inner(1);
    } while (GC_collection_in_progress());
  }
  GC_notify_full_gc();
#ifndef NO_CLOCK
  start_time_valid = FALSE;
  if ((GC_print_stats | (int)GC_measure_performance) != 0) {
    if (GC_print_stats)
      GC_log_printf("Initiating full world-stop collection!\n");
    start_time_valid = TRUE;
    GET_TIME(start_time);
  }
#endif
  GC_promote_black_lists();
  /*
   * Make sure all blocks have been reclaimed, so sweep routines do not
   * see cleared mark bits.  If we are guaranteed to finish, then this
   * is unnecessary.  In the find-leak case, we have to finish to
   * guarantee that previously unmarked objects are not reported as leaks.
   */
#ifdef PARALLEL_MARK
  if (GC_parallel)
    GC_wait_for_reclaim();
#endif
  ENTER_GC();
  if ((GC_find_leak_inner || stop_func != GC_never_stop_func)
      && !GC_reclaim_all(stop_func, FALSE)) {
    /* Aborted.  So far everything is still consistent. */
    EXIT_GC();
    /* TODO: Notify `GC_EVENT_ABANDON`. */
    return FALSE;
  }
  GC_invalidate_mark_state(); /*< flush mark stack */
  GC_clear_marks();
  SAVE_CALLERS_TO_LAST_STACK();
  GC_is_full_gc = TRUE;
  EXIT_GC();
  if (!GC_stopped_mark(stop_func)) {
    if (!GC_incremental) {
      /*
       * We are partially done and have no way to complete or use
       * current work.  Reestablish invariants as cheaply as possible.
       */
      GC_invalidate_mark_state();
      GC_unpromote_black_lists();
    } else {
      /*
       * We claim the world is already (or still) consistent.
       * We will finish incrementally.
       */
    }
    /* TODO: Notify `GC_EVENT_ABANDON`. */
    return FALSE;
  }
  GC_finish_collection();
#ifndef NO_CLOCK
  if (start_time_valid) {
    CLOCK_TYPE current_time;
    unsigned long time_diff, ns_frac_diff;

    GET_TIME(current_time);
    time_diff = MS_TIME_DIFF(current_time, start_time);
    ns_frac_diff = NS_FRAC_TIME_DIFF(current_time, start_time);
    if (GC_measure_performance) {
      GC_full_gc_total_time += time_diff; /*< may wrap */
      GC_full_gc_total_ns_frac += (unsigned32)ns_frac_diff;
      if (GC_full_gc_total_ns_frac >= (unsigned32)1000000UL) {
        /* Overflow of the nanoseconds part. */
        GC_full_gc_total_ns_frac -= (unsigned32)1000000UL;
        GC_full_gc_total_time++;
      }
    }
    if (GC_print_stats)
      GC_log_printf("Complete collection took %lu ms %lu ns\n", time_diff,
                    ns_frac_diff);
  }
#endif
  if (GC_on_collection_event)
    GC_on_collection_event(GC_EVENT_END);
  return TRUE;
}

/* The default value of `GC_rate`. */
#ifndef GC_RATE
#  define GC_RATE 10
#endif

/*
 * When `GC_collect_a_little_inner()` performs `n_blocks` units of garbage
 * collection work, a unit is intended to touch roughly `GC_rate` pages.
 * (But, every once in a while, we do more than that.)  This needs to be
 * a fairly large number with our current incremental collection strategy,
 * since otherwise we allocate too much during garbage collection, and
 * the cleanup gets expensive.
 */
STATIC unsigned GC_rate = GC_RATE;

GC_API void GC_CALL
GC_set_rate(int value)
{
  GC_ASSERT(value > 0);
  GC_rate = (unsigned)value;
}

GC_API int GC_CALL
GC_get_rate(void)
{
  return (int)GC_rate;
}

/* The default maximum number of prior attempts at world stop marking. */
#ifndef MAX_PRIOR_ATTEMPTS
#  define MAX_PRIOR_ATTEMPTS 3
#endif

/*
 * The maximum number of prior attempts at world stop marking.
 * A value of 1 means that we finish the second time, no matter how long
 * it takes.  Does not count the initial root scan for a full collection.
 */
static int max_prior_attempts = MAX_PRIOR_ATTEMPTS;

GC_API void GC_CALL
GC_set_max_prior_attempts(int value)
{
  GC_ASSERT(value >= 0);
  max_prior_attempts = value;
}

GC_API int GC_CALL
GC_get_max_prior_attempts(void)
{
  return max_prior_attempts;
}

GC_INNER void
GC_collect_a_little_inner(size_t n_blocks)
{
  IF_CANCEL(int cancel_state;)

  GC_ASSERT(I_HOLD_LOCK());
  GC_ASSERT(GC_is_initialized);
  DISABLE_CANCEL(cancel_state);
  if (GC_incremental && GC_collection_in_progress()) {
    size_t i;
    size_t max_deficit = GC_rate * n_blocks;

    ENTER_GC();
#ifdef PARALLEL_MARK
    if (GC_time_limit != GC_TIME_UNLIMITED)
      GC_parallel_mark_disabled = TRUE;
#endif
    for (i = GC_mark_deficit; i < max_deficit; i++) {
      if (GC_mark_some(NULL))
        break;
    }
#ifdef PARALLEL_MARK
    GC_parallel_mark_disabled = FALSE;
#endif
    EXIT_GC();

    if (i < max_deficit && !GC_dont_gc) {
      GC_ASSERT(!GC_collection_in_progress());
      /* Need to follow up with a full collection. */
      SAVE_CALLERS_TO_LAST_STACK();
#ifdef PARALLEL_MARK
      if (GC_parallel)
        GC_wait_for_reclaim();
#endif
#ifndef NO_CLOCK
      if (GC_time_limit != GC_TIME_UNLIMITED
          && GC_n_attempts < max_prior_attempts)
        GET_TIME(GC_start_time);
#endif
      if (GC_stopped_mark(GC_n_attempts < max_prior_attempts
                              ? GC_timeout_stop_func
                              : GC_never_stop_func)) {
        GC_finish_collection();
      } else {
        GC_n_attempts++;
      }
    }
    if (GC_mark_deficit > 0) {
      GC_mark_deficit
          = GC_mark_deficit > max_deficit ? GC_mark_deficit - max_deficit : 0;
    }
  } else if (!GC_dont_gc) {
    GC_maybe_gc();
  }
  RESTORE_CANCEL(cancel_state);
}

#if !defined(NO_FIND_LEAK) || !defined(SHORT_DBG_HDRS)
GC_INNER void (*GC_check_heap)(void) = 0;
GC_INNER void (*GC_print_all_smashed)(void) = 0;
#endif

GC_API int GC_CALL
GC_collect_a_little(void)
{
  int result;

  if (UNLIKELY(!GC_is_initialized))
    GC_init();
  LOCK();
  /*
   * Note: if the collection is in progress, this may do marking (not
   * stopping the world) even in case of disabled garbage collection.
   */
  GC_collect_a_little_inner(1);
  result = (int)GC_collection_in_progress();
  UNLOCK();
  if (GC_debugging_started && !result)
    GC_print_all_smashed();
  return result;
}

#ifdef THREADS
GC_API void GC_CALL
GC_stop_world_external(void)
{
  GC_ASSERT(GC_is_initialized);
  LOCK();
#  ifdef THREAD_LOCAL_ALLOC
  GC_ASSERT(!GC_world_stopped);
#  endif
  ENTER_GC();
  STOP_WORLD();
#  ifdef THREAD_LOCAL_ALLOC
  GC_world_stopped = TRUE;
#  endif
}

GC_API void GC_CALL
GC_start_world_external(void)
{
#  ifdef THREAD_LOCAL_ALLOC
  GC_ASSERT(GC_world_stopped);
  GC_world_stopped = FALSE;
#  else
  GC_ASSERT(GC_is_initialized);
#  endif
  START_WORLD();
  EXIT_GC();
  UNLOCK();
}
#endif /* THREADS */

#ifdef USE_MUNMAP
#  ifndef MUNMAP_THRESHOLD
#    define MUNMAP_THRESHOLD 7
#  endif
GC_INNER unsigned GC_unmap_threshold = MUNMAP_THRESHOLD;

#  define IF_USE_MUNMAP(x) x
#  define COMMA_IF_USE_MUNMAP(x) /* comma */ , x
#else
#  define IF_USE_MUNMAP(x)
#  define COMMA_IF_USE_MUNMAP(x)
#endif /* !USE_MUNMAP */

/*
 * We stop the world and mark from all roots.  If `stop_func()` ever
 * returns `TRUE`, we may fail and return `FALSE`.  Increment `GC_gc_no`
 * if we succeed.
 */
STATIC GC_bool
GC_stopped_mark(GC_stop_func stop_func)
{
  ptr_t cold_gc_frame = GC_approx_sp();
  unsigned abandoned_at;
#ifndef NO_CLOCK
  CLOCK_TYPE start_time = CLOCK_TYPE_INITIALIZER;
  GC_bool start_time_valid = FALSE;
#endif

  GC_ASSERT(I_HOLD_LOCK());
  GC_ASSERT(GC_is_initialized);
  ENTER_GC();
#if !defined(REDIRECT_MALLOC) && defined(USE_WINALLOC)
  GC_add_current_malloc_heap();
#endif
#if defined(REGISTER_LIBRARIES_EARLY)
  GC_cond_register_dynamic_libraries();
#endif

#if !defined(GC_NO_FINALIZATION) && !defined(GC_TOGGLE_REFS_NOT_NEEDED)
  GC_process_togglerefs();
#endif

  /* Output blank line for convenience here. */
  GC_COND_LOG_PRINTF(
      "\n--> Marking for collection #%lu after %lu allocated bytes\n",
      (unsigned long)GC_gc_no + 1, (unsigned long)GC_bytes_allocd);
#ifndef NO_CLOCK
  if (GC_PRINT_STATS_FLAG || GC_measure_performance) {
    GET_TIME(start_time);
    start_time_valid = TRUE;
  }
#endif
#ifdef THREADS
  if (GC_on_collection_event)
    GC_on_collection_event(GC_EVENT_PRE_STOP_WORLD);
#endif
  STOP_WORLD();
#ifdef THREADS
  if (GC_on_collection_event)
    GC_on_collection_event(GC_EVENT_POST_STOP_WORLD);
#  ifdef THREAD_LOCAL_ALLOC
  GC_world_stopped = TRUE;
#  elif defined(CPPCHECK)
  /* Workaround a warning about adjacent same `if` condition. */
  (void)0;
#  endif
#endif

#ifdef MAKE_BACK_GRAPH
  if (GC_print_back_height) {
    GC_build_back_graph();
  }
#endif

  /* Notify about marking from all roots. */
  if (GC_on_collection_event)
    GC_on_collection_event(GC_EVENT_MARK_START);

  /* Minimize junk left in my registers and on the stack. */
  GC_clear_a_few_frames();
  GC_noop6(0, 0, 0, 0, 0, 0);

  GC_initiate_gc();
#ifdef PARALLEL_MARK
  if (stop_func != GC_never_stop_func)
    GC_parallel_mark_disabled = TRUE;
#endif
  for (abandoned_at = 1; !(*stop_func)(); abandoned_at++) {
    if (GC_mark_some(cold_gc_frame)) {
#ifdef PARALLEL_MARK
      if (GC_parallel && GC_parallel_mark_disabled) {
        GC_COND_LOG_PRINTF("Stopped marking done after %u iterations"
                           " with disabled parallel marker\n",
                           abandoned_at - 1);
      }
#endif
      abandoned_at = 0;
      break;
    }
  }
#ifdef PARALLEL_MARK
  GC_parallel_mark_disabled = FALSE;
#endif

  if (abandoned_at > 0) {
    /* Give the mutator a chance. */
    GC_mark_deficit = abandoned_at - 1;
    /* TODO: Notify `GC_EVENT_MARK_ABANDON`. */
  } else {
    GC_gc_no++;
    /* Check all debugged objects for consistency. */
    if (GC_debugging_started)
      GC_check_heap();
    if (GC_on_collection_event)
      GC_on_collection_event(GC_EVENT_MARK_END);
  }

#ifdef THREADS
  if (GC_on_collection_event)
    GC_on_collection_event(GC_EVENT_PRE_START_WORLD);
#endif
#ifdef THREAD_LOCAL_ALLOC
  GC_world_stopped = FALSE;
#endif
  START_WORLD();
#ifdef THREADS
  if (GC_on_collection_event)
    GC_on_collection_event(GC_EVENT_POST_START_WORLD);
#endif

#ifndef NO_CLOCK
  if (start_time_valid) {
    CLOCK_TYPE current_time;
    unsigned long time_diff, ns_frac_diff;

    /* TODO: Avoid code duplication from `GC_try_to_collect_inner`. */
    GET_TIME(current_time);
    time_diff = MS_TIME_DIFF(current_time, start_time);
    ns_frac_diff = NS_FRAC_TIME_DIFF(current_time, start_time);
    if (GC_measure_performance) {
      GC_stopped_mark_total_time += time_diff; /*< may wrap */
      GC_stopped_mark_total_ns_frac += (unsigned32)ns_frac_diff;
      if (GC_stopped_mark_total_ns_frac >= (unsigned32)1000000UL) {
        GC_stopped_mark_total_ns_frac -= (unsigned32)1000000UL;
        GC_stopped_mark_total_time++;
      }
    }

    if (GC_PRINT_STATS_FLAG || GC_measure_performance) {
      unsigned total_time = GC_world_stopped_total_time;
      unsigned divisor = GC_world_stopped_total_divisor;

      /* Compute new world-stop delay total time. */
      if (total_time > (((unsigned)-1) >> 1)
          || divisor >= MAX_TOTAL_TIME_DIVISOR) {
        /* Halve values if overflow occurs. */
        total_time >>= 1;
        divisor >>= 1;
      }
      total_time += time_diff < (((unsigned)-1) >> 1) ? (unsigned)time_diff
                                                      : ((unsigned)-1) >> 1;
      /* Update old `GC_world_stopped_total_time` and its divisor. */
      GC_world_stopped_total_time = total_time;
      GC_world_stopped_total_divisor = ++divisor;
      if (GC_PRINT_STATS_FLAG && 0 == abandoned_at) {
        GC_ASSERT(divisor != 0);
        GC_log_printf(
            "World-stopped marking took %lu ms %lu ns (%u ms in average)\n",
            time_diff, ns_frac_diff, total_time / divisor);
      }
    }
  }
#endif

  EXIT_GC();
  if (0 == abandoned_at)
    return TRUE;
  GC_COND_LOG_PRINTF("Abandoned stopped marking after %u iterations\n",
                     abandoned_at - 1);
  return FALSE;
}

GC_INNER void
GC_set_fl_marks(ptr_t q)
{
#ifdef GC_ASSERTIONS
  ptr_t q2;
#endif
  struct hblk *h = HBLKPTR(q);
  const struct hblk *last_h = h;
  hdr *hhdr;
#ifdef MARK_BIT_PER_OBJ
  size_t sz;
#endif

  GC_ASSERT(q != NULL);
  hhdr = HDR(h);
#ifdef MARK_BIT_PER_OBJ
  sz = hhdr->hb_sz;
#endif
#ifdef GC_ASSERTIONS
  q2 = (ptr_t)obj_link(q);
#endif
  for (;;) {
    size_t bit_no = MARK_BIT_NO((size_t)((ptr_t)q - (ptr_t)h), sz);

    if (!mark_bit_from_hdr(hhdr, bit_no)) {
      set_mark_bit_from_hdr(hhdr, bit_no);
      INCR_MARKS(hhdr);
    }
    q = (ptr_t)obj_link(q);
    if (NULL == q)
      break;
#ifdef GC_ASSERTIONS
    /*
     * Detect a cycle in the free list.  The algorithm is to have
     * a "twice faster" iterator over the list which meets the first
     * one in case of a cycle existing in the list.
     */
    if (q2 != NULL) {
      q2 = (ptr_t)obj_link(q2);
      GC_ASSERT(q2 != q);
      if (q2 != NULL) {
        q2 = (ptr_t)obj_link(q2);
        GC_ASSERT(q2 != q);
      }
    }
#endif

    h = HBLKPTR(q);
    if (UNLIKELY(h != last_h)) {
      last_h = h;
      /* Update `hhdr` and `sz`. */
      hhdr = HDR(h);
#ifdef MARK_BIT_PER_OBJ
      sz = hhdr->hb_sz;
#endif
    }
  }
}

#if defined(GC_ASSERTIONS) && defined(THREAD_LOCAL_ALLOC)
/*
 * Check that all mark bits for the free list, whose first entry is
 * `*pfreelist`, are set.  The check is skipped if `*pfreelist` points to
 * a special value.
 */
void
GC_check_fl_marks(void **pfreelist)
{
  /*
   * TODO: There is a data race with `GC_FAST_MALLOC_GRANS` (which does
   * not do atomic updates to the free-list).  The race seems to be
   * harmless, and for now we just skip this check in case of TSan.
   */
#  if defined(AO_HAVE_load_acquire_read) && !defined(THREAD_SANITIZER)
  ptr_t list = GC_cptr_load_acquire_read((volatile ptr_t *)pfreelist);
  /* Atomic operations are used because the world is running. */
  ptr_t p, prev, next;

  if (ADDR(list) <= HBLKSIZE)
    return;

  prev = (ptr_t)pfreelist;
  for (p = list; p != NULL; p = next) {
    if (!GC_is_marked(p)) {
      ABORT_ARG2("Unmarked local free-list entry", ": object %p on list %p",
                 (void *)p, (void *)list);
    }

    /*
     * While traversing the free-list, it re-reads the pointer to the
     * current node before accepting its next pointer and bails out
     * if the latter has changed.  That way, it will not try to follow
     * the pointer which might be been modified after the object was
     * returned to the client.  It might perform the mark-check on the
     * just allocated object but that should be harmless.
     */
    next = GC_cptr_load_acquire_read((volatile ptr_t *)p);
    if (GC_cptr_load((volatile ptr_t *)prev) != p)
      break;
    prev = p;
  }
#  else
  /* FIXME: Not implemented (just skipped). */
  (void)pfreelist;
#  endif
}
#endif /* GC_ASSERTIONS && THREAD_LOCAL_ALLOC */

/*
 * Clear all mark bits for the free list (specified by the first entry).
 * Decrement `GC_bytes_found` by number of bytes on free list.
 */
STATIC void
GC_clear_fl_marks(ptr_t q)
{
  struct hblk *h = HBLKPTR(q);
  const struct hblk *last_h = h;
  hdr *hhdr = HDR(h);
  size_t sz = hhdr->hb_sz; /*< normally set only once */

  for (;;) {
    size_t bit_no = MARK_BIT_NO((size_t)((ptr_t)q - (ptr_t)h), sz);

    if (mark_bit_from_hdr(hhdr, bit_no)) {
      size_t n_marks = hhdr->hb_n_marks;

#ifdef LINT2
      if (0 == n_marks)
        ABORT("hhdr->hb_n_marks cannot be zero");
#else
      GC_ASSERT(n_marks != 0);
#endif
      clear_mark_bit_from_hdr(hhdr, bit_no);
      n_marks--;
#ifdef PARALLEL_MARK
      /* Approximate count, do not decrement to zero! */
      if (n_marks != 0 || !GC_parallel) {
        hhdr->hb_n_marks = n_marks;
      }
#else
      hhdr->hb_n_marks = n_marks;
#endif
    }
    GC_bytes_found -= (GC_signed_word)sz;

    q = (ptr_t)obj_link(q);
    if (NULL == q)
      break;

    h = HBLKPTR(q);
    if (UNLIKELY(h != last_h)) {
      last_h = h;
      /* Update `hhdr` and `sz`. */
      hhdr = HDR(h);
      sz = hhdr->hb_sz;
    }
  }
}

/* Mark all objects on the free lists for every object kind. */
static void
set_all_fl_marks(void)
{
  unsigned kind;

  for (kind = 0; kind < GC_n_kinds; kind++) {
    word size; /*< current object size */

    for (size = 1; size <= MAXOBJGRANULES; size++) {
      ptr_t q = (ptr_t)GC_obj_kinds[kind].ok_freelist[size];

      if (q != NULL)
        GC_set_fl_marks(q);
    }
  }
}

/*
 * Clear free-list mark bits.  Also subtract memory remaining from
 * `GC_bytes_found` count.
 */
static void
clear_all_fl_marks(void)
{
  unsigned kind;

  for (kind = 0; kind < GC_n_kinds; kind++) {
    word size; /*< current object size */

    for (size = 1; size <= MAXOBJGRANULES; size++) {
      ptr_t q = (ptr_t)GC_obj_kinds[kind].ok_freelist[size];

      if (q != NULL)
        GC_clear_fl_marks(q);
    }
  }
}

#if defined(GC_ASSERTIONS) && defined(THREAD_LOCAL_ALLOC)
void GC_check_tls(void);
#endif

GC_on_heap_resize_proc GC_on_heap_resize = 0;

/* Used for logging only. */
GC_INLINE int
GC_compute_heap_usage_percent(void)
{
  word used = GC_composite_in_use + GC_atomic_in_use + GC_bytes_allocd;
  word heap_sz = GC_heapsize - GC_unmapped_bytes;
#if defined(CPPCHECK)
  word limit = (GC_WORD_MAX >> 1) / 50; /*< to avoid a false positive */
#else
  const word limit = GC_WORD_MAX / 100;
#endif

  return used >= heap_sz ? 0
         : used < limit  ? (int)((used * 100) / heap_sz)
                         : (int)(used / (heap_sz / 100));
}

#define GC_DBGLOG_PRINT_HEAP_IN_USE()                                        \
  GC_DBGLOG_PRINTF("In-use heap: %d%% (%lu KiB pointers + %lu KiB other)\n", \
                   GC_compute_heap_usage_percent(),                          \
                   TO_KiB_UL(GC_composite_in_use),                           \
                   TO_KiB_UL(GC_atomic_in_use + GC_bytes_allocd))

/*
 * Finish up a collection.  Assumes mark bits are consistent, but the
 * world is otherwise running.
 */
STATIC void
GC_finish_collection(void)
{
#ifndef NO_CLOCK
  CLOCK_TYPE start_time = CLOCK_TYPE_INITIALIZER;
  CLOCK_TYPE finalize_time = CLOCK_TYPE_INITIALIZER;
#endif

  GC_ASSERT(I_HOLD_LOCK());
#if defined(GC_ASSERTIONS) && defined(THREAD_LOCAL_ALLOC) \
    && !defined(DBG_HDRS_ALL)
  /* Check that we marked some of our own data. */
  GC_check_tls();
  /* TODO: Add more checks. */
#endif

#ifndef NO_CLOCK
  if (GC_print_stats)
    GET_TIME(start_time);
#endif
  if (GC_on_collection_event)
    GC_on_collection_event(GC_EVENT_RECLAIM_START);

#ifndef GC_GET_HEAP_USAGE_NOT_NEEDED
  if (GC_bytes_found > 0)
    GC_reclaimed_bytes_before_gc += (word)GC_bytes_found;
#endif
  GC_bytes_found = 0;
#if defined(LINUX) && defined(__ELF__) && !defined(SMALL_CONFIG)
  if (GETENV("GC_PRINT_ADDRESS_MAP") != NULL) {
    GC_print_address_map();
  }
#endif
  COND_DUMP;
  if (GC_find_leak_inner) {
    set_all_fl_marks();
    /* This just checks; it does not really reclaim anything. */
    GC_start_reclaim(TRUE);
  }

#ifndef GC_NO_FINALIZATION
  GC_finalize();
#endif
#ifndef NO_CLOCK
  if (GC_print_stats)
    GET_TIME(finalize_time);
#endif
#ifdef MAKE_BACK_GRAPH
  if (GC_print_back_height) {
    GC_traverse_back_graph();
  }
#endif

  /*
   * Clear free-list mark bits, in case they got accidentally marked
   * (or `GC_find_leak` is set and they were intentionally marked).
   * Note that composite objects on free list are cleared, thus
   * accidentally marking a free list is not a problem; but some objects
   * on the list itself might be marked, and the given function call
   * fixes it.
   */
  clear_all_fl_marks();

  GC_VERBOSE_LOG_PRINTF("Bytes recovered before sweep - f.l. count = %ld\n",
                        (long)GC_bytes_found);

  /* Reconstruct free lists to contain everything not marked. */
  GC_start_reclaim(FALSE);

#ifdef USE_MUNMAP
  if (GC_unmap_threshold > 0    /*< memory unmapping enabled? */
      && LIKELY(GC_gc_no != 1)) /*< do not unmap during `GC_init` */
    GC_unmap_old(GC_unmap_threshold);

  GC_ASSERT(GC_heapsize >= GC_unmapped_bytes);
#endif
  GC_ASSERT(GC_our_mem_bytes >= GC_heapsize);
  GC_DBGLOG_PRINTF(
      "GC #%lu freed %ld bytes, heap %lu KiB (" IF_USE_MUNMAP(
          "+ %lu KiB unmapped ") "+ %lu KiB internal)\n",
      (unsigned long)GC_gc_no, (long)GC_bytes_found,
      TO_KiB_UL(GC_heapsize - GC_unmapped_bytes) /*, */
      COMMA_IF_USE_MUNMAP(TO_KiB_UL(GC_unmapped_bytes)),
      TO_KiB_UL(GC_our_mem_bytes - GC_heapsize + sizeof(GC_arrays)));
  GC_DBGLOG_PRINT_HEAP_IN_USE();
  if (GC_is_full_gc) {
    GC_used_heap_size_after_full = GC_heapsize - GC_large_free_bytes;
    GC_need_full_gc = FALSE;
  } else {
    GC_need_full_gc = GC_heapsize - GC_used_heap_size_after_full
                      > min_bytes_allocd() + GC_large_free_bytes;
  }

  /* Reset or increment counters for next cycle. */
  GC_n_attempts = 0;
  GC_is_full_gc = FALSE;
  GC_bytes_allocd_before_gc += GC_bytes_allocd;
  GC_non_gc_bytes_at_gc = GC_non_gc_bytes;
  GC_bytes_allocd = 0;
  GC_bytes_dropped = 0;
  GC_bytes_freed = 0;
  GC_finalizer_bytes_freed = 0;

  if (GC_on_collection_event)
    GC_on_collection_event(GC_EVENT_RECLAIM_END);
#ifndef NO_CLOCK
  if (GC_print_stats) {
    CLOCK_TYPE done_time;

    GET_TIME(done_time);
#  if !defined(SMALL_CONFIG) && !defined(GC_NO_FINALIZATION)
    /* A convenient place to output finalization statistics. */
    GC_print_finalization_stats();
#  endif
    GC_log_printf(
        "Finalize and initiate sweep took %lu ms %lu ns + %lu ms %lu ns\n",
        MS_TIME_DIFF(finalize_time, start_time),
        NS_FRAC_TIME_DIFF(finalize_time, start_time),
        MS_TIME_DIFF(done_time, finalize_time),
        NS_FRAC_TIME_DIFF(done_time, finalize_time));
  }
#elif !defined(SMALL_CONFIG) && !defined(GC_NO_FINALIZATION)
  if (GC_print_stats)
    GC_print_finalization_stats();
#endif
}

/* Note: if `stop_func` is 0, then `GC_default_stop_func` is used instead. */
STATIC GC_bool
GC_try_to_collect_general(GC_stop_func stop_func, GC_bool force_unmap)
{
  GC_bool result;
#ifdef USE_MUNMAP
  unsigned old_unmap_threshold;
#endif
  IF_CANCEL(int cancel_state;)

  if (UNLIKELY(!GC_is_initialized))
    GC_init();
  if (GC_debugging_started)
    GC_print_all_smashed();
  GC_notify_or_invoke_finalizers();
  LOCK();
  if (force_unmap) {
    /*
     * Record current heap size to make heap growth more conservative
     * afterwards (as if the heap is growing from zero size again).
     */
    GC_heapsize_at_forced_unmap = GC_heapsize;
  }
  DISABLE_CANCEL(cancel_state);
#ifdef USE_MUNMAP
  old_unmap_threshold = GC_unmap_threshold;
  if (force_unmap || (GC_force_unmap_on_gcollect && old_unmap_threshold > 0))
    GC_unmap_threshold = 1; /*< unmap as much as possible */
#endif
  /* Minimize junk left in my registers. */
  GC_noop6(0, 0, 0, 0, 0, 0);
  result = GC_try_to_collect_inner(stop_func != 0 ? stop_func
                                                  : GC_default_stop_func);
#ifdef USE_MUNMAP
  /* Restore it. */
  GC_unmap_threshold = old_unmap_threshold;
#endif
  RESTORE_CANCEL(cancel_state);
  UNLOCK();
  if (result) {
    if (GC_debugging_started)
      GC_print_all_smashed();
    GC_notify_or_invoke_finalizers();
  }
  return result;
}

/* Externally callable routines to invoke full, stop-the-world collection. */

GC_API int GC_CALL
GC_try_to_collect(GC_stop_func stop_func)
{
  GC_ASSERT(NONNULL_ARG_NOT_NULL(stop_func));
  return (int)GC_try_to_collect_general(stop_func, FALSE);
}

GC_API void GC_CALL
GC_gcollect(void)
{
  /*
   * Zero is passed as stop_func to get `GC_default_stop_func` value
   * while holding the allocator lock (to prevent data race).
   */
  (void)GC_try_to_collect_general(0, FALSE);
  if (get_have_errors())
    GC_print_all_errors();
}

GC_API void GC_CALL
GC_gcollect_and_unmap(void)
{
  /* Collect and force memory unmapping to OS. */
  (void)GC_try_to_collect_general(GC_never_stop_func, TRUE);
}

GC_INNER ptr_t
GC_os_get_mem(size_t bytes)
{
  ptr_t space;

  GC_ASSERT(I_HOLD_LOCK());
  space = (ptr_t)GET_MEM(bytes); /*< `HBLKSIZE`-aligned */
  if (UNLIKELY(NULL == space))
    return NULL;
#ifdef USE_PROC_FOR_LIBRARIES
  /* Add `HBLKSIZE`-aligned `GET_MEM`-generated block to `GC_our_memory`. */
  if (GC_n_memory >= MAX_HEAP_SECTS)
    ABORT("Too many GC-allocated memory sections: Increase MAX_HEAP_SECTS");
  GC_our_memory[GC_n_memory].hs_start = space;
  GC_our_memory[GC_n_memory].hs_bytes = bytes;
  GC_n_memory++;
#endif
  GC_our_mem_bytes += bytes;
  GC_VERBOSE_LOG_PRINTF("Got %lu bytes from OS\n", (unsigned long)bytes);
  return space;
}

/*
 * Use the chunk of memory starting at `h` of size `sz` as part of the heap.
 * Assumes `h` is `HBLKSIZE`-aligned, `sz` is a multiple of `HBLKSIZE`.
 */
STATIC void
GC_add_to_heap(struct hblk *h, size_t sz)
{
  hdr *hhdr;
  ptr_t endp;
  size_t old_capacity = 0;
  void *old_heap_sects = NULL;
#ifdef GC_ASSERTIONS
  size_t i;
#endif

  GC_ASSERT(I_HOLD_LOCK());
  GC_ASSERT(ADDR(h) % HBLKSIZE == 0);
  GC_ASSERT(sz % HBLKSIZE == 0);
  GC_ASSERT(sz > 0);
  GC_ASSERT(GC_all_nils != NULL);

  if (UNLIKELY(GC_n_heap_sects == GC_capacity_heap_sects)) {
    /* Allocate new `GC_heap_sects` with sufficient capacity. */
#ifndef INITIAL_HEAP_SECTS
#  define INITIAL_HEAP_SECTS 32
#endif
    size_t new_capacity
        = GC_n_heap_sects > 0 ? GC_n_heap_sects * 2 : INITIAL_HEAP_SECTS;
    void *new_heap_sects
        = GC_scratch_alloc(new_capacity * sizeof(struct HeapSect));

    if (NULL == new_heap_sects) {
      /* Retry with smaller yet sufficient capacity. */
      new_capacity = GC_n_heap_sects + INITIAL_HEAP_SECTS;
      new_heap_sects
          = GC_scratch_alloc(new_capacity * sizeof(struct HeapSect));
      if (NULL == new_heap_sects)
        ABORT("Insufficient memory for heap sections");
    }
    old_capacity = GC_capacity_heap_sects;
    old_heap_sects = GC_heap_sects;
    /* Transfer `GC_heap_sects` contents to the newly allocated array. */
    if (GC_n_heap_sects > 0)
      BCOPY(old_heap_sects, new_heap_sects,
            GC_n_heap_sects * sizeof(struct HeapSect));
    GC_capacity_heap_sects = new_capacity;
    GC_heap_sects = (struct HeapSect *)new_heap_sects;
    GC_COND_LOG_PRINTF("Grew heap sections array to %lu elements\n",
                       (unsigned long)new_capacity);
  }

  while (UNLIKELY(ADDR(h) <= HBLKSIZE)) {
    /* Cannot handle memory near address zero. */
    ++h;
    sz -= HBLKSIZE;
    if (0 == sz)
      return;
  }
  while (UNLIKELY(ADDR(h) >= GC_WORD_MAX - sz)) {
    /* Prevent overflow when calculating `endp`. */
    sz -= HBLKSIZE;
    if (0 == sz)
      return;
  }
  endp = (ptr_t)h + sz;

  hhdr = GC_install_header(h);
  if (UNLIKELY(NULL == hhdr)) {
    /*
     * This is extremely unlikely. Cannot add it.  This will almost
     * certainly result in a `NULL` returned from the allocator, which
     * is entirely appropriate.
     */
    return;
  }
#ifdef GC_ASSERTIONS
  /* Ensure no intersection between sections. */
  for (i = 0; i < GC_n_heap_sects; i++) {
    ptr_t hs_start = GC_heap_sects[i].hs_start;
    ptr_t hs_end = hs_start + GC_heap_sects[i].hs_bytes;

    GC_ASSERT(!(ADDR_INSIDE((ptr_t)h, hs_start, hs_end)
                || (ADDR_LT(hs_start, endp) && ADDR_GE(hs_end, endp))
                || (ADDR_LT((ptr_t)h, hs_start) && ADDR_LT(hs_end, endp))));
  }
#endif
  GC_heap_sects[GC_n_heap_sects].hs_start = (ptr_t)h;
  GC_heap_sects[GC_n_heap_sects].hs_bytes = sz;
  GC_n_heap_sects++;
  hhdr->hb_block = h;
  hhdr->hb_sz = sz;
  hhdr->hb_flags = 0;
  GC_freehblk(h);
  GC_heapsize += sz;

  if (ADDR_GE((ptr_t)GC_least_plausible_heap_addr, (ptr_t)h)
      || UNLIKELY(NULL == GC_least_plausible_heap_addr)) {
    /*
     * Making it a little smaller than necessary prevents us from
     * getting a false hit from the variable itself.  There is some
     * unintentional reflection here.
     */
    GC_least_plausible_heap_addr = (ptr_t)h - sizeof(ptr_t);
  }
  if (ADDR_LT((ptr_t)GC_greatest_plausible_heap_addr, endp)) {
    GC_greatest_plausible_heap_addr = endp;
  }
#ifdef SET_REAL_HEAP_BOUNDS
  if (ADDR(h) < GC_least_real_heap_addr
      || UNLIKELY(0 == GC_least_real_heap_addr))
    GC_least_real_heap_addr = ADDR(h) - sizeof(ptr_t);
  if (GC_greatest_real_heap_addr < ADDR(endp)) {
#  ifdef INCLUDE_LINUX_THREAD_DESCR
    /* Avoid heap intersection with the static data roots. */
    GC_exclude_static_roots_inner((ptr_t)h, endp);
#  endif
    GC_greatest_real_heap_addr = ADDR(endp);
  }
#endif
  GC_handle_protected_regions_limit();
  if (UNLIKELY(old_capacity > 0)) {
#ifndef GWW_VDB
    /*
     * Recycling may call `GC_add_to_heap()` again but should not cause
     * resizing of `GC_heap_sects`.
     */
    GC_scratch_recycle_no_gww(old_heap_sects,
                              old_capacity * sizeof(struct HeapSect));
#else
    /* TODO: Implement GWW-aware recycling as in `alloc_mark_stack`. */
    GC_noop1_ptr(old_heap_sects);
#endif
  }
}

#ifndef NO_DEBUGGING
void
GC_print_heap_sects(void)
{
  size_t i;

  GC_printf("Total heap size: %lu" IF_USE_MUNMAP(" (%lu unmapped)") "\n",
            (unsigned long)GC_heapsize /*, */
                COMMA_IF_USE_MUNMAP((unsigned long)GC_unmapped_bytes));

  for (i = 0; i < GC_n_heap_sects; i++) {
    ptr_t start = GC_heap_sects[i].hs_start;
    size_t len = GC_heap_sects[i].hs_bytes;
    unsigned nbl = 0;
#  ifndef NO_BLACK_LISTING
    struct hblk *h;

    for (h = (struct hblk *)start; ADDR_LT((ptr_t)h, start + len); h++) {
      if (GC_is_black_listed(h, HBLKSIZE))
        nbl++;
    }
#  endif
    GC_printf("Section %u from %p to %p %u/%lu blacklisted\n", (unsigned)i,
              (void *)start, (void *)&start[len], nbl,
              (unsigned long)divHBLKSZ(len));
  }
}
#endif /* !NO_DEBUGGING */

void *GC_least_plausible_heap_addr = MAKE_CPTR(GC_WORD_MAX);
void *GC_greatest_plausible_heap_addr = NULL;

STATIC word GC_max_heapsize = 0;

GC_API void GC_CALL
GC_set_max_heap_size(GC_word n)
{
  GC_max_heapsize = n;
}

word GC_max_retries = 0;

GC_INNER void
GC_scratch_recycle_inner(void *ptr, size_t sz)
{
  size_t page_offset;
  size_t displ = 0;
  size_t recycled_bytes;

  GC_ASSERT(I_HOLD_LOCK());
  if (NULL == ptr)
    return;

  GC_ASSERT(sz != 0);
  GC_ASSERT(GC_page_size != 0);
  /* TODO: Assert correct memory flags if `GWW_VDB`. */
  page_offset = ADDR(ptr) & (GC_page_size - 1);
  if (page_offset != 0)
    displ = GC_page_size - page_offset;
  recycled_bytes = sz > displ ? (sz - displ) & ~(GC_page_size - 1) : 0;
  GC_COND_LOG_PRINTF("Recycle %lu/%lu scratch-allocated bytes at %p\n",
                     (unsigned long)recycled_bytes, (unsigned long)sz, ptr);
  if (recycled_bytes > 0)
    GC_add_to_heap((struct hblk *)((ptr_t)ptr + displ), recycled_bytes);
}

GC_INNER GC_bool
GC_expand_hp_inner(word n)
{
  size_t sz;
  struct hblk *space;
  /* Number of bytes by which we expect the heap to expand soon. */
  word expansion_slop;

  GC_ASSERT(I_HOLD_LOCK());
  GC_ASSERT(GC_page_size != 0);
  if (0 == n)
    n = 1;
  sz = ROUNDUP_PAGESIZE((size_t)n * HBLKSIZE);
  GC_DBGLOG_PRINT_HEAP_IN_USE();
  if (GC_max_heapsize != 0
      && (GC_max_heapsize < (word)sz
          || GC_heapsize > GC_max_heapsize - (word)sz)) {
    /* Exceeded the self-imposed limit. */
    return FALSE;
  }
  space = (struct hblk *)GC_os_get_mem(sz);
  if (UNLIKELY(NULL == space)) {
    WARN("Failed to expand heap by %" WARN_PRIuPTR " KiB\n", sz >> 10);
    return FALSE;
  }
  GC_last_heap_growth_gc_no = GC_gc_no;
  GC_INFOLOG_PRINTF("Grow heap to %lu KiB after %lu bytes allocated\n",
                    TO_KiB_UL(GC_heapsize + sz),
                    (unsigned long)GC_bytes_allocd);

  /*
   * Adjust heap limits generously for black-listing to work better.
   * `GC_add_to_heap()` performs minimal adjustment needed for correctness.
   */
  expansion_slop = min_bytes_allocd() + 4 * MAXHINCR * HBLKSIZE;
  if ((0 == GC_last_heap_addr && (ADDR(space) & SIGNB) == 0)
      || (GC_last_heap_addr != 0 && GC_last_heap_addr < ADDR(space))) {
    /* Assume the heap is growing up. */
    if (LIKELY(ADDR(space) < GC_WORD_MAX - (sz + expansion_slop))) {
      ptr_t new_limit = (ptr_t)space + sz + expansion_slop;

      if (ADDR_LT((ptr_t)GC_greatest_plausible_heap_addr, new_limit))
        GC_greatest_plausible_heap_addr = new_limit;
    }
  } else {
    /* Heap is growing down. */
    if (LIKELY(ADDR(space) > expansion_slop + sizeof(ptr_t))) {
      ptr_t new_limit = (ptr_t)space - expansion_slop - sizeof(ptr_t);

      if (ADDR_LT(new_limit, (ptr_t)GC_least_plausible_heap_addr))
        GC_least_plausible_heap_addr = new_limit;
    }
  }
  GC_last_heap_addr = ADDR(space);

  GC_add_to_heap(space, sz);
  if (GC_on_heap_resize)
    (*GC_on_heap_resize)(GC_heapsize);

  return TRUE;
}

GC_API int GC_CALL
GC_expand_hp(size_t bytes)
{
  size_t n_blocks = OBJ_SZ_TO_BLOCKS_CHECKED(bytes);
#ifndef NO_WARN_HEAP_GROW_WHEN_GC_DISABLED
  word old_heapsize;
#endif
  GC_bool result;

  if (UNLIKELY(!GC_is_initialized))
    GC_init();
  LOCK();
#ifndef NO_WARN_HEAP_GROW_WHEN_GC_DISABLED
  old_heapsize = GC_heapsize;
#endif
  result = GC_expand_hp_inner(n_blocks);
  if (result) {
    GC_requested_heapsize += bytes;
#ifndef NO_WARN_HEAP_GROW_WHEN_GC_DISABLED
    if (GC_dont_gc) {
      /* Do not call `WARN()` if the heap growth is intentional. */
      GC_ASSERT(GC_heapsize >= old_heapsize);
      GC_heapsize_on_gc_disable += GC_heapsize - old_heapsize;
    }
#endif
  }
  UNLOCK();
  /*
   * Really returns a `GC_bool` value, but the function is externally
   * visible, so that is clumsy.
   */
  return (int)result;
}

/*
 * The minimum value of the ratio of allocated bytes since the latest
 * collection to the amount of finalizers created since that collection
 * which triggers the collection instead heap expansion.  Has no effect
 * in the incremental mode.
 */
#if defined(GC_ALLOCD_BYTES_PER_FINALIZER) && !defined(CPPCHECK)
STATIC word GC_allocd_bytes_per_finalizer = GC_ALLOCD_BYTES_PER_FINALIZER;
#else
STATIC word GC_allocd_bytes_per_finalizer = 10000;
#endif

GC_API void GC_CALL
GC_set_allocd_bytes_per_finalizer(GC_word value)
{
  GC_allocd_bytes_per_finalizer = value;
}

GC_API GC_word GC_CALL
GC_get_allocd_bytes_per_finalizer(void)
{
  return GC_allocd_bytes_per_finalizer;
}

GC_INNER GC_bool
GC_collect_or_expand(word needed_blocks, unsigned flags, GC_bool retry)
{
  static word last_fo_entries, last_bytes_finalized;

  GC_bool gc_not_stopped = TRUE;
  word blocks_to_get;
  IF_CANCEL(int cancel_state;)

  GC_ASSERT(I_HOLD_LOCK());
  GC_ASSERT(GC_is_initialized);
  DISABLE_CANCEL(cancel_state);
  if (!GC_incremental && !GC_dont_gc
      && ((GC_dont_expand && GC_bytes_allocd > 0)
          || (GC_fo_entries > last_fo_entries
              && (last_bytes_finalized | GC_bytes_finalized) != 0
              && (GC_fo_entries - last_fo_entries)
                         * GC_allocd_bytes_per_finalizer
                     > GC_bytes_allocd)
          || GC_should_collect())) {
    /*
     * Try to do a full collection using "default" `stop_func` (unless
     * nothing has been allocated since the latest collection or heap
     * expansion is disabled).
     */
    gc_not_stopped = GC_try_to_collect_inner(
        GC_bytes_allocd > 0 && (!GC_dont_expand || !retry)
            ? GC_default_stop_func
            : GC_never_stop_func);
    if (gc_not_stopped || !retry) {
      /*
       * Either the collection has not been aborted or this is the
       * first attempt (in a loop).
       */
      last_fo_entries = GC_fo_entries;
      last_bytes_finalized = GC_bytes_finalized;
      RESTORE_CANCEL(cancel_state);
      return TRUE;
    }
  }

  blocks_to_get = (GC_heapsize - GC_heapsize_at_forced_unmap)
                      / (HBLKSIZE * GC_free_space_divisor)
                  + needed_blocks;
  if (blocks_to_get > MAXHINCR) {
#ifdef NO_BLACK_LISTING
    UNUSED_ARG(flags);
    blocks_to_get = needed_blocks > MAXHINCR ? needed_blocks : MAXHINCR;
#else
    word slop;

    /*
     * Get the minimum required to make it likely that we can satisfy
     * the current request in the presence of black-listing.  This will
     * probably be bigger than `MAXHINCR`.
     */
    if ((flags & IGNORE_OFF_PAGE) != 0) {
      slop = 4;
    } else {
      slop = 2 * divHBLKSZ(BL_LIMIT);
      if (slop > needed_blocks)
        slop = needed_blocks;
    }
    if (needed_blocks + slop > MAXHINCR) {
      blocks_to_get = needed_blocks + slop;
    } else {
      blocks_to_get = MAXHINCR;
    }
#endif
    if (blocks_to_get > divHBLKSZ(GC_WORD_MAX))
      blocks_to_get = divHBLKSZ(GC_WORD_MAX);
  } else if (blocks_to_get < MINHINCR) {
    blocks_to_get = MINHINCR;
  }

  if (GC_max_heapsize > GC_heapsize) {
    word max_get_blocks = divHBLKSZ(GC_max_heapsize - GC_heapsize);
    if (blocks_to_get > max_get_blocks)
      blocks_to_get
          = max_get_blocks > needed_blocks ? max_get_blocks : needed_blocks;
  }

#ifdef USE_MUNMAP
  if (GC_unmap_threshold > 1) {
    /*
     * Return as much memory to the OS as possible before trying to
     * get memory from it.
     */
    GC_unmap_old(0);
  }
#endif
  if (!GC_expand_hp_inner(blocks_to_get)
      && (blocks_to_get == needed_blocks
          || !GC_expand_hp_inner(needed_blocks))) {
    if (!gc_not_stopped) {
      /* Do not increment `GC_alloc_fail_count` here (and no warning). */
      GC_gcollect_inner();
      GC_ASSERT(0 == GC_bytes_allocd);
    } else if (GC_alloc_fail_count++ < GC_max_retries) {
      WARN("Out of Memory!  Trying to continue...\n", 0);
      GC_gcollect_inner();
    } else {
#ifdef USE_MUNMAP
      GC_ASSERT(GC_heapsize >= GC_unmapped_bytes);
#endif
#if !defined(SMALL_CONFIG) && (CPP_WORDSZ >= 32)
#  define MAX_HEAPSIZE_WARNED_IN_BYTES (5 << 20) /*< 5 MB */

      if (GC_heapsize > (word)MAX_HEAPSIZE_WARNED_IN_BYTES) {
        WARN("Out of Memory! Heap size: %" WARN_PRIuPTR " MiB."
             " Returning NULL!\n",
             (GC_heapsize - GC_unmapped_bytes) >> 20);
      } else
#endif
      /* else */ {
        WARN("Out of Memory! Heap size: %" WARN_PRIuPTR " bytes."
             " Returning NULL!\n",
             GC_heapsize - GC_unmapped_bytes);
      }
      RESTORE_CANCEL(cancel_state);
      return FALSE;
    }
  } else if (GC_alloc_fail_count > 0) {
    GC_COND_LOG_PRINTF("Memory available again...\n");
  }
  RESTORE_CANCEL(cancel_state);
  return TRUE;
}

GC_INNER ptr_t
GC_allocobj(size_t lg, int kind)
{
#define MAX_ALLOCOBJ_RETRIES 3
  int retry_cnt = 0;
  void **flh = &GC_obj_kinds[kind].ok_freelist[lg];
#ifndef GC_DISABLE_INCREMENTAL
  GC_bool tried_minor = FALSE;
#endif

  GC_ASSERT(I_HOLD_LOCK());
  GC_ASSERT(GC_is_initialized);
  if (UNLIKELY(0 == lg))
    return NULL;

  while (NULL == *flh) {
    /*
     * Only a few iterations are expected at most, otherwise something
     * is wrong in one of the functions called below.
     */
    if (retry_cnt > MAX_ALLOCOBJ_RETRIES)
      ABORT("Too many retries in GC_allocobj");
#ifndef GC_DISABLE_INCREMENTAL
    if (GC_incremental && GC_time_limit != GC_TIME_UNLIMITED && !GC_dont_gc) {
      /*
       * True incremental mode, not just generational.
       * Do our share of marking work.
       */
      GC_collect_a_little_inner(1);
    }
#endif
    /* Sweep blocks for objects of this size. */
    GC_ASSERT(!GC_is_full_gc || NULL == GC_obj_kinds[kind].ok_reclaim_list
              || NULL == GC_obj_kinds[kind].ok_reclaim_list[lg]);
    GC_continue_reclaim(lg, kind);
#if defined(CPPCHECK)
    GC_noop1_ptr(&flh);
#endif
    if (*flh != NULL)
      break;

    GC_new_hblk(lg, kind);
#if defined(CPPCHECK)
    GC_noop1_ptr(&flh);
#endif
    if (*flh != NULL)
      break;

#ifndef GC_DISABLE_INCREMENTAL
    if (GC_incremental && GC_time_limit == GC_TIME_UNLIMITED && !tried_minor
        && !GC_dont_gc) {
      GC_collect_a_little_inner(1);
      tried_minor = TRUE;
      continue;
    }
#endif
    if (UNLIKELY(!GC_collect_or_expand(1, 0 /* `flags` */, retry_cnt > 0)))
      return NULL;
    retry_cnt++;
  }
  /* Successful allocation; reset failure count. */
  GC_alloc_fail_count = 0;
  return (ptr_t)(*flh);
}
