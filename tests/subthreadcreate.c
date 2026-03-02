
#ifdef HAVE_CONFIG_H
/* For `GC_THREADS` and `PARALLEL_MARK` macros. */
#  include "config.h"
#endif

#ifdef GC_THREADS
#  undef GC_NO_THREAD_REDIRECTS
#  include "gc.h"

#  ifdef PARALLEL_MARK
#    define AO_REQUIRE_CAS
#    if !defined(__GNUC__) && !defined(AO_ASSUME_WINDOWS98)
#      define AO_ASSUME_WINDOWS98
#    endif
#  endif
#  include "private/gc_atomic_ops.h"
#endif /* GC_THREADS */

#include <stdio.h>

#ifdef AO_HAVE_fetch_and_add1

#  ifdef GC_PTHREADS
#    include <errno.h> /*< for `EAGAIN` */
#    include <pthread.h>
#  else
#    ifndef WIN32_LEAN_AND_MEAN
#      define WIN32_LEAN_AND_MEAN 1
#    endif
#    define NOSERVICE
#    include <windows.h>
#  endif /* !GC_PTHREADS */

#  include <stdlib.h>

#  ifndef NTHREADS
#    define NTHREADS 5
#  endif

/* Number of threads to create. */
#  define NTHREADS_INNER (NTHREADS * 6)

#  ifndef MAX_SUBTHREAD_DEPTH
#    define MAX_ALIVE_THREAD_COUNT 55
#    define MAX_SUBTHREAD_DEPTH 7
#    define MAX_SUBTHREAD_COUNT 200
#  endif

#  ifndef DECAY_NUMER
#    define DECAY_NUMER 15
#    define DECAY_DENOM 16
#  endif

volatile AO_t thread_created_cnt = 0;
volatile AO_t thread_ended_cnt = 0;

#  ifdef GC_PTHREADS
static void *
entry(void *arg)
#  else
static DWORD WINAPI
entry(LPVOID arg)
#  endif
{
  int thread_num = (int)AO_fetch_and_add1(&thread_created_cnt);
  GC_word my_depth = (GC_word)(GC_uintptr_t)arg + 1;

  if (my_depth <= MAX_SUBTHREAD_DEPTH && thread_num < MAX_SUBTHREAD_COUNT
      && (thread_num % DECAY_DENOM) < DECAY_NUMER
      && thread_num - (int)AO_load(&thread_ended_cnt)
             <= MAX_ALIVE_THREAD_COUNT) {
#  ifdef GC_PTHREADS
    int err;
    pthread_t th;

    err = pthread_create(&th, NULL, entry, (void *)(GC_uintptr_t)my_depth);
    if (err != 0) {
      fprintf(stderr,
              "Thread #%d creation failed from other thread, errno= %d\n",
              thread_num, err);
      if (err != EAGAIN)
        exit(69);
    } else {
      err = pthread_detach(th);
      if (err != 0) {
        fprintf(stderr, "Thread #%d detach failed, errno= %d\n", thread_num,
                err);
        exit(2);
      }
    }
#  else
    HANDLE th;
    DWORD thread_id;

    th = CreateThread(NULL, 0, entry, (LPVOID)my_depth, 0, &thread_id);
    if (th == NULL) {
      fprintf(stderr, "Thread #%d creation failed, errcode= %d\n", thread_num,
              (int)GetLastError());
      exit(69);
    }
    CloseHandle(th);
#  endif
  }

  (void)AO_fetch_and_add1(&thread_ended_cnt);
  return 0;
}

int
main(void)
{
#  if NTHREADS > 0
  int i, n;
#    ifdef GC_PTHREADS
  int err;
  pthread_t th[NTHREADS_INNER];
#    else
  HANDLE th[NTHREADS_INNER];
#    endif
  int th_nums[NTHREADS_INNER];

  GC_INIT();
  for (i = 0; i < NTHREADS_INNER; ++i) {
    th_nums[i] = (int)AO_fetch_and_add1(&thread_created_cnt);
#    ifdef GC_PTHREADS
    err = pthread_create(&th[i], NULL, entry, 0);
    if (err != 0) {
      fprintf(stderr, "Thread #%d creation failed, errno= %d\n", th_nums[i],
              err);
      if (i > 0 && EAGAIN == err)
        break;
      exit(69);
    }
#    else
    {
      DWORD thread_id;

      th[i] = CreateThread(NULL, 0, entry, 0, 0, &thread_id);
    }
    if (th[i] == NULL) {
      fprintf(stderr, "Thread #%d creation failed, errcode= %d\n", th_nums[i],
              (int)GetLastError());
      exit(69);
    }
#    endif
  }
  n = i;
  for (i = 0; i < n; ++i) {
#    ifdef GC_PTHREADS
    void *res;

    err = pthread_join(th[i], &res);
    if (err != 0) {
      fprintf(stderr, "Thread #%d join failed, errno= %d\n", th_nums[i], err);
      exit(2);
    }
#    else
    if (WaitForSingleObject(th[i], INFINITE) != WAIT_OBJECT_0) {
      fprintf(stderr, "Thread #%d join failed, errcode= %d\n", th_nums[i],
              (int)GetLastError());
      CloseHandle(th[i]);
      exit(2);
    }
    CloseHandle(th[i]);
#    endif
  }
#  else
  (void)entry(NULL);
#  endif
  printf("Created %d threads (%d ended)\n", (int)AO_load(&thread_created_cnt),
         (int)AO_load(&thread_ended_cnt));
  return 0;
}

#else

int
main(void)
{
  printf("test skipped\n");
  return 0;
}

#endif /* !AO_HAVE_fetch_and_add1 */
