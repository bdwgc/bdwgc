/*
 * Copyright (c) 1993-1994 by Xerox Corporation.  All rights reserved.
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

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gc.h"
#include "gc/cord.h"

/*
 * This is a very incomplete test of the cord package.  It knows about
 * a few internals of the package (e.g. when C strings are returned) that
 * real clients should not rely on.
 */

#define ABORT(string)                        \
  {                                          \
    fprintf(stderr, "FAILED: %s\n", string); \
    abort();                                 \
  }

#if defined(CPPCHECK)
#  undef CORD_iter
#  undef CORD_next
#  undef CORD_pos_fetch
#  undef CORD_pos_to_cord
#  undef CORD_pos_to_index
#  undef CORD_pos_valid
#  undef CORD_prev
#endif

static int count;

#define LOG_CORD_ITER_CNT 16
#define SUBSTR_POS_BASE 1000
#define PREPARE_CAT_COUNT 100

#define CORD_ITER_CNT (1 << LOG_CORD_ITER_CNT)
#define SMALL_SUBSTR_POS (1 << (LOG_CORD_ITER_CNT - 6))
#define BIG_SUBSTR_POS (SUBSTR_POS_BASE * 36)

static int
test_fn(char c, void *client_data)
{
  if ((GC_uintptr_t)client_data != 13U)
    ABORT("bad client data");
  if (count < CORD_ITER_CNT + 1) {
    if ((count & 1) == 0) {
      if (c != 'b')
        ABORT("bad char");
    } else {
      if (c != 'a')
        ABORT("bad char");
    }
    count++;
#if defined(CPPCHECK)
    GC_noop1_ptr(client_data);
#endif
    return 0;
  } else {
    if (c != 'c')
      ABORT("bad char");
    count++;
    return 1;
  }
}

static char
id_cord_fn(size_t i, void *client_data)
{
  if (client_data != NULL)
    ABORT("id_cord_fn: bad client data");
#if defined(CPPCHECK)
  GC_noop1_ptr(client_data);
#endif
  return (char)i;
}

static void
test_cord_x1(CORD x)
{
  CORD y;
  CORD_pos p;

  count = 0;
  if (CORD_iter5(x, CORD_ITER_CNT - 1, test_fn, CORD_NO_FN,
                 (void *)(GC_uintptr_t)13)
      == 0) {
    ABORT("CORD_iter5 failed");
  }
  if (count != CORD_ITER_CNT + 2)
    ABORT("CORD_iter5 failed");

  count = 0;
  CORD_set_pos(p, x, CORD_ITER_CNT - 1);
  while (CORD_pos_valid(p)) {
    (void)test_fn(CORD_pos_fetch(p), (void *)(GC_uintptr_t)13);
    CORD_next(p);
  }
  if (count != CORD_ITER_CNT + 2)
    ABORT("Position based iteration failed");

  y = CORD_substr(x, SMALL_SUBSTR_POS - 1, 5);

  if (!y)
    ABORT("CORD_substr returned NULL");
  if (!CORD_IS_STRING(y))
    ABORT("short cord should usually be a string");
  if (strcmp(y, ("ba"
                 "ba"
                 "b"))
      != 0)
    ABORT("bad CORD_substr result");

  y = CORD_substr(x, SMALL_SUBSTR_POS, 8);
  if (!y)
    ABORT("CORD_substr returned NULL");
  if (!CORD_IS_STRING(y))
    ABORT("short cord should usually be a string");
  if (strcmp(y, ("ab"
                 "ab"
                 "ab"
                 "ab"))
      != 0)
    ABORT("bad CORD_substr result (2)");

  y = CORD_substr(x, 2 * CORD_ITER_CNT - 1, 8);
  if (!y)
    ABORT("CORD_substr returned NULL");
  if (!CORD_IS_STRING(y))
    ABORT("short cord should usually be a string");
  if (strcmp(y, "bc") != 0)
    ABORT("bad CORD_substr result (3)");
}

static void
test_cord_x2(CORD x)
{
  size_t i;
  CORD y;
  CORD_pos p;

  count = 0;
  if (CORD_iter5(x, CORD_ITER_CNT - 1, test_fn, CORD_NO_FN,
                 (void *)(GC_uintptr_t)13)
      == 0) {
    ABORT("CORD_iter5 failed");
  }
  if (count != CORD_ITER_CNT + 2)
    ABORT("CORD_iter5 failed");

  y = CORD_substr(x, SMALL_SUBSTR_POS - 1, 5);
  if (!y)
    ABORT("CORD_substr returned NULL");
  if (!CORD_IS_STRING(y))
    ABORT("short cord should usually be a string");
  if (strcmp(y, ("ba"
                 "ba"
                 "b"))
      != 0)
    ABORT("bad CORD_substr result (4)");

  y = CORD_from_fn(id_cord_fn, 0, 13);
  i = 0;
  CORD_set_pos(p, y, i);
  while (CORD_pos_valid(p)) {
    char c = CORD_pos_fetch(p);

    if ((size_t)(unsigned char)c != i)
      ABORT("Traversal of function node failed");
    CORD_next(p);
    i++;
  }
  if (i != 13)
    ABORT("Bad apparent length for function node");
#if defined(CPPCHECK)
  /* TODO: Actually test these functions. */
  CORD_prev(p);
  (void)CORD_pos_to_cord(p);
  (void)CORD_pos_to_index(p);
#endif
}

static void
test_basics(void)
{
  CORD x = CORD_from_char_star("ab");
  size_t i;

  if (CORD_cat_char_star(CORD_EMPTY, "", 0) != CORD_EMPTY)
    ABORT("CORD_cat_char_star() returned non-empty cord");

  x = CORD_cat(x, x);
  if (x == CORD_EMPTY)
    ABORT("CORD_cat(x,x) returned empty cord");
  if (!CORD_IS_STRING(x))
    ABORT("short cord should usually be a string");
  if (strcmp(x, "abab") != 0)
    ABORT("bad CORD_cat result");
  for (i = 1; i < LOG_CORD_ITER_CNT; i++) {
    x = CORD_cat(x, x);
  }
  x = CORD_cat(x, "c");
  if (CORD_len(x) != 2 * CORD_ITER_CNT + 1)
    ABORT("bad length");
  test_cord_x1(x);

  x = CORD_balance(x);
  if (CORD_len(x) != 2 * CORD_ITER_CNT + 1)
    ABORT("bad length 2");
  test_cord_x2(x);

#if defined(CPPCHECK)
  /* TODO: Actually test these functions. */
  (void)CORD_iter(CORD_EMPTY, test_fn, NULL);
  (void)CORD_riter(CORD_EMPTY, test_fn, NULL);
#endif
}

static CORD
prepare_cord_f1(CORD y)
{
  CORD w = CORD_cat(CORD_cat(y, y), y);
  CORD x = "{}";
  CORD z = CORD_catn(3, y, y, y);
  int i;

  if (CORD_cmp(w, z) != 0)
    ABORT("CORD_catn comparison wrong");
  for (i = 1; i < PREPARE_CAT_COUNT; i++) {
    x = CORD_cat(x, y);
  }
  z = CORD_balance(x);
  if (CORD_cmp(x, z) != 0)
    ABORT("balanced string comparison wrong");
  if (CORD_cmp(x, CORD_cat(z, CORD_nul(13))) >= 0)
    ABORT("comparison 2");
  if (CORD_cmp(CORD_cat(x, CORD_nul(13)), z) <= 0)
    ABORT("comparison 3");
  if (CORD_cmp(x, CORD_cat(z, "13")) >= 0)
    ABORT("comparison 4");
  z = CORD_cat(z, CORD_nul(3));
  return z;
}

static void
test_cords_f1b(CORD w, CORD z)
{
  if (CORD_cmp(w, z) != 0)
    ABORT("File conversions differ");
  if (CORD_chr(w, 0, '9') != 37)
    ABORT("CORD_chr failed 1");
  if (CORD_chr(w, 3, 'a') != 38)
    ABORT("CORD_chr failed 2");
  if (CORD_rchr(w, CORD_len(w) - 1, '}') != 1)
    ABORT("CORD_rchr failed");
}

static void
test_cords_f2(CORD w, CORD x, CORD y)
{
  CORD u;

  if (CORD_len(w) != CORD_len(x))
    ABORT("file length wrong");
  if (CORD_cmp(w, x) != 0)
    ABORT("file comparison wrong");
  if (CORD_cmp(CORD_substr(w, BIG_SUBSTR_POS, 36), y) != 0)
    ABORT("file substr wrong");
  if (strcmp(CORD_to_char_star(CORD_substr(w, BIG_SUBSTR_POS, 36)), y) != 0)
    ABORT("char * file substr wrong");
  u = CORD_substr(w, BIG_SUBSTR_POS, 2);
  if (!u)
    ABORT("CORD_substr returned NULL");
  if (strcmp(u, "ab") != 0)
    ABORT("short file substr wrong");
  if (CORD_str(x, 1, "9a") != 35)
    ABORT("CORD_str failed 1");
  if (CORD_str(x, 0, "9abcdefghijk") != 35)
    ABORT("CORD_str failed 2");
  if (CORD_str(x, 0,
               ("9abcdefghij"
                "x"))
      != CORD_NOT_FOUND)
    ABORT("CORD_str failed 3");
  if (CORD_str(x, 0, "9>") != CORD_NOT_FOUND)
    ABORT("CORD_str failed 4");
}

static void
test_extras(void)
{
#define FNAME1 "cordtst1.tmp" /*< short name (8+3) for portability */
#define FNAME2 "cordtst2.tmp"
  int i;
  CORD y = "abcdefghijklmnopqrstuvwxyz0123456789";
  CORD w, x, z;
  FILE *f, *f1a, *f1b, *f2;

  f = fopen(FNAME1, "w");
  if (!f)
    ABORT("open 1 failed");
  z = prepare_cord_f1(y);
  if (CORD_put(z, f) == EOF)
    ABORT("CORD_put failed");
  if (fclose(f) == EOF)
    ABORT("fclose failed");

  f1a = fopen(FNAME1, "rb");
  if (!f1a)
    ABORT("open 1a failed");
  w = CORD_from_file(f1a);
  if (CORD_len(w) != CORD_len(z))
    ABORT("file length wrong");
  if (CORD_cmp(w, z) != 0)
    ABORT("file comparison wrong");
  if (CORD_cmp(CORD_substr(w, (PREPARE_CAT_COUNT / 2) * 36 + 2, 36), y) != 0)
    ABORT("file substr wrong (2)");

  f1b = fopen(FNAME1, "rb");
  if (!f1b)
    ABORT("open 1b failed");
  test_cords_f1b(w, CORD_from_file_lazy(f1b));

  f = fopen(FNAME2, "w");
  if (!f)
    ABORT("open 2 failed");
#ifdef __DJGPP__
  /* FIXME: DJGPP workaround.  Why does this help? */
  if (fflush(f) != 0)
    ABORT("fflush failed");
#endif
  x = y;
  for (i = 3; i < LOG_CORD_ITER_CNT; i++) {
    x = CORD_cat(x, x);
  }

  if (CORD_put(x, f) == EOF)
    ABORT("CORD_put failed");
  if (fclose(f) == EOF)
    ABORT("fclose failed");

  f2 = fopen(FNAME2, "rb");
  if (!f2)
    ABORT("open 2a failed");
  w = CORD_from_file(f2);
  test_cords_f2(w, x, y);

  /* Note: `f1a`, `f1b`, `f2` handles are closed lazily by `cord` library. */
  /* TODO: Propose and use `CORD_fclose`. */
  *(CORD volatile *)&w = CORD_EMPTY;
  *(CORD volatile *)&z = CORD_EMPTY;
  GC_gcollect();
#ifndef GC_NO_FINALIZATION
  GC_invoke_finalizers();
  /* Of course, this does not guarantee the files are closed. */
#endif
  if (remove(FNAME1) != 0) {
    /*
     * On some systems, e.g. OS/2, this may fail if `f1` is still open.
     * But we cannot call `fclose` as it might lead to double close.
     */
    fprintf(stderr, "WARNING: remove failed: " FNAME1 "\n");
  }
}

static int
wrap_vprintf(CORD format, ...)
{
  va_list args;
  int result;

  va_start(args, format);
  result = CORD_vprintf(format, args);
  va_end(args);
  return result;
}

static int
wrap_vfprintf(FILE *f, CORD format, ...)
{
  va_list args;
  int result;

  va_start(args, format);
  result = CORD_vfprintf(f, format, args);
  va_end(args);
  return result;
}

#if defined(__DJGPP__) || defined(__DMC__) || defined(__STRICT_ANSI__)
/* `snprintf` is missing in DJGPP (v2.0.3). */
#else
#  if defined(_MSC_VER)
#    if defined(_WIN32_WCE)
/* `_snprintf` is deprecated in WinCE. */
#      define GC_SNPRINTF StringCchPrintfA
#    else
#      define GC_SNPRINTF _snprintf
#    endif
#  else
#    define GC_SNPRINTF snprintf
#  endif
#endif

/* no static */ /* no const */ char *zu_format = (char *)"%zu";

static void
test_printf(void)
{
  CORD result;
  char result2[200];
  long l = -1;
  short s = (short)-1;
  CORD x;
  int res;

  if (CORD_sprintf(&result, "%7.2f%ln", 3.14159F, &l) != 7)
    ABORT("CORD_sprintf failed 1");
  if (CORD_cmp(result, "   3.14") != 0)
    ABORT("CORD_sprintf goofed 1");
  if (l != 7)
    ABORT("CORD_sprintf goofed 2");
  if (CORD_sprintf(&result, "%-7.2s%hn%c%s", "abcd", &s, 'x', "yz") != 10)
    ABORT("CORD_sprintf failed 2");
  if (CORD_cmp(result, "ab     xyz") != 0)
    ABORT("CORD_sprintf goofed 3");
  if (s != 7)
    ABORT("CORD_sprintf goofed 4");
  x = "abcdefghij";
  x = CORD_cat(x, x);
  x = CORD_cat(x, x);
  x = CORD_cat(x, x);
  if (CORD_sprintf(&result, "->%-120.78r!\n", x) != 124)
    ABORT("CORD_sprintf failed 3");
#ifdef GC_SNPRINTF
  (void)GC_SNPRINTF(result2, sizeof(result2), "->%-120.78s!\n",
                    CORD_to_char_star(x));
#else
  (void)sprintf(result2, "->%-120.78s!\n", CORD_to_char_star(x));
#endif
  result2[sizeof(result2) - 1] = '\0';
  if (CORD_cmp(result, result2) != 0)
    ABORT("CORD_sprintf goofed 5");

#ifdef GC_SNPRINTF
  /*
   * Check whether "%zu" specifier is supported; pass the format string via
   * a variable to avoid a compiler warning if not.
   */
  res = GC_SNPRINTF(result2, sizeof(result2), zu_format, (size_t)0);
#else
  res = sprintf(result2, zu_format, (size_t)0);
#endif
  result2[sizeof(result2) - 1] = '\0';
  /* Is "%z" supported by `printf`? */
  if (res == 1) {
    if (CORD_sprintf(&result, "%zu %zd 0x%0zx", (size_t)123, (size_t)4567,
                     (size_t)0x4abc)
        != 15)
      ABORT("CORD_sprintf failed 5");
    if (CORD_cmp(result, "123 4567 0x4abc") != 0)
      ABORT("CORD_sprintf goofed 5");
  } else {
    (void)CORD_printf("printf lacks support of 'z' modifier\n");
  }

  /* TODO: Better test CORD_[v][f]printf. */
  (void)CORD_printf(CORD_EMPTY);
  (void)wrap_vfprintf(stdout, CORD_EMPTY);
  (void)wrap_vprintf(CORD_EMPTY);
}

static void
test_cat_char_star(void)
{
  CORD x = CORD_cat_char_star(CORD_cat(CORD_chars('a', 9), "bcd"), " cat", 4);
  CORD y = CORD_cat_char_star(CORD_cat(x, x), "a", 1);

  if (CORD_len(y) != 33 || CORD_fetch(y, 10) != 'c')
    ABORT("CORD_cat_char_star(CORD_cat(x,x)) failed");
}

static char
fn_get_char(size_t i, void *client_data)
{
  if ((GC_uintptr_t)client_data != 15U)
    ABORT("fn_get_char: bad client data");
#if defined(CPPCHECK)
  GC_noop1_ptr(client_data);
#endif
  return (char)('A' + i % ('Z' - 'A' + 1));
}

static void
test_dump(void)
{
  CORD x = "CORD";

  CORD_dump(CORD_cat(x, " dump"));
  CORD_dump(CORD_EMPTY);
  CORD_dump(CORD_cat(x, CORD_chars('.', 30)));
  x = CORD_from_fn(fn_get_char, (void *)(GC_uintptr_t)15, 50);
  CORD_dump(x);
}

int
main(void)
{
  GC_INIT();
#ifndef NO_INCREMENTAL
  GC_enable_incremental();
#endif
  if (GC_get_find_leak())
    printf("This test program is not designed for leak detection mode\n");
  CORD_set_oom_fn(CORD_get_oom_fn()); /*< just to test these are existing */

  test_basics();
  test_extras();
  test_printf();
  test_cat_char_star();
  test_dump();

  GC_gcollect(); /*< to close `f2` before the file removal */
  if (remove(FNAME2) != 0) {
    fprintf(stderr, "WARNING: remove failed: " FNAME2 "\n");
  }
  CORD_fprintf(stdout, "SUCCEEDED\n");
  return 0;
}
