/*
 * Copyright 1988, 1989 Hans-J. Boehm, Alan J. Demers
 * Copyright (c) 1991-1996 by Xerox Corporation.  All rights reserved.
 * Copyright (c) 1996-1999 by Silicon Graphics.  All rights reserved.
 * Copyright (c) 2007 Free Software Foundation, Inc.
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

#ifndef GC_NO_FINALIZATION
#  include "gc/javaxfc.h" /*< to get `GC_finalize_all()` as `extern "C"` */

/*
 * Type of mark procedure used for marking from finalizable object.
 * This procedure normally does not mark the object, only its descendants.
 */
typedef void (*finalization_mark_proc)(ptr_t /* `finalizable_obj_ptr` */);

#  define HASH3(addr, size, log_size)                               \
    ((size_t)((ADDR(addr) >> 3) ^ (ADDR(addr) >> (3 + (log_size)))) \
     & ((size) - (size_t)1))
#  define HASH2(addr, log_size) HASH3(addr, (size_t)1 << (log_size), log_size)

struct hash_chain_entry {
  GC_hidden_pointer hidden_key;
  struct hash_chain_entry *next;
};

struct disappearing_link {
  struct hash_chain_entry prolog;
#  define dl_hidden_link prolog.hidden_key /*< field to be cleared */
#  define dl_next(x) (struct disappearing_link *)((x)->prolog.next)
#  define dl_set_next(x, y) \
    (void)((x)->prolog.next = (struct hash_chain_entry *)(y))
  GC_hidden_pointer dl_hidden_obj; /*< pointer to object base */
};

struct finalizable_object {
  struct hash_chain_entry prolog;
  /*
   * Pointer to object base.  No longer hidden once object is on
   * `finalize_now` queue.
   */
#  define fo_hidden_base prolog.hidden_key
#  define fo_next(x) (struct finalizable_object *)((x)->prolog.next)
#  define fo_set_next(x, y) ((x)->prolog.next = (struct hash_chain_entry *)(y))
  GC_finalization_proc fo_fn;          /*< the finalizer */
  finalization_mark_proc fo_mark_proc; /*< mark-through procedure */
  ptr_t fo_client_data;
  size_t fo_object_sz; /*< in bytes */
};

#  ifdef AO_HAVE_store
/*
 * Update `finalize_now` atomically as `GC_should_invoke_finalizers`
 * does not acquire the allocator lock.
 */
#    define SET_FINALIZE_NOW(fo) \
      GC_cptr_store((volatile ptr_t *)&GC_fnlz_roots.finalize_now, (ptr_t)(fo))
#  else
#    define SET_FINALIZE_NOW(fo) (void)(GC_fnlz_roots.finalize_now = (fo))
#  endif /* !THREADS */

GC_API void GC_CALL
GC_push_finalizer_structures(void)
{
  GC_ASSERT(ADDR(&GC_dl_hashtbl.head) % ALIGNMENT == 0);
  GC_ASSERT(ADDR(&GC_fnlz_roots) % ALIGNMENT == 0);
#  ifndef GC_LONG_REFS_NOT_NEEDED
  GC_ASSERT(ADDR(&GC_ll_hashtbl.head) % ALIGNMENT == 0);
  GC_PUSH_ALL_SYM(GC_ll_hashtbl.head);
#  endif
  GC_PUSH_ALL_SYM(GC_dl_hashtbl.head);
  GC_PUSH_ALL_SYM(GC_fnlz_roots);
  /* `GC_toggleref_arr` is pushed specially by `GC_mark_togglerefs`. */
}

/*
 * Threshold of `log_size` to initiate full collection before growing
 * a hash table.
 */
#  ifndef GC_ON_GROW_LOG_SIZE_MIN
#    define GC_ON_GROW_LOG_SIZE_MIN LOG_HBLKSIZE
#  endif

/*
 * Ensure the hash table has enough capacity.  `*table_ptr` is a pointer
 * to an array of hash headers.  `*log_size_ptr` is the log of its current
 * size.  We update both `*table_ptr` and `*log_size_ptr` on success.
 */
STATIC void
GC_grow_table(struct hash_chain_entry ***table_ptr, unsigned *log_size_ptr,
              const size_t *entries_ptr)
{
  size_t i;
  struct hash_chain_entry *p;
  unsigned log_old_size = *log_size_ptr;
  unsigned log_new_size = log_old_size + 1;
  size_t old_size = NULL == *table_ptr ? 0 : (size_t)1 << log_old_size;
  size_t new_size = (size_t)1 << log_new_size;
  /* FIXME: Power-of-two size often gets rounded up to one more page. */
  struct hash_chain_entry **new_table;

  GC_ASSERT(I_HOLD_LOCK());
  /*
   * Avoid growing the table in case of at least 25% of entries can
   * be deleted by enforcing a collection.  Ignored for small tables.
   * In the incremental mode we skip this optimization, as we want to
   * avoid triggering a full collection whenever possible.
   */
  if (log_old_size >= (unsigned)GC_ON_GROW_LOG_SIZE_MIN && !GC_incremental) {
    IF_CANCEL(int cancel_state;)

    DISABLE_CANCEL(cancel_state);
    GC_gcollect_inner();
    RESTORE_CANCEL(cancel_state);
    /* `GC_finalize` might decrease entries value. */
    if (*entries_ptr < ((size_t)1 << log_old_size) - (*entries_ptr >> 2))
      return;
  }

  new_table = (struct hash_chain_entry **)GC_INTERNAL_MALLOC_IGNORE_OFF_PAGE(
      new_size * sizeof(struct hash_chain_entry *), NORMAL);
  if (NULL == new_table) {
    if (NULL == *table_ptr) {
      ABORT("Insufficient space for initial table allocation");
    } else {
      return;
    }
  }
  for (i = 0; i < old_size; i++) {
    for (p = (*table_ptr)[i]; p != NULL;) {
      ptr_t real_key = (ptr_t)GC_REVEAL_POINTER(p->hidden_key);
      struct hash_chain_entry *next = p->next;
      size_t new_hash = HASH3(real_key, new_size, log_new_size);

      p->next = new_table[new_hash];
      GC_dirty(p);
      new_table[new_hash] = p;
      p = next;
    }
  }
  *log_size_ptr = log_new_size;
  *table_ptr = new_table;
  GC_dirty(new_table); /*< entire object */
}

GC_API int GC_CALL
GC_register_disappearing_link(void **link)
{
  ptr_t base;

  base = (ptr_t)GC_base(link);
  if (base == 0)
    ABORT("Bad arg to GC_register_disappearing_link");
  return GC_general_register_disappearing_link(link, base);
}

STATIC int
GC_register_disappearing_link_inner(struct dl_hashtbl_s *dl_hashtbl,
                                    void **link, const void *obj,
                                    const char *tbl_log_name)
{
  struct disappearing_link *curr_dl;
  size_t index;
  struct disappearing_link *new_dl;

  GC_ASSERT(GC_is_initialized);
  if (UNLIKELY(GC_find_leak_inner))
    return GC_UNIMPLEMENTED;
#  ifdef GC_ASSERTIONS
  /* Just check accessibility. */
  GC_noop1_ptr(*link);
#  endif
  LOCK();
  GC_ASSERT(obj != NULL && GC_base_C(obj) == obj);
  if (UNLIKELY(NULL == dl_hashtbl->head)
      || UNLIKELY(dl_hashtbl->entries > ((size_t)1 << dl_hashtbl->log_size))) {
    GC_grow_table((struct hash_chain_entry ***)&dl_hashtbl->head,
                  &dl_hashtbl->log_size, &dl_hashtbl->entries);
    GC_COND_LOG_PRINTF("Grew %s table to %u entries\n", tbl_log_name,
                       1U << dl_hashtbl->log_size);
  }
  index = HASH2(link, dl_hashtbl->log_size);
  for (curr_dl = dl_hashtbl->head[index]; curr_dl != 0;
       curr_dl = dl_next(curr_dl)) {
    if (curr_dl->dl_hidden_link == GC_HIDE_POINTER(link)) {
      /* Alternatively, `GC_HIDE_NZ_POINTER()` could be used instead. */
      curr_dl->dl_hidden_obj = GC_HIDE_POINTER(obj);
      UNLOCK();
      return GC_DUPLICATE;
    }
  }
  new_dl = (struct disappearing_link *)GC_INTERNAL_MALLOC(
      sizeof(struct disappearing_link), NORMAL);
  if (UNLIKELY(NULL == new_dl)) {
    GC_oom_func oom_fn = GC_oom_fn;
    UNLOCK();
    new_dl = (struct disappearing_link *)(*oom_fn)(
        sizeof(struct disappearing_link));
    if (0 == new_dl) {
      return GC_NO_MEMORY;
    }
    /* It is not likely we will make it here, but... */
    LOCK();
    /* Recalculate `index` since the table may grow. */
    index = HASH2(link, dl_hashtbl->log_size);
    /* Check again that our disappearing link not in the table. */
    for (curr_dl = dl_hashtbl->head[index]; curr_dl != 0;
         curr_dl = dl_next(curr_dl)) {
      if (curr_dl->dl_hidden_link == GC_HIDE_POINTER(link)) {
        curr_dl->dl_hidden_obj = GC_HIDE_POINTER(obj);
        UNLOCK();
#  ifndef DBG_HDRS_ALL
        /* Free unused `new_dl` returned by `GC_oom_fn()`. */
        GC_free(new_dl);
#  endif
        return GC_DUPLICATE;
      }
    }
  }
  new_dl->dl_hidden_obj = GC_HIDE_POINTER(obj);
  new_dl->dl_hidden_link = GC_HIDE_POINTER(link);
  dl_set_next(new_dl, dl_hashtbl->head[index]);
  GC_dirty(new_dl);
  dl_hashtbl->head[index] = new_dl;
  dl_hashtbl->entries++;
  GC_dirty(dl_hashtbl->head + index);
  UNLOCK();
  return GC_SUCCESS;
}

GC_API int GC_CALL
GC_general_register_disappearing_link(void **link, const void *obj)
{
  if ((ADDR(link) & (ALIGNMENT - 1)) != 0 || !NONNULL_ARG_NOT_NULL(link))
    ABORT("Bad arg to GC_general_register_disappearing_link");
  return GC_register_disappearing_link_inner(&GC_dl_hashtbl, link, obj, "dl");
}

#  ifdef DBG_HDRS_ALL
#    define FREE_DL_ENTRY(curr_dl) dl_set_next(curr_dl, NULL)
#  else
#    define FREE_DL_ENTRY(curr_dl) GC_free(curr_dl)
#  endif

/* Unregisters given `link` and returns the link entry to free. */
GC_INLINE struct disappearing_link *
GC_unregister_disappearing_link_inner(struct dl_hashtbl_s *dl_hashtbl,
                                      void **link)
{
  struct disappearing_link *curr_dl;
  struct disappearing_link *prev_dl = NULL;
  size_t index;

  GC_ASSERT(I_HOLD_LOCK());
  if (UNLIKELY(NULL == dl_hashtbl->head))
    return NULL;

  index = HASH2(link, dl_hashtbl->log_size);
  for (curr_dl = dl_hashtbl->head[index]; curr_dl;
       curr_dl = dl_next(curr_dl)) {
    if (curr_dl->dl_hidden_link == GC_HIDE_POINTER(link)) {
      /* Remove found entry from the table. */
      if (NULL == prev_dl) {
        dl_hashtbl->head[index] = dl_next(curr_dl);
        GC_dirty(dl_hashtbl->head + index);
      } else {
        dl_set_next(prev_dl, dl_next(curr_dl));
        GC_dirty(prev_dl);
      }
      dl_hashtbl->entries--;
      break;
    }
    prev_dl = curr_dl;
  }
  return curr_dl;
}

GC_API int GC_CALL
GC_unregister_disappearing_link(void **link)
{
  struct disappearing_link *curr_dl;

  if ((ADDR(link) & (ALIGNMENT - 1)) != 0) {
    /* Nothing to do. */
    return 0;
  }

  LOCK();
  curr_dl = GC_unregister_disappearing_link_inner(&GC_dl_hashtbl, link);
  UNLOCK();
  if (NULL == curr_dl)
    return 0;
  FREE_DL_ENTRY(curr_dl);
  return 1;
}

/*
 * Mark from one finalizable object using the specified mark procedure.
 * May not mark the object pointed to by `real_ptr` (i.e, it is the job
 * of the caller, if appropriate).  Note that this is called with the
 * mutator running.  This is safe only if the mutator (client) gets
 * the allocator lock to reveal hidden pointers.
 */
GC_INLINE void
GC_mark_fo(ptr_t real_ptr, finalization_mark_proc fo_mark_proc)
{
  GC_ASSERT(I_HOLD_LOCK());
  fo_mark_proc(real_ptr);
  /* Process objects pushed by the mark procedure. */
  while (!GC_mark_stack_empty())
    MARK_FROM_MARK_STACK();
}

/* Complete a collection in progress, if any. */
GC_INLINE void
GC_complete_ongoing_collection(void)
{
  if (UNLIKELY(GC_collection_in_progress())) {
    while (!GC_mark_some(NULL)) {
      /* Empty. */
    }
  }
}

/* Toggle-refs support. */

#  ifndef GC_TOGGLE_REFS_NOT_NEEDED
typedef union toggle_ref_u GCToggleRef;

STATIC GC_toggleref_func GC_toggleref_callback = 0;

GC_INNER void
GC_process_togglerefs(void)
{
  size_t i;
  size_t new_size = 0;
  GC_bool needs_barrier = FALSE;

  GC_ASSERT(I_HOLD_LOCK());
  for (i = 0; i < GC_toggleref_array_size; ++i) {
    GCToggleRef *r = &GC_toggleref_arr[i];
    void *obj = r->strong_ref;

    if ((ADDR(obj) & 1) != 0) {
      obj = GC_REVEAL_POINTER(r->weak_ref);
      GC_ASSERT((ADDR(obj) & 1) == 0);
    }
    if (NULL == obj)
      continue;

    switch (GC_toggleref_callback(obj)) {
    case GC_TOGGLE_REF_DROP:
      break;
    case GC_TOGGLE_REF_STRONG:
      GC_toggleref_arr[new_size++].strong_ref = obj;
      needs_barrier = TRUE;
      break;
    case GC_TOGGLE_REF_WEAK:
      GC_toggleref_arr[new_size++].weak_ref = GC_HIDE_POINTER(obj);
      break;
    default:
      ABORT("Bad toggle-ref status returned by callback");
    }
  }

  if (new_size < GC_toggleref_array_size) {
    BZERO(&GC_toggleref_arr[new_size],
          (GC_toggleref_array_size - new_size) * sizeof(GCToggleRef));
    GC_toggleref_array_size = new_size;
  }
  if (needs_barrier)
    GC_dirty(GC_toggleref_arr); /*< entire object */
}

STATIC void GC_normal_finalize_mark_proc(ptr_t);

STATIC void
GC_mark_togglerefs(void)
{
  size_t i;

  GC_ASSERT(I_HOLD_LOCK());
  if (NULL == GC_toggleref_arr)
    return;

  GC_set_mark_bit(GC_toggleref_arr);
  for (i = 0; i < GC_toggleref_array_size; ++i) {
    void *obj = GC_toggleref_arr[i].strong_ref;
    if (obj != NULL && (ADDR(obj) & 1) == 0) {
      /* Push and mark the object. */
      GC_mark_fo((ptr_t)obj, GC_normal_finalize_mark_proc);
      GC_set_mark_bit(obj);
      GC_complete_ongoing_collection();
    }
  }
}

STATIC void
GC_clear_togglerefs(void)
{
  size_t i;

  GC_ASSERT(I_HOLD_LOCK());
  for (i = 0; i < GC_toggleref_array_size; ++i) {
    GCToggleRef *r = &GC_toggleref_arr[i];

    if ((ADDR(r->strong_ref) & 1) != 0) {
      if (!GC_is_marked(GC_REVEAL_POINTER(r->weak_ref))) {
        r->weak_ref = 0;
      } else {
        /* No need to copy, this garbage collector is a non-moving one. */
      }
    }
  }
}

GC_API void GC_CALL
GC_set_toggleref_func(GC_toggleref_func fn)
{
  LOCK();
  GC_toggleref_callback = fn;
  UNLOCK();
}

GC_API GC_toggleref_func GC_CALL
GC_get_toggleref_func(void)
{
  GC_toggleref_func fn;

  READER_LOCK();
  fn = GC_toggleref_callback;
  READER_UNLOCK();
  return fn;
}

static GC_bool
ensure_toggleref_capacity(size_t capacity_inc)
{
  GC_ASSERT(I_HOLD_LOCK());
  if (NULL == GC_toggleref_arr) {
    /* Set the initial capacity. */
    GC_toggleref_array_capacity = 32;

    GC_toggleref_arr = (GCToggleRef *)GC_INTERNAL_MALLOC_IGNORE_OFF_PAGE(
        GC_toggleref_array_capacity * sizeof(GCToggleRef), NORMAL);
    if (NULL == GC_toggleref_arr)
      return FALSE;
  }
  if (GC_toggleref_array_size + capacity_inc >= GC_toggleref_array_capacity) {
    GCToggleRef *new_array;
    while (GC_toggleref_array_capacity
           < GC_toggleref_array_size + capacity_inc) {
      GC_toggleref_array_capacity *= 2;
      if ((GC_toggleref_array_capacity
           & ((size_t)1 << (sizeof(size_t) * 8 - 1)))
          != 0) {
        /* An overflow. */
        return FALSE;
      }
    }

    new_array = (GCToggleRef *)GC_INTERNAL_MALLOC_IGNORE_OFF_PAGE(
        GC_toggleref_array_capacity * sizeof(GCToggleRef), NORMAL);
    if (UNLIKELY(NULL == new_array))
      return FALSE;
    if (LIKELY(GC_toggleref_array_size > 0))
      BCOPY(GC_toggleref_arr, new_array,
            GC_toggleref_array_size * sizeof(GCToggleRef));
    GC_INTERNAL_FREE(GC_toggleref_arr);
    GC_toggleref_arr = new_array;
  }
  return TRUE;
}

GC_API int GC_CALL
GC_toggleref_add(void *obj, int is_strong_ref)
{
  int res = GC_SUCCESS;

  GC_ASSERT(NONNULL_ARG_NOT_NULL(obj));
  LOCK();
  GC_ASSERT((ADDR(obj) & 1) == 0 && obj == GC_base(obj));
  if (GC_toggleref_callback != 0) {
    if (!ensure_toggleref_capacity(1)) {
      res = GC_NO_MEMORY;
    } else {
      GCToggleRef *r = &GC_toggleref_arr[GC_toggleref_array_size];

      if (is_strong_ref) {
        r->strong_ref = obj;
        GC_dirty(GC_toggleref_arr + GC_toggleref_array_size);
      } else {
        r->weak_ref = GC_HIDE_POINTER(obj);
        GC_ASSERT((r->weak_ref & 1) != 0);
      }
      GC_toggleref_array_size++;
    }
  }
  UNLOCK();
  return res;
}
#  endif /* !GC_TOGGLE_REFS_NOT_NEEDED */

/* Finalizer callback support. */

STATIC GC_await_finalize_proc GC_object_finalized_proc = 0;

GC_API void GC_CALL
GC_set_await_finalize_proc(GC_await_finalize_proc fn)
{
  LOCK();
  GC_object_finalized_proc = fn;
  UNLOCK();
}

GC_API GC_await_finalize_proc GC_CALL
GC_get_await_finalize_proc(void)
{
  GC_await_finalize_proc fn;

  READER_LOCK();
  fn = GC_object_finalized_proc;
  READER_UNLOCK();
  return fn;
}

#  ifndef GC_LONG_REFS_NOT_NEEDED
GC_API int GC_CALL
GC_register_long_link(void **link, const void *obj)
{
  if ((ADDR(link) & (ALIGNMENT - 1)) != 0 || !NONNULL_ARG_NOT_NULL(link))
    ABORT("Bad arg to GC_register_long_link");
  return GC_register_disappearing_link_inner(&GC_ll_hashtbl, link, obj,
                                             "long dl");
}

GC_API int GC_CALL
GC_unregister_long_link(void **link)
{
  struct disappearing_link *curr_dl;

  if ((ADDR(link) & (ALIGNMENT - 1)) != 0) {
    /* Nothing to do. */
    return 0;
  }
  LOCK();
  curr_dl = GC_unregister_disappearing_link_inner(&GC_ll_hashtbl, link);
  UNLOCK();
  if (NULL == curr_dl)
    return 0;
  FREE_DL_ENTRY(curr_dl);
  return 1;
}
#  endif /* !GC_LONG_REFS_NOT_NEEDED */

#  ifndef GC_MOVE_DISAPPEARING_LINK_NOT_NEEDED
STATIC int
GC_move_disappearing_link_inner(struct dl_hashtbl_s *dl_hashtbl, void **link,
                                void **new_link)
{
  struct disappearing_link *curr_dl, *new_dl;
  struct disappearing_link *prev_dl = NULL;
  size_t curr_index, new_index;
  GC_hidden_pointer curr_hidden_link, new_hidden_link;

#    ifdef GC_ASSERTIONS
  GC_noop1_ptr(*new_link);
#    endif
  GC_ASSERT(I_HOLD_LOCK());
  if (UNLIKELY(NULL == dl_hashtbl->head))
    return GC_NOT_FOUND;

  /* Find current link. */
  curr_index = HASH2(link, dl_hashtbl->log_size);
  curr_hidden_link = GC_HIDE_POINTER(link);
  for (curr_dl = dl_hashtbl->head[curr_index]; curr_dl;
       curr_dl = dl_next(curr_dl)) {
    if (curr_dl->dl_hidden_link == curr_hidden_link)
      break;
    prev_dl = curr_dl;
  }
  if (UNLIKELY(NULL == curr_dl)) {
    return GC_NOT_FOUND;
  } else if (link == new_link) {
    /* Nothing to do. */
    return GC_SUCCESS;
  }

  /* `link` is found; now check `new_link` is not present. */
  new_index = HASH2(new_link, dl_hashtbl->log_size);
  new_hidden_link = GC_HIDE_POINTER(new_link);
  for (new_dl = dl_hashtbl->head[new_index]; new_dl;
       new_dl = dl_next(new_dl)) {
    if (new_dl->dl_hidden_link == new_hidden_link) {
      /* Target already registered; bail out. */
      return GC_DUPLICATE;
    }
  }

  /* Remove from old, add to new, update `link`. */
  if (NULL == prev_dl) {
    dl_hashtbl->head[curr_index] = dl_next(curr_dl);
  } else {
    dl_set_next(prev_dl, dl_next(curr_dl));
    GC_dirty(prev_dl);
  }
  curr_dl->dl_hidden_link = new_hidden_link;
  dl_set_next(curr_dl, dl_hashtbl->head[new_index]);
  dl_hashtbl->head[new_index] = curr_dl;
  GC_dirty(curr_dl);
  GC_dirty(dl_hashtbl->head); /*< entire object */
  return GC_SUCCESS;
}

GC_API int GC_CALL
GC_move_disappearing_link(void **link, void **new_link)
{
  int result;

  if ((ADDR(new_link) & (ALIGNMENT - 1)) != 0
      || !NONNULL_ARG_NOT_NULL(new_link))
    ABORT("Bad new_link arg to GC_move_disappearing_link");
  if ((ADDR(link) & (ALIGNMENT - 1)) != 0) {
    /* Nothing to do. */
    return GC_NOT_FOUND;
  }
  LOCK();
  result = GC_move_disappearing_link_inner(&GC_dl_hashtbl, link, new_link);
  UNLOCK();
  return result;
}

#    ifndef GC_LONG_REFS_NOT_NEEDED
GC_API int GC_CALL
GC_move_long_link(void **link, void **new_link)
{
  int result;

  if ((ADDR(new_link) & (ALIGNMENT - 1)) != 0
      || !NONNULL_ARG_NOT_NULL(new_link))
    ABORT("Bad new_link arg to GC_move_long_link");
  if ((ADDR(link) & (ALIGNMENT - 1)) != 0) {
    /* Nothing to do. */
    return GC_NOT_FOUND;
  }
  LOCK();
  result = GC_move_disappearing_link_inner(&GC_ll_hashtbl, link, new_link);
  UNLOCK();
  return result;
}
#    endif
#  endif /* !GC_MOVE_DISAPPEARING_LINK_NOT_NEEDED */

/*
 * Various finalization marker procedures.  Note that mark stack overflow
 * is handled by the caller, and is not a disaster.
 */

#  if defined(_MSC_VER) && defined(I386)
GC_ATTR_NOINLINE
/* Otherwise some optimizer bug is tickled in VC for x86 (v19, at least). */
#  endif
STATIC void
GC_normal_finalize_mark_proc(ptr_t p)
{
  GC_mark_stack_top = GC_push_obj(p, HDR(p), GC_mark_stack_top,
                                  GC_mark_stack + GC_mark_stack_size);
}

/*
 * This only pays very partial attention to the mark descriptor.
 * It does the right thing for normal and atomic objects, and treats
 * most others as normal.
 */
STATIC void
GC_ignore_self_finalize_mark_proc(ptr_t p)
{
  const hdr *hhdr = HDR(p);
  word descr = hhdr->hb_descr;
  ptr_t current_p;
  ptr_t scan_limit;
  ptr_t target_limit = p + hhdr->hb_sz - 1;

  if ((descr & GC_DS_TAGS) == GC_DS_LENGTH) {
    scan_limit = p + descr - sizeof(ptr_t);
  } else {
    scan_limit = target_limit + 1 - sizeof(ptr_t);
  }
  for (current_p = p; ADDR_GE(scan_limit, current_p); current_p += ALIGNMENT) {
    ptr_t q;

    LOAD_PTR_OR_CONTINUE(q, current_p);
    if (ADDR_LT(q, p) || ADDR_LT(target_limit, q)) {
      GC_PUSH_ONE_HEAP(q, current_p, GC_mark_stack_top);
    }
  }
}

STATIC void
GC_null_finalize_mark_proc(ptr_t p)
{
  UNUSED_ARG(p);
}

/*
 * `GC_unreachable_finalize_mark_proc` is an alias for normal marking,
 * but it is explicitly tested for, and triggers different behavior.
 * Objects registered in this way are not finalized if they are reachable
 * by other finalizable objects, even if those other objects specify
 * no ordering.
 */
STATIC void
GC_unreachable_finalize_mark_proc(ptr_t p)
{
  /*
   * A dummy comparison to ensure the compiler not to optimize two
   * identical functions into a single one (thus, to ensure a unique
   * address of each).  Alternatively, `GC_noop1_ptr(p)` could be used.
   */
  if (UNLIKELY(NULL == p))
    return;

  GC_normal_finalize_mark_proc(p);
}

/*
 * Register a finalization function.  See `gc.h` file for details.
 * The last parameter is a procedure that determines marking for
 * finalization ordering.  Any objects marked by that procedure will be
 * guaranteed to not have been finalized when this finalizer is invoked.
 */
STATIC void
GC_register_finalizer_inner(void *obj, GC_finalization_proc fn, void *cd,
                            GC_finalization_proc *ofn, void **ocd,
                            finalization_mark_proc mp)
{
  struct finalizable_object *curr_fo;
  size_t index;
  struct finalizable_object *new_fo = 0;
  const hdr *hhdr = NULL; /*< initialized to prevent warning */

  GC_ASSERT(GC_is_initialized);
  if (UNLIKELY(GC_find_leak_inner)) {
    /* No-op.  `*ocd` and `*ofn` remain unchanged. */
    return;
  }
  LOCK();
  GC_ASSERT(obj != NULL && GC_base_C(obj) == obj);
  if (mp == GC_unreachable_finalize_mark_proc)
    GC_need_unreachable_finalization = TRUE;
  if (UNLIKELY(NULL == GC_fnlz_roots.fo_head)
      || UNLIKELY(GC_fo_entries > ((size_t)1 << GC_log_fo_table_size))) {
    GC_grow_table((struct hash_chain_entry ***)&GC_fnlz_roots.fo_head,
                  &GC_log_fo_table_size, &GC_fo_entries);
    GC_COND_LOG_PRINTF("Grew fo table to %u entries\n",
                       1U << GC_log_fo_table_size);
  }
  for (;;) {
    struct finalizable_object *prev_fo = NULL;
    GC_oom_func oom_fn;

    index = HASH2(obj, GC_log_fo_table_size);
    curr_fo = GC_fnlz_roots.fo_head[index];
    while (curr_fo != NULL) {
      GC_ASSERT(GC_size(curr_fo) >= sizeof(struct finalizable_object));
      if (curr_fo->fo_hidden_base == GC_HIDE_POINTER(obj)) {
        /*
         * Interruption by a signal in the middle of this should be safe.
         * The client may see only `*ocd` updated, but we will declare that
         * to be his problem.
         */
        if (ocd)
          *ocd = curr_fo->fo_client_data;
        if (ofn)
          *ofn = curr_fo->fo_fn;
        /* Delete the structure for `obj`. */
        if (prev_fo == 0) {
          GC_fnlz_roots.fo_head[index] = fo_next(curr_fo);
        } else {
          fo_set_next(prev_fo, fo_next(curr_fo));
          GC_dirty(prev_fo);
        }
        if (fn == 0) {
          GC_fo_entries--;
          /*
           * May not happen if we get a signal.  But a high estimate will
           * only make the table larger than necessary.
           */
#  if !defined(THREADS) && !defined(DBG_HDRS_ALL)
          GC_free(curr_fo);
#  endif
        } else {
          curr_fo->fo_fn = fn;
          curr_fo->fo_client_data = (ptr_t)cd;
          curr_fo->fo_mark_proc = mp;
          GC_dirty(curr_fo);
          /*
           * Reinsert it.  We deleted it first to maintain consistency in
           * the event of a signal.
           */
          if (prev_fo == 0) {
            GC_fnlz_roots.fo_head[index] = curr_fo;
          } else {
            fo_set_next(prev_fo, curr_fo);
            GC_dirty(prev_fo);
          }
        }
        if (NULL == prev_fo)
          GC_dirty(GC_fnlz_roots.fo_head + index);
        UNLOCK();
#  ifndef DBG_HDRS_ALL
        /* Free unused `new_fo` returned by `GC_oom_fn()`. */
        GC_free(new_fo);
#  endif
        return;
      }
      prev_fo = curr_fo;
      curr_fo = fo_next(curr_fo);
    }
    if (UNLIKELY(new_fo != 0)) {
      /* `new_fo` is returned by `GC_oom_fn()`. */
      GC_ASSERT(fn != 0);
#  ifdef LINT2
      if (NULL == hhdr)
        ABORT("Bad hhdr in GC_register_finalizer_inner");
#  endif
      break;
    }
    if (fn == 0) {
      if (ocd)
        *ocd = 0;
      if (ofn)
        *ofn = 0;
      UNLOCK();
      return;
    }
    GET_HDR(obj, hhdr);
    if (UNLIKELY(NULL == hhdr)) {
      /* We will not collect it, hence finalizer would not be run. */
      if (ocd)
        *ocd = 0;
      if (ofn)
        *ofn = 0;
      UNLOCK();
      return;
    }
    new_fo = (struct finalizable_object *)GC_INTERNAL_MALLOC(
        sizeof(struct finalizable_object), NORMAL);
    if (LIKELY(new_fo != 0))
      break;
    oom_fn = GC_oom_fn;
    UNLOCK();
    new_fo = (struct finalizable_object *)(*oom_fn)(
        sizeof(struct finalizable_object));
    if (0 == new_fo) {
      /* No enough memory.  `*ocd` and `*ofn` remain unchanged. */
      return;
    }
    /* It is not likely we will make it here, but... */
    LOCK();
    /*
     * Recalculate index since the table may grow and check again that
     * our finalizer is not in the table.
     */
  }
  GC_ASSERT(GC_size(new_fo) >= sizeof(struct finalizable_object));
  if (ocd)
    *ocd = 0;
  if (ofn)
    *ofn = 0;
  new_fo->fo_hidden_base = GC_HIDE_POINTER(obj);
  new_fo->fo_fn = fn;
  new_fo->fo_client_data = (ptr_t)cd;
  new_fo->fo_object_sz = hhdr->hb_sz;
  new_fo->fo_mark_proc = mp;
  fo_set_next(new_fo, GC_fnlz_roots.fo_head[index]);
  GC_dirty(new_fo);
  GC_fo_entries++;
  GC_fnlz_roots.fo_head[index] = new_fo;
  GC_dirty(GC_fnlz_roots.fo_head + index);
  UNLOCK();
}

GC_API void GC_CALL
GC_register_finalizer(void *obj, GC_finalization_proc fn, void *cd,
                      GC_finalization_proc *ofn, void **ocd)
{
  GC_register_finalizer_inner(obj, fn, cd, ofn, ocd,
                              GC_normal_finalize_mark_proc);
}

GC_API void GC_CALL
GC_register_finalizer_ignore_self(void *obj, GC_finalization_proc fn, void *cd,
                                  GC_finalization_proc *ofn, void **ocd)
{
  GC_register_finalizer_inner(obj, fn, cd, ofn, ocd,
                              GC_ignore_self_finalize_mark_proc);
}

GC_API void GC_CALL
GC_register_finalizer_no_order(void *obj, GC_finalization_proc fn, void *cd,
                               GC_finalization_proc *ofn, void **ocd)
{
  GC_register_finalizer_inner(obj, fn, cd, ofn, ocd,
                              GC_null_finalize_mark_proc);
}

GC_API void GC_CALL
GC_register_finalizer_unreachable(void *obj, GC_finalization_proc fn, void *cd,
                                  GC_finalization_proc *ofn, void **ocd)
{
  GC_ASSERT(GC_java_finalization);
  GC_register_finalizer_inner(obj, fn, cd, ofn, ocd,
                              GC_unreachable_finalize_mark_proc);
}

#  ifndef NO_DEBUGGING
STATIC void
GC_dump_finalization_links(const struct dl_hashtbl_s *dl_hashtbl)
{
  size_t dl_size = (size_t)1 << dl_hashtbl->log_size;
  size_t i;

  if (NULL == dl_hashtbl->head) {
    /* The table is empty. */
    return;
  }

  for (i = 0; i < dl_size; i++) {
    struct disappearing_link *curr_dl;

    for (curr_dl = dl_hashtbl->head[i]; curr_dl != 0;
         curr_dl = dl_next(curr_dl)) {
      ptr_t real_ptr = (ptr_t)GC_REVEAL_POINTER(curr_dl->dl_hidden_obj);
      ptr_t real_link = (ptr_t)GC_REVEAL_POINTER(curr_dl->dl_hidden_link);

      GC_printf("Object: %p, link value: %p, link addr: %p\n",
                (void *)real_ptr, *(void **)real_link, (void *)real_link);
    }
  }
}

GC_API void GC_CALL
GC_dump_finalization(void)
{
  struct finalizable_object *curr_fo;
  size_t i;
  size_t fo_size
      = GC_fnlz_roots.fo_head == NULL ? 0 : (size_t)1 << GC_log_fo_table_size;

  GC_printf("\n***Disappearing (short) links:\n");
  GC_dump_finalization_links(&GC_dl_hashtbl);
#    ifndef GC_LONG_REFS_NOT_NEEDED
  GC_printf("\n***Disappearing long links:\n");
  GC_dump_finalization_links(&GC_ll_hashtbl);
#    endif
  GC_printf("\n***Finalizers:\n");
  for (i = 0; i < fo_size; i++) {
    for (curr_fo = GC_fnlz_roots.fo_head[i]; curr_fo != NULL;
         curr_fo = fo_next(curr_fo)) {
      ptr_t real_ptr = (ptr_t)GC_REVEAL_POINTER(curr_fo->fo_hidden_base);

      GC_printf("Finalizable object: %p\n", (void *)real_ptr);
    }
  }
}
#  endif /* !NO_DEBUGGING */

#  ifndef THREADS
/*
 * Checks and updates the level of finalizers recursion.
 * Returns `NULL` if `GC_invoke_finalizers()` should not be called by
 * the collector (to minimize the risk of a deep finalizers recursion),
 * otherwise returns a pointer to `GC_finalizer_nested`.
 */
STATIC unsigned char *
GC_check_finalizer_nested(void)
{
  unsigned nesting_level = GC_finalizer_nested;
  if (nesting_level) {
    /*
     * We are inside another `GC_invoke_finalizers()`.  Skip some
     * implicitly-called `GC_invoke_finalizers()` depending on the
     * nesting (recursion) level.
     */
    if ((unsigned)(++GC_finalizer_skipped) < (1U << nesting_level))
      return NULL;
    GC_finalizer_skipped = 0;
  }
  GC_finalizer_nested = (unsigned char)(nesting_level + 1);
  return &GC_finalizer_nested;
}
#  endif /* !THREADS */

GC_INLINE void
GC_make_disappearing_links_disappear(struct dl_hashtbl_s *dl_hashtbl,
                                     GC_bool is_remove_dangling)
{
  size_t i;
  size_t dl_size = (size_t)1 << dl_hashtbl->log_size;
  GC_bool needs_barrier = FALSE;

  GC_ASSERT(I_HOLD_LOCK());
  if (NULL == dl_hashtbl->head) {
    /* The table is empty. */
    return;
  }

  for (i = 0; i < dl_size; i++) {
    struct disappearing_link *curr_dl, *next_dl;
    struct disappearing_link *prev_dl = NULL;

    for (curr_dl = dl_hashtbl->head[i]; curr_dl != NULL; curr_dl = next_dl) {
      next_dl = dl_next(curr_dl);
#  if defined(GC_ASSERTIONS) && !defined(THREAD_SANITIZER)
      /* Check accessibility of the location pointed by the link. */
      GC_noop1_ptr(*(ptr_t *)GC_REVEAL_POINTER(curr_dl->dl_hidden_link));
#  endif
      if (is_remove_dangling) {
        ptr_t real_link
            = (ptr_t)GC_base(GC_REVEAL_POINTER(curr_dl->dl_hidden_link));

        if (NULL == real_link || LIKELY(GC_is_marked(real_link))) {
          prev_dl = curr_dl;
          continue;
        }
      } else {
        if (LIKELY(GC_is_marked(
                (ptr_t)GC_REVEAL_POINTER(curr_dl->dl_hidden_obj)))) {
          prev_dl = curr_dl;
          continue;
        }
        *(ptr_t *)GC_REVEAL_POINTER(curr_dl->dl_hidden_link) = NULL;
      }

      /* Delete `curr_dl` entry from `dl_hashtbl`. */
      if (NULL == prev_dl) {
        dl_hashtbl->head[i] = next_dl;
        needs_barrier = TRUE;
      } else {
        dl_set_next(prev_dl, next_dl);
        GC_dirty(prev_dl);
      }
      GC_clear_mark_bit(curr_dl);
      dl_hashtbl->entries--;
    }
  }
  if (needs_barrier)
    GC_dirty(dl_hashtbl->head); /*< entire object */
}

GC_INNER void
GC_finalize(void)
{
  struct finalizable_object *curr_fo, *prev_fo, *next_fo;
  ptr_t real_ptr;
  size_t i;
  size_t fo_size
      = GC_fnlz_roots.fo_head == NULL ? 0 : (size_t)1 << GC_log_fo_table_size;
  GC_bool needs_barrier = FALSE;

  GC_ASSERT(I_HOLD_LOCK());
#  ifndef SMALL_CONFIG
  /* Save current `GC_dl_entries` value for stats printing. */
  GC_old_dl_entries = GC_dl_hashtbl.entries;
#    ifndef GC_LONG_REFS_NOT_NEEDED
  /* Save current `GC_ll_entries` value for stats printing. */
  GC_old_ll_entries = GC_ll_hashtbl.entries;
#    endif
#  endif

#  ifndef GC_TOGGLE_REFS_NOT_NEEDED
  GC_mark_togglerefs();
#  endif
  GC_make_disappearing_links_disappear(&GC_dl_hashtbl, FALSE);

  /*
   * Mark all objects reachable via chains of 1 or more pointers from
   * finalizable objects.
   */
  GC_ASSERT(!GC_collection_in_progress());
  for (i = 0; i < fo_size; i++) {
    for (curr_fo = GC_fnlz_roots.fo_head[i]; curr_fo != NULL;
         curr_fo = fo_next(curr_fo)) {
      GC_ASSERT(GC_size(curr_fo) >= sizeof(struct finalizable_object));
      real_ptr = (ptr_t)GC_REVEAL_POINTER(curr_fo->fo_hidden_base);
      if (!GC_is_marked(real_ptr)) {
        GC_MARKED_FOR_FINALIZATION(real_ptr);
        GC_mark_fo(real_ptr, curr_fo->fo_mark_proc);
        if (GC_is_marked(real_ptr)) {
          WARN("Finalization cycle involving %p\n", real_ptr);
        }
      }
    }
  }
  /* Enqueue for finalization all objects that are still unreachable. */
  GC_bytes_finalized = 0;
  for (i = 0; i < fo_size; i++) {
    curr_fo = GC_fnlz_roots.fo_head[i];
    prev_fo = NULL;
    while (curr_fo != NULL) {
      real_ptr = (ptr_t)GC_REVEAL_POINTER(curr_fo->fo_hidden_base);
      if (!GC_is_marked(real_ptr)) {
        if (!GC_java_finalization) {
          GC_set_mark_bit(real_ptr);
        }
        /* Delete from hash table. */
        next_fo = fo_next(curr_fo);
        if (NULL == prev_fo) {
          GC_fnlz_roots.fo_head[i] = next_fo;
          if (GC_object_finalized_proc) {
            GC_dirty(GC_fnlz_roots.fo_head + i);
          } else {
            needs_barrier = TRUE;
          }
        } else {
          fo_set_next(prev_fo, next_fo);
          GC_dirty(prev_fo);
        }
        GC_fo_entries--;
        if (GC_object_finalized_proc)
          GC_object_finalized_proc(real_ptr);

        /* Add to list of objects awaiting finalization. */
        fo_set_next(curr_fo, GC_fnlz_roots.finalize_now);
        GC_dirty(curr_fo);
        SET_FINALIZE_NOW(curr_fo);
        /* Unhide object pointer so any future collections will see it. */
        curr_fo->fo_hidden_base
            = (GC_hidden_pointer)GC_REVEAL_POINTER(curr_fo->fo_hidden_base);

        GC_bytes_finalized
            += (word)curr_fo->fo_object_sz + sizeof(struct finalizable_object);
        GC_ASSERT(GC_is_marked(GC_base(curr_fo)));
        curr_fo = next_fo;
      } else {
        prev_fo = curr_fo;
        curr_fo = fo_next(curr_fo);
      }
    }
  }

  if (GC_java_finalization) {
    /*
     * Make sure we mark everything reachable from objects finalized
     * using the no-order `fo_mark_proc`.
     */
    for (curr_fo = GC_fnlz_roots.finalize_now; curr_fo != NULL;
         curr_fo = fo_next(curr_fo)) {
      real_ptr = (ptr_t)curr_fo->fo_hidden_base; /*< revealed */
      if (!GC_is_marked(real_ptr)) {
        if (curr_fo->fo_mark_proc == GC_null_finalize_mark_proc) {
          GC_mark_fo(real_ptr, GC_normal_finalize_mark_proc);
        }
        if (curr_fo->fo_mark_proc != GC_unreachable_finalize_mark_proc) {
          GC_set_mark_bit(real_ptr);
        }
      }
    }

    /*
     * Now revive finalize-when-unreachable objects reachable from other
     * finalizable objects.
     */
    if (GC_need_unreachable_finalization) {
      curr_fo = GC_fnlz_roots.finalize_now;
#  if defined(GC_ASSERTIONS) || defined(LINT2)
      if (curr_fo != NULL && NULL == GC_fnlz_roots.fo_head)
        ABORT("GC_fnlz_roots.fo_head is null");
#  endif
      for (prev_fo = NULL; curr_fo != NULL;
           prev_fo = curr_fo, curr_fo = next_fo) {
        next_fo = fo_next(curr_fo);
        if (curr_fo->fo_mark_proc != GC_unreachable_finalize_mark_proc)
          continue;

        real_ptr = (ptr_t)curr_fo->fo_hidden_base; /*< revealed */
        if (!GC_is_marked(real_ptr)) {
          GC_set_mark_bit(real_ptr);
          continue;
        }
        if (NULL == prev_fo) {
          SET_FINALIZE_NOW(next_fo);
        } else {
          fo_set_next(prev_fo, next_fo);
          GC_dirty(prev_fo);
        }
        curr_fo->fo_hidden_base = GC_HIDE_POINTER(real_ptr);
        GC_bytes_finalized
            -= (word)curr_fo->fo_object_sz + sizeof(struct finalizable_object);

        i = HASH2(real_ptr, GC_log_fo_table_size);
        fo_set_next(curr_fo, GC_fnlz_roots.fo_head[i]);
        GC_dirty(curr_fo);
        GC_fo_entries++;
        GC_fnlz_roots.fo_head[i] = curr_fo;
        curr_fo = prev_fo;
        needs_barrier = TRUE;
      }
    }
  }
  if (needs_barrier)
    GC_dirty(GC_fnlz_roots.fo_head); /*< entire object */

  /* Remove dangling disappearing links. */
  GC_make_disappearing_links_disappear(&GC_dl_hashtbl, TRUE);

#  ifndef GC_TOGGLE_REFS_NOT_NEEDED
  GC_clear_togglerefs();
#  endif
#  ifndef GC_LONG_REFS_NOT_NEEDED
  GC_make_disappearing_links_disappear(&GC_ll_hashtbl, FALSE);
  GC_make_disappearing_links_disappear(&GC_ll_hashtbl, TRUE);
#  endif

  if (GC_alloc_fail_count > 0) {
    /*
     * Do not prevent running finalizers if there has been an allocation
     * failure recently.
     */
#  ifdef THREADS
    GC_reset_finalizer_nested();
#  else
    GC_finalizer_nested = 0;
#  endif
  }
}

/*
 * Count of finalizers to run, at most, during a single invocation
 * of `GC_invoke_finalizers()`; zero means no limit.  Accessed with the
 * allocator lock held.
 */
STATIC unsigned GC_interrupt_finalizers = 0;

#  ifndef JAVA_FINALIZATION_NOT_NEEDED

/*
 * Enqueue all remaining finalizers to be run.  A collection in progress,
 * if any, is completed when the first finalizer is enqueued.
 */
STATIC void
GC_enqueue_all_finalizers(void)
{
  size_t i;
  size_t fo_size
      = GC_fnlz_roots.fo_head == NULL ? 0 : (size_t)1 << GC_log_fo_table_size;

  GC_ASSERT(I_HOLD_LOCK());
  GC_bytes_finalized = 0;
  for (i = 0; i < fo_size; i++) {
    struct finalizable_object *curr_fo = GC_fnlz_roots.fo_head[i];

    GC_fnlz_roots.fo_head[i] = NULL;
    while (curr_fo != NULL) {
      struct finalizable_object *next_fo;
      ptr_t real_ptr = (ptr_t)GC_REVEAL_POINTER(curr_fo->fo_hidden_base);

      GC_mark_fo(real_ptr, GC_normal_finalize_mark_proc);
      GC_set_mark_bit(real_ptr);
      GC_complete_ongoing_collection();
      next_fo = fo_next(curr_fo);

      /* Add to list of objects awaiting finalization. */
      fo_set_next(curr_fo, GC_fnlz_roots.finalize_now);
      GC_dirty(curr_fo);
      SET_FINALIZE_NOW(curr_fo);

      /* Unhide object pointer so any future collections will see it. */
      curr_fo->fo_hidden_base
          = (GC_hidden_pointer)GC_REVEAL_POINTER(curr_fo->fo_hidden_base);
      GC_bytes_finalized
          += curr_fo->fo_object_sz + sizeof(struct finalizable_object);
      curr_fo = next_fo;
    }
  }
  /* All entries are deleted from the hash table. */
  GC_fo_entries = 0;
}

static int invoke_finalizers_internal(GC_bool all);

GC_API void GC_CALL
GC_finalize_all(void)
{
  for (;;) {
    LOCK();
    if (0 == GC_fo_entries) {
      UNLOCK();
      break;
    }
    GC_enqueue_all_finalizers();
    UNLOCK();

    /*
     * Running the finalizers in this thread is arguably not a good idea
     * when we should be notifying another thread to run them.
     * But otherwise we do not have a great way to wait for them to run.
     */
    (void)invoke_finalizers_internal(TRUE);
  }
}

#  endif /* !JAVA_FINALIZATION_NOT_NEEDED */

GC_API void GC_CALL
GC_set_interrupt_finalizers(unsigned value)
{
  LOCK();
  GC_interrupt_finalizers = value;
  UNLOCK();
}

GC_API unsigned GC_CALL
GC_get_interrupt_finalizers(void)
{
  unsigned value;

  READER_LOCK();
  value = GC_interrupt_finalizers;
  READER_UNLOCK();
  return value;
}

GC_API int GC_CALL
GC_should_invoke_finalizers(void)
{
#  ifdef AO_HAVE_load
  return GC_cptr_load((volatile ptr_t *)&GC_fnlz_roots.finalize_now) != NULL;
#  else
  return GC_fnlz_roots.finalize_now != NULL;
#  endif /* !THREADS */
}

GC_API int GC_CALL
GC_invoke_finalizers(void)
{
  GC_ASSERT(I_DONT_HOLD_LOCK());
  return invoke_finalizers_internal(FALSE);
}

static int
invoke_finalizers_internal(GC_bool all)
{
  int count = 0;
  word bytes_freed_before = 0; /*< initialized to prevent warning */

  while (GC_should_invoke_finalizers()) {
    struct finalizable_object *curr_fo;
    ptr_t real_ptr;

    LOCK();
    if (0 == count) {
      /* Note: we hold the allocator lock here. */
      bytes_freed_before = GC_bytes_freed;
    } else if (UNLIKELY(GC_interrupt_finalizers != 0) && !all
               && (unsigned)count >= GC_interrupt_finalizers) {
      UNLOCK();
      break;
    }
    curr_fo = GC_fnlz_roots.finalize_now;
#  ifdef THREADS
    if (UNLIKELY(NULL == curr_fo)) {
      UNLOCK();
      break;
    }
#  endif
    SET_FINALIZE_NOW(fo_next(curr_fo));
    UNLOCK();
    fo_set_next(curr_fo, 0);
    real_ptr = (ptr_t)curr_fo->fo_hidden_base; /*< revealed */
    curr_fo->fo_fn(real_ptr, curr_fo->fo_client_data);
    curr_fo->fo_client_data = NULL;
    ++count;
    /*
     * Explicit freeing of `curr_fo` is probably a bad idea.
     * It throws off accounting if nearly all objects are finalizable.
     * Otherwise it should not matter.
     */
  }
  /* `bytes_freed_before` is initialized whenever `count` is nonzero. */
  if (count != 0
#  if defined(THREADS) && !defined(THREAD_SANITIZER)
      /*
       * A quick check whether some memory was freed.  The race with
       * `GC_free()` is safe to be ignored because we only need to know
       * if the current thread has deallocated something.
       */
      && bytes_freed_before != GC_bytes_freed
#  endif
  ) {
    LOCK();
    GC_finalizer_bytes_freed += (GC_bytes_freed - bytes_freed_before);
    UNLOCK();
  }
  return count;
}

GC_INNER void
GC_notify_or_invoke_finalizers(void)
{
  GC_finalizer_notifier_proc notifier_fn = 0;

#  if defined(THREADS) && !defined(KEEP_BACK_PTRS) && !defined(MAKE_BACK_GRAPH)
  /* Quick check (while unlocked) for an empty finalization queue. */
  if (!GC_should_invoke_finalizers())
    return;
#  endif
  LOCK();

#  if defined(KEEP_BACK_PTRS) || defined(MAKE_BACK_GRAPH)
  /*
   * This is a convenient place to generate backtraces if appropriate,
   * since that code is not callable with the allocator lock.
   */
  if (GC_gc_no != GC_last_back_trace_gc_no
      && LIKELY(GC_gc_no > 1) /*< skip initial collection */) {
#    ifdef KEEP_BACK_PTRS
    static GC_bool bt_in_progress = FALSE;

    if (!bt_in_progress) {
      long i;

      /* Prevent a recursion or parallel usage. */
      bt_in_progress = TRUE;
      for (i = 0; i < GC_backtraces; ++i) {
        /*
         * FIXME: This tolerates concurrent heap mutation, which may
         * cause occasional mysterious results.  We need to release
         * the allocator lock, since `GC_print_callers()` acquires it.
         * It probably should not.
         */
        void *current = GC_generate_random_valid_address();

        UNLOCK();
        GC_printf("\n***Chosen address %p in object\n", current);
        GC_print_backtrace(current);
        LOCK();
      }
      bt_in_progress = FALSE;
    }
#    endif
    GC_last_back_trace_gc_no = GC_gc_no;
#    ifdef MAKE_BACK_GRAPH
    if (GC_print_back_height) {
      GC_print_back_graph_stats();
    }
#    endif
  }
#  endif
  if (NULL == GC_fnlz_roots.finalize_now) {
    UNLOCK();
    return;
  }

  if (!GC_finalize_on_demand) {
    unsigned char *pnested;

#  ifdef THREADS
    if (UNLIKELY(GC_in_thread_creation)) {
      UNLOCK();
      return;
    }
#  endif
    pnested = GC_check_finalizer_nested();
    UNLOCK();
    /* Skip `GC_invoke_finalizers()` if nested. */
    if (pnested != NULL) {
      (void)invoke_finalizers_internal(FALSE);
      /* Reset since no more finalizers or interrupted. */
      *pnested = 0;
#  ifndef THREADS
      GC_ASSERT(NULL == GC_fnlz_roots.finalize_now
                || GC_interrupt_finalizers > 0);
#  else
      /*
       * Note: in the multi-threaded case GC can run concurrently and
       * add more finalizers to run.
       */
#  endif
    }
    return;
  }

  /* These variables require synchronization to avoid data race. */
  if (GC_last_finalizer_notification != GC_gc_no) {
    notifier_fn = GC_finalizer_notifier;
    GC_last_finalizer_notification = GC_gc_no;
  }
  UNLOCK();
  if (notifier_fn != 0) {
    /* Invoke the notifier. */
    (*notifier_fn)();
  }
}

#  ifndef SMALL_CONFIG
#    ifndef GC_LONG_REFS_NOT_NEEDED
#      define IF_LONG_REFS_PRESENT_ELSE(x, y) (x)
#    else
#      define IF_LONG_REFS_PRESENT_ELSE(x, y) (y)
#    endif

GC_INNER void
GC_print_finalization_stats(void)
{
  const struct finalizable_object *fo;
  unsigned long ready = 0;

  GC_log_printf(
      "%lu finalization entries;"
      " %lu/%lu short/long disappearing links alive\n",
      (unsigned long)GC_fo_entries, (unsigned long)GC_dl_hashtbl.entries,
      (unsigned long)IF_LONG_REFS_PRESENT_ELSE(GC_ll_hashtbl.entries, 0));

  for (fo = GC_fnlz_roots.finalize_now; fo != NULL; fo = fo_next(fo))
    ++ready;
  GC_log_printf("%lu finalization-ready objects;"
                " %ld/%ld short/long links cleared\n",
                ready, (long)GC_old_dl_entries - (long)GC_dl_hashtbl.entries,
                (long)IF_LONG_REFS_PRESENT_ELSE(
                    GC_old_ll_entries - GC_ll_hashtbl.entries, 0));
}
#  endif /* !SMALL_CONFIG */

#endif /* !GC_NO_FINALIZATION */
