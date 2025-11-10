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
 * A really simple-minded text editor based on cords.
 *
 * Things it does right:
 *   - No size bounds;
 *   - Unbounded undo;
 *   - Should not crash no matter what file you invoke it on;
 *   - Scrolls horizontally.
 *
 * Things it does wrong:
 *   - It does not handle tabs reasonably (use `expand` first);
 *   - The command set is *much* too small;
 *   - The redisplay algorithm does not let `curses` do the scrolling;
 *   - The rule for moving the window over the file is suboptimal.
 */

#include <stdio.h>
#include <stdlib.h> /*< for `exit` */

#include "gc.h"
#include "gc/cord.h"

#include <ctype.h>

#if (defined(__CYGWIN__) || defined(__MINGW32__)                  \
     || (defined(__NT__) && defined(__386__)) || defined(_WIN32)) \
    && !defined(WIN32)
#  define WIN32
#endif

#if defined(WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN 1
#  endif
#  define NOSERVICE
#  include <windows.h>
#else
#  include <curses.h>
#  include <unistd.h> /*< for `sleep` */
#  define de_error(s)             \
    {                             \
      fprintf(stderr, "%s\n", s); \
      sleep(2);                   \
    }
#endif

#ifdef WIN32
#  include "de_win.h"
#endif

#include "de_cmds.h"

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
  fprintf(stderr, "Out of memory\n"); \
  exit(3);                            \
  MACRO_BLKSTMT_END

/*
 * List of line number to position mappings, in descending order.
 * There may be holes.
 */
struct LineMapRep {
  int line;
  size_t pos;
  struct LineMapRep *previous;
};
typedef struct LineMapRep *line_map;

/* List of file versions, one per edit operation. */
struct HistoryRep {
  CORD file_contents;
  struct HistoryRep *previous;
  line_map map; /*< note: this is invalid for the first record `now` */
};
typedef struct HistoryRep *history;

static history now = NULL;

/* This is `now -> file_contents`. */
static CORD current;

/* The current file length. */
static size_t current_len;

/* Map of current line number to position. */
static line_map current_map = NULL;

/*
 * Number of `current_map` entries.  Not always accurate, but reset
 * by `prune_map`.
 */
static size_t current_map_size = 0;

#define MAX_MAP_SIZE 3000

/* Current display position. */
static int dis_line = 0;
static int dis_col = 0;

#define ALL -1
#define NONE -2
static int need_redisplay = 0; /*< line that needs to be redisplayed */

/* Current cursor position. Always within file. */
static int line = 0;
static int col = 0;

/* Character position corresponding to cursor. */
static size_t file_pos = 0;

/* Invalidate line map for lines greater than `i`. */
static void
invalidate_map(int i)
{
  for (;;) {
    if (NULL == current_map)
      exit(4); /*< for CSA, should not happen */
    if (current_map->line <= i)
      break;
    current_map = current_map->previous;
    current_map_size--;
  }
}

/*
 * Reduce the number of map entries to save space for huge files.
 * This also affects maps in histories.
 */
static void
prune_map(void)
{
  line_map map = current_map;
  int start_line = map->line;

  current_map_size = 0;
  do {
    current_map_size++;
    if (map->line < start_line - LINES && map->previous != 0) {
      line_map pred = map->previous->previous;

      GC_PTR_STORE_AND_DIRTY(&map->previous, pred);
    }
    map = map->previous;
  } while (map != 0);
}

/* Add mapping entry. */
static void
add_map(int line_arg, size_t pos)
{
  line_map new_map = GC_NEW(struct LineMapRep);
  line_map cur_map;

  if (NULL == new_map)
    OUT_OF_MEMORY;
  if (current_map_size >= MAX_MAP_SIZE)
    prune_map();
  new_map->line = line_arg;
  new_map->pos = pos;
  cur_map = current_map;
  GC_PTR_STORE_AND_DIRTY(&new_map->previous, cur_map);
  current_map = new_map;
  current_map_size++;
}

/*
 * Return position of column `*c` of `i`-th line in the current file.
 * Adjust `*c` to be within the line.  A `NULL` pointer is taken as
 * column zero.  Returns `CORD_NOT_FOUND` if `i` is too big.
 * Assumes `i` is greater than `dis_line`.
 */
static size_t
line_pos(int i, int *c)
{
  int j;
  size_t cur;
  line_map map = current_map;

  while (map->line > i)
    map = map->previous;
  if (map->line < i - 2) /*< rebuild */
    invalidate_map(i);
  for (j = map->line, cur = map->pos; j < i;) {
    cur = CORD_chr(current, cur, '\n');
    if (cur == current_len - 1)
      return CORD_NOT_FOUND;
    cur++;
    if (++j > current_map->line)
      add_map(j, cur);
  }
  if (c != 0) {
    size_t next = CORD_chr(current, cur, '\n');

    if (next == CORD_NOT_FOUND)
      next = current_len - 1;
    if (next < cur + *c) {
      *c = (int)(next - cur);
    }
    cur += *c;
  }
  return cur;
}

static void
add_hist(CORD s)
{
  history new_file = GC_NEW(struct HistoryRep);

  if (NULL == new_file)
    OUT_OF_MEMORY;
  new_file->file_contents = current = s;
  current_len = CORD_len(s);
  new_file->previous = now;
  GC_END_STUBBORN_CHANGE(new_file);
  if (now != NULL) {
    now->map = current_map;
    GC_END_STUBBORN_CHANGE(now);
  }
  now = new_file;
}

static void
del_hist(void)
{
  now = now->previous;
  current = now->file_contents;
  current_map = now->map;
  current_len = CORD_len(current);
}

#ifndef WIN32
/* Current screen contents; a dynamically allocated array of cords. */
static CORD *screen = NULL;
static int screen_size = 0;

/*
 * Replace a line in the `stdscr` of `curses` package.  All control
 * characters are displayed as upper case characters in standout mode.
 * This is not terribly appropriate for tabs.
 */
static void
replace_line(int i, CORD s)
{
  size_t len = CORD_len(s);

  if (NULL == screen || LINES > screen_size) {
    screen_size = LINES;
    screen = (CORD *)GC_MALLOC(screen_size * sizeof(CORD));
    if (NULL == screen)
      OUT_OF_MEMORY;
  }

  /* A gross workaround for an apparent `curses` bug. */
  if (i == LINES - 1 && len == (unsigned)COLS) {
    s = CORD_substr(s, 0, len - 1);
  }

  if (CORD_cmp(screen[i], s) != 0) {
    CORD_pos p;

    move(i, 0);
    clrtoeol();
    move(i, 0);

#  if defined(CPPCHECK)
    memset(p, '\0', sizeof(CORD_pos));
#  endif
    CORD_FOR(p, s)
    {
      int c = CORD_pos_fetch(p) & 0x7f;

      if (iscntrl(c)) {
        standout();
        addch(c + 0x40);
        standend();
      } else {
        addch(c);
      }
    }
    GC_PTR_STORE_AND_DIRTY(screen + i, s);
  }
}
#else
#  define replace_line(i, s) invalidate_line(i)
#endif

/*
 * Return up to `COLS` characters of the line of `s` starting at `pos`,
 * returning only characters after the given `column`.
 */
static CORD
retrieve_line(CORD s, size_t pos, unsigned column)
{
  /* Avoid scanning very long lines. */
  CORD candidate = CORD_substr(s, pos, column + COLS);
  size_t eol = CORD_chr(candidate, 0, '\n');
  int len;

  if (eol == CORD_NOT_FOUND)
    eol = CORD_len(candidate);
  len = (int)eol - (int)column;
  if (len < 0)
    len = 0;
  return CORD_substr(s, pos + column, len);
}

#ifdef WIN32
#  define refresh() (void)0

const void *
retrieve_screen_line(int i)
{
  size_t pos;

  /* Prune the search. */
  invalidate_map(dis_line + LINES);

  pos = line_pos(dis_line + i, 0);
  if (pos == CORD_NOT_FOUND)
    return CORD_EMPTY;
  return retrieve_line(current, pos, dis_col);
}
#endif

/* Display the visible section of the current file. */
static void
redisplay(void)
{
  int i;

  /* Prune the search. */
  invalidate_map(dis_line + LINES);

  for (i = 0; i < LINES; i++) {
    if (need_redisplay == ALL || need_redisplay == i) {
      size_t pos = line_pos(dis_line + i, 0);

      if (pos == CORD_NOT_FOUND)
        break;
      replace_line(i, retrieve_line(current, pos, dis_col));
      if (need_redisplay == i)
        goto done;
    }
  }
  for (; i < LINES; i++)
    replace_line(i, CORD_EMPTY);
done:
  refresh();
  need_redisplay = NONE;
}

static int dis_granularity;

/*
 * Update `dis_line`, `dis_col` and `dis_pos` to make cursor visible.
 * Assumes `line`, `col`, `dis_line` and `dis_pos` are in bounds.
 */
static void
normalize_display(void)
{
  int old_line = dis_line;
  int old_col = dis_col;

  dis_granularity = 1;
  if (LINES > 15 && COLS > 15)
    dis_granularity = 2;
  while (dis_line > line)
    dis_line -= dis_granularity;
  while (dis_col > col)
    dis_col -= dis_granularity;
  while (line >= dis_line + LINES)
    dis_line += dis_granularity;
  while (col >= dis_col + COLS)
    dis_col += dis_granularity;
  if (old_line != dis_line || old_col != dis_col) {
    need_redisplay = ALL;
  }
}

#if defined(WIN32)
/* Defined in `de_win.c` file. */
#else
#  define move_cursor(x, y) move(y, x)
#endif

/*
 * Adjust display so that cursor is visible; move cursor into position.
 * Update `screen` if necessary.
 */
static void
fix_cursor(void)
{
  normalize_display();
  if (need_redisplay != NONE)
    redisplay();
  move_cursor(col - dis_col, line - dis_line);
  refresh();
#ifndef WIN32
  fflush(stdout);
#endif
}

/*
 * Make sure `line`, `col` and `dis_pos` are somewhere inside file.
 * Recompute `file_pos`.  Assumes `dis_pos` is accurate or past the
 * end of file.
 */
static void
fix_pos(void)
{
  int my_col = col;

  if ((size_t)line > current_len)
    line = (int)current_len;
  file_pos = line_pos(line, &my_col);
  if (file_pos == CORD_NOT_FOUND) {
    for (line = current_map->line, file_pos = current_map->pos;
         file_pos < current_len;
         line++, file_pos = CORD_chr(current, file_pos, '\n') + 1)
      ;
    line--;
    file_pos = line_pos(line, &col);
  } else {
    col = my_col;
  }
}

#if defined(WIN32)
#  define beep() Beep(1000 /* Hz */, 300 /* ms */)
#else
/*
 * `beep()` is part of some `curses` packages and not others.
 * We try to match the type of the built-in one, if any.
 * Declared in the platform `curses.h` file.
 */
int
beep(void)
{
  putc('\007', stderr);
  return 0;
}
#endif

#define NO_PREFIX -1
#define BARE_PREFIX -2
static int repeat_count = NO_PREFIX; /*< the current command prefix */

static int locate_mode = 0; /*< currently between 2 ^Ls */

static CORD locate_string = CORD_EMPTY; /*< the current search string */

char *arg_file_name;

#ifdef WIN32
void
set_position(int c, int l)
{
  line = l + dis_line;
  col = c + dis_col;
  fix_pos();
  move_cursor(col - dis_col, line - dis_line);
}
#endif

void
do_command(int c)
{
  int i;
  int need_fix_pos;
  FILE *out;

  if (c == '\r')
    c = '\n';
  if (locate_mode) {
    size_t new_pos;

    if (c == LOCATE) {
      locate_mode = 0;
      locate_string = CORD_EMPTY;
      return;
    }
    locate_string = CORD_cat_char(locate_string, (char)c);
    new_pos = CORD_str(current, file_pos - CORD_len(locate_string) + 1,
                       locate_string);
    if (new_pos != CORD_NOT_FOUND) {
      need_redisplay = ALL;
      new_pos += CORD_len(locate_string);
      for (;;) {
        file_pos = line_pos(line + 1, 0);
        if (file_pos > new_pos)
          break;
        line++;
      }
      col = (int)(new_pos - line_pos(line, 0));
      file_pos = new_pos;
      fix_cursor();
    } else {
      locate_string
          = CORD_substr(locate_string, 0, CORD_len(locate_string) - 1);
      beep();
    }
    return;
  }
  if (c == REPEAT) {
    repeat_count = BARE_PREFIX;
    return;
  } else if (c < 0x100 && isdigit(c)) {
    if (repeat_count == BARE_PREFIX) {
      repeat_count = c - '0';
      return;
    } else if (repeat_count != NO_PREFIX) {
      repeat_count = 10 * repeat_count + c - '0';
      return;
    }
  }
  if (repeat_count == NO_PREFIX)
    repeat_count = 1;
  if (repeat_count == BARE_PREFIX && (c == UP || c == DOWN)) {
    repeat_count = LINES - dis_granularity;
  }
  if (repeat_count == BARE_PREFIX)
    repeat_count = 8;
  need_fix_pos = 0;
  for (i = 0; i < repeat_count; i++) {
    switch (c) {
    case LOCATE:
      locate_mode = 1;
      break;
    case TOP:
      line = col = 0;
      file_pos = 0;
      break;
    case UP:
      if (line != 0) {
        line--;
        need_fix_pos = 1;
      }
      break;
    case DOWN:
      line++;
      need_fix_pos = 1;
      break;
    case LEFT:
      if (col != 0) {
        col--;
        file_pos--;
      }
      break;
    case RIGHT:
      if (CORD_fetch(current, file_pos) == '\n')
        break;
      col++;
      file_pos++;
      break;
    case UNDO:
      del_hist();
      need_redisplay = ALL;
      need_fix_pos = 1;
      break;
    case BS:
      if (col == 0) {
        beep();
        break;
      }
      col--;
      file_pos--;
      /* FALLTHRU */
    case DEL:
      if (file_pos == current_len - 1)
        break;
      /* Cannot delete a trailing newline. */
      if (CORD_fetch(current, file_pos) == '\n') {
        need_redisplay = ALL;
        need_fix_pos = 1;
      } else {
        need_redisplay = line - dis_line;
      }
      add_hist(CORD_cat(CORD_substr(current, 0, file_pos),
                        CORD_substr(current, file_pos + 1, current_len)));
      invalidate_map(line);
      break;
    case WRITE:
      {
        CORD name = CORD_cat(CORD_from_char_star(arg_file_name), ".new");

        if ((out = fopen(CORD_to_const_char_star(name), "wb")) == NULL
            || CORD_put(current, out) == EOF) {
          de_error("Write failed");
          need_redisplay = ALL;
        } else {
          fclose(out);
        }
      }
      break;
    default:
      {
        CORD left_part = CORD_substr(current, 0, file_pos);
        CORD right_part = CORD_substr(current, file_pos, current_len);

        add_hist(CORD_cat(CORD_cat_char(left_part, (char)c), right_part));
        invalidate_map(line);
        if (c == '\n') {
          col = 0;
          line++;
          file_pos++;
          need_redisplay = ALL;
        } else {
          col++;
          file_pos++;
          need_redisplay = line - dis_line;
        }
        break;
      }
    }
  }
  if (need_fix_pos)
    fix_pos();
  fix_cursor();
  repeat_count = NO_PREFIX;
}

void
generic_init(void)
{
  FILE *f;
  CORD initial;

  if ((f = fopen(arg_file_name, "rb")) == NULL) {
    initial = "\n";
  } else {
    size_t len;

    initial = CORD_from_file(f);
    len = CORD_len(initial);
    if (0 == len || CORD_fetch(initial, len - 1) != '\n') {
      initial = CORD_cat(initial, "\n");
    }
  }
  add_map(0, 0);
  add_hist(initial);
  now->map = current_map;
  /* Cannot back up further: beginning of the world. */
  now->previous = now;

  GC_END_STUBBORN_CHANGE(now);
  need_redisplay = ALL;
  fix_cursor();
}

#ifndef WIN32
int
main(int argc, char **argv)
{
  int c;
  void *buf;

  /* The application is not for testing leak detection mode. */
  GC_set_find_leak(0);

  GC_INIT();
#  ifndef NO_INCREMENTAL
  GC_enable_incremental();
#  endif

  if (argc != 2) {
    fprintf(stderr, "Usage: %s file\n", argv[0]);
    fprintf(stderr, "Cursor keys: ^B(left) ^F(right) ^P(up) ^N(down)\n");
    fprintf(stderr, "Undo: ^U    Write to <file>.new: ^W");
    fprintf(stderr, "Quit:^D     Repeat count: ^R[n]\n");
    fprintf(stderr, "Top: ^T     Locate (search, find): ^L text ^L\n");
    exit(1);
  }
  arg_file_name = argv[1];
  buf = GC_MALLOC_ATOMIC(8192);
  if (NULL == buf)
    OUT_OF_MEMORY;
  setvbuf(stdout, (char *)buf, _IOFBF, 8192);
  initscr();
  noecho();
  nonl();
  cbreak();
  generic_init();
  while ((c = getchar()) != QUIT) {
    if (c == EOF)
      break;
    do_command(c);
  }
  move(LINES - 1, 0);
  clrtoeol();
  refresh();
  nl();
  echo();
  endwin();
  return 0;
}
#endif
