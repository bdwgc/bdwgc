/*
 * Copyright 1988, 1989 Hans-J. Boehm, Alan J. Demers
 * Copyright (c) 1991-1994 by Xerox Corporation.  All rights reserved.
 * Copyright (c) 1996-1999 by Silicon Graphics.  All rights reserved.
 * Copyright (c) 1999 by Hewlett-Packard Company. All rights reserved.
 * Copyright (c) 2008-2022 Ivan Maidanski
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

#ifndef GC_LOCKS_H
#define GC_LOCKS_H

#if !defined(GC_PRIVATE_H) && !defined(CPPCHECK)
#  error gc_locks.h should be included from gc_priv.h
#endif

/*
 * Mutual exclusion between allocator/collector routines.  Needed if
 * there is more than one allocator thread.  Note that `I_HOLD_LOCK`,
 * `I_DONT_HOLD_LOCK` and `I_HOLD_READER_LOCK` macros are used only
 * positively in assertions, and may return `TRUE` in the "do not know"
 * case.
 */

#ifdef THREADS

EXTERN_C_BEGIN

#  if defined(NN_PLATFORM_CTR) || defined(NINTENDO_SWITCH)
extern void GC_lock(void);
extern void GC_unlock(void);
#    define UNCOND_LOCK() GC_lock()
#    define UNCOND_UNLOCK() GC_unlock()
#    ifdef GC_ASSERTIONS
#      define SET_LOCK_HOLDER() (void)0
#    endif
#  endif

#  if (!defined(AO_HAVE_test_and_set_acquire) || defined(GC_WIN32_THREADS) \
       || defined(LINT2) || defined(RTEMS) || defined(SN_TARGET_PS3)       \
       || defined(BASE_ATOMIC_OPS_EMULATED) || defined(USE_RWLOCK))        \
      && defined(GC_PTHREADS)
#    define USE_PTHREAD_LOCKS
#    undef USE_SPIN_LOCK
#    if (defined(GC_WIN32_THREADS) || defined(LINT2) || defined(USE_RWLOCK)) \
        && !defined(NO_PTHREAD_TRYLOCK)
/*
 * `pthread_mutex_trylock` may not win in `GC_lock` on Win32, due to
 * built-in support for spinning first?
 */
#      define NO_PTHREAD_TRYLOCK
#    endif
#  endif

#  if defined(GC_WIN32_THREADS) && !defined(USE_PTHREAD_LOCKS) \
      || defined(GC_PTHREADS)
/* A value which is not equal to `NUMERIC_THREAD_ID(id)` for any thread. */
#    define NO_THREAD ((unsigned long)(-1L))
#    ifdef GC_ASSERTIONS
GC_EXTERN unsigned long GC_lock_holder;
#      define UNSET_LOCK_HOLDER() (void)(GC_lock_holder = NO_THREAD)
#    endif
#  endif /* GC_WIN32_THREADS || GC_PTHREADS */

#  if defined(GC_WIN32_THREADS) && !defined(USE_PTHREAD_LOCKS)
#    ifdef USE_RWLOCK
GC_EXTERN SRWLOCK GC_allocate_ml;
#    else
GC_EXTERN CRITICAL_SECTION GC_allocate_ml;
#    endif
#    ifdef GC_ASSERTIONS
#      define SET_LOCK_HOLDER() (void)(GC_lock_holder = GetCurrentThreadId())
#      define I_HOLD_LOCK() \
        (!GC_need_to_lock || GC_lock_holder == GetCurrentThreadId())
#      ifdef THREAD_SANITIZER
#        define I_DONT_HOLD_LOCK() TRUE /*< conservatively say yes */
#      else
#        define I_DONT_HOLD_LOCK() \
          (!GC_need_to_lock || GC_lock_holder != GetCurrentThreadId())
#      endif
#      ifdef USE_RWLOCK
#        define UNCOND_READER_LOCK()               \
          {                                        \
            GC_ASSERT(I_DONT_HOLD_LOCK());         \
            AcquireSRWLockShared(&GC_allocate_ml); \
          }
#        define UNCOND_READER_UNLOCK()             \
          {                                        \
            GC_ASSERT(I_DONT_HOLD_LOCK());         \
            ReleaseSRWLockShared(&GC_allocate_ml); \
          }
#        define UNCOND_LOCK()                         \
          {                                           \
            GC_ASSERT(I_DONT_HOLD_LOCK());            \
            AcquireSRWLockExclusive(&GC_allocate_ml); \
            SET_LOCK_HOLDER();                        \
          }
#        define UNCOND_UNLOCK()                       \
          {                                           \
            GC_ASSERT(I_HOLD_LOCK());                 \
            UNSET_LOCK_HOLDER();                      \
            ReleaseSRWLockExclusive(&GC_allocate_ml); \
          }
#      else
#        define UNCOND_LOCK()                      \
          {                                        \
            GC_ASSERT(I_DONT_HOLD_LOCK());         \
            EnterCriticalSection(&GC_allocate_ml); \
            SET_LOCK_HOLDER();                     \
          }
#        define UNCOND_UNLOCK()                    \
          {                                        \
            GC_ASSERT(I_HOLD_LOCK());              \
            UNSET_LOCK_HOLDER();                   \
            LeaveCriticalSection(&GC_allocate_ml); \
          }
#      endif
#    else
#      ifdef USE_RWLOCK
#        define UNCOND_READER_LOCK() AcquireSRWLockShared(&GC_allocate_ml)
#        define UNCOND_READER_UNLOCK() ReleaseSRWLockShared(&GC_allocate_ml)
#        define UNCOND_LOCK() AcquireSRWLockExclusive(&GC_allocate_ml)
#        define UNCOND_UNLOCK() ReleaseSRWLockExclusive(&GC_allocate_ml)
#      else
#        define UNCOND_LOCK() EnterCriticalSection(&GC_allocate_ml)
#        define UNCOND_UNLOCK() LeaveCriticalSection(&GC_allocate_ml)
#      endif
#    endif /* !GC_ASSERTIONS */
#  elif defined(GC_PTHREADS)
EXTERN_C_END
#    include <pthread.h>
EXTERN_C_BEGIN
/*
 * POSIX allows `pthread_t` to be a structure type, though it rarely is.
 * Unfortunately, we need to use a `pthread_t` to index a data structure.
 * It also helps if comparisons do not involve a function call.
 * Hence we introduce platform-dependent macros to compare `pthread_t` ids
 * and to map them to integers (of `unsigned long` type).  This mapping
 * does not need to result in different values for each thread, though
 * that should be true as much as possible.
 */
#    if !defined(GC_WIN32_PTHREADS)
#      define NUMERIC_THREAD_ID(id) ((unsigned long)(GC_uintptr_t)(id))
#      define THREAD_EQUAL(id1, id2) ((id1) == (id2))
#      define NUMERIC_THREAD_ID_UNIQUE
#    elif defined(__WINPTHREADS_VERSION_MAJOR) /*< winpthreads */
#      define NUMERIC_THREAD_ID(id) ((unsigned long)(id))
#      define THREAD_EQUAL(id1, id2) ((id1) == (id2))
/* `NUMERIC_THREAD_ID()` is 32-bit and, thus, not unique on Win64. */
#      ifndef _WIN64
#        define NUMERIC_THREAD_ID_UNIQUE
#      endif
#    else /* pthreads-win32 */
#      define NUMERIC_THREAD_ID(id) ((unsigned long)(word)(id.p))
/*
 * The platform on which `pthread_t` is a structure.
 * Using documented internal details of pthreads-win32 library.
 * Faster than `pthread_equal()`.  Should not change with the
 * future versions of pthreads-win32 library.
 */
#      define THREAD_EQUAL(id1, id2) (id1.p == id2.p && id1.x == id2.x)
/*
 * Generic definitions based on `pthread_equal()` always work but will
 * result in poor performance (as `NUMERIC_THREAD_ID()` might give
 * a constant value) and weak assertion checking.
 */
#      undef NUMERIC_THREAD_ID_UNIQUE
#    endif

#    ifdef SN_TARGET_PSP2
EXTERN_C_END
#      include "psp2-support.h"
EXTERN_C_BEGIN
GC_EXTERN WapiMutex GC_allocate_ml_PSP2;
#      define UNCOND_LOCK()                           \
        {                                             \
          int res;                                    \
          GC_ASSERT(I_DONT_HOLD_LOCK());              \
          res = PSP2_MutexLock(&GC_allocate_ml_PSP2); \
          GC_ASSERT(0 == res);                        \
          (void)res;                                  \
          SET_LOCK_HOLDER();                          \
        }
#      define UNCOND_UNLOCK()                           \
        {                                               \
          int res;                                      \
          GC_ASSERT(I_HOLD_LOCK());                     \
          UNSET_LOCK_HOLDER();                          \
          res = PSP2_MutexUnlock(&GC_allocate_ml_PSP2); \
          GC_ASSERT(0 == res);                          \
          (void)res;                                    \
        }

#    elif (!defined(THREAD_LOCAL_ALLOC) || defined(USE_SPIN_LOCK))   \
        && !defined(USE_PTHREAD_LOCKS) && !defined(THREAD_SANITIZER) \
        && !defined(USE_RWLOCK)
/*
 * In the `THREAD_LOCAL_ALLOC` case, the allocator lock tends to
 * be held for long periods, if it is held at all.
 * Thus spinning and sleeping for fixed periods are likely to result
 * in significant wasted time.  We thus rely mostly on queued locks.
 */
#      undef USE_SPIN_LOCK
#      define USE_SPIN_LOCK
GC_INNER void GC_lock(void);
#      ifdef GC_ASSERTIONS
#        define UNCOND_LOCK()                                            \
          {                                                              \
            GC_ASSERT(I_DONT_HOLD_LOCK());                               \
            if (AO_test_and_set_acquire(&GC_allocate_lock) == AO_TS_SET) \
              GC_lock();                                                 \
            SET_LOCK_HOLDER();                                           \
          }
#        define UNCOND_UNLOCK()          \
          {                              \
            GC_ASSERT(I_HOLD_LOCK());    \
            UNSET_LOCK_HOLDER();         \
            AO_CLEAR(&GC_allocate_lock); \
          }
#      else
#        define UNCOND_LOCK()                                            \
          {                                                              \
            if (AO_test_and_set_acquire(&GC_allocate_lock) == AO_TS_SET) \
              GC_lock();                                                 \
          }
#        define UNCOND_UNLOCK() AO_CLEAR(&GC_allocate_lock)
#      endif /* !GC_ASSERTIONS */
#    else
#      ifndef USE_PTHREAD_LOCKS
#        define USE_PTHREAD_LOCKS
#      endif
#    endif /* THREAD_LOCAL_ALLOC || USE_PTHREAD_LOCKS */
#    ifdef USE_PTHREAD_LOCKS
EXTERN_C_END
#      include <pthread.h>
EXTERN_C_BEGIN
#      ifdef GC_ASSERTIONS
GC_INNER void GC_lock(void);
#        define UNCOND_LOCK()              \
          {                                \
            GC_ASSERT(I_DONT_HOLD_LOCK()); \
            GC_lock();                     \
            SET_LOCK_HOLDER();             \
          }
#      endif
#      ifdef USE_RWLOCK
GC_EXTERN pthread_rwlock_t GC_allocate_ml;
#        ifdef GC_ASSERTIONS
#          define UNCOND_READER_LOCK()                      \
            {                                               \
              GC_ASSERT(I_DONT_HOLD_LOCK());                \
              (void)pthread_rwlock_rdlock(&GC_allocate_ml); \
            }
#          define UNCOND_READER_UNLOCK()                    \
            {                                               \
              GC_ASSERT(I_DONT_HOLD_LOCK());                \
              (void)pthread_rwlock_unlock(&GC_allocate_ml); \
            }
#          define UNCOND_UNLOCK()                           \
            {                                               \
              GC_ASSERT(I_HOLD_LOCK());                     \
              UNSET_LOCK_HOLDER();                          \
              (void)pthread_rwlock_unlock(&GC_allocate_ml); \
            }
#        else
#          define UNCOND_READER_LOCK() \
            (void)pthread_rwlock_rdlock(&GC_allocate_ml)
#          define UNCOND_READER_UNLOCK() UNCOND_UNLOCK()
#          define UNCOND_LOCK() (void)pthread_rwlock_wrlock(&GC_allocate_ml)
#          define UNCOND_UNLOCK() (void)pthread_rwlock_unlock(&GC_allocate_ml)
#        endif /* !GC_ASSERTIONS */
#      else
GC_EXTERN pthread_mutex_t GC_allocate_ml;
#        ifdef GC_ASSERTIONS
#          define UNCOND_UNLOCK()                    \
            {                                        \
              GC_ASSERT(I_HOLD_LOCK());              \
              UNSET_LOCK_HOLDER();                   \
              pthread_mutex_unlock(&GC_allocate_ml); \
            }
#        else
#          if defined(NO_PTHREAD_TRYLOCK)
#            define UNCOND_LOCK() pthread_mutex_lock(&GC_allocate_ml)
#          else
GC_INNER void GC_lock(void);
#            define UNCOND_LOCK()                                \
              {                                                  \
                if (pthread_mutex_trylock(&GC_allocate_ml) != 0) \
                  GC_lock();                                     \
              }
#          endif
#          define UNCOND_UNLOCK() pthread_mutex_unlock(&GC_allocate_ml)
#        endif /* !GC_ASSERTIONS */
#      endif
#    endif /* USE_PTHREAD_LOCKS */
#    ifdef GC_ASSERTIONS
/* The allocator lock holder. */
#      define SET_LOCK_HOLDER() \
        (void)(GC_lock_holder = NUMERIC_THREAD_ID(pthread_self()))
#      define I_HOLD_LOCK() \
        (!GC_need_to_lock   \
         || GC_lock_holder == NUMERIC_THREAD_ID(pthread_self()))
#      if !defined(NUMERIC_THREAD_ID_UNIQUE) || defined(THREAD_SANITIZER)
#        define I_DONT_HOLD_LOCK() TRUE /*< conservatively say yes */
#      else
#        define I_DONT_HOLD_LOCK() \
          (!GC_need_to_lock        \
           || GC_lock_holder != NUMERIC_THREAD_ID(pthread_self()))
#      endif
#    endif /* GC_ASSERTIONS */
#    if !defined(GC_WIN32_THREADS)
/*
 * A hint that we are in the collector and holding the allocator lock
 * for an extended period.
 */
GC_EXTERN volatile unsigned char GC_collecting;

#      ifdef AO_HAVE_char_store
#        if defined(GC_ASSERTIONS) && defined(AO_HAVE_char_fetch_and_add1)
/* Ensure `ENTER_GC()` is not used recursively. */
#          define ENTER_GC() GC_ASSERT(!AO_char_fetch_and_add1(&GC_collecting))
#        else
#          define ENTER_GC() AO_char_store(&GC_collecting, TRUE)
#        endif
#        define EXIT_GC() AO_char_store(&GC_collecting, FALSE)
#      else
#        define ENTER_GC() (void)(GC_collecting = TRUE)
#        define EXIT_GC() (void)(GC_collecting = FALSE)
#      endif
#    endif
#  endif /* GC_PTHREADS */
#  if defined(GC_ALWAYS_MULTITHREADED) \
      && (defined(USE_PTHREAD_LOCKS) || defined(USE_SPIN_LOCK))
#    define GC_need_to_lock TRUE
#    define set_need_to_lock() (void)0
#  else
#    if defined(GC_ALWAYS_MULTITHREADED) && !defined(CPPCHECK)
#      error Runtime initialization of the allocator lock is needed!
#    endif
#    undef GC_ALWAYS_MULTITHREADED
#    ifdef THREAD_SANITIZER
/*
 * To workaround TSan false positive (e.g., when `GC_pthread_create()` is
 * called from multiple threads in parallel), do not set `GC_need_to_lock`
 * if it is already set.
 */
#      define set_need_to_lock()                     \
        (void)(*(GC_bool volatile *)&GC_need_to_lock \
                   ? FALSE                           \
                   : (GC_need_to_lock = TRUE))
#    else
#      define set_need_to_lock() (void)(GC_need_to_lock = TRUE)
/* We are multi-threaded now. */
#    endif
#  endif

EXTERN_C_END

#else /* !THREADS */
#  define LOCK() (void)0
#  define UNLOCK() (void)0
#  ifdef GC_ASSERTIONS
/*
 * `I_HOLD_LOCK()` and `I_DONT_HOLD_LOCK()` are used only in positive
 * assertions or to test whether we still need to acquire the allocator
 * lock; `TRUE` works in either case.
 */
#    define I_HOLD_LOCK() TRUE
#    define I_DONT_HOLD_LOCK() TRUE
#  endif
#endif /* !THREADS */

#if defined(UNCOND_LOCK) && !defined(LOCK)
#  if (defined(LINT2) && defined(USE_PTHREAD_LOCKS)) \
      || defined(GC_ALWAYS_MULTITHREADED)
/*
 * Instruct code analysis tools not to care about `GC_need_to_lock`
 * influence to `LOCK`/`UNLOCK` semantic.
 */
#    define LOCK() UNCOND_LOCK()
#    define UNLOCK() UNCOND_UNLOCK()
#    ifdef UNCOND_READER_LOCK
#      define READER_LOCK() UNCOND_READER_LOCK()
#      define READER_UNLOCK() UNCOND_READER_UNLOCK()
#    endif
#  else
/* At least two thread running; need to lock. */
#    define LOCK()           \
      do {                   \
        if (GC_need_to_lock) \
          UNCOND_LOCK();     \
      } while (0)
#    define UNLOCK()         \
      do {                   \
        if (GC_need_to_lock) \
          UNCOND_UNLOCK();   \
      } while (0)
#    ifdef UNCOND_READER_LOCK
#      define READER_LOCK()       \
        do {                      \
          if (GC_need_to_lock)    \
            UNCOND_READER_LOCK(); \
        } while (0)
#      define READER_UNLOCK()       \
        do {                        \
          if (GC_need_to_lock)      \
            UNCOND_READER_UNLOCK(); \
        } while (0)
#    endif
#  endif
#endif /* UNCOND_LOCK && !LOCK */

#ifdef READER_LOCK
#  define HAS_REAL_READER_LOCK
/* TODO: Implement I_HOLD_READER_LOCK, conservatively say yes for now. */
#  define I_HOLD_READER_LOCK() TRUE
#else
#  define READER_LOCK() LOCK()
#  define READER_UNLOCK() UNLOCK()
#  ifdef GC_ASSERTIONS
/*
 * A macro to check that the allocator lock is held at least in the
 * reader mode.
 */
#    define I_HOLD_READER_LOCK() I_HOLD_LOCK()
#  endif
#endif /* !READER_LOCK */

/*
 * A variant of `READER_UNLOCK()` which ensures that data written
 * before the unlock will be visible to the thread which acquires the
 * allocator lock in the exclusive mode.  But according to some `rwlock`
 * documentation: writers synchronize with prior writers and readers.
 */
#define READER_UNLOCK_RELEASE() READER_UNLOCK()

#ifndef ENTER_GC
#  define ENTER_GC()
#  define EXIT_GC()
#endif

#endif /* GC_LOCKS_H */
