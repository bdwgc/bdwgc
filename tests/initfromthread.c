/*
 * Copyright (c) 2011 Ludovic Courtes
 *
 * THIS MATERIAL IS PROVIDED AS IS, WITH ABSOLUTELY NO WARRANTY EXPRESSED
 * OR IMPLIED. ANY USE IS AT YOUR OWN RISK.
 *
 * Permission is hereby granted to use or copy this program
 * for any purpose, provided the above notices are retained on all copies.
 * Permission to modify the code and to distribute modified code is granted,
 * provided the above notices are retained, and a notice that the code was
 * modified is included with the above copyright notice.
 */

/*
 * Make sure `GC_INIT()` can be called from threads other than the
 * initial thread.
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#ifndef GC_THREADS
#  define GC_THREADS
#endif

/* Do not redirect thread creation and join calls. */
#define GC_NO_THREAD_REDIRECTS 1

#include "gc.h"

#ifdef GC_PTHREADS
#  include <pthread.h>
#else
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN 1
#  endif
#  define NOSERVICE
#  include <windows.h>
#endif /* !GC_PTHREADS */

#include <stdio.h>
#include <stdlib.h>

#define CHECK_OUT_OF_MEMORY(p)            \
  do {                                    \
    if (NULL == (p)) {                    \
      fprintf(stderr, "Out of memory\n"); \
      exit(69);                           \
    }                                     \
  } while (0)

#ifdef GC_PTHREADS
static void *
thread(void *arg)
#else
static DWORD WINAPI
thread(LPVOID arg)
#endif
{
  GC_INIT();
  CHECK_OUT_OF_MEMORY(GC_MALLOC(123));
  CHECK_OUT_OF_MEMORY(GC_MALLOC(12345));
#ifdef GC_PTHREADS
  return arg;
#else
  return (DWORD)(GC_uintptr_t)arg;
#endif
}

#include "private/gcconfig.h"

int
main(void)
{
#ifdef GC_PTHREADS
  int err;
  pthread_t t;

#  ifdef LINT2
  /* Explicitly initialize t to some value. */
  t = pthread_self();
#  endif
#else
  HANDLE t;
  DWORD thread_id;
#endif
#if !(defined(ANY_MSWIN) || defined(BEOS)                              \
      || (defined(DARWIN) && !defined(NO_PTHREAD_GET_STACKADDR_NP))    \
      || ((defined(FREEBSD) || defined(LINUX) || defined(NETBSD)       \
           || defined(HOST_ANDROID))                                   \
          && !defined(NO_PTHREAD_ATTR_GET_NP)                          \
          && !defined(NO_PTHREAD_GETATTR_NP))                          \
      || (defined(SOLARIS) && !defined(_STRICT_STDC))                  \
      || ((!defined(SPECIFIC_MAIN_STACKBOTTOM) || defined(HEURISTIC1)) \
          && !defined(STACKBOTTOM)))
  /* `GC_INIT()` must be called from main thread only. */
  GC_INIT();
#endif

  /* Linking fails if no multi-threading support. */
  (void)GC_get_suspend_signal();

  if (GC_get_find_leak())
    printf("This test program is not designed for leak detection mode\n");
#ifdef GC_PTHREADS
  err = pthread_create(&t, NULL, thread, NULL);
  if (err != 0) {
    fprintf(stderr, "Thread #0 creation failed, errno= %d\n", err);
    return 1;
  }
  err = pthread_join(t, NULL);
  if (err != 0) {
    fprintf(stderr, "Thread #0 join failed, errno= %d\n", err);
    return 1;
  }
#else
  t = CreateThread(NULL, 0, thread, 0, 0, &thread_id);
  if (t == NULL) {
    fprintf(stderr, "Thread #0 creation failed, errcode= %d\n",
            (int)GetLastError());
    return 1;
  }
  if (WaitForSingleObject(t, INFINITE) != WAIT_OBJECT_0) {
    fprintf(stderr, "Thread #0 join failed, errcode= %d\n",
            (int)GetLastError());
    CloseHandle(t);
    return 1;
  }
  CloseHandle(t);
#endif
  printf("SUCCEEDED\n");
  return 0;
}
