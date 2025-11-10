/*
 * Copyright (c) 1994 by Xerox Corporation.  All rights reserved.
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

#ifndef DE_WIN_H
#define DE_WIN_H

#include "de_cmds.h"

#define OTHER_FLAG 0x100
#define EDIT_CMD_FLAG 0x200
#define REPEAT_FLAG 0x400

#define CHAR_CMD(i) ((i) & (0xff))

/* `MENU`: `DE` */
#define IDM_FILESAVE (EDIT_CMD_FLAG + WRITE)
#define IDM_FILEEXIT (OTHER_FLAG + 1)
#define IDM_HELPABOUT (OTHER_FLAG + 2)
#define IDM_HELPCONTENTS (OTHER_FLAG + 3)

#define IDM_EDITPDOWN (REPEAT_FLAG + EDIT_CMD_FLAG + DOWN)
#define IDM_EDITPUP (REPEAT_FLAG + EDIT_CMD_FLAG + UP)
#define IDM_EDITUNDO (EDIT_CMD_FLAG + UNDO)
#define IDM_EDITLOCATE (EDIT_CMD_FLAG + LOCATE)
#define IDM_EDITDOWN (EDIT_CMD_FLAG + DOWN)
#define IDM_EDITUP (EDIT_CMD_FLAG + UP)
#define IDM_EDITLEFT (EDIT_CMD_FLAG + LEFT)
#define IDM_EDITRIGHT (EDIT_CMD_FLAG + RIGHT)
#define IDM_EDITBS (EDIT_CMD_FLAG + BS)
#define IDM_EDITDEL (EDIT_CMD_FLAG + DEL)
#define IDM_EDITREPEAT (EDIT_CMD_FLAG + REPEAT)
#define IDM_EDITTOP (EDIT_CMD_FLAG + TOP)

/* Screen dimensions.  Maintained by `de_win.c` file. */
extern int LINES;
extern int COLS;

/* File being edited. */
extern char *arg_file_name;

/* The following calls are from `de_win.c` file to `de.c` one. */

/* Get the contents (`CORD`) of `i`-th screen line.  Relies on `COLS`. */
const void *retrieve_screen_line(int i);

/*
 * Change the current position to the given column (`x`) and row (`y`).
 * Upper left of window is (0,0).
 */
void set_position(int x, int y);

/* The following calls are from `de.c` file to `de_win.c` one. */

/*
 * Physically move the cursor on the display, so that it appears at
 * given `column` and row (`line`).
 */
void move_cursor(int column, int line);

/* Invalidate given row (`line`) on the screen. */
void invalidate_line(int line);

/* Display error message.  The trailing "\n" is not needed. */
void de_error(const char *s);

#endif /* DE_WIN_H */
