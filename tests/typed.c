/* Extra testing of the typed allocation API. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_ASSERT(e)                                                    \
  if (!(e)) {                                                             \
    fprintf(stderr, "Assertion failure: %s:%d, %s\n", __FILE__, __LINE__, \
            #e);                                                          \
    exit(1);                                                              \
  }

#define CHECK_OUT_OF_MEMORY(p)            \
  do {                                    \
    if (NULL == (p)) {                    \
      fprintf(stderr, "Out of memory\n"); \
      exit(69);                           \
    }                                     \
  } while (0)

#ifndef NO_TYPED_TEST
#  include "gc/gc_mark.h"
#  include "gc/gc_typed.h"

/* Test basic functionality with small bitmap. */
static void
test_small_bitmap(void)
{
  GC_word bm[2] = { 0 };

  GC_set_bit(bm, 0);
  GC_set_bit(bm, 3);
  GC_set_bit(bm, 7);

  /* This should not trigger `GC_add_ext_descriptor()` since it is small. */
  TEST_ASSERT(GC_make_descriptor(bm, 8) != 0);
}

/* Test a large bitmap that should force use of ext descriptor. */
static void
test_large_bitmap(void)
{
  const size_t large_size = 2000; /*< greater than BITMAP_BITS */
  GC_descr d;
  size_t bm_sz = ((large_size + GC_WORDSZ - 1) / GC_WORDSZ) * sizeof(GC_word);
  GC_word *bm = (GC_word *)GC_MALLOC_ATOMIC(bm_sz);

  CHECK_OUT_OF_MEMORY(bm);
  memset(bm, 0, bm_sz);

  /* Set some scattered bits to simulate pointer fields. */
  GC_set_bit(bm, 0);
  GC_set_bit(bm, 15);
  GC_set_bit(bm, 100);
  GC_set_bit(bm, 255);
  GC_set_bit(bm, 500);
  GC_set_bit(bm, 750);
  GC_set_bit(bm, 999);
  GC_set_bit(bm, 1500);
  GC_set_bit(bm, 1999);

  /* This should trigger `GC_add_ext_descriptor()` internally. */
  d = GC_make_descriptor(bm, large_size);

  TEST_ASSERT(d != 0);
  TEST_ASSERT((d & GC_DS_TAGS) == GC_DS_PROC
              || (d & GC_DS_TAGS) == GC_DS_LENGTH);
  TEST_ASSERT(GC_MALLOC_EXPLICITLY_TYPED(large_size * sizeof(void *), d)
              != NULL);
}

/* Test a very large bitmap. */
static void
test_very_large_bitmap(void)
{
  const size_t very_large_size = 10000;
  GC_descr d;
  size_t i;
  size_t bm_sz
      = ((very_large_size + GC_WORDSZ - 1) / GC_WORDSZ) * sizeof(GC_word);
  GC_word *bm = (GC_word *)GC_MALLOC_ATOMIC(bm_sz);
  CHECK_OUT_OF_MEMORY(bm);
  memset(bm, 0, bm_sz);

  /* Set bits at regular intervals. */
  for (i = 0; i < very_large_size; i += 100)
    GC_set_bit(bm, i);

  d = GC_make_descriptor(bm, very_large_size);
  TEST_ASSERT(d != 0);

  TEST_ASSERT(GC_MALLOC_EXPLICITLY_TYPED_IGNORE_OFF_PAGE(
                  very_large_size * sizeof(void *), d)
              != NULL);
}

/* Test an edge case having bitmap with all bits set. */
static void
test_all_bits_set(void)
{
  const size_t size = 5000; /*< large enough */
  size_t i;
  GC_descr d;
  GC_word *bm = (GC_word *)GC_MALLOC_ATOMIC(
      ((size + GC_WORDSZ - 1) / GC_WORDSZ) * sizeof(GC_word));
  CHECK_OUT_OF_MEMORY(bm);

  /* Set all bits. */
  for (i = 0; i < size; i++) {
    GC_set_bit(bm, i);
  }

  d = GC_make_descriptor(bm, size);
  TEST_ASSERT(d != 0);

  TEST_ASSERT(GC_MALLOC_EXPLICITLY_TYPED(size * sizeof(void *), d) != NULL);
}

/* Test edge case having a bitmap with only last bit set. */
static void
test_last_bit_set(void)
{
  const size_t size = 8000; /*< large enough */
  GC_descr d;
  size_t bm_sz = ((size + GC_WORDSZ - 1) / GC_WORDSZ) * sizeof(GC_word);
  GC_word *bm = (GC_word *)GC_MALLOC_ATOMIC(bm_sz);
  CHECK_OUT_OF_MEMORY(bm);
  memset(bm, 0, bm_sz);

  GC_set_bit(bm, size - 1);
  d = GC_make_descriptor(bm, size);
  TEST_ASSERT(d != 0);

  TEST_ASSERT(GC_MALLOC_EXPLICITLY_TYPED(size * sizeof(void *), d) != NULL);
}

/* Test multiple descriptors to check the extended descriptor array growth. */
static void
test_multiple_descriptors(void)
{
  const size_t num_descriptors = 10;
  const size_t size = 3000; /*< large enough */
  size_t i;
  GC_descr descriptors[10];
  void *objects[10];

  for (i = 0; i < num_descriptors; i++) {
    size_t j;
    size_t bm_sz = ((size + GC_WORDSZ - 1) / GC_WORDSZ) * sizeof(GC_word);
    GC_word *bm = (GC_word *)GC_MALLOC_ATOMIC(bm_sz);
    CHECK_OUT_OF_MEMORY(bm);
    memset(bm, 0, bm_sz);

    /* Set some pattern of bits. */
    for (j = 0; j < size; j += (i + 1) * 10)
      GC_set_bit(bm, j);

    descriptors[i] = GC_make_descriptor(bm, size);
    TEST_ASSERT(descriptors[i] != 0);
    TEST_ASSERT((descriptors[i] & GC_DS_TAGS) == GC_DS_PROC
                || (descriptors[i] & GC_DS_TAGS) == GC_DS_LENGTH);

    objects[i]
        = GC_MALLOC_EXPLICITLY_TYPED(size * sizeof(void *), descriptors[i]);
    TEST_ASSERT(objects[i] != NULL);
  }
}

/* Test array allocation with typed descriptors. */
static void
test_typed_array_allocation(void)
{
  const size_t nelements = 100;
  const size_t element_size = 2500; /*< large enough */
  GC_descr d;
  size_t bm_sz
      = ((element_size + GC_WORDSZ - 1) / GC_WORDSZ) * sizeof(GC_word);
  GC_word *bm = (GC_word *)GC_MALLOC_ATOMIC(bm_sz);
  CHECK_OUT_OF_MEMORY(bm);
  memset(bm, 0, bm_sz);

  GC_set_bit(bm, 0);
  GC_set_bit(bm, 25);
  GC_set_bit(bm, 49);
  GC_set_bit(bm, 100);
  GC_set_bit(bm, 250);
  GC_set_bit(bm, 499);
  GC_set_bit(bm, 1000);
  GC_set_bit(bm, 2499);

  d = GC_make_descriptor(bm, element_size);
  TEST_ASSERT(d != 0);

  TEST_ASSERT(
      GC_CALLOC_EXPLICITLY_TYPED(nelements, element_size * sizeof(void *), d)
      != NULL);
}

/* Test the scenario of memory growth and reallocation. */
static void
test_memory_growth(void)
{
  const size_t initial_size = 2000; /*< large enough */
  const size_t growth_factor = 50;
  size_t i;
  void **objects;
  GC_descr *descriptors = (GC_descr *)GC_MALLOC(sizeof(GC_descr) * 50);
  CHECK_OUT_OF_MEMORY(descriptors);
  objects = (void **)GC_MALLOC(sizeof(void *) * 50);
  CHECK_OUT_OF_MEMORY(objects);

  /* Create progressively larger descriptors. */
  for (i = 0; i < 50; i++) {
    size_t size = initial_size + i * growth_factor;
    size_t j;
    size_t bm_sz = ((size + GC_WORDSZ - 1) / GC_WORDSZ) * sizeof(GC_word);
    GC_word *bm = (GC_word *)GC_MALLOC_ATOMIC(bm_sz);
    CHECK_OUT_OF_MEMORY(bm);
    memset(bm, 0, bm_sz);

    /* Set bits in a pattern. */
    for (j = 0; j < size; j += 25 + i)
      GC_set_bit(bm, j);

    descriptors[i] = GC_make_descriptor(bm, size);
    TEST_ASSERT(descriptors[i] != 0);
    objects[i]
        = GC_MALLOC_EXPLICITLY_TYPED(size * sizeof(void *), descriptors[i]);
    TEST_ASSERT(objects[i] != NULL);
  }
}

/* Test some error conditions and edge cases. */
static void
test_edge_cases(void)
{
  const size_t word_bits = GC_WORDSZ;
  GC_word *bm = GC_NEW_ATOMIC(GC_word);

  CHECK_OUT_OF_MEMORY(bm);
  bm[0] = 1; /*< set the first bit */
  TEST_ASSERT(GC_make_descriptor(bm, 1) != 0);

  /* Test with the size equal to machine word size. */
  bm = GC_NEW_ATOMIC(GC_word);
  CHECK_OUT_OF_MEMORY(bm);
  bm[0] = ~(GC_word)0; /*< set all bits */
  TEST_ASSERT(GC_make_descriptor(bm, word_bits) != 0);
}

/* Test that the allocated objects are properly marked. */
static void
test_gc_collection(void)
{
  const size_t size = 4000; /*< large enough */
  GC_descr d;
  void *obj1, *obj2;
  void *ptr1, *ptr2;
  size_t bm_sz = ((size + GC_WORDSZ - 1) / GC_WORDSZ) * sizeof(GC_word);
  GC_word *bm = (GC_word *)GC_MALLOC_ATOMIC(bm_sz);
  CHECK_OUT_OF_MEMORY(bm);
  memset(bm, 0, bm_sz);

  /* Set some pointer fields. */
  GC_set_bit(bm, 0);
  GC_set_bit(bm, 100);
  GC_set_bit(bm, 200);
  GC_set_bit(bm, 1499);
  GC_set_bit(bm, 3999);

  d = GC_make_descriptor(bm, size);
  TEST_ASSERT(d != 0);

  obj1 = GC_MALLOC_EXPLICITLY_TYPED(size * sizeof(void *), d);
  TEST_ASSERT(obj1 != NULL);
  obj2 = GC_MALLOC_EXPLICITLY_TYPED(size * sizeof(void *), d);
  TEST_ASSERT(obj2 != NULL);

  /* Allocate some pointed-to objects. */
  ptr1 = GC_MALLOC(100);
  TEST_ASSERT(ptr1 != NULL);
  ptr2 = GC_MALLOC(200);
  TEST_ASSERT(ptr2 != NULL);

  /* Set some pointers in the typed objects. */
  ((void **)obj1)[0] = ptr1;
  ((void **)obj1)[100] = ptr2;
  ((void **)obj2)[200] = ptr1;
  ((void **)obj2)[3999] = ptr2;

  GC_gcollect();

  /* The pointed-to objects should still be alive */
  TEST_ASSERT(GC_is_heap_ptr(ptr1));
  TEST_ASSERT(GC_is_heap_ptr(ptr2));

#  ifdef CPPCHECK
  GC_noop1_ptr(((void **)obj1)[100]);
  GC_noop1_ptr(((void **)obj2)[3999]);
#  endif
}

#endif /* !NO_TYPED_TEST */

int
main(void)
{
#ifdef NO_TYPED_TEST
  printf("test skipped\n");
#else
  GC_INIT();
  if (GC_get_find_leak())
    printf("This test program is not designed for leak detection mode\n");
#  ifndef NO_INCREMENTAL
  GC_enable_incremental();
#  endif

  /* Tests for `GC_add_ext_descriptor()`. */
  test_small_bitmap();
  test_large_bitmap();
  test_very_large_bitmap();
  test_all_bits_set();
  test_last_bit_set();
  test_multiple_descriptors();
  test_typed_array_allocation();
  test_memory_growth();
  test_edge_cases();
  test_gc_collection();

  printf("SUCCEEDED\n");
#endif
  return 0;
}
