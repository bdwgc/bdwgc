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

#ifndef DE_CMDS_H
#define DE_CMDS_H

#define UP 16     /*< ^P */
#define DOWN 14   /*< ^N */
#define LEFT 2    /*< ^B */
#define RIGHT 6   /*< ^F */
#define DEL 127   /*< ^? */
#define BS 8      /*< ^H */
#define UNDO 21   /*< ^U */
#define WRITE 23  /*< ^W */
#define QUIT 4    /*< ^D */
#define REPEAT 18 /*< ^R */
#define LOCATE 12 /*< ^L */
#define TOP 20    /*< ^T */

#define HELP_CONTENT_STR                              \
  "Cursor keys: ^B(left) ^F(right) ^P(up) ^N(down)\n" \
  "Undo: ^U    Write to <file_name>.new: ^W\n"        \
  "Quit: ^D    Repeat count: ^R[n]\n"                 \
  "Top: ^T     Locate (search, find): ^L text ^L"

/*
 * Execute an editor command.  The argument may be an integer greater
 * 255 denoting a windows command, one of the control characters, or
 * another ASCII character to be used as either a character to be inserted,
 * a repeat count, or a search string, depending on the current state.
 */
void do_command(int);

/* OS-independent initialization. */
void generic_init(void);

/* Check whether the program is running in the non-interactive mode. */
int is_prompt_disabled(void);

#endif /* DE_CMDS_H */
