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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif
#ifndef CORD_BUILD
#  define CORD_BUILD
#endif

#include "gc/gc.h"

#include <stdlib.h>
#include <string.h>

#include "gc/cord.h"

/*
 * An implementation of the `cord` primitives.  These are the only
 * functions that understand the representation.  We perform only
 * minimal checks on arguments to these functions.  Out of bounds
 * arguments to the iteration functions may result in client functions
 * invoked on garbage data.  In most cases, client functions should be
 * programmed defensively enough that this does not result in memory
 * smashes.
 */

CORD_oom_fn_t CORD_oom_fn = 0;

void
CORD_set_oom_fn(CORD_oom_fn_t fn)
{
  CORD_oom_fn = fn;
}

CORD_oom_fn_t
CORD_get_oom_fn(void)
{
  return CORD_oom_fn;
}

void
CORD__call_oom_fn(void)
{
  if (CORD_oom_fn != 0)
    (*CORD_oom_fn)();
}

#define OUT_OF_MEMORY       \
  {                         \
    CORD__call_oom_fn();    \
    ABORT("Out of memory"); \
  }

#define ABORT(msg)                \
  {                               \
    fprintf(stderr, "%s\n", msg); \
    abort();                      \
  }

/* A structure representing a concatenation of two strings.
 * It is assumed that both of them are not empty.
 */
struct Concatenation {
  CORD left;
  CORD right;
};

struct Function {
  CORD_fn fn;
  void *client_data;
};

struct Generic {
  char nul;
  char header;
  /* Concatenation nesting depth; 0 for function. */
  char depth;
  /*
   * Length of left-concatenated child if it is sufficiently short;
   * 0 otherwise.
   */
  unsigned char left_len;
  unsigned long len;
};

union ConcatOrFunc {
  struct Concatenation concat;
  struct Function function;
};

typedef struct {
  struct Generic generic;
  union ConcatOrFunc data;
} CordRep;

#define CONCAT_HDR 1

#define FN_HDR 4

/*
 * Substring nodes are a special case of function nodes.
 * The `client_data` field is known to point to a `substr_args`
 * structure, and the function is either `CORD_apply_access_fn`
 * or `CORD_index_access_fn`.
 */
#define SUBSTR_HDR 6

/* The following may be applied only to function and concatenation nodes: */
#define IS_CONCATENATION(s) \
  (((const CordRep *)s)->generic.header == CONCAT_HDR)

#define IS_FUNCTION(s) ((((const CordRep *)s)->generic.header & FN_HDR) != 0)

#define IS_SUBSTR(s) (((const CordRep *)s)->generic.header == SUBSTR_HDR)

#define LEN(s) (((const CordRep *)s)->generic.len)
#define DEPTH(s) (((const CordRep *)s)->generic.depth)
#define GEN_LEN(s) (CORD_IS_STRING(s) ? strlen(s) : LEN(s))

#define MAX_LEFT_LEN 255

#define LEFT_LEN(s)                                                    \
  (((const CordRep *)s)->generic.left_len != 0                         \
       ? ((const CordRep *)s)->generic.left_len                        \
       : (CORD_IS_STRING(((const CordRep *)s)->data.concat.left)       \
              ? ((const CordRep *)s)->generic.len                      \
                    - GEN_LEN(((const CordRep *)s)->data.concat.right) \
              : LEN(((const CordRep *)s)->data.concat.left)))

/* Cords shorter than this are C strings. */
#define SHORT_LIMIT (sizeof(CordRep) - 1)

/*
 * Dump the internal representation of `x` to `stdout`, with initial
 * indentation level `n`.
 */
static void
CORD_dump_inner(CORD x, unsigned n)
{
  size_t i;

  for (i = 0; i < (size_t)n; i++) {
    fputs("  ", stdout);
  }
  if (x == 0) {
    fputs("NIL\n", stdout);
  } else if (CORD_IS_STRING(x)) {
    for (i = 0; i <= SHORT_LIMIT; i++) {
      if (x[i] == '\0')
        break;
      putchar(x[i]);
    }
    if (x[i] != '\0')
      fputs("...", stdout);
    putchar('\n');
  } else if (IS_CONCATENATION(x)) {
    const struct Concatenation *conc = &((const CordRep *)x)->data.concat;

    printf("Concatenation: %p (len: %d, depth: %d)\n", (const void *)x,
           (int)LEN(x), (int)DEPTH(x));
    CORD_dump_inner(conc->left, n + 1);
    CORD_dump_inner(conc->right, n + 1);
  } else /* function */ {
    const struct Function *f = &((const CordRep *)x)->data.function;
    size_t lim = (size_t)LEN(x);

    if (IS_SUBSTR(x))
      printf("(Substring) ");
    printf("Function: %p (len: %d): ", (const void *)x, (int)lim);
    for (i = 0; i < 20 && i < lim; i++) {
      putchar(f->fn(i, f->client_data));
    }
    if (i < lim)
      fputs("...", stdout);
    putchar('\n');
  }
}

void
CORD_dump(CORD x)
{
  CORD_dump_inner(x, 0);
  fflush(stdout);
}

CORD
CORD_cat_char_star(CORD x, const char *y, size_t leny)
{
  size_t result_len;
  size_t lenx;
  int depth;

  if (x == CORD_EMPTY)
    return y;
  if (leny == 0)
    return x;
  if (CORD_IS_STRING(x)) {
    lenx = strlen(x);
    result_len = lenx + leny;
    if (result_len <= SHORT_LIMIT) {
      char *result = (char *)GC_MALLOC_ATOMIC(result_len + 1);

      if (NULL == result)
        OUT_OF_MEMORY;
#ifdef LINT2
      memcpy(result, x, lenx + 1);
#else
      /*
       * No need to copy the terminating zero as `result[lenx]` is
       * written below.
       */
      memcpy(result, x, lenx);
#endif
      memcpy(result + lenx, y, leny);
      result[result_len] = '\0';
      return (CORD)result;
    } else {
      depth = 1;
    }
  } else {
    CORD right;
    CORD left;
    char *new_right;

    lenx = LEN(x);

    if (leny <= SHORT_LIMIT / 2 && IS_CONCATENATION(x)
        && CORD_IS_STRING(right = ((const CordRep *)x)->data.concat.right)) {
      size_t right_len;

      /* Merge `y` into right part of `x`. */
      left = ((const CordRep *)x)->data.concat.left;
      if (!CORD_IS_STRING(left)) {
        right_len = lenx - LEN(left);
      } else if (((const CordRep *)x)->generic.left_len != 0) {
        right_len = lenx - ((const CordRep *)x)->generic.left_len;
      } else {
        right_len = strlen(right);
      }
      result_len = right_len + leny; /*< length of `new_right` */
      if (result_len <= SHORT_LIMIT) {
        new_right = (char *)GC_MALLOC_ATOMIC(result_len + 1);
        if (new_right == 0)
          OUT_OF_MEMORY;
        memcpy(new_right, right, right_len);
        memcpy(new_right + right_len, y, leny);
        new_right[result_len] = '\0';
        y = new_right;
        leny = result_len;
        x = left;
        lenx -= right_len;
        /* Now fall through to concatenate the two pieces: */
      }
      if (CORD_IS_STRING(x)) {
        depth = 1;
      } else {
        depth = DEPTH(x) + 1;
      }
    } else {
      depth = DEPTH(x) + 1;
    }
    result_len = lenx + leny;
  }
  {
    /* The general case; `lenx` and `result_len` are known. */
    CordRep *result = GC_NEW(CordRep);

    if (NULL == result)
      OUT_OF_MEMORY;
    result->generic.header = CONCAT_HDR;
    result->generic.depth = (char)depth;
    if (lenx <= MAX_LEFT_LEN)
      result->generic.left_len = (unsigned char)lenx;
    result->generic.len = (unsigned long)result_len;
    result->data.concat.left = x;
    GC_PTR_STORE_AND_DIRTY((/* no const */ void *)&result->data.concat.right,
                           y);
    GC_reachable_here(x);
    if (depth >= CORD_MAX_DEPTH) {
      return CORD_balance((CORD)result);
    } else {
      return (CORD)result;
    }
  }
}

CORD
CORD_cat(CORD x, CORD y)
{
  size_t result_len;
  int depth;
  size_t lenx;

  if (x == CORD_EMPTY)
    return y;
  if (y == CORD_EMPTY)
    return x;
  if (CORD_IS_STRING(y)) {
    return CORD_cat_char_star(x, y, strlen(y));
  } else if (CORD_IS_STRING(x)) {
    lenx = strlen(x);
    depth = DEPTH(y) + 1;
  } else {
    int depthy = DEPTH(y);

    lenx = LEN(x);
    depth = DEPTH(x) + 1;
    if (depthy >= depth)
      depth = depthy + 1;
  }
  result_len = lenx + LEN(y);
  {
    CordRep *result = GC_NEW(CordRep);

    if (NULL == result)
      OUT_OF_MEMORY;
    result->generic.header = CONCAT_HDR;
    result->generic.depth = (char)depth;
    if (lenx <= MAX_LEFT_LEN)
      result->generic.left_len = (unsigned char)lenx;
    result->generic.len = (unsigned long)result_len;
    result->data.concat.left = x;
    GC_PTR_STORE_AND_DIRTY((/* no const */ void *)&result->data.concat.right,
                           y);
    GC_reachable_here(x);
    if (depth >= CORD_MAX_DEPTH) {
      return CORD_balance((CORD)result);
    } else {
      return (CORD)result;
    }
  }
}

static CordRep *
CORD_from_fn_inner(CORD_fn fn, void *client_data, size_t len)
{
  if (0 == len)
    return NULL;
  if (len <= SHORT_LIMIT) {
    char *result;
    size_t i;
    char buf[SHORT_LIMIT + 1];

    for (i = 0; i < len; i++) {
      char c = fn(i, client_data);

      if (c == '\0')
        goto gen_case;
      buf[i] = c;
    }

    result = (char *)GC_MALLOC_ATOMIC(len + 1);
    if (NULL == result)
      OUT_OF_MEMORY;
    memcpy(result, buf, len);
    result[len] = '\0';
    return (CordRep *)result;
  }
gen_case:
  {
    CordRep *result = GC_NEW(CordRep);

    if (NULL == result)
      OUT_OF_MEMORY;
    result->generic.header = FN_HDR;
    /* The depth is already zero. */
    result->generic.len = (unsigned long)len;
    result->data.function.fn = fn;
    GC_PTR_STORE_AND_DIRTY(&result->data.function.client_data, client_data);
    return result;
  }
}

CORD
CORD_from_fn(CORD_fn fn, void *client_data, size_t len)
{
  return (/* const */ CORD)CORD_from_fn_inner(fn, client_data, len);
}

size_t
CORD_len(CORD x)
{
  return x == 0 ? 0 : GEN_LEN(x);
}

struct substr_args {
  CordRep *sa_cord;
  size_t sa_index;
};

static char
CORD_index_access_fn(size_t i, void *client_data)
{
  struct substr_args *descr = (struct substr_args *)client_data;

  return ((char *)descr->sa_cord)[i + descr->sa_index];
}

static char
CORD_apply_access_fn(size_t i, void *client_data)
{
  struct substr_args *descr = (struct substr_args *)client_data;
  struct Function *fn_cord = &descr->sa_cord->data.function;

  return fn_cord->fn(i + descr->sa_index, fn_cord->client_data);
}

/*
 * A variant of `CORD_substr` that simply returns a function node,
 * thus postponing its work.  `f` argument is a function that may
 * be used for efficient access to the `i`-th character.
 * Assumes `i >= 0` and `i + n < CORD_len(x)`.
 */
static CORD
CORD_substr_closure(CORD x, size_t i, size_t n, CORD_fn f)
{
  struct substr_args *sa = GC_NEW(struct substr_args);
  CordRep *result;

  if (sa == 0)
    OUT_OF_MEMORY;
  sa->sa_index = i;
  GC_PTR_STORE_AND_DIRTY(&sa->sa_cord, x);
  result = CORD_from_fn_inner(f, sa, n);
  if ((CORD)result != CORD_EMPTY && 0 == result->generic.nul)
    result->generic.header = SUBSTR_HDR;
  return (CORD)result;
}

/*
 * Substrings of function nodes and flat strings shorter than
 * this are flat strings.  Otherwise we use a functional
 * representation, which is significantly slower to access.
 */
#define SUBSTR_LIMIT (10 * SHORT_LIMIT)

/*
 * A variant of `CORD_substr` that assumes `i >= 0`, `n > 0` and
 * `i + n < CORD_len(x)`.
 */
static CORD
CORD_substr_checked(CORD x, size_t i, size_t n)
{
  if (CORD_IS_STRING(x)) {
    if (n > SUBSTR_LIMIT) {
      return CORD_substr_closure(x, i, n, CORD_index_access_fn);
    } else {
      char *result = (char *)GC_MALLOC_ATOMIC(n + 1);

      if (NULL == result)
        OUT_OF_MEMORY;
      strncpy(result, x + i, n);
      result[n] = '\0';
      return result;
    }
  } else if (IS_CONCATENATION(x)) {
    const struct Concatenation *conc = &((const CordRep *)x)->data.concat;
    size_t left_len = LEFT_LEN(x);
    size_t right_len = (size_t)LEN(x) - left_len;

    if (i >= left_len) {
      if (n == right_len)
        return conc->right;
      return CORD_substr_checked(conc->right, i - left_len, n);
    } else if (i + n <= left_len) {
      if (n == left_len)
        return conc->left;
      return CORD_substr_checked(conc->left, i, n);
    } else {
      /* Need at least one character from each side. */
      CORD left_part;
      CORD right_part;
      size_t left_part_len = left_len - i;

      if (i == 0) {
        left_part = conc->left;
      } else {
        left_part = CORD_substr_checked(conc->left, i, left_part_len);
      }
      if (i + n == right_len + left_len) {
        right_part = conc->right;
      } else {
        right_part = CORD_substr_checked(conc->right, 0, n - left_part_len);
      }
      return CORD_cat(left_part, right_part);
    }
  } else /* function */ {
    if (n > SUBSTR_LIMIT) {
      if (IS_SUBSTR(x)) {
        /* Avoid nesting substring nodes. */
        const struct Function *f = &((const CordRep *)x)->data.function;
        const struct substr_args *descr = (struct substr_args *)f->client_data;

        return CORD_substr_closure((CORD)descr->sa_cord, i + descr->sa_index,
                                   n, f->fn);
      } else {
        return CORD_substr_closure(x, i, n, CORD_apply_access_fn);
      }
    } else {
      char *result;
      const struct Function *f = &((const CordRep *)x)->data.function;
      char buf[SUBSTR_LIMIT + 1];
      char *p = buf;
      size_t j;
      size_t lim = i + n;

      for (j = i; j < lim; j++) {
        char c = f->fn(j, f->client_data);

        if (c == '\0') {
          return CORD_substr_closure(x, i, n, CORD_apply_access_fn);
        }
        *p++ = c;
      }
      result = (char *)GC_MALLOC_ATOMIC(n + 1);
      if (NULL == result)
        OUT_OF_MEMORY;
      memcpy(result, buf, n);
      result[n] = '\0';
      return result;
    }
  }
}

CORD
CORD_substr(CORD x, size_t i, size_t n)
{
  size_t len = CORD_len(x);

  if (i >= len || 0 == n)
    return 0;
  if (i + n > len)
    n = len - i;
  return CORD_substr_checked(x, i, n);
}

int
CORD_iter5(CORD x, size_t i, CORD_iter_fn f1, CORD_batched_iter_fn f2,
           void *client_data)
{
  if (0 == x)
    return 0;
  if (CORD_IS_STRING(x)) {
    const char *p = x + i;

    if (*p == '\0')
      ABORT("2nd arg to CORD_iter5 too big");
    if (f2 != CORD_NO_FN) {
      return f2(p, client_data);
    } else {
      while (*p) {
        if (f1(*p, client_data))
          return 1;
        p++;
      }
      return 0;
    }
  } else if (IS_CONCATENATION(x)) {
    const struct Concatenation *conc = &((const CordRep *)x)->data.concat;

    if (i > 0) {
      size_t left_len = LEFT_LEN(x);

      if (i >= left_len) {
        return CORD_iter5(conc->right, i - left_len, f1, f2, client_data);
      }
    }
    if (CORD_iter5(conc->left, i, f1, f2, client_data)) {
      return 1;
    }
    return CORD_iter5(conc->right, 0, f1, f2, client_data);
  } else /* function */ {
    const struct Function *f = &((const CordRep *)x)->data.function;
    size_t j;
    size_t lim = (size_t)LEN(x);

    for (j = i; j < lim; j++) {
      if (f1(f->fn(j, f->client_data), client_data)) {
        return 1;
      }
    }
    return 0;
  }
}

#undef CORD_iter
int
CORD_iter(CORD x, CORD_iter_fn f1, void *client_data)
{
  return CORD_iter5(x, 0, f1, CORD_NO_FN, client_data);
}

int
CORD_riter4(CORD x, size_t i, CORD_iter_fn f1, void *client_data)
{
  if (0 == x)
    return 0;
  if (CORD_IS_STRING(x)) {
    const char *p = x + i;

    for (;;) {
      char c = *p;

      if (c == '\0')
        ABORT("2nd arg to CORD_riter4 too big");
      if (f1(c, client_data))
        return 1;
      if (p == x)
        break;
      p--;
    }
  } else if (IS_CONCATENATION(x)) {
    const struct Concatenation *conc = &((const CordRep *)x)->data.concat;
    CORD left_part = conc->left;
    size_t left_len = LEFT_LEN(x);

    if (i >= left_len) {
      if (CORD_riter4(conc->right, i - left_len, f1, client_data)) {
        return 1;
      }
      return CORD_riter4(left_part, left_len - 1, f1, client_data);
    } else {
      return CORD_riter4(left_part, i, f1, client_data);
    }
  } else /* function */ {
    const struct Function *f = &((const CordRep *)x)->data.function;
    size_t j;

    for (j = i;; j--) {
      if (f1(f->fn(j, f->client_data), client_data)) {
        return 1;
      }
      if (0 == j)
        break;
    }
  }
  return 0;
}

int
CORD_riter(CORD x, CORD_iter_fn f1, void *client_data)
{
  size_t len = CORD_len(x);
  if (0 == len)
    return 0;
  return CORD_riter4(x, len - 1, f1, client_data);
}

/*
 * The following functions are concerned with balancing cords.
 * Strategy:
 * Scan the cord from left to right, keeping the cord scanned so far
 * as a forest of balanced trees of exponentially decreasing length.
 * When a new subtree needs to be added to the forest, we concatenate all
 * shorter ones to the new tree in the appropriate order, and then insert
 * the result into the forest.
 * Crucial invariants:
 *   1. The concatenation of the forest (in decreasing order) with the
 *      unscanned part of the rope is equal to the rope being balanced;
 *   2. All trees in the forest are balanced;
 *   3. `forest[i]` has depth at most `i`.
 */

typedef struct {
  CORD c;
  /* The actual length of `c`. */
  size_t len;
} ForestElement;

static size_t min_len[CORD_MAX_DEPTH];

static int min_len_init = 0;

/*
 * The string is the concatenation of `forest` in order of decreasing indices.
 * `forest[i].len >= fib(i + 1)` is assumed.
 */
typedef ForestElement Forest[CORD_MAX_DEPTH];

static void
CORD_init_min_len(void)
{
  int i;
  size_t last, previous;

  min_len[0] = previous = 1;
  min_len[1] = last = 2;
  for (i = 2; i < CORD_MAX_DEPTH; i++) {
    size_t current = last < (~(size_t)0) - previous ? last + previous
                                                    : last /* overflow */;

    min_len[i] = current;
    previous = last;
    last = current;
  }
  min_len_init = 1;
}

static void
CORD_init_forest(ForestElement *forest, size_t max_len)
{
  int i;

  for (i = 0; i < CORD_MAX_DEPTH; i++) {
    forest[i].c = 0;
    if (min_len[i] > max_len)
      return;
  }
  ABORT("Cord too long");
}

/*
 * Add a leaf to the appropriate level in `forest`, cleaning out lower
 * levels as necessary.  Also works if `x` is a balanced tree of
 * concatenations; however in this case an extra concatenation node
 * may be inserted above `x`; this node should not be counted in the
 * statement of the invariants.
 */
static void
CORD_add_forest(ForestElement *forest, CORD x, size_t len)
{
  int i = 0;
  CORD sum = CORD_EMPTY;
  size_t sum_len = 0;

  while (len > min_len[i + 1]) {
    if (forest[i].c != 0) {
      sum = CORD_cat(forest[i].c, sum);
      sum_len += forest[i].len;
      forest[i].c = 0;
    }
    i++;
  }
  /*
   * `sum` has depth at most 1 greater than what would be required for the
   * balance.
   */
  sum = CORD_cat(sum, x);
  sum_len += len;
  /*
   * If `x` was a leaf, then `sum` is now balanced.  To see this, consider
   * the two cases in which `forest[i - 1]` either is or is not empty.
   */
  while (sum_len >= min_len[i]) {
    if (forest[i].c != 0) {
      sum = CORD_cat(forest[i].c, sum);
      sum_len += forest[i].len;
      /*
       * This is again balanced, since `sum` was balanced, and has
       * allowable depth that differs from `i` by at most 1.
       */
      forest[i].c = 0;
    }
    i++;
  }
  i--;
  forest[i].c = sum;
  forest[i].len = sum_len;
}

static CORD
CORD_concat_forest(ForestElement *forest, size_t expected_len)
{
  int i = 0;
  CORD sum = 0;
  size_t sum_len = 0;

  while (sum_len != expected_len) {
    if (forest[i].c != 0) {
      sum = CORD_cat(forest[i].c, sum);
      sum_len += forest[i].len;
    }
    i++;
  }
  return sum;
}

/*
 * Insert the frontier of `x` into `forest`.  Balanced subtrees are treated
 * as leaves.  This potentially adds one to the depth of the final tree.
 */
static void
CORD_balance_insert(CORD x, size_t len, ForestElement *forest)
{
  int depth;

  if (CORD_IS_STRING(x)) {
    CORD_add_forest(forest, x, len);
  } else if (IS_CONCATENATION(x)
             && ((depth = DEPTH(x)) >= CORD_MAX_DEPTH
                 || len < min_len[depth])) {
    const struct Concatenation *conc = &((const CordRep *)x)->data.concat;
    size_t left_len = LEFT_LEN(x);

    CORD_balance_insert(conc->left, left_len, forest);
    CORD_balance_insert(conc->right, len - left_len, forest);
  } else /* function or balanced */ {
    CORD_add_forest(forest, x, len);
  }
}

CORD
CORD_balance(CORD x)
{
  Forest forest;
  size_t len;

  if (0 == x)
    return 0;
  if (CORD_IS_STRING(x))
    return x;
  if (!min_len_init)
    CORD_init_min_len();
  len = LEN(x);
  CORD_init_forest(forest, len);
  CORD_balance_insert(x, len, forest);
  return CORD_concat_forest(forest, len);
}

/* The position primitives. */

/* Private routines to deal with the hard cases only: */

/*
 * `p` contains a prefix of the path to `cur_pos`. Extend it to a full path
 * and set up leaf info.
 */
static void
CORD_extend_path(CORD_pos p)
{
  struct CORD_pe *current_pe = &p[0].path[p[0].path_len];
  CORD top = current_pe->pe_cord;
  size_t pos = p[0].cur_pos;
  size_t top_pos = current_pe->pe_start_pos;
  size_t top_len = GEN_LEN(top);

  /* Fill in the rest of the path. */
  while (!CORD_IS_STRING(top) && IS_CONCATENATION(top)) {
    const struct Concatenation *conc = &((const CordRep *)top)->data.concat;
    size_t left_len;

    left_len = LEFT_LEN(top);
    current_pe++;
    if (pos >= top_pos + left_len) {
      current_pe->pe_cord = top = conc->right;
      current_pe->pe_start_pos = top_pos = top_pos + left_len;
      top_len -= left_len;
    } else {
      current_pe->pe_cord = top = conc->left;
      current_pe->pe_start_pos = top_pos;
      top_len = left_len;
    }
    p[0].path_len++;
  }
  /* Fill in leaf description for fast access. */
  if (CORD_IS_STRING(top)) {
    p[0].cur_leaf = top;
    p[0].cur_start = top_pos;
    p[0].cur_end = top_pos + top_len;
  } else {
    p[0].cur_end = 0;
  }
  if (pos >= top_pos + top_len)
    p[0].path_len = CORD_POS_INVALID;
}

char
CORD__pos_fetch(CORD_pos p)
{
  /* Leaf is a function node. */
  const struct CORD_pe *pe;
  CORD leaf;
  const struct Function *f;

  if (!CORD_pos_valid(p))
    ABORT("CORD_pos_fetch: invalid argument");
  pe = &p[0].path[p[0].path_len];
  leaf = pe->pe_cord;
  if (!IS_FUNCTION(leaf))
    ABORT("CORD_pos_fetch: bad leaf");
  f = &((const CordRep *)leaf)->data.function;
  return f->fn(p[0].cur_pos - pe->pe_start_pos, f->client_data);
}

void
CORD__next(CORD_pos p)
{
  size_t cur_pos = p[0].cur_pos + 1;
  const struct CORD_pe *current_pe;
  CORD leaf;

  if (!CORD_pos_valid(p))
    ABORT("CORD_next: invalid argument");
  current_pe = &p[0].path[p[0].path_len];
  leaf = current_pe->pe_cord;

  /* Leaf is not a string or we are at end of leaf. */
  p[0].cur_pos = cur_pos;
  if (!CORD_IS_STRING(leaf)) {
    /* Function leaf. */
    const struct Function *f = &((const CordRep *)leaf)->data.function;
    size_t start_pos = current_pe->pe_start_pos;
    size_t end_pos = start_pos + (size_t)LEN(leaf);

    if (cur_pos < end_pos) {
      /* Fill cache and return. */
      size_t i;
      size_t limit = CORD_FUNCTION_BUF_SZ;
      CORD_fn fn = f->fn;
      void *client_data = f->client_data;

      if (end_pos - cur_pos < CORD_FUNCTION_BUF_SZ) {
        limit = end_pos - cur_pos;
      }
      for (i = 0; i < limit; i++) {
        p[0].function_buf[i] = fn(i + cur_pos - start_pos, client_data);
      }
      p[0].cur_start = cur_pos;
      p[0].cur_leaf = p[0].function_buf;
      p[0].cur_end = cur_pos + limit;
      return;
    }
  }
  /*
   * End of leaf.  Pop the stack until we find two concatenation nodes with
   * the same start position: this implies we were in left part.
   */
  {
    while (p[0].path_len > 0
           && current_pe[0].pe_start_pos != current_pe[-1].pe_start_pos) {
      p[0].path_len--;
      current_pe--;
    }
    if (p[0].path_len == 0) {
      p[0].path_len = CORD_POS_INVALID;
      return;
    }
  }
  p[0].path_len--;
  CORD_extend_path(p);
}

void
CORD__prev(CORD_pos p)
{
  const struct CORD_pe *pe = &p[0].path[p[0].path_len];

  if (p[0].cur_pos == 0) {
    p[0].path_len = CORD_POS_INVALID;
    return;
  }
  p[0].cur_pos--;
  if (p[0].cur_pos >= pe->pe_start_pos)
    return;

  /* Beginning of leaf. */

  /*
   * Pop the stack until we find two concatenation nodes with the
   * different start position: this implies we were in right part.
   */
  {
    const struct CORD_pe *current_pe = &p[0].path[p[0].path_len];

    while (p[0].path_len > 0
           && current_pe[0].pe_start_pos == current_pe[-1].pe_start_pos) {
      p[0].path_len--;
      current_pe--;
    }
  }
  p[0].path_len--;
  CORD_extend_path(p);
}

#undef CORD_pos_fetch
#undef CORD_next
#undef CORD_prev
#undef CORD_pos_to_index
#undef CORD_pos_to_cord
#undef CORD_pos_valid

char
CORD_pos_fetch(CORD_pos p)
{
  if (p[0].cur_end != 0) {
    return p[0].cur_leaf[p[0].cur_pos - p[0].cur_start];
  } else {
    return CORD__pos_fetch(p);
  }
}

void
CORD_next(CORD_pos p)
{
  if (p[0].cur_pos + 1 < p[0].cur_end) {
    p[0].cur_pos++;
  } else {
    CORD__next(p);
  }
}

void
CORD_prev(CORD_pos p)
{
  if (p[0].cur_end != 0 && p[0].cur_pos > p[0].cur_start) {
    p[0].cur_pos--;
  } else {
    CORD__prev(p);
  }
}

size_t
CORD_pos_to_index(CORD_pos p)
{
  return p[0].cur_pos;
}

CORD
CORD_pos_to_cord(CORD_pos p)
{
  return p[0].path[0].pe_cord;
}

int
CORD_pos_valid(CORD_pos p)
{
  return p[0].path_len != CORD_POS_INVALID;
}

void
CORD_set_pos(CORD_pos p, CORD x, size_t i)
{
  if (x == CORD_EMPTY) {
    p[0].path_len = CORD_POS_INVALID;
    return;
  }
  p[0].path[0].pe_cord = x;
  p[0].path[0].pe_start_pos = 0;
  p[0].path_len = 0;
  p[0].cur_pos = i;
  CORD_extend_path(p);
}
