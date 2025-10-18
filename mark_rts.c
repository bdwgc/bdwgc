/*
 * Copyright 1988, 1989 Hans-J. Boehm, Alan J. Demers
 * Copyright (c) 1991-1994 by Xerox Corporation.  All rights reserved.
 * Copyright (c) 2009-2024 Ivan Maidanski
 *
 * THIS MATERIAL IS PROVIDED AS IS, WITH ABSOLUTELY NO WARRANTY EXPRESSED
 * OR IMPLIED.  ANY USE IS AT YOUR OWN RISK.
 *
 * Permission is hereby granted to use or copy this program
 * for any purpose,  provided the above notices are retained on all copies.
 * Permission to modify the code and to distribute modified code is granted,
 * provided the above notices are retained, and a notice that the code was
 * modified is included with the above copyright notice.
 */

#include "private/gc_priv.h"

#include <stdio.h>

/* Data structure for list of root sets.                                */
/* We keep a hash table, so that we can filter out duplicate additions. */
/* Under Win32, we need to do a better job of filtering overlaps, so    */
/* we resort to sequential search, and pay the price.                   */
/* This is really declared in gc_priv.h:
struct roots {
        ptr_t r_start;
        ptr_t r_end;
#       if !defined(MSWIN32) && !defined(MSWINCE) && !defined(CYGWIN32)
          struct roots * r_next;
#       endif
        GC_bool r_tmp;
                -- Delete before registering new dynamic libraries
};

struct roots GC_static_roots[MAX_ROOT_SETS];
*/

int GC_no_dls = 0;      /* Register dynamic library data segments.      */

#if !defined(NO_DEBUGGING) || defined(GC_ASSERTIONS)
  /* Should return the same value as GC_root_size.      */
  GC_INNER word GC_compute_root_size(void)
  {
    int i;
    word size = 0;

    for (i = 0; i < n_root_sets; i++) {
      size += GC_static_roots[i].r_end - GC_static_roots[i].r_start;
    }
    return size;
  }
#endif /* !NO_DEBUGGING || GC_ASSERTIONS */

#if !defined(NO_DEBUGGING)
  /* For debugging:     */
  void GC_print_static_roots(void)
  {
    int i;
    word size;

    for (i = 0; i < n_root_sets; i++) {
        GC_printf("From %p to %p%s\n",
                  (void *)GC_static_roots[i].r_start,
                  (void *)GC_static_roots[i].r_end,
                  GC_static_roots[i].r_tmp ? " (temporary)" : "");
    }
    GC_printf("GC_root_size= %lu\n", (unsigned long)GC_root_size);

    if ((size = GC_compute_root_size()) != GC_root_size)
      GC_err_printf("GC_root_size incorrect!! Should be: %lu\n",
                    (unsigned long)size);
  }
#endif /* !NO_DEBUGGING */

#ifndef THREADS
  /* Primarily for debugging support:     */
  /* Is the address p in one of the registered static root sections?      */
  GC_INNER GC_bool GC_is_static_root(void *p)
  {
    static int last_root_set = MAX_ROOT_SETS;
    int i;

    if (last_root_set < n_root_sets
        && (word)p >= (word)GC_static_roots[last_root_set].r_start
        && (word)p < (word)GC_static_roots[last_root_set].r_end)
      return(TRUE);
    for (i = 0; i < n_root_sets; i++) {
        if ((word)p >= (word)GC_static_roots[i].r_start
            && (word)p < (word)GC_static_roots[i].r_end) {
          last_root_set = i;
          return(TRUE);
        }
    }
    return(FALSE);
  }
#endif /* !THREADS */

#if !defined(MSWIN32) && !defined(MSWINCE) && !defined(CYGWIN32)
/*
#   define LOG_RT_SIZE 6
#   define RT_SIZE (1 << LOG_RT_SIZE)  -- Power of 2, may be != MAX_ROOT_SETS

    struct roots * GC_root_index[RT_SIZE];
        -- Hash table header.  Used only to check whether a range is
        -- already present.
        -- really defined in gc_priv.h
*/

  GC_INLINE int rt_hash(ptr_t addr)
  {
    word result = (word) addr;
#   if CPP_WORDSZ > 8*LOG_RT_SIZE
        result ^= result >> 8*LOG_RT_SIZE;
#   endif
#   if CPP_WORDSZ > 4*LOG_RT_SIZE
        result ^= result >> 4*LOG_RT_SIZE;
#   endif
    result ^= result >> 2*LOG_RT_SIZE;
    result ^= result >> LOG_RT_SIZE;
    result &= (RT_SIZE-1);
    return(result);
  }

  /* Is a range starting at b already in the table? If so return a      */
  /* pointer to it, else NULL.                                          */
  GC_INNER void * GC_roots_present(ptr_t b)
  {
    int h = rt_hash(b);
    struct roots *p = GC_root_index[h];

    while (p != 0) {
        if (p -> r_start == (ptr_t)b) return(p);
        p = p -> r_next;
    }
    return NULL;
  }

  /* Add the given root structure to the index. */
  GC_INLINE void add_roots_to_index(struct roots *p)
  {
    int h = rt_hash(p -> r_start);

    p -> r_next = GC_root_index[h];
    GC_root_index[h] = p;
  }
#endif /* !MSWIN32 && !MSWINCE && !CYGWIN32 */

GC_INNER word GC_root_size = 0;

GC_API void GC_CALL GC_add_roots(void *b, void *e)
{
    DCL_LOCK_STATE;

    if (!EXPECT(GC_is_initialized, TRUE)) GC_init();
    LOCK();
    GC_add_roots_inner((ptr_t)b, (ptr_t)e, FALSE);
    UNLOCK();
}


/* Add [b,e) to the root set.  Adding the same interval a second time   */
/* is a moderately fast no-op, and hence benign.  We do not handle      */
/* different but overlapping intervals efficiently.  (We do handle      */
/* them correctly.)                                                     */
/* Tmp specifies that the interval may be deleted before                */
/* re-registering dynamic libraries.                                    */
void GC_add_roots_inner(ptr_t b, ptr_t e, GC_bool tmp)
{
    GC_ASSERT((word)b <= (word)e);
    b = (ptr_t)(((word)b + (ALIGNMENT - 1)) & ~(word)(ALIGNMENT - 1));
                        /* round b up to pointer alignment boundary */
    e = (ptr_t)((word)e & ~(word)(ALIGNMENT - 1));
                        /* round e down to pointer alignment boundary */
    if ((word)b >= (word)e) return; /* nothing to do */

#   if defined(MSWIN32) || defined(MSWINCE) || defined(CYGWIN32)
      /* Spend the time to ensure that there are no overlapping */
      /* or adjacent intervals.                                 */
      /* This could be done faster with e.g. a                  */
      /* balanced tree.  But the execution time here is         */
      /* virtually guaranteed to be dominated by the time it    */
      /* takes to scan the roots.                               */
      {
        int i;
        struct roots * old = NULL; /* initialized to prevent warning. */

        for (i = 0; i < n_root_sets; i++) {
            old = GC_static_roots + i;
            if ((word)b <= (word)old->r_end
                 && (word)e >= (word)old->r_start) {
                if ((word)b < (word)old->r_start) {
                    GC_root_size += old->r_start - b;
                    old -> r_start = b;
                }
                if ((word)e > (word)old->r_end) {
                    GC_root_size += e - old->r_end;
                    old -> r_end = e;
                }
                old -> r_tmp &= tmp;
                break;
            }
        }
        if (i < n_root_sets) {
          /* merge other overlapping intervals */
            struct roots *other;

            for (i++; i < n_root_sets; i++) {
              other = GC_static_roots + i;
              b = other -> r_start;
              e = other -> r_end;
              if ((word)b <= (word)old->r_end
                  && (word)e >= (word)old->r_start) {
                if ((word)b < (word)old->r_start) {
                    GC_root_size += old->r_start - b;
                    old -> r_start = b;
                }
                if ((word)e > (word)old->r_end) {
                    GC_root_size += e - old->r_end;
                    old -> r_end = e;
                }
                old -> r_tmp &= other -> r_tmp;
                /* Delete this entry. */
                  GC_root_size -= (other -> r_end - other -> r_start);
                  other -> r_start = GC_static_roots[n_root_sets-1].r_start;
                  other -> r_end = GC_static_roots[n_root_sets-1].r_end;
                  n_root_sets--;
              }
            }
          return;
        }
      }
#   else
      {
        struct roots * old = (struct roots *)GC_roots_present(b);

        if (old != 0) {
          if ((word)e <= (word)old->r_end) {
            old -> r_tmp &= tmp;
            return; /* already there */
          }
          if (old -> r_tmp == tmp || !tmp) {
            /* Extend the existing root. */
            GC_root_size += e - old -> r_end;
            old -> r_end = e;
            old -> r_tmp = tmp;
            return;
          }
          b = old -> r_end;
        }
      }
#   endif
    if (n_root_sets == MAX_ROOT_SETS) {
        ABORT("Too many root sets");
    }

#   ifdef DEBUG_ADD_DEL_ROOTS
      GC_log_printf("Adding data root section %d: %p .. %p%s\n",
                    n_root_sets, (void *)b, (void *)e,
                    tmp ? " (temporary)" : "");
#   endif
    GC_static_roots[n_root_sets].r_start = (ptr_t)b;
    GC_static_roots[n_root_sets].r_end = (ptr_t)e;
    GC_static_roots[n_root_sets].r_tmp = tmp;
#   if !defined(MSWIN32) && !defined(MSWINCE) && !defined(CYGWIN32)
      GC_static_roots[n_root_sets].r_next = 0;
      add_roots_to_index(GC_static_roots + n_root_sets);
#   endif
    GC_root_size += e - b;
    n_root_sets++;
}

GC_API void GC_CALL GC_clear_roots(void)
{
    DCL_LOCK_STATE;

    if (!EXPECT(GC_is_initialized, TRUE)) GC_init();
    LOCK();
#   ifdef THREADS
      GC_roots_were_cleared = TRUE;
#   endif
    n_root_sets = 0;
    GC_root_size = 0;
#   if !defined(MSWIN32) && !defined(MSWINCE) && !defined(CYGWIN32)
      BZERO(GC_root_index, RT_SIZE * sizeof(void *));
#   endif
#   ifdef DEBUG_ADD_DEL_ROOTS
      GC_log_printf("Clear all data root sections\n");
#   endif
    UNLOCK();
}

/* Internal use only; lock held.        */
STATIC void GC_remove_root_at_pos(int i)
{
#   ifdef DEBUG_ADD_DEL_ROOTS
      GC_log_printf("Remove data root section at %d: %p .. %p%s\n",
                    i, (void *)GC_static_roots[i].r_start,
                    (void *)GC_static_roots[i].r_end,
                    GC_static_roots[i].r_tmp ? " (temporary)" : "");
#   endif
    GC_root_size -= (GC_static_roots[i].r_end - GC_static_roots[i].r_start);
    GC_static_roots[i].r_start = GC_static_roots[n_root_sets-1].r_start;
    GC_static_roots[i].r_end = GC_static_roots[n_root_sets-1].r_end;
    GC_static_roots[i].r_tmp = GC_static_roots[n_root_sets-1].r_tmp;
    n_root_sets--;
}

#if !defined(MSWIN32) && !defined(MSWINCE) && !defined(CYGWIN32)
  STATIC void GC_rebuild_root_index(void)
  {
    int i;
    BZERO(GC_root_index, RT_SIZE * sizeof(void *));
    for (i = 0; i < n_root_sets; i++)
        add_roots_to_index(GC_static_roots + i);
  }
#endif

#if defined(DYNAMIC_LOADING) || defined(MSWIN32) || defined(MSWINCE) \
     || defined(PCR) || defined(CYGWIN32)
/* Internal use only; lock held.        */
STATIC void GC_remove_tmp_roots(void)
{
    int i;
#   if !defined(MSWIN32) && !defined(MSWINCE) && !defined(CYGWIN32)
      int old_n_roots = n_root_sets;
#   endif

    for (i = 0; i < n_root_sets; ) {
        if (GC_static_roots[i].r_tmp) {
            GC_remove_root_at_pos(i);
        } else {
            i++;
        }
    }
#   if !defined(MSWIN32) && !defined(MSWINCE) && !defined(CYGWIN32)
      if (n_root_sets < old_n_roots)
        GC_rebuild_root_index();
#   endif
}
#endif

STATIC void GC_remove_roots_inner(ptr_t b, ptr_t e);

GC_API void GC_CALL GC_remove_roots(void *b, void *e)
{
    DCL_LOCK_STATE;

    /* Quick check whether has nothing to do */
    if ((((word)b + (ALIGNMENT - 1)) & ~(word)(ALIGNMENT - 1)) >=
        ((word)e & ~(word)(ALIGNMENT - 1)))
      return;

    LOCK();
    GC_remove_roots_inner((ptr_t)b, (ptr_t)e);
    UNLOCK();
}

/* Should only be called when the lock is held */
STATIC void GC_remove_roots_inner(ptr_t b, ptr_t e)
{
    int i;
#   if !defined(MSWIN32) && !defined(MSWINCE) && !defined(CYGWIN32)
      int old_n_roots = n_root_sets;
#   endif

    for (i = 0; i < n_root_sets; ) {
        if ((word)GC_static_roots[i].r_start >= (word)b
            && (word)GC_static_roots[i].r_end <= (word)e) {
            GC_remove_root_at_pos(i);
        } else {
            i++;
        }
    }
#   if !defined(MSWIN32) && !defined(MSWINCE) && !defined(CYGWIN32)
      if (n_root_sets < old_n_roots)
        GC_rebuild_root_index();
#   endif
}

#ifdef USE_PROC_FOR_LIBRARIES
  /* Exchange the elements of the roots table.  Requires rebuild of     */
  /* the roots index table after the swap.                              */
  GC_INLINE void swap_static_roots(int i, int j)
  {
    ptr_t r_start = GC_static_roots[i].r_start;
    ptr_t r_end = GC_static_roots[i].r_end;
    GC_bool r_tmp = GC_static_roots[i].r_tmp;

    GC_static_roots[i].r_start = GC_static_roots[j].r_start;
    GC_static_roots[i].r_end = GC_static_roots[j].r_end;
    GC_static_roots[i].r_tmp = GC_static_roots[j].r_tmp;
    /* No need to swap r_next values.   */
    GC_static_roots[j].r_start = r_start;
    GC_static_roots[j].r_end = r_end;
    GC_static_roots[j].r_tmp = r_tmp;
  }

  /* Remove given range from every static root which intersects with    */
  /* the range.  It is assumed GC_remove_tmp_roots is called before     */
  /* this function is called repeatedly by GC_register_map_entries.     */
  GC_INNER void GC_remove_roots_subregion(ptr_t b, ptr_t e)
  {
    int i;
    GC_bool rebuild = FALSE;

    GC_ASSERT(I_HOLD_LOCK());
    GC_ASSERT((word)b % ALIGNMENT == 0 && (word)e % ALIGNMENT == 0);
    for (i = 0; i < n_root_sets; i++) {
      ptr_t r_start, r_end;

      if (GC_static_roots[i].r_tmp) {
        /* The remaining roots are skipped as they are all temporary. */
#       ifdef GC_ASSERTIONS
          int j;
          for (j = i + 1; j < n_root_sets; j++) {
            GC_ASSERT(GC_static_roots[j].r_tmp);
          }
#       endif
        break;
      }
      r_start = GC_static_roots[i].r_start;
      r_end = GC_static_roots[i].r_end;
      if (!EXPECT((word)e <= (word)r_start || (word)r_end <= (word)b, TRUE)) {
#       ifdef DEBUG_ADD_DEL_ROOTS
          GC_log_printf("Removing %p .. %p from root section %d (%p .. %p)\n",
                        (void *)b, (void *)e,
                        i, (void *)r_start, (void *)r_end);
#       endif
        if ((word)r_start < (word)b) {
          GC_root_size -= r_end - b;
          GC_static_roots[i].r_end = b;
          /* No need to rebuild as hash does not use r_end value. */
          if ((word)e < (word)r_end) {
            int j;

            if (rebuild) {
              GC_rebuild_root_index();
              rebuild = FALSE;
            }
            GC_add_roots_inner(e, r_end, FALSE); /* updates n_root_sets */
            for (j = i + 1; j < n_root_sets; j++)
              if (GC_static_roots[j].r_tmp)
                break;
            if (j < n_root_sets-1 && !GC_static_roots[n_root_sets-1].r_tmp) {
              /* Exchange the roots to have all temporary ones at the end. */
              swap_static_roots(j, n_root_sets - 1);
              rebuild = TRUE;
            }
          }
        } else {
          if ((word)e < (word)r_end) {
            GC_root_size -= e - r_start;
            GC_static_roots[i].r_start = e;
          } else {
            GC_remove_root_at_pos(i);
            if (i < n_root_sets - 1 && GC_static_roots[i].r_tmp
                && !GC_static_roots[i + 1].r_tmp) {
              int j;

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
    }
    if (rebuild)
      GC_rebuild_root_index();
  }
#endif /* USE_PROC_FOR_LIBRARIES */

#if !defined(NO_DEBUGGING)
  /* For the debugging purpose only.                                    */
  /* Workaround for the OS mapping and unmapping behind our back:       */
  /* Is the address p in one of the temporary static root sections?     */
  GC_API int GC_CALL GC_is_tmp_root(void *p)
  {
    static int last_root_set = MAX_ROOT_SETS;
    int i;

    if (last_root_set < n_root_sets
        && (word)p >= (word)GC_static_roots[last_root_set].r_start
        && (word)p < (word)GC_static_roots[last_root_set].r_end)
        return GC_static_roots[last_root_set].r_tmp;
    for (i = 0; i < n_root_sets; i++) {
        if ((word)p >= (word)GC_static_roots[i].r_start
            && (word)p < (word)GC_static_roots[i].r_end) {
            last_root_set = i;
            return GC_static_roots[i].r_tmp;
        }
    }
    return(FALSE);
  }
#endif /* !NO_DEBUGGING */

GC_INNER ptr_t GC_approx_sp(void)
{
    volatile word sp;
#   if ((defined(E2K) && defined(__clang__)) \
        || (defined(S390) && (__clang_major__ < 8))) && !defined(CPPCHECK)
        /* Workaround some bugs in clang:                                   */
        /* "undefined reference to llvm.frameaddress" error (clang-9/e2k);  */
        /* a crash in SystemZTargetLowering of libLLVM-3.8 (S390).          */
        sp = (word)&sp;
#   elif defined(CPPCHECK) || (__GNUC__ >= 4 /* GC_GNUC_PREREQ(4, 0) */ \
                               && !defined(STACK_NOT_SCANNED))
        /* TODO: Use GC_GNUC_PREREQ after fixing a bug in cppcheck. */
        sp = (word)__builtin_frame_address(0);
#   else
        sp = (word)&sp;
#   endif
                /* Also force stack to grow if necessary. Otherwise the */
                /* later accesses might cause the kernel to think we're */
                /* doing something wrong.                               */
    return((ptr_t)sp);
}

/*
 * Data structure for excluded static roots.
 * Real declaration is in gc_priv.h.

struct exclusion {
    ptr_t e_start;
    ptr_t e_end;
};

struct exclusion GC_excl_table[MAX_EXCLUSIONS];
                                        -- Array of exclusions, ascending
                                        -- address order.
*/

GC_API void GC_CALL GC_clear_exclusion_table(void)
{
    GC_excl_table_entries = 0;
}

/* Return the first exclusion range that includes an address >= start_addr */
/* Assumes the exclusion table contains at least one entry (namely the     */
/* GC data structures).                                                    */
STATIC struct exclusion * GC_next_exclusion(ptr_t start_addr)
{
    size_t low = 0;
    size_t high;

    GC_ASSERT(GC_excl_table_entries > 0);
    high = GC_excl_table_entries - 1;
    while (high > low) {
        size_t mid = (low + high) >> 1;

        /* low <= mid < high    */
        if ((word) GC_excl_table[mid].e_end <= (word) start_addr) {
            low = mid + 1;
        } else {
            high = mid;
        }
    }
    if ((word) GC_excl_table[low].e_end <= (word) start_addr) return 0;
    return GC_excl_table + low;
}

/* Should only be called when the lock is held.  The range boundaries   */
/* should be properly aligned and valid.                                */
GC_INNER void GC_exclude_static_roots_inner(void *start, void *finish)
{
    struct exclusion * next;
    size_t next_index;

    GC_ASSERT((word)start % ALIGNMENT == 0);
    GC_ASSERT((word)start < (word)finish);

    if (0 == GC_excl_table_entries) {
        next = 0;
    } else {
        next = GC_next_exclusion((ptr_t)start);
    }
    if (next != NULL) {
      if ((word)(next -> e_start) < (word) finish) {
        /* incomplete error check. */
        ABORT("Exclusion ranges overlap");
      }
      if ((word)(next -> e_start) == (word) finish) {
        /* extend old range backwards   */
          next -> e_start = (ptr_t)start;
          return;
      }
    }

    next_index = GC_excl_table_entries;
    if (next_index >= MAX_EXCLUSIONS) ABORT("Too many exclusions");
    if (next != NULL) {
      size_t i;

      next_index = (size_t)(next - GC_excl_table);
      for (i = GC_excl_table_entries; i > next_index; --i) {
        GC_excl_table[i] = GC_excl_table[i-1];
      }
    }
    GC_excl_table[next_index].e_start = (ptr_t)start;
    GC_excl_table[next_index].e_end = (ptr_t)finish;
    ++GC_excl_table_entries;
}

GC_API void GC_CALL GC_exclude_static_roots(void *b, void *e)
{
    DCL_LOCK_STATE;

    if (b == e) return;  /* nothing to exclude? */

    /* Round boundaries (in direction reverse to that of GC_add_roots). */
#   if ALIGNMENT > 1
      b = (void *)((word)b & ~(word)(ALIGNMENT - 1));
      e = (void *)(((word)e + (ALIGNMENT - 1)) & ~(word)(ALIGNMENT - 1));
      if (NULL == e)
        e = (void *)(~(word)(ALIGNMENT - 1)); /* handle overflow */
#   endif

    LOCK();
    GC_exclude_static_roots_inner(b, e);
    UNLOCK();
}

#if defined(WRAP_MARK_SOME) && defined(PARALLEL_MARK)
# define GC_PUSH_CONDITIONAL(b, t, all) \
                (GC_parallel \
                    ? GC_push_conditional_eager(b, t, all) \
                    : GC_push_conditional_static(b, t, all))
#else
# define GC_PUSH_CONDITIONAL(b, t, all) GC_push_conditional_static(b, t, all)
#endif

/* Invoke push_conditional on ranges that are not excluded. */
STATIC void GC_push_conditional_with_exclusions(ptr_t bottom, ptr_t top,
                                                GC_bool all)
{
    while ((word)bottom < (word)top) {
        struct exclusion *next = GC_next_exclusion(bottom);
        ptr_t excl_start;

        if (0 == next
            || (word)(excl_start = next -> e_start) >= (word)top) {
          GC_PUSH_CONDITIONAL(bottom, top, all);
          break;
        }
        if ((word)excl_start > (word)bottom)
          GC_PUSH_CONDITIONAL(bottom, excl_start, all);
        bottom = next -> e_end;
    }
}

#if defined(E2K) || defined(IA64)
  /* Similar to GC_push_all_stack_sections() but for IA-64 registers store. */
  GC_INNER void GC_push_all_register_sections(ptr_t bs_lo, ptr_t bs_hi,
                  int eager, struct GC_traced_stack_sect_s *traced_stack_sect)
  {
#   ifdef E2K
      (void)traced_stack_sect; /* TODO: Not implemented yet */
#   else
      while (traced_stack_sect != NULL) {
        ptr_t frame_bs_lo = traced_stack_sect -> backing_store_end;
        GC_ASSERT((word)frame_bs_lo <= (word)bs_hi);
        if (eager) {
            GC_push_all_eager(frame_bs_lo, bs_hi);
        } else {
            GC_push_all_stack(frame_bs_lo, bs_hi);
        }
        bs_hi = traced_stack_sect -> saved_backing_store_ptr;
        traced_stack_sect = traced_stack_sect -> prev;
      }
#   endif
    GC_ASSERT((word)bs_lo <= (word)bs_hi);
    if (eager) {
        GC_push_all_eager(bs_lo, bs_hi);
    } else {
        GC_push_all_stack(bs_lo, bs_hi);
    }
  }
#endif /* E2K || IA64 */

#ifdef THREADS

GC_INNER void GC_push_all_stack_sections(
                        ptr_t lo /* top */, ptr_t hi /* bottom */,
                        struct GC_traced_stack_sect_s *traced_stack_sect)
{
    while (traced_stack_sect != NULL) {
        GC_ASSERT((word)lo HOTTER_THAN (word)traced_stack_sect);
#       ifdef STACK_GROWS_UP
            GC_push_all_stack((ptr_t)traced_stack_sect, lo);
#       else /* STACK_GROWS_DOWN */
            GC_push_all_stack(lo, (ptr_t)traced_stack_sect);
#       endif
        lo = traced_stack_sect -> saved_stack_ptr;
        GC_ASSERT(lo != NULL);
        traced_stack_sect = traced_stack_sect -> prev;
    }
    GC_ASSERT(!((word)hi HOTTER_THAN (word)lo));
#   ifdef STACK_GROWS_UP
        /* We got them backwards! */
        GC_push_all_stack(hi, lo);
#   else /* STACK_GROWS_DOWN */
        GC_push_all_stack(lo, hi);
#   endif
}

#else /* !THREADS */

                        /* Similar to GC_push_all_eager, but only the   */
                        /* part hotter than cold_gc_frame is scanned    */
                        /* immediately.  Needed to ensure that callee-  */
                        /* save registers are not missed.               */
/*
 * A version of GC_push_all that treats all interior pointers as valid
 * and scans part of the area immediately, to make sure that saved
 * register values are not lost.
 * Cold_gc_frame delimits the stack section that must be scanned
 * eagerly.  A zero value indicates that no eager scanning is needed.
 * We don't need to worry about the manual VDB case here, since this
 * is only called in the single-threaded case.  We assume that we
 * cannot collect between an assignment and the corresponding
 * GC_dirty() call.
 */
STATIC void GC_push_all_stack_partially_eager(ptr_t bottom, ptr_t top,
                                              ptr_t cold_gc_frame)
{
#ifndef NEED_FIXUP_POINTER
  if (GC_all_interior_pointers) {
    /* Push the hot end of the stack eagerly, so that register values   */
    /* saved inside GC frames are marked before they disappear.         */
    /* The rest of the marking can be deferred until later.             */
    if (0 == cold_gc_frame) {
        GC_push_all_stack(bottom, top);
        return;
    }
    GC_ASSERT((word)bottom <= (word)cold_gc_frame
              && (word)cold_gc_frame <= (word)top);
#   ifdef STACK_GROWS_DOWN
        GC_push_all(cold_gc_frame - sizeof(ptr_t), top);
        GC_push_all_eager(bottom, cold_gc_frame);
#   else /* STACK_GROWS_UP */
        GC_push_all(bottom, cold_gc_frame + sizeof(ptr_t));
        GC_push_all_eager(cold_gc_frame, top);
#   endif /* STACK_GROWS_UP */
  } else
#endif
  /* else */ {
    GC_push_all_eager(bottom, top);
  }
# ifdef TRACE_BUF
    GC_add_trace_entry("GC_push_all_stack", (word)bottom, (word)top);
# endif
}

/* Similar to GC_push_all_stack_sections() but also uses cold_gc_frame. */
STATIC void GC_push_all_stack_part_eager_sections(
        ptr_t lo /* top */, ptr_t hi /* bottom */, ptr_t cold_gc_frame,
        struct GC_traced_stack_sect_s *traced_stack_sect)
{
    GC_ASSERT(traced_stack_sect == NULL || cold_gc_frame == NULL ||
              (word)cold_gc_frame HOTTER_THAN (word)traced_stack_sect);

    while (traced_stack_sect != NULL) {
        GC_ASSERT((word)lo HOTTER_THAN (word)traced_stack_sect);
#       ifdef STACK_GROWS_UP
            GC_push_all_stack_partially_eager((ptr_t)traced_stack_sect, lo,
                                              cold_gc_frame);
#       else /* STACK_GROWS_DOWN */
            GC_push_all_stack_partially_eager(lo, (ptr_t)traced_stack_sect,
                                              cold_gc_frame);
#       endif
        lo = traced_stack_sect -> saved_stack_ptr;
        GC_ASSERT(lo != NULL);
        traced_stack_sect = traced_stack_sect -> prev;
        cold_gc_frame = NULL; /* Use at most once.      */
    }

    GC_ASSERT(!((word)hi HOTTER_THAN (word)lo));
#   ifdef STACK_GROWS_UP
        /* We got them backwards! */
        GC_push_all_stack_partially_eager(hi, lo, cold_gc_frame);
#   else /* STACK_GROWS_DOWN */
        GC_push_all_stack_partially_eager(lo, hi, cold_gc_frame);
#   endif
}

#endif /* !THREADS */

/* Push enough of the current stack eagerly to ensure that callee-save  */
/* registers saved in GC frames are scanned.  In the non-threads case,  */
/* schedule entire stack for scanning.  The 2nd argument is a pointer   */
/* to the (possibly null) thread context, for (currently hypothetical)  */
/* more precise stack scanning.  In the presence of threads, push       */
/* enough of the current stack to ensure that callee-save registers     */
/* saved in collector frames have been seen.                            */
/* TODO: Merge it with per-thread stuff. */
STATIC void GC_push_current_stack(ptr_t cold_gc_frame,
                                  void * context GC_ATTR_UNUSED)
{
#   if defined(THREADS)
        /* cold_gc_frame is non-NULL.   */
#       ifdef STACK_GROWS_DOWN
          GC_push_all_eager(GC_approx_sp(), cold_gc_frame);
          /* For IA64, the register stack backing store is handled      */
          /* in the thread-specific code.                               */
#       else
          GC_push_all_eager(cold_gc_frame, GC_approx_sp());
#       endif
#   else
        GC_push_all_stack_part_eager_sections(GC_approx_sp(), GC_stackbottom,
                                        cold_gc_frame, GC_traced_stack_sect);
#       ifdef IA64
            /* We also need to push the register stack backing store.   */
            /* This should really be done in the same way as the        */
            /* regular stack.  For now we fudge it a bit.               */
            /* Note that the backing store grows up, so we can't use    */
            /* GC_push_all_stack_partially_eager.                       */
            {
                ptr_t bsp = GC_save_regs_ret_val;
                ptr_t cold_gc_bs_pointer = bsp - 2048;
                if (GC_all_interior_pointers
                    && (word)cold_gc_bs_pointer > (word)BACKING_STORE_BASE) {
                  /* Adjust cold_gc_bs_pointer if below our innermost   */
                  /* "traced stack section" in backing store.           */
                  if (GC_traced_stack_sect != NULL
                      && (word)cold_gc_bs_pointer
                          < (word)GC_traced_stack_sect->backing_store_end)
                    cold_gc_bs_pointer =
                                GC_traced_stack_sect->backing_store_end;
                  GC_push_all_register_sections(BACKING_STORE_BASE,
                        cold_gc_bs_pointer, FALSE, GC_traced_stack_sect);
                  GC_push_all_eager(cold_gc_bs_pointer, bsp);
                } else {
                  GC_push_all_register_sections(BACKING_STORE_BASE, bsp,
                                TRUE /* eager */, GC_traced_stack_sect);
                }
                /* All values should be sufficiently aligned that we    */
                /* don't have to worry about the boundary.              */
            }
#       elif defined(E2K)
          /* We also need to push procedure stack store.        */
          /* Procedure stack grows up.                          */
          {
            ptr_t bs_lo;
            size_t stack_size;

            GET_PROCEDURE_STACK_LOCAL(&bs_lo, &stack_size);
            GC_push_all_register_sections(bs_lo, bs_lo + stack_size,
                                          TRUE /* eager */,
                                          GC_traced_stack_sect);
            FREE_PROCEDURE_STACK_LOCAL(bs_lo, stack_size);
          }
#       endif
#   endif /* !THREADS */
}

GC_INNER void (*GC_push_typed_structures)(void) = 0;

GC_INNER void GC_cond_register_dynamic_libraries(void)
{
# if (defined(DYNAMIC_LOADING) && !defined(MSWIN_XBOX1)) \
     || defined(CYGWIN32) || defined(MSWIN32) || defined(MSWINCE) \
     || defined(PCR)
    GC_remove_tmp_roots();
    if (!GC_no_dls) GC_register_dynamic_libraries();
# else
    GC_no_dls = TRUE;
# endif
}

STATIC void GC_push_regs_and_stack(ptr_t cold_gc_frame)
{
#   ifdef THREADS
      if (NULL == cold_gc_frame)
        return; /* GC_push_all_stacks should push registers and stack */
#   endif
    GC_with_callee_saves_pushed(GC_push_current_stack, cold_gc_frame);
}

/* Call the mark routines (GC_push_one for a single pointer,            */
/* GC_push_conditional on groups of pointers) on every top level        */
/* accessible pointer.  If all is false, arrange to push only possibly  */
/* altered values.  Cold_gc_frame is an address inside a GC frame that  */
/* remains valid until all marking is complete; a NULL value indicates  */
/* that it is OK to miss some register values.  Called with the         */
/* allocation lock held.                                                */
GC_INNER void GC_push_roots(GC_bool all, ptr_t cold_gc_frame GC_ATTR_UNUSED)
{
    int i;
    unsigned kind;

    /* Next push static data.  This must happen early on, since it is   */
    /* not robust against mark stack overflow.                          */
    /* Re-register dynamic libraries, in case one got added.            */
    /* There is some argument for doing this as late as possible,       */
    /* especially on Win32, where it can change asynchronously.         */
    /* In those cases, we do it here.  But on other platforms, it's     */
    /* not safe with the world stopped, so we do it earlier.            */
#   if !defined(REGISTER_LIBRARIES_EARLY)
        GC_cond_register_dynamic_libraries();
#   endif

    /* Mark everything in static data areas.                            */
    for (i = 0; i < n_root_sets; i++) {
        GC_push_conditional_with_exclusions(
                             GC_static_roots[i].r_start,
                             GC_static_roots[i].r_end, all);
    }

    /* Mark all free list header blocks, if those were allocated from   */
    /* the garbage collected heap.  This makes sure they don't          */
    /* disappear if we are not marking from static data.  It also       */
    /* saves us the trouble of scanning them, and possibly that of      */
    /* marking the freelists.                                           */
    for (kind = 0; kind < GC_n_kinds; kind++) {
        void *base = GC_base(GC_obj_kinds[kind].ok_freelist);
        if (base != NULL) {
            GC_set_mark_bit(base);
        }
    }

    /* Mark from GC internal roots if those might otherwise have        */
    /* been excluded.                                                   */
#   ifndef GC_NO_FINALIZATION
        GC_push_finalizer_structures();
#   endif
#   ifdef THREADS
        if (GC_no_dls || GC_roots_were_cleared)
            GC_push_thread_structures();
#   endif
    if (GC_push_typed_structures)
        GC_push_typed_structures();

    /* Mark thread local free lists, even if their mark        */
    /* descriptor excludes the link field.                     */
    /* If the world is not stopped, this is unsafe.  It is     */
    /* also unnecessary, since we will do this again with the  */
    /* world stopped.                                          */
#   if defined(THREAD_LOCAL_ALLOC)
        if (GC_world_stopped)
            GC_mark_thread_local_free_lists();
#   endif

    /* Now traverse stacks, and mark from register contents.    */
    /* These must be done last, since they can legitimately     */
    /* overflow the mark stack.  This is usually done by saving */
    /* the current context on the stack, and then just tracing  */
    /* from the stack.                                          */
#   ifndef STACK_NOT_SCANNED
        GC_push_regs_and_stack(cold_gc_frame);
#   endif

    if (GC_push_other_roots != 0) {
        /* In the threads case, this also pushes thread stacks. */
        /* Note that without interior pointer recognition lots  */
        /* of stuff may have been pushed already, and this      */
        /* should be careful about mark stack overflows.        */
        (*GC_push_other_roots)();
    }
}
