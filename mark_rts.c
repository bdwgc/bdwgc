/*
 * Copyright 1988, 1989 Hans-J. Boehm, Alan J. Demers
 * Copyright (c) 1991-1994 by Xerox Corporation.  All rights reserved.
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

#if defined(E2K) && !defined(THREADS)
#  include <alloca.h>
#endif

/*
 * Data structure for list of root sets.
 * We keep a hash table, so that we can filter out duplicate additions.
 * Under Win32, we need to do a better job of filtering overlaps, so
 * we resort to sequential search, and pay the price.
 */

/* Register dynamic library data segments. */
int GC_no_dls = 0;

#if !defined(NO_DEBUGGING) || defined(GC_ASSERTIONS)
GC_INNER word
GC_compute_root_size(void)
{
  size_t i;
  word size = 0;

  for (i = 0; i < n_root_sets; i++) {
    size += (word)(GC_static_roots[i].r_end - GC_static_roots[i].r_start);
  }
  return size;
}
#endif /* !NO_DEBUGGING || GC_ASSERTIONS */

#if !defined(NO_DEBUGGING)
/* For the debugging purpose. */
void
GC_print_static_roots(void)
{
  size_t i;
  word size;

  for (i = 0; i < n_root_sets; i++) {
    GC_printf("From %p to %p%s\n", (void *)GC_static_roots[i].r_start,
              (void *)GC_static_roots[i].r_end,
              GC_static_roots[i].r_tmp ? " (temporary)" : "");
  }
  GC_printf("GC_root_size= %lu\n", (unsigned long)GC_root_size);

  if ((size = GC_compute_root_size()) != GC_root_size)
    GC_err_printf("GC_root_size incorrect!! Should be: %lu\n",
                  (unsigned long)size);
}
#endif /* !NO_DEBUGGING */

#ifndef ANY_MSWIN
GC_INLINE size_t
rt_hash(ptr_t addr)
{
  word val = ADDR(addr);

#  if CPP_WORDSZ > 4 * LOG_RT_SIZE
#    if CPP_WORDSZ > 8 * LOG_RT_SIZE
  val ^= val >> (8 * LOG_RT_SIZE);
#    endif
  val ^= val >> (4 * LOG_RT_SIZE);
#  endif
  val ^= val >> (2 * LOG_RT_SIZE);
  return (size_t)((val >> LOG_RT_SIZE) ^ val) & (RT_SIZE - 1);
}

GC_INNER void *
GC_roots_present(ptr_t b)
{
  size_t h;
  struct roots *p;

  GC_ASSERT(I_HOLD_READER_LOCK());
  h = rt_hash(b);
  for (p = GC_root_index[h]; p != NULL; p = p->r_next) {
    if (p->r_start == (ptr_t)b)
      break;
  }
  return p;
}

/* Add the given root structure to the index. */
GC_INLINE void
add_roots_to_index(struct roots *p)
{
  size_t h = rt_hash(p->r_start);

  p->r_next = GC_root_index[h];
  GC_root_index[h] = p;
}
#endif /* !ANY_MSWIN */

GC_API void GC_CALL
GC_add_roots(void *b, void *e)
{
  if (UNLIKELY(!GC_is_initialized))
    GC_init();
  LOCK();
  GC_add_roots_inner((ptr_t)b, (ptr_t)e, FALSE);
  UNLOCK();
}

GC_INNER void
GC_add_roots_inner(ptr_t b, ptr_t e, GC_bool tmp)
{
  GC_ASSERT(I_HOLD_LOCK());
  GC_ASSERT(ADDR_GE(e, b));
  b = PTR_ALIGN_UP(b, ALIGNMENT);
  e = PTR_ALIGN_DOWN(e, ALIGNMENT);
  if (ADDR_GE(b, e)) {
    /* Nothing to do. */
    return;
  }

#ifdef ANY_MSWIN
  /*
   * Spend the time to ensure that there are no overlapping or adjacent
   * intervals.  This could be done faster with e.g. a balanced tree.
   * But the execution time here is virtually guaranteed to be dominated
   * by the time it takes to scan the roots.
   */
  {
    size_t i;
    struct roots *old = NULL; /*< initialized to prevent warning */

    for (i = 0; i < n_root_sets; i++) {
      old = GC_static_roots + i;
      if (ADDR_GE(old->r_end, b) && ADDR_GE(e, old->r_start)) {
        if (ADDR_LT(b, old->r_start)) {
          GC_root_size += (word)(old->r_start - b);
          old->r_start = b;
        }
        if (ADDR_LT(old->r_end, e)) {
          GC_root_size += (word)(e - old->r_end);
          old->r_end = e;
        }
        old->r_tmp &= tmp;
        break;
      }
    }
    if (i < n_root_sets) {
      /* Merge other overlapping intervals. */
      struct roots *other;

      for (i++; i < n_root_sets; i++) {
        other = GC_static_roots + i;
        b = other->r_start;
        e = other->r_end;
        if (ADDR_GE(old->r_end, b) && ADDR_GE(e, old->r_start)) {
          if (ADDR_LT(b, old->r_start)) {
            GC_root_size += (word)(old->r_start - b);
            old->r_start = b;
          }
          if (ADDR_LT(old->r_end, e)) {
            GC_root_size += (word)(e - old->r_end);
            old->r_end = e;
          }
          old->r_tmp &= other->r_tmp;
          /* Delete this entry. */
          GC_root_size -= (word)(other->r_end - other->r_start);
          other->r_start = GC_static_roots[n_root_sets - 1].r_start;
          other->r_end = GC_static_roots[n_root_sets - 1].r_end;
          n_root_sets--;
        }
      }
      return;
    }
  }
#else
  {
    struct roots *old = (struct roots *)GC_roots_present(b);

    if (old != NULL) {
      if (ADDR_GE(old->r_end, e)) {
        old->r_tmp &= tmp;
        /* Already there. */
        return;
      }
      if (old->r_tmp == tmp || !tmp) {
        /* Extend the existing root. */
        GC_root_size += (word)(e - old->r_end);
        old->r_end = e;
        old->r_tmp = tmp;
        return;
      }
      b = old->r_end;
    }
  }
#endif
  if (n_root_sets == MAX_ROOT_SETS) {
    ABORT("Too many root sets");
  }

#ifdef DEBUG_ADD_DEL_ROOTS
  GC_log_printf("Adding data root section %u: %p .. %p%s\n",
                (unsigned)n_root_sets, (void *)b, (void *)e,
                tmp ? " (temporary)" : "");
#endif
  GC_static_roots[n_root_sets].r_start = (ptr_t)b;
  GC_static_roots[n_root_sets].r_end = (ptr_t)e;
  GC_static_roots[n_root_sets].r_tmp = tmp;
#ifndef ANY_MSWIN
  GC_static_roots[n_root_sets].r_next = 0;
  add_roots_to_index(GC_static_roots + n_root_sets);
#endif
  GC_root_size += (word)(e - b);
  n_root_sets++;
}

GC_API void GC_CALL
GC_clear_roots(void)
{
  if (UNLIKELY(!GC_is_initialized))
    GC_init();
  LOCK();
#ifdef THREADS
  GC_roots_were_cleared = TRUE;
#endif
  n_root_sets = 0;
  GC_root_size = 0;
#ifndef ANY_MSWIN
  BZERO(GC_root_index, sizeof(GC_root_index));
#endif
#ifdef DEBUG_ADD_DEL_ROOTS
  GC_log_printf("Clear all data root sections\n");
#endif
  UNLOCK();
}

STATIC void
GC_remove_root_at_pos(size_t i)
{
  GC_ASSERT(I_HOLD_LOCK());
  GC_ASSERT(i < n_root_sets);
#ifdef DEBUG_ADD_DEL_ROOTS
  GC_log_printf("Remove data root section at %u: %p .. %p%s\n", (unsigned)i,
                (void *)GC_static_roots[i].r_start,
                (void *)GC_static_roots[i].r_end,
                GC_static_roots[i].r_tmp ? " (temporary)" : "");
#endif
  GC_root_size
      -= (word)(GC_static_roots[i].r_end - GC_static_roots[i].r_start);
  GC_static_roots[i].r_start = GC_static_roots[n_root_sets - 1].r_start;
  GC_static_roots[i].r_end = GC_static_roots[n_root_sets - 1].r_end;
  GC_static_roots[i].r_tmp = GC_static_roots[n_root_sets - 1].r_tmp;
  n_root_sets--;
}

#ifndef ANY_MSWIN
STATIC void
GC_rebuild_root_index(void)
{
  size_t i;

  BZERO(GC_root_index, sizeof(GC_root_index));
  for (i = 0; i < n_root_sets; i++)
    add_roots_to_index(GC_static_roots + i);
}
#endif /* !ANY_MSWIN */

#if defined(ANY_MSWIN) || defined(DYNAMIC_LOADING)
STATIC void
GC_remove_tmp_roots(void)
{
  size_t i;
#  ifndef ANY_MSWIN
  size_t old_n_roots = n_root_sets;
#  endif

  GC_ASSERT(I_HOLD_LOCK());
  for (i = 0; i < n_root_sets;) {
    if (GC_static_roots[i].r_tmp) {
      GC_remove_root_at_pos(i);
    } else {
      i++;
    }
  }
#  ifndef ANY_MSWIN
  if (n_root_sets < old_n_roots)
    GC_rebuild_root_index();
#  endif
}
#endif /* ANY_MSWIN || DYNAMIC_LOADING */

STATIC void GC_remove_roots_inner(ptr_t b, ptr_t e);

GC_API void GC_CALL
GC_remove_roots(void *b, void *e)
{
  /* A quick check whether has nothing to do. */
  if (ADDR_GE(PTR_ALIGN_UP((ptr_t)b, ALIGNMENT),
              PTR_ALIGN_DOWN((ptr_t)e, ALIGNMENT)))
    return;

  LOCK();
  GC_remove_roots_inner((ptr_t)b, (ptr_t)e);
  UNLOCK();
}

STATIC void
GC_remove_roots_inner(ptr_t b, ptr_t e)
{
  size_t i;
#ifndef ANY_MSWIN
  size_t old_n_roots = n_root_sets;
#endif

  GC_ASSERT(I_HOLD_LOCK());
  for (i = 0; i < n_root_sets;) {
    if (ADDR_GE(GC_static_roots[i].r_start, b)
        && ADDR_GE(e, GC_static_roots[i].r_end)) {
      GC_remove_root_at_pos(i);
    } else {
      i++;
    }
  }
#ifndef ANY_MSWIN
  if (n_root_sets < old_n_roots)
    GC_rebuild_root_index();
#endif
}

#ifdef USE_PROC_FOR_LIBRARIES
/*
 * Exchange the elements of the roots table.  Requires rebuild of the roots
 * index table after the swap.
 */
GC_INLINE void
swap_static_roots(size_t i, size_t j)
{
  ptr_t r_start = GC_static_roots[i].r_start;
  ptr_t r_end = GC_static_roots[i].r_end;
  GC_bool r_tmp = GC_static_roots[i].r_tmp;

  GC_static_roots[i].r_start = GC_static_roots[j].r_start;
  GC_static_roots[i].r_end = GC_static_roots[j].r_end;
  GC_static_roots[i].r_tmp = GC_static_roots[j].r_tmp;
  /* No need to swap `r_next` values. */
  GC_static_roots[j].r_start = r_start;
  GC_static_roots[j].r_end = r_end;
  GC_static_roots[j].r_tmp = r_tmp;
}

GC_INNER void
GC_remove_roots_subregion(ptr_t b, ptr_t e)
{
  size_t i;
  GC_bool rebuild = FALSE;

  GC_ASSERT(I_HOLD_LOCK());
  GC_ASSERT(ADDR(b) % ALIGNMENT == 0 && ADDR(e) % ALIGNMENT == 0);
  for (i = 0; i < n_root_sets; i++) {
    ptr_t r_start, r_end;

    if (GC_static_roots[i].r_tmp) {
      /* The remaining roots are skipped as they are all temporary. */
#  ifdef GC_ASSERTIONS
      size_t j;

      for (j = i + 1; j < n_root_sets; j++) {
        GC_ASSERT(GC_static_roots[j].r_tmp);
      }
#  endif
      break;
    }
    r_start = GC_static_roots[i].r_start;
    r_end = GC_static_roots[i].r_end;
    if (ADDR_GE(r_start, e) || LIKELY(ADDR_GE(b, r_end)))
      continue;

#  ifdef DEBUG_ADD_DEL_ROOTS
    GC_log_printf("Removing %p .. %p from root section %u (%p .. %p)\n",
                  (void *)b, (void *)e, (unsigned)i, (void *)r_start,
                  (void *)r_end);
#  endif
    if (ADDR_LT(r_start, b)) {
      GC_root_size -= (word)(r_end - b);
      GC_static_roots[i].r_end = b;
      /* No need to rebuild as hash does not use `r_end` value. */
      if (ADDR_LT(e, r_end)) {
        size_t j;

        if (rebuild) {
          GC_rebuild_root_index();
          rebuild = FALSE;
        }
        /* Note: updates `n_root_sets` as well. */
        GC_add_roots_inner(e, r_end, FALSE);
        for (j = i + 1; j < n_root_sets; j++)
          if (GC_static_roots[j].r_tmp)
            break;
        if (j < n_root_sets - 1 && !GC_static_roots[n_root_sets - 1].r_tmp) {
          /* Exchange the roots to have all temporary ones at the end. */
          swap_static_roots(j, n_root_sets - 1);
          rebuild = TRUE;
        }
      }
    } else {
      if (ADDR_LT(e, r_end)) {
        GC_root_size -= (word)(e - r_start);
        GC_static_roots[i].r_start = e;
      } else {
        GC_remove_root_at_pos(i);
        if (i + 1 < n_root_sets && GC_static_roots[i].r_tmp
            && !GC_static_roots[i + 1].r_tmp) {
          size_t j;

          for (j = i + 2; j < n_root_sets; j++)
            if (GC_static_roots[j].r_tmp)
              break;
          /* Exchange the roots to have all temporary ones at the end. */
          swap_static_roots(i, j - 1);
        }
        i--;
      }
      rebuild = TRUE;
    }
  }
  if (rebuild)
    GC_rebuild_root_index();
}
#endif /* USE_PROC_FOR_LIBRARIES */

#if !defined(NO_DEBUGGING)
GC_API int GC_CALL
GC_is_tmp_root(void *p)
{
#  ifndef HAS_REAL_READER_LOCK
  static size_t last_root_set; /*< initialized to 0; no shared access */
#  elif defined(AO_HAVE_load) || defined(AO_HAVE_store)
  static volatile AO_t last_root_set;
#  else
  /* Note: a race is acceptable, it is just a cached index. */
  static volatile size_t last_root_set;
#  endif
  size_t i;
  int res;

  READER_LOCK();
  /* First try the cached root. */
#  if defined(AO_HAVE_load) && defined(HAS_REAL_READER_LOCK)
  i = AO_load(&last_root_set);
#  else
  i = last_root_set;
#  endif
  if (i < n_root_sets
      && ADDR_INSIDE((ptr_t)p, GC_static_roots[i].r_start,
                     GC_static_roots[i].r_end)) {
    res = (int)GC_static_roots[i].r_tmp;
  } else {
    res = 0;
    for (i = 0; i < n_root_sets; i++) {
      if (ADDR_INSIDE((ptr_t)p, GC_static_roots[i].r_start,
                      GC_static_roots[i].r_end)) {
        res = (int)GC_static_roots[i].r_tmp;
#  if defined(AO_HAVE_store) && defined(HAS_REAL_READER_LOCK)
        AO_store(&last_root_set, i);
#  else
        last_root_set = i;
#  endif
        break;
      }
    }
  }
  READER_UNLOCK();
  return res;
}
#endif /* !NO_DEBUGGING */

GC_INNER ptr_t
GC_approx_sp(void)
{
  volatile ptr_t sp;

  /*
   * This also forces stack to grow if necessary.  Otherwise the later
   * accesses might cause the kernel to think we are doing something wrong.
   */
  STORE_APPROX_SP_TO(sp);
  return (/* no volatile */ ptr_t)sp;
}

GC_API void GC_CALL
GC_clear_exclusion_table(void)
{
#ifdef DEBUG_ADD_DEL_ROOTS
  GC_log_printf("Clear static root exclusions (%u elements)\n",
                (unsigned)GC_excl_table_entries);
#endif
  GC_excl_table_entries = 0;
}

/*
 * Return the first exclusion range that includes an address not lower
 * than `start_addr`.
 */
STATIC struct exclusion *
GC_next_exclusion(ptr_t start_addr)
{
  size_t low = 0;
  size_t high;

  if (UNLIKELY(0 == GC_excl_table_entries))
    return NULL;
  high = GC_excl_table_entries - 1;
  while (high > low) {
    size_t mid = (low + high) >> 1;

    /* `low` <= `mid` < `high`. */
    if (ADDR_GE(start_addr, GC_excl_table[mid].e_end)) {
      low = mid + 1;
    } else {
      high = mid;
    }
  }
  if (ADDR_GE(start_addr, GC_excl_table[low].e_end))
    return NULL;

  return GC_excl_table + low;
}

GC_INNER void
GC_exclude_static_roots_inner(ptr_t start, ptr_t finish)
{
  struct exclusion *next;
  size_t next_index;

  GC_ASSERT(I_HOLD_LOCK());
  GC_ASSERT(ADDR(start) % ALIGNMENT == 0);
  GC_ASSERT(ADDR_LT(start, finish));

  next = GC_next_exclusion(start);
  if (next != NULL) {
    if (ADDR_LT(next->e_start, finish)) {
      /* Incomplete error check. */
      ABORT("Exclusion ranges overlap");
    }
    if (ADDR(next->e_start) == ADDR(finish)) {
      /* Extend old range backwards. */
      next->e_start = start;
#ifdef DEBUG_ADD_DEL_ROOTS
      GC_log_printf("Updating static root exclusion to %p .. %p\n",
                    (void *)start, (void *)next->e_end);
#endif
      return;
    }
  }

  next_index = GC_excl_table_entries;
  if (next_index >= MAX_EXCLUSIONS)
    ABORT("Too many exclusions");
  if (next != NULL) {
    size_t i;

    next_index = (size_t)(next - GC_excl_table);
    for (i = GC_excl_table_entries; i > next_index; --i) {
      GC_excl_table[i] = GC_excl_table[i - 1];
    }
  }
#ifdef DEBUG_ADD_DEL_ROOTS
  GC_log_printf("Adding static root exclusion at %u: %p .. %p\n",
                (unsigned)next_index, (void *)start, (void *)finish);
#endif
  GC_excl_table[next_index].e_start = start;
  GC_excl_table[next_index].e_end = finish;
  ++GC_excl_table_entries;
}

GC_API void GC_CALL
GC_exclude_static_roots(void *b, void *e)
{
  if (b == e) {
    /* Nothing to exclude. */
    return;
  }

  /* Round boundaries in direction reverse to that of `GC_add_roots`. */
#if ALIGNMENT > 1
  b = PTR_ALIGN_DOWN((ptr_t)b, ALIGNMENT);
  e = UNLIKELY(ADDR(e) > ~(word)(ALIGNMENT - 1))
          ? PTR_ALIGN_DOWN((ptr_t)e, ALIGNMENT) /*< overflow */
          : PTR_ALIGN_UP((ptr_t)e, ALIGNMENT);
#endif

  LOCK();
  GC_exclude_static_roots_inner((ptr_t)b, (ptr_t)e);
  UNLOCK();
}

#if defined(WRAP_MARK_SOME) && defined(PARALLEL_MARK)
#  define GC_PUSH_CONDITIONAL(b, t, all)                \
    (GC_parallel ? GC_push_conditional_eager(b, t, all) \
                 : GC_push_conditional_static(b, t, all))
#else
#  define GC_PUSH_CONDITIONAL(b, t, all) GC_push_conditional_static(b, t, all)
#endif

/* Invoke `GC_push_conditional` on ranges that are not excluded. */
STATIC void
GC_push_conditional_with_exclusions(ptr_t bottom, ptr_t top, GC_bool all)
{
  while (ADDR_LT(bottom, top)) {
    struct exclusion *next = GC_next_exclusion(bottom);
    ptr_t excl_start = top;

    if (next != NULL) {
      if (ADDR_GE(next->e_start, top)) {
        next = NULL;
      } else {
        excl_start = next->e_start;
      }
    }
    if (ADDR_LT(bottom, excl_start))
      GC_PUSH_CONDITIONAL(bottom, excl_start, all);
    if (NULL == next)
      break;
    bottom = next->e_end;
  }
}

#ifdef IA64
GC_INNER void
GC_push_all_register_sections(ptr_t bs_lo, ptr_t bs_hi, GC_bool eager,
                              struct GC_traced_stack_sect_s *traced_stack_sect)
{
  GC_ASSERT(I_HOLD_LOCK());
  while (traced_stack_sect != NULL) {
    ptr_t frame_bs_lo = traced_stack_sect->backing_store_end;

    GC_ASSERT(ADDR_GE(bs_hi, frame_bs_lo));
    if (eager) {
      GC_push_all_eager(frame_bs_lo, bs_hi);
    } else {
      GC_push_all_stack(frame_bs_lo, bs_hi);
    }
    bs_hi = traced_stack_sect->saved_backing_store_ptr;
    traced_stack_sect = traced_stack_sect->prev;
  }
  GC_ASSERT(ADDR_GE(bs_hi, bs_lo));
  if (eager) {
    GC_push_all_eager(bs_lo, bs_hi);
  } else {
    GC_push_all_stack(bs_lo, bs_hi);
  }
}
#endif /* IA64 */

#ifdef THREADS

GC_INNER void
GC_push_all_stack_sections(ptr_t lo /* top */, ptr_t hi /* bottom */,
                           struct GC_traced_stack_sect_s *traced_stack_sect)
{
  GC_ASSERT(I_HOLD_LOCK());
  while (traced_stack_sect != NULL) {
    GC_ASSERT(HOTTER_THAN(lo, (ptr_t)traced_stack_sect));
#  ifdef STACK_GROWS_UP
    GC_push_all_stack((ptr_t)traced_stack_sect, lo);
#  else
    GC_push_all_stack(lo, (ptr_t)traced_stack_sect);
#  endif
    lo = traced_stack_sect->saved_stack_ptr;
    GC_ASSERT(lo != NULL);
    traced_stack_sect = traced_stack_sect->prev;
  }
  GC_ASSERT(!HOTTER_THAN(hi, lo));
#  ifdef STACK_GROWS_UP
  /* We got them backwards! */
  GC_push_all_stack(hi, lo);
#  else
  GC_push_all_stack(lo, hi);
#  endif
}

#else /* !THREADS */

/*
 * Similar to `GC_push_all_eager`, but only the part hotter than
 * `cold_gc_frame` is scanned immediately.  Needed to ensure that
 * callee-save registers are not missed.  Treats all interior pointers
 * as valid and scans part of the area immediately, to make sure that
 * saved register values are not lost.  `cold_gc_frame` delimits the
 * stack section that must be scanned eagerly.  A zero value indicates
 * that no eager scanning is needed.  We do not need to worry about
 * the manual VDB case here, since this is only called in the
 * single-threaded case.  We assume that we cannot collect between
 * an assignment and the corresponding `GC_dirty()` call.
 */
STATIC void
GC_push_all_stack_partially_eager(ptr_t bottom, ptr_t top, ptr_t cold_gc_frame)
{
#  ifndef NEED_FIXUP_POINTER
  if (GC_all_interior_pointers) {
    /*
     * Push the hot end of the stack eagerly, so that register values saved
     * inside GC frames are marked before they disappear.  The rest of the
     * marking can be deferred until later.
     */
    if (0 == cold_gc_frame) {
      GC_push_all_stack(bottom, top);
      return;
    }
    GC_ASSERT(ADDR_GE(cold_gc_frame, bottom) && ADDR_GE(top, cold_gc_frame));
#    ifdef STACK_GROWS_UP
    GC_push_all(bottom, cold_gc_frame + sizeof(ptr_t));
    GC_push_all_eager(cold_gc_frame, top);
#    else
    GC_push_all(cold_gc_frame - sizeof(ptr_t), top);
    GC_push_all_eager(bottom, cold_gc_frame);
#    endif
  } else
#  endif
  /* else */ {
    GC_push_all_eager(bottom, top);
  }
#  ifdef TRACE_BUF
  GC_add_trace_entry("GC_push_all_stack", bottom, top);
#  endif
}

/* Similar to `GC_push_all_stack_sections()` but also uses `cold_gc_frame`. */
STATIC void
GC_push_all_stack_part_eager_sections(
    ptr_t lo /* top */, ptr_t hi /* bottom */, ptr_t cold_gc_frame,
    struct GC_traced_stack_sect_s *traced_stack_sect)
{
  GC_ASSERT(traced_stack_sect == NULL || cold_gc_frame == NULL
            || HOTTER_THAN(cold_gc_frame, (ptr_t)traced_stack_sect));

  while (traced_stack_sect != NULL) {
    GC_ASSERT(HOTTER_THAN(lo, (ptr_t)traced_stack_sect));
#  ifdef STACK_GROWS_UP
    GC_push_all_stack_partially_eager((ptr_t)traced_stack_sect, lo,
                                      cold_gc_frame);
#  else
    GC_push_all_stack_partially_eager(lo, (ptr_t)traced_stack_sect,
                                      cold_gc_frame);
#  endif
    lo = traced_stack_sect->saved_stack_ptr;
    GC_ASSERT(lo != NULL);
    traced_stack_sect = traced_stack_sect->prev;
    /* Note: use at most once. */
    cold_gc_frame = NULL;
  }

  GC_ASSERT(!HOTTER_THAN(hi, lo));
#  ifdef STACK_GROWS_UP
  /* We got them backwards! */
  GC_push_all_stack_partially_eager(hi, lo, cold_gc_frame);
#  else
  GC_push_all_stack_partially_eager(lo, hi, cold_gc_frame);
#  endif
}

#endif /* !THREADS */

/*
 * Push enough of the current stack eagerly to ensure that callee-save
 * registers saved in GC frames are scanned.  In the single-threaded case,
 * schedule the entire stack for scanning.  The 2nd argument (`context`)
 * is a pointer to the (possibly `NULL`) thread context, for (currently
 * hypothetical) more precise stack scanning.  In the presence of threads,
 * push enough of the current stack to ensure that callee-save registers
 * saved in collector frames have been seen.
 */
/* TODO: Merge it with per-thread stuff. */
STATIC void
GC_push_current_stack(ptr_t cold_gc_frame, void *context)
{
  UNUSED_ARG(context);
  GC_ASSERT(I_HOLD_LOCK());
#if defined(THREADS)
  /* `cold_gc_frame` is non-`NULL`. */
#  ifdef STACK_GROWS_UP
  GC_push_all_eager(cold_gc_frame, GC_approx_sp());
#  else
  GC_push_all_eager(GC_approx_sp(), cold_gc_frame);
  /*
   * For IA-64, the register stack backing store is handled in the
   * thread-specific code.
   */
#  endif
#else
  GC_push_all_stack_part_eager_sections(GC_approx_sp(), GC_stackbottom,
                                        cold_gc_frame, GC_traced_stack_sect);
#  ifdef IA64
  /*
   * We also need to push the register stack backing store.
   * This should really be done in the same way as the regular stack.
   * For now we fudge it a bit.  Note that the backing store grows up,
   * so we cannot use `GC_push_all_stack_partially_eager`.
   */
  {
    ptr_t bsp = GC_save_regs_ret_val;
    ptr_t cold_gc_bs_pointer = bsp - 2048;
    if (GC_all_interior_pointers
        && ADDR_LT(GC_register_stackbottom, cold_gc_bs_pointer)) {
      /*
       * Adjust `cold_gc_bs_pointer` if below our innermost
       * "traced stack section" in backing store.
       */
      if (GC_traced_stack_sect != NULL
          && ADDR_LT(cold_gc_bs_pointer,
                     GC_traced_stack_sect->backing_store_end)) {
        cold_gc_bs_pointer = GC_traced_stack_sect->backing_store_end;
      }
      GC_push_all_register_sections(GC_register_stackbottom,
                                    cold_gc_bs_pointer, FALSE,
                                    GC_traced_stack_sect);
      GC_push_all_eager(cold_gc_bs_pointer, bsp);
    } else {
      GC_push_all_register_sections(GC_register_stackbottom, bsp,
                                    TRUE /* `eager` */, GC_traced_stack_sect);
    }
    /*
     * All values should be sufficiently aligned that we do not have to
     * worry about the boundary.
     */
  }
#  elif defined(E2K)
  /* We also need to push procedure stack store.  Procedure stack grows up. */
  {
    ptr_t bs_lo;
    size_t stack_size;

    /* TODO: Support `ps_ofs` here and in `GC_do_blocking_inner`. */
    GET_PROCEDURE_STACK_LOCAL(0, &bs_lo, &stack_size);
    GC_push_all_eager(bs_lo, bs_lo + stack_size);
  }
#  endif
#endif /* !THREADS */
}

GC_INNER void (*GC_push_typed_structures)(void) = 0;

GC_INNER void
GC_cond_register_dynamic_libraries(void)
{
  GC_ASSERT(I_HOLD_LOCK());
#if defined(DYNAMIC_LOADING) && !defined(MSWIN_XBOX1) || defined(ANY_MSWIN)
  GC_remove_tmp_roots();
  if (!GC_no_dls)
    GC_register_dynamic_libraries();
#else
  GC_no_dls = TRUE;
#endif
}

STATIC void
GC_push_regs_and_stack(ptr_t cold_gc_frame)
{
  GC_ASSERT(I_HOLD_LOCK());
#ifdef THREADS
  if (NULL == cold_gc_frame) {
    /* `GC_push_all_stacks` should push registers and stack. */
    return;
  }
#endif
  GC_with_callee_saves_pushed(GC_push_current_stack, cold_gc_frame);
}

GC_INNER void
GC_push_roots(GC_bool all, ptr_t cold_gc_frame)
{
  size_t i;
  unsigned kind;

  GC_ASSERT(I_HOLD_LOCK());

  /* The initialization is needed for `GC_push_all_stacks`. */
  GC_ASSERT(GC_is_initialized);

  /*
   * Next push static data.  This must happen early on, since it is not
   * robust against mark stack overflow.  Re-register dynamic libraries,
   * in case one got added.  There is some argument for doing this as late
   * as possible, especially on Win32, where it can change asynchronously.
   * In those cases, we do it here.  But on other platforms, it is not safe
   * with the world stopped, so we do it earlier.
   */
#if !defined(REGISTER_LIBRARIES_EARLY)
  GC_cond_register_dynamic_libraries();
#endif

  /* Mark everything in static data areas. */
  for (i = 0; i < n_root_sets; i++) {
    GC_push_conditional_with_exclusions(GC_static_roots[i].r_start,
                                        GC_static_roots[i].r_end, all);
  }

  /*
   * Mark all free-list header blocks, if those were allocated from
   * the garbage-collected heap.  This makes sure they do not disappear
   * if we are not marking from static data.  It also saves us the trouble
   * of scanning them, and possibly that of marking the free lists.
   */
  for (kind = 0; kind < GC_n_kinds; kind++) {
    const void *base = GC_base(GC_obj_kinds[kind].ok_freelist);

    if (base != NULL) {
      GC_set_mark_bit(base);
    }
  }

  /*
   * Mark from the collector internal roots if those might otherwise
   * have been excluded.
   */
#ifndef GC_NO_FINALIZATION
  GC_push_finalizer_structures();
#endif
#ifdef THREADS
  if (GC_no_dls || GC_roots_were_cleared)
    GC_push_thread_structures();
#endif
  if (GC_push_typed_structures) {
    GC_push_typed_structures();
  }

#if defined(THREAD_LOCAL_ALLOC)
  /*
   * Mark thread-local free lists, even if their mark descriptor excludes
   * the link field.  If the world is not stopped, this is unsafe.
   * It is also unnecessary, since we will do this again with the world
   * stopped.
   */
  if (GC_world_stopped) {
    GC_mark_thread_local_free_lists();
  }
#endif

  /*
   * Now traverse stacks, and mark from register contents.
   * These must be done last, since they can legitimately overflow
   * the mark stack.  This is usually done by saving the current
   * context on the stack, and then just tracing from the stack.
   */
#ifdef STACK_NOT_SCANNED
  UNUSED_ARG(cold_gc_frame);
#else
  GC_push_regs_and_stack(cold_gc_frame);
#endif

  if (GC_push_other_roots != 0) {
    /*
     * In the multi-threaded case, this also pushes thread stacks.
     * Note that without the interior pointers recognition lots of stuff
     * may have already been pushed, and this should be careful about
     * mark stack overflows.
     */
    (*GC_push_other_roots)();
  }
}
