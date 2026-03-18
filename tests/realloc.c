
#include <stdio.h>
#include <stdlib.h>

#include "gc.h"

#define COUNT 10000000

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

int
main(void)
{
  int i;
  unsigned long last_heap_size = 0;

  GC_INIT();
  if (GC_get_find_leak())
    printf("This test program is not designed for leak detection mode\n");

  for (i = 0; i < COUNT; i++) {
    int **p = GC_NEW(int *);
    int *q;

    CHECK_OUT_OF_MEMORY(p);
    q = GC_NEW_ATOMIC(int);
    CHECK_OUT_OF_MEMORY(q);
    TEST_ASSERT(NULL == *p);

    *p = (int *)GC_REALLOC(q, (i % 8 != 0 ? 2 : 4) * sizeof(int));
    CHECK_OUT_OF_MEMORY(*p);

    if (i % 10 == 0) {
      unsigned long heap_size = (unsigned long)GC_get_heap_size();

      if (heap_size != last_heap_size) {
        printf("Heap size: %lu\n", heap_size);
        last_heap_size = heap_size;
      }
    }
  }
  printf("SUCCEEDED\n");
  return 0;
}
