/*
 * Copyright (c) 1994 by Xerox Corporation.  All rights reserved.
 * Copyright (c) 1996 by Silicon Graphics.  All rights reserved.
 * Copyright (c) 1998 by Fergus Henderson.  All rights reserved.
 * Copyright (c) 2000-2009 by Hewlett-Packard Development Company.
 * All rights reserved.
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

/* Private declarations for thread support. */

#ifndef GC_PTHREAD_SUPPORT_H
#define GC_PTHREAD_SUPPORT_H

#include "gc_priv.h"

#ifdef THREADS

#  if defined(GC_PTHREADS) || defined(GC_PTHREADS_PARAMARK)
#    include <pthread.h>
#  endif

#  ifdef DARWIN
#    include <mach/mach.h>
#    include <mach/thread_act.h>
#  endif

#  ifdef THREAD_LOCAL_ALLOC
#    include "thread_local_alloc.h"
#  endif

#  ifdef THREAD_SANITIZER
#    include "dbg_mlc.h" /*< for `oh` type */
#  endif

EXTERN_C_BEGIN

#  ifdef WOW64_THREAD_CONTEXT_WORKAROUND
#    if defined(__WATCOMC__) && defined(GC_WIN32_THREADS)
/*
 * Open Watcom (as of v2 2024-11-02 Build) does not define `NT_TIB`
 * in the platform `winnt.h` file.
 */
struct GC_NT_TIB_s {
  /* `EXCEPTION_REGISTRATION_RECORD *` */ void *ExceptionList; /*< unused */
  PVOID StackBase;
  PVOID StackLimit;
  /* The remaining fields are unused. */
};
typedef struct GC_NT_TIB_s GC_NT_TIB;
#    else
#      define GC_NT_TIB NT_TIB
#    endif
#  endif

struct GC_StackContext_Rep {
#  if defined(THREAD_SANITIZER) && defined(SIGNAL_BASED_STOP_WORLD)
  /*
   * A dummy field to avoid TSan false positive about the race
   * between `GC_has_other_debug_info` and `GC_suspend_handler_inner`
   * (the latter one sets `stack_ptr`).
   */
  char dummy[sizeof(oh)];
#  endif

  /*
   * Cold end of the stack (except for main thread on non-Windows).
   * On Windows: 0 means entry invalid; not `in_use` implies `stack_end`
   * is zero.
   */
#  if !defined(GC_NO_THREADS_DISCOVERY) && defined(GC_WIN32_THREADS)
  volatile
#  endif
      ptr_t stack_end;

  /* Valid only in some platform-specific states. */
  ptr_t stack_ptr;

#  ifdef GC_WIN32_THREADS
#    define ADDR_LIMIT ((ptr_t)GC_WORD_MAX)
  /*
   * Last known minimum (hottest) address in stack or `ADDR_LIMIT`
   * if unset.
   */
  ptr_t last_stack_min;
#    ifdef I386
  /*
   * The cold end of the stack saved by `GC_record_stack_base` (never
   * modified by `GC_set_stackbottom()`); used for the old way of the
   * coroutines support.
   */
  ptr_t initial_stack_base;
#    endif
#  elif defined(DARWIN) && !defined(DARWIN_DONT_PARSE_STACK)
  /*
   * Result of `GC_FindTopOfStack(0)`; valid only if the thread is blocked;
   * non-`NULL` value means already set.
   */
  ptr_t topOfStack;
#  endif

#  if defined(E2K) || defined(IA64)
  ptr_t backing_store_end;
  ptr_t backing_store_ptr;
#  endif

#  ifdef GC_WIN32_THREADS
  /* For now, alt-stack is not implemented for Win32. */
#  else
  /* The start of the alt-stack if there is one, `NULL` otherwise. */
  ptr_t altstack;

  /* Same for the "normal" stack (set by `GC_register_altstack`). */
  ptr_t normstack;

  /* The size of the alt-stack if exists. */
  size_t altstack_size;

  size_t normstack_size;
#  endif

#  ifdef E2K
  /* The current offset in the procedure stack. */
  size_t ps_ofs;
#  endif

#  ifndef GC_NO_FINALIZATION
  unsigned char finalizer_nested;

  /*
   * Used by `GC_check_finalizer_nested()` to minimize the level of recursion
   * when a client finalizer allocates memory.  Initially it is zero and
   * `finalizer_nested` is zero.
   */
  unsigned short finalizer_skipped;
#  endif

  /*
   * Points to the "frame" data held in stack by the innermost
   * `GC_call_with_gc_active()` of this stack (thread); may be `NULL`.
   */
  struct GC_traced_stack_sect_s *traced_stack_sect;
};
typedef struct GC_StackContext_Rep *GC_stack_context_t;

#  ifdef GC_WIN32_THREADS
typedef DWORD thread_id_t;
#    define thread_id_self() GetCurrentThreadId()
#    define THREAD_ID_EQUAL(id1, id2) ((id1) == (id2))
/*
 * Convert a platform-specific thread `id` (of `thread_id_t` type) to some
 * pointer identifying the thread.  This is used mostly for a debugging
 * purpose when printing thread id.
 */
#    define THREAD_ID_TO_VPTR(id) CAST_THRU_UINTPTR(void *, id)
#  else
typedef pthread_t thread_id_t;
#    define thread_id_self() pthread_self()
#    define THREAD_ID_EQUAL(id1, id2) THREAD_EQUAL(id1, id2)
#    define THREAD_ID_TO_VPTR(id) PTHREAD_TO_VPTR(id)
#  endif

struct GC_Thread_Rep {
  union {
#  if !defined(GC_NO_THREADS_DISCOVERY) && defined(GC_WIN32_THREADS)
    /*
     * Updated without a lock.  We assert that each unused entry has
     * invalid `id` of zero and zero `stack_end`.
     * Used only if `GC_win32_dll_threads`.
     */
    volatile AO_t in_use;

    /*
     * The same but of the type that matches the first argument of
     * `InterlockedExchange()`; `volatile` is omitted because the
     * ancient version of the function prototype lacked the qualifier.
     */
    LONG long_in_use;
#  endif

    /*
     * Hash table link without `GC_win32_dll_threads`.  More recently
     * allocated threads with a given `pthreads` id come first.
     * (All but the first are guaranteed to be dead, but we may not yet
     * have registered the join.)
     */
    struct GC_Thread_Rep *next;
  } tm; /*< table_management */

  GC_stack_context_t crtn;

  thread_id_t id; /*< hash table key */
#  ifdef DARWIN
  mach_port_t mach_thread;
#  elif defined(GC_WIN32_THREADS) && defined(GC_PTHREADS)
  pthread_t pthread_id;
#  elif defined(USE_TKILL_ON_ANDROID)
  pid_t kernel_id;
#  endif

#  ifdef MSWINCE
  /*
   * According to MSDN docs for WinCE targets:
   *   - `DuplicateHandle()` is not applicable to thread handles;
   *   - the value returned by `GetCurrentThreadId()` could be used as
   *     a "real" thread handle (for `SuspendThread()`, `ResumeThread()`
   *     and `GetThreadContext()`).
   */
#    define THREAD_HANDLE(p) ((HANDLE)(word)(p)->id)
#  elif defined(GC_WIN32_THREADS)
  HANDLE handle;
#    define THREAD_HANDLE(p) ((p)->handle)
#  endif /* GC_WIN32_THREADS && !MSWINCE */

  /* Note: this is protected by the allocator lock. */
  unsigned char flags;

  /* Thread has exited (`pthreads` only). */
#  define FINISHED 0x1
#  ifndef GC_PTHREADS
#    define KNOWN_FINISHED(p) FALSE
#  else
#    define KNOWN_FINISHED(p) (((p)->flags & FINISHED) != 0)
  /*
   * Thread is treated as detached.  Thread may really be detached,
   * or it may have been explicitly registered, in which case we can
   * deallocate its `GC_Thread_Rep` entity once it unregisters itself,
   * since it may not return a pointer to the collector heap.
   */
#    define DETACHED 0x2
#  endif
#  if (defined(GC_HAVE_PTHREAD_EXIT) || !defined(GC_NO_PTHREAD_CANCEL)) \
      && defined(GC_PTHREADS)
  /* Collections are disabled while the thread is exiting. */
#    define DISABLED_GC 0x10
#  endif
  /*
   * Thread is in the do-blocking state.  If the flag is set, the thread
   * has stored its stack pointer value and will acquire the allocator lock
   * before any pointer manipulation.  Thus, it does not need a signal sent
   * to stop it.
   */
#  define DO_BLOCKING 0x20
#  ifdef GC_WIN32_THREADS
  /* Thread is suspended by `SuspendThread()`. */
#    define IS_SUSPENDED 0x40
#  endif

#  ifdef SIGNAL_BASED_STOP_WORLD
  /*
   * The value of `GC_stop_count` when the thread last successfully
   * handled a suspend signal.
   */
  volatile AO_t last_stop_count;
#    ifdef GC_ENABLE_SUSPEND_THREAD
  /*
   * Note: an odd value means thread was suspended externally;
   * incremented on every call of `GC_suspend_thread()` and
   * `GC_resume_thread()`; updated with the allocator lock held, but
   * could be read from a signal handler.
   */
  volatile AO_t ext_suspend_cnt;
#    endif
#  endif

#  ifdef GC_PTHREADS
  /*
   * The value returned from the thread.  Used only to avoid premature
   * reclamation of any data it might reference.  This is unfortunately
   * also the reason we need to intercept join and detach.
   */
  void *status;
#  endif

#  ifdef THREAD_LOCAL_ALLOC
  struct thread_local_freelists tlfs GC_ATTR_PTRT_ALIGNED;
#  endif

#  ifdef NACL
  /*
   * Grab `NACL_GC_REG_STORAGE_SIZE` pointers off the stack when going
   * into a `syscall()`.  20 is more than we need, but it is
   * an overestimate in case the instrumented function uses any
   * callee-saved registers, they may be pushed to the stack much
   * earlier.  Also, on x86_64 `push` puts 8 bytes on the stack even
   * though our pointers have 4 bytes in length.
   */
#    ifdef ARM32
  /* Space for `r4` ... `r8`, `r10`, ... `r12`, `r14`. */
#      define NACL_GC_REG_STORAGE_SIZE 9
#    else
#      define NACL_GC_REG_STORAGE_SIZE 20
#    endif
  ptr_t reg_storage[NACL_GC_REG_STORAGE_SIZE];
#  elif defined(PLATFORM_HAVE_GC_REG_STORAGE_SIZE)
  word registers[PLATFORM_GC_REG_STORAGE_SIZE]; /*< used externally */
#  endif

#  if defined(WOW64_THREAD_CONTEXT_WORKAROUND) && defined(MSWINRT_FLAVOR)
  GC_NT_TIB *tib;
#  endif

#  ifdef RETRY_GET_THREAD_CONTEXT /* `&& GC_WIN32_THREADS` */
  ptr_t context_sp;

  /*
   * Populated as part of `GC_suspend()` as resume/suspend loop may be
   * needed for `GetThreadContext()` to succeed.
   */
  word context_regs[PUSHED_REGS_COUNT];
#  endif
};
typedef struct GC_Thread_Rep *GC_thread;

#  if defined(GC_PTHREADS) || defined(GC_PTHREADS_PARAMARK)
/* Convert an opaque `pthread_t` value to a pointer identifying the thread. */
#    if defined(GC_WIN32_THREADS) && !defined(CYGWIN32)                  \
        && (defined(GC_WIN32_PTHREADS) || defined(GC_PTHREADS_PARAMARK)) \
        && !defined(__WINPTHREADS_VERSION_MAJOR)
/*
 * Using documented internal details of pthreads-win32 library.
 * `pthread_t` is a structure.
 */
#      define PTHREAD_TO_VPTR(id) ((void *)(id).p)
#    else
#      define PTHREAD_TO_VPTR(id) CAST_THRU_UINTPTR(void *, id)
#    endif
#  endif

#  ifndef THREAD_TABLE_SZ
/* Note: this is a power of 2 (for speed). */
#    define THREAD_TABLE_SZ 256
#  endif

#  ifdef GC_WIN32_THREADS
#    define THREAD_TABLE_INDEX(id) /*< `id` is of `DWORD` type */ \
      (int)((((id) >> 8) ^ (id)) % THREAD_TABLE_SZ)
#  elif CPP_WORDSZ > 32
#    define THREAD_TABLE_INDEX(id)                                          \
      (int)(((((NUMERIC_THREAD_ID(id) >> 8) ^ NUMERIC_THREAD_ID(id)) >> 16) \
             ^ ((NUMERIC_THREAD_ID(id) >> 8) ^ NUMERIC_THREAD_ID(id)))      \
            % THREAD_TABLE_SZ)
#  else
#    define THREAD_TABLE_INDEX(id)                                        \
      (int)(((NUMERIC_THREAD_ID(id) >> 16) ^ (NUMERIC_THREAD_ID(id) >> 8) \
             ^ NUMERIC_THREAD_ID(id))                                     \
            % THREAD_TABLE_SZ)
#  endif

/*
 * The set (hash table) of all known threads.  We intercept thread
 * creation and join/detach.  Protected by the allocator lock.
 * Not used if `GC_win32_dll_threads`.
 */
GC_EXTERN GC_thread GC_threads[THREAD_TABLE_SZ];

#  ifndef MAX_MARKERS
#    define MAX_MARKERS 16
#  endif

#  ifdef STACKPTR_CORRECTOR_AVAILABLE
GC_EXTERN GC_sp_corrector_proc GC_sp_corrector;
#  endif

GC_EXTERN GC_on_thread_event_proc GC_on_thread_event;

#  ifdef GC_WIN32_THREADS

#    ifdef GC_NO_THREADS_DISCOVERY
#      define GC_win32_dll_threads FALSE
#    elif defined(GC_DISCOVER_TASK_THREADS)
#      define GC_win32_dll_threads TRUE
#    else
/*
 * `GC_win32_dll_threads` must be set (if needed) at the application
 * initialization time, i.e. before any collector or thread calls.
 * We make it a "dynamic" option only to avoid multiple library versions.
 */
GC_EXTERN GC_bool GC_win32_dll_threads;
#    endif

#    ifdef PARALLEL_MARK
GC_EXTERN int GC_available_markers_m1;

/*
 * The desired amount of marker threads (including the initiating one).
 * Note: the default value (0) means the number of markers should be
 * selected automatically.
 */
GC_EXTERN unsigned GC_required_markers_cnt;

/* The cold end of the stack for markers. */
GC_EXTERN ptr_t GC_marker_sp[MAX_MARKERS - 1];

/*
 * Last known minimum (hottest) address in stack (or `ADDR_LIMIT` if
 * unset) for markers.
 */
GC_EXTERN ptr_t GC_marker_last_stack_min[MAX_MARKERS - 1];

#      ifndef GC_PTHREADS_PARAMARK
/*
 * A table mapping the helper thread id to the mark helper index
 * (linear search is used since the mapping contains only a few entries).
 */
GC_EXTERN thread_id_t GC_marker_Id[MAX_MARKERS - 1];
#      endif

#      if !defined(HAVE_PTHREAD_SETNAME_NP_WITH_TID) && !defined(MSWINCE)
GC_INNER void GC_init_win32_thread_naming(HMODULE hK32);
#      endif
#      ifdef GC_PTHREADS_PARAMARK
GC_INNER void *GC_mark_thread(void *);
#      elif defined(MSWINCE)
GC_INNER DWORD WINAPI GC_mark_thread(LPVOID);
#      else
GC_INNER unsigned __stdcall GC_mark_thread(void *);
#      endif
#    endif /* PARALLEL_MARK */

/*
 * Add a thread to `GC_threads`.  We assume it was not already there.
 * Note: the `id` field should be set by the caller.
 */
GC_INNER GC_thread GC_new_thread(thread_id_t);

GC_INNER void GC_record_stack_base(GC_stack_context_t crtn,
                                   const struct GC_stack_base *sb);

/*
 * This may be called from `DllMain`, and hence operates under unusual
 * constraints.  In particular, it must be lock-free if
 * `GC_win32_dll_threads`.  Always called from the thread being added.
 * If not `GC_win32_dll_threads`, then we already hold the allocator
 * lock except possibly during single-threaded startup code.
 * Does not initialize thread-local free lists.
 */
GC_INNER GC_thread GC_register_my_thread_inner(const struct GC_stack_base *sb,
                                               thread_id_t self_id);

#    ifdef GC_PTHREADS
GC_INNER void GC_win32_cache_self_pthread(thread_id_t);
#    else
/*
 * Delete a thread from `GC_threads`.  We assume it is there.
 * (The code intentionally traps if it was not.)  It is also safe to delete
 * the main thread.  If `GC_win32_dll_threads`, it should be called only
 * from the thread being deleted (except for `DLL_PROCESS_DETACH` case).
 * If a thread has been joined, but we have not yet been notified, then
 * there may be more than one thread in the table with the same thread
 * id - this is OK because we delete a specific one.
 */
GC_INNER void GC_delete_thread(GC_thread);
#    endif

#    ifdef CAN_HANDLE_FORK
/* Prepare for a process fork if requested. */
GC_INNER void GC_setup_atfork(void);
#    endif

#    if !defined(DONT_USE_ATEXIT) || !defined(GC_NO_THREADS_DISCOVERY)
GC_EXTERN thread_id_t GC_main_thread_id;
#    endif

#    ifndef GC_NO_THREADS_DISCOVERY
/*
 * Search in `dll_thread_table` and return the `GC_thread[]` entity
 * corresponding to the given thread `id`.
 * May be called without a lock, but should be called in contexts in
 * those the requested thread cannot be asynchronously deleted, e.g.
 * from the thread itself.
 */
GC_INNER GC_thread GC_win32_dll_lookup_thread(thread_id_t id);
#    endif

#    ifdef MPROTECT_VDB
/*
 * Make sure given thread descriptor is not protected by the VDB
 * implementation.  Used to prevent write faults when the world is
 * (partially) stopped, since it may have been stopped with a system
 * lock held, and that lock may be required for fault handling.
 */
GC_INNER void GC_win32_unprotect_thread(GC_thread);
#    else
#      define GC_win32_unprotect_thread(t) (void)(t)
#    endif /* !MPROTECT_VDB */

#  else
#    define GC_win32_dll_threads FALSE
#  endif /* !GC_WIN32_THREADS */

#  ifdef GC_PTHREADS
#    ifdef GC_WIN32_THREADS
/*
 * Return a `GC_thread` corresponding to a given `pthread_t`, or
 * `NULL` if it is not there.  We assume that this is only called for
 * `pthreads` ids that have not yet terminated or are still joinable,
 * and cannot be terminated concurrently.
 */
GC_INNER GC_thread GC_lookup_by_pthread(pthread_t);
#    else
#      define GC_lookup_by_pthread(t) GC_lookup_thread(t)
#    endif
#  endif /* GC_PTHREADS */

/*
 * Return a `GC_thread` corresponding to a given thread `id`, or
 * `NULL` if it is not there.  Caller holds the allocator lock in the
 * reader mode, at least, or otherwise inhibits updates.  If there is more
 * than one thread with the given `id`, we return the most recent one.
 */
GC_INNER GC_thread GC_lookup_thread(thread_id_t id);

#  define GC_self_thread_inner() GC_lookup_thread(thread_id_self())

/*
 * Wait until an in-progress collection has finished.
 * We hold the allocator lock and repeatedly release it in order to wait.
 * If `wait_for_all`, then we exit with the allocator lock held and
 * no collection is in progress; otherwise we just wait for the current
 * collection to finish.
 */
GC_INNER void GC_wait_for_gc_completion(GC_bool wait_for_all);

#  ifdef NACL
GC_INNER void GC_nacl_initialize_gc_thread(GC_thread);
GC_INNER void GC_nacl_shutdown_gc_thread(void);
#  endif

#  if defined(PTHREAD_STOP_WORLD_IMPL)            \
          && !defined(NO_SIGNALS_UNBLOCK_IN_MAIN) \
      || defined(GC_EXPLICIT_SIGNALS_UNBLOCK)
/*
 * Some targets (e.g., Solaris) might require this to be called when
 * doing thread registering from the thread destructor.
 */
GC_INNER void GC_unblock_gc_signals(void);
#  endif

#  if defined(GC_ENABLE_SUSPEND_THREAD) && defined(SIGNAL_BASED_STOP_WORLD)
GC_INNER void GC_suspend_self_inner(GC_thread me, size_t suspend_cnt);

/*
 * A wrapper over `GC_suspend_self_inner()`.  Similar to
 * `GC_do_blocking_inner()` but assuming the allocator lock is held and
 * `fn` is `GC_suspend_self_inner`.
 */
GC_INNER void GC_suspend_self_blocked(ptr_t thread_me, void *context);
#  endif

#  if defined(GC_PTHREADS) && !defined(PLATFORM_THREADS) \
      && !defined(SN_TARGET_PSP2)

#    ifdef GC_PTHREAD_START_STANDALONE
#      define GC_INNER_PTHRSTART /*< empty */
#    else
#      define GC_INNER_PTHRSTART GC_INNER
#    endif

GC_INNER_PTHRSTART void *GC_CALLBACK
GC_pthread_start_inner(struct GC_stack_base *sb, void *arg);

/*
 * Called from `GC_pthread_start_inner()`.  Defined in `pthread_support.c`
 * file to minimize the number of files included from `pthread_start.c` file
 * (because `sem_t` and `sem_post()` are not used in that file directly).
 */
GC_INNER_PTHRSTART GC_thread
GC_start_rtn_prepare_thread(void *(**pstart)(void *), void **pstart_arg,
                            struct GC_stack_base *sb, void *arg);

/*
 * Called at thread exit.  Never called for main thread.  That is OK,
 * since it results in at most a tiny one-time leak.  And LinuxThreads
 * implementation does not reclaim the primordial (main) thread resources
 * or id anyway.
 */
GC_INNER_PTHRSTART void GC_thread_exit_proc(void *);
#  endif /* GC_PTHREADS */

#  ifdef DARWIN
#    ifndef DARWIN_DONT_PARSE_STACK
GC_INNER ptr_t GC_FindTopOfStack(unsigned long);
#    endif
#    if defined(PARALLEL_MARK) && !defined(GC_NO_THREADS_DISCOVERY)
/* Note: this is used only by `GC_suspend_thread_list()`. */
GC_INNER GC_bool GC_is_mach_marker(thread_act_t);
#    endif
#  endif /* DARWIN */

#  ifdef PTHREAD_STOP_WORLD_IMPL
GC_INNER void GC_stop_init(void);
#  endif

EXTERN_C_END

#endif /* THREADS */

#endif /* GC_PTHREAD_SUPPORT_H */
