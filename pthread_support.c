/*
 * Copyright (c) 1994 by Xerox Corporation.  All rights reserved.
 * Copyright (c) 1996 by Silicon Graphics.  All rights reserved.
 * Copyright (c) 1998 by Fergus Henderson.  All rights reserved.
 * Copyright (c) 2000-2008 by Hewlett-Packard Company.  All rights reserved.
 * Copyright (c) 2008-2022 Ivan Maidanski
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

#include "private/pthread_support.h"

/*
 * Support code originally for LinuxThreads, the `clone`-based kernel
 * thread package for Linux which is included in `libc6`.
 *
 * This code no doubt makes some assumptions beyond what is guaranteed by
 * the `pthreads` standard, though it now does very little of that.
 * It now also supports NPTL, and many other POSIX thread implementations.
 * We are trying to merge all flavors of `pthreads` support code into this
 * file.
 */

#ifdef THREADS

#  ifdef GC_PTHREADS
#    if defined(DARWIN) \
        || (defined(GC_WIN32_THREADS) && defined(EMULATE_PTHREAD_SEMAPHORE))
#      include "private/darwin_semaphore.h"
#    elif !defined(PLATFORM_THREADS) && !defined(SN_TARGET_PSP2)
#      include <semaphore.h>
#    endif
#    include <errno.h>
#  endif /* GC_PTHREADS */

#  if !defined(GC_WIN32_THREADS)
#    include <sched.h>
#    include <time.h>
#    if !defined(PLATFORM_THREADS) && !defined(SN_TARGET_PSP2)
#      ifndef RTEMS
#        include <sys/mman.h>
#      endif
#      include <fcntl.h>
#      include <sys/stat.h>
#      include <sys/time.h>
#    endif
#    if defined(GC_EXPLICIT_SIGNALS_UNBLOCK) \
        || !defined(GC_NO_PTHREAD_SIGMASK)   \
        || (defined(GC_PTHREADS_PARAMARK)    \
            && !defined(NO_MARKER_SPECIAL_SIGMASK))
#      include <signal.h>
#    endif
#  endif /* !GC_WIN32_THREADS */

#  ifdef E2K
#    include <alloca.h>
#  endif

#  if defined(DARWIN) || defined(ANY_BSD)
#    if defined(NETBSD) || defined(OPENBSD)
#      include <sys/param.h>
#    endif
#    include <sys/sysctl.h>
#  elif defined(DGUX)
#    include <sys/_int_psem.h>
#    include <sys/dg_sys_info.h>
/* Note: `sem_t` is `uint` in DG/UX. */
typedef unsigned int sem_t;
#  endif

#  if defined(GC_PTHREADS) && !defined(PLATFORM_THREADS) \
      && !defined(SN_TARGET_PSP2)
/* Undefine macros used to redirect `pthreads` primitives. */
#    undef pthread_create
#    ifndef GC_NO_PTHREAD_SIGMASK
#      undef pthread_sigmask
#    endif
#    ifndef GC_NO_PTHREAD_CANCEL
#      undef pthread_cancel
#    endif
#    ifdef GC_HAVE_PTHREAD_EXIT
#      undef pthread_exit
#    endif
#    undef pthread_join
#    undef pthread_detach
#    if defined(OSF1) && defined(_PTHREAD_USE_MANGLED_NAMES_) \
        && !defined(_PTHREAD_USE_PTDNAM_)
/* Restore the original mangled names on Tru64 UNIX. */
#      define pthread_create __pthread_create
#      define pthread_join __pthread_join
#      define pthread_detach __pthread_detach
#      ifndef GC_NO_PTHREAD_CANCEL
#        define pthread_cancel __pthread_cancel
#      endif
#      ifdef GC_HAVE_PTHREAD_EXIT
#        define pthread_exit __pthread_exit
#      endif
#    endif
#  endif /* GC_PTHREADS */

#  if !defined(GC_WIN32_THREADS) && !defined(PLATFORM_THREADS) \
      && !defined(SN_TARGET_PSP2)
/* TODO: Enable `GC_USE_DLOPEN_WRAP` for Cygwin? */

#    ifdef GC_USE_LD_WRAP
#      define WRAP_FUNC(f) __wrap_##f
#      define REAL_FUNC(f) __real_##f
int REAL_FUNC(pthread_create)(pthread_t *,
                              GC_PTHREAD_CREATE_CONST pthread_attr_t *,
                              void *(*start_routine)(void *), void *);
int REAL_FUNC(pthread_join)(pthread_t, void **);
int REAL_FUNC(pthread_detach)(pthread_t);
#      ifndef GC_NO_PTHREAD_SIGMASK
int REAL_FUNC(pthread_sigmask)(int, const sigset_t *, sigset_t *);
#      endif
#      ifndef GC_NO_PTHREAD_CANCEL
int REAL_FUNC(pthread_cancel)(pthread_t);
#      endif
#      ifdef GC_HAVE_PTHREAD_EXIT
void REAL_FUNC(pthread_exit)(void *) GC_PTHREAD_EXIT_ATTRIBUTE;
#      endif
#    elif defined(GC_USE_DLOPEN_WRAP)
#      include <dlfcn.h>
#      define WRAP_FUNC(f) f
#      define REAL_FUNC(f) GC_real_##f
/*
 * We define both `GC_<fn>` and plain `fn` to be the wrapped function.
 * In that way plain calls work, as do calls from files that include
 * `gc.h` file which redefines `fn` to `GC_<fn>`.
 */
/* FIXME: Needs work for `DARWIN` and True64 (`OSF1`). */
typedef int (*GC_pthread_create_t)(pthread_t *,
                                   GC_PTHREAD_CREATE_CONST pthread_attr_t *,
                                   void *(*)(void *), void *);
static GC_pthread_create_t REAL_FUNC(pthread_create);
#      ifndef GC_NO_PTHREAD_SIGMASK
typedef int (*GC_pthread_sigmask_t)(int, const sigset_t *, sigset_t *);
static GC_pthread_sigmask_t REAL_FUNC(pthread_sigmask);
#      endif
typedef int (*GC_pthread_join_t)(pthread_t, void **);
static GC_pthread_join_t REAL_FUNC(pthread_join);
typedef int (*GC_pthread_detach_t)(pthread_t);
static GC_pthread_detach_t REAL_FUNC(pthread_detach);
#      ifndef GC_NO_PTHREAD_CANCEL
typedef int (*GC_pthread_cancel_t)(pthread_t);
static GC_pthread_cancel_t REAL_FUNC(pthread_cancel);
#      endif
#      ifdef GC_HAVE_PTHREAD_EXIT
typedef void (*GC_pthread_exit_t)(void *) GC_PTHREAD_EXIT_ATTRIBUTE;
static GC_pthread_exit_t REAL_FUNC(pthread_exit);
#      endif
#    else
#      define WRAP_FUNC(f) GC_##f
#      ifdef DGUX
#        define REAL_FUNC(f) __d10_##f
#      else
#        define REAL_FUNC(f) f
#      endif
#    endif /* !GC_USE_LD_WRAP && !GC_USE_DLOPEN_WRAP */

/*
 * Define `GC_` functions as aliases for the plain ones, which will
 * be intercepted.  This allows files that include `gc.h` file, and
 * hence generate references to the `GC_` symbols, to see the right ones.
 */
#    if defined(GC_USE_LD_WRAP) || defined(GC_USE_DLOPEN_WRAP)

GC_API int
GC_pthread_create(pthread_t *t, GC_PTHREAD_CREATE_CONST pthread_attr_t *a,
                  void *(*fn)(void *), void *arg)
{
  return pthread_create(t, a, fn, arg);
}

#      ifndef GC_NO_PTHREAD_SIGMASK
GC_API int
GC_pthread_sigmask(int how, const sigset_t *mask, sigset_t *old)
{
  return pthread_sigmask(how, mask, old);
}
#      endif /* !GC_NO_PTHREAD_SIGMASK */

GC_API int
GC_pthread_join(pthread_t t, void **res)
{
  return pthread_join(t, res);
}

GC_API int
GC_pthread_detach(pthread_t t)
{
  return pthread_detach(t);
}

#      ifndef GC_NO_PTHREAD_CANCEL
GC_API int
GC_pthread_cancel(pthread_t t)
{
  return pthread_cancel(t);
}
#      endif /* !GC_NO_PTHREAD_CANCEL */

#      ifdef GC_HAVE_PTHREAD_EXIT
GC_API GC_PTHREAD_EXIT_ATTRIBUTE void
GC_pthread_exit(void *retval)
{
  pthread_exit(retval);
}
#      endif
#    endif /* GC_USE_LD_WRAP || GC_USE_DLOPEN_WRAP */

#    ifdef GC_USE_DLOPEN_WRAP
/*
 * Resolve a symbol with name `n` from the dynamic library (given by
 * handle `h`) and cast it to the given functional type `fn`.
 */
#      define TYPED_DLSYM(fn, h, n) CAST_THRU_UINTPTR(fn, dlsym(h, n))

STATIC void
GC_init_real_syms(void)
{
  void *dl_handle;

  GC_ASSERT(!GC_syms_wrap_initialized);
#      ifdef RTLD_NEXT
  dl_handle = RTLD_NEXT;
#      else
  dl_handle = dlopen("libpthread.so.0", RTLD_LAZY);
  if (NULL == dl_handle) {
    /* Retry without ".0" suffix. */
    dl_handle = dlopen("libpthread.so", RTLD_LAZY);
    if (NULL == dl_handle)
      ABORT("Couldn't open libpthread");
  }
#      endif
  REAL_FUNC(pthread_create)
      = TYPED_DLSYM(GC_pthread_create_t, dl_handle, "pthread_create");
#      ifdef RTLD_NEXT
  if (REAL_FUNC(pthread_create) == 0)
    ABORT("pthread_create not found"
          " (probably -lgc is specified after -lpthread)");
#      endif
#      ifndef GC_NO_PTHREAD_SIGMASK
  REAL_FUNC(pthread_sigmask)
      = TYPED_DLSYM(GC_pthread_sigmask_t, dl_handle, "pthread_sigmask");
#      endif
  REAL_FUNC(pthread_join)
      = TYPED_DLSYM(GC_pthread_join_t, dl_handle, "pthread_join");
  REAL_FUNC(pthread_detach)
      = TYPED_DLSYM(GC_pthread_detach_t, dl_handle, "pthread_detach");
#      ifndef GC_NO_PTHREAD_CANCEL
  REAL_FUNC(pthread_cancel)
      = TYPED_DLSYM(GC_pthread_cancel_t, dl_handle, "pthread_cancel");
#      endif
#      ifdef GC_HAVE_PTHREAD_EXIT
  REAL_FUNC(pthread_exit)
      = TYPED_DLSYM(GC_pthread_exit_t, dl_handle, "pthread_exit");
#      endif
  GC_syms_wrap_initialized = TRUE;
}

#      define INIT_REAL_SYMS()                  \
        if (LIKELY(GC_syms_wrap_initialized)) { \
        } else                                  \
          GC_init_real_syms()
#    else
#      define INIT_REAL_SYMS() (void)0
#    endif /* !GC_USE_DLOPEN_WRAP */

#  else
#    define WRAP_FUNC(f) GC_##f
#    define REAL_FUNC(f) f
#    define INIT_REAL_SYMS() (void)0
#  endif /* GC_WIN32_THREADS */

#  if defined(MPROTECT_VDB) && defined(DARWIN)
GC_INNER int
GC_inner_pthread_create(pthread_t *t,
                        GC_PTHREAD_CREATE_CONST pthread_attr_t *a,
                        void *(*fn)(void *), void *arg)
{
  INIT_REAL_SYMS();
  return REAL_FUNC(pthread_create)(t, a, fn, arg);
}
#  endif

#  ifdef THREAD_LOCAL_ALLOC

GC_INNER void
GC_mark_thread_local_free_lists(void)
{
  int i;
  GC_thread p;

  for (i = 0; i < THREAD_TABLE_SZ; ++i) {
    for (p = GC_threads[i]; p != NULL; p = p->tm.next) {
      if (!KNOWN_FINISHED(p))
        GC_mark_thread_local_fls_for(&p->tlfs);
    }
  }
}

#    if defined(GC_ASSERTIONS)
/*
 * Check that all thread-local free-lists are completely marked.
 * Also check that thread-specific-data structures are marked.
 */
void
GC_check_tls(void)
{
  int i;
  GC_thread p;

  for (i = 0; i < THREAD_TABLE_SZ; ++i) {
    for (p = GC_threads[i]; p != NULL; p = p->tm.next) {
      if (!KNOWN_FINISHED(p))
        GC_check_tls_for(&p->tlfs);
    }
  }
#      if defined(USE_CUSTOM_SPECIFIC)
  if (GC_thread_key != 0)
    GC_check_tsd_marks(GC_thread_key);
#      endif
}
#    endif

#  endif /* THREAD_LOCAL_ALLOC */

#  ifdef GC_WIN32_THREADS
/*
 * A macro for functions and variables that should be accessible
 * from `win32_threads.c` file but otherwise could be `static`.
 */
#    define GC_INNER_WIN32THREAD GC_INNER
#  else
#    define GC_INNER_WIN32THREAD STATIC
#  endif

#  ifdef PARALLEL_MARK

#    if defined(GC_WIN32_THREADS) || defined(USE_PROC_FOR_LIBRARIES) \
        || (defined(IA64)                                            \
            && (defined(HAVE_PTHREAD_ATTR_GET_NP)                    \
                || defined(HAVE_PTHREAD_GETATTR_NP)))
GC_INNER_WIN32THREAD ptr_t GC_marker_sp[MAX_MARKERS - 1] = { 0 };
#    endif /* GC_WIN32_THREADS || USE_PROC_FOR_LIBRARIES */

#    if defined(IA64) && defined(USE_PROC_FOR_LIBRARIES)
static ptr_t marker_bsp[MAX_MARKERS - 1] = { 0 };
#    endif

#    if defined(DARWIN) && !defined(GC_NO_THREADS_DISCOVERY)
static mach_port_t marker_mach_threads[MAX_MARKERS - 1] = { 0 };

GC_INNER GC_bool
GC_is_mach_marker(thread_act_t thread)
{
  int i;
  for (i = 0; i < GC_markers_m1; i++) {
    if (marker_mach_threads[i] == thread)
      return TRUE;
  }
  return FALSE;
}
#    endif /* DARWIN && !GC_NO_THREADS_DISCOVERY */

#    ifdef HAVE_PTHREAD_SETNAME_NP_WITH_TID_AND_ARG
/* For NetBSD. */
static void
set_marker_thread_name(unsigned id)
{
  int err = pthread_setname_np(pthread_self(), "GC-marker-%zu",
                               NUMERIC_TO_VPTR(id));
  if (UNLIKELY(err != 0))
    WARN("pthread_setname_np failed, errno= %" WARN_PRIdPTR "\n",
         (GC_signed_word)err);
}

#    elif defined(HAVE_PTHREAD_SETNAME_NP_WITH_TID)     \
        || defined(HAVE_PTHREAD_SETNAME_NP_WITHOUT_TID) \
        || defined(HAVE_PTHREAD_SET_NAME_NP)
#      ifdef HAVE_PTHREAD_SET_NAME_NP
#        include <pthread_np.h>
#      endif
static void
set_marker_thread_name(unsigned id)
{
  /*
   * Note: a smaller size of the buffer may result in
   * "output may be truncated" compiler warning.
   */
  char name_buf[10 + 20 + 1];

  GC_snprintf_s_ld_s(name_buf, sizeof(name_buf), "GC-marker-", (long)id, "");
#      ifdef HAVE_PTHREAD_SETNAME_NP_WITHOUT_TID
  /* The iOS or OS X case. */
  (void)pthread_setname_np(name_buf);
#      elif defined(HAVE_PTHREAD_SET_NAME_NP)
  /* The OpenBSD case. */
  pthread_set_name_np(pthread_self(), name_buf);
#      else
  /* The case of Linux, Solaris, etc. */
  GC_ASSERT(strlen(name_buf) < 16);
  /* `pthread_setname_np()` may fail for longer names. */
  if (UNLIKELY(pthread_setname_np(pthread_self(), name_buf) != 0))
    WARN("pthread_setname_np failed\n", 0);
#      endif
}

#    elif defined(GC_WIN32_THREADS) && !defined(MSWINCE)
/*
 * A pointer to `SetThreadDescription()` which is available since Windows 10.
 * The function prototype is in the platform `processthreadsapi.h` file.
 */
static FARPROC setThreadDescription_fn;

GC_INNER void
GC_init_win32_thread_naming(HMODULE hK32)
{
  if (hK32)
    setThreadDescription_fn = GetProcAddress(hK32, "SetThreadDescription");
}

static void
set_marker_thread_name(unsigned id)
{
  WCHAR name_buf[16];
  int len = sizeof(L"GC-marker-") / sizeof(WCHAR) - 1;
  HRESULT hr;

  if (!setThreadDescription_fn) {
    /* `SetThreadDescription()` is missing. */
    return;
  }

  /* Compose the name manually as `swprintf` may be unavailable. */
  BCOPY(L"GC-marker-", name_buf, len * sizeof(WCHAR));
  if (id >= 10)
    name_buf[len++] = (WCHAR)('0' + (id / 10) % 10);
  name_buf[len] = (WCHAR)('0' + id % 10);
  name_buf[len + 1] = 0;

  /*
   * Invoke `SetThreadDescription()`.  Cast the function pointer to
   * `GC_funcptr_uint` first to avoid "incompatible function types"
   * compiler warning.
   */
  hr = (*(HRESULT(WINAPI *)(HANDLE, const WCHAR *))(
      GC_funcptr_uint)setThreadDescription_fn)(GetCurrentThread(), name_buf);
  if (hr < 0)
    WARN("SetThreadDescription failed\n", 0);
}
#    else
#      define set_marker_thread_name(id) (void)(id)
#    endif

GC_INNER_WIN32THREAD
#    ifdef GC_PTHREADS_PARAMARK
void *
GC_mark_thread(void *id)
#    elif defined(MSWINCE)
DWORD WINAPI
GC_mark_thread(LPVOID id)
#    else
unsigned __stdcall GC_mark_thread(void *id)
#    endif
{
  word my_mark_no = 0;
  word id_n = (word)(GC_uintptr_t)id;
  IF_CANCEL(int cancel_state;)

  if (id_n == GC_WORD_MAX)
    return 0; /*< to prevent a compiler warning */

  /*
   * Mark threads are not cancellable; they should be invisible to
   * client.
   */
  DISABLE_CANCEL(cancel_state);

  set_marker_thread_name((unsigned)id_n);
#    if defined(GC_WIN32_THREADS) || defined(USE_PROC_FOR_LIBRARIES) \
        || (defined(IA64)                                            \
            && (defined(HAVE_PTHREAD_ATTR_GET_NP)                    \
                || defined(HAVE_PTHREAD_GETATTR_NP)))
  GC_marker_sp[id_n] = GC_approx_sp();
#    endif
#    if defined(IA64) && defined(USE_PROC_FOR_LIBRARIES)
  marker_bsp[id_n] = GC_save_regs_in_stack();
#    endif
#    if defined(DARWIN) && !defined(GC_NO_THREADS_DISCOVERY)
  marker_mach_threads[id_n] = mach_thread_self();
#    endif
#    if !defined(GC_PTHREADS_PARAMARK)
  GC_marker_Id[id_n] = thread_id_self();
#    endif

  /* Inform `GC_start_mark_threads` about completion of marker data init. */
  GC_acquire_mark_lock();
  /* Note: the count variable may have a negative value. */
  if (0 == --GC_fl_builder_count)
    GC_notify_all_builder();

  /*
   * `GC_mark_no` is passed only to allow `GC_help_marker` to
   * terminate promptly.  This is important if it were called from the
   * signal handler or from the allocator lock acquisition code.
   * On Linux, it is not safe to call it from a signal handler, since
   * it uses mutex and condition variables.  Since it is called only
   * here, the argument is unnecessary.
   */
  for (;; ++my_mark_no) {
    if (my_mark_no - GC_mark_no > (word)2) {
      /* Resynchronize if we get far off, e.g. because `GC_mark_no` wrapped. */
      my_mark_no = GC_mark_no;
    }
#    ifdef DEBUG_THREADS
    GC_log_printf("Starting helper for mark number %lu (thread %u)\n",
                  (unsigned long)my_mark_no, (unsigned)id_n);
#    endif
    GC_help_marker(my_mark_no);
  }
}

GC_INNER_WIN32THREAD int GC_available_markers_m1 = 0;

#  endif /* PARALLEL_MARK */

#  ifdef GC_PTHREADS_PARAMARK

#    ifdef GLIBC_2_1_MUTEX_HACK
/*
 * Ugly workaround for a Linux threads bug in the final versions
 * of `glibc` 2.1.  `pthread_mutex_trylock` sets the mutex owner
 * field even when it fails to acquire the mutex.  This causes
 * `pthread_cond_wait` to die.  Should not be needed for `glibc` 2.2.
 * According to the man page, we should use
 * `PTHREAD_ERRORCHECK_MUTEX_INITIALIZER_NP`, but that is not actually
 * defined.
 */
static pthread_mutex_t mark_mutex
    = { 0, 0, 0, PTHREAD_MUTEX_ERRORCHECK_NP, { 0, 0 } };
#    else
static pthread_mutex_t mark_mutex = PTHREAD_MUTEX_INITIALIZER;
#    endif

#    ifdef CAN_HANDLE_FORK
/* Note: this is initialized by `GC_start_mark_threads_inner()`. */
static pthread_cond_t mark_cv;
#    else
static pthread_cond_t mark_cv = PTHREAD_COND_INITIALIZER;
#    endif

GC_INNER void
GC_start_mark_threads_inner(void)
{
  int i;
  pthread_attr_t attr;
#    ifndef NO_MARKER_SPECIAL_SIGMASK
  sigset_t set, oldset;
#    endif

  GC_ASSERT(I_HOLD_LOCK());
  ASSERT_CANCEL_DISABLED();
  if (GC_available_markers_m1 <= 0 || GC_parallel) {
    /* Skip if parallel markers disabled or already started. */
    return;
  }
  GC_wait_for_gc_completion(TRUE);

#    ifdef CAN_HANDLE_FORK
  /*
   * Initialize `mark_cv` (for the first time), or cleanup its value
   * after forking in the child process.  All the marker threads in the
   * parent process were blocked on this variable at process fork, so
   * `pthread_cond_wait()` malfunction (hang) is possible in the child
   * process without such a cleanup.
   */

  /*
   * TODO: This is not portable, it is better to shortly unblock all
   * marker threads in the parent process at `fork`.
   */
  {
    pthread_cond_t mark_cv_local = PTHREAD_COND_INITIALIZER;
    BCOPY(&mark_cv_local, &mark_cv, sizeof(mark_cv));
  }
#    endif

  GC_ASSERT(0 == GC_fl_builder_count);
  INIT_REAL_SYMS(); /*< for `pthread_create` */

  if (pthread_attr_init(&attr) != 0)
    ABORT("pthread_attr_init failed");
  if (pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED) != 0)
    ABORT("pthread_attr_setdetachstate failed");

#    ifdef DEFAULT_STACK_MAYBE_SMALL
  /*
   * The default stack size is usually too small: increase it.
   * Otherwise marker threads may run out of space.
   */
  {
    size_t old_size;

    if (pthread_attr_getstacksize(&attr, &old_size) != 0)
      ABORT("pthread_attr_getstacksize failed");
    if (old_size < MIN_STACK_SIZE && old_size != 0 /* stack size is known */) {
      if (pthread_attr_setstacksize(&attr, MIN_STACK_SIZE) != 0)
        ABORT("pthread_attr_setstacksize failed");
    }
  }
#    endif /* DEFAULT_STACK_MAYBE_SMALL */

#    ifndef NO_MARKER_SPECIAL_SIGMASK
  /*
   * Apply special signal mask to GC marker threads, and do not drop
   * user-defined signals by the marker threads.
   */
  if (sigfillset(&set) != 0)
    ABORT("sigfillset failed");

#      ifdef SIGNAL_BASED_STOP_WORLD
  /* These are used by GC to stop and restart the world. */
  if (sigdelset(&set, GC_get_suspend_signal()) != 0
      || sigdelset(&set, GC_get_thr_restart_signal()) != 0)
    ABORT("sigdelset failed");
#      endif

  if (UNLIKELY(REAL_FUNC(pthread_sigmask)(SIG_BLOCK, &set, &oldset) != 0)) {
    WARN("pthread_sigmask set failed, no markers started\n", 0);
    GC_markers_m1 = 0;
    (void)pthread_attr_destroy(&attr);
    return;
  }
#    endif /* !NO_MARKER_SPECIAL_SIGMASK */

  /* To have proper `GC_parallel` value in `GC_help_marker()`. */
  GC_markers_m1 = GC_available_markers_m1;

  for (i = 0; i < GC_available_markers_m1; ++i) {
    pthread_t new_thread;

#    ifdef GC_WIN32_THREADS
    GC_marker_last_stack_min[i] = ADDR_LIMIT;
#    endif
    if (UNLIKELY(REAL_FUNC(pthread_create)(&new_thread, &attr, GC_mark_thread,
                                           NUMERIC_TO_VPTR(i))
                 != 0)) {
      WARN("Marker thread %" WARN_PRIdPTR " creation failed\n",
           (GC_signed_word)i);
      /* Do not try to create other marker threads. */
      GC_markers_m1 = i;
      break;
    }
  }

#    ifndef NO_MARKER_SPECIAL_SIGMASK
  /* Restore previous signal mask. */
  if (UNLIKELY(REAL_FUNC(pthread_sigmask)(SIG_SETMASK, &oldset, NULL) != 0)) {
    WARN("pthread_sigmask restore failed\n", 0);
  }
#    endif

  (void)pthread_attr_destroy(&attr);
  GC_wait_for_markers_init();
  GC_COND_LOG_PRINTF("Started %d mark helper threads\n", GC_markers_m1);
}

#  endif /* GC_PTHREADS_PARAMARK */

GC_INNER GC_thread GC_threads[THREAD_TABLE_SZ] = { 0 };

/*
 * It may not be safe to allocate when we register the first thread.
 * Note that `next` and `status` fields are unused, but there might be
 * some other fields (`crtn`) to be pushed.
 */
static struct GC_StackContext_Rep first_crtn;
static struct GC_Thread_Rep first_thread;

/*
 * A place to retain a pointer to an allocated object while a thread
 * registration is ongoing.  Protected by the allocator lock.
 */
static GC_stack_context_t saved_crtn = NULL;

GC_INNER void
GC_push_thread_structures(void)
{
  GC_ASSERT(I_HOLD_LOCK());
#  if !defined(GC_NO_THREADS_DISCOVERY) && defined(GC_WIN32_THREADS)
  if (GC_win32_dll_threads) {
    /*
     * Unlike the other threads implementations, the thread table here
     * contains no pointers to the collectible heap (note also that
     * `GC_PTHREADS` is incompatible with `DllMain`-based thread
     * registration).  Thus we have no private structures we need to
     * preserve.
     */
  } else
#  endif
  /* else */ {
    GC_push_all(&GC_threads, (ptr_t)(&GC_threads) + sizeof(GC_threads));
    GC_ASSERT(NULL == first_thread.tm.next);
#  ifdef GC_PTHREADS
    GC_ASSERT(NULL == first_thread.status);
#  endif
    GC_PUSH_ALL_SYM(first_thread.crtn);
    GC_PUSH_ALL_SYM(saved_crtn);
  }
#  if defined(THREAD_LOCAL_ALLOC) && defined(USE_CUSTOM_SPECIFIC)
  GC_PUSH_ALL_SYM(GC_thread_key);
#  endif
}

#  if defined(MPROTECT_VDB) && defined(GC_WIN32_THREADS)
GC_INNER void
GC_win32_unprotect_thread(GC_thread t)
{
  GC_ASSERT(I_HOLD_LOCK());
  if (!GC_win32_dll_threads && GC_auto_incremental) {
    GC_stack_context_t crtn = t->crtn;

    if (crtn != &first_crtn) {
      GC_ASSERT(SMALL_OBJ(GC_size(crtn)));
      GC_remove_protection(HBLKPTR(crtn), 1, FALSE);
    }
    if (t != &first_thread) {
      GC_ASSERT(SMALL_OBJ(GC_size(t)));
      GC_remove_protection(HBLKPTR(t), 1, FALSE);
    }
  }
}
#  endif /* MPROTECT_VDB && GC_WIN32_THREADS */

#  ifdef DEBUG_THREADS
STATIC int
GC_count_threads(void)
{
  int i;
  int count = 0;

#    if !defined(GC_NO_THREADS_DISCOVERY) && defined(GC_WIN32_THREADS)
  if (GC_win32_dll_threads)
    return -1; /*< not implemented */
#    endif
  GC_ASSERT(I_HOLD_READER_LOCK());
  for (i = 0; i < THREAD_TABLE_SZ; ++i) {
    GC_thread p;

    for (p = GC_threads[i]; p != NULL; p = p->tm.next) {
      if (!KNOWN_FINISHED(p))
        ++count;
    }
  }
  return count;
}
#  endif /* DEBUG_THREADS */

GC_INNER_WIN32THREAD GC_thread
GC_new_thread(thread_id_t self_id)
{
  int hv = THREAD_TABLE_INDEX(self_id);
  GC_thread result;

  GC_ASSERT(I_HOLD_LOCK());
#  ifdef DEBUG_THREADS
  GC_log_printf("Creating thread %p\n", THREAD_ID_TO_VPTR(self_id));
  for (result = GC_threads[hv]; result != NULL; result = result->tm.next)
    if (!THREAD_ID_EQUAL(result->id, self_id)) {
      GC_log_printf("Hash collision at GC_threads[%d]\n", hv);
      break;
    }
#  endif
  if (UNLIKELY(NULL == first_thread.crtn)) {
    result = &first_thread;
    first_thread.crtn = &first_crtn;
    GC_ASSERT(NULL == GC_threads[hv]);
#  if defined(CPPCHECK) && defined(THREAD_SANITIZER) \
      && defined(SIGNAL_BASED_STOP_WORLD)
    GC_noop1((unsigned char)first_crtn.dummy[0]);
#  endif
  } else {
    GC_stack_context_t crtn;

    GC_ASSERT(!GC_win32_dll_threads);
    GC_ASSERT(!GC_in_thread_creation);
    GC_in_thread_creation = TRUE; /*< OK to collect from unknown thread */
    crtn = (GC_stack_context_t)GC_INTERNAL_MALLOC(
        sizeof(struct GC_StackContext_Rep), NORMAL);

    /*
     * The current stack is not scanned until the thread is registered,
     * thus `crtn` pointer is to be retained in the global data roots for
     * a while (and pushed explicitly if a collection occurs here).
     */
    GC_ASSERT(NULL == saved_crtn);
    saved_crtn = crtn;
    result
        = (GC_thread)GC_INTERNAL_MALLOC(sizeof(struct GC_Thread_Rep), NORMAL);
    /* No more collections till thread is registered. */
    saved_crtn = NULL;
    GC_in_thread_creation = FALSE;
    if (NULL == crtn || NULL == result)
      ABORT("Failed to allocate memory for thread registering");
    result->crtn = crtn;
  }
  /* The `id` field is not set here. */
#  ifdef USE_TKILL_ON_ANDROID
  result->kernel_id = gettid();
#  endif
  result->tm.next = GC_threads[hv];
  GC_threads[hv] = result;
#  ifdef NACL
  GC_nacl_initialize_gc_thread(result);
#  endif
  GC_ASSERT(0 == result->flags);
  if (LIKELY(result != &first_thread))
    GC_dirty(result);
  return result;
}

GC_INNER_WIN32THREAD void
GC_delete_thread(GC_thread t)
{
#  if !defined(GC_NO_THREADS_DISCOVERY) && defined(GC_WIN32_THREADS)
  if (GC_win32_dll_threads) {
    HANDLE handle = t->handle;

    GC_cptr_store_release(&t->handle, NULL);
    CloseHandle(handle);
    /*
     * This is intended to be lock-free.  It is either called synchronously
     * from the thread being deleted, or by the joining thread.  In this
     * branch asynchronous changes to `*t` are possible.  Note that it is
     * not allowed to call `GC_printf` (and the friends) here, see
     * `GC_stop_world()` in `win32_threads.c` file for the information.
     */
    t->crtn->stack_end = NULL;
    t->id = 0;
    /* The thread is not suspended. */
    t->flags = 0;
#    ifdef RETRY_GET_THREAD_CONTEXT
    t->context_sp = NULL;
#    endif
    AO_store_release(&t->tm.in_use, FALSE);
  } else
#  endif
  /* else */ {
    thread_id_t id = t->id;
    int hv = THREAD_TABLE_INDEX(id);
    GC_thread p;
    GC_thread prev = NULL;

    GC_ASSERT(I_HOLD_LOCK());
#  if defined(DEBUG_THREADS) && !defined(MSWINCE) \
      && (!defined(MSWIN32) || defined(CONSOLE_LOG))
    GC_log_printf("Deleting thread %p, n_threads= %d\n", THREAD_ID_TO_VPTR(id),
                  GC_count_threads());
#  endif
#  if defined(GC_WIN32_THREADS) && !defined(MSWINCE)
    CloseHandle(t->handle);
#  endif
    for (p = GC_threads[hv]; p != t; p = p->tm.next) {
      prev = p;
    }
    if (NULL == prev) {
      GC_threads[hv] = p->tm.next;
    } else {
      GC_ASSERT(prev != &first_thread);
      prev->tm.next = p->tm.next;
      GC_dirty(prev);
    }
    if (LIKELY(p != &first_thread)) {
#  ifdef DARWIN
      mach_port_deallocate(mach_task_self(), p->mach_thread);
#  endif
      GC_ASSERT(p->crtn != &first_crtn);
      GC_INTERNAL_FREE(p->crtn);
      GC_INTERNAL_FREE(p);
    }
  }
}

GC_INNER GC_thread
GC_lookup_thread(thread_id_t id)
{
  GC_thread p;

#  if !defined(GC_NO_THREADS_DISCOVERY) && defined(GC_WIN32_THREADS)
  if (GC_win32_dll_threads)
    return GC_win32_dll_lookup_thread(id);
#  endif
  for (p = GC_threads[THREAD_TABLE_INDEX(id)]; p != NULL; p = p->tm.next) {
    if (LIKELY(THREAD_ID_EQUAL(p->id, id)))
      break;
  }
  return p;
}

/*
 * Same as `GC_self_thread_inner()` but acquires the allocator lock (in
 * the reader mode).
 */
STATIC GC_thread
GC_self_thread(void)
{
  GC_thread p;

  READER_LOCK();
  p = GC_self_thread_inner();
  READER_UNLOCK();
  return p;
}

#  ifndef GC_NO_FINALIZATION
GC_INNER void
GC_reset_finalizer_nested(void)
{
  GC_ASSERT(I_HOLD_LOCK());
  GC_self_thread_inner()->crtn->finalizer_nested = 0;
}

GC_INNER unsigned char *
GC_check_finalizer_nested(void)
{
  GC_thread me;
  GC_stack_context_t crtn;
  unsigned nesting_level;

  GC_ASSERT(I_HOLD_LOCK());
  me = GC_self_thread_inner();
#    if defined(INCLUDE_LINUX_THREAD_DESCR) && defined(REDIRECT_MALLOC)
  /*
   * As noted in `GC_pthread_start`, an allocation may happen in
   * `GC_get_stack_base`, causing `GC_notify_or_invoke_finalizers`
   * to be called before the thread gets registered.
   */
  if (UNLIKELY(NULL == me))
    return NULL;
#    endif
  crtn = me->crtn;
  nesting_level = crtn->finalizer_nested;
  if (nesting_level) {
    /*
     * We are inside another `GC_invoke_finalizers()`.  Skip some
     * implicitly-called `GC_invoke_finalizers()` depending on the
     * nesting (recursion) level.
     */
    if ((unsigned)(++crtn->finalizer_skipped) < (1U << nesting_level))
      return NULL;
    crtn->finalizer_skipped = 0;
  }
  crtn->finalizer_nested = (unsigned char)(nesting_level + 1);
  return &crtn->finalizer_nested;
}
#  endif /* !GC_NO_FINALIZATION */

#  define ADDR_INSIDE_OBJ(p, obj) \
    ADDR_INSIDE(p, (ptr_t)(&(obj)), (ptr_t)(&(obj)) + sizeof(obj))

#  if defined(GC_ASSERTIONS) && defined(THREAD_LOCAL_ALLOC)
/* This is called from thread-local `GC_malloc()`. */
GC_bool
GC_is_thread_tsd_valid(void *tsd)
{
  GC_thread me = GC_self_thread();

  return ADDR_INSIDE_OBJ((ptr_t)tsd, me->tlfs);
}
#  endif /* GC_ASSERTIONS && THREAD_LOCAL_ALLOC */

GC_API int GC_CALL
GC_thread_is_registered(void)
{
  /* TODO: Use `GC_get_tlfs()` instead. */
  GC_thread me = GC_self_thread();

  return me != NULL && !KNOWN_FINISHED(me);
}

GC_API void GC_CALL
GC_register_altstack(void *normstack, size_t normstack_size, void *altstack,
                     size_t altstack_size)
{
#  ifdef GC_WIN32_THREADS
  /* TODO: Implement. */
  UNUSED_ARG(normstack);
  UNUSED_ARG(normstack_size);
  UNUSED_ARG(altstack);
  UNUSED_ARG(altstack_size);
#  else
  GC_thread me;
  GC_stack_context_t crtn;

  READER_LOCK();
  me = GC_self_thread_inner();
  if (UNLIKELY(NULL == me)) {
    /* We are called before `GC_thr_init()`. */
    me = &first_thread;
  }
  crtn = me->crtn;
  crtn->normstack = (ptr_t)normstack;
  crtn->normstack_size = normstack_size;
  crtn->altstack = (ptr_t)altstack;
  crtn->altstack_size = altstack_size;
  READER_UNLOCK_RELEASE();
#  endif
}

#  ifdef USE_PROC_FOR_LIBRARIES
GC_INNER GC_bool
GC_segment_is_thread_stack(ptr_t lo, ptr_t hi)
{
  int i;
  GC_thread p;

  GC_ASSERT(I_HOLD_READER_LOCK());
#    ifdef PARALLEL_MARK
  for (i = 0; i < GC_markers_m1; ++i) {
    if (ADDR_LT(lo, GC_marker_sp[i]) && ADDR_LT(GC_marker_sp[i], hi))
      return TRUE;
#      ifdef IA64
    if (ADDR_LT(lo, marker_bsp[i]) && ADDR_LT(marker_bsp[i], hi))
      return TRUE;
#      endif
  }
#    endif
  for (i = 0; i < THREAD_TABLE_SZ; i++) {
    for (p = GC_threads[i]; p != NULL; p = p->tm.next) {
      ptr_t stack_end = p->crtn->stack_end;

      if (stack_end != NULL) {
#    ifdef STACK_GROWS_UP
        if (ADDR_INSIDE(stack_end, lo, hi))
          return TRUE;
#    else
        if (ADDR_LT(lo, stack_end) && ADDR_GE(hi, stack_end))
          return TRUE;
#    endif
      }
    }
  }
  return FALSE;
}
#  endif /* USE_PROC_FOR_LIBRARIES */

#  if (defined(HAVE_PTHREAD_ATTR_GET_NP) || defined(HAVE_PTHREAD_GETATTR_NP)) \
      && defined(IA64)
GC_INNER ptr_t
GC_greatest_stack_base_below(ptr_t bound)
{
  int i;
  GC_thread p;
  ptr_t result = NULL;

  GC_ASSERT(I_HOLD_READER_LOCK());
#    ifdef PARALLEL_MARK
  for (i = 0; i < GC_markers_m1; ++i) {
    if (ADDR_LT(result, GC_marker_sp[i]) && ADDR_LT(GC_marker_sp[i], bound))
      result = GC_marker_sp[i];
  }
#    endif
  for (i = 0; i < THREAD_TABLE_SZ; i++) {
    for (p = GC_threads[i]; p != NULL; p = p->tm.next) {
      ptr_t stack_end = p->crtn->stack_end;

      if (ADDR_LT(result, stack_end) && ADDR_LT(stack_end, bound))
        result = stack_end;
    }
  }
  return result;
}
#  endif /* IA64 */

#  ifndef STAT_READ
/*
 * Note: if `read()` is wrapped, this may need to be redefined to call
 * the real one.
 */
#    define STAT_READ read
#  endif

#  ifdef HPUX
#    define GC_get_nprocs() pthread_num_processors_np()

#  elif defined(AIX) || defined(COSMO) || defined(HAIKU)         \
      || defined(HOST_ANDROID) || defined(HURD) || defined(NACL) \
      || defined(OSF1) || defined(SOLARIS)
GC_INLINE int
GC_get_nprocs(void)
{
  int nprocs = (int)sysconf(_SC_NPROCESSORS_ONLN);
  /* Note: ignore any error silently. */
  return nprocs > 0 ? nprocs : 1;
}

#  elif defined(IRIX5)
GC_INLINE int
GC_get_nprocs(void)
{
  int nprocs = (int)sysconf(_SC_NPROC_ONLN);
  /* Note: ignore any error silently. */
  return nprocs > 0 ? nprocs : 1;
}

#  elif defined(LINUX)
/* Return the number of processors. */
STATIC int
GC_get_nprocs(void)
{
  /*
   * Should be just `return sysconf(_SC_NPROCESSORS_ONLN)` but that
   * appears to be buggy in many cases.  We look for lines "cpu<N>" in
   * `/proc/stat` pseudo-file.  No need to read the entire `/proc/stat`
   * pseudo-file to get maximum "cpu<N>" such as:
   *   - the requested lines are located at the beginning of the file;
   *   - the lines with "cpu<N>" where `N` is greater than `MAX_MARKERS`
   *     are not needed.
   */
#    define PROC_STAT_BUF_SZ ((1 + MAX_MARKERS) * 100)
  char stat_buf[PROC_STAT_BUF_SZ + 1]; /*< the size should be enough */
  int f;
  int result, i, len;

  f = open("/proc/stat", O_RDONLY);
  if (f < 0) {
    WARN("Could not open /proc/stat\n", 0);
    /* Assume an uniprocessor. */
    return 1;
  }
  len = STAT_READ(f, stat_buf, sizeof(stat_buf) - 1);
  /* Unlikely that we need to retry because of an incomplete read here. */
  if (len < 0) {
    WARN("Failed to read /proc/stat, errno= %" WARN_PRIdPTR "\n",
         (GC_signed_word)errno);
    close(f);
    return 1;
  }
  /* Avoid potential buffer overrun by `atoi()`. */
  stat_buf[len] = '\0';

  close(f);

  /*
   * Some old kernels only have a single "cpu nnnn ..." entry in
   * `/proc/stat` pseudo-file.  We identify those as uniprocessors.
   */
  result = 1;

  for (i = 0; i < len - 4; ++i) {
    if (stat_buf[i] == '\n' && stat_buf[i + 1] == 'c' && stat_buf[i + 2] == 'p'
        && stat_buf[i + 3] == 'u') {
      int cpu_no = atoi(&stat_buf[i + 4]);
      if (cpu_no >= result)
        result = cpu_no + 1;
    }
  }
  return result;
}

#  elif defined(DGUX)
/*
 * Return the number of processors, or a non-positive value if the number
 * cannot be determined.
 */
STATIC int
GC_get_nprocs(void)
{
  int numCpus;
  struct dg_sys_info_pm_info pm_sysinfo;
  int status = 0;

  status = dg_sys_info((long int *)&pm_sysinfo, DG_SYS_INFO_PM_INFO_TYPE,
                       DG_SYS_INFO_PM_CURRENT_VERSION);
  if (status < 0) {
    /* Set -1 for an error. */
    numCpus = -1;
  } else {
    /* Active CPUs. */
    numCpus = pm_sysinfo.idle_vp_count;
  }
  return numCpus;
}

#  elif defined(ANY_BSD) || defined(DARWIN)
STATIC int
GC_get_nprocs(void)
{
  int mib[] = { CTL_HW, HW_NCPU };
  int res;
  size_t len = sizeof(res);

  sysctl(mib, sizeof(mib) / sizeof(int), &res, &len, NULL, 0);
  return res;
}

#  else
/* E.g., RTEMS. */
/* TODO: Implement. */
#    define GC_get_nprocs() 1
#  endif

#  if defined(LINUX) && defined(ARM32)
/*
 * Some buggy Linux/arm kernels show only non-sleeping CPUs in
 * `/proc/stat` pseudo-file (and in `/proc/cpuinfo` pseudo-file), so
 * another data system source is tried first.  Returns a non-positive
 * value on error.
 */
STATIC int
GC_get_nprocs_present(void)
{
  char stat_buf[16];
  int f;
  int len;

  f = open("/sys/devices/system/cpu/present", O_RDONLY);
  if (f < 0) {
    /* Cannot open the file. */
    return -1;
  }

  len = STAT_READ(f, stat_buf, sizeof(stat_buf));
  close(f);

  /*
   * Recognized file format: "0\n" or "0-<max_cpu_num>\n".
   * The file might probably contain a comma-separated list
   * but we do not need to handle it (just silently ignore).
   */
  if (len < 2 || stat_buf[0] != '0' || stat_buf[len - 1] != '\n') {
    /* A read error or an unrecognized content. */
    return 0;
  } else if (len == 2) {
    /* An uniprocessor. */
    return 1;
  } else if (stat_buf[1] != '-') {
    /* An unrecognized content. */
    return 0;
  }

  /* Terminate the string. */
  stat_buf[len - 1] = '\0';

  /* Skip "0-" and parse `max_cpu_num`. */
  return atoi(&stat_buf[2]) + 1;
}
#  endif /* LINUX && ARM32 */

#  if defined(CAN_HANDLE_FORK) && defined(THREAD_SANITIZER)
#    include "private/gc_pmark.h" /*< for `MS_NONE` */

/*
 * Workaround for TSan which does not notice that the allocator lock
 * is acquired in `fork_prepare_proc()`.
 */
GC_ATTR_NO_SANITIZE_THREAD
static GC_bool
collection_in_progress(void)
{
  return GC_mark_state != MS_NONE;
}
#  else
#    define collection_in_progress() GC_collection_in_progress()
#  endif

GC_INNER void
GC_wait_for_gc_completion(GC_bool wait_for_all)
{
#  if !defined(THREAD_SANITIZER) || !defined(CAN_CALL_ATFORK)
  /*
   * `GC_lock_holder` is accessed with the allocator lock held, so
   * there is no data race actually (unlike what is reported by TSan).
   */
  GC_ASSERT(I_HOLD_LOCK());
#  endif
  ASSERT_CANCEL_DISABLED();
#  ifdef GC_DISABLE_INCREMENTAL
  (void)wait_for_all;
#  else
  if (GC_incremental && collection_in_progress()) {
    word old_gc_no = GC_gc_no;

    /*
     * Make sure that no part of our stack is still on the mark stack,
     * since it is about to be unmapped.
     */
#    ifdef LINT2
    /*
     * Note: do not transform this `if`-`do`-`while` construction into
     * a single while statement because it might cause some static code
     * analyzers to report a false positive (FP) code defect about
     * missing unlock after lock.
     */
#    endif
    do {
      GC_ASSERT(!GC_in_thread_creation);
      GC_in_thread_creation = TRUE;
      GC_collect_a_little_inner(1);
      GC_in_thread_creation = FALSE;

      UNLOCK();
#    ifdef GC_WIN32_THREADS
      Sleep(0);
#    else
      sched_yield();
#    endif
      LOCK();
    } while (GC_incremental && collection_in_progress()
             && (wait_for_all || old_gc_no == GC_gc_no));
  }
#  endif
}

#  if defined(GC_ASSERTIONS) && defined(GC_PTHREADS_PARAMARK)
STATIC unsigned long GC_mark_lock_holder = NO_THREAD;
#  endif

#  ifdef CAN_HANDLE_FORK

/*
 * Procedures called before and after a process fork.  The goal here is
 * to make it safe to call `GC_malloc()` in the forked child process.
 * It is unclear that is attainable, since the Single UNIX Specification
 * seems to imply that one should only call async-signal-safe functions,
 * and we probably cannot quite guarantee that.  But we give it our best
 * shot.  (That same specification also implies that it is not safe to
 * call the system `malloc` between `fork` and `exec`.  Thus we are doing
 * no worse than it.)
 */

IF_CANCEL(static int fork_cancel_state;) /*< protected by the allocator lock */

#    ifdef PARALLEL_MARK
#      ifdef THREAD_SANITIZER
#        if defined(GC_ASSERTIONS) && defined(CAN_CALL_ATFORK)
STATIC void GC_generic_lock(pthread_mutex_t *);
#        endif
GC_ATTR_NO_SANITIZE_THREAD
static void wait_for_reclaim_atfork(void);
#      else
#        define wait_for_reclaim_atfork() GC_wait_for_reclaim()
#      endif
#    endif /* PARALLEL_MARK */

/*
 * Prevent TSan false positive (FP) about the race during items removal
 * from `GC_threads`.  (The race cannot happen since only one thread
 * survives in the child process.)
 */
#    ifdef CAN_CALL_ATFORK
GC_ATTR_NO_SANITIZE_THREAD
#    endif
static void
store_to_threads_table(int hv, GC_thread me)
{
  GC_threads[hv] = me;
}

/*
 * Remove all entries from the `GC_threads` table, except the one for
 * the current thread.  Also update thread identifiers stored in the
 * table for the current thread.  We need to do this in the child process
 * after a `fork()`, since only the current thread survives in the child
 * process.
 */
STATIC void
GC_remove_all_threads_but_me(void)
{
  int hv;
  GC_thread me = NULL;
#    ifndef GC_WIN32_THREADS
#      define pthread_id id
#    endif

  for (hv = 0; hv < THREAD_TABLE_SZ; ++hv) {
    GC_thread p, next;

    for (p = GC_threads[hv]; p != NULL; p = next) {
      next = p->tm.next;
      if (THREAD_EQUAL(p->pthread_id, GC_parent_pthread_self) && me == NULL) {
        /* Ignore dead threads with the same id. */
        me = p;
        p->tm.next = NULL;
      } else {
#    ifdef THREAD_LOCAL_ALLOC
        if (!KNOWN_FINISHED(p)) {
          /*
           * Cannot call `GC_destroy_thread_local` here.  The free
           * lists may be in an inconsistent state (as thread `p` may
           * be updating one of the lists by `GC_generic_malloc_many()`
           * or `GC_FAST_MALLOC_GRANS()` when `fork()` is invoked).
           * This should not be a problem because the lost elements
           * of the free lists will be collected during GC.
           */
          GC_remove_specific_after_fork(GC_thread_key, p->pthread_id);
        }
#    endif
        /*
         * TODO: To avoid TSan hang (when updating `GC_bytes_freed`),
         * we just skip explicit freeing of `GC_threads` entries.
         */
#    if !defined(THREAD_SANITIZER) || !defined(CAN_CALL_ATFORK)
        if (p != &first_thread) {
          /* TODO: Should call `mach_port_deallocate`? */
          GC_ASSERT(p->crtn != &first_crtn);
          GC_INTERNAL_FREE(p->crtn);
          GC_INTERNAL_FREE(p);
        }
#    endif
      }
    }
    store_to_threads_table(hv, NULL);
  }

#    if defined(CPPCHECK) || defined(LINT2)
  if (NULL == me)
    ABORT("Current thread is not found after fork");
#    else
  GC_ASSERT(me != NULL);
#    endif
  /*
   * Update `pthreads` id as it is not guaranteed to be the same between
   * this (child) process and the parent one.
   */
  me->pthread_id = pthread_self();
#    ifdef GC_WIN32_THREADS
  /*
   * Update Win32 thread id and handle.  They differ from that in the
   * parent process.
   */
  me->id = thread_id_self();
#      ifndef MSWINCE
  if (!DuplicateHandle(GetCurrentProcess(), GetCurrentThread(),
                       GetCurrentProcess(), (HANDLE *)&me->handle,
                       0 /* `dwDesiredAccess` */, FALSE /* `bInheritHandle` */,
                       DUPLICATE_SAME_ACCESS))
    ABORT("DuplicateHandle failed");
#      endif
#    endif
#    ifdef DARWIN
  /*
   * Update thread id after process fork (it is OK to call
   * `GC_destroy_thread_local()` and `GC_free_inner()` before update).
   */
  me->mach_thread = mach_thread_self();
#    endif
#    ifdef USE_TKILL_ON_ANDROID
  me->kernel_id = gettid();
#    endif

  /* Put `me` back to `GC_threads`. */
  store_to_threads_table(THREAD_TABLE_INDEX(me->id), me);

#    ifdef THREAD_LOCAL_ALLOC
#      ifdef USE_CUSTOM_SPECIFIC
  GC_update_specific_after_fork(GC_thread_key);
#      else
  /*
   * Some TLS implementations (e.g., in Cygwin) might be not `fork`-friendly,
   * so we re-assign thread-local pointer to `tlfs` for safety instead of the
   * assertion check (again, it is OK to call `GC_destroy_thread_local()` and
   * `GC_free_inner()` before).
   */
  {
    int res = GC_setspecific(GC_thread_key, &me->tlfs);

    if (COVERT_DATAFLOW(res) != 0)
      ABORT("GC_setspecific failed (in child)");
  }
#      endif
#    endif
#    undef pthread_id
}

/* Called before a `fork()`. */
#    if defined(GC_ASSERTIONS) && defined(CAN_CALL_ATFORK)
/* `GC_lock_holder` is updated safely (no data race actually). */
GC_ATTR_NO_SANITIZE_THREAD
#    endif
static void
fork_prepare_proc(void)
{
#    if defined(GC_EXPLICIT_SIGNALS_UNBLOCK) && defined(CAN_CALL_ATFORK)
  /*
   * The signals might be blocked by `fork()` implementation when the
   * at-fork prepare handler is invoked.
   */
  if (GC_handle_fork == 1)
    GC_unblock_gc_signals();
#    endif

  /*
   * Acquire all relevant locks, so that after releasing the locks the child
   * process will see a consistent state in which monitor invariants hold.
   * Unfortunately, we cannot acquire `libc` locks we might need, and there
   * seems to be no guarantee that `libc` must install a suitable `fork`
   * handler.  Wait for an ongoing collection to finish, since we cannot
   * finish it in the (one remaining thread in) the child process.
   */

  LOCK();
  DISABLE_CANCEL(fork_cancel_state);
  GC_parent_pthread_self = pthread_self();
  /* The following waits may include cancellation points. */
#    ifdef PARALLEL_MARK
  if (GC_parallel)
    wait_for_reclaim_atfork();
#    endif
  GC_wait_for_gc_completion(TRUE);
#    ifdef PARALLEL_MARK
  if (GC_parallel) {
#      if defined(THREAD_SANITIZER) && defined(GC_ASSERTIONS) \
          && defined(CAN_CALL_ATFORK)
    /*
     * Prevent TSan false positive (FP) about the data race when updating
     * `GC_mark_lock_holder`.
     */
    GC_generic_lock(&mark_mutex);
#      else
    GC_acquire_mark_lock();
#      endif
  }
#    endif
  GC_acquire_dirty_lock();
}

/*
 * Called in the parent process after a `fork()` (even if the latter
 * has failed).
 */
#    if defined(GC_ASSERTIONS) && defined(CAN_CALL_ATFORK)
GC_ATTR_NO_SANITIZE_THREAD
#    endif
static void
fork_parent_proc(void)
{
  GC_release_dirty_lock();
#    ifdef PARALLEL_MARK
  if (GC_parallel) {
#      if defined(THREAD_SANITIZER) && defined(GC_ASSERTIONS) \
          && defined(CAN_CALL_ATFORK)
    /* To match that in `fork_prepare_proc`. */
    (void)pthread_mutex_unlock(&mark_mutex);
#      else
    GC_release_mark_lock();
#      endif
  }
#    endif
  RESTORE_CANCEL(fork_cancel_state);
#    ifdef GC_ASSERTIONS
  BZERO(&GC_parent_pthread_self, sizeof(pthread_t));
#    endif
  UNLOCK();
}

/* Called in the child process after a `fork()`. */
#    if defined(GC_ASSERTIONS) && defined(CAN_CALL_ATFORK)
GC_ATTR_NO_SANITIZE_THREAD
#    endif
static void
fork_child_proc(void)
{
#    ifdef GC_ASSERTIONS
  /*
   * Update `GC_lock_holder` as value of `thread_id_self()` might differ
   * from that of the parent process.
   */
  SET_LOCK_HOLDER();
#    endif
  GC_release_dirty_lock();
#    ifndef GC_DISABLE_INCREMENTAL
  GC_dirty_update_child();
#    endif
#    ifdef PARALLEL_MARK
  if (GC_parallel) {
#      ifdef GC_WIN32_THREADS
    GC_release_mark_lock();
#      else
#        if !defined(GC_ASSERTIONS) \
            || (defined(THREAD_SANITIZER) && defined(CAN_CALL_ATFORK))
    /* Do not change `GC_mark_lock_holder`. */
#        else
    GC_mark_lock_holder = NO_THREAD;
#        endif
    /*
     * The unlock operation may fail on some targets, just ignore
     * the error silently.
     */
    (void)pthread_mutex_unlock(&mark_mutex);
    /*
     * Reinitialize the mark lock.  The reason is the same as for
     * `GC_allocate_ml` below.
     */
    (void)pthread_mutex_destroy(&mark_mutex);
    /* TODO: `GLIBC_2_19_TSX_BUG` has no effect. */
    if (pthread_mutex_init(&mark_mutex, NULL) != 0)
      ABORT("mark_mutex re-init failed in child");
#      endif
    /*
     * Turn off parallel marking in the child process, since we are probably
     * just going to exec, and we would have to restart mark threads.
     */
    GC_parallel = FALSE;
  }
#      ifdef THREAD_SANITIZER
  /* TSan does not support threads creation in the child process. */
  GC_available_markers_m1 = 0;
#      endif
#    endif
  /* Clean up the thread table, so that just our thread is left. */
  GC_remove_all_threads_but_me();
  GC_stackbase_info_update_after_fork();
  RESTORE_CANCEL(fork_cancel_state);
#    ifdef GC_ASSERTIONS
  BZERO(&GC_parent_pthread_self, sizeof(pthread_t));
#    endif
  UNLOCK();
  /*
   * Even though after a `fork()` the child process only inherits the
   * single thread that called the `fork()`, if another thread in the
   * parent process was attempting to lock the mutex while being held
   * in `fork_child_prepare()`, the mutex will be left in
   * an inconsistent state in the child process after the `UNLOCK()`.
   * This is the case, at least, in Mac OS X and leads to an unusable
   * collector in the child process which will block when attempting
   * to perform any GC operation that acquires the allocator lock.
   */
#    if defined(USE_PTHREAD_LOCKS) && !defined(GC_WIN32_THREADS)
  GC_ASSERT(I_DONT_HOLD_LOCK());
  /*
   * Reinitialize the mutex.  It should be safe since we are running
   * this in the child process which only inherits a single thread.
   * `pthread_mutex_destroy()` and `pthread_rwlock_destroy()` may
   * return `EBUSY`, which makes no sense, but that is the reason for
   * the need of the reinitialization.
   * Note: excluded for Cygwin as does not seem to be needed.
   */
#      ifdef USE_RWLOCK
  (void)pthread_rwlock_destroy(&GC_allocate_ml);
#        ifdef DARWIN
  /* A workaround for `pthread_rwlock_init()` fail with `EBUSY`. */
  {
    pthread_rwlock_t rwlock_local = PTHREAD_RWLOCK_INITIALIZER;
    BCOPY(&rwlock_local, &GC_allocate_ml, sizeof(GC_allocate_ml));
  }
#        else
  if (pthread_rwlock_init(&GC_allocate_ml, NULL) != 0)
    ABORT("pthread_rwlock_init failed (in child)");
#        endif
#      else
  (void)pthread_mutex_destroy(&GC_allocate_ml);
  /*
   * TODO: Probably some targets (e.g. with `GLIBC_2_19_TSX_BUG`) might
   * need the default mutex attribute to be passed instead of `NULL`.
   */
  if (pthread_mutex_init(&GC_allocate_ml, NULL) != 0)
    ABORT("pthread_mutex_init failed (in child)");
#      endif
#    endif
}

/*
 * Routines for `fork()` handling by client (no-op if `pthread_atfork`
 * works).
 */

GC_API void GC_CALL
GC_atfork_prepare(void)
{
  if (UNLIKELY(!GC_is_initialized))
    GC_init();
  if (GC_handle_fork <= 0)
    fork_prepare_proc();
}

GC_API void GC_CALL
GC_atfork_parent(void)
{
  if (GC_handle_fork <= 0)
    fork_parent_proc();
}

GC_API void GC_CALL
GC_atfork_child(void)
{
  if (GC_handle_fork <= 0)
    fork_child_proc();
}

GC_INNER_WIN32THREAD void
GC_setup_atfork(void)
{
  if (GC_handle_fork) {
#    ifdef CAN_CALL_ATFORK
    if (pthread_atfork(fork_prepare_proc, fork_parent_proc, fork_child_proc)
        == 0) {
      /* Handlers successfully registered. */
      GC_handle_fork = 1;
    } else
#    endif
    /* else */ {
      if (GC_handle_fork != -1)
        ABORT("pthread_atfork failed");
    }
  }
}

#  endif /* CAN_HANDLE_FORK */

#  ifdef INCLUDE_LINUX_THREAD_DESCR
__thread int GC_dummy_thread_local;
#  endif

#  ifdef PARALLEL_MARK
#    ifndef GC_WIN32_THREADS
static void setup_mark_lock(void);
#    endif

GC_INNER_WIN32THREAD unsigned GC_required_markers_cnt = 0;

GC_API void GC_CALL
GC_set_markers_count(unsigned markers)
{
  GC_required_markers_cnt = markers < MAX_MARKERS ? markers : MAX_MARKERS;
}
#  endif /* PARALLEL_MARK */

GC_INNER GC_bool GC_in_thread_creation = FALSE;

GC_INNER_WIN32THREAD void
GC_record_stack_base(GC_stack_context_t crtn, const struct GC_stack_base *sb)
{
#  if !defined(DARWIN) && !defined(GC_WIN32_THREADS)
  crtn->stack_ptr = (ptr_t)sb->mem_base;
#  endif
  if ((crtn->stack_end = (ptr_t)sb->mem_base) == NULL)
    ABORT("Bad stack base in GC_register_my_thread");
#  ifdef E2K
  crtn->ps_ofs = (size_t)(GC_uintptr_t)sb->reg_base;
#  elif defined(IA64)
  crtn->backing_store_end = (ptr_t)sb->reg_base;
#  elif defined(I386) && defined(GC_WIN32_THREADS)
  crtn->initial_stack_base = (ptr_t)sb->mem_base;
#  endif
}

#  if !defined(GC_NO_THREADS_DISCOVERY) && defined(GC_WIN32_THREADS) \
      || !defined(DONT_USE_ATEXIT)
GC_INNER_WIN32THREAD thread_id_t GC_main_thread_id;
#  endif

#  ifndef DONT_USE_ATEXIT
GC_INNER GC_bool
GC_is_main_thread(void)
{
  GC_ASSERT(GC_thr_initialized);
  return THREAD_ID_EQUAL(GC_main_thread_id, thread_id_self());
}
#  endif /* !DONT_USE_ATEXIT */

#  ifndef GC_WIN32_THREADS

STATIC GC_thread
GC_register_my_thread_inner(const struct GC_stack_base *sb,
                            thread_id_t self_id)
{
  GC_thread me;

  GC_ASSERT(I_HOLD_LOCK());
  me = GC_new_thread(self_id);
  me->id = self_id;
#    ifdef DARWIN
  me->mach_thread = mach_thread_self();
#    endif
  GC_record_stack_base(me->crtn, sb);
  return me;
}

/*
 * Number of processors.  We may not have access to all of them, but
 * this is as good a guess as any...
 */
STATIC int GC_nprocs = 1;

GC_INNER void
GC_thr_init(void)
{
  GC_ASSERT(I_HOLD_LOCK());
  GC_ASSERT(!GC_thr_initialized);
  GC_ASSERT(ADDR(&GC_threads) % ALIGNMENT == 0);
#    ifdef GC_ASSERTIONS
  GC_thr_initialized = TRUE;
#    endif
#    ifdef CAN_HANDLE_FORK
  GC_setup_atfork();
#    endif

#    ifdef INCLUDE_LINUX_THREAD_DESCR
  /*
   * Explicitly register the region including the address of a thread-local
   * variable.  This should include thread locals for the main thread,
   * except for those allocated in response to `dlopen()` calls.
   */
  {
    ptr_t thread_local_addr = (ptr_t)(&GC_dummy_thread_local);
    ptr_t main_thread_start, main_thread_end;
    if (!GC_enclosing_writable_mapping(thread_local_addr, &main_thread_start,
                                       &main_thread_end)) {
      ABORT("Failed to find TLS mapping for the primordial thread");
    } else {
      /* `main_thread_start` and `main_thread_end` are initialized. */
      GC_add_roots_inner(main_thread_start, main_thread_end, FALSE);
    }
  }
#    endif

  /* Set `GC_nprocs` and `GC_available_markers_m1` variables. */
  {
    const char *nprocs_string = GETENV("GC_NPROCS");
    GC_nprocs = -1;
    if (nprocs_string != NULL)
      GC_nprocs = atoi(nprocs_string);
  }
  if (GC_nprocs <= 0
#    if defined(LINUX) && defined(ARM32)
      /* Workaround for some Linux/arm kernels. */
      && (GC_nprocs = GC_get_nprocs_present()) <= 1
#    endif
  ) {
    GC_nprocs = GC_get_nprocs();
  }
  if (GC_nprocs <= 0) {
    WARN("GC_get_nprocs() returned %" WARN_PRIdPTR "\n",
         (GC_signed_word)GC_nprocs);
    /* Assume a dual-core CPU. */
    GC_nprocs = 2;
#    ifdef PARALLEL_MARK
    /* But use only one marker. */
    GC_available_markers_m1 = 0;
#    endif
  } else {
#    ifdef PARALLEL_MARK
    {
      const char *markers_string = GETENV("GC_MARKERS");
      int markers = GC_required_markers_cnt;

      if (markers_string != NULL) {
        markers = atoi(markers_string);
        if (markers <= 0 || markers > MAX_MARKERS) {
          WARN("Too big or invalid number of mark threads: %" WARN_PRIdPTR
               "; using maximum threads\n",
               (GC_signed_word)markers);
          markers = MAX_MARKERS;
        }
      } else if (0 == markers) {
        /*
         * Unless the client sets the desired number of parallel markers,
         * it is determined based on the number of CPU cores.
         */
        markers = GC_nprocs;
#      if defined(GC_MIN_MARKERS) && !defined(CPPCHECK)
        /* This is primarily for targets without `getenv()`. */
        if (markers < GC_MIN_MARKERS)
          markers = GC_MIN_MARKERS;
#      endif
        if (markers > MAX_MARKERS) {
          /* Silently limit the amount of markers. */
          markers = MAX_MARKERS;
        }
      }
      GC_available_markers_m1 = markers - 1;
    }
#    endif
  }
  GC_COND_LOG_PRINTF("Number of processors: %d\n", GC_nprocs);

#    if defined(BASE_ATOMIC_OPS_EMULATED) && defined(SIGNAL_BASED_STOP_WORLD)
  /*
   * Ensure the process is running on just one CPU core.  This is needed
   * because the AO primitives emulated with locks cannot be used inside
   * signal handlers.
   */
  {
    cpu_set_t mask;
    int cpu_set_cnt = 0;
    int cpu_lowest_set = 0;
#      ifdef RANDOM_ONE_CPU_CORE
    int cpu_highest_set = 0;
#      endif
    /* Ensure at least 2 cores. */
    int i = GC_nprocs > 1 ? GC_nprocs : 2;

    if (sched_getaffinity(0 /* current process */, sizeof(mask), &mask) == -1)
      ABORT_ARG1("sched_getaffinity failed", ": errno= %d", errno);
    while (i-- > 0)
      if (CPU_ISSET(i, &mask)) {
#      ifdef RANDOM_ONE_CPU_CORE
        if (i + 1 != cpu_lowest_set)
          cpu_highest_set = i;
#      endif
        cpu_lowest_set = i;
        cpu_set_cnt++;
      }
    if (0 == cpu_set_cnt)
      ABORT("sched_getaffinity returned empty mask");
    if (cpu_set_cnt > 1) {
#      ifdef RANDOM_ONE_CPU_CORE
      if (cpu_lowest_set < cpu_highest_set) {
        /* Pseudo-randomly adjust the bit to set among valid ones. */
        cpu_lowest_set
            += (unsigned)getpid() % (cpu_highest_set - cpu_lowest_set + 1);
      }
#      endif
      CPU_ZERO(&mask);
      /* Select just one CPU. */
      CPU_SET(cpu_lowest_set, &mask);
      if (sched_setaffinity(0, sizeof(mask), &mask) == -1)
        ABORT_ARG1("sched_setaffinity failed", ": errno= %d", errno);
      WARN("CPU affinity mask is set to %p\n", (word)1 << cpu_lowest_set);
    }
  }
#    endif /* BASE_ATOMIC_OPS_EMULATED */

#    ifndef DARWIN
  GC_stop_init();
#    endif

#    ifdef PARALLEL_MARK
  if (GC_available_markers_m1 <= 0) {
    /* Disable parallel marking. */
    GC_parallel = FALSE;
    GC_COND_LOG_PRINTF("Single marker thread, turning off parallel marking\n");
  } else {
    setup_mark_lock();
  }
#    endif

  /* Add the initial thread, so we can stop it. */
  {
    struct GC_stack_base sb;
    GC_thread me;
    thread_id_t self_id = thread_id_self();

    sb.mem_base = GC_stackbottom;
    GC_ASSERT(sb.mem_base != NULL);
#    if defined(E2K) || defined(IA64)
    sb.reg_base = GC_register_stackbottom;
#    endif
    GC_ASSERT(NULL == GC_self_thread_inner());
    me = GC_register_my_thread_inner(&sb, self_id);
#    ifndef DONT_USE_ATEXIT
    GC_main_thread_id = self_id;
#    endif
    me->flags = DETACHED;
  }
}

#  endif /* !GC_WIN32_THREADS */

GC_INNER void
GC_init_parallel(void)
{
#  ifdef THREAD_LOCAL_ALLOC
  GC_thread me;

  GC_ASSERT(GC_is_initialized);
  LOCK();
  me = GC_self_thread_inner();
  GC_init_thread_local(&me->tlfs);
  UNLOCK();
#  endif
#  if !defined(GC_NO_THREADS_DISCOVERY) && defined(GC_WIN32_THREADS)
  if (GC_win32_dll_threads) {
    /*
     * Cannot intercept thread creation.  Hence we do not know if other
     * threads exist.  However, client is not allowed to create other threads
     * before collector initialization.  Thus it is OK not to lock before
     * this.
     */
    set_need_to_lock();
  }
#  endif
}

#  if !defined(GC_NO_PTHREAD_SIGMASK) && defined(GC_PTHREADS)
#    define GC_wrap_pthread_sigmask WRAP_FUNC(pthread_sigmask)
GC_API int
GC_wrap_pthread_sigmask(int how, const sigset_t *set, sigset_t *oset)
{
#    ifdef GC_WIN32_THREADS
  /*
   * `pthreads-win32` library does not support `sigmask`.
   * So, nothing is required here...
   */
#    else
  sigset_t fudged_set;

  INIT_REAL_SYMS();
  if (LIKELY(set != NULL) && (how == SIG_BLOCK || how == SIG_SETMASK)) {
    int sig_suspend = GC_get_suspend_signal();

    fudged_set = *set;
    GC_ASSERT(sig_suspend >= 0);
    if (sigdelset(&fudged_set, sig_suspend) != 0)
      ABORT("sigdelset failed");
    set = &fudged_set;
  }
#    endif
  return REAL_FUNC(pthread_sigmask)(how, set, oset);
}
#    undef GC_wrap_pthread_sigmask
#  endif /* !GC_NO_PTHREAD_SIGMASK */

/*
 * Wrapper for functions that are likely to block for an appreciable
 * length of time.
 */

#  ifdef E2K
/*
 * Cannot be defined as a function because the stack-allocated buffer
 * (pointed to by `bs_lo`) should be preserved till completion of
 * `GC_do_blocking_inner` (or `GC_suspend_self_blocked`).
 */
#    define do_blocking_enter(pTopOfStackUnset, me)                   \
      do {                                                            \
        ptr_t bs_lo;                                                  \
        size_t stack_size;                                            \
        GC_stack_context_t crtn = (me)->crtn;                         \
                                                                      \
        *(pTopOfStackUnset) = FALSE;                                  \
        crtn->stack_ptr = GC_approx_sp();                             \
        GC_ASSERT(NULL == crtn->backing_store_end);                   \
        GET_PROCEDURE_STACK_LOCAL(crtn->ps_ofs, &bs_lo, &stack_size); \
        crtn->backing_store_end = bs_lo;                              \
        crtn->backing_store_ptr = bs_lo + stack_size;                 \
        (me)->flags |= DO_BLOCKING;                                   \
      } while (0)

#  else /* !E2K */
static void
do_blocking_enter(GC_bool *pTopOfStackUnset, GC_thread me)
{
#    if defined(SPARC) || defined(IA64)
  ptr_t bs_hi = GC_save_regs_in_stack();
  /* TODO: Registers saving already done by `GC_with_callee_saves_pushed`. */
#    endif
  GC_stack_context_t crtn = me->crtn;

  GC_ASSERT(I_HOLD_READER_LOCK());
  GC_ASSERT((me->flags & DO_BLOCKING) == 0);
  *pTopOfStackUnset = FALSE;
#    ifdef SPARC
  crtn->stack_ptr = bs_hi;
#    else
  crtn->stack_ptr = GC_approx_sp();
#    endif
#    if defined(DARWIN) && !defined(DARWIN_DONT_PARSE_STACK)
  if (NULL == crtn->topOfStack) {
    /*
     * `GC_do_blocking_inner` is not called recursively, so `topOfStack`
     * should be computed now.
     */
    *pTopOfStackUnset = TRUE;
    crtn->topOfStack = GC_FindTopOfStack(0);
  }
#    endif
#    ifdef IA64
  crtn->backing_store_ptr = bs_hi;
#    endif
  me->flags |= DO_BLOCKING;
  /* Save context here if we want to support precise stack marking. */
}
#  endif /* !E2K */

static void
do_blocking_leave(GC_thread me, GC_bool topOfStackUnset)
{
  GC_ASSERT(I_HOLD_READER_LOCK());
  me->flags &= (unsigned char)~DO_BLOCKING;
#  ifdef E2K
  {
    GC_stack_context_t crtn = me->crtn;

    GC_ASSERT(crtn->backing_store_end != NULL);
    crtn->backing_store_ptr = NULL;
    crtn->backing_store_end = NULL;
  }
#  endif
#  if defined(DARWIN) && !defined(DARWIN_DONT_PARSE_STACK)
  if (topOfStackUnset) {
    /* Make it unset again. */
    me->crtn->topOfStack = NULL;
  }
#  else
  (void)topOfStackUnset;
#  endif
}

GC_INNER void
GC_do_blocking_inner(ptr_t data, void *context)
{
  GC_thread me;
  GC_bool topOfStackUnset;

  UNUSED_ARG(context);
  READER_LOCK();
  me = GC_self_thread_inner();
  do_blocking_enter(&topOfStackUnset, me);
  READER_UNLOCK_RELEASE();

  ((struct blocking_data *)data)->client_data /*< result */
      = ((struct blocking_data *)data)
            ->fn(((struct blocking_data *)data)->client_data);

  /* This will block if the world is stopped. */
  READER_LOCK();

#  ifdef LINT2
  {
#    ifdef GC_ASSERTIONS
    GC_thread saved_me = me;
#    endif

    /*
     * The pointer to the GC thread descriptor should not be changed while
     * the thread is registered but a static analysis tool might complain
     * that this pointer value (obtained in the first locked section) is
     * unreliable in the second locked section.
     */
    me = GC_self_thread_inner();
    GC_ASSERT(me == saved_me);
  }
#  endif
#  if defined(GC_ENABLE_SUSPEND_THREAD) && defined(SIGNAL_BASED_STOP_WORLD)
  /*
   * Note: this code cannot be moved into `do_blocking_leave()` otherwise
   * there could be a static analysis tool warning (false positive) about
   * unlock without a matching lock.
   */
  while (UNLIKELY((me->ext_suspend_cnt & 1) != 0)) {
    /* Read suspend counter (number) before unlocking. */
    size_t suspend_cnt = me->ext_suspend_cnt;

    READER_UNLOCK_RELEASE();
    GC_suspend_self_inner(me, suspend_cnt);
    READER_LOCK();
  }
#  endif
  do_blocking_leave(me, topOfStackUnset);
  READER_UNLOCK_RELEASE();
}

#  if defined(GC_ENABLE_SUSPEND_THREAD) && defined(SIGNAL_BASED_STOP_WORLD)
GC_INNER void
GC_suspend_self_blocked(ptr_t thread_me, void *context)
{
  GC_thread me = (GC_thread)thread_me;
  GC_bool topOfStackUnset;

  UNUSED_ARG(context);

  /*
   * The caller holds the allocator lock in the exclusive mode, thus
   * we require and restore it to the same mode upon return from the
   * function.
   */
  GC_ASSERT(I_HOLD_LOCK());

  do_blocking_enter(&topOfStackUnset, me);
  while ((me->ext_suspend_cnt & 1) != 0) {
    size_t suspend_cnt = me->ext_suspend_cnt;

    UNLOCK();
    GC_suspend_self_inner(me, suspend_cnt);
    LOCK();
  }
  do_blocking_leave(me, topOfStackUnset);
}
#  endif /* GC_ENABLE_SUSPEND_THREAD */

GC_API void GC_CALL
GC_set_stackbottom(void *gc_thread_handle, const struct GC_stack_base *sb)
{
  GC_thread t = (GC_thread)gc_thread_handle;
  GC_stack_context_t crtn;

  GC_ASSERT(sb->mem_base != NULL);
  if (UNLIKELY(!GC_is_initialized)) {
    GC_ASSERT(NULL == t);
    /* Alter the stack bottom of the primordial thread. */
    GC_stackbottom = (char *)sb->mem_base;
#  if defined(E2K) || defined(IA64)
    GC_register_stackbottom = (ptr_t)sb->reg_base;
#  endif
    return;
  }

  GC_ASSERT(I_HOLD_READER_LOCK());
  if (NULL == t) {
    /* The current thread. */
    t = GC_self_thread_inner();
  }
  GC_ASSERT(!KNOWN_FINISHED(t));
  crtn = t->crtn;
  GC_ASSERT((t->flags & DO_BLOCKING) == 0
            && NULL == crtn->traced_stack_sect); /*< for now */

  crtn->stack_end = (ptr_t)sb->mem_base;
#  ifdef E2K
  crtn->ps_ofs = (size_t)(GC_uintptr_t)sb->reg_base;
#  elif defined(IA64)
  crtn->backing_store_end = (ptr_t)sb->reg_base;
#  endif
#  ifdef GC_WIN32_THREADS
  /* Reset the known minimum (hottest address in the stack). */
  crtn->last_stack_min = ADDR_LIMIT;
#  endif
}

GC_API void *GC_CALL
GC_get_my_stackbottom(struct GC_stack_base *sb)
{
  GC_thread me;
  GC_stack_context_t crtn;

  READER_LOCK();
  me = GC_self_thread_inner();
  /* The thread is assumed to be registered. */
  crtn = me->crtn;
  sb->mem_base = crtn->stack_end;
#  ifdef E2K
  /* Store the offset in the procedure stack, not address. */
  sb->reg_base = NUMERIC_TO_VPTR(crtn->ps_ofs);
#  elif defined(IA64)
  sb->reg_base = crtn->backing_store_end;
#  endif
  READER_UNLOCK();
  return me; /*< `gc_thread_handle` */
}

GC_ATTR_NOINLINE
GC_API void *GC_CALL
GC_call_with_gc_active(GC_fn_type fn, void *client_data)
{
  struct GC_traced_stack_sect_s stacksect;
  GC_thread me;
  GC_stack_context_t crtn;
  ptr_t stack_end;
#  ifdef E2K
  ptr_t saved_bs_ptr, saved_bs_end;
  size_t saved_ps_ofs;
#  endif

  /* This will block if the world is stopped. */
  READER_LOCK();

  me = GC_self_thread_inner();
  crtn = me->crtn;

  /*
   * Adjust our stack bottom value (this could happen unless
   * `GC_get_stack_base()` was used which returned `GC_SUCCESS`).
   */
  stack_end = crtn->stack_end; /*< read of a `volatile` field */
  GC_ASSERT(stack_end != NULL);
  STORE_APPROX_SP_TO(*(volatile ptr_t *)&stacksect.saved_stack_ptr);
  if (HOTTER_THAN(stack_end, stacksect.saved_stack_ptr)) {
    crtn->stack_end = stacksect.saved_stack_ptr;
#  if defined(I386) && defined(GC_WIN32_THREADS)
    crtn->initial_stack_base = stacksect.saved_stack_ptr;
#  endif
  }

  if ((me->flags & DO_BLOCKING) == 0) {
    /* We are not inside `GC_do_blocking()` - do nothing more. */
    READER_UNLOCK_RELEASE();
    /* Cast `fn` to a `volatile` type to prevent its call inlining. */
    client_data = (*(GC_fn_type volatile *)&fn)(client_data);
    /* Prevent treating the above as a tail call. */
    GC_noop1(COVERT_DATAFLOW(ADDR(&stacksect)));
    return client_data; /*< result */
  }

#  if defined(GC_ENABLE_SUSPEND_THREAD) && defined(SIGNAL_BASED_STOP_WORLD)
  while (UNLIKELY((me->ext_suspend_cnt & 1) != 0)) {
    size_t suspend_cnt = me->ext_suspend_cnt;

    READER_UNLOCK_RELEASE();
    GC_suspend_self_inner(me, suspend_cnt);
    READER_LOCK();
    GC_ASSERT(me->crtn == crtn);
  }
#  endif

  /* Setup new "stack section". */
  stacksect.saved_stack_ptr = crtn->stack_ptr;
#  ifdef E2K
  GC_ASSERT(crtn->backing_store_end != NULL);
  {
    unsigned long long sz_ull;

    GET_PROCEDURE_STACK_SIZE_INNER(&sz_ull);
    saved_ps_ofs = crtn->ps_ofs;
    GC_ASSERT(saved_ps_ofs <= (size_t)sz_ull);
    crtn->ps_ofs = (size_t)sz_ull;
  }
  saved_bs_end = crtn->backing_store_end;
  saved_bs_ptr = crtn->backing_store_ptr;
  crtn->backing_store_ptr = NULL;
  crtn->backing_store_end = NULL;
#  elif defined(IA64)
  /* This is the same as in `GC_call_with_stack_base()`. */
  stacksect.backing_store_end = GC_save_regs_in_stack();
  /*
   * Unnecessarily flushes the register stack, but that probably does
   * not hurt.
   */
  stacksect.saved_backing_store_ptr = crtn->backing_store_ptr;
#  endif
  stacksect.prev = crtn->traced_stack_sect;
  me->flags &= (unsigned char)~DO_BLOCKING;
  crtn->traced_stack_sect = &stacksect;

  READER_UNLOCK_RELEASE();
  client_data = (*(GC_fn_type volatile *)&fn)(client_data);
  GC_ASSERT((me->flags & DO_BLOCKING) == 0);

  /* Restore original "stack section". */
  READER_LOCK();
  GC_ASSERT(me->crtn == crtn);
  GC_ASSERT(crtn->traced_stack_sect == &stacksect);
#  ifdef CPPCHECK
  GC_noop1_ptr(crtn->traced_stack_sect);
#  endif
  crtn->traced_stack_sect = stacksect.prev;
#  ifdef E2K
  GC_ASSERT(NULL == crtn->backing_store_end);
  crtn->backing_store_end = saved_bs_end;
  crtn->backing_store_ptr = saved_bs_ptr;
  crtn->ps_ofs = saved_ps_ofs;
#  elif defined(IA64)
  crtn->backing_store_ptr = stacksect.saved_backing_store_ptr;
#  endif
  me->flags |= DO_BLOCKING;
  crtn->stack_ptr = stacksect.saved_stack_ptr;
  READER_UNLOCK_RELEASE();
  return client_data; /*< result */
}

STATIC void
GC_unregister_my_thread_inner(GC_thread me)
{
  GC_ASSERT(I_HOLD_LOCK());
#  ifdef DEBUG_THREADS
  GC_log_printf("Unregistering thread %p, gc_thread= %p, n_threads= %d\n",
                THREAD_ID_TO_VPTR(me->id), (void *)me, GC_count_threads());
#  endif
  GC_ASSERT(!KNOWN_FINISHED(me));
#  if defined(THREAD_LOCAL_ALLOC)
  GC_destroy_thread_local(&me->tlfs);
#  endif
#  ifdef NACL
  GC_nacl_shutdown_gc_thread();
#  endif
#  ifdef GC_PTHREADS
#    if defined(GC_HAVE_PTHREAD_EXIT) || !defined(GC_NO_PTHREAD_CANCEL)
  /*
   * Handle `DISABLED_GC` flag which is set by the intercepted
   * `pthread_cancel()` or `pthread_exit()`.
   */
  if ((me->flags & DISABLED_GC) != 0) {
    GC_dont_gc--;
  }
#    endif
  if ((me->flags & DETACHED) == 0) {
    me->flags |= FINISHED;
  } else
#  endif
  /* else */ {
    GC_delete_thread(me);
  }
#  if defined(THREAD_LOCAL_ALLOC)
  /*
   * It is required to call `GC_remove_specific()` defined in
   * `specific.c` file.
   */
  GC_remove_specific(GC_thread_key);
#  endif
}

GC_API int GC_CALL
GC_unregister_my_thread(void)
{
  GC_thread me;
  IF_CANCEL(int cancel_state;)

  /*
   * Client should not unregister the thread explicitly
   * if it is registered by `DllMain`, except for the main thread.
   */
#  if !defined(GC_NO_THREADS_DISCOVERY) && defined(GC_WIN32_THREADS)
  GC_ASSERT(!GC_win32_dll_threads
            || THREAD_ID_EQUAL(GC_main_thread_id, thread_id_self()));
#  endif

  LOCK();
  DISABLE_CANCEL(cancel_state);
  /*
   * Wait for any collection that may be marking from our stack to complete
   * before we remove this thread.
   */
  GC_wait_for_gc_completion(FALSE);
  me = GC_self_thread_inner();
  GC_ASSERT(THREAD_ID_EQUAL(me->id, thread_id_self()));
  GC_unregister_my_thread_inner(me);
  RESTORE_CANCEL(cancel_state);
  UNLOCK();
  return GC_SUCCESS;
}

#  if !defined(GC_NO_PTHREAD_CANCEL) && defined(GC_PTHREADS)
/*
 * We should deal with the fact that apparently on Solaris and, probably,
 * on some Linux we cannot collect while a thread is exiting, since
 * signals are not handled properly.  This currently gives rise to deadlocks.
 * The only workaround seen is to intercept `pthread_cancel()` and
 * `pthread_exit()`, and disable the collections until the thread exit
 * handler is called.  That is ugly, because we risk growing the heap
 * unnecessarily.  But it seems that we do not really have an option in that
 * the process is not in a fully functional state while a thread is exiting.
 */
#    define GC_wrap_pthread_cancel WRAP_FUNC(pthread_cancel)
GC_API int
GC_wrap_pthread_cancel(pthread_t thread)
{
#    ifdef CANCEL_SAFE
  GC_thread t;
#    endif

  INIT_REAL_SYMS();
#    ifdef CANCEL_SAFE
  LOCK();
  t = GC_lookup_by_pthread(thread);
  /*
   * We test `DISABLED_GC` because `pthread_exit` could be called at
   * the same time.  (If `t` is `NULL`, then `pthread_cancel()` should
   * return `ESRCH`.)
   */
  if (t != NULL && (t->flags & DISABLED_GC) == 0) {
    t->flags |= DISABLED_GC;
    GC_dont_gc++;
  }
  UNLOCK();
#    endif
  return REAL_FUNC(pthread_cancel)(thread);
}
#    undef GC_wrap_pthread_cancel
#  endif /* !GC_NO_PTHREAD_CANCEL */

#  ifdef GC_HAVE_PTHREAD_EXIT
#    define GC_wrap_pthread_exit WRAP_FUNC(pthread_exit)
GC_API GC_PTHREAD_EXIT_ATTRIBUTE void
GC_wrap_pthread_exit(void *retval)
{
  GC_thread me;

  INIT_REAL_SYMS();
  LOCK();
  me = GC_self_thread_inner();
  /*
   * We test `DISABLED_GC` because someone else could call `pthread_cancel()`
   * at the same time.
   */
  if (me != NULL && (me->flags & DISABLED_GC) == 0) {
    me->flags |= DISABLED_GC;
    GC_dont_gc++;
  }
  UNLOCK();

  REAL_FUNC(pthread_exit)(retval);
}
#    undef GC_wrap_pthread_exit
#  endif /* GC_HAVE_PTHREAD_EXIT */

GC_API void GC_CALL
GC_allow_register_threads(void)
{
  /*
   * Check the collector is initialized and the current thread is
   * registered.
   */
  GC_ASSERT(GC_self_thread() != NULL);

  /* Initialize symbols while still single-threaded. */
  INIT_REAL_SYMS();

  GC_init_lib_bounds();
  GC_start_mark_threads();
  set_need_to_lock();
}

#  if defined(PTHREAD_STOP_WORLD_IMPL)            \
          && !defined(NO_SIGNALS_UNBLOCK_IN_MAIN) \
      || defined(GC_EXPLICIT_SIGNALS_UNBLOCK)
GC_INNER void
GC_unblock_gc_signals(void)
{
  sigset_t set;

  /* This is for `pthread_sigmask`. */
  INIT_REAL_SYMS();

  sigemptyset(&set);
  sigaddset(&set, GC_get_suspend_signal());
  sigaddset(&set, GC_get_thr_restart_signal());
  if (REAL_FUNC(pthread_sigmask)(SIG_UNBLOCK, &set, NULL) != 0)
    ABORT("pthread_sigmask failed");
}
#  endif /* PTHREAD_STOP_WORLD_IMPL || GC_EXPLICIT_SIGNALS_UNBLOCK */

GC_API int GC_CALL
GC_register_my_thread(const struct GC_stack_base *sb)
{
  GC_thread me;

  if (!GC_need_to_lock)
    ABORT("Threads explicit registering is not previously enabled");

  /* We lock here, since we want to wait for an ongoing GC. */
  LOCK();
  me = GC_self_thread_inner();
  if (LIKELY(NULL == me)) {
    me = GC_register_my_thread_inner(sb, thread_id_self());
#  ifdef GC_PTHREADS
#    ifdef CPPCHECK
    GC_noop1(me->flags);
#    endif
    /*
     * Treat as detached, since we do not need to worry about pointer
     * results.
     */
    me->flags |= DETACHED;
#  else
    (void)me;
#  endif
  } else {
#  ifdef GC_PTHREADS
    if (KNOWN_FINISHED(me)) {
      /*
       * This code is executed when a thread is registered from the destructor
       * of the client thread key.
       */
#    ifdef NACL
      GC_nacl_initialize_gc_thread(me);
#    endif
#    ifdef DARWIN
      /*
       * Reinitialize `mach_thread` to avoid `thread_suspend()` fail
       * with `MACH_SEND_INVALID_DEST` error.
       */
      me->mach_thread = mach_thread_self();
#    endif
      GC_record_stack_base(me->crtn, sb);
      me->flags &= (unsigned char)~FINISHED; /*< but not `DETACHED` */
    } else
#  endif
    /* else */ {
      UNLOCK();
      return GC_DUPLICATE;
    }
  }

#  ifdef THREAD_LOCAL_ALLOC
  GC_init_thread_local(&me->tlfs);
#  endif
#  ifdef GC_EXPLICIT_SIGNALS_UNBLOCK
  /*
   * Since this could be executed from a thread destructor,
   * our signals might already be blocked.
   */
  GC_unblock_gc_signals();
#  endif
#  if defined(GC_ENABLE_SUSPEND_THREAD) && defined(SIGNAL_BASED_STOP_WORLD)
  if (UNLIKELY((me->ext_suspend_cnt & 1) != 0)) {
    GC_with_callee_saves_pushed(GC_suspend_self_blocked, (ptr_t)me);
  }
#  endif
  UNLOCK();
  return GC_SUCCESS;
}

#  if defined(GC_PTHREADS) && !defined(PLATFORM_THREADS) \
      && !defined(SN_TARGET_PSP2)

GC_INNER_PTHRSTART void
GC_thread_exit_proc(void *arg)
{
  GC_thread me = (GC_thread)arg;
  IF_CANCEL(int cancel_state;)

#    ifdef DEBUG_THREADS
  GC_log_printf("Called GC_thread_exit_proc on %p, gc_thread= %p\n",
                THREAD_ID_TO_VPTR(me->id), (void *)me);
#    endif
  LOCK();
  DISABLE_CANCEL(cancel_state);
  GC_wait_for_gc_completion(FALSE);
  GC_unregister_my_thread_inner(me);
  RESTORE_CANCEL(cancel_state);
  UNLOCK();
}

#    define GC_wrap_pthread_join WRAP_FUNC(pthread_join)
GC_API int
GC_wrap_pthread_join(pthread_t thread, void **retval)
{
  int result;
  GC_thread t;

  INIT_REAL_SYMS();
#    ifdef DEBUG_THREADS
  GC_log_printf("thread %p is joining thread %p\n",
                PTHREAD_TO_VPTR(pthread_self()), PTHREAD_TO_VPTR(thread));
#    endif

  /* After the join, thread id may have been recycled. */
  READER_LOCK();
  t = (GC_thread)COVERT_DATAFLOW_P(GC_lookup_by_pthread(thread));
  /*
   * This is guaranteed to be the intended one, since the thread id
   * cannot have been recycled by `pthreads`.
   */
  READER_UNLOCK();

  result = REAL_FUNC(pthread_join)(thread, retval);
#    ifdef FREEBSD
  /*
   * On FreeBSD, the wrapped `pthread_join()` sometimes returns (what
   * appears to be) a spurious `EINTR` which caused the test and real code
   * to fail gratuitously.  Having looked at system `pthreads` library
   * source code, I see how such return code value may be generated:
   * in one path of the code, `pthread_join()` just returns the `errno`
   * setting of the thread being joined - this does not match the POSIX
   * specification or the local man pages.  Thus, I have taken the liberty
   * to catch this one spurious return value.
   */
  if (UNLIKELY(result == EINTR))
    result = 0;
#    endif

  if (LIKELY(0 == result)) {
    LOCK();
    /*
     * Here the `pthread_id` may have been recycled.  Delete the thread from
     *`GC_threads` (unless it has been registered again from the destructor
     * of the client thread key).
     */
    if (KNOWN_FINISHED(t)) {
      GC_delete_thread(t);
    }
    UNLOCK();
  }

#    ifdef DEBUG_THREADS
  GC_log_printf("thread %p join with thread %p %s\n",
                PTHREAD_TO_VPTR(pthread_self()), PTHREAD_TO_VPTR(thread),
                result != 0 ? "failed" : "succeeded");
#    endif
  return result;
}
#    undef GC_wrap_pthread_join

#    define GC_wrap_pthread_detach WRAP_FUNC(pthread_detach)
GC_API int
GC_wrap_pthread_detach(pthread_t thread)
{
  int result;
  GC_thread t;

  INIT_REAL_SYMS();
  READER_LOCK();
  t = (GC_thread)COVERT_DATAFLOW_P(GC_lookup_by_pthread(thread));
  READER_UNLOCK();
  result = REAL_FUNC(pthread_detach)(thread);
  if (LIKELY(0 == result)) {
    LOCK();
    /* Here the `pthread_id` may have been recycled. */
    if (KNOWN_FINISHED(t)) {
      GC_delete_thread(t);
    } else {
      t->flags |= DETACHED;
    }
    UNLOCK();
  }
  return result;
}
#    undef GC_wrap_pthread_detach

struct start_info {
  void *(*start_routine)(void *);
  void *arg;
  /*
   * Note: a value of 1 means the thread is in our thread table, but
   * the parent process has not noticed it yet.
   */
  sem_t registered;
  unsigned char flags;
};

GC_INNER_PTHRSTART GC_thread
GC_start_rtn_prepare_thread(void *(**pstart)(void *), void **pstart_arg,
                            struct GC_stack_base *sb, void *arg)
{
  struct start_info *psi = (struct start_info *)arg;
  thread_id_t self_id = thread_id_self();
  GC_thread me;

#    ifdef DEBUG_THREADS
  GC_log_printf("Starting thread %p, sp= %p\n",
                PTHREAD_TO_VPTR(pthread_self()), (void *)GC_approx_sp());
#    endif
  /*
   * If a collection occurs before the thread is registered, that collection
   * will ignore this thread.  That is fine, since it will block trying to
   * acquire the allocator lock, and will not yet hold interesting pointers.
   */
  LOCK();
  /*
   * We register the thread here instead of in the parent process, so that
   * we do not need to hold the allocator lock during `pthread_create()`
   * call.
   */
  me = GC_register_my_thread_inner(sb, self_id);
  GC_ASSERT(me != &first_thread);
  me->flags = psi->flags;
#    ifdef GC_WIN32_THREADS
  GC_win32_cache_self_pthread(self_id);
#    endif
#    ifdef THREAD_LOCAL_ALLOC
  GC_init_thread_local(&me->tlfs);
#    endif
  UNLOCK();

  *pstart = psi->start_routine;
  *pstart_arg = psi->arg;
#    if defined(DEBUG_THREADS) && defined(FUNCPTR_IS_DATAPTR)
  GC_log_printf("start_routine= %p\n", CAST_THRU_UINTPTR(void *, *pstart));
#    endif
  sem_post(&psi->registered);
  /* This was the last action on `*psi`; OK to deallocate. */
  return me;
}

STATIC void *
GC_pthread_start(void *arg)
{
#    ifdef INCLUDE_LINUX_THREAD_DESCR
  struct GC_stack_base sb;

#      ifdef REDIRECT_MALLOC
  /*
   * `GC_get_stack_base()` may call `pthread_getattr_np()`, which
   * can unfortunately call `realloc()`, which may allocate from
   * an unregistered thread.  This is unpleasant, since it might
   * force heap growth (or, even, heap overflow).
   */
  GC_disable();
#      endif
  if (GC_get_stack_base(&sb) != GC_SUCCESS)
    ABORT("Failed to get thread stack base");
#      ifdef REDIRECT_MALLOC
  GC_enable();
#      endif
  return GC_pthread_start_inner(&sb, arg);
#    else
  return GC_call_with_stack_base(GC_pthread_start_inner, arg);
#    endif
}

#    define GC_wrap_pthread_create WRAP_FUNC(pthread_create)
GC_API int
GC_wrap_pthread_create(pthread_t *new_thread,
                       GC_PTHREAD_CREATE_CONST pthread_attr_t *attr,
                       void *(*start_routine)(void *), void *arg)
{
  int result;
  struct start_info si;

  GC_ASSERT(I_DONT_HOLD_LOCK());
  INIT_REAL_SYMS();
  if (UNLIKELY(!GC_is_initialized))
    GC_init();
  GC_ASSERT(GC_thr_initialized);

  GC_init_lib_bounds();
  if (sem_init(&si.registered, GC_SEM_INIT_PSHARED, 0) == -1)
    ABORT("sem_init failed");
  si.flags = 0;
  si.start_routine = start_routine;
  si.arg = arg;

  /*
   * We resist the temptation to muck with the stack size here, even if the
   * default is unreasonably small.  That is the client's responsibility.
   */
#    ifdef GC_ASSERTIONS
  {
    size_t stack_size = 0;
    if (NULL != attr) {
      if (pthread_attr_getstacksize(attr, &stack_size) != 0)
        ABORT("pthread_attr_getstacksize failed");
    }
    if (0 == stack_size) {
      pthread_attr_t my_attr;

      if (pthread_attr_init(&my_attr) != 0)
        ABORT("pthread_attr_init failed");
      if (pthread_attr_getstacksize(&my_attr, &stack_size) != 0)
        ABORT("pthread_attr_getstacksize failed");
      (void)pthread_attr_destroy(&my_attr);
    }
    /*
     * On Solaris 10 and on Win32 with `winpthreads` library, with the
     * default `attr` initialization, `stack_size` remains 0; fudge it.
     */
    if (UNLIKELY(0 == stack_size)) {
#      if !defined(SOLARIS) && !defined(GC_WIN32_PTHREADS)
      WARN("Failed to get stack size for assertion checking\n", 0);
#      endif
      stack_size = 1000000;
    }
    GC_ASSERT(stack_size >= 65536);
    /*
     * Our threads may need to do some work for the GC.
     * Ridiculously small threads will not work, and they
     * probably would not work anyway.
     */
  }
#    endif

  if (attr != NULL) {
    int detachstate;

    if (pthread_attr_getdetachstate(attr, &detachstate) != 0)
      ABORT("pthread_attr_getdetachstate failed");
    if (PTHREAD_CREATE_DETACHED == detachstate)
      si.flags |= DETACHED;
  }

#    ifdef PARALLEL_MARK
  if (!GC_parallel && UNLIKELY(GC_available_markers_m1 > 0))
    GC_start_mark_threads();
#    endif
#    ifdef DEBUG_THREADS
  GC_log_printf("About to start new thread from thread %p\n",
                PTHREAD_TO_VPTR(pthread_self()));
#    endif
  set_need_to_lock();
  result = REAL_FUNC(pthread_create)(new_thread, attr, GC_pthread_start, &si);

  /*
   * Wait until child has been added to the thread table.
   * This also ensures that we hold onto the stack-allocated `si`
   * until the child is done with it.
   */
  if (LIKELY(0 == result)) {
    IF_CANCEL(int cancel_state;)

    /* `pthread_create()` is not a cancellation point. */
    DISABLE_CANCEL(cancel_state);

    while (sem_wait(&si.registered) == -1) {
#    ifdef HAIKU
      /* To workaround some bug in Haiku semaphores. */
      if (EACCES == errno)
        continue;
#    endif
      if (errno != EINTR)
        ABORT("sem_wait failed");
    }
    RESTORE_CANCEL(cancel_state);
  }
  sem_destroy(&si.registered);
  return result;
}
#    undef GC_wrap_pthread_create

#  endif /* GC_PTHREADS && !PLATFORM_THREADS && !SN_TARGET_PSP2 */

#  if ((defined(GC_PTHREADS_PARAMARK) || defined(USE_PTHREAD_LOCKS)) \
       && !defined(NO_PTHREAD_TRYLOCK))                              \
      || defined(USE_SPIN_LOCK)
/*
 * Spend a few cycles in a way that cannot introduce contention with
 * other threads.
 */
#    define GC_PAUSE_SPIN_CYCLES 10
STATIC void
GC_pause(void)
{
  int i;

  for (i = 0; i < GC_PAUSE_SPIN_CYCLES; ++i) {
    /* Something that is unlikely to be optimized away. */
#    if defined(AO_HAVE_compiler_barrier) && !defined(BASE_ATOMIC_OPS_EMULATED)
    AO_compiler_barrier();
#    else
    GC_noop1(i);
#    endif
  }
}
#  endif /* USE_SPIN_LOCK || !NO_PTHREAD_TRYLOCK */

#  ifndef SPIN_MAX
/* Maximum number of calls to `GC_pause()` before give up. */
#    define SPIN_MAX 128
#  endif

#  if (!defined(USE_SPIN_LOCK) && !defined(NO_PTHREAD_TRYLOCK) \
       && defined(USE_PTHREAD_LOCKS))                          \
      || defined(GC_PTHREADS_PARAMARK)
/*
 * If we do not want to use the below spinlock implementation, either
 * because we do not have a `GC_test_and_set` implementation, or because
 * we do not want to risk sleeping, we can still try spinning
 * on `pthread_mutex_trylock` for a while.  This appears to be very
 * beneficial in many cases.
 * I suspect that under high contention this is nearly always better
 * than the spin lock.  But it is a bit slower on a uniprocessor.
 * Hence we still default to the spin lock.  This is also used to
 * acquire the mark lock for the parallel marker.
 *
 * Here we use a strict exponential back-off scheme.  I do not know
 * whether that is better or worse than the above.  We eventually
 * yield by calling `pthread_mutex_lock()`; it never makes sense to
 * explicitly sleep.
 */

#    ifdef LOCK_STATS
/* Note that `LOCK_STATS` requires `AO_HAVE_test_and_set`. */
volatile AO_t GC_spin_count = 0;
volatile AO_t GC_block_count = 0;
volatile AO_t GC_unlocked_count = 0;
#    endif

STATIC void
GC_generic_lock(pthread_mutex_t *lock)
{
#    ifndef NO_PTHREAD_TRYLOCK
  unsigned pause_length = 1;
  unsigned i;

  if (LIKELY(0 == pthread_mutex_trylock(lock))) {
#      ifdef LOCK_STATS
    (void)AO_fetch_and_add1(&GC_unlocked_count);
#      endif
    return;
  }
  for (; pause_length <= (unsigned)SPIN_MAX; pause_length <<= 1) {
    for (i = 0; i < pause_length; ++i) {
      GC_pause();
    }
    switch (pthread_mutex_trylock(lock)) {
    case 0:
#      ifdef LOCK_STATS
      (void)AO_fetch_and_add1(&GC_spin_count);
#      endif
      return;
    case EBUSY:
      break;
    default:
      ABORT("Unexpected error from pthread_mutex_trylock");
    }
  }
#    endif /* !NO_PTHREAD_TRYLOCK */
#    ifdef LOCK_STATS
  (void)AO_fetch_and_add1(&GC_block_count);
#    endif
  pthread_mutex_lock(lock);
}
#  endif /* !USE_SPIN_LOCK || ... */

#  if defined(GC_PTHREADS) && !defined(GC_WIN32_THREADS)
GC_INNER volatile unsigned char GC_collecting = FALSE;

#    if defined(AO_HAVE_char_load) && !defined(BASE_ATOMIC_OPS_EMULATED)
#      define is_collecting() ((GC_bool)AO_char_load(&GC_collecting))
#    else
/*
 * `GC_collecting` is a hint, a potential data race between `GC_lock()`
 * and `ENTER_GC()`/`EXIT_GC()` is OK to ignore.
 */
#      define is_collecting() ((GC_bool)GC_collecting)
#    endif
#  endif /* GC_PTHREADS && !GC_WIN32_THREADS */

#  ifdef GC_ASSERTIONS
GC_INNER unsigned long GC_lock_holder = NO_THREAD;
#  endif

#  if defined(USE_SPIN_LOCK)
/*
 * Reasonably fast spin locks.  Basically the same implementation as
 * in STL `alloc.h` file.  This is not really the right way to do this
 * but until the POSIX scheduling mess gets straightened out...
 */

/* Spin cycles if we suspect we are running on an uniprocessor. */
#    define low_spin_max 30

/* Spin cycles for a multiprocessor. */
#    define high_spin_max SPIN_MAX

static volatile AO_t spin_max = low_spin_max;

/*
 * A potential data race between threads invoking `GC_lock` which reads
 * and updates `spin_max` and `last_spins` could be ignored because these
 * variables are hints only.
 */
static volatile AO_t last_spins = 0;

GC_INNER void
GC_lock(void)
{
  AO_t my_spin_max, my_last_spins_half;
  size_t i;

  if (LIKELY(AO_test_and_set_acquire(&GC_allocate_lock) == AO_TS_CLEAR))
    return;

  my_spin_max = AO_load(&spin_max);
  my_last_spins_half = AO_load(&last_spins) / 2;
  for (i = 0; i < my_spin_max; i++) {
    if (is_collecting() || GC_nprocs == 1)
      goto yield;
    if (i < my_last_spins_half) {
      GC_pause();
      continue;
    }
    if (AO_test_and_set_acquire(&GC_allocate_lock) == AO_TS_CLEAR) {
      /*
       * Got it, spinning worked!  Thus we are probably not being
       * scheduled against the other process with which we were
       * contending.  Thus it makes sense to spin longer the next time.
       */
      AO_store(&last_spins, i);
      AO_store(&spin_max, high_spin_max);
      return;
    }
  }
  /* We are probably being scheduled against the other process.  Sleep. */
  AO_store(&spin_max, low_spin_max);
yield:
  for (i = 0;; ++i) {
    if (AO_test_and_set_acquire(&GC_allocate_lock) == AO_TS_CLEAR) {
      return;
    }

    /*
     * Under Linux, very short sleeps tend to wait until the current time
     * quantum expires.  On old Linux kernels, `nanosleep()` (<= 2 ms) just
     * spins.  (Under Linux 2.4, this happens only for real-time processes.)
     * We want to minimize both behaviors here.
     */
#    define SLEEP_THRESHOLD 12

    if (i < SLEEP_THRESHOLD) {
      sched_yield();
    } else {
      struct timespec ts;

      /*
       * Do not wait for more than about 15 ms, even under extreme
       * contention.
       */
      if (i > 24)
        i = 24;

      ts.tv_sec = 0;
      ts.tv_nsec = (unsigned32)1 << i;
      nanosleep(&ts, 0);
    }
  }
}

#  elif defined(USE_PTHREAD_LOCKS)
#    ifdef USE_RWLOCK
GC_INNER pthread_rwlock_t GC_allocate_ml = PTHREAD_RWLOCK_INITIALIZER;
#    else
GC_INNER pthread_mutex_t GC_allocate_ml = PTHREAD_MUTEX_INITIALIZER;
#    endif

#    ifndef NO_PTHREAD_TRYLOCK
GC_INNER void
GC_lock(void)
{
  if (1 == GC_nprocs || is_collecting()) {
    pthread_mutex_lock(&GC_allocate_ml);
  } else {
    GC_generic_lock(&GC_allocate_ml);
  }
}
#    elif defined(GC_ASSERTIONS)
GC_INNER void
GC_lock(void)
{
#      ifdef USE_RWLOCK
  (void)pthread_rwlock_wrlock(&GC_allocate_ml); /*< exclusive */
#      else
  pthread_mutex_lock(&GC_allocate_ml);
#      endif
}
#    endif /* NO_PTHREAD_TRYLOCK && GC_ASSERTIONS */

#  endif /* !USE_SPIN_LOCK && USE_PTHREAD_LOCKS */

#  ifdef GC_PTHREADS_PARAMARK

#    if defined(GC_ASSERTIONS) && defined(GC_WIN32_THREADS) \
        && !defined(USE_PTHREAD_LOCKS)
/* Note: result is not guaranteed to be unique. */
#      define NUMERIC_THREAD_ID(id) ((unsigned long)ADDR(PTHREAD_TO_VPTR(id)))
#    endif

#    ifdef GC_ASSERTIONS
#      define SET_MARK_LOCK_HOLDER \
        (void)(GC_mark_lock_holder = NUMERIC_THREAD_ID(pthread_self()))
#      define UNSET_MARK_LOCK_HOLDER                       \
        do {                                               \
          GC_ASSERT(GC_mark_lock_holder                    \
                    == NUMERIC_THREAD_ID(pthread_self())); \
          GC_mark_lock_holder = NO_THREAD;                 \
        } while (0)
#    else
#      define SET_MARK_LOCK_HOLDER (void)0
#      define UNSET_MARK_LOCK_HOLDER (void)0
#    endif /* !GC_ASSERTIONS */

static pthread_cond_t builder_cv = PTHREAD_COND_INITIALIZER;

#    ifndef GC_WIN32_THREADS
static void
setup_mark_lock(void)
{
#      ifdef GLIBC_2_19_TSX_BUG
  pthread_mutexattr_t mattr;
  int glibc_minor = -1;
  int glibc_major = GC_parse_version(&glibc_minor, gnu_get_libc_version());

  if (glibc_major > 2 || (glibc_major == 2 && glibc_minor >= 19)) {
    /* TODO: Disable this workaround for `glibc` with fixed TSX. */
    /* This disables lock elision to workaround a bug in `glibc` 2.19+. */
    if (pthread_mutexattr_init(&mattr) != 0)
      ABORT("pthread_mutexattr_init failed");
    if (pthread_mutexattr_settype(&mattr, PTHREAD_MUTEX_NORMAL) != 0)
      ABORT("pthread_mutexattr_settype failed");
    if (pthread_mutex_init(&mark_mutex, &mattr) != 0)
      ABORT("pthread_mutex_init failed");
    (void)pthread_mutexattr_destroy(&mattr);
  }
#      endif
}
#    endif /* !GC_WIN32_THREADS */

GC_INNER void
GC_acquire_mark_lock(void)
{
#    if defined(NUMERIC_THREAD_ID_UNIQUE) && !defined(THREAD_SANITIZER)
  GC_ASSERT(GC_mark_lock_holder != NUMERIC_THREAD_ID(pthread_self()));
#    endif
  GC_generic_lock(&mark_mutex);
  SET_MARK_LOCK_HOLDER;
}

GC_INNER void
GC_release_mark_lock(void)
{
  UNSET_MARK_LOCK_HOLDER;
  if (pthread_mutex_unlock(&mark_mutex) != 0)
    ABORT("pthread_mutex_unlock failed");
}

/*
 * Collector must wait for free-list builders for 2 reasons:
 *   - Mark bits may still be getting examined without lock;
 *   - Partial free lists referenced only by locals may not be scanned
 *     correctly, e.g. if they contain "pointer-free" objects, since the
 *     free-list link may be ignored.
 */
STATIC void
GC_wait_builder(void)
{
  ASSERT_CANCEL_DISABLED();
  UNSET_MARK_LOCK_HOLDER;
  if (pthread_cond_wait(&builder_cv, &mark_mutex) != 0)
    ABORT("pthread_cond_wait failed");
  GC_ASSERT(GC_mark_lock_holder == NO_THREAD);
  SET_MARK_LOCK_HOLDER;
}

GC_INNER void
GC_wait_for_reclaim(void)
{
  GC_acquire_mark_lock();
  while (GC_fl_builder_count > 0) {
    GC_wait_builder();
  }
  GC_release_mark_lock();
}

#    if defined(CAN_HANDLE_FORK) && defined(THREAD_SANITIZER)
/*
 * Identical to `GC_wait_for_reclaim()` but with the `no_sanitize`
 * attribute as a workaround for TSan which does not notice that the
 * allocator lock is acquired in `fork_prepare_proc()`.
 */
GC_ATTR_NO_SANITIZE_THREAD
static void
wait_for_reclaim_atfork(void)
{
  GC_acquire_mark_lock();
  while (GC_fl_builder_count > 0)
    GC_wait_builder();
  GC_release_mark_lock();
}
#    endif /* CAN_HANDLE_FORK && THREAD_SANITIZER */

GC_INNER void
GC_notify_all_builder(void)
{
  GC_ASSERT(GC_mark_lock_holder == NUMERIC_THREAD_ID(pthread_self()));
  if (pthread_cond_broadcast(&builder_cv) != 0)
    ABORT("pthread_cond_broadcast failed");
}

GC_INNER void
GC_wait_marker(void)
{
  ASSERT_CANCEL_DISABLED();
  GC_ASSERT(GC_parallel);
  UNSET_MARK_LOCK_HOLDER;
  if (pthread_cond_wait(&mark_cv, &mark_mutex) != 0)
    ABORT("pthread_cond_wait failed");
  GC_ASSERT(GC_mark_lock_holder == NO_THREAD);
  SET_MARK_LOCK_HOLDER;
}

GC_INNER void
GC_notify_all_marker(void)
{
  GC_ASSERT(GC_parallel);
  if (pthread_cond_broadcast(&mark_cv) != 0)
    ABORT("pthread_cond_broadcast failed");
}

#  endif /* GC_PTHREADS_PARAMARK */

GC_INNER GC_on_thread_event_proc GC_on_thread_event = 0;

GC_API void GC_CALL
GC_set_on_thread_event(GC_on_thread_event_proc fn)
{
  /* Note: `fn` may be 0 (means no event notifier). */
  LOCK();
  GC_on_thread_event = fn;
  UNLOCK();
}

GC_API GC_on_thread_event_proc GC_CALL
GC_get_on_thread_event(void)
{
  GC_on_thread_event_proc fn;

  READER_LOCK();
  fn = GC_on_thread_event;
  READER_UNLOCK();
  return fn;
}

#  ifdef STACKPTR_CORRECTOR_AVAILABLE
GC_INNER GC_sp_corrector_proc GC_sp_corrector = 0;
#  endif

GC_API void GC_CALL
GC_set_sp_corrector(GC_sp_corrector_proc fn)
{
#  ifdef STACKPTR_CORRECTOR_AVAILABLE
  LOCK();
  GC_sp_corrector = fn;
  UNLOCK();
#  else
  UNUSED_ARG(fn);
#  endif
}

GC_API GC_sp_corrector_proc GC_CALL
GC_get_sp_corrector(void)
{
#  ifdef STACKPTR_CORRECTOR_AVAILABLE
  GC_sp_corrector_proc fn;

  READER_LOCK();
  fn = GC_sp_corrector;
  READER_UNLOCK();
  return fn;
#  else
  return 0; /*< unsupported */
#  endif
}

#  ifdef PTHREAD_REGISTER_CANCEL_WEAK_STUBS
/* Workaround "undefined reference" linkage errors on some targets. */
EXTERN_C_BEGIN
extern void __pthread_register_cancel(void) __attribute__((__weak__));
extern void __pthread_unregister_cancel(void) __attribute__((__weak__));
EXTERN_C_END

void
__pthread_register_cancel(void)
{
}
void
__pthread_unregister_cancel(void)
{
}
#  endif

#  undef do_blocking_enter

#endif /* THREADS */
