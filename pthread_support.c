/*
 * Copyright (c) 1994 by Xerox Corporation.  All rights reserved.
 * Copyright (c) 1996 by Silicon Graphics.  All rights reserved.
 * Copyright (c) 1998 by Fergus Henderson.  All rights reserved.
 * Copyright (c) 2000-2005 by Hewlett-Packard Company.  All rights reserved.
 * Copyright (c) 2008-2025 Ivan Maidanski
 *
 * THIS MATERIAL IS PROVIDED AS IS, WITH ABSOLUTELY NO WARRANTY EXPRESSED
 * OR IMPLIED.  ANY USE IS AT YOUR OWN RISK.
 *
 * Permission is hereby granted to use or copy this program
 * for any purpose,  provided the above notices are retained on all copies.
 * Permission to modify the code and to distribute modified code is granted,
 * provided the above notices are retained, and a notice that the code was
 * modified is included with the above copyright notice.
 */

#include "private/pthread_support.h"

/*
 * Support code originally for LinuxThreads, the clone()-based kernel
 * thread package for Linux which is included in libc6.
 *
 * This code no doubt makes some assumptions beyond what is
 * guaranteed by the pthread standard, though it now does
 * very little of that.  It now also supports NPTL, and many
 * other Posix thread implementations.  We are trying to merge
 * all flavors of pthread support code into this file.
 */

#if defined(GC_PTHREADS) && !defined(GC_WIN32_THREADS)

# include <stdlib.h>
# include <pthread.h>
# include <sched.h>
# include <time.h>
# include <errno.h>
# include <unistd.h>
# if !defined(SN_TARGET_ORBIS) && !defined(SN_TARGET_PSP2)
#   if !defined(GC_RTEMS_PTHREADS)
#     include <sys/mman.h>
#   endif
#   include <sys/time.h>
#   include <sys/types.h>
#   include <sys/stat.h>
#   include <fcntl.h>
# endif
# include <signal.h>

# include "gc_inline.h"

#if defined(GC_DARWIN_THREADS)
# include "private/darwin_semaphore.h"
#else
# include <semaphore.h>
#endif /* !GC_DARWIN_THREADS */

#if defined(GC_DARWIN_THREADS) || defined(GC_FREEBSD_THREADS)
# include <sys/sysctl.h>
#endif /* GC_DARWIN_THREADS */

#if defined(GC_NETBSD_THREADS) || defined(GC_OPENBSD_THREADS)
# include <sys/param.h>
# include <sys/sysctl.h>
#endif /* GC_NETBSD_THREADS */

/* Allocator lock definitions.          */
#if !defined(USE_SPIN_LOCK)
  GC_INNER pthread_mutex_t GC_allocate_ml = PTHREAD_MUTEX_INITIALIZER;
#endif

#ifdef GC_ASSERTIONS
  GC_INNER unsigned long GC_lock_holder = NO_THREAD;
                /* Used only for assertions.    */
#endif

#if defined(GC_DGUX386_THREADS)
# include <sys/dg_sys_info.h>
# include <sys/_int_psem.h>
  /* sem_t is an uint in DG/UX */
  typedef unsigned int sem_t;
#endif /* GC_DGUX386_THREADS */

/* Undefine macros used to redirect pthread primitives. */
# undef pthread_create
# ifndef GC_NO_PTHREAD_SIGMASK
#   undef pthread_sigmask
# endif
# ifndef GC_NO_PTHREAD_CANCEL
#   undef pthread_cancel
# endif
# ifdef GC_HAVE_PTHREAD_EXIT
#   undef pthread_exit
# endif
# undef pthread_join
# undef pthread_detach
# if defined(GC_OSF1_THREADS) && defined(_PTHREAD_USE_MANGLED_NAMES_) \
     && !defined(_PTHREAD_USE_PTDNAM_)
  /* Restore the original mangled names on Tru64 UNIX.  */
#   define pthread_create __pthread_create
#   define pthread_join __pthread_join
#   define pthread_detach __pthread_detach
#   ifndef GC_NO_PTHREAD_CANCEL
#     define pthread_cancel __pthread_cancel
#   endif
#   ifdef GC_HAVE_PTHREAD_EXIT
#     define pthread_exit __pthread_exit
#   endif
# endif

#ifdef GC_USE_LD_WRAP
#   define WRAP_FUNC(f) __wrap_##f
#   define REAL_FUNC(f) __real_##f
    int REAL_FUNC(pthread_create)(pthread_t *,
                                  GC_PTHREAD_CREATE_CONST pthread_attr_t *,
                                  void *(*start_routine)(void *), void *);
    int REAL_FUNC(pthread_join)(pthread_t, void **);
    int REAL_FUNC(pthread_detach)(pthread_t);
#   ifndef GC_NO_PTHREAD_SIGMASK
      int REAL_FUNC(pthread_sigmask)(int, const sigset_t *, sigset_t *);
#   endif
#   ifndef GC_NO_PTHREAD_CANCEL
      int REAL_FUNC(pthread_cancel)(pthread_t);
#   endif
#   ifdef GC_HAVE_PTHREAD_EXIT
      void REAL_FUNC(pthread_exit)(void *) GC_PTHREAD_EXIT_ATTRIBUTE;
#   endif
#else
#   ifdef GC_USE_DLOPEN_WRAP
#     include <dlfcn.h>
#     define WRAP_FUNC(f) f
#     define REAL_FUNC(f) GC_real_##f
      /* We define both GC_f and plain f to be the wrapped function.    */
      /* In that way plain calls work, as do calls from files that      */
      /* included gc.h, which redefined f to GC_f.                      */
      /* FIXME: Needs work for DARWIN and True64 (OSF1) */
      typedef int (* GC_pthread_create_t)(pthread_t *,
                                    GC_PTHREAD_CREATE_CONST pthread_attr_t *,
                                    void * (*)(void *), void *);
      static GC_pthread_create_t REAL_FUNC(pthread_create);
#     ifndef GC_NO_PTHREAD_SIGMASK
        typedef int (* GC_pthread_sigmask_t)(int, const sigset_t *,
                                             sigset_t *);
        static GC_pthread_sigmask_t REAL_FUNC(pthread_sigmask);
#     endif
      typedef int (* GC_pthread_join_t)(pthread_t, void **);
      static GC_pthread_join_t REAL_FUNC(pthread_join);
      typedef int (* GC_pthread_detach_t)(pthread_t);
      static GC_pthread_detach_t REAL_FUNC(pthread_detach);
#     ifndef GC_NO_PTHREAD_CANCEL
        typedef int (* GC_pthread_cancel_t)(pthread_t);
        static GC_pthread_cancel_t REAL_FUNC(pthread_cancel);
#     endif
#     ifdef GC_HAVE_PTHREAD_EXIT
        typedef void (* GC_pthread_exit_t)(void *) GC_PTHREAD_EXIT_ATTRIBUTE;
        static GC_pthread_exit_t REAL_FUNC(pthread_exit);
#     endif
#   else
#     define WRAP_FUNC(f) GC_##f
#     if !defined(GC_DGUX386_THREADS)
#       define REAL_FUNC(f) f
#     else /* GC_DGUX386_THREADS */
#       define REAL_FUNC(f) __d10_##f
#     endif /* GC_DGUX386_THREADS */
#   endif
#endif

#if defined(GC_USE_LD_WRAP) || defined(GC_USE_DLOPEN_WRAP)
  /* Define GC_ functions as aliases for the plain ones, which will     */
  /* be intercepted.  This allows files which include gc.h, and hence   */
  /* generate references to the GC_ symbols, to see the right symbols.  */
  GC_API int GC_pthread_create(pthread_t * t,
                               GC_PTHREAD_CREATE_CONST pthread_attr_t *a,
                               void * (* fn)(void *), void * arg)
  {
    return pthread_create(t, a, fn, arg);
  }

# ifndef GC_NO_PTHREAD_SIGMASK
    GC_API int GC_pthread_sigmask(int how, const sigset_t *mask,
                                  sigset_t *old)
    {
      return pthread_sigmask(how, mask, old);
    }
# endif /* !GC_NO_PTHREAD_SIGMASK */

  GC_API int GC_pthread_join(pthread_t t, void **res)
  {
    return pthread_join(t, res);
  }

  GC_API int GC_pthread_detach(pthread_t t)
  {
    return pthread_detach(t);
  }

# ifndef GC_NO_PTHREAD_CANCEL
    GC_API int GC_pthread_cancel(pthread_t t)
    {
      return pthread_cancel(t);
    }
# endif /* !GC_NO_PTHREAD_CANCEL */

# ifdef GC_HAVE_PTHREAD_EXIT
    GC_API GC_PTHREAD_EXIT_ATTRIBUTE void GC_pthread_exit(void *retval)
    {
      pthread_exit(retval);
    }
# endif
#endif /* Linker-based interception. */

#ifdef GC_USE_DLOPEN_WRAP
  STATIC GC_bool GC_syms_initialized = FALSE;

  STATIC void GC_init_real_syms(void)
  {
    void *dl_handle;

    if (GC_syms_initialized) return;
#   ifdef RTLD_NEXT
      dl_handle = RTLD_NEXT;
#   else
      dl_handle = dlopen("libpthread.so.0", RTLD_LAZY);
      if (NULL == dl_handle) {
        dl_handle = dlopen("libpthread.so", RTLD_LAZY); /* without ".0" */
      }
      if (NULL == dl_handle) ABORT("Couldn't open libpthread");
#   endif
    REAL_FUNC(pthread_create) = (GC_pthread_create_t)(word)
                                dlsym(dl_handle, "pthread_create");
#   ifdef RTLD_NEXT
      if (REAL_FUNC(pthread_create) == 0)
        ABORT("pthread_create not found"
              " (probably -lgc is specified after -lpthread)");
#   endif
#   ifndef GC_NO_PTHREAD_SIGMASK
      REAL_FUNC(pthread_sigmask) = (GC_pthread_sigmask_t)(word)
                                dlsym(dl_handle, "pthread_sigmask");
#   endif
    REAL_FUNC(pthread_join) = (GC_pthread_join_t)(word)
                                dlsym(dl_handle, "pthread_join");
    REAL_FUNC(pthread_detach) = (GC_pthread_detach_t)(word)
                                  dlsym(dl_handle, "pthread_detach");
#   ifndef GC_NO_PTHREAD_CANCEL
      REAL_FUNC(pthread_cancel) = (GC_pthread_cancel_t)(word)
                                    dlsym(dl_handle, "pthread_cancel");
#   endif
#   ifdef GC_HAVE_PTHREAD_EXIT
      REAL_FUNC(pthread_exit) = (GC_pthread_exit_t)(word)
                                  dlsym(dl_handle, "pthread_exit");
#   endif
    GC_syms_initialized = TRUE;
  }

# define INIT_REAL_SYMS() if (EXPECT(GC_syms_initialized, TRUE)) {} \
                            else GC_init_real_syms()
#else
# define INIT_REAL_SYMS() (void)0
#endif

static GC_bool parallel_initialized = FALSE;

#if defined(MPROTECT_VDB) && defined(DARWIN)
  GC_INNER int GC_inner_pthread_create(pthread_t *t,
                                GC_PTHREAD_CREATE_CONST pthread_attr_t *a,
                                void *(*fn)(void *), void *arg)
  {
    INIT_REAL_SYMS();
    return REAL_FUNC(pthread_create)(t, a, fn, arg);
  }
#endif

#ifndef GC_ALWAYS_MULTITHREADED
  GC_INNER GC_bool GC_need_to_lock = FALSE;
#endif

STATIC int GC_nprocs = 1;
                        /* Number of processors.  We may not have       */
                        /* access to all of them, but this is as good   */
                        /* a guess as any ...                           */

#ifdef THREAD_LOCAL_ALLOC
  /* We must explicitly mark ptrfree and gcj free lists, since the free */
  /* list links wouldn't otherwise be found.  We also set them in the   */
  /* normal free lists, since that involves touching less memory than   */
  /* if we scanned them normally.                                       */
  GC_INNER void GC_mark_thread_local_free_lists(void)
  {
    int i;
    GC_thread p;

    for (i = 0; i < THREAD_TABLE_SZ; ++i) {
      for (p = GC_threads[i]; 0 != p; p = p -> next) {
        if (!(p -> flags & FINISHED))
          GC_mark_thread_local_fls_for(&(p->tlfs));
      }
    }
  }

# if defined(GC_ASSERTIONS)
    /* Check that all thread-local free-lists are completely marked.    */
    /* Also check that thread-specific-data structures are marked.      */
    void GC_check_tls(void)
    {
        int i;
        GC_thread p;

        for (i = 0; i < THREAD_TABLE_SZ; ++i) {
          for (p = GC_threads[i]; 0 != p; p = p -> next) {
            if (!(p -> flags & FINISHED))
              GC_check_tls_for(&(p->tlfs));
          }
        }
#       if defined(USE_CUSTOM_SPECIFIC)
          if (GC_thread_key != 0)
            GC_check_tsd_marks(GC_thread_key);
#       endif
    }
# endif /* GC_ASSERTIONS */

#endif /* THREAD_LOCAL_ALLOC */

# ifndef MAX_MARKERS
#   define MAX_MARKERS 16
# endif

#ifdef PARALLEL_MARK

static ptr_t marker_sp[MAX_MARKERS - 1] = {0};
#ifdef IA64
  static ptr_t marker_bsp[MAX_MARKERS - 1] = {0};
#endif

#if defined(GC_DARWIN_THREADS) && !defined(GC_NO_THREADS_DISCOVERY)
  static mach_port_t marker_mach_threads[MAX_MARKERS - 1] = {0};

  /* Used only by GC_suspend_thread_list().     */
  GC_INNER GC_bool GC_is_mach_marker(thread_act_t thread)
  {
    int i;
    for (i = 0; i < GC_markers_m1; i++) {
      if (marker_mach_threads[i] == thread)
        return TRUE;
    }
    return FALSE;
  }
#endif /* GC_DARWIN_THREADS */

#ifdef HAVE_PTHREAD_SETNAME_NP_WITH_TID_AND_ARG /* NetBSD */
  static void set_marker_thread_name(unsigned id)
  {
    int err = pthread_setname_np(pthread_self(), "GC-marker-%zu",
                                 (void*)(size_t)id);
    if (err != 0)
      WARN("pthread_setname_np failed, errno= %" WARN_PRIdPTR "\n",
           (signed_word)err);
  }
#elif defined(HAVE_PTHREAD_SETNAME_NP_WITH_TID) \
      || defined(HAVE_PTHREAD_SETNAME_NP_WITHOUT_TID)
  static void set_marker_thread_name(unsigned id)
  {
    char name_buf[16];  /* pthread_setname_np may fail for longer names */
    int len = sizeof("GC-marker-") - 1;

    /* Compose the name manually as snprintf may be unavailable or      */
    /* "%u directive output may be truncated" warning may occur.        */
    BCOPY("GC-marker-", name_buf, len);
    if (id >= 10)
      name_buf[len++] = (char)('0' + (id / 10) % 10);
    name_buf[len] = (char)('0' + id % 10);
    name_buf[len + 1] = '\0';

#   ifdef HAVE_PTHREAD_SETNAME_NP_WITHOUT_TID /* iOS, OS X */
      (void)pthread_setname_np(name_buf);
#   else /* Linux, Solaris, etc. */
      if (pthread_setname_np(pthread_self(), name_buf) != 0)
        WARN("pthread_setname_np failed\n", 0);
#   endif
  }
#else
# define set_marker_thread_name(id) (void)(id)
#endif

STATIC void * GC_mark_thread(void * id)
{
  word my_mark_no = 0;
  IF_CANCEL(int cancel_state;)

  if ((word)id == GC_WORD_MAX) return 0; /* to prevent a compiler warning */
  DISABLE_CANCEL(cancel_state);
                         /* Mark threads are not cancellable; they      */
                         /* should be invisible to client.              */
  set_marker_thread_name((unsigned)(word)id);
  marker_sp[(word)id] = GC_approx_sp();
# ifdef IA64
    marker_bsp[(word)id] = GC_save_regs_in_stack();
# endif
# if defined(GC_DARWIN_THREADS) && !defined(GC_NO_THREADS_DISCOVERY)
    marker_mach_threads[(word)id] = mach_thread_self();
# endif

  /* Inform GC_start_mark_threads about completion of marker data init. */
  GC_acquire_mark_lock();
  if (0 == --GC_fl_builder_count) /* count may have a negative value */
    GC_notify_all_builder();

  for (;; ++my_mark_no) {
    /* GC_mark_no is passed only to allow GC_help_marker to terminate   */
    /* promptly.  This is important if it were called from the signal   */
    /* handler or from the GC lock acquisition code.  Under Linux, it's */
    /* not safe to call it from a signal handler, since it uses mutexes */
    /* and condition variables.  Since it is called only here, the      */
    /* argument is unnecessary.                                         */
    if (my_mark_no < GC_mark_no || my_mark_no > GC_mark_no + 2) {
        /* resynchronize if we get far off, e.g. because GC_mark_no     */
        /* wrapped.                                                     */
        my_mark_no = GC_mark_no;
    }
#   ifdef DEBUG_THREADS
      GC_log_printf("Starting mark helper for mark number %lu\n",
                    (unsigned long)my_mark_no);
#   endif
    GC_help_marker(my_mark_no);
  }
}

STATIC pthread_t GC_mark_threads[MAX_MARKERS];

#ifdef GLIBC_2_1_MUTEX_HACK
  /* Ugly workaround for a linux threads bug in the final versions      */
  /* of glibc2.1.  Pthread_mutex_trylock sets the mutex owner           */
  /* field even when it fails to acquire the mutex.  This causes        */
  /* pthread_cond_wait to die.  Remove for glibc2.2.                    */
  /* According to the man page, we should use                           */
  /* PTHREAD_ERRORCHECK_MUTEX_INITIALIZER_NP, but that isn't actually   */
  /* defined.                                                           */
  static pthread_mutex_t mark_mutex =
        {0, 0, 0, PTHREAD_MUTEX_ERRORCHECK_NP, {0, 0}};
#else
  static pthread_mutex_t mark_mutex = PTHREAD_MUTEX_INITIALIZER;
#endif

static int available_markers_m1 = 0;

#ifdef CAN_HANDLE_FORK
  static pthread_cond_t mark_cv;
                        /* initialized by GC_start_mark_threads_inner   */
#else
  static pthread_cond_t mark_cv = PTHREAD_COND_INITIALIZER;
#endif

STATIC void GC_wait_for_gc_completion(GC_bool wait_for_all);

GC_INNER void GC_start_mark_threads_inner(void)
{
    int i;
    pthread_attr_t attr;
#   ifndef NO_MARKER_SPECIAL_SIGMASK
      sigset_t set, oldset;
#   endif

    GC_ASSERT(I_HOLD_LOCK());
    ASSERT_CANCEL_DISABLED();
    if (available_markers_m1 <= 0 || GC_parallel) return;
                /* Skip if parallel markers disabled or already started. */
    GC_wait_for_gc_completion(TRUE);

#   ifdef CAN_HANDLE_FORK
      /* Initialize mark_cv (for the first time), or cleanup its value  */
      /* after forking in the child process.  All the marker threads in */
      /* the parent process were blocked on this variable at fork, so   */
      /* pthread_cond_wait() malfunction (hang) is possible in the      */
      /* child process without such a cleanup.                          */
      /* TODO: This is not portable, it is better to shortly unblock    */
      /* all marker threads in the parent process at fork.              */
      {
        pthread_cond_t mark_cv_local = PTHREAD_COND_INITIALIZER;
        BCOPY(&mark_cv_local, &mark_cv, sizeof(mark_cv));
      }
#   endif

    GC_ASSERT(GC_fl_builder_count == 0);
    INIT_REAL_SYMS(); /* for pthread_create */
    if (0 != pthread_attr_init(&attr)) ABORT("pthread_attr_init failed");
    if (0 != pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED))
        ABORT("pthread_attr_setdetachstate failed");

#   ifdef DEFAULT_STACK_MAYBE_SMALL
      /* Default stack size is usually too small: increase it.  */
      /* Otherwise marker threads or GC may run out of space.   */
      {
        size_t old_size;

        if (pthread_attr_getstacksize(&attr, &old_size) != 0)
          ABORT("pthread_attr_getstacksize failed");
        if (old_size < MIN_STACK_SIZE
            && old_size != 0 /* stack size is known */) {
          if (pthread_attr_setstacksize(&attr, MIN_STACK_SIZE) != 0)
            ABORT("pthread_attr_setstacksize failed");
        }
      }
#   endif /* DEFAULT_STACK_MAYBE_SMALL */

#   ifndef NO_MARKER_SPECIAL_SIGMASK
      /* Apply special signal mask to GC marker threads, and don't drop */
      /* user defined signals by GC marker threads.                     */
      if (sigfillset(&set) != 0)
        ABORT("sigfillset failed");

#     if !defined(GC_DARWIN_THREADS) && !defined(GC_OPENBSD_UTHREADS) \
         && !defined(NACL)
        /* These are used by GC to stop and restart the world.  */
        if (sigdelset(&set, GC_get_suspend_signal()) != 0
            || sigdelset(&set, GC_get_thr_restart_signal()) != 0)
          ABORT("sigdelset failed");
#     endif

      if (REAL_FUNC(pthread_sigmask)(SIG_BLOCK, &set, &oldset) != 0) {
        WARN("pthread_sigmask set failed, no markers started\n", 0);
        GC_markers_m1 = 0;
        (void)pthread_attr_destroy(&attr);
        return;
      }
#   endif /* !NO_MARKER_SPECIAL_SIGMASK */

    /* To have proper GC_parallel value in GC_help_marker.      */
    GC_markers_m1 = available_markers_m1;

    for (i = 0; i < available_markers_m1; ++i) {
      if (0 != REAL_FUNC(pthread_create)(GC_mark_threads + i, &attr,
                              GC_mark_thread, (void *)(word)i)) {
        WARN("Marker thread creation failed\n", 0);
        /* Don't try to create other marker threads.    */
        GC_markers_m1 = i;
        break;
      }
    }

#   ifndef NO_MARKER_SPECIAL_SIGMASK
      /* Restore previous signal mask.  */
      if (REAL_FUNC(pthread_sigmask)(SIG_SETMASK, &oldset, NULL) != 0) {
        WARN("pthread_sigmask restore failed\n", 0);
      }
#   endif

    (void)pthread_attr_destroy(&attr);
    GC_wait_for_markers_init();
    GC_COND_LOG_PRINTF("Started %d mark helper threads\n", GC_markers_m1);
}

#endif /* PARALLEL_MARK */

GC_INNER GC_bool GC_thr_initialized = FALSE;

GC_INNER volatile GC_thread GC_threads[THREAD_TABLE_SZ] = {0};

/* It may not be safe to allocate when we register the first thread.    */
/* As "next" and "status" fields are unused, no need to push this       */
/* (but "backing_store_end" field should be pushed on E2K).             */
static struct GC_Thread_Rep first_thread;

void GC_push_thread_structures(void)
{
    GC_ASSERT(I_HOLD_LOCK());
    GC_push_all((/* no volatile */ void *)&GC_threads,
                (ptr_t)(&GC_threads) + sizeof(GC_threads));
#   ifdef E2K
      GC_PUSH_ALL_SYM(first_thread.backing_store_end);
#   endif
#   if defined(THREAD_LOCAL_ALLOC) && defined(USE_CUSTOM_SPECIFIC)
      GC_PUSH_ALL_SYM(GC_thread_key);
#   endif
}

#ifdef DEBUG_THREADS
  STATIC int GC_count_threads(void)
  {
    int i;
    int count = 0;
    GC_ASSERT(I_HOLD_LOCK());
    for (i = 0; i < THREAD_TABLE_SZ; ++i) {
        GC_thread th = GC_threads[i];
        while (th) {
            if (!(th->flags & FINISHED))
                ++count;
            th = th->next;
        }
    }
    return count;
  }
#endif /* DEBUG_THREADS */

/* Add a thread to GC_threads.  We assume it wasn't already there.      */
/* Caller holds allocation lock.                                        */
STATIC GC_thread GC_new_thread(pthread_t id)
{
    int hv = THREAD_TABLE_INDEX(id);
    GC_thread result;
    static GC_bool first_thread_used = FALSE;

#   ifdef DEBUG_THREADS
        GC_log_printf("Creating thread %p\n", (void *)(word)id);
        for (result = GC_threads[hv]; result != NULL; result = result->next)
          if (!THREAD_EQUAL(result->id, id)) {
            GC_log_printf("Hash collision at GC_threads[%d]\n", hv);
            break;
          }
#   endif
    GC_ASSERT(I_HOLD_LOCK());
    if (!EXPECT(first_thread_used, TRUE)) {
        result = &first_thread;
        first_thread_used = TRUE;
        GC_ASSERT(NULL == GC_threads[hv]);
#       if defined(THREAD_SANITIZER) && defined(CPPCHECK)
          GC_noop1((unsigned char)result->dummy[0]);
#       endif
    } else {
        result = (struct GC_Thread_Rep *)
                 GC_INTERNAL_MALLOC(sizeof(struct GC_Thread_Rep), NORMAL);
        if (result == 0) return(0);
    }
    result -> id = id;
#   ifdef USE_TKILL_ON_ANDROID
      result -> kernel_id = gettid();
#   endif
    result -> next = GC_threads[hv];
    GC_threads[hv] = result;
#   ifdef NACL
      GC_nacl_gc_thread_self = result;
      GC_nacl_initialize_gc_thread();
#   endif
    GC_ASSERT(result -> flags == 0 && result -> thread_blocked == 0);
    if (EXPECT(result != &first_thread, TRUE))
      GC_dirty(result);
    return(result);
}

/* Delete a thread from GC_threads.  We assume it is there.     */
/* (The code intentionally traps if it wasn't.)                 */
/* It is safe to delete the main thread.                        */
STATIC void GC_delete_thread(pthread_t id)
{
    int hv = THREAD_TABLE_INDEX(id);
    GC_thread p = GC_threads[hv];
    GC_thread prev = NULL;

#   ifdef DEBUG_THREADS
      GC_log_printf("Deleting thread %p, n_threads= %d\n",
                    (void *)(word)id, GC_count_threads());
#   endif
    GC_ASSERT(I_HOLD_LOCK());
    while (!THREAD_EQUAL(p -> id, id)) {
        prev = p;
        p = p -> next;
    }
    if (prev == 0) {
        GC_threads[hv] = p -> next;
    } else {
        GC_ASSERT(prev != &first_thread);
        prev -> next = p -> next;
        GC_dirty(prev);
    }
    if (p != &first_thread) {
#     ifdef GC_DARWIN_THREADS
        mach_port_deallocate(mach_task_self(), p->stop_info.mach_thread);
#     endif
      GC_INTERNAL_FREE(p);
    }
}

/* If a thread has been joined, but we have not yet             */
/* been notified, then there may be more than one thread        */
/* in the table with the same pthread id.                       */
/* This is OK, but we need a way to delete a specific one.      */
STATIC void GC_delete_gc_thread(GC_thread t)
{
    pthread_t id = t -> id;
    int hv = THREAD_TABLE_INDEX(id);
    GC_thread p = GC_threads[hv];
    GC_thread prev = NULL;

    GC_ASSERT(I_HOLD_LOCK());
    while (p != t) {
        prev = p;
        p = p -> next;
    }
    if (prev == 0) {
        GC_threads[hv] = p -> next;
    } else {
        GC_ASSERT(prev != &first_thread);
        prev -> next = p -> next;
        GC_dirty(prev);
    }
#   ifdef GC_DARWIN_THREADS
        mach_port_deallocate(mach_task_self(), p->stop_info.mach_thread);
#   endif
    GC_INTERNAL_FREE(p);

#   ifdef DEBUG_THREADS
      GC_log_printf("Deleted thread %p, n_threads= %d\n",
                    (void *)(word)id, GC_count_threads());
#   endif
}

/* Return a GC_thread corresponding to a given pthread_t.       */
/* Returns 0 if it's not there.                                 */
/* Caller holds allocation lock or otherwise inhibits           */
/* updates.                                                     */
/* If there is more than one thread with the given id we        */
/* return the most recent one.                                  */
GC_INNER GC_thread GC_lookup_thread(pthread_t id)
{
    GC_thread p = GC_threads[THREAD_TABLE_INDEX(id)];

    while (p != 0 && !THREAD_EQUAL(p -> id, id)) p = p -> next;
    return(p);
}

#ifndef GC_NO_FINALIZATION
  /* Called by GC_finalize() (in case of an allocation failure observed). */
  GC_INNER void GC_reset_finalizer_nested(void)
  {
    GC_thread me = GC_lookup_thread(pthread_self());

    me->finalizer_nested = 0;
  }

  /* Checks and updates the thread-local level of finalizers recursion. */
  /* Returns NULL if GC_invoke_finalizers() should not be called by the */
  /* collector (to minimize the risk of a deep finalizers recursion),   */
  /* otherwise returns a pointer to the thread-local finalizer_nested.  */
  /* Called by GC_notify_or_invoke_finalizers() only (the GC lock is    */
  /* held).                                                             */
  GC_INNER unsigned char *GC_check_finalizer_nested(void)
  {
    GC_thread me = GC_lookup_thread(pthread_self());
    unsigned nesting_level;

#   if defined(INCLUDE_LINUX_THREAD_DESCR) && defined(REDIRECT_MALLOC)
      /* As noted in GC_start_routine, an allocation may happen in  */
      /* GC_get_stack_base, causing GC_notify_or_invoke_finalizers  */
      /* to be called before the thread gets registered.            */
      if (EXPECT(NULL == me, FALSE)) return NULL;
#   endif
    nesting_level = me->finalizer_nested;
    if (nesting_level) {
      /* We are inside another GC_invoke_finalizers().          */
      /* Skip some implicitly-called GC_invoke_finalizers()     */
      /* depending on the nesting (recursion) level.            */
      if (++me->finalizer_skipped < (1U << nesting_level)) return NULL;
      me->finalizer_skipped = 0;
    }
    me->finalizer_nested = (unsigned char)(nesting_level + 1);
    return &me->finalizer_nested;
  }
#endif /* !GC_NO_FINALIZATION */

#if defined(GC_ASSERTIONS) && defined(THREAD_LOCAL_ALLOC)
  /* This is called from thread-local GC_malloc(). */
  GC_bool GC_is_thread_tsd_valid(void *tsd)
  {
    GC_thread me;
    DCL_LOCK_STATE;

    LOCK();
    me = GC_lookup_thread(pthread_self());
    UNLOCK();
    return (word)tsd >= (word)(&me->tlfs)
            && (word)tsd < (word)(&me->tlfs) + sizeof(me->tlfs);
  }
#endif /* GC_ASSERTIONS && THREAD_LOCAL_ALLOC */

GC_API int GC_CALL GC_thread_is_registered(void)
{
    pthread_t self = pthread_self();
    GC_thread me;
    DCL_LOCK_STATE;

    LOCK();
    me = GC_lookup_thread(self);
    UNLOCK();
    return me != NULL && !(me -> flags & FINISHED);
}

static pthread_t main_pthread_id;
static void *main_stack, *main_altstack;
static word main_stack_size, main_altstack_size;

GC_API void GC_CALL GC_register_altstack(void *stack, GC_word stack_size,
                                         void *altstack,
                                         GC_word altstack_size)
{
  GC_thread me;
  pthread_t self = pthread_self();
  DCL_LOCK_STATE;

  LOCK();
  me = GC_lookup_thread(self);
  if (me != NULL) {
    me->stack = (ptr_t)stack;
    me->stack_size = stack_size;
    me->altstack = (ptr_t)altstack;
    me->altstack_size = altstack_size;
  } else {
    /* This happens if we are called before GC_thr_init.    */
    main_pthread_id = self;
    main_stack = stack;
    main_stack_size = stack_size;
    main_altstack = altstack;
    main_altstack_size = altstack_size;
  }
  UNLOCK();
}

#if defined(GC_ASSERTIONS) && defined(PARALLEL_MARK)
  STATIC unsigned long GC_mark_lock_holder = NO_THREAD;
#endif

#ifdef CAN_HANDLE_FORK

  /* Prevent TSan false positive about the race during items removal    */
  /* from GC_threads.  (The race cannot happen since only one thread    */
  /* survives in the child.)                                            */
# ifdef CAN_CALL_ATFORK
    GC_ATTR_NO_SANITIZE_THREAD
# endif
  static void store_to_threads_table(int hv, GC_thread me)
  {
    GC_threads[hv] = me;
  }

/* Remove all entries from the GC_threads table, except the one (if */
/* any) for the current thread.  Also update thread identifiers     */
/* stored in the table for the current thread.  We need to do this  */
/* in the child process after a fork(), since only the current      */
/* thread survives in the child.                                    */
STATIC void GC_remove_all_threads_but_me(void)
{
    int hv;
    GC_thread me = NULL;

    for (hv = 0; hv < THREAD_TABLE_SZ; ++hv) {
      GC_thread p, next;

      for (p = GC_threads[hv]; 0 != p; p = next) {
        next = p -> next;
        if (THREAD_EQUAL(p -> id, GC_parent_pthread_self)
            && me == NULL) { /* ignore dead threads with the same id */
          me = p;
          p -> next = 0;
        } else {
#         ifdef THREAD_LOCAL_ALLOC
            if (!(p -> flags & FINISHED)) {
              /* Cannot call GC_destroy_thread_local here.  The free    */
              /* lists may be in an inconsistent state (as thread p may */
              /* be updating one of the lists by GC_generic_malloc_many */
              /* or GC_FAST_MALLOC_GRANS when fork is invoked).         */
              /* This should not be a problem because the lost elements */
              /* of the free lists will be collected during GC.         */
              GC_remove_specific_after_fork(GC_thread_key, p -> id);
            }
#         endif
          /* TODO: To avoid TSan hang (when updating GC_bytes_freed),   */
          /* we just skip explicit freeing of GC_threads entries.       */
#         if !defined(THREAD_SANITIZER) || !defined(CAN_CALL_ATFORK)
            if (p != &first_thread) GC_INTERNAL_FREE(p);
#         endif
        }
      }
      store_to_threads_table(hv, NULL);
    }

    if (NULL == me)
      return; /* fork() is called from an unregistered thread */

    /* Update pthread's id as it is not guaranteed to be the same   */
    /* between this (child) process and the parent one.             */
    me -> id = pthread_self();
#   ifdef GC_DARWIN_THREADS
      /* Update thread Id after fork (it is OK to call  */
      /* GC_destroy_thread_local and GC_free_inner      */
      /* before update).                                */
      me -> stop_info.mach_thread = mach_thread_self();
#   endif
#   ifdef USE_TKILL_ON_ANDROID
      me -> kernel_id = gettid();
#   endif

    /* Put "me" back to GC_threads.     */
    store_to_threads_table(THREAD_TABLE_INDEX(me -> id), me);

#   ifdef THREAD_LOCAL_ALLOC
#     ifdef USE_CUSTOM_SPECIFIC
        GC_update_specific_after_fork(GC_thread_key);
#     else
        {
          int res;

          /* Some TLS implementations might be not fork-friendly, so    */
          /* we re-assign thread-local pointer to 'tlfs' for safety     */
          /* instead of the assertion check (again, it is OK to call    */
          /* GC_destroy_thread_local and GC_free_inner before).         */
          res = GC_setspecific(GC_thread_key, &me->tlfs);
          if (COVERT_DATAFLOW(res) != 0)
            ABORT("GC_setspecific failed (in child)");
        }
#     endif
#   endif
}
#endif /* CAN_HANDLE_FORK */

#if defined(USE_PROC_FOR_LIBRARIES) && defined(LINUX)
  GC_INNER GC_bool GC_segment_is_thread_stack(ptr_t lo, ptr_t hi)
  {
    int i;
    GC_thread p;

    GC_ASSERT(I_HOLD_LOCK());
#   ifdef PARALLEL_MARK
      for (i = 0; i < GC_markers_m1; ++i) {
        if ((word)marker_sp[i] > (word)lo && (word)marker_sp[i] < (word)hi)
          return TRUE;
#       ifdef IA64
          if ((word)marker_bsp[i] > (word)lo
              && (word)marker_bsp[i] < (word)hi)
            return TRUE;
#       endif
      }
#   endif
    for (i = 0; i < THREAD_TABLE_SZ; i++) {
      for (p = GC_threads[i]; p != 0; p = p -> next) {
        if (0 != p -> stack_end) {
#         ifdef STACK_GROWS_UP
            if ((word)p->stack_end >= (word)lo
                && (word)p->stack_end < (word)hi)
              return TRUE;
#         else /* STACK_GROWS_DOWN */
            if ((word)p->stack_end > (word)lo
                && (word)p->stack_end <= (word)hi)
              return TRUE;
#         endif
        }
      }
    }
    return FALSE;
  }
#endif

#if (defined(HAVE_PTHREAD_ATTR_GET_NP) || defined(HAVE_PTHREAD_GETATTR_NP)) \
    && defined(IA64)
  /* Find the largest stack_base smaller than bound.  May be used       */
  /* to find the boundary between a register stack and adjacent         */
  /* immediately preceding memory stack.                                */
  GC_INNER ptr_t GC_greatest_stack_base_below(ptr_t bound)
  {
    int i;
    GC_thread p;
    ptr_t result = 0;

    GC_ASSERT(I_HOLD_LOCK());
#   ifdef PARALLEL_MARK
      for (i = 0; i < GC_markers_m1; ++i) {
        if ((word)marker_sp[i] > (word)result
            && (word)marker_sp[i] < (word)bound)
          result = marker_sp[i];
      }
#   endif
    for (i = 0; i < THREAD_TABLE_SZ; i++) {
      for (p = GC_threads[i]; p != 0; p = p -> next) {
        if ((word)p->stack_end > (word)result
            && (word)p->stack_end < (word)bound) {
          result = p -> stack_end;
        }
      }
    }
    return result;
  }
#endif /* IA64 */

#ifndef STAT_READ
# define STAT_READ read
        /* If read is wrapped, this may need to be redefined to call    */
        /* the real one.                                                */
#endif

#ifdef GC_HPUX_THREADS
# define GC_get_nprocs() pthread_num_processors_np()

#elif defined(GC_OSF1_THREADS) || defined(GC_AIX_THREADS) \
      || defined(GC_HAIKU_THREADS) || defined(GC_SOLARIS_THREADS) \
      || defined(HURD) || defined(HOST_ANDROID) || defined(NACL)
  GC_INLINE int GC_get_nprocs(void)
  {
    int nprocs = (int)sysconf(_SC_NPROCESSORS_ONLN);
    return nprocs > 0 ? nprocs : 1; /* ignore error silently */
  }

#elif defined(GC_IRIX_THREADS)
  GC_INLINE int GC_get_nprocs(void)
  {
    int nprocs = (int)sysconf(_SC_NPROC_ONLN);
    return nprocs > 0 ? nprocs : 1; /* ignore error silently */
  }

#elif defined(GC_LINUX_THREADS) /* && !HOST_ANDROID && !NACL */
  /* Return the number of processors. */
  STATIC int GC_get_nprocs(void)
  {
    /* Should be "return sysconf(_SC_NPROCESSORS_ONLN);" but that     */
    /* appears to be buggy in many cases.                             */
    /* We look for lines "cpu<n>" in /proc/stat.                      */
#   define PROC_STAT_BUF_SZ ((1 + MAX_MARKERS) * 100) /* should be enough */
    /* No need to read the entire /proc/stat to get maximum cpu<N> as   */
    /* - the requested lines are located at the beginning of the file;  */
    /* - the lines with cpu<N> where N > MAX_MARKERS are not needed.    */
    char stat_buf[PROC_STAT_BUF_SZ+1];
    int f;
    int result, i, len;

    f = open("/proc/stat", O_RDONLY);
    if (f < 0) {
      WARN("Could not open /proc/stat\n", 0);
      return 1; /* assume an uniprocessor */
    }
    len = STAT_READ(f, stat_buf, sizeof(stat_buf)-1);
    /* Unlikely that we need to retry because of an incomplete read here. */
    if (len < 0) {
      WARN("Failed to read /proc/stat, errno= %" WARN_PRIdPTR "\n",
           (signed_word)errno);
      close(f);
      return 1;
    }
    stat_buf[len] = '\0'; /* to avoid potential buffer overrun by atoi() */
    close(f);

    result = 1;
        /* Some old kernels only have a single "cpu nnnn ..."   */
        /* entry in /proc/stat.  We identify those as           */
        /* uniprocessors.                                       */

    for (i = 0; i < len - 4; ++i) {
      if (stat_buf[i] == '\n' && stat_buf[i+1] == 'c'
          && stat_buf[i+2] == 'p' && stat_buf[i+3] == 'u') {
        int cpu_no = atoi(&stat_buf[i + 4]);
        if (cpu_no >= result)
          result = cpu_no + 1;
      }
    }
    return result;
  }

#elif defined(GC_DGUX386_THREADS)
  /* Return the number of processors, or i <= 0 if it can't be determined. */
  STATIC int GC_get_nprocs(void)
  {
    int numCpus;
    struct dg_sys_info_pm_info pm_sysinfo;
    int status = 0;

    status = dg_sys_info((long int *) &pm_sysinfo,
        DG_SYS_INFO_PM_INFO_TYPE, DG_SYS_INFO_PM_CURRENT_VERSION);
    if (status < 0)
       /* set -1 for error */
       numCpus = -1;
    else
      /* Active CPUs */
      numCpus = pm_sysinfo.idle_vp_count;
    return(numCpus);
  }

#elif defined(GC_DARWIN_THREADS) || defined(GC_FREEBSD_THREADS) \
      || defined(GC_NETBSD_THREADS) || defined(GC_OPENBSD_THREADS)
  STATIC int GC_get_nprocs(void)
  {
    int mib[] = {CTL_HW,HW_NCPU};
    int res;
    size_t len = sizeof(res);

    sysctl(mib, sizeof(mib)/sizeof(int), &res, &len, NULL, 0);
    return res;
  }

#else
  /* E.g., GC_RTEMS_PTHREADS */
# define GC_get_nprocs() 1 /* not implemented */
#endif /* !GC_LINUX_THREADS && !GC_DARWIN_THREADS && ... */

#if defined(ARM32) && defined(GC_LINUX_THREADS) && !defined(NACL)
  /* Some buggy Linux/arm kernels show only non-sleeping CPUs in        */
  /* /proc/stat (and /proc/cpuinfo), so another data system source is   */
  /* tried first.  Result <= 0 on error.                                */
  STATIC int GC_get_nprocs_present(void)
  {
    char stat_buf[16];
    int f;
    int len;

    f = open("/sys/devices/system/cpu/present", O_RDONLY);
    if (f < 0)
      return -1; /* cannot open the file */

    len = STAT_READ(f, stat_buf, sizeof(stat_buf));
    close(f);

    /* Recognized file format: "0\n" or "0-<max_cpu_id>\n"      */
    /* The file might probably contain a comma-separated list   */
    /* but we do not need to handle it (just silently ignore).  */
    if (len < 2 || stat_buf[0] != '0' || stat_buf[len - 1] != '\n') {
      return 0; /* read error or unrecognized contents */
    } else if (len == 2) {
      return 1; /* an uniprocessor */
    } else if (stat_buf[1] != '-') {
      return 0; /* unrecognized contents */
    }

    stat_buf[len - 1] = '\0'; /* terminate the string */
    return atoi(&stat_buf[2]) + 1; /* skip "0-" and parse max_cpu_num */
  }
#endif /* ARM32 && GC_LINUX_THREADS && !NACL */

#if defined(CAN_HANDLE_FORK) && defined(THREAD_SANITIZER)
# include "private/gc_pmark.h" /* for MS_NONE */

  /* Workaround for TSan which does not notice that the GC lock */
  /* is acquired in fork_prepare_proc().                        */
  GC_ATTR_NO_SANITIZE_THREAD
  static GC_bool collection_in_progress(void)
  {
    return GC_mark_state != MS_NONE;
  }
#else
# define collection_in_progress() GC_collection_in_progress()
#endif

/* We hold the GC lock.  Wait until an in-progress GC has finished.     */
/* Repeatedly RELEASES GC LOCK in order to wait.                        */
/* If wait_for_all is true, then we exit with the GC lock held and no   */
/* collection in progress; otherwise we just wait for the current GC    */
/* to finish.                                                           */
STATIC void GC_wait_for_gc_completion(GC_bool wait_for_all)
{
    DCL_LOCK_STATE;
#   if !defined(THREAD_SANITIZER) || !defined(CAN_CALL_ATFORK)
      /* GC_lock_holder is accessed with the lock held, so there is no  */
      /* data race actually (unlike what is reported by TSan).          */
      GC_ASSERT(I_HOLD_LOCK());
#   endif
    ASSERT_CANCEL_DISABLED();
    if (GC_incremental && collection_in_progress()) {
        word old_gc_no = GC_gc_no;

        /* Make sure that no part of our stack is still on the mark stack, */
        /* since it's about to be unmapped.                                */
        while (GC_incremental && collection_in_progress()
               && (wait_for_all || old_gc_no == GC_gc_no)) {
            ENTER_GC();
            GC_in_thread_creation = TRUE;
            GC_collect_a_little_inner(1);
            GC_in_thread_creation = FALSE;
            EXIT_GC();
            UNLOCK();
            sched_yield();
            LOCK();
        }
    }
}

#ifdef CAN_HANDLE_FORK
/* Procedures called before and after a fork.  The goal here is to make */
/* it safe to call GC_malloc() in a forked child.  It's unclear that is */
/* attainable, since the single UNIX spec seems to imply that one       */
/* should only call async-signal-safe functions, and we probably can't  */
/* quite guarantee that.  But we give it our best shot.  (That same     */
/* spec also implies that it's not safe to call the system malloc       */
/* between fork() and exec().  Thus we're doing no worse than it.)      */

IF_CANCEL(static int fork_cancel_state;)
                                /* protected by allocation lock.        */

# ifdef PARALLEL_MARK
#   ifdef THREAD_SANITIZER
#     if defined(GC_ASSERTIONS) && defined(CAN_CALL_ATFORK)
        STATIC void GC_generic_lock(pthread_mutex_t *);
#     endif
      GC_ATTR_NO_SANITIZE_THREAD
      static void wait_for_reclaim_atfork(void);
#   else
#     define wait_for_reclaim_atfork() GC_wait_for_reclaim()
#   endif
# endif /* PARALLEL_MARK */

/* Same as GC_thread_is_registered() but assumes the allocator lock */
/* is held.                                                         */
static GC_bool is_thread_registered_inner(void)
{
  GC_thread me = GC_lookup_thread(pthread_self());

  return me != NULL && !(me -> flags & FINISHED);
}

/* Called before a fork()               */
#if defined(GC_ASSERTIONS) && defined(CAN_CALL_ATFORK)
  /* GC_lock_holder is updated safely (no data race actually).  */
  GC_ATTR_NO_SANITIZE_THREAD
#endif
static void fork_prepare_proc(void)
{
#   if defined(GC_EXPLICIT_SIGNALS_UNBLOCK) && defined(CAN_CALL_ATFORK)
      /* The signals might be blocked by fork() implementation when the */
      /* at-fork prepare handler is invoked.                            */
      if (GC_handle_fork == 1)
        GC_unblock_gc_signals();
#   endif

    /* Acquire all relevant locks, so that after releasing the locks    */
    /* the child will see a consistent state in which monitor           */
    /* invariants hold.  Unfortunately, we can't acquire libc locks     */
    /* we might need, and there seems to be no guarantee that libc      */
    /* must install a suitable fork handler.                            */
    /* Wait for an ongoing GC to finish, since we can't finish it in    */
    /* the (one remaining thread in) the child.                         */
      LOCK();
      DISABLE_CANCEL(fork_cancel_state);
      GC_parent_pthread_self = pthread_self();
                /* Following waits may include cancellation points. */
#     if defined(PARALLEL_MARK)
        if (GC_parallel)
          wait_for_reclaim_atfork();
#     endif
      if (is_thread_registered_inner()) {
        /* fork() is called from a thread registered in the collector. */
        GC_wait_for_gc_completion(TRUE);
      }
#     if defined(PARALLEL_MARK)
        if (GC_parallel) {
#         if defined(THREAD_SANITIZER) && defined(GC_ASSERTIONS) \
             && defined(CAN_CALL_ATFORK)
            /* Prevent TSan false positive about the data race  */
            /* when updating GC_mark_lock_holder.               */
            GC_generic_lock(&mark_mutex);
#         else
            GC_acquire_mark_lock();
#         endif
        }
#     endif
      GC_acquire_dirty_lock();
}

/* Called in parent after a fork() (even if the latter failed). */
#if defined(GC_ASSERTIONS) && defined(CAN_CALL_ATFORK)
  GC_ATTR_NO_SANITIZE_THREAD
#endif
static void fork_parent_proc(void)
{
    GC_release_dirty_lock();
#   if defined(PARALLEL_MARK)
      if (GC_parallel) {
#       if defined(THREAD_SANITIZER) && defined(GC_ASSERTIONS) \
           && defined(CAN_CALL_ATFORK)
          /* To match that in fork_prepare_proc. */
          (void)pthread_mutex_unlock(&mark_mutex);
#       else
          GC_release_mark_lock();
#       endif
      }
#   endif
    RESTORE_CANCEL(fork_cancel_state);
#   ifdef GC_ASSERTIONS
      BZERO(&GC_parent_pthread_self, sizeof(pthread_t));
#   endif
    UNLOCK();
}

/* Called in child after a fork()       */
#if defined(GC_ASSERTIONS) && defined(CAN_CALL_ATFORK)
  GC_ATTR_NO_SANITIZE_THREAD
#endif
static void fork_child_proc(void)
{
#   ifdef GC_ASSERTIONS
      /* Update GC_lock_holder as value of pthread_self() might     */
      /* differ from that of the parent process.                    */
      SET_LOCK_HOLDER();
#   endif
    GC_release_dirty_lock();
#   ifndef GC_DISABLE_INCREMENTAL
      GC_dirty_update_child();
#   endif
#   ifdef PARALLEL_MARK
      if (GC_parallel) {
#       if !defined(GC_ASSERTIONS) \
           || (defined(THREAD_SANITIZER) && defined(CAN_CALL_ATFORK))
          /* Do not change GC_mark_lock_holder. */
#       else
          GC_mark_lock_holder = NO_THREAD;
#       endif
        /* The unlock operation may fail on some targets, just ignore   */
        /* the error silently.                                          */
        (void)pthread_mutex_unlock(&mark_mutex);
        /* Reinitialize the mark lock.  The reason is the same as for   */
        /* GC_allocate_ml below.                                        */
        (void)pthread_mutex_destroy(&mark_mutex);
        /* TODO: GLIBC_2_19_TSX_BUG has no effect. */
        if (pthread_mutex_init(&mark_mutex, NULL) != 0)
          ABORT("mark_mutex re-init failed in child");
        /* Turn off parallel marking in the child, since we are probably  */
        /* just going to exec, and we would have to restart mark threads. */
        GC_parallel = FALSE;
      }
#     ifdef THREAD_SANITIZER
        /* TSan does not support threads creation in the child process. */
        available_markers_m1 = 0;
#     endif
#   endif
    /* Clean up the thread table, so that just our thread is left.      */
    GC_remove_all_threads_but_me();
    RESTORE_CANCEL(fork_cancel_state);
#   ifdef GC_ASSERTIONS
      BZERO(&GC_parent_pthread_self, sizeof(pthread_t));
#   endif
    UNLOCK();
    /* Even though after a fork the child only inherits the single      */
    /* thread that called the fork(), if another thread in the parent   */
    /* was attempting to lock the mutex while being held in             */
    /* fork_child_prepare(), the mutex will be left in an inconsistent  */
    /* state in the child after the UNLOCK.  This is the case, at       */
    /* least, in Mac OS X and leads to an unusable GC in the child      */
    /* which will block when attempting to perform any GC operation     */
    /* that acquires the allocation mutex.                              */
#   ifdef USE_PTHREAD_LOCKS
      GC_ASSERT(I_DONT_HOLD_LOCK());
      /* Reinitialize the mutex.  It should be safe since we are        */
      /* running this in the child which only inherits a single thread. */
      /* mutex_destroy() may return EBUSY, which makes no sense, but    */
      /* that is the reason for the need of the reinitialization.       */
      (void)pthread_mutex_destroy(&GC_allocate_ml);
      /* TODO: Probably some targets (e.g. with GLIBC_2_19_TSX_BUG) might */
      /* need the default mutex attribute to be passed instead of NULL.   */
      if (0 != pthread_mutex_init(&GC_allocate_ml, NULL))
        ABORT("pthread_mutex_init failed (in child)");
#   endif
}

  /* Routines for fork handling by client (no-op if pthread_atfork works). */
  GC_API void GC_CALL GC_atfork_prepare(void)
  {
    if (!EXPECT(GC_is_initialized, TRUE)) GC_init();
    if (GC_handle_fork <= 0)
      fork_prepare_proc();
  }

  GC_API void GC_CALL GC_atfork_parent(void)
  {
    if (GC_handle_fork <= 0)
      fork_parent_proc();
  }

  GC_API void GC_CALL GC_atfork_child(void)
  {
    if (GC_handle_fork <= 0)
      fork_child_proc();
  }
#endif /* CAN_HANDLE_FORK */

#ifdef INCLUDE_LINUX_THREAD_DESCR
  __thread int GC_dummy_thread_local;
#endif

#ifdef PARALLEL_MARK
  static void setup_mark_lock(void);

  static unsigned required_markers_cnt = 0;
                        /* The default value (0) means the number of    */
                        /* markers should be selected automatically.    */
#endif /* PARALLEL_MARK */

GC_API void GC_CALL GC_set_markers_count(unsigned markers GC_ATTR_UNUSED)
{
# ifdef PARALLEL_MARK
    required_markers_cnt = markers < MAX_MARKERS ? markers : MAX_MARKERS;
# endif
}

#ifndef DONT_USE_ATEXIT
  STATIC pthread_t GC_main_thread_id;

  GC_INNER GC_bool GC_is_main_thread(void)
  {
    GC_ASSERT(GC_thr_initialized);
    return THREAD_EQUAL(GC_main_thread_id, pthread_self());
  }
#endif /* !DONT_USE_ATEXIT */

GC_INNER void GC_thr_init(void)
{
  GC_ASSERT(I_HOLD_LOCK());
  if (GC_thr_initialized) return;
  GC_thr_initialized = TRUE;

  GC_ASSERT((word)&GC_threads % ALIGNMENT == 0);
# ifdef CAN_HANDLE_FORK
    /* Prepare for forks if requested.  */
    if (GC_handle_fork) {
#     ifdef CAN_CALL_ATFORK
        if (pthread_atfork(fork_prepare_proc, fork_parent_proc,
                           fork_child_proc) == 0) {
          /* Handlers successfully registered.  */
          GC_handle_fork = 1;
        } else
#     endif
      /* else */ if (GC_handle_fork != -1)
        ABORT("pthread_atfork failed");
    }
# endif
# ifdef INCLUDE_LINUX_THREAD_DESCR
    /* Explicitly register the region including the address     */
    /* of a thread local variable.  This should include thread  */
    /* locals for the main thread, except for those allocated   */
    /* in response to dlopen calls.                             */
    {
      ptr_t thread_local_addr = (ptr_t)(&GC_dummy_thread_local);
      ptr_t main_thread_start, main_thread_end;
      if (!GC_enclosing_mapping(thread_local_addr, &main_thread_start,
                                &main_thread_end)) {
        ABORT("Failed to find mapping for main thread thread locals");
      } else {
        /* main_thread_start and main_thread_end are initialized.       */
        GC_add_roots_inner(main_thread_start, main_thread_end, FALSE);
      }
    }
# endif
  /* Add the initial thread, so we can stop it. */
  {
    pthread_t self = pthread_self();
    GC_thread t = GC_new_thread(self);

    if (t == NULL)
      ABORT("Failed to allocate memory for the initial thread");
#   ifdef GC_DARWIN_THREADS
      t -> stop_info.mach_thread = mach_thread_self();
#   else
      t -> stop_info.stack_ptr = GC_approx_sp();
#   endif
#   ifndef DONT_USE_ATEXIT
      GC_main_thread_id = self;
#   endif
    t -> flags = DETACHED | MAIN_THREAD;
    if (THREAD_EQUAL(self, main_pthread_id)) {
      t -> stack = (ptr_t)main_stack;
      t -> stack_size = main_stack_size;
      t -> altstack = (ptr_t)main_altstack;
      t -> altstack_size = main_altstack_size;
    }
  }

  /* Set GC_nprocs and available_markers_m1.    */
  {
    char * nprocs_string = GETENV("GC_NPROCS");
    GC_nprocs = -1;
    if (nprocs_string != NULL) GC_nprocs = atoi(nprocs_string);
  }
  if (GC_nprocs <= 0
#     if defined(ARM32) && defined(GC_LINUX_THREADS) && !defined(NACL)
        && (GC_nprocs = GC_get_nprocs_present()) <= 1
                                /* Workaround for some Linux/arm kernels */
#     endif
      )
  {
    GC_nprocs = GC_get_nprocs();
  }
  if (GC_nprocs <= 0) {
    WARN("GC_get_nprocs() returned %" WARN_PRIdPTR "\n",
         (signed_word)GC_nprocs);
    GC_nprocs = 2; /* assume dual-core */
#   ifdef PARALLEL_MARK
      available_markers_m1 = 0; /* but use only one marker */
#   endif
  } else {
#   ifdef PARALLEL_MARK
      {
        char * markers_string = GETENV("GC_MARKERS");
        int markers = required_markers_cnt;

        if (markers_string != NULL) {
          markers = atoi(markers_string);
          if (markers <= 0 || markers > MAX_MARKERS) {
            WARN("Too big or invalid number of mark threads: %" WARN_PRIdPTR
                 "; using maximum threads\n", (signed_word)markers);
            markers = MAX_MARKERS;
          }
        } else if (0 == markers) {
          /* Unless the client sets the desired number of       */
          /* parallel markers, it is determined based on the    */
          /* number of CPU cores.                               */
          markers = GC_nprocs;
#         if defined(GC_MIN_MARKERS) && !defined(CPPCHECK)
            /* This is primarily for targets without getenv().  */
            if (markers < GC_MIN_MARKERS)
              markers = GC_MIN_MARKERS;
#         endif
          if (markers > MAX_MARKERS)
            markers = MAX_MARKERS; /* silently limit the value */
        }
        available_markers_m1 = markers - 1;
      }
#   endif
  }
  GC_COND_LOG_PRINTF("Number of processors: %d\n", GC_nprocs);

# if defined(BASE_ATOMIC_OPS_EMULATED) && !defined(GC_DARWIN_THREADS) \
     && !defined(GC_OPENBSD_UTHREADS) && !defined(NACL) \
     && !defined(PLATFORM_STOP_WORLD) && !defined(SN_TARGET_PSP2)
    /* Ensure the process is running on just one CPU core.      */
    /* This is needed because the AO primitives emulated with   */
    /* locks cannot be used inside signal handlers.             */
    {
      cpu_set_t mask;
      int cpu_set_cnt = 0;
      int cpu_lowest_set = 0;
      int i = GC_nprocs > 1 ? GC_nprocs : 2; /* check at least 2 cores */

      if (sched_getaffinity(0 /* current process */,
                            sizeof(mask), &mask) == -1)
        ABORT_ARG1("sched_getaffinity failed", ": errno= %d", errno);
      while (i-- > 0)
        if (CPU_ISSET(i, &mask)) {
          cpu_lowest_set = i;
          cpu_set_cnt++;
        }
      if (0 == cpu_set_cnt)
        ABORT("sched_getaffinity returned empty mask");
      if (cpu_set_cnt > 1) {
        CPU_ZERO(&mask);
        CPU_SET(cpu_lowest_set, &mask); /* select just one CPU */
        if (sched_setaffinity(0, sizeof(mask), &mask) == -1)
          ABORT_ARG1("sched_setaffinity failed", ": errno= %d", errno);
        WARN("CPU affinity mask is set to %p\n", (word)1 << cpu_lowest_set);
      }
    }
# endif /* BASE_ATOMIC_OPS_EMULATED */

# ifndef GC_DARWIN_THREADS
    GC_stop_init();
# endif

# ifdef PARALLEL_MARK
    if (available_markers_m1 <= 0) {
      /* Disable parallel marking.      */
      GC_parallel = FALSE;
      GC_COND_LOG_PRINTF(
                "Single marker thread, turning off parallel marking\n");
    } else {
      setup_mark_lock();
    }
# endif
}

/* Perform all initializations, including those that    */
/* may require allocation.                              */
/* Called without allocation lock.                      */
/* Must be called before a second thread is created.    */
/* Did we say it's called without the allocation lock?  */
GC_INNER void GC_init_parallel(void)
{
#   if defined(THREAD_LOCAL_ALLOC)
      DCL_LOCK_STATE;
#   endif
    if (parallel_initialized) return;
    parallel_initialized = TRUE;

    /* GC_init() calls us back, so set flag first.      */
    if (!GC_is_initialized) GC_init();
    /* Initialize thread local free lists if used.      */
#   if defined(THREAD_LOCAL_ALLOC)
      LOCK();
      GC_init_thread_local(&(GC_lookup_thread(pthread_self())->tlfs));
      UNLOCK();
#   endif
}

#ifndef GC_NO_PTHREAD_SIGMASK
  GC_API int WRAP_FUNC(pthread_sigmask)(int how, const sigset_t *set,
                                        sigset_t *oset)
  {
    sigset_t fudged_set;

    INIT_REAL_SYMS();
    if (set != NULL && (how == SIG_BLOCK || how == SIG_SETMASK)) {
        int sig_suspend = GC_get_suspend_signal();

        fudged_set = *set;
        GC_ASSERT(sig_suspend >= 0);
        if (sigdelset(&fudged_set, sig_suspend) != 0)
            ABORT("sigdelset failed");
        set = &fudged_set;
    }
    return(REAL_FUNC(pthread_sigmask)(how, set, oset));
  }
#endif /* !GC_NO_PTHREAD_SIGMASK */

static GC_bool do_blocking_enter(GC_thread me)
{
#   if defined(SPARC) || defined(IA64)
        ptr_t stack_ptr = GC_save_regs_in_stack();
        /* TODO: regs saving already done by GC_with_callee_saves_pushed */
#   elif defined(E2K)
        size_t stack_size;
#   endif
    GC_bool topOfStackUnset = FALSE;

    GC_ASSERT(I_HOLD_LOCK());
    GC_ASSERT(!(me -> thread_blocked));
#   ifdef SPARC
        me -> stop_info.stack_ptr = stack_ptr;
#   else
        me -> stop_info.stack_ptr = GC_approx_sp();
#   endif
#   if defined(GC_DARWIN_THREADS) && !defined(DARWIN_DONT_PARSE_STACK)
        if (me -> topOfStack == NULL) {
            /* GC_do_blocking_inner is not called recursively,  */
            /* so topOfStack should be computed now.            */
            topOfStackUnset = TRUE;
            me -> topOfStack = GC_FindTopOfStack(0);
        }
#   endif
#   ifdef IA64
        me -> backing_store_ptr = stack_ptr;
#   elif defined(E2K)
        GC_ASSERT(NULL == me -> backing_store_end);
        stack_size = GC_alloc_and_get_procedure_stack(&me->backing_store_end);
        me->backing_store_ptr = me->backing_store_end + stack_size;
#   endif
    me -> thread_blocked = (unsigned char)TRUE;
    /* Save context here if we want to support precise stack marking */
    return topOfStackUnset;
}

static void do_blocking_leave(GC_thread me, GC_bool topOfStackUnset)
{
    GC_ASSERT(I_HOLD_LOCK());
#   if defined(CPPCHECK)
      GC_noop1((word)&me->thread_blocked);
#   endif
    me -> thread_blocked = FALSE;
#   ifdef E2K
        GC_ASSERT(me -> backing_store_end != NULL);
         /* Note that me->backing_store_end value here may differ from  */
         /* the one stored in this function previously.                 */
        GC_INTERNAL_FREE(me -> backing_store_end);
        me -> backing_store_ptr = NULL;
        me -> backing_store_end = NULL;
#   endif
#   if defined(GC_DARWIN_THREADS) && !defined(DARWIN_DONT_PARSE_STACK)
        if (topOfStackUnset)
            me -> topOfStack = NULL; /* make topOfStack unset again */
#   else
        (void)topOfStackUnset;
#   endif
}

/* Wrapper for functions that are likely to block for an appreciable    */
/* length of time.                                                      */
GC_INNER void GC_do_blocking_inner(ptr_t data, void * context GC_ATTR_UNUSED)
{
    struct blocking_data *d = (struct blocking_data *)data;
    GC_thread me;
    GC_bool topOfStackUnset;
    DCL_LOCK_STATE;

    LOCK();
    me = GC_lookup_thread(pthread_self());
    topOfStackUnset = do_blocking_enter(me);
    UNLOCK();

    d -> client_data = (d -> fn)(d -> client_data);

    LOCK();   /* This will block if the world is stopped.       */
#   ifdef LINT2
      {
#        ifdef GC_ASSERTIONS
           GC_thread saved_me = me;
#        endif

         /* The pointer to the GC thread descriptor should not be   */
         /* changed while the thread is registered but a static     */
         /* analysis tool might complain that this pointer value    */
         /* (obtained in the first locked section) is unreliable in */
         /* the second locked section.                              */
         me = GC_lookup_thread(pthread_self());
         GC_ASSERT(me == saved_me);
      }
#   endif
#   if defined(GC_ENABLE_SUSPEND_THREAD) && !defined(GC_DARWIN_THREADS) \
       && !defined(GC_OPENBSD_UTHREADS) && !defined(NACL) \
       && !defined(PLATFORM_STOP_WORLD) && !defined(SN_TARGET_PSP2)
      /* Note: this code cannot be moved into do_blocking_leave()   */
      /* otherwise there could be a static analysis tool warning    */
      /* (false positive) about unlock without a matching lock.     */
      while (EXPECT((me -> stop_info.ext_suspend_cnt & 1) != 0, FALSE)) {
        word suspend_cnt = (word)(me -> stop_info.ext_suspend_cnt);
                        /* read suspend counter (number) before unlocking */

        UNLOCK();
        GC_suspend_self_inner(me, suspend_cnt);
        LOCK();
      }
#   endif
    do_blocking_leave(me, topOfStackUnset);
    UNLOCK();
}

#if defined(GC_ENABLE_SUSPEND_THREAD) && !defined(GC_DARWIN_THREADS) \
    && !defined(GC_OPENBSD_UTHREADS) && !defined(NACL) \
    && !defined(PLATFORM_STOP_WORLD) && !defined(SN_TARGET_PSP2)
  /* Similar to GC_do_blocking_inner() but assuming the GC lock is held */
  /* and fn is GC_suspend_self_inner.                                   */
  GC_INNER void GC_suspend_self_blocked(ptr_t thread_me,
                                        void * context GC_ATTR_UNUSED)
  {
    GC_thread me = (GC_thread)thread_me;
    GC_bool topOfStackUnset;
    DCL_LOCK_STATE;

    GC_ASSERT(I_HOLD_LOCK());
    topOfStackUnset = do_blocking_enter(me);
    while ((me -> stop_info.ext_suspend_cnt & 1) != 0) {
      word suspend_cnt = (word)(me -> stop_info.ext_suspend_cnt);

      UNLOCK();
      GC_suspend_self_inner(me, suspend_cnt);
      LOCK();
    }
    do_blocking_leave(me, topOfStackUnset);
  }
#endif

GC_API void GC_CALL GC_set_stackbottom(void *gc_thread_handle,
                                       const struct GC_stack_base *sb)
{
    GC_thread t = (GC_thread)gc_thread_handle;

    GC_ASSERT(sb -> mem_base != NULL);
    if (!EXPECT(GC_is_initialized, TRUE)) {
        GC_ASSERT(NULL == t);
    } else {
        GC_ASSERT(I_HOLD_LOCK());
        if (NULL == t) /* current thread? */
            t = GC_lookup_thread(pthread_self());
        GC_ASSERT((t -> flags & FINISHED) == 0);
        GC_ASSERT(!(t -> thread_blocked)
                  && NULL == t -> traced_stack_sect); /* for now */

        if ((t -> flags & MAIN_THREAD) == 0) {
            t -> stack_end = (ptr_t)sb->mem_base;
#           ifdef IA64
                t -> backing_store_end = (ptr_t)sb->reg_base;
#           endif
            return;
        }
        /* Otherwise alter the stack bottom of the primordial thread.   */
    }

    GC_stackbottom = (char*)sb->mem_base;
#   ifdef IA64
        GC_register_stackbottom = (ptr_t)sb->reg_base;
#   endif
}

GC_API void * GC_CALL GC_get_my_stackbottom(struct GC_stack_base *sb)
{
    pthread_t self = pthread_self();
    GC_thread me;
    DCL_LOCK_STATE;

    LOCK();
    me = GC_lookup_thread(self);
    /* The thread is assumed to be registered.  */
    if ((me -> flags & MAIN_THREAD) == 0) {
        sb -> mem_base = me -> stack_end;
#       ifdef IA64
            sb -> reg_base = me -> backing_store_end;
#       elif defined(E2K)
            sb -> reg_base = NULL;
#       endif
    } else {
        sb -> mem_base = GC_stackbottom;
#       ifdef IA64
            sb -> reg_base = GC_register_stackbottom;
#       elif defined(E2K)
            sb -> reg_base = NULL;
#       endif
    }
    UNLOCK();
    return (void *)me; /* gc_thread_handle */
}

/* GC_call_with_gc_active() has the opposite to GC_do_blocking()        */
/* functionality.  It might be called from a user function invoked by   */
/* GC_do_blocking() to temporarily back allow calling any GC function   */
/* and/or manipulating pointers to the garbage collected heap.          */
GC_API void * GC_CALL GC_call_with_gc_active(GC_fn_type fn,
                                             void * client_data)
{
    struct GC_traced_stack_sect_s stacksect;
    pthread_t self = pthread_self();
    GC_thread me;
#   ifdef E2K
      size_t stack_size;
#   endif
    DCL_LOCK_STATE;

    LOCK();   /* This will block if the world is stopped.       */
    me = GC_lookup_thread(self);

    /* Adjust our stack bottom value (this could happen unless  */
    /* GC_get_stack_base() was used which returned GC_SUCCESS). */
    if ((me -> flags & MAIN_THREAD) == 0) {
      GC_ASSERT(me -> stack_end != NULL);
      if ((word)me->stack_end HOTTER_THAN (word)(&stacksect))
        me -> stack_end = (ptr_t)(&stacksect);
    } else {
      /* The original stack. */
      if ((word)GC_stackbottom HOTTER_THAN (word)(&stacksect))
        GC_stackbottom = (ptr_t)COVERT_DATAFLOW(&stacksect);
    }

    if (!me->thread_blocked) {
      /* We are not inside GC_do_blocking() - do nothing more.  */
      UNLOCK();
      client_data = fn(client_data);
      /* Prevent treating the above as a tail call.     */
      GC_noop1(COVERT_DATAFLOW(&stacksect));
      return client_data; /* result */
    }

#   if defined(GC_ENABLE_SUSPEND_THREAD) && !defined(GC_DARWIN_THREADS) \
       && !defined(GC_OPENBSD_UTHREADS) && !defined(NACL) \
       && !defined(PLATFORM_STOP_WORLD) && !defined(SN_TARGET_PSP2)
      while (EXPECT((me -> stop_info.ext_suspend_cnt & 1) != 0, FALSE)) {
        word suspend_cnt = (word)(me -> stop_info.ext_suspend_cnt);
        UNLOCK();
        GC_suspend_self_inner(me, suspend_cnt);
        LOCK();
      }
#   endif

    /* Setup new "stack section".       */
    stacksect.saved_stack_ptr = me -> stop_info.stack_ptr;
#   ifdef IA64
      /* This is the same as in GC_call_with_stack_base().      */
      stacksect.backing_store_end = GC_save_regs_in_stack();
      /* Unnecessarily flushes register stack,          */
      /* but that probably doesn't hurt.                */
      stacksect.saved_backing_store_ptr = me -> backing_store_ptr;
#   elif defined(E2K)
      GC_ASSERT(me -> backing_store_end != NULL);
      GC_INTERNAL_FREE(me -> backing_store_end);
      me -> backing_store_ptr = NULL;
      me -> backing_store_end = NULL;
#   endif
    stacksect.prev = me -> traced_stack_sect;
    me -> thread_blocked = FALSE;
    me -> traced_stack_sect = &stacksect;

    UNLOCK();
    client_data = fn(client_data);
    GC_ASSERT(me -> thread_blocked == FALSE);
    GC_ASSERT(me -> traced_stack_sect == &stacksect);

    /* Restore original "stack section".        */
#   if defined(CPPCHECK)
      GC_noop1((word)me->traced_stack_sect);
#   endif
#   ifdef E2K
      (void)GC_save_regs_in_stack();
#   endif
    LOCK();
    me -> traced_stack_sect = stacksect.prev;
#   ifdef IA64
      me -> backing_store_ptr = stacksect.saved_backing_store_ptr;
#   elif defined(E2K)
      GC_ASSERT(NULL == me -> backing_store_end);
      stack_size = GC_alloc_and_get_procedure_stack(&me->backing_store_end);
      me->backing_store_ptr = me->backing_store_end + stack_size;
#   endif
    me -> thread_blocked = (unsigned char)TRUE;
    me -> stop_info.stack_ptr = stacksect.saved_stack_ptr;
    UNLOCK();

    return client_data; /* result */
}

STATIC void GC_unregister_my_thread_inner(GC_thread me)
{
    GC_ASSERT(I_HOLD_LOCK());
#   ifdef DEBUG_THREADS
      GC_log_printf(
                "Unregistering thread %p, gc_thread= %p, n_threads= %d\n",
                (void *)(word)me->id, (void *)me, GC_count_threads());
#   endif
    GC_ASSERT(!(me -> flags & FINISHED));
#   if defined(THREAD_LOCAL_ALLOC)
      GC_ASSERT(GC_getspecific(GC_thread_key) == &me->tlfs);
      GC_destroy_thread_local(&(me->tlfs));
#   endif
#   ifdef NACL
      GC_nacl_shutdown_gc_thread();
      GC_nacl_gc_thread_self = NULL;
#   endif
#   if defined(GC_HAVE_PTHREAD_EXIT) || !defined(GC_NO_PTHREAD_CANCEL)
      /* Handle DISABLED_GC flag which is set by the    */
      /* intercepted pthread_cancel or pthread_exit.    */
      if ((me -> flags & DISABLED_GC) != 0) {
        GC_dont_gc--;
      }
#   endif
    if (me -> flags & DETACHED) {
        GC_delete_thread(pthread_self());
    } else {
        me -> flags |= FINISHED;
    }
#   if defined(THREAD_LOCAL_ALLOC)
      /* It is required to call remove_specific defined in specific.c. */
      GC_remove_specific(GC_thread_key);
#   endif
}

GC_API int GC_CALL GC_unregister_my_thread(void)
{
    pthread_t self = pthread_self();
    GC_thread me;
    IF_CANCEL(int cancel_state;)
    DCL_LOCK_STATE;

    LOCK();
    DISABLE_CANCEL(cancel_state);
    /* Wait for any GC that may be marking from our stack to    */
    /* complete before we remove this thread.                   */
    GC_wait_for_gc_completion(FALSE);
    me = GC_lookup_thread(self);
#   ifdef DEBUG_THREADS
        GC_log_printf(
                "Called GC_unregister_my_thread on %p, gc_thread= %p\n",
                (void *)(word)self, (void *)me);
#   endif
    GC_ASSERT(THREAD_EQUAL(me->id, self));
    GC_unregister_my_thread_inner(me);
    RESTORE_CANCEL(cancel_state);
    UNLOCK();
    return GC_SUCCESS;
}

/* Called at thread exit.                               */
/* Never called for main thread.  That's OK, since it   */
/* results in at most a tiny one-time leak.  And        */
/* linuxthreads doesn't reclaim the main threads        */
/* resources or id anyway.                              */
GC_INNER_PTHRSTART void GC_thread_exit_proc(void *arg)
{
    IF_CANCEL(int cancel_state;)
    DCL_LOCK_STATE;

#   ifdef DEBUG_THREADS
        GC_log_printf("Called GC_thread_exit_proc on %p, gc_thread= %p\n",
                      (void *)(word)((GC_thread)arg)->id, arg);
#   endif
    LOCK();
    DISABLE_CANCEL(cancel_state);
    GC_wait_for_gc_completion(FALSE);
    GC_unregister_my_thread_inner((GC_thread)arg);
    RESTORE_CANCEL(cancel_state);
    UNLOCK();
}

#if !defined(SN_TARGET_ORBIS) && !defined(SN_TARGET_PSP2)
  GC_API int WRAP_FUNC(pthread_join)(pthread_t thread, void **retval)
  {
    int result;
    GC_thread t;
    DCL_LOCK_STATE;

    INIT_REAL_SYMS();
    LOCK();
    t = (GC_thread)COVERT_DATAFLOW(GC_lookup_thread(thread));
    /* This is guaranteed to be the intended one, since the thread id   */
    /* can't have been recycled by pthreads.                            */
    UNLOCK();
    result = REAL_FUNC(pthread_join)(thread, retval);
# if defined(GC_FREEBSD_THREADS)
    /* On FreeBSD, the wrapped pthread_join() sometimes returns (what
       appears to be) a spurious EINTR which caused the test and real code
       to gratuitously fail.  Having looked at system pthread library source
       code, I see how this return code may be generated.  In one path of
       code, pthread_join() just returns the errno setting of the thread
       being joined.  This does not match the POSIX specification or the
       local man pages thus I have taken the liberty to catch this one
       spurious return value properly conditionalized on GC_FREEBSD_THREADS. */
    if (result == EINTR) result = 0;
# endif
    if (result == 0) {
        LOCK();
        /* Here the pthread thread id may have been recycled.           */
        /* Delete the thread from GC_threads (unless it has been        */
        /* registered again from the client thread key destructor).     */
        if ((t -> flags & FINISHED) != 0)
          GC_delete_gc_thread(t);
        UNLOCK();
    }
    return result;
  }

  GC_API int WRAP_FUNC(pthread_detach)(pthread_t thread)
  {
    int result;
    GC_thread t;
    DCL_LOCK_STATE;

    INIT_REAL_SYMS();
    if (!EXPECT(GC_is_initialized, TRUE)) {
      /*
       * The only case this seems to be needed is when the client calls
       * pthread_detach(pthread_self()) before the collector initialization.
       */
      GC_init();
    }
    LOCK();
    t = (GC_thread)COVERT_DATAFLOW(GC_lookup_thread(thread));
    UNLOCK();
    result = REAL_FUNC(pthread_detach)(thread);
    if (result == 0) {
      LOCK();
      t -> flags |= DETACHED;
      /* Here the pthread thread id may have been recycled. */
      if ((t -> flags & FINISHED) != 0) {
        GC_delete_gc_thread(t);
      }
      UNLOCK();
    }
    return result;
  }
#endif /* !SN_TARGET_ORBIS && !SN_TARGET_PSP2 */

#ifndef GC_NO_PTHREAD_CANCEL
  /* We should deal with the fact that apparently on Solaris and,       */
  /* probably, on some Linux we can't collect while a thread is         */
  /* exiting, since signals aren't handled properly.  This currently    */
  /* gives rise to deadlocks.  The only workaround seen is to intercept */
  /* pthread_cancel() and pthread_exit(), and disable the collections   */
  /* until the thread exit handler is called.  That's ugly, because we  */
  /* risk growing the heap unnecessarily. But it seems that we don't    */
  /* really have an option in that the process is not in a fully        */
  /* functional state while a thread is exiting.                        */
  GC_API int WRAP_FUNC(pthread_cancel)(pthread_t thread)
  {
#   ifdef CANCEL_SAFE
      GC_thread t;
      DCL_LOCK_STATE;
#   endif

    INIT_REAL_SYMS();
#   ifdef CANCEL_SAFE
      LOCK();
      t = GC_lookup_thread(thread);
      /* We test DISABLED_GC because pthread_exit could be called at    */
      /* the same time.  (If t is NULL then pthread_cancel should       */
      /* return ESRCH.)                                                 */
      if (t != NULL && (t -> flags & DISABLED_GC) == 0) {
        t -> flags |= DISABLED_GC;
        GC_dont_gc++;
      }
      UNLOCK();
#   endif
    return REAL_FUNC(pthread_cancel)(thread);
  }
#endif /* !GC_NO_PTHREAD_CANCEL */

#ifdef GC_HAVE_PTHREAD_EXIT
  GC_API GC_PTHREAD_EXIT_ATTRIBUTE void WRAP_FUNC(pthread_exit)(void *retval)
  {
    pthread_t self = pthread_self();
    GC_thread me;
    DCL_LOCK_STATE;

    INIT_REAL_SYMS();
    LOCK();
    me = GC_lookup_thread(self);
    /* We test DISABLED_GC because someone else could call    */
    /* pthread_cancel at the same time.                       */
    if (me != 0 && (me -> flags & DISABLED_GC) == 0) {
      me -> flags |= DISABLED_GC;
      GC_dont_gc++;
    }
    UNLOCK();

    REAL_FUNC(pthread_exit)(retval);
  }
#endif /* GC_HAVE_PTHREAD_EXIT */

GC_INNER GC_bool GC_in_thread_creation = FALSE;
                                /* Protected by allocation lock. */

GC_INLINE void GC_record_stack_base(GC_thread me,
                                    const struct GC_stack_base *sb)
{
#   ifndef GC_DARWIN_THREADS
      me -> stop_info.stack_ptr = (ptr_t)sb->mem_base;
#   endif
    me -> stack_end = (ptr_t)sb->mem_base;
    if (me -> stack_end == NULL)
      ABORT("Bad stack base in GC_register_my_thread");
#   ifdef IA64
      me -> backing_store_end = (ptr_t)sb->reg_base;
#   endif
}

STATIC GC_thread GC_register_my_thread_inner(const struct GC_stack_base *sb,
                                             pthread_t my_pthread)
{
    GC_thread me;

    GC_in_thread_creation = TRUE; /* OK to collect from unknown thread. */
    me = GC_new_thread(my_pthread);
    GC_in_thread_creation = FALSE;
    if (me == 0)
      ABORT("Failed to allocate memory for thread registering");
#   ifdef GC_DARWIN_THREADS
      me -> stop_info.mach_thread = mach_thread_self();
#   endif
    GC_record_stack_base(me, sb);
#   ifdef GC_EXPLICIT_SIGNALS_UNBLOCK
      /* Since this could be executed from a detached thread    */
      /* destructor, our signals might already be blocked.      */
      GC_unblock_gc_signals();
#   endif
    return me;
}

GC_API void GC_CALL GC_allow_register_threads(void)
{
# ifdef GC_ASSERTIONS
    DCL_LOCK_STATE;

    /* Check GC is initialized and the current thread is registered. */
    LOCK(); /* just to match that in win32_threads.c */
    GC_ASSERT(GC_lookup_thread(pthread_self()) != 0);
    UNLOCK();
# endif
  INIT_REAL_SYMS(); /* to initialize symbols while single-threaded */
  GC_init_lib_bounds();
  GC_start_mark_threads();
  set_need_to_lock();
}

GC_API int GC_CALL GC_register_my_thread(const struct GC_stack_base *sb)
{
    pthread_t self = pthread_self();
    GC_thread me;
    DCL_LOCK_STATE;

    if (GC_need_to_lock == FALSE)
        ABORT("Threads explicit registering is not previously enabled");

    LOCK();
    me = GC_lookup_thread(self);
    if (0 == me) {
        me = GC_register_my_thread_inner(sb, self);
#       if defined(CPPCHECK)
          GC_noop1(me->flags);
#       endif
        me -> flags |= DETACHED;
          /* Treat as detached, since we do not need to worry about     */
          /* pointer results.                                           */
#       if defined(THREAD_LOCAL_ALLOC)
          GC_init_thread_local(&(me->tlfs));
#       endif
        UNLOCK();
        return GC_SUCCESS;
    } else if ((me -> flags & FINISHED) != 0) {
        /* This code is executed when a thread is registered from the   */
        /* client thread key destructor.                                */
#       ifdef NACL
          GC_nacl_gc_thread_self = me;
          GC_nacl_initialize_gc_thread();
#       endif
#       ifdef GC_DARWIN_THREADS
          /* Reinitialize mach_thread to avoid thread_suspend fail      */
          /* with MACH_SEND_INVALID_DEST error.                         */
          me -> stop_info.mach_thread = mach_thread_self();
#       endif
        GC_record_stack_base(me, sb);
        me -> flags &= ~FINISHED; /* but not DETACHED */
#       ifdef GC_EXPLICIT_SIGNALS_UNBLOCK
          /* Since this could be executed from a thread destructor,     */
          /* our signals might be blocked.                              */
          GC_unblock_gc_signals();
#       endif
#       if defined(THREAD_LOCAL_ALLOC)
          GC_init_thread_local(&(me->tlfs));
#       endif
#       if defined(GC_ENABLE_SUSPEND_THREAD) && !defined(GC_DARWIN_THREADS) \
           && !defined(GC_OPENBSD_UTHREADS) && !defined(NACL) \
           && !defined(PLATFORM_STOP_WORLD) && !defined(SN_TARGET_PSP2)
          if ((me -> stop_info.ext_suspend_cnt & 1) != 0) {
            GC_with_callee_saves_pushed(GC_suspend_self_blocked, (ptr_t)me);
          }
#       endif
        UNLOCK();
        return GC_SUCCESS;
    } else {
        UNLOCK();
        return GC_DUPLICATE;
    }
}

struct start_info {
    void *(*start_routine)(void *);
    void *arg;
    word flags;
    sem_t registered;           /* 1 ==> in our thread table, but       */
                                /* parent hasn't yet noticed.           */
};

/* Called from GC_inner_start_routine().  Defined in this file to       */
/* minimize the number of include files in pthread_start.c (because     */
/* sem_t and sem_post() are not used in that file directly).            */
GC_INNER_PTHRSTART GC_thread GC_start_rtn_prepare_thread(
                                        void *(**pstart)(void *),
                                        void **pstart_arg,
                                        struct GC_stack_base *sb, void *arg)
{
    struct start_info * si = (struct start_info *)arg;
    pthread_t self = pthread_self();
    GC_thread me;
    DCL_LOCK_STATE;

#   ifdef DEBUG_THREADS
      GC_log_printf("Starting thread %p, pid= %ld, sp= %p\n",
                    (void *)(word)self, (long)getpid(), (void *)&arg);
#   endif
    LOCK();
    me = GC_register_my_thread_inner(sb, self);
    me -> flags = si -> flags;
#   if defined(THREAD_LOCAL_ALLOC)
      GC_init_thread_local(&(me->tlfs));
#   endif
    UNLOCK();
    *pstart = si -> start_routine;
#   ifdef DEBUG_THREADS
      GC_log_printf("start_routine= %p\n", (void *)(signed_word)(*pstart));
#   endif
    *pstart_arg = si -> arg;
    sem_post(&(si -> registered));      /* Last action on si.   */
                                        /* OK to deallocate.    */
    return me;
}

#if !defined(SN_TARGET_ORBIS) && !defined(SN_TARGET_PSP2)
  STATIC void * GC_start_routine(void * arg)
  {
#   ifdef INCLUDE_LINUX_THREAD_DESCR
      struct GC_stack_base sb;

#     ifdef REDIRECT_MALLOC
        /* GC_get_stack_base may call pthread_getattr_np, which can     */
        /* unfortunately call realloc, which may allocate from an       */
        /* unregistered thread.  This is unpleasant, since it might     */
        /* force heap growth (or, even, heap overflow).                 */
        GC_disable();
#     endif
      if (GC_get_stack_base(&sb) != GC_SUCCESS)
        ABORT("Failed to get thread stack base");
#     ifdef REDIRECT_MALLOC
        GC_enable();
#     endif
      return GC_inner_start_routine(&sb, arg);
#   else
      return GC_call_with_stack_base(GC_inner_start_routine, arg);
#   endif
  }

  GC_API int WRAP_FUNC(pthread_create)(pthread_t *new_thread,
                       GC_PTHREAD_CREATE_CONST pthread_attr_t *attr,
                       void *(*start_routine)(void *), void *arg)
  {
    int result;
    int detachstate;
    word my_flags = 0;
    struct start_info si;
    DCL_LOCK_STATE;
        /* This is otherwise saved only in an area mmapped by the thread */
        /* library, which isn't visible to the collector.                */

    /* We resist the temptation to muck with the stack size here,       */
    /* even if the default is unreasonably small.  That's the client's  */
    /* responsibility.                                                  */

    INIT_REAL_SYMS();
    if (!EXPECT(parallel_initialized, TRUE))
      GC_init_parallel();
    GC_init_lib_bounds();
    if (sem_init(&si.registered, GC_SEM_INIT_PSHARED, 0) != 0)
      ABORT("sem_init failed");

    si.start_routine = start_routine;
    si.arg = arg;
    LOCK();
    if (!EXPECT(GC_thr_initialized, TRUE))
      GC_thr_init();
#   ifdef GC_ASSERTIONS
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
        /* On Solaris 10, with default attr initialization,     */
        /* stack_size remains 0.  Fudge it.                     */
        if (0 == stack_size) {
#           ifndef SOLARIS
              WARN("Failed to get stack size for assertion checking\n", 0);
#           endif
            stack_size = 1000000;
        }
        GC_ASSERT(stack_size >= 65536);
        /* Our threads may need to do some work for the GC.     */
        /* Ridiculously small threads won't work, and they      */
        /* probably wouldn't work anyway.                       */
      }
#   endif
    if (NULL == attr) {
        detachstate = PTHREAD_CREATE_JOINABLE;
    } else {
        if (pthread_attr_getdetachstate(attr, &detachstate) != 0)
            ABORT("pthread_attr_getdetachstate failed");
    }
    if (PTHREAD_CREATE_DETACHED == detachstate) my_flags |= DETACHED;
    si.flags = my_flags;
    UNLOCK();
#   ifdef DEBUG_THREADS
      GC_log_printf("About to start new thread from thread %p\n",
                    (void *)(word)pthread_self());
#   endif
#   ifdef PARALLEL_MARK
      if (EXPECT(!GC_parallel && available_markers_m1 > 0, FALSE))
        GC_start_mark_threads();
#   endif
    set_need_to_lock();
    result = REAL_FUNC(pthread_create)(new_thread, attr, GC_start_routine,
                                       &si);

    /* Wait until child has been added to the thread table.             */
    /* This also ensures that we hold onto the stack-allocated si until */
    /* the child is done with it.                                       */
    if (0 == result) {
        IF_CANCEL(int cancel_state;)

#       ifdef DEBUG_THREADS
            /* new_thread is non-NULL because pthread_create requires it. */
            GC_log_printf("Started thread %p\n", (void *)(word)(*new_thread));
#       endif
        DISABLE_CANCEL(cancel_state);
                /* pthread_create is not a cancellation point. */
        while (0 != sem_wait(&si.registered)) {
#           if defined(GC_HAIKU_THREADS)
              /* To workaround some bug in Haiku semaphores. */
              if (EACCES == errno) continue;
#           endif
            if (EINTR != errno) ABORT("sem_wait failed");
        }
        RESTORE_CANCEL(cancel_state);
    }
    sem_destroy(&si.registered);
    return(result);
  }
#endif /* !SN_TARGET_ORBIS && !SN_TARGET_PSP2 */

#if defined(USE_SPIN_LOCK) || !defined(NO_PTHREAD_TRYLOCK)
/* Spend a few cycles in a way that can't introduce contention with     */
/* other threads.                                                       */
#define GC_PAUSE_SPIN_CYCLES 10
STATIC void GC_pause(void)
{
    int i;

    for (i = 0; i < GC_PAUSE_SPIN_CYCLES; ++i) {
        /* Something that's unlikely to be optimized away. */
#     if defined(AO_HAVE_compiler_barrier) \
         && !defined(BASE_ATOMIC_OPS_EMULATED)
        AO_compiler_barrier();
#     else
        GC_noop1(i);
#     endif
    }
}
#endif

#ifndef SPIN_MAX
# define SPIN_MAX 128   /* Maximum number of calls to GC_pause before   */
                        /* give up.                                     */
#endif

GC_INNER volatile GC_bool GC_collecting = FALSE;
                        /* A hint that we're in the collector and       */
                        /* holding the allocation lock for an           */
                        /* extended period.                             */

#if (!defined(USE_SPIN_LOCK) && !defined(NO_PTHREAD_TRYLOCK)) \
        || defined(PARALLEL_MARK)
/* If we don't want to use the below spinlock implementation, either    */
/* because we don't have a GC_test_and_set implementation, or because   */
/* we don't want to risk sleeping, we can still try spinning on         */
/* pthread_mutex_trylock for a while.  This appears to be very          */
/* beneficial in many cases.                                            */
/* I suspect that under high contention this is nearly always better    */
/* than the spin lock.  But it's a bit slower on a uniprocessor.        */
/* Hence we still default to the spin lock.                             */
/* This is also used to acquire the mark lock for the parallel          */
/* marker.                                                              */

/* Here we use a strict exponential backoff scheme.  I don't know       */
/* whether that's better or worse than the above.  We eventually        */
/* yield by calling pthread_mutex_lock(); it never makes sense to       */
/* explicitly sleep.                                                    */

/* #define LOCK_STATS */
/* Note that LOCK_STATS requires AO_HAVE_test_and_set.  */
#ifdef LOCK_STATS
  volatile AO_t GC_spin_count = 0;
  volatile AO_t GC_block_count = 0;
  volatile AO_t GC_unlocked_count = 0;
#endif

STATIC void GC_generic_lock(pthread_mutex_t * lock)
{
#ifndef NO_PTHREAD_TRYLOCK
    unsigned pause_length = 1;
    unsigned i;

    if (0 == pthread_mutex_trylock(lock)) {
#       ifdef LOCK_STATS
            (void)AO_fetch_and_add1(&GC_unlocked_count);
#       endif
        return;
    }
    for (; pause_length <= SPIN_MAX; pause_length <<= 1) {
        for (i = 0; i < pause_length; ++i) {
            GC_pause();
        }
        switch(pthread_mutex_trylock(lock)) {
            case 0:
#               ifdef LOCK_STATS
                    (void)AO_fetch_and_add1(&GC_spin_count);
#               endif
                return;
            case EBUSY:
                break;
            default:
                ABORT("Unexpected error from pthread_mutex_trylock");
        }
    }
#endif /* !NO_PTHREAD_TRYLOCK */
#   ifdef LOCK_STATS
        (void)AO_fetch_and_add1(&GC_block_count);
#   endif
    pthread_mutex_lock(lock);
}

#endif /* !USE_SPIN_LOCK || ... */

#if defined(AO_HAVE_char_load) && !defined(BASE_ATOMIC_OPS_EMULATED)
# define is_collecting() \
                ((GC_bool)AO_char_load((unsigned char *)&GC_collecting))
#else
  /* GC_collecting is a hint, a potential data race between     */
  /* GC_lock() and ENTER/EXIT_GC() is OK to ignore.             */
# define is_collecting() GC_collecting
#endif

#if defined(USE_SPIN_LOCK)

/* Reasonably fast spin locks.  Basically the same implementation */
/* as STL alloc.h.  This isn't really the right way to do this.   */
/* but until the POSIX scheduling mess gets straightened out ...  */

# define low_spin_max 30 /* spin cycles if we suspect uniprocessor  */
# define high_spin_max SPIN_MAX /* spin cycles for multiprocessor   */

  static volatile AO_t spin_max = low_spin_max;
  static volatile AO_t last_spins = 0;
                                /* A potential data race between        */
                                /* threads invoking GC_lock which reads */
                                /* and updates spin_max and last_spins  */
                                /* could be ignored because these       */
                                /* variables are hints only.            */

GC_INNER void GC_lock(void)
{
    unsigned my_spin_max;
    unsigned my_last_spins;
    unsigned i;

    if (AO_test_and_set_acquire(&GC_allocate_lock) == AO_TS_CLEAR) {
        return;
    }
    my_spin_max = (unsigned)AO_load(&spin_max);
    my_last_spins = (unsigned)AO_load(&last_spins);
    for (i = 0; i < my_spin_max; i++) {
        if (is_collecting() || GC_nprocs == 1)
          goto yield;
        if (i < my_last_spins/2) {
            GC_pause();
            continue;
        }
        if (AO_test_and_set_acquire(&GC_allocate_lock) == AO_TS_CLEAR) {
            /*
             * got it!
             * Spinning worked.  Thus we're probably not being scheduled
             * against the other process with which we were contending.
             * Thus it makes sense to spin longer the next time.
             */
            AO_store(&last_spins, (AO_t)i);
            AO_store(&spin_max, (AO_t)high_spin_max);
            return;
        }
    }
    /* We are probably being scheduled against the other process.  Sleep. */
    AO_store(&spin_max, (AO_t)low_spin_max);
yield:
    for (i = 0;; ++i) {
        if (AO_test_and_set_acquire(&GC_allocate_lock) == AO_TS_CLEAR) {
            return;
        }
#       define SLEEP_THRESHOLD 12
                /* Under Linux very short sleeps tend to wait until     */
                /* the current time quantum expires.  On old Linux      */
                /* kernels nanosleep (<= 2 ms) just spins.              */
                /* (Under 2.4, this happens only for real-time          */
                /* processes.)  We want to minimize both behaviors      */
                /* here.                                                */
        if (i < SLEEP_THRESHOLD) {
            sched_yield();
        } else {
            struct timespec ts;

            if (i > 24) i = 24;
                        /* Don't wait for more than about 15 ms,        */
                        /* even under extreme contention.               */
            ts.tv_sec = 0;
            ts.tv_nsec = 1 << i;
            nanosleep(&ts, 0);
        }
    }
}

#elif defined(USE_PTHREAD_LOCKS)

# ifndef NO_PTHREAD_TRYLOCK
    GC_INNER void GC_lock(void)
    {
      if (1 == GC_nprocs || is_collecting()) {
        pthread_mutex_lock(&GC_allocate_ml);
      } else {
        GC_generic_lock(&GC_allocate_ml);
      }
    }
# elif defined(GC_ASSERTIONS)
    GC_INNER void GC_lock(void)
    {
      pthread_mutex_lock(&GC_allocate_ml);
    }
# endif

#endif /* !USE_SPIN_LOCK && USE_PTHREAD_LOCKS */

#ifdef PARALLEL_MARK

# ifdef GC_ASSERTIONS
#   define SET_MARK_LOCK_HOLDER \
                (void)(GC_mark_lock_holder = NUMERIC_THREAD_ID(pthread_self()))
#   define UNSET_MARK_LOCK_HOLDER \
                do { \
                  GC_ASSERT(GC_mark_lock_holder \
                                == NUMERIC_THREAD_ID(pthread_self())); \
                  GC_mark_lock_holder = NO_THREAD; \
                } while (0)
# else
#   define SET_MARK_LOCK_HOLDER (void)0
#   define UNSET_MARK_LOCK_HOLDER (void)0
# endif /* !GC_ASSERTIONS */

static pthread_cond_t builder_cv = PTHREAD_COND_INITIALIZER;

static void setup_mark_lock(void)
{
# ifdef GLIBC_2_19_TSX_BUG
    pthread_mutexattr_t mattr;
    int glibc_minor = -1;
    int glibc_major = GC_parse_version(&glibc_minor, gnu_get_libc_version());

    if (glibc_major > 2 || (glibc_major == 2 && glibc_minor >= 19)) {
      /* TODO: disable this workaround for glibc with fixed TSX */
      /* This disables lock elision to workaround a bug in glibc 2.19+  */
      if (0 != pthread_mutexattr_init(&mattr)) {
        ABORT("pthread_mutexattr_init failed");
      }
      if (0 != pthread_mutexattr_settype(&mattr, PTHREAD_MUTEX_NORMAL)) {
        ABORT("pthread_mutexattr_settype failed");
      }
      if (0 != pthread_mutex_init(&mark_mutex, &mattr)) {
        ABORT("pthread_mutex_init failed");
      }
      (void)pthread_mutexattr_destroy(&mattr);
    }
# endif
}

GC_INNER void GC_acquire_mark_lock(void)
{
#   if defined(NUMERIC_THREAD_ID_UNIQUE) && !defined(THREAD_SANITIZER)
      GC_ASSERT(GC_mark_lock_holder != NUMERIC_THREAD_ID(pthread_self()));
#   endif
    GC_generic_lock(&mark_mutex);
    SET_MARK_LOCK_HOLDER;
}

GC_INNER void GC_release_mark_lock(void)
{
    UNSET_MARK_LOCK_HOLDER;
    if (pthread_mutex_unlock(&mark_mutex) != 0) {
        ABORT("pthread_mutex_unlock failed");
    }
}

/* Collector must wait for a freelist builders for 2 reasons:           */
/* 1) Mark bits may still be getting examined without lock.             */
/* 2) Partial free lists referenced only by locals may not be scanned   */
/*    correctly, e.g. if they contain "pointer-free" objects, since the */
/*    free-list link may be ignored.                                    */
STATIC void GC_wait_builder(void)
{
    ASSERT_CANCEL_DISABLED();
    UNSET_MARK_LOCK_HOLDER;
    if (pthread_cond_wait(&builder_cv, &mark_mutex) != 0) {
        ABORT("pthread_cond_wait failed");
    }
    GC_ASSERT(GC_mark_lock_holder == NO_THREAD);
    SET_MARK_LOCK_HOLDER;
}

GC_INNER void GC_wait_for_reclaim(void)
{
    GC_acquire_mark_lock();
    while (GC_fl_builder_count > 0) {
        GC_wait_builder();
    }
    GC_release_mark_lock();
}

# if defined(CAN_HANDLE_FORK) && defined(THREAD_SANITIZER)
    /* Identical to GC_wait_for_reclaim() but with the no_sanitize      */
    /* attribute as a workaround for TSan which does not notice that    */
    /* the GC lock is acquired in fork_prepare_proc().                  */
    GC_ATTR_NO_SANITIZE_THREAD
    static void wait_for_reclaim_atfork(void)
    {
      GC_acquire_mark_lock();
      while (GC_fl_builder_count > 0)
        GC_wait_builder();
      GC_release_mark_lock();
    }
# endif /* CAN_HANDLE_FORK && THREAD_SANITIZER */

GC_INNER void GC_notify_all_builder(void)
{
    GC_ASSERT(GC_mark_lock_holder == NUMERIC_THREAD_ID(pthread_self()));
    if (pthread_cond_broadcast(&builder_cv) != 0) {
        ABORT("pthread_cond_broadcast failed");
    }
}

GC_INNER void GC_wait_marker(void)
{
    ASSERT_CANCEL_DISABLED();
    GC_ASSERT(GC_parallel);
    UNSET_MARK_LOCK_HOLDER;
    if (pthread_cond_wait(&mark_cv, &mark_mutex) != 0) {
        ABORT("pthread_cond_wait failed");
    }
    GC_ASSERT(GC_mark_lock_holder == NO_THREAD);
    SET_MARK_LOCK_HOLDER;
}

GC_INNER void GC_notify_all_marker(void)
{
    GC_ASSERT(GC_parallel);
    if (pthread_cond_broadcast(&mark_cv) != 0) {
        ABORT("pthread_cond_broadcast failed");
    }
}

#endif /* PARALLEL_MARK */

#ifdef PTHREAD_REGISTER_CANCEL_WEAK_STUBS
  /* Workaround "undefined reference" linkage errors on some targets. */
  EXTERN_C_BEGIN
  extern void __pthread_register_cancel(void) __attribute__((__weak__));
  extern void __pthread_unregister_cancel(void) __attribute__((__weak__));
  EXTERN_C_END

  void __pthread_register_cancel(void) {}
  void __pthread_unregister_cancel(void) {}
#endif

#endif /* GC_PTHREADS */
