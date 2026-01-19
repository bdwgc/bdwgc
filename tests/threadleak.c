
#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#if !defined(GC_THREADS) && !defined(TEST_NO_THREADS)
#  define GC_THREADS
#endif

#undef GC_NO_THREAD_REDIRECTS
#include "gc/leak_detector.h"

#ifndef TEST_NO_THREADS
#  ifdef GC_PTHREADS
#    include <errno.h> /*< for `EAGAIN` */
#    include <pthread.h>
#  else
#    ifndef WIN32_LEAN_AND_MEAN
#      define WIN32_LEAN_AND_MEAN 1
#    endif
#    define NOSERVICE
#    include <windows.h>
#  endif
#endif

#include <stdio.h>
#include <stdlib.h>

#define N_TESTS 100

#define CHECK_OUT_OF_MEMORY(p)            \
  do {                                    \
    if (NULL == (p)) {                    \
      fprintf(stderr, "Out of memory\n"); \
      exit(69);                           \
    }                                     \
  } while (0)

#ifdef GC_PTHREADS
static void *
test(void *arg)
#else
static DWORD WINAPI
test(LPVOID arg)
#endif
{
  int *p[N_TESTS];
  int i;
  for (i = 0; i < N_TESTS; ++i) {
    p[i] = (int *)malloc(sizeof(int) + i);
    CHECK_OUT_OF_MEMORY(p[i]);
  }
  CHECK_LEAKS();
  for (i = 1; i < N_TESTS; ++i) {
    free(p[i]);
  }
#ifdef GC_PTHREADS
  return arg;
#else
  return (DWORD)(GC_uintptr_t)arg;
#endif
}

#ifndef TEST_NO_THREADS
#  ifndef NTHREADS
#    define NTHREADS 5
#  endif
#else
#  undef NTHREADS
#  define NTHREADS 0
#endif

int
main(void)
{
#if NTHREADS > 0
  int i, n;
#  ifdef GC_PTHREADS
  pthread_t t[NTHREADS];
#  else
  HANDLE t[NTHREADS];
#  endif
#endif

#ifndef NO_FIND_LEAK
  /* Just in case the code is compiled without `FIND_LEAK` macro defined. */
  GC_set_find_leak(1);
#endif
  GC_INIT();
  /* This is optional if `pthread_create()` is redirected. */
  GC_allow_register_threads();

#if NTHREADS > 0
  for (i = 0; i < NTHREADS; ++i) {
#  ifdef GC_PTHREADS
    int err = pthread_create(t + i, 0, test, 0);

    if (err != 0) {
      fprintf(stderr, "Thread #%d creation failed, errno= %d\n", i, err);
      if (i > 1 && EAGAIN == err)
        break;
      exit(2);
    }
#  else
    DWORD thread_id;

    t[i] = CreateThread(NULL, 0, test, 0, 0, &thread_id);
    if (NULL == t[i]) {
      fprintf(stderr, "Thread #%d creation failed, errcode= %d\n", i,
              (int)GetLastError());
      exit(2);
    }
#  endif
  }
  n = i;
  for (i = 0; i < n; ++i) {
    int err;

#  ifdef GC_PTHREADS
    err = pthread_join(t[i], 0);
#  else
    err = WaitForSingleObject(t[i], INFINITE) == WAIT_OBJECT_0
              ? 0
              : (int)GetLastError();
#  endif
    if (err != 0) {
      fprintf(stderr, "Thread #%d join failed, errcode= %d\n", i, err);
      exit(2);
    }
  }
#else
  (void)test(NULL);
#endif

  CHECK_LEAKS();
  CHECK_LEAKS();
  CHECK_LEAKS();
  printf("SUCCEEDED\n");
  return 0;
}
