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
 * This is a custom implementation of a subset of the
 * `pthread_getspecific`/`pthread_setspecific` interface.  This appears
 * to outperform the standard LinuxThreads one by a significant margin.
 * The major restriction is that each thread may only make a single
 * `pthread_setspecific()` call on a single key.  (The current data
 * structure does not really require that.  This restriction should be
 * easily removable.)  We do not currently support the destruction
 * functions, though that could be done.  We also currently assume that
 * only one `pthread_setspecific()` call can be executed at a time,
 * though that assumption would be easy to remove by adding a lock.
 */

#ifndef GC_SPECIFIC_H
#define GC_SPECIFIC_H

#if !defined(GC_THREAD_LOCAL_ALLOC_H)
#  error specific.h should be included from thread_local_alloc.h
#endif

#include <errno.h>

EXTERN_C_BEGIN

/*
 * Called during key creation or by `GC_setspecific()`.  For the GC we
 * already hold the allocator lock.  Currently allocated objects leak
 * on thread exit.  It is hard to avoid, but OK if we allocate garbage
 * collected memory.
 */
#define MALLOC_CLEAR(n) GC_INTERNAL_MALLOC(n, NORMAL)

#define TS_CACHE_SIZE 1024
#define TS_CACHE_HASH(n) ((((n) >> 8) ^ (n)) & (TS_CACHE_SIZE - 1))

#define TS_HASH_SIZE 1024
#define TS_HASH(p) ((unsigned)((ADDR(p) >> 8) ^ ADDR(p)) & (TS_HASH_SIZE - 1))

#ifdef GC_ASSERTIONS
/*
 * Thread-local storage is not guaranteed to be scanned by the collector.
 * We hide values stored in "specific" entries for a test purpose.
 */
typedef GC_hidden_pointer ts_entry_value_t;
#  define TS_HIDE_VALUE(p) GC_HIDE_NZ_POINTER(p)
#  define TS_REVEAL_PTR(p) GC_REVEAL_NZ_POINTER(p)
#else
typedef void *ts_entry_value_t;
#  define TS_HIDE_VALUE(p) (p)
#  define TS_REVEAL_PTR(p) (p)
#endif

/*
 * An entry describing a thread-specific value for a given thread.
 * All such accessible structures preserve the invariant that if
 * either thread is a valid `pthreads` id or `qtid` is a valid
 * "quick thread identifier" for a thread, then value holds the
 * corresponding thread specific value.  This invariant must be
 * preserved at all times, since asynchronous reads are allowed.
 */
typedef struct thread_specific_entry {
  volatile AO_t qtid; /*< a "quick thread identifier", only for cache */
  ts_entry_value_t value;
  struct thread_specific_entry *next;
  pthread_t thread;
} tse;

/*
 * We represent each thread-specific datum as two tables.  The first is
 * a cache, indexed by a "quick thread identifier".  The latter one is
 * an easy-to-compute value, which is guaranteed to determine the thread,
 * though a thread may correspond to more than one value.  We typically
 * use the address of a page in the stack.  The second is a hash table,
 * indexed by `pthread_self()`.  It is used only as a backup.
 */

/*
 * Return the "quick thread identifier".  The default variant.
 * Assumes the page size, or at least thread stack separation, is at
 * least 4 KB.  Must be defined so that it never returns 0.  (Page zero
 * cannot really be part of any stack, since that would make 0 a valid
 * stack pointer.)
 */
#define ts_quick_thread_id() ((size_t)(ADDR(GC_approx_sp()) >> 12))

#define INVALID_QTID ((size_t)0)
#define INVALID_THREADID ((pthread_t)0)

typedef struct thread_specific_data {
  tse *volatile cache[TS_CACHE_SIZE]; /*< a faster index to the hash table */
  tse *hash[TS_HASH_SIZE];
  pthread_mutex_t lock;
} tsd;

typedef tsd *GC_key_t;

#define GC_key_create(key, d) GC_key_create_inner(key)
GC_INNER int GC_key_create_inner(tsd **key_ptr);

/*
 * Set the thread-local value associated with `key`.
 * Should not be used to overwrite a previously set value.
 */
GC_INNER int GC_setspecific(tsd *key, void *value);

/* This function is called on thread exit. */
#define GC_remove_specific(key) \
  GC_remove_specific_after_fork(key, pthread_self())

/*
 * Remove thread-specific data for a given thread.  This function is called
 * at `fork` from the child process for all threads except for the survived
 * one.
 */
GC_INNER void GC_remove_specific_after_fork(tsd *key, pthread_t t);

#ifdef CAN_HANDLE_FORK
/*
 * Update thread-specific data for the survived thread of the child process.
 * Should be called once after removing thread-specific data for other
 * threads.
 */
GC_INNER void GC_update_specific_after_fork(tsd *key);
#endif

/*
 * An internal variant of `GC_getspecific` that assumes a cache miss.
 * Note that even the slow path does not lock.
 */
GC_INNER void *GC_slow_getspecific(tsd *key, size_t qtid,
                                   tse *volatile *entry_ptr);

GC_INLINE void *
GC_getspecific(tsd *key)
{
  size_t qtid = ts_quick_thread_id();
  tse *volatile *entry_ptr = &key->cache[TS_CACHE_HASH(qtid)];
  const tse *entry = *entry_ptr; /*< must be loaded only once */

  GC_ASSERT(qtid != INVALID_QTID);
  if (LIKELY(entry->qtid == qtid)) {
    GC_ASSERT(entry->thread == pthread_self());
    return TS_REVEAL_PTR(entry->value);
  }

  return GC_slow_getspecific(key, qtid, entry_ptr);
}

EXTERN_C_END

#endif /* GC_SPECIFIC_H */
