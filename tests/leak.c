
#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>

#ifdef DONT_INCLUDE_LEAK_DETECTOR /*< for CPPCHECK */
#  include <string.h>
#  ifdef GC_REQUIRE_WCSDUP
#    include <wchar.h>
#  endif
#  include "gc.h"
#  if defined(REDIRECT_MALLOC) && !defined(REDIRECT_MALLOC_IN_HEADER)
#    ifdef __cplusplus
extern "C" {
#    endif
GC_API void *aligned_alloc(size_t align, size_t lb);
GC_API void free_aligned_sized(void *p, size_t align, size_t lb);
GC_API void free_sized(void *p, size_t lb);
GC_API void freezero(void *p, size_t clear_lb);
GC_API void freezeroall(void *p);
GC_API void *reallocf(void *p, size_t lb);
#    ifdef __cplusplus
} /* extern "C" */
#    endif
#  endif
#else
#  include "gc/leak_detector.h"
#endif

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

#if defined(REDIRECT_MALLOC) && !defined(REDIRECT_MALLOC_IN_HEADER) \
    || !defined(DONT_INCLUDE_LEAK_DETECTOR)
  p[0] = (char *)aligned_alloc(8, 50 /* `size` */);
  CHECK_OUT_OF_MEMORY(p[0]);
  free_aligned_sized(p[0], 8, 50);
#endif

#ifndef DONT_INCLUDE_LEAK_DETECTOR
  p[0] = (char *)_aligned_malloc(70 /* `size` */, 16);
  CHECK_OUT_OF_MEMORY(p[0]);
  _aligned_free(p[0]);
#endif

  p[0] = strdup("abc");
  CHECK_OUT_OF_MEMORY(p[0]);
  for (i = 1; i < N_TESTS; ++i) {
    p[i] = (char *)malloc(sizeof(int) + i);
    CHECK_OUT_OF_MEMORY(p[i]);
#ifndef DONT_INCLUDE_LEAK_DETECTOR
    (void)malloc_usable_size(p[i]);
#endif
  }
#ifndef DONT_INCLUDE_LEAK_DETECTOR
  CHECK_LEAKS();
#endif
  for (i = 3; i < N_TESTS / 2; ++i) {
#ifndef DONT_INCLUDE_LEAK_DETECTOR
    if ((i & 1) != 0) {
      p[i] = (char *)reallocarray(p[i], i, 43);
    } else
#endif
#if defined(REDIRECT_MALLOC) && !defined(REDIRECT_MALLOC_IN_HEADER) \
    || !defined(DONT_INCLUDE_LEAK_DETECTOR)
      /* else */ if ((i & 2) == 0) {
        p[i] = (char *)reallocf(p[i], i * 32 + 1);
      } else
#endif
      /* else */ {
        p[i] = (char *)realloc(p[i], i * 16 + 1);
      }
    CHECK_OUT_OF_MEMORY(p[i]);
  }
#ifndef DONT_INCLUDE_LEAK_DETECTOR
  CHECK_LEAKS();
#endif
  for (i = 2; i < N_TESTS; ++i) {
#if defined(REDIRECT_MALLOC) && !defined(REDIRECT_MALLOC_IN_HEADER) \
    || !defined(DONT_INCLUDE_LEAK_DETECTOR)
    if ((i & 1) != 0) {
      free(p[i]);
    } else if ((i & 2) != 0) {
      freezero(p[i], i);
    } else if ((i & 0x4) != 0) {
      freezeroall(p[i]);
    }
#else
    free(p[i]);
#endif
  }
  for (i = 0; i < N_TESTS / 8; ++i) {
    p[i] = i < 3 || i > 6 ? (char *)malloc(sizeof(int) + i)
                          : strndup("abcd", i);
    CHECK_OUT_OF_MEMORY(p[i]);
    if (i == 3) {
#if defined(REDIRECT_MALLOC) && !defined(REDIRECT_MALLOC_IN_HEADER) \
    || !defined(DONT_INCLUDE_LEAK_DETECTOR)
      free_sized(p[i], i /* `strlen(p[i])` */ + 1);
#else
      free(p[i]);
#endif
    }
  }
  p[0] = (char *)calloc(3, 16);
  CHECK_OUT_OF_MEMORY(p[0]);
#ifdef DONT_INCLUDE_LEAK_DETECTOR
  free(p[0]);
#elif defined(sun) || defined(__sun)
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
#ifndef DONT_INCLUDE_LEAK_DETECTOR
  CHECK_LEAKS();
  CHECK_LEAKS();
  CHECK_LEAKS();
#endif
  printf("SUCCEEDED\n");
  return 0;
}
