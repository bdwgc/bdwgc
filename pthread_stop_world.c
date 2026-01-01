/*
 * Copyright (c) 1994 by Xerox Corporation.  All rights reserved.
 * Copyright (c) 1996 by Silicon Graphics.  All rights reserved.
 * Copyright (c) 1998 by Fergus Henderson.  All rights reserved.
 * Copyright (c) 2000-2009 by Hewlett-Packard Development Company.
 * All rights reserved.
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

#if defined(GC_PTHREADS) && !defined(GC_WIN32_THREADS) && \
    !defined(GC_DARWIN_THREADS)

#ifdef NACL

#include <unistd.h>
#include <sys/time.h>

STATIC int GC_nacl_num_gc_threads = 0;
STATIC __thread int GC_nacl_thread_idx = -1;
STATIC int GC_nacl_park_threads_now = 0;
STATIC pthread_t GC_nacl_thread_parker = -1;

GC_INNER __thread GC_thread GC_nacl_gc_thread_self = NULL;

int GC_nacl_thread_parked[MAX_NACL_GC_THREADS];
int GC_nacl_thread_used[MAX_NACL_GC_THREADS];

#elif !defined(GC_OPENBSD_THREADS)

#include <signal.h>
#include <semaphore.h>
#include <errno.h>
#include <unistd.h>
#include "atomic_ops.h"

/* It's safe to call original pthread_sigmask() here.   */
#undef pthread_sigmask

#ifdef DEBUG_THREADS
# ifndef NSIG
#   if defined(MAXSIG)
#     define NSIG (MAXSIG+1)
#   elif defined(_NSIG)
#     define NSIG _NSIG
#   elif defined(__SIGRTMAX)
#     define NSIG (__SIGRTMAX+1)
#   else
      --> please fix it
#   endif
# endif /* NSIG */

  void GC_print_sig_mask(void)
  {
    sigset_t blocked;
    int i;

    if (pthread_sigmask(SIG_BLOCK, NULL, &blocked) != 0)
      ABORT("pthread_sigmask failed");
    GC_printf("Blocked: ");
    for (i = 1; i < NSIG; i++) {
      if (sigismember(&blocked, i))
        GC_printf("%d ", i);
    }
    GC_printf("\n");
  }
#endif /* DEBUG_THREADS */

/* Remove the signals that we want to allow in thread stopping  */
/* handler from a set.                                          */
STATIC void GC_remove_allowed_signals(sigset_t *set)
{
    if (sigdelset(set, SIGINT) != 0
          || sigdelset(set, SIGQUIT) != 0
          || sigdelset(set, SIGABRT) != 0
          || sigdelset(set, SIGTERM) != 0) {
        ABORT("sigdelset() failed");
    }

#   ifdef MPROTECT_VDB
      /* Handlers write to the thread structure, which is in the heap,  */
      /* and hence can trigger a protection fault.                      */
      if (sigdelset(set, SIGSEGV) != 0
#         ifdef SIGBUS
            || sigdelset(set, SIGBUS) != 0
#         endif
          ) {
        ABORT("sigdelset() failed");
      }
#   endif
}

static sigset_t suspend_handler_mask;

STATIC volatile AO_t GC_stop_count = 0;
                        /* Incremented at the beginning of GC_stop_world. */

STATIC volatile AO_t GC_world_is_stopped = FALSE;
                        /* FALSE ==> it is safe for threads to restart, */
                        /* i.e. they will see another suspend signal    */
                        /* before they are expected to stop (unless     */
                        /* they have stopped voluntarily).              */

#ifdef GC_OSF1_THREADS
  STATIC GC_bool GC_retry_signals = TRUE;
#else
  STATIC GC_bool GC_retry_signals = FALSE;
#endif

/*
 * We use signals to stop threads during GC.
 *
 * Suspended threads wait in signal handler for SIG_THR_RESTART.
 * That's more portable than semaphores or condition variables.
 * (We do use sem_post from a signal handler, but that should be portable.)
 *
 * The thread suspension signal SIG_SUSPEND is now defined in gc_priv.h.
 * Note that we can't just stop a thread; we need it to save its stack
 * pointer(s) and acknowledge.
 */

#ifndef SIG_THR_RESTART
#  if defined(GC_HPUX_THREADS) || defined(GC_OSF1_THREADS) \
      || defined(GC_NETBSD_THREADS)
#    ifdef _SIGRTMIN
#      define SIG_THR_RESTART (_SIGRTMIN + 5)
#    else
#      define SIG_THR_RESTART (SIGRTMIN + 5)
#    endif
#  else
#   define SIG_THR_RESTART SIGXCPU
#  endif
#endif

#ifdef GC_EXPLICIT_SIGNALS_UNBLOCK
  /* Some targets (e.g., Solaris) might require this to be called when  */
  /* doing thread registering from the thread destructor.               */
  GC_INNER void GC_unblock_gc_signals(void)
  {
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIG_SUSPEND);
    sigaddset(&set, SIG_THR_RESTART);
    if (pthread_sigmask(SIG_UNBLOCK, &set, NULL) != 0)
      ABORT("pthread_sigmask failed");
  }
#endif /* GC_EXPLICIT_SIGNALS_UNBLOCK */

STATIC sem_t GC_suspend_ack_sem;

#ifdef GC_NETBSD_THREADS
# define GC_NETBSD_THREADS_WORKAROUND
  /* It seems to be necessary to wait until threads have restarted.     */
  /* But it is unclear why that is the case.                            */
  STATIC sem_t GC_restart_ack_sem;
#endif

STATIC void GC_suspend_handler_inner(ptr_t sig_arg, void *context);

#ifdef SA_SIGINFO
  /*ARGSUSED*/
  STATIC void GC_suspend_handler(int sig, siginfo_t *info, void *context)
#else
  STATIC void GC_suspend_handler(int sig)
#endif
{
# if defined(IA64) || defined(HP_PA) || defined(M68K)
    int old_errno = errno;
    GC_with_callee_saves_pushed(GC_suspend_handler_inner, (ptr_t)(word)sig);
    errno = old_errno;
# else
    /* We believe that in all other cases the full context is already   */
    /* in the signal handler frame.                                     */
    int old_errno = errno;
#   ifndef SA_SIGINFO
      void *context = 0;
#   endif
    GC_suspend_handler_inner((ptr_t)(word)sig, context);
    errno = old_errno;
# endif
}

/*ARGSUSED*/
STATIC void GC_suspend_handler_inner(ptr_t sig_arg, void *context)
{
  pthread_t self = pthread_self();
  GC_thread me;
  IF_CANCEL(int cancel_state;)
  AO_t my_stop_count = AO_load_acquire(&GC_stop_count);
                        /* After the barrier, this thread should see    */
                        /* the actual contents of GC_threads.           */

  if ((signed_word)sig_arg != SIG_SUSPEND) {
#   if defined(GC_FREEBSD_THREADS)
      /* Workaround "deferred signal handling" bug in FreeBSD 9.2.      */
      if (0 == sig_arg) return;
#   endif
    ABORT("Bad signal in suspend_handler");
  }

  DISABLE_CANCEL(cancel_state);
      /* pthread_setcancelstate is not defined to be async-signal-safe. */
      /* But the glibc version appears to be in the absence of          */
      /* asynchronous cancellation.  And since this signal handler      */
      /* to block on sigsuspend, which is both async-signal-safe        */
      /* and a cancellation point, there seems to be no obvious way     */
      /* out of it.  In fact, it looks to me like an async-signal-safe  */
      /* cancellation point is inherently a problem, unless there is    */
      /* some way to disable cancellation in the handler.               */
# ifdef DEBUG_THREADS
    GC_log_printf("Suspending 0x%x\n", (unsigned)self);
# endif

  me = GC_lookup_thread(self);
  /* The lookup here is safe, since I'm doing this on behalf    */
  /* of a thread which holds the allocation lock in order       */
  /* to stop the world.  Thus concurrent modification of the    */
  /* data structure is impossible.                              */
  if (me -> stop_info.last_stop_count == my_stop_count) {
      /* Duplicate signal.  OK if we are retrying.      */
      if (!GC_retry_signals) {
          WARN("Duplicate suspend signal in thread %p\n", self);
      }
      RESTORE_CANCEL(cancel_state);
      return;
  }
# ifdef SPARC
      me -> stop_info.stack_ptr = GC_save_regs_in_stack();
# else
      me -> stop_info.stack_ptr = GC_approx_sp();
# endif
# ifdef IA64
      me -> backing_store_ptr = GC_save_regs_in_stack();
# endif

  /* Tell the thread that wants to stop the world that this     */
  /* thread has been stopped.  Note that sem_post() is          */
  /* the only async-signal-safe primitive in LinuxThreads.      */
  sem_post(&GC_suspend_ack_sem);
  AO_store_release(&me->stop_info.last_stop_count, my_stop_count);

  /* Wait until that thread tells us to restart by sending      */
  /* this thread a SIG_THR_RESTART signal.                      */
  /* SIG_THR_RESTART should be masked at this point.  Thus      */
  /* there is no race.                                          */
  /* We do not continue until we receive a SIG_THR_RESTART,     */
  /* but we do not take that as authoritative.  (We may be      */
  /* accidentally restarted by one of the user signals we       */
  /* don't block.)  After we receive the signal, we use a       */
  /* primitive and expensive mechanism to wait until it's       */
  /* really safe to proceed.  Under normal circumstances,       */
  /* this code should not be executed.                          */
  do {
      sigsuspend (&suspend_handler_mask);
  } while (AO_load_acquire(&GC_world_is_stopped)
           && AO_load(&GC_stop_count) == my_stop_count);
  /* If the RESTART signal gets lost, we can still lose.  That should   */
  /* be less likely than losing the SUSPEND signal, since we don't do   */
  /* much between the sem_post and sigsuspend.                          */
  /* We'd need more handshaking to work around that.                    */
  /* Simply dropping the sigsuspend call should be safe, but is         */
  /* unlikely to be efficient.                                          */

# ifdef DEBUG_THREADS
    GC_log_printf("Continuing 0x%x\n", (unsigned)self);
# endif
  RESTORE_CANCEL(cancel_state);
}

STATIC void GC_restart_handler(int sig)
{
# if defined(DEBUG_THREADS) || defined(GC_NETBSD_THREADS_WORKAROUND)
    int old_errno = errno;      /* Preserve errno value.        */
# endif

  if (sig != SIG_THR_RESTART) ABORT("Bad signal in restart handler");

# ifdef GC_NETBSD_THREADS_WORKAROUND
    sem_post(&GC_restart_ack_sem);
# endif

  /*
  ** Note: even if we don't do anything useful here,
  ** it would still be necessary to have a signal handler,
  ** rather than ignoring the signals, otherwise
  ** the signals will not be delivered at all, and
  ** will thus not interrupt the sigsuspend() above.
  */

# ifdef DEBUG_THREADS
    GC_log_printf("In GC_restart_handler for 0x%x\n",
                  (unsigned)pthread_self());
# endif
# if defined(DEBUG_THREADS) || defined(GC_NETBSD_THREADS_WORKAROUND)
    errno = old_errno;
# endif
}

#endif /* !GC_OPENBSD_THREADS && !NACL */

#ifdef IA64
# define IF_IA64(x) x
#else
# define IF_IA64(x)
#endif
/* We hold allocation lock.  Should do exactly the right thing if the   */
/* world is stopped.  Should not fail if it isn't.                      */
GC_INNER void GC_push_all_stacks(void)
{
    GC_bool found_me = FALSE;
    size_t nthreads = 0;
    int i;
    GC_thread p;
    ptr_t lo, hi;
    /* On IA64, we also need to scan the register backing store. */
    IF_IA64(ptr_t bs_lo; ptr_t bs_hi;)
    struct GC_traced_stack_sect_s *traced_stack_sect;
    pthread_t self = pthread_self();
    word total_size = 0;

    if (!GC_thr_initialized) GC_thr_init();
#   ifdef DEBUG_THREADS
      GC_log_printf("Pushing stacks from thread 0x%x\n", (unsigned)self);
#   endif
    for (i = 0; i < THREAD_TABLE_SZ; i++) {
      for (p = GC_threads[i]; p != 0; p = p -> next) {
        if (p -> flags & FINISHED) continue;
        ++nthreads;
        traced_stack_sect = p -> traced_stack_sect;
        if (THREAD_EQUAL(p -> id, self)) {
            GC_ASSERT(!p->thread_blocked);
#           ifdef SPARC
                lo = (ptr_t)GC_save_regs_in_stack();
#           else
                lo = GC_approx_sp();
#           endif
            found_me = TRUE;
            IF_IA64(bs_hi = (ptr_t)GC_save_regs_in_stack();)
        } else {
            lo = p -> stop_info.stack_ptr;
            IF_IA64(bs_hi = p -> backing_store_ptr;)
            if (traced_stack_sect != NULL
                    && traced_stack_sect->saved_stack_ptr == lo) {
              /* If the thread has never been stopped since the recent  */
              /* GC_call_with_gc_active invocation then skip the top    */
              /* "stack section" as stack_ptr already points to.        */
              traced_stack_sect = traced_stack_sect->prev;
            }
        }
        if ((p -> flags & MAIN_THREAD) == 0) {
            hi = p -> stack_end;
            IF_IA64(bs_lo = p -> backing_store_end);
        } else {
            /* The original stack. */
            hi = GC_stackbottom;
            IF_IA64(bs_lo = BACKING_STORE_BASE;)
        }
#       ifdef DEBUG_THREADS
          GC_log_printf("Stack for thread 0x%x = [%p,%p)\n",
                        (unsigned)(p -> id), lo, hi);
#       endif
        if (0 == lo) ABORT("GC_push_all_stacks: sp not set!");
        GC_push_all_stack_sections(lo, hi, traced_stack_sect);
#       ifdef STACK_GROWS_UP
          total_size += lo - hi;
#       else
          total_size += hi - lo; /* lo <= hi */
#       endif
#       ifdef NACL
          /* Push reg_storage as roots, this will cover the reg context. */
          GC_push_all_stack((ptr_t)p -> stop_info.reg_storage,
              (ptr_t)(p -> stop_info.reg_storage + NACL_GC_REG_STORAGE_SIZE));
          total_size += NACL_GC_REG_STORAGE_SIZE * sizeof(ptr_t);
#       endif
#       ifdef IA64
#         ifdef DEBUG_THREADS
            GC_log_printf("Reg stack for thread 0x%x = [%p,%p)\n",
                          (unsigned)p -> id, bs_lo, bs_hi);
#         endif
          /* FIXME: This (if p->id==self) may add an unbounded number of */
          /* entries, and hence overflow the mark stack, which is bad.   */
          GC_push_all_register_sections(bs_lo, bs_hi,
                                        THREAD_EQUAL(p -> id, self),
                                        traced_stack_sect);
          total_size += bs_hi - bs_lo; /* bs_lo <= bs_hi */
#       endif
      }
    }
    if (GC_print_stats == VERBOSE) {
      GC_log_printf("Pushed %d thread stacks\n", (int)nthreads);
    }
    if (!found_me && !GC_in_thread_creation)
      ABORT("Collecting from unknown thread");
    GC_total_stacksize = total_size;
}

#ifdef DEBUG_THREADS
  /* There seems to be a very rare thread stopping problem.  To help us */
  /* debug that, we save the ids of the stopping thread.                */
  pthread_t GC_stopping_thread;
  int GC_stopping_pid = 0;
#endif

#ifdef PLATFORM_ANDROID
  extern int tkill(pid_t tid, int sig); /* from sys/linux-unistd.h */

  static int android_thread_kill(pid_t tid, int sig)
  {
    int ret;
    int old_errno = errno;

    ret = tkill(tid, sig);
    if (ret < 0) {
        ret = errno;
        errno = old_errno;
    }

    return ret;
  }
#endif /* PLATFORM_ANDROID */

/* We hold the allocation lock.  Suspend all threads that might */
/* still be running.  Return the number of suspend signals that */
/* were sent.                                                   */
STATIC int GC_suspend_all(void)
{
  int n_live_threads = 0;
  int i;

# ifndef NACL
    GC_thread p;
#   ifndef GC_OPENBSD_THREADS
      int result;
#   endif
    pthread_t self = pthread_self();

#   ifdef DEBUG_THREADS
      GC_stopping_thread = self;
      GC_stopping_pid = getpid();
#   endif
    for (i = 0; i < THREAD_TABLE_SZ; i++) {
      for (p = GC_threads[i]; p != 0; p = p -> next) {
        if (!THREAD_EQUAL(p -> id, self)) {
            if (p -> flags & FINISHED) continue;
            if (p -> thread_blocked) /* Will wait */ continue;
#           ifndef GC_OPENBSD_THREADS
              if (p -> stop_info.last_stop_count == GC_stop_count) continue;
              n_live_threads++;
#           endif
#           ifdef DEBUG_THREADS
              GC_log_printf("Sending suspend signal to 0x%x\n",
                            (unsigned)(p -> id));
#           endif

#           ifdef GC_OPENBSD_THREADS
              {
                stack_t stack;
                if (pthread_suspend_np(p -> id) != 0)
                  ABORT("pthread_suspend_np failed");
                if (pthread_stackseg_np(p->id, &stack))
                  ABORT("pthread_stackseg_np failed");
                p -> stop_info.stack_ptr = (ptr_t)stack.ss_sp - stack.ss_size;
              }
#           else
#             ifndef PLATFORM_ANDROID
                result = pthread_kill(p -> id, SIG_SUSPEND);
#             else
                result = android_thread_kill(p -> kernel_id, SIG_SUSPEND);
#             endif
              switch(result) {
                case ESRCH:
                    /* Not really there anymore.  Possible? */
                    n_live_threads--;
                    break;
                case 0:
                    break;
                default:
                    ABORT("pthread_kill failed");
              }
#           endif
        }
      }
    }

# else /* NACL */
#   ifndef NACL_PARK_WAIT_NANOSECONDS
#     define NACL_PARK_WAIT_NANOSECONDS (100 * 1000)
#   endif
#   ifdef DEBUG_THREADS
      GC_log_printf("pthread_stop_world: num_threads %d\n",
                    GC_nacl_num_gc_threads - 1);
#   endif
    GC_nacl_thread_parker = pthread_self();
    GC_nacl_park_threads_now = 1;
#   ifdef DEBUG_THREADS
      GC_stopping_thread = GC_nacl_thread_parker;
      GC_stopping_pid = getpid();
#   endif

    while (1) {
      int num_threads_parked = 0;
      struct timespec ts;
      int num_used = 0;

      /* Check the 'parked' flag for each thread the GC knows about.    */
      for (i = 0; i < MAX_NACL_GC_THREADS
                  && num_used < GC_nacl_num_gc_threads; i++) {
        if (GC_nacl_thread_used[i] == 1) {
          num_used++;
          if (GC_nacl_thread_parked[i] == 1) {
            num_threads_parked++;
          }
        }
      }
      /* -1 for the current thread.     */
      if (num_threads_parked >= GC_nacl_num_gc_threads - 1)
        break;
      ts.tv_sec = 0;
      ts.tv_nsec = NACL_PARK_WAIT_NANOSECONDS;
#     ifdef DEBUG_THREADS
        GC_log_printf("Sleep waiting for %d threads to park...\n",
                      GC_nacl_num_gc_threads - num_threads_parked - 1);
#     endif
      /* This requires _POSIX_TIMERS feature.   */
      nanosleep(&ts, 0);
    }
# endif /* NACL */
  return n_live_threads;
}

GC_INNER void GC_stop_world(void)
{
# if !defined(GC_OPENBSD_THREADS) && !defined(NACL)
    int i;
    int n_live_threads;
    int code;
# endif
  GC_ASSERT(I_HOLD_LOCK());
# ifdef DEBUG_THREADS
    GC_log_printf("Stopping the world from 0x%x\n", (unsigned)pthread_self());
# endif

  /* Make sure all free list construction has stopped before we start.  */
  /* No new construction can start, since free list construction is     */
  /* required to acquire and release the GC lock before it starts,      */
  /* and we have the lock.                                              */
# ifdef PARALLEL_MARK
    if (GC_parallel) {
      GC_acquire_mark_lock();
      GC_ASSERT(GC_fl_builder_count == 0);
      /* We should have previously waited for it to become zero.        */
    }
# endif /* PARALLEL_MARK */

# if defined(GC_OPENBSD_THREADS) || defined(NACL)
    (void)GC_suspend_all();
# else
    AO_store(&GC_stop_count, GC_stop_count+1);
        /* Only concurrent reads are possible. */
    AO_store_release(&GC_world_is_stopped, TRUE);
    n_live_threads = GC_suspend_all();

    if (GC_retry_signals) {
      unsigned long wait_usecs = 0;  /* Total wait since retry. */
#     define WAIT_UNIT 3000
#     define RETRY_INTERVAL 100000
      for (;;) {
        int ack_count;

        sem_getvalue(&GC_suspend_ack_sem, &ack_count);
        if (ack_count == n_live_threads) break;
        if (wait_usecs > RETRY_INTERVAL) {
          int newly_sent = GC_suspend_all();

          if (GC_print_stats) {
            GC_log_printf("Resent %d signals after timeout\n", newly_sent);
          }
          sem_getvalue(&GC_suspend_ack_sem, &ack_count);
          if (newly_sent < n_live_threads - ack_count) {
            WARN("Lost some threads during GC_stop_world?!\n",0);
            n_live_threads = ack_count + newly_sent;
          }
          wait_usecs = 0;
        }
        usleep(WAIT_UNIT);
        wait_usecs += WAIT_UNIT;
      }
    }

    for (i = 0; i < n_live_threads; i++) {
      retry:
        if (0 != (code = sem_wait(&GC_suspend_ack_sem))) {
          /* On Linux, sem_wait is documented to always return zero.    */
          /* But the documentation appears to be incorrect.             */
          if (errno == EINTR) {
            /* Seems to happen with some versions of gdb.       */
            goto retry;
          }
          ABORT("sem_wait for handler failed");
        }
    }
# endif

# ifdef PARALLEL_MARK
    if (GC_parallel)
      GC_release_mark_lock();
# endif
# ifdef DEBUG_THREADS
    GC_log_printf("World stopped from 0x%x\n", (unsigned)pthread_self());
    GC_stopping_thread = 0;
# endif
}

#ifdef NACL
# if defined(__x86_64__)
#   define NACL_STORE_REGS() \
        do { \
          __asm__ __volatile__ ("push %rbx"); \
          __asm__ __volatile__ ("push %rbp"); \
          __asm__ __volatile__ ("push %r12"); \
          __asm__ __volatile__ ("push %r13"); \
          __asm__ __volatile__ ("push %r14"); \
          __asm__ __volatile__ ("push %r15"); \
          __asm__ __volatile__ ("mov %%esp, %0" \
                    : "=m" (GC_nacl_gc_thread_self->stop_info.stack_ptr)); \
          BCOPY(GC_nacl_gc_thread_self->stop_info.stack_ptr, \
                GC_nacl_gc_thread_self->stop_info.reg_storage, \
                NACL_GC_REG_STORAGE_SIZE * sizeof(ptr_t)); \
          __asm__ __volatile__ ("naclasp $48, %r15"); \
        } while (0)
# elif defined(__i386__)
#   define NACL_STORE_REGS() \
        do { \
          __asm__ __volatile__ ("push %ebx"); \
          __asm__ __volatile__ ("push %ebp"); \
          __asm__ __volatile__ ("push %esi"); \
          __asm__ __volatile__ ("push %edi"); \
          __asm__ __volatile__ ("mov %%esp, %0" \
                    : "=m" (GC_nacl_gc_thread_self->stop_info.stack_ptr)); \
          BCOPY(GC_nacl_gc_thread_self->stop_info.stack_ptr, \
                GC_nacl_gc_thread_self->stop_info.reg_storage, \
                NACL_GC_REG_STORAGE_SIZE * sizeof(ptr_t));\
          __asm__ __volatile__ ("add $16, %esp"); \
        } while (0)
# else
#   error FIXME for non-amd64/x86 NaCl
# endif

  GC_API_OSCALL void nacl_pre_syscall_hook(void)
  {
    if (GC_nacl_thread_idx != -1) {
      NACL_STORE_REGS();
      GC_nacl_gc_thread_self->stop_info.stack_ptr = GC_approx_sp();
      GC_nacl_thread_parked[GC_nacl_thread_idx] = 1;
    }
  }

  GC_API_OSCALL void __nacl_suspend_thread_if_needed(void)
  {
    if (GC_nacl_park_threads_now) {
      pthread_t self = pthread_self();

      /* Don't try to park the thread parker.   */
      if (GC_nacl_thread_parker == self)
        return;

      /* This can happen when a thread is created outside of the GC     */
      /* system (wthread mostly).                                       */
      if (GC_nacl_thread_idx < 0)
        return;

      /* If it was already 'parked', we're returning from a syscall,    */
      /* so don't bother storing registers again, the GC has a set.     */
      if (!GC_nacl_thread_parked[GC_nacl_thread_idx]) {
        NACL_STORE_REGS();
        GC_nacl_gc_thread_self->stop_info.stack_ptr = GC_approx_sp();
      }
      GC_nacl_thread_parked[GC_nacl_thread_idx] = 1;
      while (GC_nacl_park_threads_now) {
        /* Just spin.   */
      }
      GC_nacl_thread_parked[GC_nacl_thread_idx] = 0;

      /* Clear out the reg storage for next suspend.    */
      BZERO(GC_nacl_gc_thread_self->stop_info.reg_storage,
            NACL_GC_REG_STORAGE_SIZE * sizeof(ptr_t));
    }
  }

  GC_API_OSCALL void nacl_post_syscall_hook(void)
  {
    /* Calling __nacl_suspend_thread_if_needed right away should        */
    /* guarantee we don't mutate the GC set.                            */
    __nacl_suspend_thread_if_needed();
    if (GC_nacl_thread_idx != -1) {
      GC_nacl_thread_parked[GC_nacl_thread_idx] = 0;
    }
  }

  STATIC GC_bool GC_nacl_thread_parking_inited = FALSE;
  STATIC pthread_mutex_t GC_nacl_thread_alloc_lock = PTHREAD_MUTEX_INITIALIZER;

  GC_INNER void GC_nacl_initialize_gc_thread(void)
  {
    int i;
    pthread_mutex_lock(&GC_nacl_thread_alloc_lock);
    if (!GC_nacl_thread_parking_inited) {
      BZERO(GC_nacl_thread_parked, sizeof(GC_nacl_thread_parked));
      BZERO(GC_nacl_thread_used, sizeof(GC_nacl_thread_used));
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

  GC_INNER void GC_nacl_shutdown_gc_thread(void)
  {
    pthread_mutex_lock(&GC_nacl_thread_alloc_lock);
    GC_ASSERT(GC_nacl_thread_idx >= 0);
    GC_ASSERT(GC_nacl_thread_idx < MAX_NACL_GC_THREADS);
    GC_ASSERT(GC_nacl_thread_used[GC_nacl_thread_idx] != 0);
    GC_nacl_thread_used[GC_nacl_thread_idx] = 0;
    GC_nacl_thread_idx = -1;
    GC_nacl_num_gc_threads--;
    pthread_mutex_unlock(&GC_nacl_thread_alloc_lock);
  }
#endif /* NACL */

/* Caller holds allocation lock, and has held it continuously since     */
/* the world stopped.                                                   */
GC_INNER void GC_start_world(void)
{
# ifndef NACL
    pthread_t self = pthread_self();
    register int i;
    register GC_thread p;
#   ifndef GC_OPENBSD_THREADS
      register int n_live_threads = 0;
      register int result;
#   endif
#   ifdef GC_NETBSD_THREADS_WORKAROUND
      int code;
#   endif

#   ifdef DEBUG_THREADS
      GC_log_printf("World starting\n");
#   endif

#   ifndef GC_OPENBSD_THREADS
      AO_store_release(&GC_world_is_stopped, FALSE);
                    /* The updated value should now be visible to the   */
                    /* signal handler (note that pthread_kill is not on */
                    /* the list of functions which synchronize memory). */
#   endif
    for (i = 0; i < THREAD_TABLE_SZ; i++) {
      for (p = GC_threads[i]; p != 0; p = p -> next) {
        if (!THREAD_EQUAL(p -> id, self)) {
            if (p -> flags & FINISHED) continue;
            if (p -> thread_blocked) continue;
#           ifndef GC_OPENBSD_THREADS
              n_live_threads++;
#           endif
#           ifdef DEBUG_THREADS
              GC_log_printf("Sending restart signal to 0x%x\n",
                            (unsigned)(p -> id));
#           endif

#         ifdef GC_OPENBSD_THREADS
            if (pthread_resume_np(p -> id) != 0)
              ABORT("pthread_resume_np failed");
#         else
#           ifndef PLATFORM_ANDROID
              result = pthread_kill(p -> id, SIG_THR_RESTART);
#           else
              result = android_thread_kill(p -> kernel_id, SIG_THR_RESTART);
#           endif
            switch(result) {
                case ESRCH:
                    /* Not really there anymore.  Possible? */
                    n_live_threads--;
                    break;
                case 0:
                    break;
                default:
                    ABORT("pthread_kill failed");
            }
#         endif
        }
      }
    }
#   ifdef GC_NETBSD_THREADS_WORKAROUND
      for (i = 0; i < n_live_threads; i++) {
        while (0 != (code = sem_wait(&GC_restart_ack_sem))) {
          if (errno != EINTR) {
            if (GC_print_stats)
              GC_log_printf("sem_wait() returned %d\n", code);
            ABORT("sem_wait() for restart handler failed");
          }
        }
      }
#   endif
#   ifdef DEBUG_THREADS
      GC_log_printf("World started\n");
#   endif
# else /* NACL */
#   ifdef DEBUG_THREADS
      GC_log_printf("World starting...\n");
#   endif
    GC_nacl_park_threads_now = 0;
# endif
}

GC_INNER void GC_stop_init(void)
{
# if !defined(GC_OPENBSD_THREADS) && !defined(NACL)
    struct sigaction act;

    if (sem_init(&GC_suspend_ack_sem, GC_SEM_INIT_PSHARED, 0) != 0)
        ABORT("sem_init failed");
#   ifdef GC_NETBSD_THREADS_WORKAROUND
      if (sem_init(&GC_restart_ack_sem, GC_SEM_INIT_PSHARED, 0) != 0)
        ABORT("sem_init failed");
#   endif

#   ifdef SA_RESTART
      act.sa_flags = SA_RESTART
#   else
      act.sa_flags = 0
#   endif
#   ifdef SA_SIGINFO
                     | SA_SIGINFO
#   endif
        ;
    if (sigfillset(&act.sa_mask) != 0) {
        ABORT("sigfillset() failed");
    }
#   ifdef GC_RTEMS_PTHREADS
      if(sigprocmask(SIG_UNBLOCK, &act.sa_mask, NULL) != 0) {
        ABORT("rtems sigprocmask() failed");
      }
#   endif
    GC_remove_allowed_signals(&act.sa_mask);
    /* SIG_THR_RESTART is set in the resulting mask.            */
    /* It is unmasked by the handler when necessary.            */
#   ifdef SA_SIGINFO
      act.sa_sigaction = GC_suspend_handler;
#   else
      act.sa_handler = GC_suspend_handler;
#   endif
    if (sigaction(SIG_SUSPEND, &act, NULL) != 0) {
        ABORT("Cannot set SIG_SUSPEND handler");
    }

#   ifdef SA_SIGINFO
      act.sa_flags &= ~ SA_SIGINFO;
#   endif
    act.sa_handler = GC_restart_handler;
    if (sigaction(SIG_THR_RESTART, &act, NULL) != 0) {
        ABORT("Cannot set SIG_THR_RESTART handler");
    }

    /* Initialize suspend_handler_mask. It excludes SIG_THR_RESTART. */
    if (sigfillset(&suspend_handler_mask) != 0) ABORT("sigfillset() failed");
    GC_remove_allowed_signals(&suspend_handler_mask);
    if (sigdelset(&suspend_handler_mask, SIG_THR_RESTART) != 0)
        ABORT("sigdelset() failed");

    /* Check for GC_RETRY_SIGNALS.      */
    if (0 != GETENV("GC_RETRY_SIGNALS")) {
        GC_retry_signals = TRUE;
    }
    if (0 != GETENV("GC_NO_RETRY_SIGNALS")) {
        GC_retry_signals = FALSE;
    }
    if (GC_print_stats && GC_retry_signals) {
      GC_log_printf("Will retry suspend signal if necessary\n");
    }
# endif /* !GC_OPENBSD_THREADS && !NACL */
}

#endif
