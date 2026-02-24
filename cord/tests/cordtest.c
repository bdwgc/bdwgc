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

#ifdef TEST_COVERAGE
#  undef CORD_iter
#  undef CORD_next
#  undef CORD_pos_fetch
#  undef CORD_pos_to_cord
#  undef CORD_prev
#endif

#if defined(CPPCHECK) || defined(TEST_COVERAGE)
#  undef CORD_pos_to_index
#  undef CORD_pos_valid
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

  if (CORD_iter(CORD_EMPTY, test_fn, NULL) != 0
      || CORD_riter(CORD_EMPTY, test_fn, NULL) != 0)
    ABORT("CORD_[r]iter(CORD_EMPTY) failed");
  if (CORD_riter(x, test_fn, (void *)(GC_uintptr_t)13) != 1)
    ABORT("CORD_riter failed");
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

  if (CORD_cmp(CORD_EMPTY, "a") >= 0 || CORD_cmp("b", CORD_EMPTY) <= 0
      || CORD_cmp(CORD_EMPTY, CORD_EMPTY) != 0)
    ABORT("CORD_cmp() failed for empty cord");
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
test_cat_char(void)
{
  CORD y = CORD_cat_char(CORD_from_char_star("hello"), '!');
  CORD z;

  if (CORD_len(y) != 6 || CORD_fetch(y, 5) != '!'
      || strcmp(CORD_to_char_star(y), "hello!") != 0)
    ABORT("CORD_cat_char result wrong");

  y = CORD_cat_char(CORD_EMPTY, 'a');
  if (CORD_len(y) != 1 || CORD_fetch(y, 0) != 'a')
    ABORT("CORD_cat_char with empty cord wrong");

  y = CORD_cat_char(CORD_from_char_star("hello"), '\0');
  if (CORD_len(y) != 6)
    ABORT("CORD_cat_char with null char length wrong");
  z = CORD_substr(y, 5, 1);
  if (CORD_len(z) != 1 || CORD_fetch(z, 0) != '\0')
    ABORT("CORD_cat_char with null char wrong");

  y = CORD_cat_char(CORD_from_char_star("a"), 'b');
  y = CORD_cat_char(y, 'c');
  if (CORD_len(y) != 3 || strcmp(CORD_to_char_star(y), "abc") != 0)
    ABORT("CORD_cat_char chaining result wrong");
}

static void
test_cat_char_star(void)
{
  CORD x = CORD_cat_char_star(CORD_cat(CORD_chars('a', 9), "bcd"), " cat", 4);
  CORD y = CORD_cat_char_star(CORD_cat(x, x), "a", 1);

  if (CORD_len(y) != 33 || CORD_fetch(y, 10) != 'c')
    ABORT("CORD_cat_char_star(CORD_cat(x,x)) failed");
}

static void
test_to_const_char_star(void)
{
  const char *result = CORD_to_const_char_star(CORD_EMPTY);

  if (result[0] != '\0')
    ABORT("CORD_to_const_char_star with empty cord wrong");

  result = CORD_to_const_char_star("hello");
  if (strcmp(result, "hello") != 0)
    ABORT("CORD_to_const_char_star with simple string wrong");

  result = CORD_to_const_char_star(
      CORD_cat(CORD_from_char_star("hello"), CORD_chars(' ', 30)));
  if (strncmp(result, "hello ", 6) != 0)
    ABORT("CORD_to_const_char_star with concat wrong");

  result = CORD_to_const_char_star(CORD_from_char_star("test"));
  if (result[0] != 't')
    ABORT("CORD_to_const_char_star result wrong");
}

static void
test_cord_str(void)
{
  CORD x;

  /* Find a substring at the beginning. */
  x = CORD_from_char_star("hello world");
  if (CORD_str(x, 0, CORD_from_char_star("hello")) != 0)
    ABORT("CORD_str should find substring at beginning");

  /* Find a substring in the middle. */
  x = CORD_from_char_star("hello world");
  if (CORD_str(x, 0, CORD_from_char_star("world")) != 6)
    ABORT("CORD_str should find substring in middle");

  /* Find a non-string cord. */
  x = CORD_chars('.', 50);
  if (CORD_str(CORD_cat(CORD_from_char_star("hello"), x), 1, x) != 5)
    ABORT("CORD_str should find substring in middle");

  /* Substring is not found. */
  x = CORD_from_char_star("hello world");
  if (CORD_str(x, 0, CORD_from_char_star("xyz")) != CORD_NOT_FOUND)
    ABORT("CORD_str should not find non-existent substring");

  /* Find from some offset. */
  x = CORD_from_char_star("hello hello");
  if (CORD_str(x, 1, CORD_from_char_star("hello")) != 6)
    ABORT("CORD_str should find substring with start offset");

  /* Find empty cord. */
  x = CORD_from_char_star("hello");
  if (CORD_str(x, 0, CORD_EMPTY) != 0)
    ABORT("CORD_str should find empty substring at start");

  /* Find a substring longer than cord. */
  x = CORD_from_char_star("hi");
  if (CORD_str(x, 0, CORD_from_char_star("hello")) != CORD_NOT_FOUND)
    ABORT("CORD_str should not find substring longer than cord");

  /* Find in a concatenated cord. */
  x = CORD_cat(CORD_from_char_star("hello"), CORD_from_char_star(" world"));
  if (CORD_str(x, 0, CORD_from_char_star("world")) != 6)
    ABORT("CORD_str should find substring in concatenated cord");
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
test_prev(void)
{
  CORD x;
  size_t i, len;
  CORD_pos p;

  x = "hello";
  len = CORD_len(x);
  CORD_set_pos(p, x, len - 1);

  for (i = 0; i < len; i++) {
    if (!CORD_pos_valid(p))
      ABORT("Position became invalid unexpectedly in prev test");
    if (CORD_pos_fetch(p) != x[len - 1 - i])
      ABORT("CORD_prev character mismatch in simple string");
    if (i < len - 1)
      CORD_prev(p);
  }
  if (CORD_pos_to_index(p) != 0)
    ABORT("Invalid result of CORD_pos_to_index");
  if (CORD_pos_to_cord(p) != x)
    ABORT("Cord returned by CORD_pos_to_cord is wrong");

  CORD_prev(p);
  if (CORD_pos_valid(p))
    ABORT("Position should be invalid before beginning");

  CORD_set_pos(p, CORD_cat("hello", " world"), 5);
  CORD_prev(p);
  if (!CORD_pos_valid(p))
    ABORT("Position should be valid at concatenation boundary (prev)");
  if (CORD_pos_fetch(p) != 'o')
    ABORT("CORD_prev failed at concatenation boundary");

  x = CORD_cat(CORD_nul(3),
               CORD_from_fn(fn_get_char, (void *)(GC_uintptr_t)15, 45));
  x = CORD_cat(x, CORD_nul(2));
  CORD_set_pos(p, x, 49);

  for (i = 0; i <= 49 && CORD_pos_valid(p); i++) {
    char c = CORD_pos_fetch(p);

    if (c != (char)(i < 2 || i > 46 ? '\0' : 'A' + (46 - i) % ('Z' - 'A' + 1)))
      ABORT("CORD_prev character mismatch in function node");
    if (i < 49)
      CORD_prev(p);
  }

  x = CORD_substr(x, 10, 20);
  CORD_set_pos(p, x, 19);
  for (i = 0; i <= 19 && CORD_pos_valid(p); i++) {
    if (CORD_pos_fetch(p) != (char)('A' + (26 - i) % ('Z' - 'A' + 1)))
      ABORT("CORD_prev character mismatch in substring");
    if (i < 19)
      CORD_prev(p);
  }
}

static char
fn_char_at(size_t i, void *client_data)
{
  return ((char *)client_data)[i];
}

static void
test_substr(void)
{
  CORD func_cord, long_substr, nested_substr;
  CORD long_data, long_func_cord, second_substr;
  int i;
  char buf[64];

  for (i = 0; i < (int)sizeof(buf) - 1; i++)
    buf[i] = (char)('0' + i);
  buf[i] = '\0';

  func_cord = CORD_from_fn(fn_char_at, buf, sizeof(buf) - 1);
  long_substr = CORD_substr(func_cord, 0, sizeof(buf) - 1);
  nested_substr = CORD_substr(long_substr, 5, 360);
  if (CORD_len(nested_substr) != strlen(buf) - 5)
    ABORT("Incorrect nested substring length");

  long_data = CORD_EMPTY;
  for (i = 0; i < 20; i++)
    long_data = CORD_cat(long_data, buf);

  long_func_cord = CORD_from_fn(fn_char_at, CORD_to_char_star(long_data),
                                CORD_len(long_data));
  second_substr = CORD_substr(CORD_substr(long_func_cord, 0, 400), 10, 360);
  if (second_substr == CORD_EMPTY)
    ABORT("CORD_substr returned NULL for nested substring with long data");
  if (CORD_len(second_substr) != 360)
    ABORT("Incorrect nested substring length with long data");
  if (CORD_fetch(second_substr, 100)
      != (char)('0' + (100 + 10) % (sizeof(buf) - 1)))
    ABORT("Incorrect nested substring has invalid character");
}

static void
test_dump(void)
{
#ifndef TEST_COVERAGE
  /* Skip testing of `CORD_dump()` to suppress test output. */
#  define CORD_dump(x) (void)(x)
#endif
  CORD x = "CORD";

  CORD_dump(CORD_cat(x, " dump"));
  CORD_dump(CORD_EMPTY);
  CORD_dump(CORD_cat(x, CORD_chars('.', 30)));
  x = CORD_from_fn(fn_get_char, (void *)(GC_uintptr_t)15, 50);
  CORD_dump(x);
#undef CORD_dump
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
  test_cat_char();
  test_cat_char_star();
  test_to_const_char_star();
  test_cord_str();
  test_prev();
  test_substr();
  test_dump();

  GC_gcollect(); /*< to close `f2` before the file removal */
  if (remove(FNAME2) != 0) {
    fprintf(stderr, "WARNING: remove failed: " FNAME2 "\n");
  }
  CORD_fprintf(stdout, "SUCCEEDED\n");
  return 0;
}
