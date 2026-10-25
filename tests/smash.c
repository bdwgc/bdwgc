/* Test that overwrite error detection works reasonably. */

#ifndef GC_DEBUG
#  define GC_DEBUG
#endif

#include "gc.h"
#include "gc/gc_inline.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define COUNT 7000
#define SIZE 40

#define CHECK_OUT_OF_MEMORY(p)            \
  do {                                    \
    if (NULL == (p)) {                    \
      fprintf(stderr, "Out of memory\n"); \
      exit(69);                           \
    }                                     \
  } while (0)

/* Copied from `gc_priv.h` file. */
#ifndef GC_ATTR_NOINLINE
#  if GC_GNUC_PREREQ(4, 0)
#    define GC_ATTR_NOINLINE __attribute__((__noinline__))
#  elif _MSC_VER >= 1400
#    define GC_ATTR_NOINLINE __declspec(noinline)
#  else
#    define GC_ATTR_NOINLINE /*< empty */
#  endif
#endif

/*
 * Test that a collection clears the global free list of an object kind even
 * if the kind has no reclaim list yet.  Get pointer-free objects only through
 * `GC_generic_malloc_many` (which, unlike `GC_generic_malloc_inner`, does not
 * create the reclaim list), free one with `GC_free` (which puts it on the
 * global free list regardless of the reclaim list), and collect.  If the free
 * list is not cleared, the next `GC_generic_malloc_many` hands out objects
 * residing in a block that the collection has freed.  This runs before any
 * other allocation, while the `PTRFREE` kind has no reclaim list.  Several
 * rounds are needed because a stray pointer could keep the block alive.
 */
static void GC_ATTR_NOINLINE
test_many_round(int round)
{
#define MANY_LB 32 /*< object size (a multiple of `GC_GRANULE_BYTES`) */
  void *list, *p;

  GC_generic_malloc_many(MANY_LB, GC_I_PTRFREE, &list);
  CHECK_OUT_OF_MEMORY(list);
  for (p = list; p != NULL; p = GC_NEXT(p)) {
    if (GC_base(p) == NULL) {
      fprintf(stderr,
              "round %d:"
              " GC_generic_malloc_many returned object %p in a free block\n",
              round, p);
      exit(1);
    }
  }
  GC_free(list);
}

/* Overwrite the stack area used by `test_many_round()`. */
static void GC_ATTR_NOINLINE
test_many_clear_stack(void)
{
  volatile char buf[16384];

  memset((/* no volatile */ char *)buf, 0, sizeof(buf));
}

static void
test_malloc_many(void)
{
  int round;

  for (round = 0; round < 20; round++) {
    test_many_round(round);
    test_many_clear_stack();
    GC_gcollect();
  }
}

char *A[COUNT];

char *volatile q;

int
main(void)
{
  int i;
  char *p;

  GC_INIT();
  test_malloc_many();
  for (i = 0; i < COUNT; ++i) {
    A[i] = p = (char *)GC_MALLOC(SIZE);
    CHECK_OUT_OF_MEMORY(p);
    if (i % 3000 == 0) {
      q = NULL;
      GC_gcollect();
    } else if (i % 5678 == 0) {
      /*
       * Write a byte past the end of the allocated object but not beyond
       * the last word of the object's memory.  A `volatile` intermediate
       * pointer variable is used to avoid a compiler complain of
       * out-of-bounds access.
       */
      q = &p[(SIZE + i / 2000) /* 42 */];
      *q = 42;
    }
  }
  printf("SUCCEEDED\n");
  return 0;
}
