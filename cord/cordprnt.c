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

/*
 * A `sprintf` implementation that understands cords.  This is probably
 * not terribly portable.  It assumes an ANSI platform `stdarg.h` file.
 * It further assumes that I can make copies of `va_list` variables,
 * and read arguments repeatedly by applying `va_arg` to the copies.
 * This could be avoided at some performance cost.
 * We also assume that unsigned and signed integers of various kinds
 * have the same sizes, and can be cast back and forth.
 * We assume that `void *` and `char *` have the same size.
 * All this cruft is needed because we want to rely on the underlying
 * `sprintf` implementation whenever possible.
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "gc/gc.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#ifndef CORD_BUILD
#  define CORD_BUILD
#endif
#ifndef CORD_DONT_DECLARE_OOM_FN
/* Avoid `CORD_oom_fn` symbol redefinition by MinGW. */
#  define CORD_DONT_DECLARE_OOM_FN
#endif
#include "gc/cord.h"
#include "gc/ec.h"

/* Maximum length of a single conversion specification. */
#define CONV_SPEC_LEN 50

/* Maximum length of any conversion with the default width and precision. */
#define CONV_RESULT_LEN 50

#if defined(CPPCHECK)
#  define MACRO_BLKSTMT_BEGIN {
#  define MACRO_BLKSTMT_END }
#else
#  define MACRO_BLKSTMT_BEGIN do {
#  define MACRO_BLKSTMT_END \
    }                       \
    while (0)
#endif

#define OUT_OF_MEMORY                 \
  MACRO_BLKSTMT_BEGIN                 \
  CORD__call_oom_fn();                \
  fprintf(stderr, "Out of memory\n"); \
  abort();                            \
  MACRO_BLKSTMT_END

static size_t
ec_len(CORD_ec x)
{
  return CORD_len(x[0].ec_cord) + (size_t)(x[0].ec_bufptr - x[0].ec_buf);
}

/* Possible non-numeric precision values. */
#define NONE -1
#define VARIABLE -2

/*
 * Copy the conversion specification from `CORD_pos` into the
 * buffer `buf`.  Return a negative value on error.
 * `source` initially points one past the leading "%" symbol;
 * it is left-pointing at the conversion type.  Assign width and
 * precision fields to `*width` and `*prec`, respectively.  If the
 * width or precision is specified as "*", then the corresponding
 * variable is assigned.  Set `*left` to 1 if left adjustment flag
 * is present.  Set `*long_arg` to 1 if `long` flag ("l" or "L") is
 * present, or to -1 if "h" is present, or to 2 if "z" is present.
 */
static int
extract_conv_spec(CORD_pos source, char *buf, int *width, int *prec, int *left,
                  int *long_arg)
{
  int result = 0;
  int current_number = 0;
  int saw_period = 0;
  int saw_number = 0;
  int chars_so_far = 0;
  char current;

  *width = NONE;
  buf[chars_so_far++] = '%';
  while (CORD_pos_valid(source)) {
    if (chars_so_far >= CONV_SPEC_LEN)
      return -1;
    current = CORD_pos_fetch(source);
    buf[chars_so_far++] = current;
    switch (current) {
    case '*':
      saw_number = 1;
      current_number = VARIABLE;
      break;
    case '0':
      if (!saw_number) {
        /* Zero-fill flag; ignore it. */
        break;
      }
      current_number *= 10;
      break;
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    case '8':
    case '9':
      saw_number = 1;
      current_number *= 10;
      current_number += current - '0';
      break;
    case '.':
      saw_period = 1;
      if (saw_number) {
        *width = current_number;
        saw_number = 0;
      }
      current_number = 0;
      break;
    case 'l':
    case 'L':
      *long_arg = 1;
      current_number = 0;
      break;
    case 'z':
      *long_arg = 2;
      current_number = 0;
      break;
    case 'h':
      *long_arg = -1;
      current_number = 0;
      break;
    case ' ':
    case '+':
    case '#':
      current_number = 0;
      break;
    case '-':
      *left = 1;
      current_number = 0;
      break;
    case 'd':
    case 'i':
    case 'o':
    case 'u':
    case 'x':
    case 'X':
    case 'f':
    case 'e':
    case 'E':
    case 'g':
    case 'G':
    case 'c':
    case 'C':
    case 's':
    case 'S':
    case 'p':
    case 'n':
    case 'r':
      goto done;
    default:
      return -1;
    }
    CORD_next(source);
  }
  return -1;
done:
  if (saw_number) {
    if (saw_period) {
      *prec = current_number;
    } else {
      *prec = NONE;
      *width = current_number;
    }
  } else {
    *prec = NONE;
  }
  buf[chars_so_far] = '\0';
  return result;
}

#if defined(__DJGPP__) || defined(__STRICT_ANSI__)
/* `vsnprintf` is missing in DJGPP (v2.0.3). */
#  define GC_VSNPRINTF(buf, bufsz, format, args) vsprintf(buf, format, args)
#elif defined(_MSC_VER)
#  if defined(_WIN32_WCE)
/* `_vsnprintf` is deprecated in WinCE. */
#    define GC_VSNPRINTF StringCchVPrintfA
#  else
#    define GC_VSNPRINTF _vsnprintf
#  endif
#else
#  define GC_VSNPRINTF vsnprintf
#endif

int
CORD_vsprintf(CORD *out, CORD format, va_list args)
{
  CORD_ec result;
  int count;
  char current;
  CORD_pos pos;
  char conv_spec[CONV_SPEC_LEN + 1];

#if defined(CPPCHECK)
  memset(pos, '\0', sizeof(CORD_pos));
#endif
  CORD_ec_init(result);
  for (CORD_set_pos(pos, format, 0); CORD_pos_valid(pos); CORD_next(pos)) {
    current = CORD_pos_fetch(pos);
    if (current == '%') {
      CORD_next(pos);
      if (!CORD_pos_valid(pos))
        return -1;
      current = CORD_pos_fetch(pos);
      if (current == '%') {
        CORD_ec_append(result, current);
      } else {
        int width, prec;
        int left_adj = 0;
        int long_arg = 0;
        CORD arg;
        size_t len;

        if (extract_conv_spec(pos, conv_spec, &width, &prec, &left_adj,
                              &long_arg)
            < 0) {
          return -1;
        }
        current = CORD_pos_fetch(pos);
        switch (current) {
        case 'n':
          /* Assign the length to next argument. */
          if (long_arg == 0) {
            unsigned *pos_ptr = va_arg(args, unsigned *);
            *pos_ptr = (unsigned)ec_len(result);
          } else if (long_arg == 2) {
            size_t *pos_ptr = va_arg(args, size_t *);
            *pos_ptr = ec_len(result);
          } else if (long_arg > 0) {
            unsigned long *pos_ptr = va_arg(args, unsigned long *);
            *pos_ptr = (unsigned long)ec_len(result);
          } else {
            unsigned short *pos_ptr = va_arg(args, unsigned short *);
            *pos_ptr = (unsigned short)ec_len(result);
          }
          goto done;
        case 'r':
          /* Append the cord and padding, if any. */
          if (width == VARIABLE)
            width = va_arg(args, int);
          if (prec == VARIABLE)
            prec = va_arg(args, int);
          arg = va_arg(args, CORD);
          len = CORD_len(arg);
          if (prec != NONE && len > (unsigned)prec) {
            if (prec < 0)
              return -1;
            arg = CORD_substr(arg, 0, (unsigned)prec);
            len = (unsigned)prec;
          }
          if (width != NONE && len < (unsigned)width) {
            char *blanks = (char *)GC_MALLOC_ATOMIC((unsigned)width - len + 1);

            if (NULL == blanks)
              OUT_OF_MEMORY;
            memset(blanks, ' ', (unsigned)width - len);
            blanks[(unsigned)width - len] = '\0';
            if (left_adj) {
              arg = CORD_cat(arg, blanks);
            } else {
              arg = CORD_cat(blanks, arg);
            }
          }
          CORD_ec_append_cord(result, arg);
          goto done;
        case 'c':
          if (width == NONE && prec == NONE) {
            char c;

            c = (char)va_arg(args, int);
            CORD_ec_append(result, c);
            goto done;
          }
          break;
        case 's':
          if (width == NONE && prec == NONE) {
            const char *str = va_arg(args, char *);
            char c;

            while ((c = *str++) != '\0') {
              CORD_ec_append(result, c);
            }
            goto done;
          }
          break;
        default:
          break;
        }
        /* Use standard `sprintf` to perform the conversion. */
        {
          char *buf;
          va_list vsprintf_args;
          int max_size = 0;
          int res = 0;

#if defined(CPPCHECK)
          va_copy(vsprintf_args, args);
#elif defined(__va_copy)
          __va_copy(vsprintf_args, args);
#elif defined(__GNUC__) && !defined(__DJGPP__) \
    && !defined(__EMX__) /*< and probably in other cases */
          va_copy(vsprintf_args, args);
#else
          vsprintf_args = args;
#endif
          if (width == VARIABLE)
            width = va_arg(args, int);
          if (prec == VARIABLE)
            prec = va_arg(args, int);
          if (width != NONE)
            max_size = width;
          if (prec != NONE && prec > max_size)
            max_size = prec;
          max_size += CONV_RESULT_LEN;
          if (max_size >= CORD_BUFSZ) {
            buf = (char *)GC_MALLOC_ATOMIC((unsigned)max_size + 1);
            if (NULL == buf)
              OUT_OF_MEMORY;
          } else {
            if (CORD_BUFSZ - (result[0].ec_bufptr - result[0].ec_buf)
                < max_size) {
              CORD_ec_flush_buf(result);
            }
            buf = result[0].ec_bufptr;
          }
          switch (current) {
          case 'd':
          case 'i':
          case 'o':
          case 'u':
          case 'x':
          case 'X':
          case 'c':
            if (long_arg <= 0) {
              (void)va_arg(args, int);
            } else if (long_arg == 2) {
              (void)va_arg(args, size_t);
            } else /* `long_arg == 1` */ {
              (void)va_arg(args, long);
            }
            break;
          case 's':
          case 'p':
            (void)va_arg(args, char *);
            break;
          case 'f':
          case 'e':
          case 'E':
          case 'g':
          case 'G':
            (void)va_arg(args, double);
            break;
          default:
            res = -1;
          }
          if (0 == res)
            res = GC_VSNPRINTF(buf, (unsigned)max_size + 1, conv_spec,
                               vsprintf_args);
#if defined(CPPCHECK) || defined(__va_copy) \
    || (defined(__GNUC__) && !defined(__DJGPP__) && !defined(__EMX__))
          va_end(vsprintf_args);
#endif
          len = (unsigned)res;
          if ((GC_uintptr_t)len == (GC_uintptr_t)buf) {
            /* Old-style `vsprintf`. */
            len = strlen(buf);
          } else if (res < 0) {
            return -1;
          }
          if (buf != result[0].ec_bufptr) {
            char c;

            while ((c = *buf++) != '\0') {
              CORD_ec_append(result, c);
            }
          } else {
            result[0].ec_bufptr = buf + len;
          }
        }
      done:;
      }
    } else {
      CORD_ec_append(result, current);
    }
  }
  count = (int)ec_len(result);
  *out = CORD_balance(CORD_ec_to_cord(result));
  return count;
}

int
CORD_sprintf(CORD *out, CORD format, ...)
{
  va_list args;
  int result;

  va_start(args, format);
  result = CORD_vsprintf(out, format, args);
  va_end(args);
  return result;
}

int
CORD_fprintf(FILE *f, CORD format, ...)
{
  va_list args;
  int result;
  CORD out = CORD_EMPTY; /*< initialized to prevent a compiler warning */

  va_start(args, format);
  result = CORD_vsprintf(&out, format, args);
  va_end(args);
  if (result > 0)
    CORD_put(out, f);
  return result;
}

int
CORD_vfprintf(FILE *f, CORD format, va_list args)
{
  int result;
  CORD out = CORD_EMPTY;

  result = CORD_vsprintf(&out, format, args);
  if (result > 0)
    CORD_put(out, f);
  return result;
}

int
CORD_printf(CORD format, ...)
{
  va_list args;
  int result;
  CORD out = CORD_EMPTY;

  va_start(args, format);
  result = CORD_vsprintf(&out, format, args);
  va_end(args);
  if (result > 0)
    CORD_put(out, stdout);
  return result;
}

int
CORD_vprintf(CORD format, va_list args)
{
  int result;
  CORD out = CORD_EMPTY;

  result = CORD_vsprintf(&out, format, args);
  if (result > 0)
    CORD_put(out, stdout);
  return result;
}
