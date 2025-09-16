/*
 * Copyright (c) 1994 by Xerox Corporation.  All rights reserved.
 * Copyright (c) 1996 by Silicon Graphics.  All rights reserved.
 * Copyright (c) 1998 by Fergus Henderson.  All rights reserved.
 * Copyright (c) 2000-2009 by Hewlett-Packard Development Company.
 * All rights reserved.
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

#ifndef GC_DARWIN_SEMAPHORE_H
#define GC_DARWIN_SEMAPHORE_H

#include "gc_priv.h"

/*
 * This is a very simple semaphore implementation based on `pthreads`.
 * It is not async-signal safe.  But this is not a problem because
 * signals are not used to suspend threads on the target.
 */

#if !defined(DARWIN) && !defined(GC_WIN32_THREADS) || !defined(GC_PTHREADS)
#  error darwin_semaphore.h included for improper target
#endif

#include <errno.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  pthread_mutex_t mutex;
  pthread_cond_t cond;
  int value;
} sem_t;

GC_INLINE int
sem_init(sem_t *sem, int pshared, int value)
{
  int err;

  if (UNLIKELY(pshared != 0)) {
    errno = EPERM; /*< unsupported */
    return -1;
  }
  sem->value = value;
  err = pthread_mutex_init(&sem->mutex, NULL);
  if (UNLIKELY(err != 0)) {
    errno = err;
    return -1;
  }
  err = pthread_cond_init(&sem->cond, NULL);
  if (UNLIKELY(err != 0)) {
    (void)pthread_mutex_destroy(&sem->mutex);
    errno = err;
    return -1;
  }
  return 0;
}

GC_INLINE int
sem_post(sem_t *sem)
{
  int err = pthread_mutex_lock(&sem->mutex);

  if (UNLIKELY(err != 0)) {
    errno = err;
    return -1;
  }
  sem->value++;
  err = pthread_cond_signal(&sem->cond);
  if (UNLIKELY(err != 0)) {
    (void)pthread_mutex_unlock(&sem->mutex);
    errno = err;
    return -1;
  }
  err = pthread_mutex_unlock(&sem->mutex);
  if (UNLIKELY(err != 0)) {
    errno = err;
    return -1;
  }
  return 0;
}

GC_INLINE int
sem_wait(sem_t *sem)
{
  int err = pthread_mutex_lock(&sem->mutex);

  if (UNLIKELY(err != 0)) {
    errno = err;
    return -1;
  }
  while (0 == sem->value) {
    err = pthread_cond_wait(&sem->cond, &sem->mutex);
    if (UNLIKELY(err != 0)) {
      (void)pthread_mutex_unlock(&sem->mutex);
      errno = err;
      return -1;
    }
  }
  sem->value--;
  err = pthread_mutex_unlock(&sem->mutex);
  if (UNLIKELY(err != 0)) {
    errno = err;
    return -1;
  }
  return 0;
}

GC_INLINE int
sem_destroy(sem_t *sem)
{
  int err = pthread_cond_destroy(&sem->cond);

  if (UNLIKELY(err != 0)) {
    errno = err;
    return -1;
  }
  err = pthread_mutex_destroy(&sem->mutex);
  if (UNLIKELY(err != 0)) {
    errno = err;
    return -1;
  }
  return 0;
}

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif
