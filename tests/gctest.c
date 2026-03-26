/*
 * Copyright 1988, 1989 Hans-J. Boehm, Alan J. Demers
 * Copyright (c) 1991-1994 by Xerox Corporation.  All rights reserved.
 * Copyright (c) 1996 by Silicon Graphics.  All rights reserved.
 * Copyright (c) 2009-2025 Ivan Maidanski
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
 * An incomplete test for the garbage collector.  Some more obscure entry
 * points are not tested at all.  This must be compiled with the same flags
 * used to build the collector.  It uses the collector internals to allow
 * more precise results checking for some of the tests.
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#undef GC_BUILD

#if (defined(DBG_HDRS_ALL) || defined(MAKE_BACK_GRAPH)) && !defined(GC_DEBUG) \
    && !defined(CPPCHECK)
#  define GC_DEBUG
#endif

/* In case `DEFAULT_VDB` is specified manually (e.g. passed to `CFLAGS`). */
#ifdef DEFAULT_VDB
#  define TEST_DEFAULT_VDB
#endif

#if defined(CPPCHECK) && defined(GC_PTHREADS) && !defined(_GNU_SOURCE)
#  define _GNU_SOURCE 1
#endif
#ifdef GC_NO_THREADS_DISCOVERY
#  undef GC_NO_THREAD_REDIRECTS
#endif
#include "gc.h"

#include "gc/javaxfc.h"

#if !defined(GC_PTHREADS) && !defined(GC_WIN32_THREADS)
#  undef THREADS
#endif

/* Number of additional threads to fork. */
#ifndef NTHREADS
/*
 * This excludes the main thread, which also runs a test.
 * In the single-threaded case, a number of times to rerun it.
 */
#  define NTHREADS 5
#endif

#if defined(_WIN32_WCE) && !defined(__GNUC__)
#  include <winbase.h>
/* `define assert ASSERT` */
#else
/* Not normally used, but handy for debugging. */
#  include <assert.h>
#endif

#if defined(_DEBUG) && (_MSC_VER >= 1900 /* VS 2015+ */)
#  ifndef _CRTDBG_MAP_ALLOC
#    define _CRTDBG_MAP_ALLOC
#  endif
/*
 * This should be included before `gc_priv.h` file (see the note about
 * `_malloca` redefinition bug in `gcconfig.h` file).
 */
#  include <crtdbg.h> /*< for `_CrtDumpMemoryLeaks`, `_CrtSetDbgFlag` */
#endif

#if (defined(GC_NO_FINALIZATION) || defined(DBG_HDRS_ALL)) \
    && !defined(NO_TYPED_TEST)
#  define NO_TYPED_TEST
#endif

#ifndef NO_TYPED_TEST
#  include "gc/gc_typed.h"
#endif

/* For output, locking, some statistics and `gcconfig.h` file. */
#define NOT_GCBUILD
#include "private/gc_priv.h"

#ifdef TEST_NO_THREADS
#  undef THREADS
#  undef GC_PTHREADS
#  undef GC_WIN32_PTHREADS
#  undef GC_WIN32_THREADS
#  undef THREAD_LOCAL_ALLOC
#endif

#if defined(GC_PRINT_VERBOSE_STATS) || defined(GCTEST_PRINT_VERBOSE)
#  define print_stats VERBOSE
#  define INIT_PRINT_STATS (void)0
#else
/* Use own variable as `GC_print_stats` might not be visible. */
static int print_stats = 0;
#  ifdef GC_READ_ENV_FILE
/* `GETENV()` uses the collector internal function in this case. */
#    define INIT_PRINT_STATS (void)0
#  else
#    define INIT_PRINT_STATS                          \
      do {                                            \
        if (GETENV("GC_PRINT_VERBOSE_STATS") != NULL) \
          print_stats = VERBOSE;                      \
        else if (GETENV("GC_PRINT_STATS") != NULL)    \
          print_stats = 1;                            \
      } while (0)
#  endif
#endif /* !GC_PRINT_VERBOSE_STATS */

#if defined(GC_PTHREADS) && !defined(GC_WIN32_PTHREADS)
#  include <pthread.h>
#endif

#if ((defined(DARWIN) && defined(MPROTECT_VDB) && !defined(THREADS) \
      && !defined(MAKE_BACK_GRAPH) && !defined(TEST_HANDLE_FORK))   \
     || (defined(THREADS) && !defined(CAN_HANDLE_FORK))             \
     || defined(HAVE_NO_FORK) || defined(USE_WINALLOC))             \
    && !defined(NO_TEST_HANDLE_FORK)
#  define NO_TEST_HANDLE_FORK
#endif

#ifndef NO_TEST_HANDLE_FORK
#  include <sys/types.h>
#  include <sys/wait.h>
#  include <unistd.h>
#  if defined(HANDLE_FORK) && defined(CAN_CALL_ATFORK)
/* This causes abort in `GC_init` on `pthread_atfork` failure. */
#    define INIT_FORK_SUPPORT GC_set_handle_fork(1)
#  elif !defined(TEST_FORK_WITHOUT_ATFORK)
/*
 * Note: passing -1 implies `fork()` should be as well manually surrounded
 * with `GC_atfork_prepare`, `GC_atfork_parent` and `GC_atfork_child` calls.
 */
#    define INIT_FORK_SUPPORT GC_set_handle_fork(-1)
#  endif
#endif

#ifndef INIT_FORK_SUPPORT
#  define INIT_FORK_SUPPORT (void)0
#endif

#ifndef THREADS
#  define FINALIZER_LOCK() (void)0
#  define FINALIZER_UNLOCK() (void)0
#elif defined(GC_PTHREADS)
static pthread_mutex_t incr_lock = PTHREAD_MUTEX_INITIALIZER;
#  define FINALIZER_LOCK() pthread_mutex_lock(&incr_lock)
#  define FINALIZER_UNLOCK() pthread_mutex_unlock(&incr_lock)
#else
static CRITICAL_SECTION incr_cs;
#  define FINALIZER_LOCK() EnterCriticalSection(&incr_cs)
#  define FINALIZER_UNLOCK() LeaveCriticalSection(&incr_cs)
#endif

#include <stdarg.h>

#ifdef GC_PTHREADS
#  include <errno.h> /*< for `EAGAIN` */
#endif

#ifdef TEST_MANUAL_VDB
#  define INIT_MANUAL_VDB_ALLOWED GC_set_manual_vdb_allowed(1)
#else
#  define INIT_MANUAL_VDB_ALLOWED GC_set_manual_vdb_allowed(0)
#endif

#ifdef TEST_PAGES_EXECUTABLE
#  define INIT_PAGES_EXECUTABLE GC_set_pages_executable(1)
#else
#  define INIT_PAGES_EXECUTABLE (void)0
#endif

#define CHECK_PACKAGE_VERSION                                \
  do {                                                       \
    if (GC_get_version()                                     \
        != (((GC_VERSION_VAL_T)GC_VERSION_MAJOR << 16)       \
            | (GC_VERSION_MINOR << 8) | GC_VERSION_MICRO)) { \
      GC_printf("GC version mismatch\n");                    \
      exit(4);                                               \
    }                                                        \
  } while (0)

/*
 * Call `GC_INIT()` only on platforms on which we think we really need it,
 * so that we can test automatic initialization on the rest.
 */
#if defined(TEST_EXPLICIT_GC_INIT) || defined(AIX) || defined(HOST_ANDROID) \
    || (defined(MSWINCE) && !defined(GC_WINMAIN_REDIRECT))
#  define GC_OPT_INIT GC_INIT()
#else
#  define GC_OPT_INIT (void)0
#endif

#define INIT_FIND_LEAK       \
  if (!GC_get_find_leak()) { \
  } else                     \
    GC_printf("This test program is not designed for leak detection mode\n")

#ifdef NO_CLOCK
#  define INIT_PERF_MEASUREMENT (void)0
#else
#  define INIT_PERF_MEASUREMENT GC_start_performance_measurement()
#endif

#define GC_COND_INIT()     \
  INIT_FORK_SUPPORT;       \
  INIT_MANUAL_VDB_ALLOWED; \
  INIT_PAGES_EXECUTABLE;   \
  GC_OPT_INIT;             \
  CHECK_PACKAGE_VERSION;   \
  INIT_PRINT_STATS;        \
  INIT_FIND_LEAK;          \
  INIT_PERF_MEASUREMENT

#define TEST_ASSERT(e)                                                   \
  if (!(e)) {                                                            \
    GC_printf("Assertion failure: %s:%d, %s\n", __FILE__, __LINE__, #e); \
    FAIL;                                                                \
  }

#define CHECK_OUT_OF_MEMORY(p)    \
  if (NULL == (p)) {              \
    GC_printf("Out of memory\n"); \
    exit(69);                     \
  }

static void *
checkOOM(void *p)
{
  CHECK_OUT_OF_MEMORY(p);
  return p;
}

/* Define AO primitives for a single-threaded mode. */
#ifndef AO_HAVE_compiler_barrier
/* `AO_t` is not defined. */
#  define AO_t size_t
#endif
#ifndef AO_HAVE_fetch_and_add1
#  define AO_fetch_and_add1(p) ((*(p))++)
/* This is used only to update counters. */
#endif

/* The allocation statistics.  Synchronization is not strictly necessary. */
static AO_t uncollectable_count = 0;
static AO_t collectable_count = 0;
static AO_t atomic_count = 0;
static AO_t realloc_count = 0;

/*
 * Amount of space wasted in `cons` nodes; also used in `gcj_cons`,
 * `mktree` and `chktree` (for other purposes).
 */
static AO_t extra_count = 0;

#if defined(LINT2)
#  define FAIL abort()
#else
#  define FAIL ABORT("Test failed")
#endif

/*
 * `AT_END` may be defined to exercise the interior pointer test if the
 * collector is configured with `ALL_INTERIOR_POINTERS`.  As it stands,
 * this test should succeed with either configuration.  In the `FIND_LEAK`
 * configuration, it should find lots of leaks, since we free almost nothing.
 */

struct SEXPR {
  struct SEXPR *sexpr_car;
  struct SEXPR *sexpr_cdr;
};

typedef struct SEXPR *sexpr;

#define INT_TO_SEXPR(v) ((sexpr)NUMERIC_TO_VPTR(v))
#define SEXPR_TO_INT(p) ((int)(GC_uintptr_t)(p))

#undef nil
#define nil (INT_TO_SEXPR(0))
#define car(x) ((x)->sexpr_car)
#define cdr(x) ((x)->sexpr_cdr)
#define is_nil(x) ((x) == nil)

/*
 * Silly implementation of Lisp "cons".  Intentionally wastes lots of space
 * to test collector.
 */
#ifdef VERY_SMALL_CONFIG
#  define cons small_cons
#else
static sexpr
cons(sexpr x, sexpr y)
{
  sexpr r;
  int *p;
  unsigned my_extra = (unsigned)AO_fetch_and_add1(&extra_count) % 5000;

  r = (sexpr)checkOOM(GC_MALLOC(sizeof(struct SEXPR) + my_extra));
  AO_fetch_and_add1(&collectable_count);
  for (p = (int *)r;
       GC_ADDR_LT((char *)p, (char *)r + my_extra + sizeof(struct SEXPR));
       p++) {
    TEST_ASSERT(0 == *p);
    *p = (int)((13 << 11) + ((p - (int *)r) & 0xfff));
  }
#  ifdef AT_END
  r = (sexpr)((char *)r + (my_extra & ~7U));
#  endif
  r->sexpr_car = x;
  GC_PTR_STORE_AND_DIRTY(&r->sexpr_cdr, y);
  GC_reachable_here(x);
  return r;
}
#endif /* !VERY_SMALL_CONFIG */

#include "gc/gc_mark.h"

#ifdef GC_GCJ_SUPPORT
#  include "gc/gc_gcj.h"

/* The following structure emulates the "vtable" in `gcj`. */
struct fake_vtable {
  /* A class pointer in the real `gcj`. */
  char dummy[GC_GCJ_MARK_DESCR_OFFSET];
  GC_word descr;
};

/* A length-based descriptor. */
const struct fake_vtable gcj_class_struct1
    = { { 0 },
        (sizeof(struct SEXPR) + sizeof(struct fake_vtable *)) | GC_DS_LENGTH };

/* A bitmap-based descriptor. */
const struct fake_vtable gcj_class_struct2
    = { { 0 }, ((GC_word)3 << (CPP_WORDSZ - 3)) | GC_DS_BITMAP };

static struct GC_ms_entry *GC_CALLBACK
fake_gcj_mark_proc(GC_word *addr, struct GC_ms_entry *mark_stack_top,
                   struct GC_ms_entry *mark_stack_limit, GC_word env)
{
  sexpr x;

  if (1 == env) {
    /* Object allocated by the debug allocator. */
    addr = (GC_word *)GC_USR_PTR_FROM_BASE(addr);
  }
  /* Skip "vtable" pointer. */
  x = (sexpr)((void **)addr + 1);

  mark_stack_top = GC_MARK_AND_PUSH(x->sexpr_cdr, mark_stack_top,
                                    mark_stack_limit, (void **)&x->sexpr_cdr);
  return GC_MARK_AND_PUSH(x->sexpr_car, mark_stack_top, mark_stack_limit,
                          (void **)&x->sexpr_car);
}
#endif /* GC_GCJ_SUPPORT */

static sexpr
small_cons(sexpr x, sexpr y)
{
  sexpr r = GC_NEW(struct SEXPR);

  CHECK_OUT_OF_MEMORY(r);
  AO_fetch_and_add1(&collectable_count);
  r->sexpr_car = x;
  GC_PTR_STORE_AND_DIRTY(&r->sexpr_cdr, y);
  GC_reachable_here(x);
  return r;
}

#ifdef NO_CONS_ATOMIC_LEAF
#  define small_cons_leaf(x) small_cons(INT_TO_SEXPR(x), nil)
#else
static sexpr
small_cons_leaf(int x)
{
  sexpr r = (sexpr)checkOOM(GC_MALLOC_ATOMIC(sizeof(struct SEXPR)));

  AO_fetch_and_add1(&atomic_count);
  r->sexpr_car = INT_TO_SEXPR(x);
  r->sexpr_cdr = nil;
  return r;
}
#endif

static sexpr
small_cons_uncollectable(sexpr x, sexpr y)
{
  sexpr r = GC_NEW_UNCOLLECTABLE(struct SEXPR);

  CHECK_OUT_OF_MEMORY(r);
  AO_fetch_and_add1(&uncollectable_count);
  r->sexpr_cdr = (sexpr)GC_HIDE_POINTER(y);
  GC_PTR_STORE_AND_DIRTY(&r->sexpr_car, x);
  return r;
}

#ifdef GC_GCJ_SUPPORT
static sexpr
gcj_cons(sexpr x, sexpr y)
{
  sexpr result;
  size_t cnt = (size_t)AO_fetch_and_add1(&extra_count);
  const void *d = (cnt & 1) != 0 ? &gcj_class_struct1 : &gcj_class_struct2;
  size_t lb = sizeof(struct SEXPR) + sizeof(struct fake_vtable *);
  void *r;

  if ((cnt & 2) != 0) {
    r = GC_GCJ_MALLOC_IGNORE_OFF_PAGE(lb + (cnt <= HBLKSIZE / 2 ? cnt : 0), d);
  } else {
    r = GC_GCJ_MALLOC(lb, d);
  }
  CHECK_OUT_OF_MEMORY(r);
  AO_fetch_and_add1(&collectable_count);
  /* Skip "vtable" pointer. */
  result = (sexpr)((void **)r + 1);

  result->sexpr_car = x;
  GC_PTR_STORE_AND_DIRTY(&result->sexpr_cdr, y);
  GC_reachable_here(x);
  return result;
}
#endif /* GC_GCJ_SUPPORT */

/* Return `reverse(x)` concatenated with `y`. */
static sexpr
reverse1(sexpr x, sexpr y)
{
  if (is_nil(x)) {
    return y;
  } else {
    return reverse1(cdr(x), cons(car(x), y));
  }
}

static sexpr
reverse(sexpr x)
{
#ifdef TEST_WITH_SYSTEM_MALLOC
  /*
   * This causes `gctest` to allocate (and leak) large chunks of memory
   * with the standard system `malloc()`.  This should cause the root
   * set and collected heap to grow significantly if `malloc`'ed memory
   * is somehow getting traced by the collector.
   */
  GC_noop1((GC_word)GC_HIDE_NZ_POINTER(checkOOM(malloc(100000))));
#endif
  return reverse1(x, nil);
}

#ifdef THREADS
#  ifdef GC_PTHREADS
#    define THREAD_RET_TYPE_CALL_CONV void *
#  else
#    define THREAD_RET_TYPE_CALL_CONV DWORD __stdcall
#  endif

static THREAD_RET_TYPE_CALL_CONV
do_gcollect(void *arg)
{
  UNUSED_ARG(arg);
  if (print_stats)
    GC_log_printf("Collect from a standalone thread\n");
  GC_gcollect();
  return 0;
}

static void
collect_from_other_thread(void)
{
#  ifdef GC_PTHREADS
  pthread_t t;
  int err = pthread_create(&t, NULL, do_gcollect, NULL /* `arg` */);

  if (err != 0) {
    GC_printf("gcollect thread creation failed, errno= %d\n", err);
    exit(69);
  }
  TEST_ASSERT(pthread_join(t, NULL) == 0);
#  else
  DWORD thread_id;
  HANDLE h = CreateThread(NULL, 0, do_gcollect, NULL, 0, &thread_id);

  if (h == (HANDLE)NULL) {
    GC_printf("gcollect thread creation failed, errcode= %d\n",
              (int)GetLastError());
    exit(69);
  }
  TEST_ASSERT(WaitForSingleObject(h, INFINITE) == WAIT_OBJECT_0);
#  endif
}

#  define MAX_GCOLLECT_THREADS ((NTHREADS + 2) / 3)
static volatile AO_t gcollect_threads_cnt = 0;
#endif

static sexpr
ints(int low, int up)
{
  if (up < 0 ? low > -up : low > up) {
    if (up < 0) {
#ifdef THREADS
      if (AO_fetch_and_add1(&gcollect_threads_cnt) + 1
          <= MAX_GCOLLECT_THREADS) {
        collect_from_other_thread();
        return nil;
      }
#endif
      GC_gcollect_and_unmap();
    }
    return nil;
  } else {
    return small_cons(small_cons_leaf(low), ints(low + 1, up));
  }
}

#ifdef GC_GCJ_SUPPORT
/* Return `gcj_reverse(x)` concatenated with `y`. */
static sexpr
gcj_reverse1(sexpr x, sexpr y)
{
  if (is_nil(x)) {
    return y;
  } else {
    return gcj_reverse1(cdr(x), gcj_cons(car(x), y));
  }
}

static sexpr
gcj_reverse(sexpr x)
{
  return gcj_reverse1(x, nil);
}

static sexpr
gcj_ints(int low, int up)
{
  if (low > up) {
    return nil;
  } else {
    return gcj_cons(gcj_cons(INT_TO_SEXPR(low), nil), gcj_ints(low + 1, up));
  }
}
#endif /* GC_GCJ_SUPPORT */

/*
 * To check uncollectible allocation we build lists with disguised `cdr`
 * pointers, and make sure they do not go away.
 */
static sexpr
uncollectable_ints(int low, int up)
{
  if (low > up) {
    return nil;
  } else {
    return small_cons_uncollectable(small_cons_leaf(low),
                                    uncollectable_ints(low + 1, up));
  }
}

static void
check_ints(sexpr list, int low, int up)
{
  TEST_ASSERT(!is_nil(list));
  TEST_ASSERT(SEXPR_TO_INT(car(car(list))) == low);
  if (low == up) {
    TEST_ASSERT(cdr(list) == nil);
  } else {
    check_ints(cdr(list), low + 1, up);
  }
}

#define UNCOLLECTABLE_CDR(x) (sexpr) GC_REVEAL_POINTER(cdr(x))

static void
check_uncollectable_ints(sexpr list, int low, int up)
{
  TEST_ASSERT(SEXPR_TO_INT(car(car(list))) == low);
  if (low == up) {
    TEST_ASSERT(UNCOLLECTABLE_CDR(list) == nil);
  } else {
    check_uncollectable_ints(UNCOLLECTABLE_CDR(list), low + 1, up);
  }
}

#ifdef PRINT_AND_CHECK_INT_LIST
/* The following might be useful for debugging. */

static void
print_int_list(sexpr x)
{
  if (is_nil(x)) {
    GC_printf("NIL\n");
  } else {
    GC_printf("(%d)", SEXPR_TO_INT(car(car(x))));
    if (!is_nil(cdr(x))) {
      GC_printf(", ");
      print_int_list(cdr(x));
    } else {
      GC_printf("\n");
    }
  }
}

static void
check_marks_int_list(sexpr x)
{
  if (!GC_is_marked(x)) {
    GC_printf("[unm:%p]", (void *)x);
  } else {
    GC_printf("[mkd:%p]", (void *)x);
  }
  if (is_nil(x)) {
    GC_printf("NIL\n");
  } else {
    if (!GC_is_marked(car(x)))
      GC_printf("[unm car:%p]", (void *)car(x));
    GC_printf("(%d)", SEXPR_TO_INT(car(car(x))));
    if (!is_nil(cdr(x))) {
      GC_printf(", ");
      check_marks_int_list(cdr(x));
    } else {
      GC_printf("\n");
    }
  }
}
#endif /* PRINT_AND_CHECK_INT_LIST */

/* A tiny list reversal test to check thread creation. */
#ifdef THREADS
#  ifdef GC_ENABLE_SUSPEND_THREAD
#    include "gc/javaxfc.h"
#  endif

#  ifdef VERY_SMALL_CONFIG
#    define TINY_REVERSE_UPPER_VALUE 4
#  else
#    define TINY_REVERSE_UPPER_VALUE 10
#  endif

static void
tiny_reverse_test_inner(void)
{
  int i;

  for (i = 0; i < 5; ++i) {
    check_ints(reverse(reverse(ints(1, TINY_REVERSE_UPPER_VALUE))), 1,
               TINY_REVERSE_UPPER_VALUE);
  }
}

#  if defined(GC_ENABLE_SUSPEND_THREAD) && defined(SIGNAL_BASED_STOP_WORLD) \
      && !defined(OSF1)
#    define TEST_THREAD_SUSPENDED
#    ifndef AO_HAVE_load_acquire
static AO_t
AO_load_acquire(const volatile AO_t *addr)
{
  AO_t result;

  FINALIZER_LOCK();
  result = *addr;
  FINALIZER_UNLOCK();
  return result;
}
#    endif
#    ifndef AO_HAVE_store_release
static void
AO_store_release(volatile AO_t *addr, AO_t new_val)
{
  FINALIZER_LOCK();
  *addr = new_val;
  FINALIZER_UNLOCK();
}
#    endif
#  endif /* GC_ENABLE_SUSPEND_THREAD && SIGNAL_BASED_STOP_WORLD */

#  if !defined(GC_PTHREADS) && !defined(MSWINCE) && !defined(MSWIN_XBOX1) \
      && !defined(NO_CRT) && !defined(NO_TEST_ENDTHREADEX)
#    define TEST_ENDTHREADEX
static unsigned __stdcall
#  else
static THREAD_RET_TYPE_CALL_CONV
#  endif
    tiny_reverse_test(void *p_resumed)
{
#  ifdef TEST_THREAD_SUSPENDED
  if (p_resumed != NULL) {
    /* Test self-suspend is working. */
    GC_suspend_thread(pthread_self());
    AO_store_release((volatile AO_t *)p_resumed, (AO_t)1);
  }
#  else
  (void)p_resumed;
#  endif
  tiny_reverse_test_inner();
#  ifdef GC_ENABLE_SUSPEND_THREAD
  /* Force collection from a thread. */
  GC_gcollect();
#  endif
#  if defined(GC_PTHREADS) && !defined(GC_NO_PTHREAD_CANCEL)
  {
    static volatile AO_t tiny_cancel_cnt = 0;

    if (AO_fetch_and_add1(&tiny_cancel_cnt) % 3 == 0)
      TEST_ASSERT(GC_pthread_cancel(pthread_self()) == 0);
  }
#  endif
#  if defined(THREADS) \
      && (!defined(GC_PTHREADS) || defined(GC_HAVE_PTHREAD_EXIT))
  {
    static volatile AO_t tiny_exit_cnt = 0;

    if ((AO_fetch_and_add1(&tiny_exit_cnt) & 1) == 0) {
#    ifdef TEST_ENDTHREADEX
      GC_endthreadex(0);
#    elif defined(GC_PTHREADS)
      GC_pthread_exit(p_resumed);
#    else
      GC_ExitThread(0);
#    endif
    }
  }
#  endif
  return 0;
}

static void
fork_a_thread(void)
{
#  ifdef GC_PTHREADS
  pthread_t t;
  int err;
#  else
  HANDLE h;
#    ifdef TEST_ENDTHREADEX
  unsigned thread_id;
#    else
  DWORD thread_id;
#    endif
#  endif
#  ifdef GC_ENABLE_SUSPEND_THREAD
  static volatile AO_t forked_cnt = 0;
  volatile AO_t *p_resumed = NULL;

  if (AO_fetch_and_add1(&forked_cnt) % 2 == 0) {
    p_resumed = GC_NEW(AO_t);
    CHECK_OUT_OF_MEMORY(p_resumed);
    AO_fetch_and_add1(&collectable_count);
  }
#  else
#    define p_resumed NULL
#  endif
#  ifdef GC_PTHREADS
  err = pthread_create(&t, NULL, tiny_reverse_test, (void *)p_resumed);
  if (err != 0) {
    GC_printf("Small thread creation failed %d\n", err);
    exit(69);
  }
#  else
#    ifdef TEST_ENDTHREADEX
  h = (HANDLE)GC_beginthreadex(NULL /* `security` */, 0 /* `stack_size` */,
                               tiny_reverse_test, NULL /* `arglist` */,
                               0 /* `initflag` */, &thread_id);
#    else
  /*
   * Note: the types of the arguments are specified explicitly to test
   * the prototype.
   */
  h = CreateThread((SECURITY_ATTRIBUTES *)NULL, 0U, tiny_reverse_test, NULL,
                   (DWORD)0, &thread_id);
#    endif
  if (h == (HANDLE)NULL) {
    GC_printf("Small thread creation failed, errcode= %d\n",
              (int)GetLastError());
    exit(69);
  }
#  endif
#  ifdef TEST_THREAD_SUSPENDED
  if (GC_is_thread_suspended(t))
    TEST_ASSERT(p_resumed != NULL);
  /* Note: might be already self-suspended. */
  GC_suspend_thread(t);
  TEST_ASSERT(GC_is_thread_suspended(t));
  /* This should be no-op. */
  GC_suspend_thread(t);

  for (;;) {
    GC_resume_thread(t);
    if (NULL == p_resumed || AO_load_acquire(p_resumed))
      break;
    GC_collect_a_little();
  }
  TEST_ASSERT(!GC_is_thread_suspended(t));
  /* This should be no-op. */
  GC_resume_thread(t);

  if (NULL == p_resumed)
    GC_collect_a_little();
  /* Thread could be running or already terminated (but not joined). */
  GC_suspend_thread(t);
  GC_collect_a_little();
  TEST_ASSERT(GC_is_thread_suspended(t));
  GC_resume_thread(t);
#  endif
#  ifdef GC_PTHREADS
  TEST_ASSERT(pthread_join(t, 0) == 0);
#  else
  TEST_ASSERT(WaitForSingleObject(h, INFINITE) == WAIT_OBJECT_0);
#  endif
}
#endif /* THREADS */

static void
test_generic_malloc_or_special(const void *p)
{
  size_t size;
  int kind;
  void *p2;

  kind = GC_get_kind_and_size(p, &size);
  TEST_ASSERT(GC_size(p) == size);
  p2 = checkOOM(GC_GENERIC_OR_SPECIAL_MALLOC(10, kind));
  TEST_ASSERT(GC_get_kind_and_size(p2, NULL) == kind);
  GC_FREEZEROALL(p2);
  GC_FREE(checkOOM(GC_GENERIC_MALLOC(21, kind)));
}

#ifndef AO_HAVE_load_acquire
static char *
GC_cptr_load_acquire(char *const volatile *addr)
{
  char *result;

  FINALIZER_LOCK();
  result = *addr;
  FINALIZER_UNLOCK();
  return result;
}
#endif

#ifndef AO_HAVE_store_release
/* Not a macro as `new_val` argument should be evaluated before the lock. */
static void
GC_cptr_store_release(char *volatile *addr, char *new_val)
{
  FINALIZER_LOCK();
  *addr = new_val;
  FINALIZER_UNLOCK();
}
#endif

/* Try to force `A.aa` to be strangely aligned. */
volatile struct A_s {
  char dummy;
  char *volatile aa;
} A;
#define a_set(p) GC_cptr_store_release(&A.aa, (char *)(p))
#define a_get() (sexpr) GC_cptr_load_acquire(&A.aa)

/*
 * Repeatedly reverse lists built out of very-different-sized `cons` cells.
 * Check that we did not lose anything.
 */
static void *GC_CALLBACK
reverse_test_inner(void *data)
{
  int i;
  sexpr b;
  sexpr c;
  sexpr d;
  sexpr e;
  sexpr *f, *g, *h;

  if (data == 0) {
    /* This stack frame is not guaranteed to be scanned. */
    return GC_call_with_gc_active(reverse_test_inner, NUMERIC_TO_VPTR(1));
  }

#if defined(CPPCHECK)
  GC_noop1_ptr(data);
#endif
#ifndef BIG
#  if defined(UNIX_LIKE) && defined(NO_GETCONTEXT) /*< e.g. musl */
  /* Assume 128 KB stacks at least. */
#    if defined(__aarch64__) || defined(__s390x__)
#      define BIG 600
#    else
#      define BIG 1000
#    endif
#  elif defined(MSWINCE) || defined(EMBOX) || defined(RTEMS) \
      || (defined(COSMO) && defined(THREADS))
  /* WinCE only allows 64 KB stacks. */
#    define BIG 500
#  elif defined(EMSCRIPTEN) || defined(OSF1)
  /*
   * Wasm reports "Maximum call stack size exceeded" error otherwise.
   * Tru64 UNIX has limited stack space by default, and large frames.
   */
#    define BIG 200
#  elif defined(__MACH__) && defined(__ppc64__)
#    define BIG 2500
#  else
#    define BIG 4500
#  endif
#endif

  a_set(ints(1, 49));
  b = ints(1, 50);
  /* Force garbage collection inside. */
  c = ints(1, -BIG);
  d = uncollectable_ints(1, 100);
  test_generic_malloc_or_special(d);
  e = uncollectable_ints(1, 1);
  /* Check that `realloc` updates object descriptors correctly. */
  f = (sexpr *)checkOOM(GC_MALLOC(4 * sizeof(sexpr)));
  AO_fetch_and_add1(&collectable_count);
  f = (sexpr *)checkOOM(GC_REALLOCF(f, 6 * sizeof(sexpr)));
  AO_fetch_and_add1(&realloc_count);
  GC_PTR_STORE_AND_DIRTY(f + 5, ints(1, 17));
  g = (sexpr *)checkOOM(GC_MALLOC(513 * sizeof(sexpr)));
  AO_fetch_and_add1(&collectable_count);
  test_generic_malloc_or_special(g);
  g = (sexpr *)checkOOM(GC_REALLOC(g, 800 * sizeof(sexpr)));
  AO_fetch_and_add1(&realloc_count);
  GC_PTR_STORE_AND_DIRTY(g + 799, ints(1, 18));
  h = (sexpr *)checkOOM(GC_MALLOC(1025 * sizeof(sexpr)));
  AO_fetch_and_add1(&collectable_count);
  h = (sexpr *)checkOOM(GC_REALLOC(h, 2000 * sizeof(sexpr)));
  AO_fetch_and_add1(&realloc_count);
#ifdef GC_GCJ_SUPPORT
  GC_PTR_STORE_AND_DIRTY(h + 1999, gcj_ints(1, 200));
  for (i = 0; i < 51; ++i) {
    GC_PTR_STORE_AND_DIRTY(h + 1999, gcj_reverse(h[1999]));
  }
  /* Leave it as the reversed list for now. */
#else
  GC_PTR_STORE_AND_DIRTY(h + 1999, ints(1, 200));
#endif
  /* Try to force some collections and reuse of small list elements. */
  for (i = 0; i < 10; i++) {
    (void)ints(1, BIG);
  }
  /* Superficially test the interior pointers recognition on stack. */
  c = (sexpr)((char *)c + sizeof(char *));
  d = (sexpr)((char *)d + sizeof(char *));

  GC_FREE(e);

  check_ints(b, 1, 50);
#ifdef PRINT_AND_CHECK_INT_LIST
  print_int_list(b);
  check_marks_int_list(b);
#endif
#ifndef EMSCRIPTEN
  check_ints(a_get(), 1, 49);
#else
  /* FIXME: `gctest` fails unless `check_ints(a_get(), ...)` are skipped. */
#endif
  for (i = 0; i < 50; i++) {
    check_ints(b, 1, 50);
    b = reverse(reverse(b));
  }
  check_ints(b, 1, 50);
#ifndef EMSCRIPTEN
  check_ints(a_get(), 1, 49);
#endif
  for (i = 0; i < 10 * (NTHREADS + 1); i++) {
#ifdef THREADS
#  if NTHREADS > 0
    if (i % 10 == 0)
      fork_a_thread();
#  else
    GC_noop1((GC_word)(GC_funcptr_uint)(&fork_a_thread));
#  endif
#endif
    /*
     * This maintains the invariant that `a` always points to a list of
     * 49 integers.  Thus, this is thread safe without locks, assuming
     * acquire/release barriers in `a_get`/`a_set` and atomic pointer
     * assignments (otherwise, e.g., `check_ints()` may see an uninitialized
     * object returned by `GC_MALLOC`).
     */
    a_set(reverse(reverse(a_get())));
#if !defined(AT_END) && !defined(THREADS)
    /* This is not thread safe, since realloc explicitly deallocates. */
    a_set(checkOOM(GC_REALLOC(a_get(), (i & 1) != 0 ? 500 : 8200)));
    AO_fetch_and_add1(&realloc_count);
#endif
  }
#ifndef EMSCRIPTEN
  check_ints(a_get(), 1, 49);
#endif
  check_ints(b, 1, 50);

  /* Restore `c` and `d` values. */
  c = (sexpr)((char *)c - sizeof(char *));
  d = (sexpr)((char *)d - sizeof(char *));

  check_ints(c, 1, BIG);
  check_uncollectable_ints(d, 1, 100);
  check_ints(f[5], 1, 17);
  check_ints(g[799], 1, 18);
#ifdef GC_GCJ_SUPPORT
  GC_PTR_STORE_AND_DIRTY(h + 1999, gcj_reverse(h[1999]));
#endif
  check_ints(h[1999], 1, 200);
#ifndef THREADS
  a_set(NULL);
#endif
  *CAST_THRU_UINTPTR(volatile sexpr *, &b) = 0;
  *CAST_THRU_UINTPTR(volatile sexpr *, &c) = 0;
  return 0;
}

static void
reverse_test(void)
{
  /* Test `GC_do_blocking`/`GC_call_with_gc_active`. */
  (void)GC_do_blocking(reverse_test_inner, 0);
}

/*
 * The rest of this builds balanced binary trees, checks that they do not
 * disappear, and tests finalization.
 */
typedef struct treenode {
  int level;
  struct treenode *lchild;
  struct treenode *rchild;
} tn;

#ifndef GC_NO_FINALIZATION
int finalizable_count = 0;
#endif

int finalized_count = 0;
int dropped_something = 0;

#ifndef GC_NO_FINALIZATION
static void GC_CALLBACK
finalizer(void *obj, void *client_data)
{
  tn *t = (tn *)obj;

#  ifdef CPPCHECK
  GC_noop1_ptr(client_data);
#  endif
  FINALIZER_LOCK();
  TEST_ASSERT((int)(GC_uintptr_t)client_data == t->level);
  finalized_count++;
  /* Detect duplicate finalization immediately. */
  t->level = -1;
  FINALIZER_UNLOCK();
}

static void GC_CALLBACK
dummy_finalizer(void *obj, void *client_data)
{
  UNUSED_ARG(obj);
  UNUSED_ARG(client_data);
}

#  define MAX_FINALIZED_PER_THREAD 4000

#  define MAX_FINALIZED ((NTHREADS + 1) * MAX_FINALIZED_PER_THREAD)

void *live_indicators[MAX_FINALIZED] = { NULL };
#  ifndef GC_LONG_REFS_NOT_NEEDED
void *live_long_refs[MAX_FINALIZED] = { NULL };
#  endif

int live_indicators_count = 0;
#endif /* !GC_NO_FINALIZATION */

static tn *
mktree(int n)
{
  tn *result = GC_NEW(tn);
  tn *left, *right;

  CHECK_OUT_OF_MEMORY(result);
  AO_fetch_and_add1(&collectable_count);
  if (0 == n)
    return NULL;
  result->level = n;
  result->lchild = left = mktree(n - 1);
  result->rchild = right = mktree(n - 1);
  if (AO_fetch_and_add1(&extra_count) % 17 == 0 && n >= 2) {
    const tn *tmp;

    CHECK_OUT_OF_MEMORY(left);
    tmp = left->rchild;
    CHECK_OUT_OF_MEMORY(right);
    GC_PTR_STORE_AND_DIRTY(&left->rchild, right->lchild);
    GC_PTR_STORE_AND_DIRTY(&right->lchild, tmp);
  }
  if (AO_fetch_and_add1(&extra_count) % 119 == 0) {
#ifndef GC_NO_FINALIZATION
    int my_index;
    void **new_link = GC_NEW(void *);

    CHECK_OUT_OF_MEMORY(new_link);
    AO_fetch_and_add1(&collectable_count);
#endif
    {
      /* Losing a count here causes erroneous report of failure. */
      FINALIZER_LOCK();
#ifndef GC_NO_FINALIZATION
      finalizable_count++;
      my_index = live_indicators_count++;
#endif
      FINALIZER_UNLOCK();
    }

#ifndef GC_NO_FINALIZATION
    if (!GC_get_find_leak()) {
      GC_REGISTER_FINALIZER(result, finalizer, NUMERIC_TO_VPTR(n),
                            (GC_finalization_proc *)0, (void **)0);
      TEST_ASSERT(my_index < MAX_FINALIZED);
      live_indicators[my_index] = (void *)(GC_uintptr_t)13;
      TEST_ASSERT(GC_GENERAL_REGISTER_DISAPPEARING_LINK(
                      &live_indicators[my_index], result)
                  == 0);
      TEST_ASSERT(GC_move_disappearing_link(&live_indicators[my_index],
                                            &live_indicators[my_index])
                  == GC_SUCCESS);
      *new_link = live_indicators[my_index];
      TEST_ASSERT(
          GC_move_disappearing_link(&live_indicators[my_index], new_link)
          == GC_SUCCESS);
      /*
       * Note: if other thread is performing `fork()` at this moment,
       * then the stack of the current thread is dropped (together with
       * `new_link` variable) in the child process, and `GC_dl_hashtbl`
       * entry with the `link` field equal to `new_link` will be removed
       * when a collection occurs (as expected).
       */
      TEST_ASSERT(GC_unregister_disappearing_link(new_link) == 1);
      TEST_ASSERT(
          GC_move_disappearing_link(&live_indicators[my_index], new_link)
          == GC_NOT_FOUND);
      TEST_ASSERT(GC_GENERAL_REGISTER_DISAPPEARING_LINK(
                      &live_indicators[my_index], result)
                  == 0);
#  ifndef GC_LONG_REFS_NOT_NEEDED
      TEST_ASSERT(GC_REGISTER_LONG_LINK(&live_long_refs[my_index], result)
                  == 0);
      TEST_ASSERT(GC_move_long_link(&live_long_refs[my_index],
                                    &live_long_refs[my_index])
                  == GC_SUCCESS);
      *new_link = live_long_refs[my_index];
      TEST_ASSERT(GC_move_long_link(&live_long_refs[my_index], new_link)
                  == GC_SUCCESS);
      TEST_ASSERT(GC_unregister_long_link(new_link) == 1);
      TEST_ASSERT(GC_move_long_link(&live_long_refs[my_index], new_link)
                  == GC_NOT_FOUND);
      TEST_ASSERT(GC_REGISTER_LONG_LINK(&live_long_refs[my_index], result)
                  == 0);
#  endif
    }
#endif
    GC_reachable_here(result);
  }
  GC_END_STUBBORN_CHANGE(result);
  GC_reachable_here(left);
  GC_reachable_here(right);
  return result;
}

static void
chktree(tn *t, int n)
{
  if (0 == n) {
    /* It should be a leaf. */
    TEST_ASSERT(NULL == t);
    return;
  }
  TEST_ASSERT(t->level == n);
  if (AO_fetch_and_add1(&extra_count) % 373 == 0) {
    (void)checkOOM(GC_MALLOC((size_t)AO_fetch_and_add1(&extra_count) % 5001));
    AO_fetch_and_add1(&collectable_count);
  }
  chktree(t->lchild, n - 1);
  if (AO_fetch_and_add1(&extra_count) % 73 == 0) {
    (void)checkOOM(GC_MALLOC((size_t)AO_fetch_and_add1(&extra_count) % 373));
    AO_fetch_and_add1(&collectable_count);
  }
  chktree(t->rchild, n - 1);
}

#ifndef VERY_SMALL_CONFIG
#  if defined(GC_PTHREADS)
pthread_key_t fl_key;
#  endif

static void *
alloc8bytes(void)
{
#  ifndef GC_PTHREADS
  AO_fetch_and_add1(&atomic_count);
  return GC_MALLOC_ATOMIC(8);
#  elif defined(SMALL_CONFIG) || defined(GC_DEBUG)
  AO_fetch_and_add1(&collectable_count);
  return GC_MALLOC(8);
#  else
  void **my_free_list_ptr;
  void *my_free_list;
  const void *next;

  my_free_list_ptr = (void **)pthread_getspecific(fl_key);
  if (NULL == my_free_list_ptr) {
    my_free_list_ptr = GC_NEW_UNCOLLECTABLE(void *);
    if (NULL == my_free_list_ptr)
      return NULL;
    AO_fetch_and_add1(&uncollectable_count);
    TEST_ASSERT(pthread_setspecific(fl_key, my_free_list_ptr) == 0);
  }
  my_free_list = *my_free_list_ptr;
  if (NULL == my_free_list) {
    my_free_list = GC_malloc_many(8);
    if (NULL == my_free_list)
      return NULL;
  }
  next = GC_NEXT(my_free_list);
  GC_PTR_STORE_AND_DIRTY(my_free_list_ptr, next);
  GC_NEXT(my_free_list) = NULL;
  AO_fetch_and_add1(&collectable_count);
  return my_free_list;
#  endif
}

static void
alloc_small(int n)
{
  int i;

  for (i = 0; i < n; i += 8) {
    const void *p = alloc8bytes();

    CHECK_OUT_OF_MEMORY(p);
  }
}
#endif /* !VERY_SMALL_CONFIG */

#include "gc/gc_inline.h"

static void
test_tfls(void)
{
  void *results[3];
  void *tfls[3][GC_TINY_FREELISTS];

  if (!GC_get_dont_add_byte_at_end() && GC_get_all_interior_pointers()) {
    /* Skip. */
    return;
  }

  BZERO(tfls, sizeof(tfls));
  /* TODO: Improve testing of `GC_FAST_MALLOC_GRANS` functionality. */
  GC_MALLOC_WORDS(results[0], 11, tfls[0]);
  CHECK_OUT_OF_MEMORY(results[0]);
  GC_MALLOC_ATOMIC_WORDS(results[1], 20, tfls[1]);
  CHECK_OUT_OF_MEMORY(results[1]);
#if !defined(CPPCHECK)
  /* Workaround "variable l can be declared as pointer to const" warning. */
  GC_CONS(results[2], results[0], results[1], tfls[2]);
  CHECK_OUT_OF_MEMORY(results[2]);
#endif
}

#if defined(THREADS) && defined(GC_DEBUG)
#  ifdef VERY_SMALL_CONFIG
#    define TREE_HEIGHT 12
#  else
#    define TREE_HEIGHT 15
#  endif
#else
#  ifdef VERY_SMALL_CONFIG
#    define TREE_HEIGHT 13
#  else
#    define TREE_HEIGHT 16
#  endif
#endif
static void
tree_test(void)
{
  tn *root;
  int i;

  root = mktree(TREE_HEIGHT);
#ifndef VERY_SMALL_CONFIG
  alloc_small(5000000);
#endif
  chktree(root, TREE_HEIGHT);
  FINALIZER_LOCK();
  if (finalized_count > 0)
    TEST_ASSERT(dropped_something);
  dropped_something = 1;
  FINALIZER_UNLOCK();
  /* Root needs to remain live until `dropped_something` is set. */
  GC_reachable_here(root);

  root = mktree(TREE_HEIGHT);
  chktree(root, TREE_HEIGHT);
  for (i = TREE_HEIGHT; i >= 0; i--) {
    root = mktree(i);
    chktree(root, i);
  }
#ifndef VERY_SMALL_CONFIG
  alloc_small(5000000);
#endif
}

unsigned n_tests = 0;

#ifndef NO_TYPED_TEST
const GC_word bm_huge[320 / CPP_WORDSZ] = {
#  if CPP_WORDSZ == 32
  0xffffffff,
  0xffffffff,
  0xffffffff,
  0xffffffff,
  0xffffffff,
#  endif
  (GC_word)((GC_signed_word)-1),
  (GC_word)((GC_signed_word)-1),
  (GC_word)((GC_signed_word)-1),
  (GC_word)((GC_signed_word)-1),
  ((GC_word)((GC_signed_word)-1)) >> 8 /*< highest byte is zero */
};

/* A very simple test of explicitly typed allocation. */
static void
typed_test(void)
{
  void **old;
  void **newP;
  GC_word bm3[1] = { 0 };
  GC_word bm2[1] = { 0 };
  GC_word bm_large[1] = { 0xf7ff7fff };
  GC_descr d1;
  GC_descr d2;
  GC_descr d3 = GC_make_descriptor(bm_large, 32);
  GC_descr d4 = GC_make_descriptor(bm_huge, 320);
#  ifndef GC_DEBUG
  struct GC_calloc_typed_descr_s ctd_l;
#  endif
  void **x = (void **)checkOOM(
      GC_MALLOC_EXPLICITLY_TYPED(320 * sizeof(void *) + 123, d4));
  int i;

  AO_fetch_and_add1(&collectable_count);
  (void)GC_make_descriptor(bm_large, 32);
  TEST_ASSERT(GC_get_bit(bm_huge, 32) != 0);
  TEST_ASSERT(GC_get_bit(bm_huge, 311) != 0);
  TEST_ASSERT(GC_get_bit(bm_huge, 319) == 0);
  GC_set_bit(bm3, 0);
  GC_set_bit(bm3, 1);
  d1 = GC_make_descriptor(bm3, 2);
  GC_set_bit(bm2, 1);
  d2 = GC_make_descriptor(bm2, 2);
#  ifndef GC_DEBUG
  if (GC_calloc_prepare_explicitly_typed(&ctd_l, sizeof(ctd_l), 1001,
                                         3 * sizeof(void *), d2)
      != 1) {
    GC_printf("Out of memory in calloc typed prepare\n");
    exit(69);
  }
#  endif
  old = NULL;
  for (i = 0; i < 4000; i++) {
    if ((i & 0xff) != 0) {
      newP = (void **)GC_MALLOC_EXPLICITLY_TYPED(4 * sizeof(void *), d1);
    } else {
      newP = (void **)GC_MALLOC_EXPLICITLY_TYPED_IGNORE_OFF_PAGE(
          4 * sizeof(void *), d1);
    }
    CHECK_OUT_OF_MEMORY(newP);
    AO_fetch_and_add1(&collectable_count);
    TEST_ASSERT(NULL == newP[0]);
    TEST_ASSERT(NULL == newP[1]);
    newP[0] = NUMERIC_TO_VPTR(17);
    GC_PTR_STORE_AND_DIRTY(newP + 1, old);
    old = newP;
    AO_fetch_and_add1(&collectable_count);
    newP = (void **)GC_MALLOC_EXPLICITLY_TYPED(4 * sizeof(void *), d2);
    CHECK_OUT_OF_MEMORY(newP);
    newP[0] = NUMERIC_TO_VPTR(17);
    GC_PTR_STORE_AND_DIRTY(newP + 1, old);
    old = newP;
    AO_fetch_and_add1(&collectable_count);
    newP = (void **)GC_MALLOC_EXPLICITLY_TYPED(33 * sizeof(void *), d3);
    CHECK_OUT_OF_MEMORY(newP);
    newP[0] = NUMERIC_TO_VPTR(17);
    GC_PTR_STORE_AND_DIRTY(newP + 1, old);
    old = newP;
    AO_fetch_and_add1(&collectable_count);
    newP = (void **)GC_CALLOC_EXPLICITLY_TYPED(4, 2 * sizeof(void *), d1);
    CHECK_OUT_OF_MEMORY(newP);
    newP[0] = NUMERIC_TO_VPTR(17);
    GC_PTR_STORE_AND_DIRTY(newP + 1, old);
    old = newP;
    AO_fetch_and_add1(&collectable_count);
    if ((i & 0xff) != 0) {
      newP = (void **)GC_CALLOC_EXPLICITLY_TYPED(7, 3 * sizeof(void *), d2);
    } else {
#  ifdef GC_DEBUG
      newP = (void **)GC_CALLOC_EXPLICITLY_TYPED(1001, 3 * sizeof(void *), d2);
#  else
      newP = (void **)GC_calloc_do_explicitly_typed(&ctd_l, sizeof(ctd_l));
#  endif
      if (newP != NULL) {
        TEST_ASSERT(NULL == newP[0]);
        TEST_ASSERT(NULL == newP[1]);
      }
    }
    CHECK_OUT_OF_MEMORY(newP);
    newP[0] = NUMERIC_TO_VPTR(17);
    GC_PTR_STORE_AND_DIRTY(newP + 1, old);
    old = newP;
  }
  for (i = 0; i < 20000; i++) {
    TEST_ASSERT((GC_uintptr_t)newP[0] == (GC_uintptr_t)17);
    newP[0] = NULL;
    old = newP;
    newP = (void **)old[1];
  }
  GC_gcollect();
  GC_noop1_ptr(x);
}
#endif /* !NO_TYPED_TEST */

#ifndef DBG_HDRS_ALL
static AO_t fail_count = 0;

static void GC_CALLBACK
fail_proc1(void *arg)
{
  UNUSED_ARG(arg);
  AO_fetch_and_add1(&fail_count);
}

#  ifdef THREADS
#    define TEST_FAIL_COUNT(n) 1
#  else
#    define TEST_FAIL_COUNT(n) (fail_count >= (AO_t)(n))
#  endif
#endif /* !DBG_HDRS_ALL */

#ifndef NO_CLOCK
static CLOCK_TYPE start_main_time;
#endif

static void
set_print_procs(void)
{
#ifndef CPPCHECK
  if (ADDR(&A.aa) % ALIGNMENT != 0) {
    GC_printf("A.aa is not aligned properly\n");
    FAIL;
  }
#endif
  /* Set these global variables just once to avoid TSan false positives. */
  A.dummy = 17;
#ifndef DBG_HDRS_ALL
  GC_set_is_valid_displacement_print_proc(fail_proc1);
  GC_set_is_visible_print_proc(fail_proc1);
#endif

#ifndef NO_CLOCK
  GET_TIME(start_main_time);
#endif
}

static void
uniq(void *p, ...)
{
  va_list a;
  void *q[100];
  int n = 0, i, j;
  q[n++] = p;
  va_start(a, p);
  for (; (q[n] = va_arg(a, void *)) != NULL; n++) {
    /* Empty. */
  }
  va_end(a);
  for (i = 0; i < n; i++)
    for (j = 0; j < i; j++)
      if (q[i] == q[j]) {
        GC_printf("Apparently failed to mark from some function arguments.\n"
                  "Perhaps GC_with_callee_saves_pushed was configured "
                  "incorrectly?\n");
        FAIL;
      }
}

#include "private/gc_alloc_ptrs.h"

static void *GC_CALLBACK
inc_int_counter(void *pcounter)
{
  ++(*(int *)pcounter);
  return NULL;
}

struct thr_handle_sb_s {
  void *gc_thread_handle;
  struct GC_stack_base sb;
};

static void *GC_CALLBACK
set_stackbottom(void *cd)
{
  GC_set_stackbottom(((struct thr_handle_sb_s *)cd)->gc_thread_handle,
                     &((struct thr_handle_sb_s *)cd)->sb);
  return NULL;
}

static void
run_one_test(void)
{
  char *x;
#ifndef DBG_HDRS_ALL
  char *y;
  char **z;
#endif
#ifndef NO_CLOCK
  CLOCK_TYPE start_time;
  CLOCK_TYPE reverse_time;
  unsigned long time_diff;
#endif
#ifndef NO_TEST_HANDLE_FORK
  pid_t pid;
  int wstatus;
#endif
  struct thr_handle_sb_s thr_handle_sb;

  GC_FREEZEROALL(NULL);
#ifdef THREADS
  TEST_ASSERT(GC_thread_is_registered() || !GC_is_init_called());
#endif
  test_tfls();
#ifndef DBG_HDRS_ALL
  x = (char *)checkOOM(GC_malloc(7));
  AO_fetch_and_add1(&collectable_count);
  y = (char *)checkOOM(GC_malloc(7));
  AO_fetch_and_add1(&collectable_count);
  TEST_ASSERT(GC_size(x) == 8 || GC_size(y) == GC_GRANULE_BYTES);
  x = (char *)checkOOM(GC_malloc(15));
  AO_fetch_and_add1(&collectable_count);
  y = (char *)checkOOM(GC_malloc(15));
  AO_fetch_and_add1(&collectable_count);
  TEST_ASSERT(GC_size(x) == 16 || GC_size(y) == GC_GRANULE_BYTES);
  x = (char *)checkOOM(GC_malloc(0));
  AO_fetch_and_add1(&collectable_count);
  TEST_ASSERT(GC_size(x) == GC_GRANULE_BYTES);
  x = (char *)checkOOM(GC_malloc_uncollectable(0));
  AO_fetch_and_add1(&uncollectable_count);
  TEST_ASSERT(GC_size(x) == GC_GRANULE_BYTES);
  x = (char *)checkOOM(GC_malloc(16));
  AO_fetch_and_add1(&collectable_count);
  TEST_ASSERT(GC_base(GC_PTR_ADD(x, 13)) == x);
  TEST_ASSERT(GC_is_heap_ptr(x));
  TEST_ASSERT(!GC_is_heap_ptr(&x));
  TEST_ASSERT(!GC_is_heap_ptr(&fail_count));
  TEST_ASSERT(!GC_is_heap_ptr(NULL));
  (void)GC_PRE_INCR(x, 0);
  (void)GC_POST_INCR(x);
  (void)GC_POST_DECR(x);
  TEST_ASSERT(GC_base(x) == x);
#  if defined(FUNCPTR_IS_DATAPTR)
  y = CAST_THRU_UINTPTR(char *, fail_proc1);
  TEST_ASSERT(GC_base(y) == NULL);
#  endif
  TEST_ASSERT(GC_same_obj(x + 5, x) == x + 5);
  TEST_ASSERT(GC_is_visible(y) == y);
  TEST_ASSERT(GC_is_visible(x) == x);
  z = (char **)checkOOM(GC_malloc(8));
  AO_fetch_and_add1(&collectable_count);
  GC_PTR_STORE(z, x);
  GC_end_stubborn_change(z);
  TEST_ASSERT(*z == x);
#  if !defined(IA64) && !defined(POWERPC)
  if (!TEST_FAIL_COUNT(1)) {
    /*
     * On a PowerPC, function pointers refer to a descriptor in the data
     * segment, so there should have been no failures.  The same applies
     * to IA-64.
     */
    GC_printf("GC_is_visible produced wrong failure indication\n");
    FAIL;
  }
#  endif
  TEST_ASSERT(GC_is_valid_displacement(y) == y);
  TEST_ASSERT(GC_is_valid_displacement(x) == x);
  TEST_ASSERT(GC_is_valid_displacement(x + 3) == x + 3);

  {
    size_t i;
    void *p;

    p = GC_malloc(17);
    CHECK_OUT_OF_MEMORY(p);
    AO_fetch_and_add1(&collectable_count);

    /* TODO: `GC_memalign` and friends are not tested well. */
    for (i = sizeof(void *); i <= HBLKSIZE * 4; i *= 2) {
      p = checkOOM(GC_memalign(i, 17));
      AO_fetch_and_add1(&collectable_count);
      TEST_ASSERT(ADDR(p) % i == 0);
      TEST_ASSERT(0 == *(int *)p);
      TEST_ASSERT(GC_base(p) == p);
      if (i >= (size_t)GC_GRANULE_BYTES && i <= HBLKSIZE)
        GC_freezero(p, i);
    }
    if (GC_posix_memalign(&p, 64, 1) != 0) {
      GC_printf("Out of memory in GC_posix_memalign\n");
      exit(69);
    }
    GC_noop1_ptr(p);
    AO_fetch_and_add1(&collectable_count);
  }
#  ifndef GC_NO_VALLOC
  {
    void *p = checkOOM(GC_valloc(78));

    AO_fetch_and_add1(&collectable_count);
    TEST_ASSERT((ADDR(p) & 0x1ff /* at least */) == 0);
    TEST_ASSERT(0 == *(int *)p);
    TEST_ASSERT(GC_base(p) == p);

    p = checkOOM(GC_pvalloc(123));
    AO_fetch_and_add1(&collectable_count);
    TEST_ASSERT((ADDR(p) & 0x1ff) == 0);
    TEST_ASSERT(0 == *(int *)p);
    TEST_ASSERT(GC_base(p) == p);
    TEST_ASSERT((GC_size(p) & 0x1e0 /* at least */) == 0);
  }
#  endif
#  ifndef ALL_INTERIOR_POINTERS
#    if defined(POWERPC)
  if (!TEST_FAIL_COUNT(1))
#    else
  if (!TEST_FAIL_COUNT(GC_get_all_interior_pointers() ? 1 : 2))
#    endif
  {
    GC_printf("GC_is_valid_displacement produced wrong failure indication\n");
    FAIL;
  }
#  endif
#endif /* DBG_HDRS_ALL */
  x = GC_STRNDUP("abc", 1);
  CHECK_OUT_OF_MEMORY(x);
  AO_fetch_and_add1(&atomic_count);
  TEST_ASSERT(strlen(x) == 1);
#if defined(CPPCHECK)
  GC_noop1_ptr(x);
#endif
#ifdef GC_REQUIRE_WCSDUP
  {
    static const wchar_t ws[] = { 'a', 'b', 'c', 0 };
    const void *p = GC_WCSDUP(ws);

    CHECK_OUT_OF_MEMORY(p);
    AO_fetch_and_add1(&atomic_count);
  }
#endif
#ifndef GC_NO_FINALIZATION
  if (!GC_get_find_leak()) {
    void **p = GC_NEW_ATOMIC(void *);

    CHECK_OUT_OF_MEMORY(p); /*< LINT2: do not use `checkOOM()` */
    AO_fetch_and_add1(&atomic_count);
    *p = x;
    TEST_ASSERT(GC_register_disappearing_link(p) == 0);
    if (GC_get_java_finalization()) {
      GC_finalization_proc ofn = 0;
      void *ocd = NULL;

      GC_REGISTER_FINALIZER_UNREACHABLE(p, dummy_finalizer, NULL, &ofn, &ocd);
      TEST_ASSERT(0 == ofn);
      TEST_ASSERT(NULL == ocd);
    }
#  ifndef GC_TOGGLE_REFS_NOT_NEEDED
    if (GC_TOGGLEREF_ADD(p, 1) == GC_NO_MEMORY) {
      GC_printf("Out of memory in GC_toggleref_add\n");
      exit(69);
    }
#  endif
  }
#endif
  /* Test floating point alignment. */
  {
    double *dp = GC_NEW(double);

    CHECK_OUT_OF_MEMORY(dp);
    AO_fetch_and_add1(&collectable_count);
    *dp = 1.0;
    dp = GC_NEW(double);
    CHECK_OUT_OF_MEMORY(dp);
    AO_fetch_and_add1(&collectable_count);
    *dp = 1.0;
#ifndef NO_DEBUGGING
    (void)GC_count_set_marks_in_hblk(dp);
#endif
  }
  /* Test zero-sized allocation a bit more. */
  {
    size_t i;

    GC_FREEZERO(NULL, 20);
    for (i = 0; i < 10000; ++i) {
      (void)checkOOM(GC_MALLOC(0));
      AO_fetch_and_add1(&collectable_count);
      GC_FREEZERO(checkOOM(GC_MALLOC(0)), 32);
      (void)checkOOM(GC_MALLOC_ATOMIC(0));
      AO_fetch_and_add1(&atomic_count);
      GC_FREEZERO(checkOOM(GC_MALLOC_ATOMIC(0)), 0);
      test_generic_malloc_or_special(checkOOM(GC_malloc_atomic(1)));
      AO_fetch_and_add1(&atomic_count);
      GC_FREE(checkOOM(GC_MALLOC_ATOMIC_IGNORE_OFF_PAGE(1)));
      GC_FREE(checkOOM(GC_MALLOC_IGNORE_OFF_PAGE(2)));
      (void)checkOOM(GC_generic_malloc_ignore_off_page(2 * HBLKSIZE, NORMAL));
      AO_fetch_and_add1(&collectable_count);
    }
  }
  thr_handle_sb.gc_thread_handle = GC_get_my_stackbottom(&thr_handle_sb.sb);
  GC_push_obj_descr(NULL, GC_DS_LENGTH);
#ifdef GC_GCJ_SUPPORT
  GC_REGISTER_DISPLACEMENT(sizeof(struct fake_vtable *));
  GC_init_gcj_malloc_mp(0U, fake_gcj_mark_proc, GC_GCJ_MARK_DESCR_OFFSET);
#endif
  /* Make sure that function arguments are visible to the collector. */
  uniq(GC_malloc(12), GC_malloc(12), GC_malloc(12),
       (GC_gcollect(), GC_malloc(12)), GC_malloc(12), GC_malloc(12),
       GC_malloc(12), (GC_gcollect(), GC_malloc(12)), GC_malloc(12),
       GC_malloc(12), GC_malloc(12), (GC_gcollect(), GC_malloc(12)),
       GC_malloc(12), GC_malloc(12), GC_malloc(12),
       (GC_gcollect(), GC_malloc(12)), GC_malloc(12), GC_malloc(12),
       GC_malloc(12), (GC_gcollect(), GC_malloc(12)), (void *)0);
  /* `GC_malloc(0)` must return `NULL` or something we can deallocate. */
  GC_free(checkOOM(GC_malloc(0)));
  GC_free(checkOOM(GC_malloc_atomic(0)));
  GC_free(checkOOM(GC_malloc(0)));
  GC_freezero(checkOOM(GC_malloc_atomic(0)), GC_SIZE_MAX);
#ifndef NO_TEST_HANDLE_FORK
  GC_atfork_prepare();
  pid = fork();
  if (pid != 0) {
    GC_atfork_parent();
    if (pid == -1) {
      GC_printf("Process fork failed\n");
      exit(69);
    }
    if (print_stats)
      GC_log_printf("Forked child process, pid= %ld\n", (long)pid);
    TEST_ASSERT(waitpid(pid, &wstatus, 0) != -1);
    if (!WIFEXITED(wstatus) || WEXITSTATUS(wstatus) != 0) {
      GC_printf("Child process failed, pid= %ld, status= 0x%x\n", (long)pid,
                wstatus);
      FAIL;
    }
  } else {
    pid_t child_pid = getpid();

    GC_atfork_child();
    if (print_stats)
      GC_log_printf("Started a child process, pid= %ld\n", (long)child_pid);
#  ifdef PARALLEL_MARK
    /* No parallel markers. */
    GC_gcollect();
#  endif
    GC_start_mark_threads();
    GC_gcollect();
#  ifdef THREADS
    /*
     * Skip "Premature finalization" check in the child process because
     * there could be a chance that some other thread of the parent process
     * was executing `mktree()` at the moment of process fork.
     */
    dropped_something = 1;
#  endif
    tree_test();
#  ifndef NO_TYPED_TEST
    typed_test();
#  endif
#  ifdef THREADS
    if (print_stats)
      GC_log_printf("Starting tiny reverse test, pid= %ld\n", (long)child_pid);
    tiny_reverse_test_inner();
    GC_gcollect();
#  endif
    if (print_stats)
      GC_log_printf("Finished a child process, pid= %ld\n", (long)child_pid);
    exit(0);
  }
#endif
  (void)GC_call_with_reader_lock(set_stackbottom, &thr_handle_sb,
                                 1 /* `release` */);

  /* Repeated list reversal test. */
#ifndef NO_CLOCK
  GET_TIME(start_time);
#endif
  reverse_test();
#ifndef NO_CLOCK
  if (print_stats) {
    GET_TIME(reverse_time);
    time_diff = MS_TIME_DIFF(reverse_time, start_time);
    GC_log_printf("Finished reverse_test at time %u (%p)\n",
                  (unsigned)time_diff, (void *)&start_time);
  }
#endif
#ifndef NO_TYPED_TEST
  typed_test();
#  ifndef NO_CLOCK
  if (print_stats) {
    CLOCK_TYPE typed_time;

    GET_TIME(typed_time);
    time_diff = MS_TIME_DIFF(typed_time, start_time);
    GC_log_printf("Finished typed_test at time %u (%p)\n", (unsigned)time_diff,
                  (void *)&start_time);
  }
#  endif
#endif /* !NO_TYPED_TEST */
  tree_test();
#ifdef TEST_WITH_SYSTEM_MALLOC
  free(checkOOM(calloc(1, 1)));
  free(checkOOM(realloc(NULL, 64)));
#endif
#ifndef NO_CLOCK
  if (print_stats) {
    CLOCK_TYPE tree_time;

    GET_TIME(tree_time);
    time_diff = MS_TIME_DIFF(tree_time, start_time);
    GC_log_printf("Finished tree_test at time %u (%p)\n", (unsigned)time_diff,
                  (void *)&start_time);
  }
#endif
  /* Run `reverse_test` a second time, so we hopefully notice corruption. */
  reverse_test();
#ifndef NO_DEBUGGING
  (void)GC_is_tmp_root(&atomic_count);
#endif
#ifndef NO_CLOCK
  if (print_stats) {
    GET_TIME(reverse_time);
    time_diff = MS_TIME_DIFF(reverse_time, start_time);
    GC_log_printf("Finished second reverse_test at time %u (%p)\n",
                  (unsigned)time_diff, (void *)&start_time);
  }
#endif
  /*
   * `GC_allocate_ml` and `GC_need_to_lock_real` are not exported, and
   * `AO_fetch_and_add1()` may be unavailable to update a counter.
   */
  (void)GC_call_with_alloc_lock(inc_int_counter, &n_tests);

  /* Dummy checking of API functions while the allocator lock is held. */
  GC_alloc_lock();
  GC_incr_bytes_allocd(0);
  GC_incr_bytes_freed(0);
  GC_alloc_unlock();

#ifndef NO_CLOCK
  if (print_stats)
    GC_log_printf("Finished %p\n", (void *)&start_time);
#endif
}

/* Execute some tests after termination of other test threads (if any). */
static void
run_single_threaded_test(void)
{
  GC_disable();
  GC_FREE(checkOOM(GC_MALLOC(100)));
  /* Add a block to heap. */
  GC_expand_hp(0);
  GC_enable();
}

static void GC_CALLBACK
reachable_objs_counter(void *obj, size_t size, void *plocalcnt)
{
  TEST_ASSERT(size != 0);
  TEST_ASSERT(GC_base(obj) == obj);
  TEST_ASSERT(GC_size(obj) == size);
  (*(unsigned *)plocalcnt)++;
}

static void *GC_CALLBACK
count_reachable_objs(void *plocalcnt)
{
  GC_enumerate_reachable_objects_inner(reachable_objs_counter, plocalcnt);
  return NULL;
}

/* A minimal testing of `LONG_MULT()`. */
static void
test_long_mult(void)
{
#if !defined(CPPCHECK) || !defined(NO_LONGLONG64)
  unsigned32 hp, lp;

  LONG_MULT(hp, lp, (unsigned32)0x1234567UL, (unsigned32)0xfedcba98UL);
#  if defined(CPPCHECK)
  GC_noop1_ptr(&hp);
  GC_noop1_ptr(&lp);
#  endif
  TEST_ASSERT(hp == (unsigned32)0x121fa00UL);
  TEST_ASSERT(lp == (unsigned32)0x23e20b28UL);

  LONG_MULT(hp, lp, (unsigned32)0xdeadbeefUL, (unsigned32)0xefcdab12UL);
  TEST_ASSERT(hp == (unsigned32)0xd0971b30UL);
  TEST_ASSERT(lp == (unsigned32)0xbd2411ceUL);
#endif
}

#define NUMBER_ROUND_UP(v, bound) \
  ((((v) + (bound) - (unsigned)1) / (bound)) * (bound))

static size_t initial_heapsize;

static void
check_heap_stats(void)
{
  size_t max_heap_sz;
  size_t init_heap_sz = initial_heapsize;
  int i;
#if !defined(GC_NO_FINALIZATION) && defined(FINALIZE_ON_DEMAND)
  int late_finalize_count = 0;
#endif
  unsigned obj_count;
#ifndef NO_CLOCK
  CLOCK_TYPE curr_time;

  GET_TIME(curr_time);
#endif
  TEST_ASSERT(GC_is_init_called());
#if !defined(NO_DEBUGGING) && defined(TEST_COVERAGE)
  GC_print_free_list(NORMAL, 32);
  GC_dump_regions();
  GC_dump();
#endif

  /*
   * The upper bounds are a guess, which has been empirically adjusted.
   * On low-end uniprocessors with the incremental collection these may be
   * particularly dubious, since empirically the heap tends to grow largely
   * as a result of the GC not getting enough cycles.
   */
#if CPP_PTRSZ == 64
  max_heap_sz = 26000000;
#else
  max_heap_sz = 16000000;
#endif
#ifdef VERY_SMALL_CONFIG
  max_heap_sz /= 4;
#endif
#ifdef GC_DEBUG
  max_heap_sz *= 2;
#  ifdef SAVE_CALL_CHAIN
  max_heap_sz *= 3;
#    ifdef SAVE_CALL_COUNT
  max_heap_sz += max_heap_sz * NFRAMES / 4;
#    endif
#  endif
#endif
#if defined(ADDRESS_SANITIZER) && !defined(__clang__)
  max_heap_sz = max_heap_sz * 2 - max_heap_sz / 3;
#endif
#ifdef MEMORY_SANITIZER
  max_heap_sz += max_heap_sz / 4;
#endif
  max_heap_sz *= n_tests;
#if defined(USE_MMAP) || defined(MSWIN32)
  max_heap_sz = NUMBER_ROUND_UP(max_heap_sz, 4 * 1024 * 1024);
#endif
  /* Add 1% for recycled blocks. */
  init_heap_sz += init_heap_sz / 100;
  if (max_heap_sz < init_heap_sz)
    max_heap_sz = init_heap_sz;

  /*
   * Do garbage collection repeatedly so that all inaccessible objects
   * can be finalized.  Should work even if the collection is disabled.
   */
  while (GC_collect_a_little()) {
    /* Empty. */
  }

  for (i = 0; i < 16; i++) {
    GC_gcollect();
#ifndef GC_NO_FINALIZATION
#  ifdef FINALIZE_ON_DEMAND
    late_finalize_count +=
#  endif
        GC_invoke_finalizers();
#endif
  }
  if (print_stats) {
    struct GC_stack_base sb;
    int res = GC_get_stack_base(&sb);

    if (res == GC_SUCCESS) {
      GC_log_printf("Primordial thread stack bottom: %p\n", sb.mem_base);
    } else {
      TEST_ASSERT(res == GC_UNIMPLEMENTED);
      GC_log_printf("GC_get_stack_base() unimplemented\n");
    }
  }
  obj_count = 0;
  (void)GC_call_with_reader_lock(count_reachable_objs, &obj_count, 0);
#ifdef THREADS
  GC_printf("Completed %u tests (concurrently)\n", n_tests);
#else
  GC_printf("Completed %u tests\n", n_tests);
#endif
  GC_printf("Allocated %d collectable objects\n", (int)collectable_count);
  GC_printf("Allocated %d uncollectable objects\n", (int)uncollectable_count);
  GC_printf("Allocated %d atomic objects\n", (int)atomic_count);
  GC_printf("Reallocated %d objects\n", (int)realloc_count);
#ifndef NO_TEST_HANDLE_FORK
  GC_printf("Garbage collection after fork is tested too\n");
#endif
#ifndef GC_NO_FINALIZATION
  if (!GC_get_find_leak()) {
    int still_live = 0;
#  ifndef GC_LONG_REFS_NOT_NEEDED
    int still_long_live = 0;
#  endif

#  ifdef FINALIZE_ON_DEMAND
    TEST_ASSERT(finalized_count == late_finalize_count);
#  endif
    if (finalized_count > finalizable_count
        || finalized_count < finalizable_count / 2) {
      GC_printf("Finalized %d/%d objects - finalization is probably broken\n",
                finalized_count, finalizable_count);
      FAIL;
    } else {
      GC_printf("Finalized %d/%d objects - finalization is probably OK\n",
                finalized_count, finalizable_count);
    }
    for (i = 0; i < MAX_FINALIZED; i++) {
      if (live_indicators[i] != NULL) {
        still_live++;
      }
#  ifndef GC_LONG_REFS_NOT_NEEDED
      if (live_long_refs[i] != NULL) {
        still_long_live++;
      }
#  endif
    }
    i = finalizable_count - finalized_count - still_live;
    if (i != 0) {
      GC_printf("%d disappearing links remain and %d more objects "
                "were not finalized\n",
                still_live, i);
      if (i > 10) {
        GC_printf("\tVery suspicious!\n");
      } else {
        GC_printf("\tSlightly suspicious, but probably OK\n");
      }
    }
#  ifndef GC_LONG_REFS_NOT_NEEDED
    if (still_long_live != 0) {
      GC_printf("%d 'long' links remain\n", still_long_live);
    }
#  endif
  }
#endif
  GC_printf("Total number of bytes allocated is %lu\n",
            (unsigned long)GC_get_total_bytes());
  GC_printf("Total memory use by allocated blocks is %lu bytes\n",
            (unsigned long)GC_get_memory_use());
  GC_printf("Final heap size is %lu bytes\n",
            (unsigned long)GC_get_heap_size());
  if (GC_get_total_bytes() < (size_t)n_tests *
#ifdef VERY_SMALL_CONFIG
                                 2700000
#else
                                 33500000
#endif
  ) {
    GC_printf("Incorrect execution - missed some allocations\n");
    FAIL;
  }
  if (GC_get_heap_size() + GC_get_unmapped_bytes() > max_heap_sz
      && !GC_get_find_leak()) {
    GC_printf("Unexpected heap growth - collector may be broken"
              " (heapsize: %lu, expected: %lu)\n",
              (unsigned long)(GC_get_heap_size() + GC_get_unmapped_bytes()),
              (unsigned long)max_heap_sz);
    FAIL;
  }
#ifdef USE_MUNMAP
  GC_printf("Obtained %lu bytes from OS (of which %lu bytes unmapped)\n",
            (unsigned long)GC_get_obtained_from_os_bytes(),
            (unsigned long)GC_get_unmapped_bytes());
#else
  GC_printf("Obtained %lu bytes from OS\n",
            (unsigned long)GC_get_obtained_from_os_bytes());
#endif
  GC_printf("Final number of reachable objects is %u\n", obj_count);

#ifndef GC_GET_HEAP_USAGE_NOT_NEEDED
  /* Get global counters (just to check the functions work). */
  GC_get_heap_usage_safe(NULL, NULL, NULL, NULL, NULL);
  {
    struct GC_prof_stats_s stats;
    (void)GC_get_prof_stats(&stats, sizeof(stats));
#  ifdef THREADS
    (void)GC_get_prof_stats_unsafe(&stats, sizeof(stats));
#  endif
  }
  (void)GC_get_size_map_at(-1);
  (void)GC_get_size_map_at(1);
#endif
  TEST_ASSERT(GC_size(NULL) == 0);
  test_long_mult();

#ifndef NO_CLOCK
  GC_printf("Full collections took %lu ms\n", GC_get_full_gc_total_time());
  GC_printf("World-stopped pauses took %lu ms (%lu us each in avg.)\n",
            GC_get_stopped_mark_total_time(),
            GC_get_avg_stopped_mark_time_ns() / 1000);
#endif
#ifdef PARALLEL_MARK
  GC_printf("Completed %u collections (using %d marker threads)\n",
            (unsigned)GC_get_gc_no(), GC_get_parallel() + 1);
#else
  GC_printf("Completed %u collections\n", (unsigned)GC_get_gc_no());
#endif
#ifdef NO_CLOCK
  GC_printf("Collector appears to work\n");
#else
  GC_printf("Collector appears to work (testing done in %lu ms)\n",
            MS_TIME_DIFF(curr_time, start_main_time));
#endif
}

static void GC_CALLBACK
warn_proc(const char *msg, GC_uintptr_t arg)
{
  GC_printf(msg, arg);
  /* `FAIL;` */
}

static void
enable_incremental_mode(void)
{
#ifndef NO_INCREMENTAL
  unsigned vdbs = (unsigned)COVERT_DATAFLOW(GC_get_supported_vdbs());
#endif

#if !defined(CPPCHECK)
  GC_printf("Running on " OS_TYPE "/" MACH_TYPE " target\n");
#endif
#ifndef NO_INCREMENTAL
  if (vdbs != GC_VDB_NONE)
    GC_printf(
        "Supported VDBs:%s%s%s%s%s%s\n", vdbs & GC_VDB_MANUAL ? " manual" : "",
        vdbs & GC_VDB_DEFAULT ? " default" : "",
        vdbs & GC_VDB_GWW ? " gww" : "", vdbs & GC_VDB_PROC ? " proc" : "",
        vdbs & GC_VDB_SOFT ? " soft" : "",
        vdbs & GC_VDB_MPROTECT ? " mprotect" : "");
#endif
  initial_heapsize = GC_get_heap_size();
#if (defined(TEST_DEFAULT_VDB) || defined(TEST_MANUAL_VDB) \
     || !defined(DEFAULT_VDB))                             \
    && !defined(GC_DISABLE_INCREMENTAL)
#  if !defined(MAKE_BACK_GRAPH) && !defined(NO_INCREMENTAL)   \
      && !(defined(USE_PROC_FOR_LIBRARIES) && defined(LINUX)) \
      && !defined(REDIRECT_MALLOC)
  GC_enable_incremental();
#  endif
  if (GC_is_incremental_mode()) {
    if (GC_get_manual_vdb_allowed()) {
      GC_printf("Switched to incremental mode (manual VDB)\n");
    } else {
      GC_printf("Switched to incremental mode\n");
      switch (GC_get_actual_vdb()) {
      case GC_VDB_GWW:
        GC_printf("Using GetWriteWatch-based implementation\n");
        break;
      case GC_VDB_PROC:
      case GC_VDB_SOFT:
        GC_printf("Reading dirty bits from /proc\n");
        break;
      case GC_VDB_MPROTECT:
        GC_printf("Emulating dirty bits with mprotect/signals%s\n",
                  GC_incremental_protection_needs() & GC_PROTECTS_PTRFREE_HEAP
                      ? " (covering also ptr-free pages)"
                      : "");
        break;
      default:
        /* Nothing for others. */
        break;
      }
    }
  }
#endif
}

#if defined(_DEBUG) && (_MSC_VER >= 1900 /* VS 2015+ */) && !defined(THREADS)
/*
 * Ensure that there is no system-malloc-allocated objects at normal exit
 * (i.e. no such memory leaked).
 */
#  define CRTMEM_CHECK_INIT() \
    (void)_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF)
#  define CRTMEM_DUMP_LEAKS()                                 \
    do {                                                      \
      if (_CrtDumpMemoryLeaks()) {                            \
        GC_printf("System-malloc-allocated memory leaked\n"); \
        FAIL;                                                 \
      }                                                       \
    } while (0)
#else
#  define CRTMEM_CHECK_INIT() (void)0
#  define CRTMEM_DUMP_LEAKS() (void)0
#endif

#if defined(MSWINCE) && defined(THREADS) && !defined(GC_PTHREADS)
HANDLE win_created_h;
HWND win_handle;

static LRESULT CALLBACK
window_proc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  LRESULT ret = 0;
  switch (uMsg) {
  case WM_HIBERNATE:
    GC_printf("Received WM_HIBERNATE, calling GC_gcollect\n");
    /* Force "unmap as much memory as possible" mode. */
    GC_gcollect_and_unmap();
    break;
  case WM_CLOSE:
    GC_printf("Received WM_CLOSE, closing window\n");
    DestroyWindow(hwnd);
    break;
  case WM_DESTROY:
    PostQuitMessage(0);
    break;
  default:
    ret = DefWindowProc(hwnd, uMsg, wParam, lParam);
    break;
  }
  return ret;
}

static DWORD __stdcall thr_window(void *arg)
{
  WNDCLASS win_class = { CS_NOCLOSE,
                         window_proc,
                         0,
                         0,
                         GetModuleHandle(NULL),
                         NULL,
                         NULL,
                         (HBRUSH)(COLOR_APPWORKSPACE + 1),
                         NULL,
                         TEXT("GCtestWindow") };
  MSG msg;

  UNUSED_ARG(arg);
  TEST_ASSERT(RegisterClass(&win_class));
  win_handle = CreateWindowEx(
      0, TEXT("GCtestWindow"), TEXT("GCtest"), 0, CW_USEDEFAULT, CW_USEDEFAULT,
      CW_USEDEFAULT, CW_USEDEFAULT, NULL, NULL, GetModuleHandle(NULL), NULL);
  if (NULL == win_handle) {
    GC_printf("CreateWindow failed\n");
    exit(69);
  }
  SetEvent(win_created_h);
  ShowWindow(win_handle, SW_SHOW);
  UpdateWindow(win_handle);

  while (GetMessage(&msg, NULL, 0, 0)) {
    TranslateMessage(&msg);
    DispatchMessage(&msg);
  }
  return 0;
}
#endif

static void GC_CALLBACK
describe_norm_type(void *p, char *out_buf)
{
  UNUSED_ARG(p);
  BCOPY("NORMAL", out_buf, sizeof("NORMAL"));
}

static int GC_CALLBACK
has_static_roots(const char *dlpi_name, void *section_start,
                 size_t section_size)
{
  UNUSED_ARG(dlpi_name);
  UNUSED_ARG(section_start);
  UNUSED_ARG(section_size);
  return 1;
}

static GC_stop_func default_stop_func;
static GC_word stop_func_last_gc;

static int GC_CALLBACK
test_stop_func(void)
{
  GC_word gc_no = GC_get_gc_no();

  if (gc_no % 123 == 0 && stop_func_last_gc != gc_no) {
    stop_func_last_gc = gc_no;
    return 1;
  }
  return default_stop_func();
}

#ifndef STACK_NOT_SCANNED
static unsigned custom_push_cnt = 0;

static void GC_CALLBACK
test_custom_push(void **bottom, void **top, void *client_data, unsigned hint)
{
  ASSERT_ALIGNMENT(bottom);
  ASSERT_ALIGNMENT(top);
  TEST_ASSERT(ADDR_GE((char *)top, (char *)bottom));
  TEST_ASSERT(&A == client_data);
  UNUSED_ARG(hint);
  custom_push_cnt++;
#  if defined(CPPCHECK)
  GC_noop1_ptr(client_data);
#  endif
}

static void *GC_CALLBACK
test_stack_base_cb(struct GC_stack_base *sb, void *arg)
{
  void *p = NULL;

#  if defined(CPPCHECK)
  GC_noop1_ptr(p);
  GC_noop1_ptr(sb);
#  endif
  GC_custom_push_regs_and_stack(test_custom_push, arg, sb, NULL);
  GC_reachable_here(p);
  return arg;
}
#endif

#ifdef THREADS
static THREAD_RET_TYPE_CALL_CONV
thr_run_one_test(void *arg)
{
  UNUSED_ARG(arg);
  run_one_test();
  return 0;
}
#endif

#if defined(CPPCHECK)
#  define UNTESTED(sym) GC_noop1((GC_word)(GC_funcptr_uint)(&sym))
#endif

#if defined(MSWINCE) && defined(UNDER_CE)
#  define WINMAIN_LPTSTR LPWSTR
#else
#  define WINMAIN_LPTSTR LPSTR
#endif

#ifdef RTEMS
#  include <bsp.h>
#  define CONFIGURE_APPLICATION_NEEDS_CLOCK_DRIVER
#  define CONFIGURE_APPLICATION_NEEDS_CONSOLE_DRIVER
#  define CONFIGURE_RTEMS_INIT_TASKS_TABLE
#  define CONFIGURE_MAXIMUM_TASKS 1
#  define CONFIGURE_INIT
#  define CONFIGURE_INIT_TASK_STACK_SIZE (64 * 1024)
#  include <rtems/confdefs.h>
rtems_task
Init(rtems_task_argument ignored)
#elif ((defined(MSWIN32) && !defined(__MINGW32__)) || defined(MSWINCE)) \
    && !defined(NO_WINMAIN_ENTRY)
int APIENTRY
WinMain(HINSTANCE instance, HINSTANCE prev, WINMAIN_LPTSTR cmd, int n)
#else
int
main(void)
#endif
{
#ifdef THREADS
#  if NTHREADS > 0
  int nthreads;
#    ifdef GC_PTHREADS
  pthread_t th[NTHREADS];
#    else
  HANDLE h[NTHREADS];
#    endif
#  endif
#  ifdef GC_PTHREADS
  pthread_attr_t attr;
#  else
  DWORD thread_id;
#  endif
#endif
#if defined(MSWINCE) && defined(GC_WIN32_THREADS)
  HANDLE win_thr_h;
#endif
#ifdef IRIX5
  int local_var = 0;

  /*
   * Force a larger stack to be preallocated.  Since the initial one cannot
   * always grow later.  Require 1 MB.
   */
  *((volatile char *)&local_var - 1024 * 1024) = 0;
#endif
#ifdef RTEMS
  UNUSED_ARG(ignored);
#  if defined(CPPCHECK)
  GC_noop1((GC_word)(GC_funcptr_uint)(&Init));
#  endif
#elif ((defined(MSWIN32) && !defined(__MINGW32__)) || defined(MSWINCE)) \
    && !defined(NO_WINMAIN_ENTRY)
  UNUSED_ARG(instance);
  UNUSED_ARG(prev);
  UNUSED_ARG(cmd);
  UNUSED_ARG(n);
#  ifdef CPPCHECK
  GC_noop1((GC_word)(GC_funcptr_uint)(&WinMain));
#  endif
#endif
  CRTMEM_CHECK_INIT();
#ifndef STACK_NOT_SCANNED
  (void)GC_call_with_stack_base(test_stack_base_cb,
                                (/* no volatile */ void *)&A);
  TEST_ASSERT(custom_push_cnt != 0);
#endif
#ifdef GC_PTHREADS
#  ifdef HPUX
  /*
   * Default stack size is too small, especially with the 64-bit ABI.
   * Increase the stack size.
   */
  if (pthread_default_stacksize_np(1024 * 1024, 0) != 0) {
    GC_printf("pthread_default_stacksize_np failed\n");
  }
#  endif
#  ifdef PTW32_STATIC_LIB
  pthread_win32_process_attach_np();
  pthread_win32_thread_attach_np();
#  endif
#endif
  if (!GC_is_init_called()) {
    /*
     * No-op.  Ensure the collector is not initialized implicitly,
     * e.g. when `malloc` redirection is on.
     */
    GC_clear_exclusion_table();
#if defined(THREADS) && !defined(GC_NO_THREADS_DISCOVERY)    \
    && !defined(THREAD_LOCAL_ALLOC)                          \
    && (defined(DARWIN) && !defined(DARWIN_DONT_PARSE_STACK) \
        || (defined(GC_WIN32_THREADS) && defined(GC_DLL)     \
            && !defined(MSWINCE)))
    /* Test with implicit thread registration if possible. */
    GC_use_threads_discovery();
#  ifdef DARWIN
    GC_printf("Using Darwin task-threads-based world stop and push\n");
#  else
    GC_printf("Using DllMain to track threads\n");
#  endif
#endif
  }
#ifdef THREADS
  GC_set_markers_count(0);
#  ifdef TEST_REUSE_SIG_SUSPEND
  GC_set_suspend_signal(GC_get_thr_restart_signal());
#  else
  GC_set_suspend_signal(GC_get_suspend_signal());
#  endif
#endif
  GC_set_pointer_mask(GC_get_pointer_mask());
  GC_set_pointer_shift(GC_get_pointer_shift());
#if defined(TEST_MPROTECT_VDB_DISALLOWED) && !defined(GC_DISABLE_INCREMENTAL)
  GC_set_mprotect_vdb_allowed(0);
#endif
  GC_COND_INIT();

#ifdef GC_PTHREADS
  TEST_ASSERT(pthread_attr_init(&attr) == 0);
#  if defined(AIX) || defined(DARWIN) || defined(FREEBSD) || defined(IRIX5) \
      || defined(OPENBSD)
  TEST_ASSERT(pthread_attr_setstacksize(&attr, 1000 * 1024) == 0);
#  endif
#endif
  enable_incremental_mode();
  GC_set_min_bytes_allocd(1);
  TEST_ASSERT(GC_get_min_bytes_allocd() == 1);
  GC_set_rate(10);
  GC_set_max_prior_attempts(GC_get_max_prior_attempts());
  TEST_ASSERT(GC_get_rate() == 10);
#if defined(GC_WIN32_THREADS) && !defined(GC_PTHREADS)
  InitializeCriticalSection(&incr_cs);
#endif
  GC_set_warn_proc(warn_proc);
#if !defined(VERY_SMALL_CONFIG) && defined(GC_PTHREADS)
  {
    int err = pthread_key_create(&fl_key, 0);

    if (err != 0) {
      GC_printf("Key creation failed, errno= %d\n", err);
      exit(69);
    }
  }
#endif
#if defined(MSWINCE) && defined(GC_WIN32_THREADS)
  win_created_h = CreateEvent(NULL, FALSE, FALSE, NULL);
  if (win_created_h == (HANDLE)NULL) {
    GC_printf("Event creation failed, errcode= %d\n", (int)GetLastError());
    exit(69);
  }
  win_thr_h = CreateThread(NULL, 0, thr_window, 0, 0, &thread_id);
  if (win_thr_h == (HANDLE)NULL) {
    GC_printf("Thread creation failed, errcode= %d\n", (int)GetLastError());
    exit(69);
  }
  TEST_ASSERT(WaitForSingleObject(win_created_h, INFINITE) == WAIT_OBJECT_0);
  CloseHandle(win_created_h);
#endif
  set_print_procs();

  /* Minimal testing of some API functions. */
  GC_exclude_static_roots(&atomic_count,
                          (char *)&atomic_count + sizeof(atomic_count));
  GC_register_has_static_roots_callback(has_static_roots);
  GC_register_describe_type_fn(GC_I_NORMAL, describe_norm_type);
#ifdef GC_GCJ_SUPPORT
  (void)GC_new_proc(fake_gcj_mark_proc);
#endif

  GC_start_incremental_collection();
  default_stop_func = GC_get_stop_func();
  GC_set_stop_func(test_stop_func);
#if NTHREADS > 0
  {
    int i;
    for (i = 0; i < NTHREADS; i++) {
#  ifndef THREADS
      run_one_test();
#  elif defined(GC_PTHREADS)
      int err = pthread_create(th + i, &attr, thr_run_one_test, 0);

      if (err != 0) {
        GC_printf("Thread #%d creation failed, errno= %d\n", i, err);
        if (i > 0 && EAGAIN == err) {
          /* Resource is temporarily unavailable. */
          break;
        }
        exit(69);
      }
#  else
      h[i] = CreateThread(NULL, 0, thr_run_one_test, 0, 0, &thread_id);
      if (h[i] == (HANDLE)NULL) {
        GC_printf("Thread creation failed, errcode= %d\n",
                  (int)GetLastError());
        exit(69);
      }
#  endif
    }
#  ifdef THREADS
    nthreads = i;
#  endif
    for (; i <= NTHREADS; i++)
      run_one_test();
#  ifdef THREADS
    for (i = 0; i < nthreads; i++) {
#    ifdef GC_PTHREADS
      TEST_ASSERT(pthread_join(th[i], 0) == 0);
#    else
      TEST_ASSERT(WaitForSingleObject(h[i], INFINITE) == WAIT_OBJECT_0);
#    endif
    }
#  endif
  }
#elif defined(THREADS)
  (void)thr_run_one_test(NULL);
#else
  run_one_test();
#endif
#if defined(MSWINCE) && defined(GC_WIN32_THREADS)
  PostMessage(win_handle, WM_CLOSE, 0, 0);
  TEST_ASSERT(WaitForSingleObject(win_thr_h, INFINITE) == WAIT_OBJECT_0);
#endif
  run_single_threaded_test();
#ifdef TRACE_BUF
  GC_print_trace(0);
#endif
  check_heap_stats();
#ifndef MSWINCE
  (void)fflush(stdout);
#endif
#ifdef GC_PTHREADS
  (void)pthread_attr_destroy(&attr);
#endif

  /* Dummy checking of various getters and setters. */
  (void)GC_get_bytes_since_gc();
  (void)GC_get_free_bytes();
  (void)GC_get_hblk_size();
  (void)GC_get_is_valid_displacement_print_proc();
  (void)GC_get_is_visible_print_proc();
  (void)GC_get_pages_executable();
  (void)GC_get_warn_proc();
  (void)GC_is_disabled();
  GC_set_allocd_bytes_per_finalizer(GC_get_allocd_bytes_per_finalizer());
  GC_set_disable_automatic_collection(GC_get_disable_automatic_collection());
  GC_set_dont_expand(GC_get_dont_expand());
  GC_set_dont_precollect(GC_get_dont_precollect());
  GC_set_finalize_on_demand(GC_get_finalize_on_demand());
  GC_set_finalizer_notifier(GC_get_finalizer_notifier());
  GC_set_force_unmap_on_gcollect(GC_get_force_unmap_on_gcollect());
  GC_set_free_space_divisor(GC_get_free_space_divisor());
  GC_set_full_freq(GC_get_full_freq());
  GC_set_max_retries(GC_get_max_retries());
  GC_set_mprotect_vdb_allowed(GC_get_mprotect_vdb_allowed());
  GC_set_no_dls(GC_get_no_dls());
  GC_set_non_gc_bytes(GC_get_non_gc_bytes());
  GC_set_on_collection_event(GC_get_on_collection_event());
  GC_set_on_heap_resize(GC_get_on_heap_resize());
  GC_set_on_mark_stack_empty(GC_get_on_mark_stack_empty());
  GC_set_on_os_get_mem(GC_get_on_os_get_mem());
  GC_set_oom_fn(GC_get_oom_fn());
  GC_set_push_other_roots(GC_get_push_other_roots());
  GC_set_same_obj_print_proc(GC_get_same_obj_print_proc());
  GC_set_start_callback(GC_get_start_callback());
  GC_set_time_limit(GC_get_time_limit());
  GC_set_abort_func(GC_get_abort_func());
#ifdef THREADS
  GC_set_on_thread_event(GC_get_on_thread_event());
  GC_set_sp_corrector(GC_get_sp_corrector());
  GC_set_thr_restart_signal(GC_get_thr_restart_signal());
#endif
#ifndef NO_CLOCK
  GC_set_time_limit_tv(GC_get_time_limit_tv());
#endif
#ifndef GC_NO_FINALIZATION
  GC_set_await_finalize_proc(GC_get_await_finalize_proc());
  GC_set_interrupt_finalizers(GC_get_interrupt_finalizers());
#  ifndef GC_TOGGLE_REFS_NOT_NEEDED
  GC_set_toggleref_func(GC_get_toggleref_func());
#  endif
#endif
#ifdef CPPCHECK
#  ifdef AO_HAVE_nop
  AO_nop();
#  endif
  UNTESTED(GC_abort_on_oom);
#  ifndef GC_NO_DEINIT
  UNTESTED(GC_deinit);
#  endif
#  ifdef THREADS
  UNTESTED(GC_register_altstack);
#  endif
#endif

#ifdef GC_PTHREADS
#  if !defined(GC_NO_DLOPEN) && !defined(DARWIN) && !defined(GC_WIN32_THREADS)
  {
    void *h = GC_dlopen("libc.so", 0 /* some value (maybe invalid) */);
    if (h != NULL)
      dlclose(h);
  }
#  endif
#  ifndef GC_NO_PTHREAD_SIGMASK
  {
    sigset_t blocked;

    TEST_ASSERT(GC_pthread_sigmask(SIG_BLOCK, NULL, &blocked) == 0);
    TEST_ASSERT(GC_pthread_sigmask(SIG_BLOCK, &blocked, NULL) == 0);
  }
#  endif
#endif
#ifdef THREADS
  GC_stop_world_external();
  GC_start_world_external();
#endif
#if !defined(GC_NO_FINALIZATION) && !defined(JAVA_FINALIZATION_NOT_NEEDED)
  GC_finalize_all();
#endif
  GC_clear_roots();
#if !defined(GC_ANDROID_LOG) && !defined(KOS) && !defined(OS2) \
    && !defined(MSWIN32) && !defined(MSWINCE)
  GC_set_log_fd(2);
#endif

#if defined(ANY_MSWIN) && !defined(GC_NO_DEINIT) && !defined(THREADS)
  GC_win32_free_heap();
#endif
  CRTMEM_DUMP_LEAKS();
#if defined(PTW32_STATIC_LIB) && defined(GC_PTHREADS)
  pthread_win32_thread_detach_np();
  pthread_win32_process_detach_np();
#elif defined(THREADS)
  /* Just to check it works (for `main`). */
  (void)GC_unregister_my_thread();
#endif
#ifdef RTEMS
  exit(0);
#else
  return 0;
#endif
}
