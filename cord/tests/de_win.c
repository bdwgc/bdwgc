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

/*
 * The Windows-specific part of `de`.  This has been started as the generic
 * Windows application template but its significant parts did not survive
 * to the final version.
 */

#if defined(__CYGWIN__) || defined(__MINGW32__)                 \
    || (defined(__NT__) && defined(__386__)) || defined(_WIN32) \
    || defined(WIN32)

#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN 1
#  endif
#  define NOSERVICE
#  include <windows.h>

#  if defined(__DMC__)
#    define INT_PTR int
#  endif

#  include <ctype.h>

#  include "gc.h"
#  include "gc/cord.h"

#  include "de_win.h"

int LINES = 0;
int COLS = 0;

#  define szAppName TEXT("DE")

static HWND hwnd;

void
de_error(const char *s)
{
  if (is_prompt_disabled()) {
    fprintf(stderr, "%s\n", s);
    return;
  }

  (void)MessageBoxA(hwnd, s, "Demonstration Editor",
                    MB_ICONINFORMATION | MB_OK);
  InvalidateRect(hwnd, NULL, TRUE);
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam,
                                LPARAM lParam);

int APIENTRY
WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR command_line,
        int nCmdShow)
{
  MSG msg;
  WNDCLASS wndclass;
  HACCEL hAccel;

  GC_set_find_leak(0);
  GC_INIT();
#  ifndef NO_INCREMENTAL
  GC_enable_incremental();
#  endif
#  if defined(CPPCHECK)
  GC_noop1((GC_word)(GC_uintptr_t)(&WinMain));
#  endif

  if (!hPrevInstance) {
    wndclass.style = CS_HREDRAW | CS_VREDRAW;
    wndclass.lpfnWndProc = WndProc;
    wndclass.cbClsExtra = 0;
    wndclass.cbWndExtra = DLGWINDOWEXTRA;
    wndclass.hInstance = hInstance;
    wndclass.hIcon = LoadIcon(hInstance, szAppName);
    wndclass.hCursor = LoadCursor(NULL, IDC_ARROW);
    wndclass.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
    wndclass.lpszMenuName = TEXT("DE");
    wndclass.lpszClassName = szAppName;

    if (RegisterClass(&wndclass) == 0) {
      de_error("RegisterClass error");
      return 1;
    }
  }

  /* Empirically, the command line does not include the command name... */
  if (command_line == 0 || *command_line == 0) {
    de_error("File name argument required");
    return 0;
  } else {
    char *p = command_line;

    while (*p != 0 && !isspace(*(unsigned char *)p))
      p++;
    arg_file_name
        = CORD_to_char_star(CORD_substr(command_line, 0, p - command_line));
  }

  hwnd = CreateWindow(
      szAppName, TEXT("Demonstration Editor") /* `lpWindowName` */,
      WS_OVERLAPPEDWINDOW | WS_CAPTION /* `dwStyle` */,
      CW_USEDEFAULT /* `x` */, 0 /* `y` */, CW_USEDEFAULT /* `nWidth` */,
      0 /* `nHeight` */, NULL /* `hWndParent` */, NULL /* `hMenu` */,
      hInstance, NULL /* `lpParam` */);
  if (NULL == hwnd) {
    de_error("CreateWindow error");
    return 1;
  }

  ShowWindow(hwnd, nCmdShow);
  if (is_prompt_disabled()) {
    /* Exit not waiting for the user input. */
    return 0;
  }

  hAccel = LoadAccelerators(hInstance, szAppName);
  while (GetMessage(&msg, NULL, 0, 0)) {
    if (!TranslateAccelerator(hwnd, hAccel, &msg)) {
      TranslateMessage(&msg);
      DispatchMessage(&msg);
    }
  }
  return (int)msg.wParam;
}

/* Return the argument with all control characters replaced by blanks. */
static char *
plain_chars(const char *text, size_t len)
{
  char *result = (char *)GC_MALLOC_ATOMIC(len + 1);
  size_t i;

  if (NULL == result)
    return NULL;
  for (i = 0; i < len; i++) {
    if (iscntrl(((unsigned char *)text)[i])) {
      result[i] = ' ';
    } else {
      result[i] = text[i];
    }
  }
  result[len] = '\0';
  return result;
}

/*
 * Return the argument with all non-control-characters replaced by blank,
 * and all control characters `c` replaced by `c + 64`.
 */
static char *
control_chars(const char *text, size_t len)
{
  char *result = (char *)GC_MALLOC_ATOMIC(len + 1);
  size_t i;

  if (NULL == result)
    return NULL;
  for (i = 0; i < len; i++) {
    if (iscntrl(((unsigned char *)text)[i])) {
      result[i] = (char)(text[i] + 0x40);
    } else {
      result[i] = ' ';
    }
  }
  result[len] = '\0';
  return result;
}

static int char_width;
static int char_height;

static void
get_line_rect(int line_arg, int win_width, RECT *rectp)
{
  rectp->top = line_arg * (LONG)char_height;
  rectp->bottom = rectp->top + char_height;
  rectp->left = 0;
  rectp->right = win_width;
}

/* A flag whether the caret is currently visible. */
static int caret_visible = 0;

/* A flag whether the screen has been painted at least once. */
static int screen_was_painted = 0;

static void update_cursor(void);

static INT_PTR CALLBACK
AboutBoxCallback(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
  (void)lParam;
  switch (message) {
  case WM_INITDIALOG:
    SetFocus(GetDlgItem(hDlg, IDOK));
    break;

  case WM_COMMAND:
    switch (wParam) {
    case IDOK:
      EndDialog(hDlg, TRUE);
      break;
    }
    break;

  case WM_CLOSE:
    EndDialog(hDlg, TRUE);
    return TRUE;
  }
  return FALSE;
}

static LRESULT CALLBACK
WndProc(HWND hwnd_arg, UINT message, WPARAM wParam, LPARAM lParam)
{
  static HINSTANCE hInstance;
  HDC dc;
  PAINTSTRUCT ps;
  RECT client_area;
  RECT this_line;
  RECT dummy;
  TEXTMETRIC tm;
  int i;
  int id;

  switch (message) {
  case WM_CREATE:
    hInstance = ((LPCREATESTRUCT)lParam)->hInstance;
    dc = GetDC(hwnd_arg);
    SelectObject(dc, GetStockObject(SYSTEM_FIXED_FONT));
    GetTextMetrics(dc, &tm);
    ReleaseDC(hwnd_arg, dc);
    char_width = tm.tmAveCharWidth;
    char_height = tm.tmHeight + tm.tmExternalLeading;
    GetClientRect(hwnd_arg, &client_area);
    COLS = (client_area.right - client_area.left) / char_width;
    LINES = (client_area.bottom - client_area.top) / char_height;
    generic_init();
    return 0;

  case WM_CHAR:
    if (wParam == QUIT) {
      SendMessage(hwnd_arg, WM_CLOSE, 0, 0L);
    } else {
      do_command((int)wParam);
    }
    return 0;

  case WM_SETFOCUS:
    CreateCaret(hwnd_arg, NULL, char_width, char_height);
    ShowCaret(hwnd_arg);
    caret_visible = 1;
    update_cursor();
    return 0;

  case WM_KILLFOCUS:
    HideCaret(hwnd_arg);
    DestroyCaret();
    caret_visible = 0;
    return 0;

  case WM_LBUTTONUP:
    {
      unsigned xpos = LOWORD(lParam); /*< from left */
      unsigned ypos = HIWORD(lParam); /*< from top */

      set_position(xpos / (unsigned)char_width, ypos / (unsigned)char_height);
    }
    return 0;

  case WM_COMMAND:
    id = LOWORD(wParam);
    if (id & EDIT_CMD_FLAG) {
      if (id & REPEAT_FLAG)
        do_command(REPEAT);
      do_command(CHAR_CMD(id));
      return 0;
    }
    switch (id) {
    case IDM_FILEEXIT:
      SendMessage(hwnd_arg, WM_CLOSE, 0, 0L);
      return 0;
    case IDM_HELPABOUT:
      if (DialogBox(hInstance, TEXT("ABOUTBOX"), hwnd_arg, AboutBoxCallback)) {
        InvalidateRect(hwnd_arg, NULL, TRUE);
      }
      return 0;
    case IDM_HELPCONTENTS:
      /* Not an error, just show a message box. */
      de_error(HELP_CONTENT_STR);
      return 0;
    }
    break;

  case WM_CLOSE:
    DestroyWindow(hwnd_arg);
    return 0;

  case WM_DESTROY:
    PostQuitMessage(0);
#  ifndef GC_NO_DEINIT
    GC_win32_free_heap();
#  endif
    return 0;

  case WM_PAINT:
    dc = BeginPaint(hwnd_arg, &ps);
    GetClientRect(hwnd_arg, &client_area);
    COLS = (client_area.right - client_area.left) / char_width;
    LINES = (client_area.bottom - client_area.top) / char_height;
    SelectObject(dc, GetStockObject(SYSTEM_FIXED_FONT));
    for (i = 0; i < LINES; i++) {
      get_line_rect(i, client_area.right, &this_line);
      if (IntersectRect(&dummy, &this_line, &ps.rcPaint)) {
        CORD raw_line = (CORD)retrieve_screen_line(i);
        size_t len = CORD_len(raw_line);
        const char *text = CORD_to_char_star(raw_line);
        /* May contain embedded NUL characters. */
        char *plain = plain_chars(text, len);
        char *blanks = CORD_to_char_star(CORD_chars(' ', COLS - len));
        char *control = control_chars(text, len);
        if (NULL == plain || NULL == control)
          de_error("Out of memory!");

#  define RED RGB(255, 0, 0)

        SetBkMode(dc, OPAQUE);
        SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));

        if (plain != NULL)
          TextOutA(dc, this_line.left, this_line.top, plain, (int)len);
        TextOutA(dc, this_line.left + (int)len * char_width, this_line.top,
                 blanks, (int)(COLS - len));
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RED);
        if (control != NULL)
          TextOutA(dc, this_line.left, this_line.top, control,
                   (int)strlen(control));
      }
    }
    EndPaint(hwnd_arg, &ps);
    screen_was_painted = 1;
    return 0;
  }
  return DefWindowProc(hwnd_arg, message, wParam, lParam);
}

static int last_col;
static int last_line;

void
move_cursor(int c, int l)
{
  last_col = c;
  last_line = l;

  if (caret_visible)
    update_cursor();
}

static void
update_cursor(void)
{
  SetCaretPos(last_col * char_width, last_line * char_height);
  ShowCaret(hwnd);
}

void
invalidate_line(int i)
{
  RECT line_r;

  if (!screen_was_painted) {
    /*
     * Invalidating a rectangle before painting seems result in a major
     * performance problem.
     */
    return;
  }
  get_line_rect(i, COLS * char_width, &line_r);
  InvalidateRect(hwnd, &line_r, FALSE);
}

#else

/*
 * ANSI C does not allow translation units to be empty.
 * So we guarantee this one is nonempty.
 */
extern int GC_quiet_de_win;

#endif /* !WIN32 */
