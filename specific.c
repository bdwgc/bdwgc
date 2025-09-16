/*
 * Copyright (c) 2000 by Hewlett-Packard Company.  All rights reserved.
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
 * To determine type of `tsd` implementation; includes `private/specific.h`
 * file if needed.
 */
#include "private/thread_local_alloc.h"

#if defined(USE_CUSTOM_SPECIFIC)

/*
 * A thread-specific data entry which will never appear valid to a reader.
 * Used to fill in empty cache entries to avoid a check for 0.
 */
static const tse invalid_tse = { INVALID_QTID, 0, 0, INVALID_THREADID };

GC_INNER int
GC_key_create_inner(tsd **key_ptr)
{
  int i;
  int ret;
  tsd *result;

  GC_ASSERT(I_HOLD_LOCK());
  /* A quick alignment check, since we need atomic stores. */
  GC_ASSERT(ADDR(&invalid_tse.next) % ALIGNMENT == 0);
  result = (tsd *)MALLOC_CLEAR(sizeof(tsd));
  if (NULL == result)
    return ENOMEM;
  ret = pthread_mutex_init(&result->lock, NULL);
  if (ret != 0)
    return ret;

  for (i = 0; i < TS_CACHE_SIZE; ++i) {
    result->cache[i] = (tse *)GC_CAST_AWAY_CONST_PVOID(&invalid_tse);
  }
#  ifdef GC_ASSERTIONS
  for (i = 0; i < TS_HASH_SIZE; ++i) {
    GC_ASSERT(NULL == result->hash[i]);
  }
#  endif
  *key_ptr = result;
  return 0;
}

GC_INNER int
GC_setspecific(tsd *key, void *value)
{
  pthread_t self = pthread_self();
  unsigned hash_val = TS_HASH(self);
  volatile tse *entry;

  GC_ASSERT(I_HOLD_LOCK());
  GC_ASSERT(self != INVALID_THREADID);
  /* Disable garbage collection during `GC_malloc`. */
  GC_dont_gc++;
  entry = (volatile tse *)MALLOC_CLEAR(sizeof(tse));
  GC_dont_gc--;
  if (UNLIKELY(NULL == entry))
    return ENOMEM;

  pthread_mutex_lock(&key->lock);
  entry->next = key->hash[hash_val];
#  ifdef GC_ASSERTIONS
  {
    tse *p;

    /* Ensure no existing entry. */
    for (p = entry->next; p != NULL; p = p->next) {
      GC_ASSERT(!THREAD_EQUAL(p->thread, self));
    }
  }
#  endif
  entry->thread = self;
  entry->value = TS_HIDE_VALUE(value);
  GC_ASSERT(entry->qtid == INVALID_QTID);
  /*
   * There can only be one writer at a time, but this needs to be atomic
   * with respect to concurrent readers.
   */
  GC_cptr_store_release((volatile ptr_t *)&key->hash[hash_val],
                        (ptr_t)CAST_AWAY_VOLATILE_PVOID(entry));
  GC_dirty(CAST_AWAY_VOLATILE_PVOID(entry));
  GC_dirty(key->hash + hash_val);
  if (pthread_mutex_unlock(&key->lock) != 0)
    ABORT("pthread_mutex_unlock failed (setspecific)");
  return 0;
}

GC_INNER void
GC_remove_specific_after_fork(tsd *key, pthread_t t)
{
  unsigned hash_val = TS_HASH(t);
  tse *entry;
  tse *prev = NULL;

#  ifdef CAN_HANDLE_FORK
  /*
   * Both `GC_setspecific` and `GC_remove_specific` should be called with
   * the allocator lock held to ensure the consistency of the hash table
   * in the forked child process.
   */
  GC_ASSERT(I_HOLD_LOCK());
#  endif
  pthread_mutex_lock(&key->lock);
  for (entry = key->hash[hash_val];
       entry != NULL && !THREAD_EQUAL(entry->thread, t); entry = entry->next) {
    prev = entry;
  }
  /*
   * Invalidate `qtid` field, since `qtid` values may be reused, and
   * a later cache lookup could otherwise find this `entry`.
   */
  if (entry != NULL) {
    entry->qtid = INVALID_QTID;
    if (NULL == prev) {
      key->hash[hash_val] = entry->next;
      GC_dirty(key->hash + hash_val);
    } else {
      prev->next = entry->next;
      GC_dirty(prev);
    }
    /*
     * Atomic! Concurrent accesses still work.  They must, since readers
     * do not lock.  We should not need a `volatile` access here, since
     * both this and the preceding write should become visible no later
     * than the `pthread_mutex_unlock()` call.
     */
  }
  /*
   * If we wanted to deallocate the entry, we would first have to clear
   * any cache entries pointing to it.  That probably requires additional
   * synchronization, since we cannot prevent a concurrent cache lookup,
   * which should still be examining deallocated memory.  This can only
   * happen if the concurrent access is from another thread, and hence
   * has missed the cache, but still...
   */
#  ifdef LINT2
  GC_noop1_ptr(entry);
#  endif

  /*
   * With GC, we are done, since the pointers from the cache will be
   * overwritten, all local pointers to the entries will be dropped,
   * and the entry will then be reclaimed.
   */
  if (pthread_mutex_unlock(&key->lock) != 0)
    ABORT("pthread_mutex_unlock failed (remove_specific after fork)");
}

#  ifdef CAN_HANDLE_FORK
GC_INNER void
GC_update_specific_after_fork(tsd *key)
{
  unsigned hash_val = TS_HASH(GC_parent_pthread_self);
  tse *entry;

  GC_ASSERT(I_HOLD_LOCK());
#    ifdef LINT2
  pthread_mutex_lock(&key->lock);
#    endif
  entry = key->hash[hash_val];
  if (LIKELY(entry != NULL)) {
    GC_ASSERT(THREAD_EQUAL(entry->thread, GC_parent_pthread_self));
    GC_ASSERT(NULL == entry->next);
    /* Remove the `entry` from the table. */
    key->hash[hash_val] = NULL;
    entry->thread = pthread_self();
    /* Then put the `entry` back to the table (based on new hash value). */
    key->hash[TS_HASH(entry->thread)] = entry;
  }
#    ifdef LINT2
  (void)pthread_mutex_unlock(&key->lock);
#    endif
}
#  endif

GC_INNER void *
GC_slow_getspecific(tsd *key, size_t qtid, tse *volatile *cache_ptr)
{
  pthread_t self = pthread_self();
  tse *entry = key->hash[TS_HASH(self)];

  GC_ASSERT(qtid != INVALID_QTID);
  while (entry != NULL && !THREAD_EQUAL(entry->thread, self)) {
    entry = entry->next;
  }
  if (entry == NULL)
    return NULL;
  /*
   * Set the cache `entry`.  It is safe to do this asynchronously.
   * Either value is safe, though may produce spurious misses.
   * We are replacing one `qtid` with another one for the same thread.
   */
  AO_store(&entry->qtid, qtid);

  GC_cptr_store((volatile ptr_t *)cache_ptr, (ptr_t)entry);
  return TS_REVEAL_PTR(entry->value);
}

#  ifdef GC_ASSERTIONS
/*
 * Check that that all elements of the data structure associated with
 * `key` are marked.
 */
void
GC_check_tsd_marks(tsd *key)
{
  int i;
  tse *p;

  if (!GC_is_marked(GC_base(key))) {
    ABORT("Unmarked thread-specific-data table");
  }
  for (i = 0; i < TS_HASH_SIZE; ++i) {
    for (p = key->hash[i]; p != NULL; p = p->next) {
      if (!GC_is_marked(GC_base(p))) {
        ABORT_ARG1("Unmarked thread-specific-data entry", " at %p", (void *)p);
      }
    }
  }
  for (i = 0; i < TS_CACHE_SIZE; ++i) {
    p = key->cache[i];
    if (p != &invalid_tse && !GC_is_marked(GC_base(p))) {
      ABORT_ARG1("Unmarked cached thread-specific-data entry", " at %p",
                 (void *)p);
    }
  }
}
#  endif /* GC_ASSERTIONS */

#endif /* USE_CUSTOM_SPECIFIC */
