/*
 * Copyright (c) 2017 Ivan Maidanski
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
 * Minimal testing of atomic operations used by the collector.
 * Primary use is to determine whether the compiler atomic intrinsics
 * can be relied on.
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <stdio.h>

#if defined(GC_BUILTIN_ATOMIC) || defined(GC_THREADS)

#  include <stdlib.h>

#  ifdef PARALLEL_MARK
#    define AO_REQUIRE_CAS
#    if !defined(__GNUC__) && !defined(AO_ASSUME_WINDOWS98)
#      define AO_ASSUME_WINDOWS98
#    endif
#  endif

#  include "private/gc_atomic_ops.h"

#  define TEST_ASSERT(e)                                                 \
    if (!(e)) {                                                          \
      fprintf(stderr, "Assertion failure, line %d: " #e "\n", __LINE__); \
      exit(-1);                                                          \
    }

int
main(void)
{
  AO_t x = 13;
#  if defined(AO_HAVE_char_load) || defined(AO_HAVE_char_store)
  unsigned char c = 117;
#  endif
#  ifdef AO_HAVE_test_and_set_acquire
  AO_TS_t z = AO_TS_INITIALIZER;

  TEST_ASSERT(AO_test_and_set_acquire(&z) == AO_TS_CLEAR);
  TEST_ASSERT(AO_test_and_set_acquire(&z) == AO_TS_SET);
  AO_CLEAR(&z);
#  endif
  AO_compiler_barrier();
#  ifdef AO_HAVE_nop_full
  AO_nop_full();
#  endif
#  ifdef AO_HAVE_char_load
  TEST_ASSERT(AO_char_load(&c) == 117);
#  endif
#  ifdef AO_HAVE_char_store
  AO_char_store(&c, 119);
  TEST_ASSERT(c == 119);
#  endif
#  ifdef AO_HAVE_load_acquire
  TEST_ASSERT(AO_load_acquire(&x) == 13);
#  endif
#  if defined(AO_HAVE_fetch_and_add) && defined(AO_HAVE_fetch_and_add1) \
      && defined(AO_HAVE_fetch_and_sub1)
  TEST_ASSERT(AO_fetch_and_add(&x, 42) == 13);
  TEST_ASSERT(AO_fetch_and_add(&x, (AO_t)(-43)) == 55);
  TEST_ASSERT(AO_fetch_and_add1(&x) == 12);
  TEST_ASSERT(AO_fetch_and_sub1(&x) == 13);
  TEST_ASSERT(AO_fetch_and_add1(&x) == 12); /*< the 2nd call */
#  endif
#  ifdef AO_HAVE_compare_and_swap_release
  TEST_ASSERT(!AO_compare_and_swap_release(&x, 14, 42));
  TEST_ASSERT(x == 13);
  TEST_ASSERT(AO_compare_and_swap_release(&x, 13, 42));
  TEST_ASSERT(x == 42);
#    ifdef AO_REQUIRE_CAS
  {
    char *cptr = (char *)NULL;

    TEST_ASSERT(GC_cptr_compare_and_swap(&cptr, (char *)NULL, (char *)&x));
    TEST_ASSERT(cptr == (char *)&x);
  }
#    endif
#  else
  if (*(volatile AO_t *)&x == 13)
    *(volatile AO_t *)&x = 42;
#  endif
#  ifdef AO_HAVE_or
  AO_or(&x, 66);
  TEST_ASSERT(x == 106);
#  endif
#  ifdef AO_HAVE_store_release
  AO_store_release(&x, 113);
  TEST_ASSERT(x == 113);
#  endif
  return 0;
}

#else

int
main(void)
{
  printf("test skipped\n");
  return 0;
}

#endif
