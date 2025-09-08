
#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>

#include "gc/leak_detector.h"

#define N_TESTS 100

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
  char *p[N_TESTS];
  unsigned i;

#ifndef NO_FIND_LEAK
  /* Just in case the code is compiled without `FIND_LEAK` macro defined. */
  GC_set_find_leak(1);
#endif
  /* Needed if thread-local allocation is enabled. */
  /* FIXME: This is not ideal. */
  GC_INIT();

  p[0] = (char *)aligned_alloc(8, 50 /* `size` */);
  if (NULL == p[0]) {
    fprintf(stderr, "aligned_alloc failed\n");
    return 1;
  }
  free_aligned_sized(p[0], 8, 50);

  p[0] = (char *)_aligned_malloc(70 /* `size` */, 16);
  if (NULL == p[0]) {
    fprintf(stderr, "_aligned_malloc failed\n");
    return 1;
  }
  _aligned_free(p[0]);

  p[0] = strdup("abc");
  CHECK_OUT_OF_MEMORY(p[0]);
  for (i = 1; i < N_TESTS; ++i) {
    p[i] = (char *)malloc(sizeof(int) + i);
    CHECK_OUT_OF_MEMORY(p[i]);
    (void)malloc_usable_size(p[i]);
  }
  CHECK_LEAKS();
  for (i = 3; i < N_TESTS / 2; ++i) {
    p[i] = (char *)((i & 1) != 0 ? reallocarray(p[i], i, 43)
                                 : realloc(p[i], i * 16 + 1));
    CHECK_OUT_OF_MEMORY(p[i]);
  }
  CHECK_LEAKS();
  for (i = 2; i < N_TESTS; ++i) {
    free(p[i]);
  }
  for (i = 0; i < N_TESTS / 8; ++i) {
    p[i] = i < 3 || i > 6 ? (char *)malloc(sizeof(int) + i)
                          : strndup("abcd", i);
    CHECK_OUT_OF_MEMORY(p[i]);
    if (i == 3)
      free_sized(p[i], i /* `strlen(p[i])` */ + 1);
  }
  p[0] = (char *)calloc(3, 16);
  CHECK_OUT_OF_MEMORY(p[0]);
#if defined(sun) || defined(__sun)
  cfree(p[0], 3, 16);
#else
  cfree(p[0]);
#endif
#ifdef GC_REQUIRE_WCSDUP
  {
    static const wchar_t ws[] = { 'w', 0 };

    p[0] = (char *)wcsdup(ws);
    CHECK_OUT_OF_MEMORY(p[0]);
  }
#endif
  CHECK_LEAKS();
  CHECK_LEAKS();
  CHECK_LEAKS();
  printf("SUCCEEDED\n");
  return 0;
}
