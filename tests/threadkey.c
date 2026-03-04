
#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#ifndef GC_THREADS
#  define GC_THREADS
#endif

#define GC_NO_THREAD_REDIRECTS 1

#include "gc.h"

#include <stdio.h>
#include <stdlib.h>

#define TEST_ASSERT(e)                                                    \
  if (!(e)) {                                                             \
    fprintf(stderr, "Assertion failure: %s:%d, %s\n", __FILE__, __LINE__, \
            #e);                                                          \
    exit(1);                                                              \
  }

#if (!defined(GC_PTHREADS) || defined(GC_SOLARIS_THREADS) \
     || defined(__native_client__))                       \
    && !defined(SKIP_THREADKEY_TEST)
/*
 * FIXME: Skip this test on Solaris for now.  The test may fail on other
 * targets as well.  Currently, tested only on Linux, Cygwin and Darwin.
 */
#  define SKIP_THREADKEY_TEST
#endif

#ifdef SKIP_THREADKEY_TEST

int
main(void)
{
  printf("test skipped\n");
  return 0;
}

#else

#  include <errno.h> /*< for `EAGAIN` */
#  include <pthread.h>

pthread_key_t key;

/* TODO: Use `pthread_once_t` on Solaris. */
pthread_once_t key_once = PTHREAD_ONCE_INIT;

static void *
entry(void *arg)
{
  pthread_setspecific(key,
                      (void *)GC_HIDE_NZ_POINTER(GC_STRDUP("hello, world")));
  return arg;
}

static void *GC_CALLBACK
on_thread_exit_inner(struct GC_stack_base *sb, void *arg)
{
  int res = GC_register_my_thread(sb);
  pthread_t t;
  int creation_res; /*< to suppress a warning about unchecked result */
  pthread_attr_t attr;

  TEST_ASSERT(pthread_attr_init(&attr) == 0);
  TEST_ASSERT(pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED)
              == 0);
  creation_res = GC_pthread_create(&t, &attr, entry, NULL);
  (void)pthread_attr_destroy(&attr);
  if (res == GC_SUCCESS)
    GC_unregister_my_thread();

#  if defined(CPPCHECK)
  GC_noop1_ptr(sb);
  GC_noop1_ptr(arg);
#  endif
  return arg ? (void *)(GC_uintptr_t)creation_res : 0;
}

static void
on_thread_exit(void *v)
{
  (void)GC_call_with_stack_base(on_thread_exit_inner, v);
}

static void
make_key(void)
{
  pthread_key_create(&key, on_thread_exit);
}

#  ifndef NTHREADS
#    define NTHREADS 5
#  endif

/* Number of threads to create. */
#  define NTHREADS_INNER (NTHREADS * 6)

int
main(void)
{
  int i;

  GC_INIT();
  if (GC_get_find_leak())
    printf("This test program is not designed for leak detection mode\n");
  /* TODO: Call `make_key()` instead on Solaris. */
  pthread_once(&key_once, make_key);

  for (i = 0; i < NTHREADS_INNER; i++) {
    pthread_t t;
    void *res;
    int err = GC_pthread_create(&t, NULL, entry, NULL);

    if (err != 0) {
      fprintf(stderr, "Thread #%d creation failed, errno= %d\n", i, err);
      if (i > 0 && EAGAIN == err)
        break;
      exit(69);
    }

    if ((i & 1) != 0) {
      TEST_ASSERT(GC_pthread_join(t, &res) == 0);
    } else {
      TEST_ASSERT(GC_pthread_detach(t) == 0);
    }
  }
  printf("SUCCEEDED\n");
  return 0;
}

#endif /* !SKIP_THREADKEY_TEST */
