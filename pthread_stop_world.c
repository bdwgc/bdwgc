/*
 * Copyright (c) 1994 by Xerox Corporation.  All rights reserved.
 * Copyright (c) 1996 by Silicon Graphics.  All rights reserved.
 * Copyright (c) 1998 by Fergus Henderson.  All rights reserved.
 * Copyright (c) 2000-2009 by Hewlett-Packard Development Company.
 * All rights reserved.
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

#ifdef PTHREAD_STOP_WORLD_IMPL

#  ifdef NACL
#    include <sys/time.h>
#  else
#    include <errno.h>
#    include <semaphore.h>
#    include <signal.h>
#    include <time.h>
#  endif /* !NACL */

#  ifdef E2K
#    include <alloca.h>
#  endif

GC_INLINE void
GC_usleep(unsigned us)
{
#  if defined(LINT2) || defined(THREAD_SANITIZER)
  /*
   * Workaround "waiting while holding a lock" static analyzer warning.
   * Workaround a rare hang in `usleep()` trying to acquire TSan Lock.
   */
  while (us-- > 0) {
    /* Sleep for a moment, pretending it takes 1us. */
    sched_yield();
  }
#  elif defined(CPPCHECK) /* `|| _POSIX_C_SOURCE >= 199309L` */
  struct timespec ts;

  ts.tv_sec = 0;
  ts.tv_nsec = (unsigned32)us * 1000;
  /* This requires `_POSIX_TIMERS` feature. */
  (void)nanosleep(&ts, NULL);
#  else
  usleep(us);
#  endif
}

#  ifdef NACL

STATIC int GC_nacl_num_gc_threads = 0;
STATIC volatile int GC_nacl_park_threads_now = 0;
STATIC volatile pthread_t GC_nacl_thread_parker = -1;

STATIC __thread int GC_nacl_thread_idx = -1;

/* TODO: Use `GC_get_tlfs()` instead. */
STATIC __thread GC_thread GC_nacl_gc_thread_self = NULL;

volatile int GC_nacl_thread_parked[MAX_NACL_GC_THREADS];
int GC_nacl_thread_used[MAX_NACL_GC_THREADS];

#  else /* !NACL */

#    if (!defined(AO_HAVE_load_acquire) || !defined(AO_HAVE_store_release)) \
        && !defined(CPPCHECK)
#      error AO_load_acquire and/or AO_store_release are missing;
#      error please define AO_REQUIRE_CAS manually
#    endif

#    ifdef DEBUG_THREADS
/* It is safe to call original `pthread_sigmask()` here. */
#      undef pthread_sigmask

#      ifndef NSIG
#        ifdef CPPCHECK
#          define NSIG 32
#        elif defined(MAXSIG)
#          define NSIG (MAXSIG + 1)
#        elif defined(_NSIG)
#          define NSIG _NSIG
#        elif defined(__SIGRTMAX)
#          define NSIG (__SIGRTMAX + 1)
#        else
#          error define NSIG
#        endif
#      endif

void
GC_print_sig_mask(void)
{
  sigset_t blocked;
  int i;

  if (pthread_sigmask(SIG_BLOCK, NULL, &blocked) != 0)
    ABORT("pthread_sigmask failed");
  for (i = 1; i < NSIG; i++) {
    if (sigismember(&blocked, i))
      GC_printf("Signal blocked: %d\n", i);
  }
}
#    endif /* DEBUG_THREADS */

/*
 * Remove the signals that we want to allow in thread stopping handler
 * from a set.
 */
STATIC void
GC_remove_allowed_signals(sigset_t *set)
{
  if (sigdelset(set, SIGINT) != 0 || sigdelset(set, SIGQUIT) != 0
      || sigdelset(set, SIGABRT) != 0 || sigdelset(set, SIGTERM) != 0) {
    ABORT("sigdelset failed");
  }

#    ifdef MPROTECT_VDB
  /*
   * The handlers write to the thread structure, which is in the heap,
   * and hence can trigger a protection fault.
   */
  if (sigdelset(set, SIGSEGV) != 0
#      ifdef HAVE_SIGBUS
      || sigdelset(set, SIGBUS) != 0
#      endif
  ) {
    ABORT("sigdelset failed");
  }
#    endif
}

static sigset_t suspend_handler_mask;

#    define THREAD_RESTARTED 0x1

/*
 * Incremented (to the nearest even value) at the beginning of
 * `GC_stop_world()` (or when a thread is requested to be suspended by
 * `GC_suspend_thread`) and once more (to an odd value) at the
 * beginning of `GC_start_world`.  The lowest bit is `THREAD_RESTARTED`
 * one which, if set, means it is safe for threads to restart, i.e.
 * they will see another suspend signal before they are expected to
 * stop (unless they have stopped voluntarily).
 */
STATIC volatile AO_t GC_stop_count;

STATIC GC_bool GC_retry_signals = FALSE;

/*
 * We use signals to stop threads during GC.
 *
 * Suspended threads wait in signal handler for `SIG_THR_RESTART`.
 * That is more portable than semaphores or condition variables.
 * (We do use `sem_post()` from a signal handler, but that should be
 * portable.)
 *
 * The thread suspension signal `SIG_SUSPEND` is now defined in `gc_priv.h`
 * file.  Note that we cannot just stop a thread; we need it to save its
 * stack pointers and acknowledge.
 */
#    ifndef SIG_THR_RESTART
#      if defined(SUSPEND_HANDLER_NO_CONTEXT) || defined(GC_REUSE_SIG_SUSPEND)
/* Reuse the suspend signal. */
#        define SIG_THR_RESTART SIG_SUSPEND
#      elif defined(HPUX) || defined(NETBSD) || defined(OSF1) \
          || defined(GC_USESIGRT_SIGNALS)
#        if defined(_SIGRTMIN) && !defined(CPPCHECK)
#          define SIG_THR_RESTART (_SIGRTMIN + 5)
#        else
#          define SIG_THR_RESTART (SIGRTMIN + 5)
#        endif
#      elif defined(FREEBSD) && defined(__GLIBC__)
#        define SIG_THR_RESTART (32 + 5)
#      elif defined(FREEBSD) || defined(HURD) || defined(RTEMS)
#        define SIG_THR_RESTART SIGUSR2
#      else
#        define SIG_THR_RESTART SIGXCPU
#      endif
#    endif /* !SIG_THR_RESTART */

#    define SIGNAL_UNSET (-1)

/*
 * Since `SIG_SUSPEND` and/or `SIG_THR_RESTART` could represent
 * a non-constant expression (e.g., in case of `SIGRTMIN`), actual
 * signal numbers are determined by `GC_stop_init()` unless manually
 * set (before the collector initialization).  Might be set to the
 * same signal number.
 */
STATIC int GC_sig_suspend = SIGNAL_UNSET;
STATIC int GC_sig_thr_restart = SIGNAL_UNSET;

GC_API void GC_CALL
GC_set_suspend_signal(int sig)
{
  if (GC_is_initialized)
    return;

  GC_sig_suspend = sig;
}

GC_API void GC_CALL
GC_set_thr_restart_signal(int sig)
{
  if (GC_is_initialized)
    return;

  GC_sig_thr_restart = sig;
}

GC_API int GC_CALL
GC_get_suspend_signal(void)
{
  return GC_sig_suspend != SIGNAL_UNSET ? GC_sig_suspend : SIG_SUSPEND;
}

GC_API int GC_CALL
GC_get_thr_restart_signal(void)
{
  return GC_sig_thr_restart != SIGNAL_UNSET ? GC_sig_thr_restart
                                            : SIG_THR_RESTART;
}

#    ifdef BASE_ATOMIC_OPS_EMULATED
/*
 * The AO primitives emulated with locks cannot be used inside signal
 * handlers as this could cause a deadlock or a double lock.
 * The following "async" macro definitions are correct only for
 * an uniprocessor case and are provided for a test purpose.
 */
#      define ao_load_acquire_async(p) (*(p))
#      define ao_load_async(p) ao_load_acquire_async(p)
#      define ao_store_release_async(p, v) (void)(*(p) = (v))
#      define ao_cptr_store_async(p, v) (void)(*(p) = (v))
#    else
#      define ao_load_acquire_async(p) AO_load_acquire(p)
#      define ao_load_async(p) AO_load(p)
#      define ao_store_release_async(p, v) AO_store_release(p, v)
#      define ao_cptr_store_async(p, v) GC_cptr_store(p, v)
#    endif /* !BASE_ATOMIC_OPS_EMULATED */

/* Note: this is also used to acknowledge restart. */
STATIC sem_t GC_suspend_ack_sem;

STATIC void GC_suspend_handler_inner(ptr_t dummy, void *context);

#    ifdef SUSPEND_HANDLER_NO_CONTEXT
STATIC void
GC_suspend_handler(int sig)
#    else
STATIC void
GC_suspend_sigaction(int sig, siginfo_t *info, void *context)
#    endif
{
  int old_errno = errno;

  if (sig != GC_sig_suspend) {
#    ifdef FREEBSD
    /* Workaround "deferred signal handling" bug in FreeBSD 9.2. */
    if (0 == sig)
      return;
#    endif
    ABORT("Bad signal in suspend_handler");
  }

#    ifdef SUSPEND_HANDLER_NO_CONTEXT
  /* A quick check if the signal is called to restart the world. */
  if ((ao_load_async(&GC_stop_count) & THREAD_RESTARTED) != 0)
    return;
  GC_with_callee_saves_pushed(GC_suspend_handler_inner, NULL);
#    else
  UNUSED_ARG(info);
  /*
   * We believe that in this case the full context is already in the
   * signal handler frame.
   */
  GC_suspend_handler_inner(NULL, context);
#    endif
  errno = old_errno;
}

/*
 * The lookup here is safe, since this is done on behalf of a thread which
 * holds the allocator lock in order to stop the world.  Thus concurrent
 * modification of the data structure is impossible.  Unfortunately, we
 * have to instruct TSan that the lookup is safe.
 */
#    ifdef THREAD_SANITIZER
/*
 * Almost same as `GC_self_thread_inner()` except for the `no_sanitize`
 * attribute added and the result is never `NULL`.
 */
GC_ATTR_NO_SANITIZE_THREAD
static GC_thread
GC_lookup_self_thread_async(void)
{
  thread_id_t self_id = thread_id_self();
  GC_thread p = GC_threads[THREAD_TABLE_INDEX(self_id)];

  for (;; p = p->tm.next) {
    if (THREAD_EQUAL(p->id, self_id))
      break;
  }
  return p;
}
#    else
#      define GC_lookup_self_thread_async() GC_self_thread_inner()
#    endif

GC_INLINE void
GC_store_stack_ptr(GC_stack_context_t crtn)
{
  /*
   * There is no data race between the suspend handler (storing
   * `stack_ptr`) and `GC_push_all_stacks` (fetching `stack_ptr`)
   * because `GC_push_all_stacks` is executed after `GC_stop_world`
   * exits and the latter runs `sem_wait()` repeatedly waiting for all
   * the suspended threads to call `sem_post()`.  Nonetheless,
   * `stack_ptr` is stored (here) and fetched (by `GC_push_all_stacks`)
   * using the atomic primitives to avoid the related TSan warning.
   */
#    ifdef SPARC
  ao_cptr_store_async(&crtn->stack_ptr, GC_save_regs_in_stack());
  /* TODO: Registers saving already done by `GC_with_callee_saves_pushed`. */
#    else
#      ifdef IA64
  crtn->backing_store_ptr = GC_save_regs_in_stack();
#      endif
  ao_cptr_store_async(&crtn->stack_ptr, GC_approx_sp());
#    endif
}

STATIC void
GC_suspend_handler_inner(ptr_t dummy, void *context)
{
  GC_thread me;
  GC_stack_context_t crtn;
#    ifdef E2K
  ptr_t bs_lo;
  size_t stack_size;
#    endif
  IF_CANCEL(int cancel_state;)
#    ifdef GC_ENABLE_SUSPEND_THREAD
  AO_t suspend_cnt;
#    endif
  AO_t my_stop_count = ao_load_acquire_async(&GC_stop_count);
  /*
   * After the barrier, this thread should see the actual content of
   * `GC_threads`.
   */

  UNUSED_ARG(dummy);
  UNUSED_ARG(context);
  if ((my_stop_count & THREAD_RESTARTED) != 0) {
    /* Restarting the world. */
    return;
  }

  /*
   * `pthread_setcancelstate()` is not defined to be async-signal-safe.
   * But the `glibc` version appears to be defined, in the absence of
   * asynchronous cancellation.  And since this signal handler to block
   * on `sigsuspend`, which is both async-signal-safe and
   * a cancellation point, there seems to be no obvious way out of it.
   * In fact, it looks like an async-signal-safe cancellation point
   * is inherently a problem, unless there is some way to disable
   * cancellation in the handler.
   */
  DISABLE_CANCEL(cancel_state);

#    ifdef DEBUG_THREADS
  GC_log_printf("Suspending %p\n", PTHREAD_TO_VPTR(pthread_self()));
#    endif
  me = GC_lookup_self_thread_async();
  if ((me->last_stop_count & ~(word)THREAD_RESTARTED) == my_stop_count) {
    /* Duplicate signal.  OK if we are retrying. */
    if (!GC_retry_signals) {
      WARN("Duplicate suspend signal in thread %p\n", pthread_self());
    }
    RESTORE_CANCEL(cancel_state);
    return;
  }
  crtn = me->crtn;
  GC_store_stack_ptr(crtn);
#    ifdef E2K
  GC_ASSERT(NULL == crtn->backing_store_end);
  GET_PROCEDURE_STACK_LOCAL(crtn->ps_ofs, &bs_lo, &stack_size);
  crtn->backing_store_end = bs_lo;
  crtn->backing_store_ptr = bs_lo + stack_size;
#    endif
#    ifdef GC_ENABLE_SUSPEND_THREAD
  suspend_cnt = ao_load_async(&me->ext_suspend_cnt);
#    endif

  /*
   * Tell the thread that wants to stop the world that this thread has
   * been stopped.  Note that `sem_post()` is the only async-signal-safe
   * primitive in LinuxThreads.
   */
  sem_post(&GC_suspend_ack_sem);
  ao_store_release_async(&me->last_stop_count, my_stop_count);

  /*
   * Wait until that thread tells us to restart by sending this thread
   * a `GC_sig_thr_restart` signal (should be masked at this point thus
   * there is no race).  We do not continue until we receive that signal,
   * but we do not take that as authoritative.  (We may be accidentally
   * restarted by one of the user signals we do not block.)
   * After we receive the signal, we use a primitive and an expensive
   * mechanism to wait until it is really safe to proceed.
   */
  do {
    sigsuspend(&suspend_handler_mask);
    /* Iterate while not restarting the world or thread is suspended. */
  } while (ao_load_acquire_async(&GC_stop_count) == my_stop_count
#    ifdef GC_ENABLE_SUSPEND_THREAD
           || ((suspend_cnt & 1) != 0
               && ao_load_async(&me->ext_suspend_cnt) == suspend_cnt)
#    endif
  );

#    ifdef DEBUG_THREADS
  GC_log_printf("Resuming %p\n", PTHREAD_TO_VPTR(pthread_self()));
#    endif
#    ifdef E2K
  GC_ASSERT(crtn->backing_store_end == bs_lo);
  crtn->backing_store_ptr = NULL;
  crtn->backing_store_end = NULL;
#    endif

#    ifndef GC_NETBSD_THREADS_WORKAROUND
  if (GC_retry_signals || GC_sig_suspend == GC_sig_thr_restart)
#    endif
  {
    /*
     * If the `SIG_THR_RESTART` signal loss is possible (though it should
     * be less likely than losing the `SIG_SUSPEND` signal as we do not
     * do much between the first `sem_post` and `sigsuspend` calls), more
     * handshaking is provided to work around it.
     */
    sem_post(&GC_suspend_ack_sem);
    /* Set the flag that the thread has been restarted. */
    if (GC_retry_signals)
      ao_store_release_async(&me->last_stop_count,
                             my_stop_count | THREAD_RESTARTED);
  }
  RESTORE_CANCEL(cancel_state);
}

static void
suspend_restart_barrier(int n_live_threads)
{
  int i;

  for (i = 0; i < n_live_threads; i++) {
    while (sem_wait(&GC_suspend_ack_sem) == -1) {
      /*
       * On Linux, `sem_wait()` is documented to always return zero.
       * But the documentation appears to be incorrect.
       * `EINTR` seems to happen with some versions of gdb.
       */
      if (errno != EINTR)
        ABORT("sem_wait failed");
    }
  }
#    ifdef GC_ASSERTIONS
  sem_getvalue(&GC_suspend_ack_sem, &i);
  GC_ASSERT(0 == i);
#    endif
}

#    define WAIT_UNIT 3000 /* us */

static int
resend_lost_signals(int n_live_threads, int (*suspend_restart_all)(void))
{
#    define RESEND_SIGNALS_LIMIT 150
#    define RETRY_INTERVAL 100000 /* us */

  if (n_live_threads > 0) {
    unsigned long wait_usecs = 0; /*< total wait since retry */
    int retry = 0;
    int prev_sent = 0;

    for (;;) {
      int ack_count;

      sem_getvalue(&GC_suspend_ack_sem, &ack_count);
      if (ack_count == n_live_threads)
        break;
      if (wait_usecs > RETRY_INTERVAL) {
        int newly_sent = suspend_restart_all();

        if (newly_sent != prev_sent) {
          /* Restart the counter. */
          retry = 0;
        } else if (++retry >= RESEND_SIGNALS_LIMIT) {
          /* No progress. */
          ABORT_ARG1("Signals delivery fails constantly", " at GC #%lu",
                     (unsigned long)GC_gc_no);
        }

        GC_COND_LOG_PRINTF("Resent %d signals after timeout, retry: %d\n",
                           newly_sent, retry);
        sem_getvalue(&GC_suspend_ack_sem, &ack_count);
        if (newly_sent < n_live_threads - ack_count) {
          WARN("Lost some threads while stopping or starting world?!\n", 0);
          n_live_threads = ack_count + newly_sent;
        }
        prev_sent = newly_sent;
        wait_usecs = 0;
      }
      GC_usleep(WAIT_UNIT);
      wait_usecs += WAIT_UNIT;
    }
  }
  return n_live_threads;
}

#    ifdef HAVE_CLOCK_GETTIME
#      define TS_NSEC_ADD(ts, ns)                                     \
        (ts.tv_nsec += (ns),                                          \
         (void)(ts.tv_nsec >= 1000000L * 1000                         \
                    ? (ts.tv_nsec -= 1000000L * 1000, ts.tv_sec++, 0) \
                    : 0))
#    endif

static void
resend_lost_signals_retry(int n_live_threads, int (*suspend_restart_all)(void))
{
#    if defined(HAVE_CLOCK_GETTIME) && !defined(DONT_TIMEDWAIT_ACK_SEM)
#      define TIMEOUT_BEFORE_RESEND 10000 /* us */
  struct timespec ts;

  if (n_live_threads > 0 && clock_gettime(CLOCK_REALTIME, &ts) == 0) {
    int i;

    TS_NSEC_ADD(ts, TIMEOUT_BEFORE_RESEND * (unsigned32)1000);
    /*
     * First, try to wait for the semaphore with some timeout.
     * On failure, fall back to `WAIT_UNIT` pause and resend of the signal.
     */
    for (i = 0; i < n_live_threads; i++) {
      if (sem_timedwait(&GC_suspend_ack_sem, &ts) == -1)
        break; /*< wait timed out or any other error */
    }
    /* Update the count of threads to wait the acknowledge from. */
    n_live_threads -= i;
  }
#    endif
  n_live_threads = resend_lost_signals(n_live_threads, suspend_restart_all);
  suspend_restart_barrier(n_live_threads);
}

STATIC void
GC_restart_handler(int sig)
{
#    ifdef DEBUG_THREADS
  /* Preserve `errno` value. */
  int old_errno = errno;
#    endif

  if (sig != GC_sig_thr_restart) {
    ABORT("Bad signal in restart handler");
  }

  /*
   * Note: even if we do not do anything useful here, it would still
   * be necessary to have a signal handler, rather than ignoring the
   * signals, otherwise the signals will not be delivered at all, and
   * will thus not interrupt the `sigsuspend()` above.
   */
#    ifdef DEBUG_THREADS
  GC_log_printf("In GC_restart_handler for %p\n",
                PTHREAD_TO_VPTR(pthread_self()));
  errno = old_errno;
#    endif
}

#    ifdef USE_TKILL_ON_ANDROID
EXTERN_C_BEGIN
extern int tkill(pid_t tid, int sig); /*< from platform `sys/linux-unistd.h` */
EXTERN_C_END
#      define THREAD_SYSTEM_ID(t) (t)->kernel_id
#    else
#      define THREAD_SYSTEM_ID(t) (t)->id
#    endif

#    ifndef RETRY_TKILL_EAGAIN_LIMIT
#      define RETRY_TKILL_EAGAIN_LIMIT 16
#    endif

static int
raise_signal(GC_thread p, int sig)
{
  int res;
#    ifdef RETRY_TKILL_ON_EAGAIN
  int retry;
#    endif
#    if defined(SIMULATE_LOST_SIGNALS) && !defined(GC_ENABLE_SUSPEND_THREAD)
#      ifndef LOST_SIGNALS_RATIO
#        define LOST_SIGNALS_RATIO 25
#      endif
  /* Note: race is OK, it is for test purpose only. */
  static int signal_cnt;

  if (GC_retry_signals && (++signal_cnt) % LOST_SIGNALS_RATIO == 0) {
    /* Simulate the signal is sent but lost. */
    return 0;
  }
#    endif
#    ifdef RETRY_TKILL_ON_EAGAIN
  for (retry = 0;; retry++)
#    endif
  {
#    ifdef USE_TKILL_ON_ANDROID
    int old_errno = errno;

    res = tkill(THREAD_SYSTEM_ID(p), sig);
    if (res < 0) {
      res = errno;
      errno = old_errno;
    }
#    else
    res = pthread_kill(THREAD_SYSTEM_ID(p), sig);
#    endif
#    ifdef RETRY_TKILL_ON_EAGAIN
    if (res != EAGAIN || retry >= RETRY_TKILL_EAGAIN_LIMIT)
      break;
    /* A temporal overflow of the real-time signal queue. */
    GC_usleep(WAIT_UNIT);
#    endif
  }
  return res;
}

#    ifdef GC_ENABLE_SUSPEND_THREAD
#      include <sys/time.h>

/* This is to get the prototypes as `extern "C"`. */
#      include "gc/javaxfc.h"

STATIC void
GC_brief_async_signal_safe_sleep(void)
{
  struct timeval tv;
  tv.tv_sec = 0;
#      if defined(GC_TIME_LIMIT) && !defined(CPPCHECK)
  tv.tv_usec = 1000 * GC_TIME_LIMIT / 2;
#      else
  tv.tv_usec = 1000 * 15 / 2;
#      endif
  (void)select(0, 0, 0, 0, &tv);
}

GC_INNER void
GC_suspend_self_inner(GC_thread me, size_t suspend_cnt)
{
  IF_CANCEL(int cancel_state;)

  GC_ASSERT((suspend_cnt & 1) != 0);
  DISABLE_CANCEL(cancel_state);
#      ifdef DEBUG_THREADS
  GC_log_printf("Suspend self: %p\n", THREAD_ID_TO_VPTR(me->id));
#      endif
  while (ao_load_acquire_async(&me->ext_suspend_cnt) == suspend_cnt) {
    /* TODO: Use `sigsuspend()` even for self-suspended threads. */
    GC_brief_async_signal_safe_sleep();
  }
#      ifdef DEBUG_THREADS
  GC_log_printf("Resume self: %p\n", THREAD_ID_TO_VPTR(me->id));
#      endif
  RESTORE_CANCEL(cancel_state);
}

GC_API void GC_CALL
GC_suspend_thread(GC_SUSPEND_THREAD_ID thread)
{
  GC_thread t;
  AO_t next_stop_count;
  AO_t suspend_cnt;
  IF_CANCEL(int cancel_state;)

  LOCK();
  t = GC_lookup_by_pthread((pthread_t)thread);
  if (NULL == t) {
    UNLOCK();
    return;
  }
  suspend_cnt = t->ext_suspend_cnt;
  if ((suspend_cnt & 1) != 0) /*< already suspended? */ {
    GC_ASSERT(!THREAD_EQUAL((pthread_t)thread, pthread_self()));
    UNLOCK();
    return;
  }
  if ((t->flags & (FINISHED | DO_BLOCKING)) != 0) {
    t->ext_suspend_cnt = suspend_cnt | 1; /*< suspend */
    /* Terminated but not joined yet, or in do-blocking state. */
    UNLOCK();
    return;
  }

  if (THREAD_EQUAL((pthread_t)thread, pthread_self())) {
    t->ext_suspend_cnt = suspend_cnt | 1;
    GC_with_callee_saves_pushed(GC_suspend_self_blocked, (ptr_t)t);
    UNLOCK();
    return;
  }

  /* `GC_suspend_thread()` is not a cancellation point. */
  DISABLE_CANCEL(cancel_state);
#      ifdef PARALLEL_MARK
  /*
   * Ensure we do not suspend a thread while it is rebuilding a free list,
   * otherwise such a deadlock is possible:
   *   - thread 1 is blocked in `GC_wait_for_reclaim` holding the allocator
   *     lock;
   *   - thread 2 is suspended in `GC_reclaim_generic` invoked from
   *     `GC_generic_malloc_many` (with `GC_fl_builder_count` greater than
   *     zero);
   *   - thread 3 is blocked acquiring the allocator lock in
   *     `GC_resume_thread`.
   */
  if (GC_parallel)
    GC_wait_for_reclaim();
#      endif

  if (GC_manual_vdb) {
    /* See the relevant comment in `GC_stop_world()`. */
    GC_acquire_dirty_lock();
  }
  /*
   * Else do not acquire the dirty lock as the write fault handler
   * might be trying to acquire it too, and the suspend handler
   * execution is deferred until the write fault handler completes.
   */

  next_stop_count = GC_stop_count + THREAD_RESTARTED;
  GC_ASSERT((next_stop_count & THREAD_RESTARTED) == 0);
  AO_store(&GC_stop_count, next_stop_count);

  /* Set the flag making the change visible to the signal handler. */
  AO_store_release(&t->ext_suspend_cnt, suspend_cnt | 1);

  /* TODO: Support `GC_retry_signals` (not needed for TSan). */
  switch (raise_signal(t, GC_sig_suspend)) {
  /* `ESRCH` cannot happen as terminated threads are handled above. */
  case 0:
    break;
  default:
    ABORT("pthread_kill failed");
  }

  /*
   * Wait for the thread to complete threads table lookup and
   * `stack_ptr` assignment.
   */
  GC_ASSERT(GC_thr_initialized);
  suspend_restart_barrier(1);
  if (GC_manual_vdb)
    GC_release_dirty_lock();
  AO_store(&GC_stop_count, next_stop_count | THREAD_RESTARTED);

  RESTORE_CANCEL(cancel_state);
  UNLOCK();
}

GC_API void GC_CALL
GC_resume_thread(GC_SUSPEND_THREAD_ID thread)
{
  GC_thread t;

  LOCK();
  t = GC_lookup_by_pthread((pthread_t)thread);
  if (t != NULL) {
    AO_t suspend_cnt = t->ext_suspend_cnt;

    if ((suspend_cnt & 1) != 0) { /*< is suspended? */
      GC_ASSERT((GC_stop_count & THREAD_RESTARTED) != 0);
      /* Mark the thread as not suspended - it will be resumed shortly. */
      AO_store(&t->ext_suspend_cnt, suspend_cnt + 1);

      if ((t->flags & (FINISHED | DO_BLOCKING)) == 0) {
        int result = raise_signal(t, GC_sig_thr_restart);

        /* TODO: Support signal resending on `GC_retry_signals`. */
        if (result != 0)
          ABORT_ARG1("pthread_kill failed in GC_resume_thread",
                     ": errcode= %d", result);
#      ifndef GC_NETBSD_THREADS_WORKAROUND
        if (GC_retry_signals || GC_sig_suspend == GC_sig_thr_restart)
#      endif
        {
          IF_CANCEL(int cancel_state;)

          DISABLE_CANCEL(cancel_state);
          suspend_restart_barrier(1);
          RESTORE_CANCEL(cancel_state);
        }
      }
    }
  }
  UNLOCK();
}

GC_API int GC_CALL
GC_is_thread_suspended(GC_SUSPEND_THREAD_ID thread)
{
  GC_thread t;
  int is_suspended = 0;

  READER_LOCK();
  t = GC_lookup_by_pthread((pthread_t)thread);
  if (t != NULL && (t->ext_suspend_cnt & 1) != 0)
    is_suspended = (int)TRUE;
  READER_UNLOCK();
  return is_suspended;
}
#    endif /* GC_ENABLE_SUSPEND_THREAD */

#    undef ao_cptr_store_async
#    undef ao_load_acquire_async
#    undef ao_load_async
#    undef ao_store_release_async
#  endif /* !NACL */

GC_INNER void
GC_push_all_stacks(void)
{
  GC_bool found_me = FALSE;
  size_t nthreads = 0;
  int i;
  GC_thread p;
  ptr_t lo; /*< stack top (`sp`) */
  ptr_t hi; /*< bottom */
#  if defined(E2K) || defined(IA64)
  /* We also need to scan the register backing store. */
  ptr_t bs_lo, bs_hi;
#  endif
  struct GC_traced_stack_sect_s *traced_stack_sect;
  pthread_t self = pthread_self();
  word total_size = 0;

  GC_ASSERT(I_HOLD_LOCK());
  GC_ASSERT(GC_thr_initialized);
#  ifdef DEBUG_THREADS
  GC_log_printf("Pushing stacks from thread %p\n", PTHREAD_TO_VPTR(self));
#  endif
  for (i = 0; i < THREAD_TABLE_SZ; i++) {
    for (p = GC_threads[i]; p != NULL; p = p->tm.next) {
#  if defined(E2K) || defined(IA64)
      GC_bool is_self = FALSE;
#  endif
      GC_stack_context_t crtn = p->crtn;

      GC_ASSERT(THREAD_TABLE_INDEX(p->id) == i);
      if (KNOWN_FINISHED(p))
        continue;
      ++nthreads;
      traced_stack_sect = crtn->traced_stack_sect;
      if (THREAD_EQUAL(p->id, self)) {
        GC_ASSERT((p->flags & DO_BLOCKING) == 0);
#  ifdef SPARC
        lo = GC_save_regs_in_stack();
#  else
        lo = GC_approx_sp();
#    ifdef IA64
        bs_hi = GC_save_regs_in_stack();
#    elif defined(E2K)
        {
          size_t stack_size;

          GC_ASSERT(NULL == crtn->backing_store_end);
          GET_PROCEDURE_STACK_LOCAL(crtn->ps_ofs, &bs_lo, &stack_size);
          bs_hi = bs_lo + stack_size;
        }
#    endif
#  endif
        found_me = TRUE;
#  if defined(E2K) || defined(IA64)
        is_self = TRUE;
#  endif
      } else {
        lo = GC_cptr_load(&crtn->stack_ptr);
#  ifdef IA64
        bs_hi = crtn->backing_store_ptr;
#  elif defined(E2K)
        bs_lo = crtn->backing_store_end;
        bs_hi = crtn->backing_store_ptr;
#  endif
        if (traced_stack_sect != NULL
            && traced_stack_sect->saved_stack_ptr == lo) {
          /*
           * If the thread has never been stopped since the recent
           * `GC_call_with_gc_active` invocation, then skip the top
           * "stack section" as `stack_ptr` already points to.
           */
          traced_stack_sect = traced_stack_sect->prev;
        }
      }
      hi = crtn->stack_end;
#  ifdef IA64
      bs_lo = crtn->backing_store_end;
#  endif
#  ifdef DEBUG_THREADS
#    ifdef STACK_GROWS_UP
      GC_log_printf("Stack for thread %p is (%p,%p]\n",
                    THREAD_ID_TO_VPTR(p->id), (void *)hi, (void *)lo);
#    else
      GC_log_printf("Stack for thread %p is [%p,%p)\n",
                    THREAD_ID_TO_VPTR(p->id), (void *)lo, (void *)hi);
#    endif
#  endif
      if (NULL == lo)
        ABORT("GC_push_all_stacks: sp not set!");
      if (crtn->altstack != NULL && ADDR_GE(lo, crtn->altstack)
          && ADDR_GE(crtn->altstack + crtn->altstack_size, lo)) {
#  ifdef STACK_GROWS_UP
        hi = crtn->altstack;
#  else
        hi = crtn->altstack + crtn->altstack_size;
#  endif
        /* FIXME: Need to scan the normal stack too, but how? */
      }
#  ifdef STACKPTR_CORRECTOR_AVAILABLE
      if (GC_sp_corrector != 0)
        GC_sp_corrector((void **)&lo, THREAD_ID_TO_VPTR(p->id));
#  endif
      GC_push_all_stack_sections(lo, hi, traced_stack_sect);
#  ifdef STACK_GROWS_UP
      total_size += lo - hi;
#  else
      total_size += hi - lo; /*< `lo` is not greater than `hi` */
#  endif
#  ifdef NACL
      /* Push `reg_storage` as roots, this will cover the reg context. */
      GC_push_all_stack((ptr_t)p->reg_storage,
                        (ptr_t)(p->reg_storage + NACL_GC_REG_STORAGE_SIZE));
      total_size += NACL_GC_REG_STORAGE_SIZE * sizeof(ptr_t);
#  endif
#  ifdef E2K
      if ((GC_stop_count & THREAD_RESTARTED) != 0
#    ifdef GC_ENABLE_SUSPEND_THREAD
          && (p->ext_suspend_cnt & 1) == 0
#    endif
          && !is_self && (p->flags & DO_BLOCKING) == 0) {
        /* Procedure stack buffer has already been freed. */
        continue;
      }
#  endif
#  if defined(E2K) || defined(IA64)
#    ifdef DEBUG_THREADS
      GC_log_printf("Reg stack for thread %p is [%p,%p)\n",
                    THREAD_ID_TO_VPTR(p->id), (void *)bs_lo, (void *)bs_hi);
#    endif
      GC_ASSERT(bs_lo != NULL && bs_hi != NULL);
      /*
       * FIXME: This (if `is_self`) may add an unbounded number of entries,
       * and hence overflow the mark stack, which is bad.
       */
#    ifdef IA64
      GC_push_all_register_sections(bs_lo, bs_hi, is_self, traced_stack_sect);
#    else
      if (is_self) {
        GC_push_all_eager(bs_lo, bs_hi);
      } else {
        GC_push_all_stack(bs_lo, bs_hi);
      }
#    endif
      total_size += bs_hi - bs_lo; /*< `bs_lo` is not greater than `bs_hi` */
#  endif
    }
  }
  GC_VERBOSE_LOG_PRINTF("Pushed %d thread stacks\n", (int)nthreads);
  if (!found_me && !GC_in_thread_creation)
    ABORT("Collecting from unknown thread");
  GC_total_stacksize = total_size;
}

#  ifdef DEBUG_THREADS
/*
 * There seems to be a very rare thread stopping problem.
 * To help us debug that, we save the ids of the stopping thread.
 */
pthread_t GC_stopping_thread;
int GC_stopping_pid = 0;
#  endif

/*
 * Suspend all threads that might still be running.  Return the number
 * of suspend signals that were sent.
 */
STATIC int
GC_suspend_all(void)
{
  int n_live_threads = 0;
  int i;
#  ifndef NACL
  GC_thread p;
  pthread_t self = pthread_self();
  int result;

  GC_ASSERT((GC_stop_count & THREAD_RESTARTED) == 0);
  GC_ASSERT(I_HOLD_LOCK());
  for (i = 0; i < THREAD_TABLE_SZ; i++) {
    for (p = GC_threads[i]; p != NULL; p = p->tm.next) {
      if (!THREAD_EQUAL(p->id, self)) {
        if ((p->flags & (FINISHED | DO_BLOCKING)) != 0)
          continue;
#    ifdef GC_ENABLE_SUSPEND_THREAD
        if ((p->ext_suspend_cnt & 1) != 0)
          continue;
#    endif
        if (AO_load(&p->last_stop_count) == GC_stop_count) {
          /* Matters only if `GC_retry_signals`. */
          continue;
        }
        n_live_threads++;
#    ifdef DEBUG_THREADS
        GC_log_printf("Sending suspend signal to %p\n",
                      THREAD_ID_TO_VPTR(p->id));
#    endif

        /*
         * The synchronization between `GC_dirty` (based on test-and-set)
         * and the signal-based thread suspension is performed in
         * `GC_stop_world()` because `GC_release_dirty_lock()` cannot be
         * called before acknowledging that the thread is really suspended.
         */
        result = raise_signal(p, GC_sig_suspend);
        switch (result) {
        case ESRCH:
          /* Not really there anymore.  Possible? */
          n_live_threads--;
          break;
        case 0:
          if (GC_on_thread_event) {
            /* Note: thread id might be truncated. */
            GC_on_thread_event(GC_EVENT_THREAD_SUSPENDED,
                               THREAD_ID_TO_VPTR(THREAD_SYSTEM_ID(p)));
          }
          break;
        default:
          ABORT_ARG1("pthread_kill failed at suspend", ": errcode= %d",
                     result);
        }
      }
    }
  }

#  else /* NACL */
#    ifndef NACL_PARK_WAIT_USEC
#      define NACL_PARK_WAIT_USEC 100 /* us */
#    endif
  unsigned long num_sleeps = 0;

  GC_ASSERT(I_HOLD_LOCK());
#    ifdef DEBUG_THREADS
  GC_log_printf("pthread_stop_world: number of threads: %d\n",
                GC_nacl_num_gc_threads - 1);
#    endif
  GC_nacl_thread_parker = pthread_self();
  GC_nacl_park_threads_now = 1;

  if (GC_manual_vdb)
    GC_acquire_dirty_lock();
  for (;;) {
    int num_threads_parked = 0;
    int num_used = 0;

    /* Check the "parked" flag for each thread the collector knows about. */
    for (i = 0; i < MAX_NACL_GC_THREADS && num_used < GC_nacl_num_gc_threads;
         i++) {
      if (GC_nacl_thread_used[i] == 1) {
        num_used++;
        if (GC_nacl_thread_parked[i] == 1) {
          num_threads_parked++;
          if (GC_on_thread_event)
            GC_on_thread_event(GC_EVENT_THREAD_SUSPENDED, NUMERIC_TO_VPTR(i));
        }
      }
    }
    /* Note: -1 for the current thread. */
    if (num_threads_parked >= GC_nacl_num_gc_threads - 1)
      break;
#    ifdef DEBUG_THREADS
    GC_log_printf("Sleep waiting for %d threads to park...\n",
                  GC_nacl_num_gc_threads - num_threads_parked - 1);
#    endif
    GC_usleep(NACL_PARK_WAIT_USEC);
    if (++num_sleeps > (1000 * 1000) / NACL_PARK_WAIT_USEC) {
      WARN("GC appears stalled waiting for %" WARN_PRIdPTR
           " threads to park...\n",
           GC_nacl_num_gc_threads - num_threads_parked - 1);
      num_sleeps = 0;
    }
  }
  if (GC_manual_vdb)
    GC_release_dirty_lock();
#  endif /* NACL */
  return n_live_threads;
}

GC_INNER void
GC_stop_world(void)
{
#  ifndef NACL
  int n_live_threads;
#  endif
  GC_ASSERT(I_HOLD_LOCK());
  /*
   * Make sure all free list construction has stopped before we start.
   * No new construction can start, since it is required to acquire and
   * release the allocator lock before start.
   */

  GC_ASSERT(GC_thr_initialized);
#  ifdef DEBUG_THREADS
  GC_stopping_thread = pthread_self();
  GC_stopping_pid = getpid();
  GC_log_printf("Stopping the world from %p\n",
                PTHREAD_TO_VPTR(GC_stopping_thread));
#  endif
#  ifdef PARALLEL_MARK
  if (GC_parallel) {
    GC_acquire_mark_lock();
    GC_ASSERT(GC_fl_builder_count == 0);
    /* We should have previously waited for it to become zero. */
  }
#  endif /* PARALLEL_MARK */

#  ifdef NACL
  (void)GC_suspend_all();
#  else
  /* Note: only concurrent reads are possible. */
  AO_store(&GC_stop_count, GC_stop_count + THREAD_RESTARTED);
  if (GC_manual_vdb) {
    GC_acquire_dirty_lock();
    /*
     * The write fault handler cannot be called if `GC_manual_vdb` (thus
     * double-locking should not occur in `async_set_pht_entry_from_index`
     * based on test-and-set).
     */
  }
  n_live_threads = GC_suspend_all();
  if (GC_retry_signals) {
    resend_lost_signals_retry(n_live_threads, GC_suspend_all);
  } else {
    suspend_restart_barrier(n_live_threads);
  }
  if (GC_manual_vdb) {
    /* Note: cannot be done in `GC_suspend_all`. */
    GC_release_dirty_lock();
  }
#  endif

#  ifdef PARALLEL_MARK
  if (GC_parallel)
    GC_release_mark_lock();
#  endif
#  ifdef DEBUG_THREADS
  GC_log_printf("World stopped from %p\n", PTHREAD_TO_VPTR(pthread_self()));
  GC_stopping_thread = 0;
#  endif
}

#  ifdef NACL
#    if defined(__x86_64__)
#      define NACL_STORE_REGS()                                 \
        do {                                                    \
          __asm__ __volatile__("push %rbx");                    \
          __asm__ __volatile__("push %rbp");                    \
          __asm__ __volatile__("push %r12");                    \
          __asm__ __volatile__("push %r13");                    \
          __asm__ __volatile__("push %r14");                    \
          __asm__ __volatile__("push %r15");                    \
          __asm__ __volatile__(                                 \
              "mov %%esp, %0"                                   \
              : "=m"(GC_nacl_gc_thread_self->crtn->stack_ptr)); \
          BCOPY(GC_nacl_gc_thread_self->crtn->stack_ptr,        \
                GC_nacl_gc_thread_self->reg_storage,            \
                NACL_GC_REG_STORAGE_SIZE * sizeof(ptr_t));      \
          __asm__ __volatile__("naclasp $48, %r15");            \
        } while (0)
#    elif defined(__i386__)
#      define NACL_STORE_REGS()                                 \
        do {                                                    \
          __asm__ __volatile__("push %ebx");                    \
          __asm__ __volatile__("push %ebp");                    \
          __asm__ __volatile__("push %esi");                    \
          __asm__ __volatile__("push %edi");                    \
          __asm__ __volatile__(                                 \
              "mov %%esp, %0"                                   \
              : "=m"(GC_nacl_gc_thread_self->crtn->stack_ptr)); \
          BCOPY(GC_nacl_gc_thread_self->crtn->stack_ptr,        \
                GC_nacl_gc_thread_self->reg_storage,            \
                NACL_GC_REG_STORAGE_SIZE * sizeof(ptr_t));      \
          __asm__ __volatile__("add $16, %esp");                \
        } while (0)
#    elif defined(__arm__)
#      define NACL_STORE_REGS()                                 \
        do {                                                    \
          __asm__ __volatile__("push {r4-r8,r10-r12,lr}");      \
          __asm__ __volatile__(                                 \
              "mov r0, %0"                                      \
              :                                                 \
              : "r"(&GC_nacl_gc_thread_self->crtn->stack_ptr)); \
          __asm__ __volatile__("bic r0, r0, #0xc0000000");      \
          __asm__ __volatile__("str sp, [r0]");                 \
          BCOPY(GC_nacl_gc_thread_self->crtn->stack_ptr,        \
                GC_nacl_gc_thread_self->reg_storage,            \
                NACL_GC_REG_STORAGE_SIZE * sizeof(ptr_t));      \
          __asm__ __volatile__("add sp, sp, #40");              \
          __asm__ __volatile__("bic sp, sp, #0xc0000000");      \
        } while (0)
#    else
#      error TODO Please port NACL_STORE_REGS
#    endif

GC_API_OSCALL void
nacl_pre_syscall_hook(void)
{
  if (GC_nacl_thread_idx != -1) {
    NACL_STORE_REGS();
    GC_nacl_gc_thread_self->crtn->stack_ptr = GC_approx_sp();
    GC_nacl_thread_parked[GC_nacl_thread_idx] = 1;
  }
}

GC_API_OSCALL void
__nacl_suspend_thread_if_needed(void)
{
  if (!GC_nacl_park_threads_now)
    return;

  /* Do not try to park the thread parker. */
  if (GC_nacl_thread_parker == pthread_self())
    return;

  /*
   * This can happen when a thread is created outside of the collector
   * system (`wthread` mostly).
   */
  if (GC_nacl_thread_idx < 0)
    return;

  /*
   * If it was already "parked", we are returning from a `syscall`,
   * so do not bother storing registers again, the collector has a set of.
   */
  if (!GC_nacl_thread_parked[GC_nacl_thread_idx]) {
    NACL_STORE_REGS();
    GC_nacl_gc_thread_self->crtn->stack_ptr = GC_approx_sp();
  }
  GC_nacl_thread_parked[GC_nacl_thread_idx] = 1;
  while (GC_nacl_park_threads_now) {
    /* Just spin. */
  }
  GC_nacl_thread_parked[GC_nacl_thread_idx] = 0;

  /* Clear out the `reg_storage` for the next suspend. */
  BZERO(GC_nacl_gc_thread_self->reg_storage,
        NACL_GC_REG_STORAGE_SIZE * sizeof(ptr_t));
}

GC_API_OSCALL void
nacl_post_syscall_hook(void)
{
  /*
   * Calling `__nacl_suspend_thread_if_needed` right away should guarantee
   * we do not mutate the collector set.
   */
  __nacl_suspend_thread_if_needed();
  if (GC_nacl_thread_idx != -1) {
    GC_nacl_thread_parked[GC_nacl_thread_idx] = 0;
  }
}

STATIC GC_bool GC_nacl_thread_parking_inited = FALSE;
STATIC pthread_mutex_t GC_nacl_thread_alloc_lock = PTHREAD_MUTEX_INITIALIZER;

struct nacl_irt_blockhook {
  int (*register_block_hooks)(void (*pre)(void), void (*post)(void));
};

EXTERN_C_BEGIN
extern size_t nacl_interface_query(const char *interface_ident, void *table,
                                   size_t tablesize);
EXTERN_C_END

GC_INNER void
GC_nacl_initialize_gc_thread(GC_thread me)
{
  int i;
  static struct nacl_irt_blockhook gc_hook;

  GC_ASSERT(NULL == GC_nacl_gc_thread_self);
  GC_nacl_gc_thread_self = me;
  pthread_mutex_lock(&GC_nacl_thread_alloc_lock);
  if (!EXPECT(GC_nacl_thread_parking_inited, TRUE)) {
    BZERO(GC_nacl_thread_parked, sizeof(GC_nacl_thread_parked));
    BZERO(GC_nacl_thread_used, sizeof(GC_nacl_thread_used));
    /*
     * TODO: Replace with the public "register hook" function when
     * available from `glibc`.
     */
    nacl_interface_query("nacl-irt-blockhook-0.1", &gc_hook, sizeof(gc_hook));
    gc_hook.register_block_hooks(nacl_pre_syscall_hook,
                                 nacl_post_syscall_hook);
    GC_nacl_thread_parking_inited = TRUE;
  }
  GC_ASSERT(GC_nacl_num_gc_threads <= MAX_NACL_GC_THREADS);
  for (i = 0; i < MAX_NACL_GC_THREADS; i++) {
    if (GC_nacl_thread_used[i] == 0) {
      GC_nacl_thread_used[i] = 1;
      GC_nacl_thread_idx = i;
      GC_nacl_num_gc_threads++;
      break;
    }
  }
  pthread_mutex_unlock(&GC_nacl_thread_alloc_lock);
}

GC_INNER void
GC_nacl_shutdown_gc_thread(void)
{
  GC_ASSERT(GC_nacl_gc_thread_self != NULL);
  pthread_mutex_lock(&GC_nacl_thread_alloc_lock);
  GC_ASSERT(GC_nacl_thread_idx >= 0);
  GC_ASSERT(GC_nacl_thread_idx < MAX_NACL_GC_THREADS);
  GC_ASSERT(GC_nacl_thread_used[GC_nacl_thread_idx] != 0);
  GC_nacl_thread_used[GC_nacl_thread_idx] = 0;
  GC_nacl_thread_idx = -1;
  GC_nacl_num_gc_threads--;
  pthread_mutex_unlock(&GC_nacl_thread_alloc_lock);
  GC_nacl_gc_thread_self = NULL;
}

#  else /* !NACL */

static GC_bool in_resend_restart_signals;

/*
 * Restart all threads that were suspended by the collector.
 * Return the number of restart signals that were sent.
 */
STATIC int
GC_restart_all(void)
{
  int n_live_threads = 0;
  int i;
  pthread_t self = pthread_self();
  GC_thread p;
  int result;

  GC_ASSERT(I_HOLD_LOCK());
  GC_ASSERT((GC_stop_count & THREAD_RESTARTED) != 0);
  for (i = 0; i < THREAD_TABLE_SZ; i++) {
    for (p = GC_threads[i]; p != NULL; p = p->tm.next) {
      if (!THREAD_EQUAL(p->id, self)) {
        if ((p->flags & (FINISHED | DO_BLOCKING)) != 0)
          continue;
#    ifdef GC_ENABLE_SUSPEND_THREAD
        if ((p->ext_suspend_cnt & 1) != 0)
          continue;
#    endif
        if (GC_retry_signals
            && AO_load(&p->last_stop_count) == GC_stop_count) {
          /* The thread has been restarted. */
          if (!in_resend_restart_signals) {
            /*
             * Some user signal (which we do not block, e.g. `SIGQUIT`)
             * has already restarted the thread, but nonetheless we need to
             * count the latter in `n_live_threads`, so that to decrement
             * the semaphore's value proper amount of times.  (We are also
             * sending the restart signal to the thread, it is not needed
             * actually but does not hurt.)
             */
          } else {
            continue;
            /*
             * FIXME: Still, an extremely low chance exists that the user
             * signal restarts the thread after the restart signal has been
             * lost (causing `sem_timedwait()` to fail) while retrying,
             * causing finally a mismatch between `GC_suspend_ack_sem` and
             * `n_live_threads`.
             */
          }
        }
        n_live_threads++;
#    ifdef DEBUG_THREADS
        GC_log_printf("Sending restart signal to %p\n",
                      THREAD_ID_TO_VPTR(p->id));
#    endif
        result = raise_signal(p, GC_sig_thr_restart);
        switch (result) {
        case ESRCH:
          /* Not really there anymore.  Possible? */
          n_live_threads--;
          break;
        case 0:
          if (GC_on_thread_event)
            GC_on_thread_event(GC_EVENT_THREAD_UNSUSPENDED,
                               THREAD_ID_TO_VPTR(THREAD_SYSTEM_ID(p)));
          break;
        default:
          ABORT_ARG1("pthread_kill failed at resume", ": errcode= %d", result);
        }
      }
    }
  }
  return n_live_threads;
}
#  endif /* !NACL */

GC_INNER void
GC_start_world(void)
{
#  ifndef NACL
  int n_live_threads;

  /* The allocator lock is held continuously since the world stopped. */
  GC_ASSERT(I_HOLD_LOCK());

#    ifdef DEBUG_THREADS
  GC_log_printf("World starting\n");
#    endif
  /*
   * Note: the updated value should now be visible to the signal handler
   * (note that `pthread_kill` is not on the list of functions that
   * synchronize memory).
   */
  AO_store_release(&GC_stop_count, GC_stop_count + THREAD_RESTARTED);

  GC_ASSERT(!in_resend_restart_signals);
  n_live_threads = GC_restart_all();
  if (GC_retry_signals) {
    in_resend_restart_signals = TRUE;
    resend_lost_signals_retry(n_live_threads, GC_restart_all);
    in_resend_restart_signals = FALSE;
  } else {
#    ifndef GC_NETBSD_THREADS_WORKAROUND
    if (GC_sig_suspend == GC_sig_thr_restart)
#    endif
    {
      suspend_restart_barrier(n_live_threads);
    }
  }
#    ifdef DEBUG_THREADS
  GC_log_printf("World started\n");
#    endif
#  else /* NACL */
#    ifdef DEBUG_THREADS
  GC_log_printf("World starting...\n");
#    endif
  GC_nacl_park_threads_now = 0;
  if (GC_on_thread_event) {
    GC_on_thread_event(GC_EVENT_THREAD_UNSUSPENDED, NULL);
    /* TODO: Send event for every unsuspended thread. */
  }
#  endif
}

GC_INNER void
GC_stop_init(void)
{
#  ifndef NACL
  struct sigaction act;
  char *str;

  if (SIGNAL_UNSET == GC_sig_suspend)
    GC_sig_suspend = SIG_SUSPEND;
  if (SIGNAL_UNSET == GC_sig_thr_restart)
    GC_sig_thr_restart = SIG_THR_RESTART;

  if (sem_init(&GC_suspend_ack_sem, GC_SEM_INIT_PSHARED, 0) == -1)
    ABORT("sem_init failed");
  GC_stop_count = THREAD_RESTARTED; /*< i.e. the world is not stopped */

  if (sigfillset(&act.sa_mask) != 0) {
    ABORT("sigfillset failed");
  }
#    ifdef RTEMS
  if (sigprocmask(SIG_UNBLOCK, &act.sa_mask, NULL) != 0) {
    ABORT("sigprocmask failed");
  }
#    endif
  GC_remove_allowed_signals(&act.sa_mask);
  /*
   * `GC_sig_thr_restart` is set in the resulting mask.
   * It is unmasked by the handler when necessary.
   */

#    ifdef SA_RESTART
  act.sa_flags = SA_RESTART;
#    else
  act.sa_flags = 0;
#    endif
#    ifdef SUSPEND_HANDLER_NO_CONTEXT
  act.sa_handler = GC_suspend_handler;
#    else
  act.sa_flags |= SA_SIGINFO;
  act.sa_sigaction = GC_suspend_sigaction;
#    endif
  /* `act.sa_restorer` is deprecated and should not be initialized. */
  if (sigaction(GC_sig_suspend, &act, NULL) != 0) {
    ABORT("Cannot set SIG_SUSPEND handler");
  }

  if (GC_sig_suspend != GC_sig_thr_restart) {
#    ifndef SUSPEND_HANDLER_NO_CONTEXT
    act.sa_flags &= ~SA_SIGINFO;
#    endif
    act.sa_handler = GC_restart_handler;
    if (sigaction(GC_sig_thr_restart, &act, NULL) != 0)
      ABORT("Cannot set SIG_THR_RESTART handler");
  } else {
    GC_COND_LOG_PRINTF("Using same signal for suspend and restart\n");
  }

  /* Initialize `suspend_handler_mask` (excluding `GC_sig_thr_restart`). */
  if (sigfillset(&suspend_handler_mask) != 0)
    ABORT("sigfillset failed");
  GC_remove_allowed_signals(&suspend_handler_mask);
  if (sigdelset(&suspend_handler_mask, GC_sig_thr_restart) != 0)
    ABORT("sigdelset failed");

#    ifndef NO_RETRY_SIGNALS
  /*
   * Any platform could lose signals, so let's be conservative and
   * always enable signals retry logic.
   */
  GC_retry_signals = TRUE;
#    endif
  /* Override the default value of `GC_retry_signals`. */
  str = GETENV("GC_RETRY_SIGNALS");
  if (str != NULL) {
    /* Do not retry if the environment variable is set to "0". */
    GC_retry_signals = *str != '0' || *(str + 1) != '\0';
  }
  if (GC_retry_signals) {
    GC_COND_LOG_PRINTF(
        "Will retry suspend and restart signals if necessary\n");
  }
#    ifndef NO_SIGNALS_UNBLOCK_IN_MAIN
  /* Explicitly unblock the signals once before new threads creation. */
  GC_unblock_gc_signals();
#    endif
#  endif /* !NACL */
}

#endif /* PTHREAD_STOP_WORLD_IMPL */
