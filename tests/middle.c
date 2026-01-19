/*
 * Test at the boundary between small and large objects.
 * Inspired by a test case from Zoltan Varga.
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>

#define NOT_GCBUILD
#include "private/gc_priv.h"

#undef rand
static GC_RAND_STATE_T seed;
#define rand() GC_RAND_NEXT(&seed)

#define N_TESTS 32000

/* Typical page size. */
#define ALLOC_SZ 4096

#define CHECK_OUT_OF_MEMORY(p)            \
  do {                                    \
    if (NULL == (p)) {                    \
      fprintf(stderr, "Out of memory\n"); \
      exit(69);                           \
    }                                     \
  } while (0)

int
main(void)
{
  int i;

  GC_set_all_interior_pointers(0);
  GC_INIT();
  if (GC_get_find_leak())
    printf("This test program is not designed for leak detection mode\n");

  for (i = 0; i < N_TESTS; ++i) {
    CHECK_OUT_OF_MEMORY(GC_malloc_atomic(ALLOC_SZ));
    CHECK_OUT_OF_MEMORY(GC_malloc(ALLOC_SZ));
  }

#ifndef TEST_NO_THREADS
  /* Test delayed start of marker threads, if they are enabled. */
  GC_start_mark_threads();
#endif

  for (i = 0; i < N_TESTS; ++i) {
    CHECK_OUT_OF_MEMORY(GC_malloc_atomic(ALLOC_SZ / 2));
    CHECK_OUT_OF_MEMORY(GC_malloc(ALLOC_SZ / 2));
  }

  for (i = 0; i < N_TESTS; ++i) {
    CHECK_OUT_OF_MEMORY(GC_malloc_atomic((unsigned)rand() % ALLOC_SZ));
    CHECK_OUT_OF_MEMORY(GC_malloc((unsigned)rand() % (ALLOC_SZ / 8)));
  }

  printf("Final heap size is %lu\n", (unsigned long)GC_get_heap_size());
  return 0;
}
