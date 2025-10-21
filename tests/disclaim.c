/*
 * Copyright (c) 2011 by Hewlett-Packard Company.  All rights reserved.
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
 * Test that objects reachable from an object allocated with
 * `GC_malloc_with_finalizer` is not reclaimable before the finalizer
 * is called.
 */

#ifdef HAVE_CONFIG_H
/* For `GC_THREADS` (and `GC_PTHREADS`). */
#  include "config.h"
#endif

#undef GC_NO_THREAD_REDIRECTS
#include "gc/gc_disclaim.h"

#define NOT_GCBUILD
#include "private/gc_priv.h"

#include <string.h>

/*
 * Redefine the standard `rand()` with a trivial (yet sufficient for
 * the test purpose) implementation to avoid crashes inside `rand()`
 * on some hosts (e.g. FreeBSD 13.0) when used concurrently.
 * The standard specifies `rand()` as not a thread-safe API function.
 * On other hosts (e.g. OpenBSD 7.3), use of the standard `rand()`
 * causes "rand() may return deterministic values" warning.
 * Note: concurrent update of seed does not hurt the test.
 */
#undef rand
static GC_RAND_STATE_T seed;
#define rand() GC_RAND_NEXT(&seed)

#define MAX_LOG_MISC_SIZES 20 /*< up to 1 MB */
#define POP_SIZE 1000
#define MUTATE_CNT_BASE (6 * 1000000)

#define my_assert(e)                                                   \
  if (!(e)) {                                                          \
    fflush(stdout);                                                    \
    fprintf(stderr, "Assertion failure, line %d: " #e "\n", __LINE__); \
    exit(-1);                                                          \
  }

#define CHECK_OUT_OF_MEMORY(p)            \
  do {                                    \
    if (NULL == (p)) {                    \
      fprintf(stderr, "Out of memory\n"); \
      exit(69);                           \
    }                                     \
  } while (0)

static int
is_equal(void *s, int c, size_t len)
{
  while (len--) {
    if (*(char *)s != c)
      return 0;
    s = (char *)s + 1;
  }
  return 1;
}

#define MEM_FILL_BYTE 0x56

static void GC_CALLBACK
misc_sizes_dct(void *obj, void *cd)
{
  unsigned log_size = *(unsigned char *)obj;
  size_t size;

  my_assert(log_size < sizeof(size_t) * 8);
  my_assert(cd == NULL);
  size = (size_t)1 << log_size;
  my_assert(is_equal((char *)obj + 1, MEM_FILL_BYTE, size - 1));
#if defined(CPPCHECK)
  GC_noop1_ptr(cd);
#endif
}

static void
test_misc_sizes(void)
{
  static const struct GC_finalizer_closure fc = { misc_sizes_dct, NULL };
  int i;
  for (i = 1; i <= MAX_LOG_MISC_SIZES; ++i) {
    void *p = GC_finalized_malloc((size_t)1 << i, &fc);

    CHECK_OUT_OF_MEMORY(p);
    my_assert(is_equal(p, 0, (size_t)1 << i));
    memset(p, MEM_FILL_BYTE, (size_t)1 << i);
    *(unsigned char *)p = (unsigned char)i;
  }
}

typedef struct pair_s *pair_t;

struct pair_s {
  char magic[16];
  int checksum;
  pair_t car;
  pair_t cdr;
};

static const char *const pair_magic = "PAIR_MAGIC_BYTES";

static int
is_pair(pair_t p)
{
  return memcmp(p->magic, pair_magic, sizeof(p->magic)) == 0;
}

#define CSUM_SEED 782
#define PTR_HASH(p) (GC_HIDE_NZ_POINTER(p) >> 4)

static void GC_CALLBACK
pair_dct(void *obj, void *cd)
{
  pair_t p = (pair_t)obj;
  int checksum = CSUM_SEED;

  my_assert(cd == (void *)PTR_HASH(p));
  /* Check that `obj` and its fields are not trashed. */
#ifdef DEBUG_DISCLAIM_DESTRUCT
  printf("Destruct %p: (car= %p, cdr= %p)\n", (void *)p, (void *)p->car,
         (void *)p->cdr);
#endif
  my_assert(GC_base(obj));
  my_assert(is_pair(p));
  my_assert(!p->car || is_pair(p->car));
  my_assert(!p->cdr || is_pair(p->cdr));
  if (p->car)
    checksum += p->car->checksum;
  if (p->cdr)
    checksum += p->cdr->checksum;
  my_assert(p->checksum == checksum);

  /* Invalidate it. */
  memset(p->magic, '*', sizeof(p->magic));
  p->checksum = 0;
  p->car = NULL;
  p->cdr = NULL;
#if defined(CPPCHECK)
  GC_noop1_ptr(cd);
#endif
}

static pair_t
pair_new(pair_t car, pair_t cdr)
{
  pair_t p;
  struct GC_finalizer_closure *pfc
      = GC_NEW_ATOMIC(struct GC_finalizer_closure);

  CHECK_OUT_OF_MEMORY(pfc);
  pfc->proc = pair_dct;
  p = (pair_t)GC_finalized_malloc(sizeof(struct pair_s), pfc);
  CHECK_OUT_OF_MEMORY(p);
  pfc->cd = (void *)PTR_HASH(p);
  my_assert(!is_pair(p));
  my_assert(is_equal(p, 0, sizeof(struct pair_s)));
  memcpy(p->magic, pair_magic, sizeof(p->magic));
  p->checksum = CSUM_SEED + (car != NULL ? car->checksum : 0)
                + (cdr != NULL ? cdr->checksum : 0);
  p->car = car;
  GC_ptr_store_and_dirty(&p->cdr, cdr);
  GC_reachable_here(car);
#ifdef DEBUG_DISCLAIM_DESTRUCT
  printf("Construct %p: (car= %p, cdr= %p)\n", (void *)p, (void *)p->car,
         (void *)p->cdr);
#endif
  return p;
}

static void
pair_check_rec(pair_t p)
{
  while (p) {
    int checksum = CSUM_SEED;

    if (p->car)
      checksum += p->car->checksum;
    if (p->cdr)
      checksum += p->cdr->checksum;
    my_assert(p->checksum == checksum);
    p = (rand() & 1) != 0 ? p->cdr : p->car;
  }
}

#ifdef GC_PTHREADS
#  ifndef NTHREADS
/* Note: this excludes the main thread, which also runs a test. */
#    define NTHREADS 5
#  endif
#  include <errno.h> /*< for `EAGAIN` */
#  include <pthread.h>
#else
#  undef NTHREADS
#  define NTHREADS 0
#endif

#define MUTATE_CNT (MUTATE_CNT_BASE / (NTHREADS + 1))
#define GROW_LIMIT (MUTATE_CNT / 10)

static void *
test(void *data)
{
  int i;
  pair_t pop[POP_SIZE];
  memset(pop, 0, sizeof(pop));
  for (i = 0; i < MUTATE_CNT; ++i) {
    int t = rand() % POP_SIZE;
    int j;

    switch (rand() % (i > GROW_LIMIT ? 5 : 3)) {
    case 0:
    case 3:
      if (pop[t])
        pop[t] = pop[t]->car;
      break;
    case 1:
    case 4:
      if (pop[t])
        pop[t] = pop[t]->cdr;
      break;
    case 2:
      j = rand() % POP_SIZE;
      pop[t] = pair_new(pop[j], pop[rand() % POP_SIZE]);
      break;
    }
    if (rand() % 8 == 1)
      pair_check_rec(pop[rand() % POP_SIZE]);
  }
  return data;
}

int
main(void)
{
#if NTHREADS > 0
  pthread_t th[NTHREADS];
  int i, n;
#endif

  /* Test the same signal usage for threads suspend and restart on Linux. */
#ifdef GC_PTHREADS
  GC_set_thr_restart_signal(GC_get_suspend_signal());
#endif

  /* Make the test stricter. */
  GC_set_all_interior_pointers(0);

#ifdef TEST_MANUAL_VDB
  GC_set_manual_vdb_allowed(1);
#endif
  GC_INIT();
  GC_init_finalized_malloc();
#ifndef NO_INCREMENTAL
  GC_enable_incremental();
#endif
  if (GC_get_find_leak())
    printf("This test program is not designed for leak detection mode\n");

  test_misc_sizes();

#if NTHREADS > 0
  printf("Threaded disclaim test.\n");
  for (i = 0; i < NTHREADS; ++i) {
    int err = pthread_create(&th[i], NULL, test, NULL);

    if (err != 0) {
      fprintf(stderr, "Thread #%d creation failed, errno= %d\n", i, err);
      if (i > 1 && EAGAIN == err)
        break;
      exit(1);
    }
  }
  n = i;
#endif
  test(NULL);
#if NTHREADS > 0
  for (i = 0; i < n; ++i) {
    int err = pthread_join(th[i], NULL);

    if (err != 0) {
      fprintf(stderr, "Thread #%d join failed, errno= %d\n", i, err);
      exit(69);
    }
  }
#endif
  printf("SUCCEEDED\n");
  return 0;
}
