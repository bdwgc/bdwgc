
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef GC_IGNORE_WARN
/*
 * Ignore misleading "Out of Memory!" warning (which is printed on every
 * `GC_MALLOC` call below) by defining this macro before include `gc.h` file.
 */
#  define GC_IGNORE_WARN
#endif

#ifndef GC_MAXIMUM_HEAP_SIZE
#  define GC_MAXIMUM_HEAP_SIZE (100 * 1024 * 1024)
#  define GC_INITIAL_HEAP_SIZE (GC_MAXIMUM_HEAP_SIZE / 20)
/*
 * Otherwise heap expansion aborts when deallocating large block.
 * That is OK.  We test this corner case mostly to make sure that
 * it fails predictably.
 */
#endif

#ifndef GC_ATTR_ALLOC_SIZE
/*
 * Omit `alloc_size` attribute to avoid compiler warnings about exceeding
 * maximum object size when values close to `GC_SWORD_MAX` are passed
 * to `GC_MALLOC()`.
 */
#  define GC_ATTR_ALLOC_SIZE(argnum) /*< empty */
#endif

#include "gc.h"

#define TEST_ASSERT(e)                                                    \
  if (!(e)) {                                                             \
    fprintf(stderr, "Assertion failure: %s:%d, %s\n", __FILE__, __LINE__, \
            #e);                                                          \
    exit(1);                                                              \
  }

/*
 * Check that very large allocation requests fail.  A non-null result would
 * usually indicate that the size was somehow converted to a negative number.
 * Clients should not do this, but we should fail in the expected manner.
 */

#undef SIZE_MAX
#define SIZE_MAX (~(size_t)0)

#define U_SSIZE_MAX (SIZE_MAX >> 1) /*< unsigned */

int
main(void)
{
  GC_INIT();

  TEST_ASSERT(GC_MALLOC(U_SSIZE_MAX - 4096) == NULL);
  /* Skip other checks to avoid "exceeds maximum object size" gcc warning. */
#if !(defined(_FORTIFY_SOURCE) && defined(__x86_64__) && defined(__ILP32__))
  TEST_ASSERT(GC_MALLOC(U_SSIZE_MAX - 1024) == NULL);
  TEST_ASSERT(GC_MALLOC(U_SSIZE_MAX) == NULL);
#endif
#if !defined(_FORTIFY_SOURCE)
  TEST_ASSERT(GC_MALLOC(U_SSIZE_MAX + 1) == NULL);
  TEST_ASSERT(GC_MALLOC(U_SSIZE_MAX + 1024) == NULL);
  TEST_ASSERT(GC_MALLOC(SIZE_MAX - 1024) == NULL);
  TEST_ASSERT(GC_MALLOC(SIZE_MAX - 16) == NULL);
  TEST_ASSERT(GC_MALLOC(SIZE_MAX - 8) == NULL);
  TEST_ASSERT(GC_MALLOC(SIZE_MAX - 4) == NULL);
  TEST_ASSERT(GC_MALLOC(SIZE_MAX) == NULL);
#endif
  printf("SUCCEEDED\n");
  return 0;
}
