/*
 * Copyright (c) 2001 by Hewlett-Packard Company. All rights reserved.
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

#include "private/dbg_mlc.h"

/*
 * This implements a full, though not well-tuned, representation of the
 * backwards points-to graph.  This is used to test for non-GC-robust
 * data structures; the code is not used during normal garbage collection.
 *
 * One restriction is that we drop all back-edges from nodes with very
 * high in-degree, and simply add them to a list of such nodes.  They are
 * then treated as permanent roots.  If this by itself does not introduce
 * a space leak, then such nodes cannot contribute to a growing space leak.
 */

#ifdef MAKE_BACK_GRAPH

#  if (!defined(DBG_HDRS_ALL)                                          \
       || (ALIGNMENT != CPP_PTRSZ / 8) /* `|| !defined(UNIX_LIKE)` */) \
      && !defined(CPPCHECK)
#    error The configuration does not support MAKE_BACK_GRAPH
#  endif

/*
 * We store single back pointers directly in the object's `oh_bg_ptr` field.
 * If there is more than one pointer to an object, we store `q` or'ed with
 * `FLAG_MANY`, where `q` is a pointer to a `back_edges` object.
 * Every once in a while we use a `back_edges` object even for a single
 * pointer, since we need the other fields in the `back_edges` structure to
 * be present in some fraction of the objects.  Otherwise we get serious
 * performance issues.
 */
#  define FLAG_MANY 2

/* Directly points to a reachable object; retain for the next collection. */
#  define RETAIN 1

/* Special values of `height` field of `back_edges`. */
#  define HEIGHT_UNKNOWN (-2)
#  define HEIGHT_IN_PROGRESS (-1)

typedef struct back_edges_s back_edges;

#  define MAX_BACK_EDGE_STRUCTS 100000

/*
 * Allocate a new back edge structure.  Should be more sophisticated
 * if this were production code.
 */
static back_edges *
new_back_edges(void)
{
  GC_ASSERT(I_HOLD_LOCK());
  if (NULL == GC_back_edge_space) {
    size_t bytes_to_get;

    GC_ASSERT(0 == GC_n_back_edge_structs);
    GC_ASSERT(NULL == GC_avail_back_edges);
    GC_ASSERT(GC_page_size != 0);
    bytes_to_get
        = ROUNDUP_PAGESIZE_IF_MMAP(MAX_BACK_EDGE_STRUCTS * sizeof(back_edges));

    GC_back_edge_space = (back_edges *)GC_os_get_mem(bytes_to_get);
    if (NULL == GC_back_edge_space)
      ABORT("Insufficient memory for back edges");
  }
  if (GC_avail_back_edges != NULL) {
    back_edges *result = GC_avail_back_edges;
    GC_avail_back_edges = result->cont;
    result->cont = NULL;
    return result;
  }
  if (GC_n_back_edge_structs >= MAX_BACK_EDGE_STRUCTS - 1) {
    ABORT("Needed too much space for back edges:"
          " adjust MAX_BACK_EDGE_STRUCTS");
  }
  return &GC_back_edge_space[GC_n_back_edge_structs++];
}

/* Deallocate `p` and its associated continuation structures. */
static void
deallocate_back_edges(back_edges *p)
{
  back_edges *last;

  for (last = p; last->cont != NULL;)
    last = last->cont;

  last->cont = GC_avail_back_edges;
  GC_avail_back_edges = p;
}

#  define INITIAL_IN_PROGRESS 10000

static void
push_in_progress(ptr_t p)
{
  GC_ASSERT(I_HOLD_LOCK());
  if (GC_backgraph_n_in_progress >= GC_backgraph_in_progress_size) {
    ptr_t *new_in_progress_space;

    GC_ASSERT(GC_page_size != 0);
    if (NULL == GC_backgraph_in_progress_space) {
      GC_ASSERT(0 == GC_backgraph_n_in_progress);
      GC_ASSERT(0 == GC_backgraph_in_progress_size);
      GC_backgraph_in_progress_size
          = ROUNDUP_PAGESIZE_IF_MMAP(INITIAL_IN_PROGRESS * sizeof(ptr_t))
            / sizeof(ptr_t);
      new_in_progress_space = (ptr_t *)GC_os_get_mem(
          GC_backgraph_in_progress_size * sizeof(ptr_t));
    } else {
      GC_backgraph_in_progress_size *= 2;
      new_in_progress_space = (ptr_t *)GC_os_get_mem(
          GC_backgraph_in_progress_size * sizeof(ptr_t));
      if (new_in_progress_space != NULL)
        BCOPY(GC_backgraph_in_progress_space, new_in_progress_space,
              GC_backgraph_n_in_progress * sizeof(ptr_t));
    }
#  ifndef GWW_VDB
    GC_scratch_recycle_no_gww(GC_backgraph_in_progress_space,
                              GC_backgraph_n_in_progress * sizeof(ptr_t));
#  elif defined(LINT2)
    /* TODO: Implement GWW-aware recycling as in `alloc_mark_stack`. */
    GC_noop1_ptr(GC_backgraph_in_progress_space);
#  endif
    GC_backgraph_in_progress_space = new_in_progress_space;
  }
  if (NULL == GC_backgraph_in_progress_space)
    ABORT("MAKE_BACK_GRAPH: Out of in-progress space: "
          "Huge linear data structure?");
  GC_backgraph_in_progress_space[GC_backgraph_n_in_progress++] = p;
}

static GC_bool
is_in_progress(const char *p)
{
  size_t i;
  for (i = 0; i < GC_backgraph_n_in_progress; ++i) {
    if (GC_backgraph_in_progress_space[i] == p)
      return TRUE;
  }
  return FALSE;
}

GC_INLINE void
pop_in_progress(ptr_t p)
{
#  ifndef GC_ASSERTIONS
  UNUSED_ARG(p);
#  endif
  --GC_backgraph_n_in_progress;
  GC_ASSERT(GC_backgraph_in_progress_space[GC_backgraph_n_in_progress] == p);
}

#  define GET_OH_BG_PTR(p) (ptr_t) GC_REVEAL_POINTER(((oh *)(p))->oh_bg_ptr)
#  define SET_OH_BG_PTR(p, q) (((oh *)(p))->oh_bg_ptr = GC_HIDE_POINTER(q))

/* Ensure that `p` has a `back_edges` structure associated with it. */
static void
ensure_struct(ptr_t p)
{
  ptr_t old_back_ptr = GET_OH_BG_PTR(p);

  GC_ASSERT(I_HOLD_LOCK());
  if ((ADDR(old_back_ptr) & FLAG_MANY) == 0) {
    back_edges *be = new_back_edges();

    be->flags = 0;
#  if defined(CPPCHECK)
    GC_noop1_ptr(&old_back_ptr);
    /* Workaround a false positive that `old_back_ptr` cannot be `NULL`. */
#  endif
    if (NULL == old_back_ptr) {
      be->n_edges = 0;
    } else {
      be->n_edges = 1;
      be->edges[0] = old_back_ptr;
    }
    be->height = HEIGHT_UNKNOWN;
    be->height_gc_no = (unsigned short)(GC_gc_no - 1);
    GC_ASSERT(ADDR_GE((ptr_t)be, (ptr_t)GC_back_edge_space));
    SET_OH_BG_PTR(p, CPTR_SET_FLAGS(be, FLAG_MANY));
  }
}

/*
 * Add the (forward) edge from `p` to `q` to the backward graph.  Both `p`
 * and `q` are pointers to the object base, i.e. pointers to an `oh`.
 */
static void
add_edge(ptr_t p, ptr_t q)
{
  ptr_t pred = GET_OH_BG_PTR(q);
  back_edges *be, *be_cont;
  word i;

  GC_ASSERT(p == GC_base(p) && q == GC_base(q));
  GC_ASSERT(I_HOLD_LOCK());
  if (!GC_HAS_DEBUG_INFO(q) || !GC_HAS_DEBUG_INFO(p)) {
    /*
     * This is really a misinterpreted free-list link, since we saw
     * a pointer to a free list.  Do not overwrite it!
     */
    return;
  }
#  if defined(CPPCHECK)
  GC_noop1_ptr(&pred);
#  endif
  if (NULL == pred) {
    /*
     * A not very random number we use to occasionally allocate
     * a `back_edges` structure even for a single backward edge.
     * This prevents us from repeatedly tracing back through very long
     * chains, since we will have some place to store `height` and
     * `HEIGHT_IN_PROGRESS` flag along the way.
     */
#  define GOT_LUCKY_NUMBER (((++random_number) & 0x7f) == 0)
    static unsigned random_number = 13;

    SET_OH_BG_PTR(q, p);
    if (GOT_LUCKY_NUMBER)
      ensure_struct(q);
    return;
  }

  /* Check whether it was already in the list of predecessors. */
  {
    back_edges *e = (back_edges *)CPTR_CLEAR_FLAGS(pred, FLAG_MANY);
    word n_edges;
    word total;
    int local = 0;

    if ((ADDR(pred) & FLAG_MANY) != 0) {
      n_edges = e->n_edges;
    } else if ((COVERT_DATAFLOW(ADDR(pred)) & 1) == 0) {
      /* A misinterpreted free-list link. */
      n_edges = 1;
      local = -1;
    } else {
      n_edges = 0;
    }
    for (total = 0; total < n_edges; ++total) {
      if (local == BACKGRAPH_MAX_IN) {
        e = e->cont;
        local = 0;
      }
      if (local >= 0)
        pred = e->edges[local++];
      if (pred == p)
        return;
    }
  }

  ensure_struct(q);
  be = (back_edges *)CPTR_CLEAR_FLAGS(GET_OH_BG_PTR(q), FLAG_MANY);
  for (i = be->n_edges, be_cont = be; i > BACKGRAPH_MAX_IN;
       i -= BACKGRAPH_MAX_IN)
    be_cont = be_cont->cont;
  if (i == BACKGRAPH_MAX_IN) {
    be_cont->cont = new_back_edges();
    be_cont = be_cont->cont;
    i = 0;
  }
  be_cont->edges[i] = p;
  be->n_edges++;
#  ifdef DEBUG_PRINT_BIG_N_EDGES
  if (GC_print_stats == VERBOSE && be->n_edges == 100) {
    GC_err_printf("The following object has big in-degree:\n");
#    ifdef THREADS
    /*
     * Note: we cannot call the debug variant of `GC_print_heap_obj` here
     * because the allocator lock is held.
     */
    GC_default_print_heap_obj_proc(q);
#    else
    GC_print_heap_obj(q);
#    endif
  }
#  endif
}

typedef void (*per_object_func)(ptr_t p, size_t sz, word descr);

static GC_CALLBACK void
per_object_helper(struct hblk *h, void *fn_ptr)
{
  const hdr *hhdr = HDR(h);
  word descr = hhdr->hb_descr;
  per_object_func fn = *(per_object_func *)fn_ptr;
  size_t sz = hhdr->hb_sz;
  size_t i = 0;

  do {
    fn((ptr_t)(h->hb_body + i), sz, descr);
    i += sz;
  } while (i + sz <= HBLKSIZE);
}

GC_INLINE void
GC_apply_to_each_object(per_object_func fn)
{
  GC_apply_to_all_blocks(per_object_helper, &fn);
}

static void
reset_back_edge(ptr_t p, size_t sz, word descr)
{
  UNUSED_ARG(sz);
  UNUSED_ARG(descr);
  GC_ASSERT(I_HOLD_LOCK());
  /* Skip any free-list links, or dropped blocks. */
  if (GC_HAS_DEBUG_INFO(p)) {
    ptr_t old_back_ptr = GET_OH_BG_PTR(p);

    if ((ADDR(old_back_ptr) & FLAG_MANY) != 0) {
      back_edges *be = (back_edges *)CPTR_CLEAR_FLAGS(old_back_ptr, FLAG_MANY);

      if (!(be->flags & RETAIN)) {
        deallocate_back_edges(be);
        SET_OH_BG_PTR(p, NULL);
      } else {
        GC_ASSERT(GC_is_marked(p));

        /*
         * Back edges may point to objects that will not be retained.
         * Delete them for now, but remember the height.  Some will be
         * added back at next collection.
         */
        be->n_edges = 0;
        if (be->cont != NULL) {
          deallocate_back_edges(be->cont);
          be->cont = NULL;
        }

        GC_ASSERT(GC_is_marked(p));
        /* We only retain things for one collection cycle at a time. */
        be->flags &= (unsigned short)~RETAIN;
      }
    } else /* simple back pointer */ {
      /* Clear to avoid dangling pointer. */
      SET_OH_BG_PTR(p, NULL);
    }
  }
}

static void
add_back_edges(ptr_t p, size_t sz, word descr)
{
  ptr_t current_p = p + sizeof(oh);

  /* For now, fix up non-length descriptors conservatively. */
  if ((descr & GC_DS_TAGS) != GC_DS_LENGTH) {
    descr = sz;
  }

  for (; ADDR_LT(current_p, p + descr); current_p += sizeof(ptr_t)) {
    ptr_t q;

    LOAD_PTR_OR_CONTINUE(q, current_p);
    FIXUP_POINTER(q);
    if (GC_least_real_heap_addr < ADDR(q)
        && ADDR(q) < GC_greatest_real_heap_addr) {
      ptr_t target = (ptr_t)GC_base(q);

      if (target != NULL)
        add_edge(p, target);
    }
  }
}

GC_INNER void
GC_build_back_graph(void)
{
  GC_ASSERT(I_HOLD_LOCK());
  GC_apply_to_each_object(add_back_edges);
}

/*
 * Return an approximation to the length of the longest simple path through
 * unreachable objects to `p`.  We refer to this as the height of `p`.
 */
static word
backwards_height(ptr_t p)
{
  word result;
  ptr_t pred = GET_OH_BG_PTR(p);
  back_edges *be;

  GC_ASSERT(I_HOLD_LOCK());
#  if defined(CPPCHECK)
  GC_noop1_ptr(&pred);
#  endif
  if (NULL == pred)
    return 1;
  if ((ADDR(pred) & FLAG_MANY) == 0) {
    if (is_in_progress(p)) {
      /*
       * DFS (depth-first search) back edge, i.e. we followed an edge to
       * an object already on our stack.  Ignore.
       */
      return 0;
    }
    push_in_progress(p);
    result = backwards_height(pred) + 1;
    pop_in_progress(p);
    return result;
  }
  be = (back_edges *)CPTR_CLEAR_FLAGS(pred, FLAG_MANY);
  if (be->height >= 0 && be->height_gc_no == (unsigned short)GC_gc_no)
    return (word)be->height;
  /* Ignore back edges in DFS. */
  if (be->height == HEIGHT_IN_PROGRESS)
    return 0;

  result = be->height > 0 ? (word)be->height : 1U;
  be->height = HEIGHT_IN_PROGRESS;

  {
    back_edges *e = be;
    word n_edges;
    word total;
    int local = 0;

    if ((ADDR(pred) & FLAG_MANY) != 0) {
      n_edges = e->n_edges;
    } else if ((ADDR(pred) & 1) == 0) {
      /* A misinterpreted free-list link. */
      n_edges = 1;
      local = -1;
    } else {
      n_edges = 0;
    }
    for (total = 0; total < n_edges; ++total) {
      word this_height;
      if (local == BACKGRAPH_MAX_IN) {
        e = e->cont;
        local = 0;
      }
      if (local >= 0)
        pred = e->edges[local++];

      /*
       * Execute the following once for each predecessor `pred` of `p`
       * in the points-to graph.
       */
      if (GC_is_marked(pred) && (ADDR(GET_OH_BG_PTR(p)) & FLAG_MANY) == 0) {
        GC_COND_LOG_PRINTF("Found bogus pointer from %p to %p\n", (void *)pred,
                           (void *)p);
        /*
         * Reachable object "points to" unreachable one.  Could be caused
         * by our lax treatment of the collector descriptors.
         */
        this_height = 1;
      } else {
        this_height = backwards_height(pred);
      }
      if (this_height >= result)
        result = this_height + 1;
    }
  }

  be->height = (GC_signed_word)result;
  be->height_gc_no = (unsigned short)GC_gc_no;
  return result;
}

/*
 * Compute the maximum height of every unreachable predecessor `p` of
 * a reachable object.  Arrange to save the heights of all such objects `p`
 * so that they can be used in calculating the height of objects in the next
 * collection.  Set `GC_backgraph_deepest_height` to be the maximum height we
 * encounter, and `GC_backgraph_deepest_obj` to be the corresponding object.
 */
static void
update_deepest_height(ptr_t p, size_t sz, word descr)
{
  UNUSED_ARG(sz);
  UNUSED_ARG(descr);
  GC_ASSERT(I_HOLD_LOCK());
  if (GC_is_marked(p) && GC_HAS_DEBUG_INFO(p)) {
    word p_height = 0;
    ptr_t p_deepest_obj = NULL;
    ptr_t back_ptr;
    back_edges *be = NULL;

    /*
     * If we remembered a height last time, use it as a minimum.
     * It may have increased due to newly unreachable chains pointing
     * to `p`, but it cannot have decreased.
     */
    back_ptr = GET_OH_BG_PTR(p);
#  if defined(CPPCHECK)
    GC_noop1_ptr(&back_ptr);
#  endif
    if (back_ptr != NULL && (ADDR(back_ptr) & FLAG_MANY) != 0) {
      be = (back_edges *)CPTR_CLEAR_FLAGS(back_ptr, FLAG_MANY);
      if (be->height != HEIGHT_UNKNOWN)
        p_height = (word)be->height;
    }

    {
      ptr_t pred = back_ptr;
      back_edges *e = (back_edges *)CPTR_CLEAR_FLAGS(pred, FLAG_MANY);
      word n_edges;
      word total;
      int local = 0;

      if ((ADDR(pred) & FLAG_MANY) != 0) {
        n_edges = e->n_edges;
      } else if (pred != NULL && (ADDR(pred) & 1) == 0) {
        /* A misinterpreted free-list link. */
        n_edges = 1;
        local = -1;
      } else {
        n_edges = 0;
      }
      for (total = 0; total < n_edges; ++total) {
        if (local == BACKGRAPH_MAX_IN) {
          e = e->cont;
          local = 0;
        }
        if (local >= 0)
          pred = e->edges[local++];

        /*
         * Execute the following once for each predecessor `pred` of `p`
         * in the points-to graph.
         */
        if (!GC_is_marked(pred) && GC_HAS_DEBUG_INFO(pred)) {
          word this_height = backwards_height(pred);

          if (this_height > p_height) {
            p_height = this_height;
            p_deepest_obj = pred;
          }
        }
      }
    }

    if (p_height > 0) {
      /* Remember the height for next time. */
      if (NULL == be) {
        ensure_struct(p);
        back_ptr = GET_OH_BG_PTR(p);
        be = (back_edges *)CPTR_CLEAR_FLAGS(back_ptr, FLAG_MANY);
      }
      be->flags |= RETAIN;
      be->height = (GC_signed_word)p_height;
      be->height_gc_no = (unsigned short)GC_gc_no;
    }
    if (p_height > GC_backgraph_deepest_height) {
      GC_backgraph_deepest_height = p_height;
      GC_backgraph_deepest_obj = p_deepest_obj;
    }
  }
}

GC_INNER void
GC_traverse_back_graph(void)
{
  GC_ASSERT(I_HOLD_LOCK());
  GC_backgraph_deepest_height = 0;
  GC_apply_to_each_object(update_deepest_height);
  if (GC_backgraph_deepest_obj != NULL) {
    /* Keep the pointer until we can print it. */
    GC_set_mark_bit(GC_backgraph_deepest_obj);
  }
}

void
GC_print_back_graph_stats(void)
{
  GC_ASSERT(I_HOLD_LOCK());
  GC_printf(
      "Maximum backwards height of reachable objects at GC #%lu is %lu\n",
      (unsigned long)GC_gc_no, (unsigned long)GC_backgraph_deepest_height);
  if (GC_backgraph_deepest_height > GC_backgraph_max_deepest_h) {
    ptr_t obj = GC_backgraph_deepest_obj;

    GC_backgraph_max_deepest_h = GC_backgraph_deepest_height;
    UNLOCK();
    GC_err_printf("The following unreachable object is last"
                  " in a longest chain of unreachable objects:\n");
    GC_print_heap_obj(obj);
    LOCK();
  }
  GC_COND_LOG_PRINTF("Needed max total of %d back-edge structs\n",
                     GC_n_back_edge_structs);
  GC_apply_to_each_object(reset_back_edge);
  GC_backgraph_deepest_obj = NULL;
}

#endif /* MAKE_BACK_GRAPH */
