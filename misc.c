/*
 * Copyright 1988, 1989 Hans-J. Boehm, Alan J. Demers
 * Copyright (c) 1991-1994 by Xerox Corporation.  All rights reserved.
 * Copyright (c) 1999-2001 by Hewlett-Packard Company. All rights reserved.
 * Copyright (c) 2008-2025 Ivan Maidanski
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

#include "private/gc_pmark.h"

#include <limits.h>
#include <stdarg.h>

#if defined(SOLARIS) && defined(THREADS)
#  include <sys/syscall.h>
#endif

#if defined(CYGWIN) || defined(SYMBIAN) || defined(UNIX_LIKE) \
    || (defined(CONSOLE_LOG) && defined(MSWIN32))
#  include <fcntl.h>
#  include <sys/stat.h>
#endif

#if defined(CONSOLE_LOG) && defined(MSWIN32) && !defined(__GNUC__)
#  include <io.h>
#endif

#ifdef NONSTOP
#  include <floss.h>
#endif

#ifdef THREADS
#  if defined(SN_TARGET_PSP2)
GC_INNER WapiMutex GC_allocate_ml_PSP2 = { 0, NULL };
#  elif defined(GC_DEFN_ALLOCATE_ML) && !defined(USE_RWLOCK) \
      || defined(SN_TARGET_PS3)
#    include <pthread.h>
GC_INNER pthread_mutex_t GC_allocate_ml;
#  else
/*
 * For other platforms with threads, the allocator lock and, possibly,
 * `GC_lock_holder` are defined in the code for multi-threading support.
 */
#  endif
#endif /* THREADS */

#ifdef DYNAMIC_LOADING
/*
 * We need to register the main data segment.  Returns `TRUE` unless
 * this is done implicitly as part of dynamic library registration.
 */
#  define GC_REGISTER_MAIN_STATIC_DATA() GC_register_main_static_data()
#elif defined(GC_DONT_REGISTER_MAIN_STATIC_DATA)
#  define GC_REGISTER_MAIN_STATIC_DATA() FALSE
#else
/*
 * Do not unnecessarily call `GC_register_main_static_data()` in case
 * `dyn_load.c` file is not linked in.
 */
#  define GC_REGISTER_MAIN_STATIC_DATA() TRUE
#endif

#ifdef NEED_CANCEL_DISABLE_COUNT
__thread unsigned char GC_cancel_disable_count = 0;
#endif

struct _GC_arrays GC_arrays /* `= { 0 }` */;

GC_INNER unsigned GC_n_mark_procs = GC_RESERVED_MARK_PROCS;

GC_INNER unsigned GC_n_kinds = GC_N_KINDS_INITIAL_VALUE;

ptr_t GC_stackbottom = NULL;

int GC_dont_gc = FALSE;

int GC_dont_precollect = FALSE;

int GC_quiet = 0;

#if !defined(NO_CLOCK) || !defined(SMALL_CONFIG)
GC_INNER int GC_print_stats = 0;
#endif

#ifdef MAKE_BACK_GRAPH
#  ifdef GC_PRINT_BACK_HEIGHT
GC_INNER GC_bool GC_print_back_height = TRUE;
#  else
GC_INNER GC_bool GC_print_back_height = FALSE;
#  endif
#endif

#ifndef NO_DEBUGGING
#  ifdef GC_DUMP_REGULARLY
GC_INNER GC_bool GC_dump_regularly = TRUE;
#  else
GC_INNER GC_bool GC_dump_regularly = FALSE;
#  endif
#endif

#ifdef KEEP_BACK_PTRS
GC_INNER long GC_backtraces = 0;
#endif

#ifdef FIND_LEAK
int GC_find_leak = 1;
#else
int GC_find_leak = 0;
#endif

#if !defined(NO_FIND_LEAK) && !defined(SHORT_DBG_HDRS)
#  ifdef GC_FINDLEAK_DELAY_FREE
GC_INNER GC_bool GC_findleak_delay_free = TRUE;
#  else
GC_INNER GC_bool GC_findleak_delay_free = FALSE;
#  endif
#endif /* !NO_FIND_LEAK && !SHORT_DBG_HDRS */

#ifndef NO_ALL_INTERIOR_POINTERS
#  ifdef ALL_INTERIOR_POINTERS
int GC_all_interior_pointers = 1;
#  else
int GC_all_interior_pointers = 0;
#  endif
#endif

#ifdef FINALIZE_ON_DEMAND
int GC_finalize_on_demand = 1;
#else
int GC_finalize_on_demand = 0;
#endif

#ifdef JAVA_FINALIZATION
int GC_java_finalization = 1;
#else
int GC_java_finalization = 0;
#endif

/* All accesses to it should be synchronized to avoid data race. */
GC_finalizer_notifier_proc GC_finalizer_notifier
    = (GC_finalizer_notifier_proc)0;

#ifdef GC_FORCE_UNMAP_ON_GCOLLECT
GC_INNER GC_bool GC_force_unmap_on_gcollect = TRUE;
#else
GC_INNER GC_bool GC_force_unmap_on_gcollect = FALSE;
#endif

#ifndef GC_LARGE_ALLOC_WARN_INTERVAL
#  define GC_LARGE_ALLOC_WARN_INTERVAL 5
#endif

#ifndef NO_BLACK_LISTING
GC_INNER long GC_large_alloc_warn_interval = GC_LARGE_ALLOC_WARN_INTERVAL;
#endif

STATIC void *GC_CALLBACK
GC_default_oom_fn(size_t bytes_requested)
{
  UNUSED_ARG(bytes_requested);
  return NULL;
}

/* All accesses to it should be synchronized to avoid data race. */
GC_oom_func GC_oom_fn = GC_default_oom_fn;

#ifdef CAN_HANDLE_FORK
#  ifdef HANDLE_FORK
GC_INNER int GC_handle_fork = 1;
#  else
GC_INNER int GC_handle_fork = FALSE;
#  endif

#elif !defined(HAVE_NO_FORK)
GC_API void GC_CALL
GC_atfork_prepare(void)
{
#  ifdef THREADS
  ABORT("fork() handling unsupported");
#  endif
}

GC_API void GC_CALL
GC_atfork_parent(void)
{
  /* Empty. */
}

GC_API void GC_CALL
GC_atfork_child(void)
{
  /* Empty. */
}
#endif /* !CAN_HANDLE_FORK && !HAVE_NO_FORK */

GC_API void GC_CALL
GC_set_handle_fork(int value)
{
#ifdef CAN_HANDLE_FORK
  if (!GC_is_initialized) {
    /* Map all negative values except for -1 to a positive one. */
    GC_handle_fork = value >= -1 ? value : 1;
  }
#elif defined(THREADS) || (defined(DARWIN) && defined(MPROTECT_VDB))
  if (!GC_is_initialized && value) {
#  ifndef SMALL_CONFIG
    /* Initialize `GC_manual_vdb` and `GC_stderr`. */
    GC_init();
#    ifndef THREADS
    if (GC_manual_vdb)
      return;
#    endif
#  endif
    ABORT("fork() handling unsupported");
  }
#else
  /* No at-fork handler is needed in the single-threaded mode. */
  UNUSED_ARG(value);
#endif
}

/*
 * Set things up so that `GC_size_map[i] >= granules(i)`, but not too
 * much bigger and so that `GC_size_map` contains relatively few
 * distinct entries.  This was originally stolen from Russ Atkinson's
 * Cedar quantization algorithm (but we precompute it).
 */
STATIC void
GC_init_size_map(void)
{
  size_t i = 1;

  /* Map size 0 to something bigger; this avoids problems at lower levels. */
  GC_size_map[0] = 1;

  for (; i <= GRANULES_TO_BYTES(GC_TINY_FREELISTS - 1) - EXTRA_BYTES; i++) {
    GC_size_map[i] = ALLOC_REQUEST_GRANS(i);
#ifndef _MSC_VER
    /* Seems to tickle bug in VC++ 2008 for x86_64. */
    GC_ASSERT(GC_size_map[i] < GC_TINY_FREELISTS);
#endif
  }
  /* We leave the rest of the array to be filled in on demand. */
}

/*
 * The following is a gross hack to deal with a problem that can occur
 * on machines that are sloppy about stack frame sizes, notably SPARC.
 * Bogus pointers may be written to the stack and not cleared for
 * a LONG time, because they always fall into holes in stack frames
 * that are not written.  We partially address this by clearing
 * sections of the stack whenever we get control.
 */

#ifndef SMALL_CLEAR_SIZE
/* Clear this many words of the stack every time. */
#  define SMALL_CLEAR_SIZE 256
#endif

#if defined(ALWAYS_SMALL_CLEAR_STACK) || defined(STACK_NOT_SCANNED)
GC_API void *GC_CALL
GC_clear_stack(void *arg)
{
#  ifndef STACK_NOT_SCANNED
  volatile ptr_t dummy[SMALL_CLEAR_SIZE];

  BZERO(CAST_AWAY_VOLATILE_PVOID(dummy), sizeof(dummy));
#  endif
  return arg;
}
#else

#  ifdef THREADS
/* Clear this much sometimes. */
#    define BIG_CLEAR_SIZE 2048
#  else
#    define DEGRADE_RATE 50
#  endif

#  if defined(__APPLE_CC__) && !GC_CLANG_PREREQ(6, 0)
#    define CLEARSTACK_LIMIT_MODIFIER volatile /*< to workaround some bug */
#  else
#    define CLEARSTACK_LIMIT_MODIFIER /*< empty */
#  endif

EXTERN_C_BEGIN
void *GC_clear_stack_inner(void *, CLEARSTACK_LIMIT_MODIFIER ptr_t);
EXTERN_C_END

#  ifndef ASM_CLEAR_CODE
/*
 * Clear the stack up to about `limit`.  Return `arg`.  This function is
 * not `static` because it could also be erroneously defined in `.S` file,
 * so this error would be caught by the linker.
 */
void *
GC_clear_stack_inner(void *arg, CLEARSTACK_LIMIT_MODIFIER ptr_t limit)
{
#    define CLEAR_SIZE 213 /*< granularity */
  volatile ptr_t dummy[CLEAR_SIZE];

  BZERO(CAST_AWAY_VOLATILE_PVOID(dummy), sizeof(dummy));
  if (HOTTER_THAN((/* no volatile */ ptr_t)limit, GC_approx_sp())) {
    (void)GC_clear_stack_inner(arg, limit);
  }
  /*
   * Make sure the recursive call is not a tail call, and the `bzero` call
   * is not recognized as dead code.
   */
#    if defined(CPPCHECK)
  GC_noop1(ADDR(dummy[0]));
#    else
  GC_noop1(COVERT_DATAFLOW(ADDR(dummy)));
#    endif
  return arg;
}
#  endif /* !ASM_CLEAR_CODE */

#  ifdef THREADS
/* Used to occasionally clear a bigger chunk. */
/* TODO: Should be more random than it is... */
static unsigned
next_random_no(void)
{
#    ifdef AO_HAVE_fetch_and_add1
  static volatile AO_t random_no;

  return (unsigned)AO_fetch_and_add1(&random_no) % 13;
#    else
  static unsigned random_no = 0;

  return (random_no++) % 13;
#    endif
}
#  endif /* THREADS */

GC_API void *GC_CALL
GC_clear_stack(void *arg)
{
  /* Note: this is hotter than the actual stack pointer. */
  ptr_t sp = GC_approx_sp();
#  ifdef THREADS
  volatile ptr_t dummy[SMALL_CLEAR_SIZE];
#  endif

  /*
   * Extra bytes we clear every time.  This clears our own activation
   * record, and should cause more frequent clearing near the cold end
   * of the stack, a good thing.
   */
#  define SLOP 400

  /*
   * We make `GC_high_water` this much hotter than we really saw it,
   * to cover for the GC noise above our current frame.
   */
#  define GC_SLOP 4000

  /*
   * We restart the clearing process after this many bytes of allocation.
   * Otherwise very heavily recursive programs with sparse stacks may
   * result in heaps that grow almost without bounds.  As the heap gets
   * larger, collection frequency decreases, thus clearing frequency
   * would decrease, thus more junk remains accessible, thus the heap
   * gets larger...
   */
#  define CLEAR_THRESHOLD 100000

#  ifdef THREADS
  if (next_random_no() == 0) {
    ptr_t limit = sp;

    MAKE_HOTTER(limit, BIG_CLEAR_SIZE * sizeof(ptr_t));
    /*
     * Make it sufficiently aligned for assembly implementations
     * of `GC_clear_stack_inner`.
     */
    limit = PTR_ALIGN_DOWN(limit, 0x10);
    return GC_clear_stack_inner(arg, limit);
  }
  BZERO(CAST_AWAY_VOLATILE_PVOID(dummy), sizeof(dummy));
#  else
  if (GC_gc_no != GC_stack_last_cleared) {
    /* Start things over, so we clear the entire stack again. */
    if (UNLIKELY(NULL == GC_high_water))
      GC_high_water = (ptr_t)GC_stackbottom;
    GC_min_sp = GC_high_water;
    GC_stack_last_cleared = GC_gc_no;
    GC_bytes_allocd_at_reset = GC_bytes_allocd;
  }
  /* Adjust `GC_high_water`. */
  GC_ASSERT(GC_high_water != NULL);
  MAKE_COOLER(GC_high_water, PTRS_TO_BYTES(DEGRADE_RATE) + GC_SLOP);
  if (HOTTER_THAN(sp, GC_high_water))
    GC_high_water = sp;
  MAKE_HOTTER(GC_high_water, GC_SLOP);
  {
    ptr_t limit = GC_min_sp;

    MAKE_HOTTER(limit, SLOP);
    if (HOTTER_THAN(limit, sp)) {
      limit = PTR_ALIGN_DOWN(limit, 0x10);
      GC_min_sp = sp;
      return GC_clear_stack_inner(arg, limit);
    }
  }
  if (GC_bytes_allocd - GC_bytes_allocd_at_reset > CLEAR_THRESHOLD) {
    /* Restart clearing process, but limit how much clearing we do. */
    GC_min_sp = sp;
    MAKE_HOTTER(GC_min_sp, CLEAR_THRESHOLD / 4);
    if (HOTTER_THAN(GC_min_sp, GC_high_water))
      GC_min_sp = GC_high_water;
    GC_bytes_allocd_at_reset = GC_bytes_allocd;
  }
#  endif
  return arg;
}

#endif /* !ALWAYS_SMALL_CLEAR_STACK && !STACK_NOT_SCANNED */

GC_API void *GC_CALL
GC_base(void *p)
{
  ptr_t r = (ptr_t)p;
  struct hblk *h;
  bottom_index *bi;
  hdr *hhdr;
  ptr_t limit;
  size_t sz;

  if (UNLIKELY(!GC_is_initialized))
    return NULL;
  h = HBLKPTR(r);
  GET_BI(r, bi);
  hhdr = HDR_FROM_BI(bi, r);
  if (NULL == hhdr)
    return NULL;

  /*
   * If it is a pointer to the middle of a large object, then move it
   * to the beginning.
   */
  if (IS_FORWARDING_ADDR_OR_NIL(hhdr)) {
    h = GC_find_starting_hblk(h, &hhdr);
    r = (ptr_t)h;
  }
  if (HBLK_IS_FREE(hhdr))
    return NULL;

  /* Make sure `r` points to the beginning of the object. */
  r = PTR_ALIGN_DOWN(r, sizeof(ptr_t));

  sz = hhdr->hb_sz;
  r -= HBLKDISPL(r) % sz;
  limit = r + sz;
  if ((ADDR_LT((ptr_t)(h + 1), limit) && sz <= HBLKSIZE)
      || ADDR_GE((ptr_t)p, limit))
    return NULL;

  return r;
}

GC_API int GC_CALL
GC_is_heap_ptr(const void *p)
{
  bottom_index *bi;

  GC_ASSERT(GC_is_initialized);
  GET_BI(p, bi);
  return HDR_FROM_BI(bi, p) != NULL;
}

GC_API size_t GC_CALL
GC_size(const void *p)
{
  const hdr *hhdr;

  /* Accept `NULL` for compatibility with `malloc_usable_size()`. */
  if (UNLIKELY(NULL == p))
    return 0;

  hhdr = HDR(p);
  return hhdr->hb_sz;
}

/*
 * These getters remain unsynchronized for compatibility (since some clients
 * could call some of them from a GC callback holding the allocator lock).
 */

GC_API size_t GC_CALL
GC_get_heap_size(void)
{
  /*
   * Ignore the memory space returned to OS (i.e. count only the space
   * owned by the garbage collector).
   */
  return (size_t)(GC_heapsize - GC_unmapped_bytes);
}

GC_API size_t GC_CALL
GC_get_obtained_from_os_bytes(void)
{
  return (size_t)GC_our_mem_bytes;
}

GC_API size_t GC_CALL
GC_get_free_bytes(void)
{
  /* Ignore the memory space returned to OS. */
  return (size_t)(GC_large_free_bytes - GC_unmapped_bytes);
}

GC_API size_t GC_CALL
GC_get_unmapped_bytes(void)
{
  return (size_t)GC_unmapped_bytes;
}

GC_API size_t GC_CALL
GC_get_bytes_since_gc(void)
{
  return (size_t)GC_bytes_allocd;
}

GC_API size_t GC_CALL
GC_get_total_bytes(void)
{
  return (size_t)(GC_bytes_allocd + GC_bytes_allocd_before_gc);
}

#ifndef GC_GET_HEAP_USAGE_NOT_NEEDED

GC_API size_t GC_CALL
GC_get_size_map_at(int i)
{
  if ((unsigned)i > MAXOBJBYTES)
    return GC_SIZE_MAX;
  return GRANULES_TO_BYTES(GC_size_map[i]);
}

GC_API void GC_CALL
GC_get_heap_usage_safe(GC_word *p_heap_size, GC_word *p_free_bytes,
                       GC_word *p_unmapped_bytes, GC_word *p_bytes_since_gc,
                       GC_word *p_total_bytes)
{
  READER_LOCK();
  if (p_heap_size != NULL)
    *p_heap_size = GC_heapsize - GC_unmapped_bytes;
  if (p_free_bytes != NULL)
    *p_free_bytes = GC_large_free_bytes - GC_unmapped_bytes;
  if (p_unmapped_bytes != NULL)
    *p_unmapped_bytes = GC_unmapped_bytes;
  if (p_bytes_since_gc != NULL)
    *p_bytes_since_gc = GC_bytes_allocd;
  if (p_total_bytes != NULL)
    *p_total_bytes = GC_bytes_allocd + GC_bytes_allocd_before_gc;
  READER_UNLOCK();
}

/* Fill in GC statistics provided the destination is of enough size. */
static void
fill_prof_stats(struct GC_prof_stats_s *pstats)
{
  pstats->heapsize_full = GC_heapsize;
  pstats->free_bytes_full = GC_large_free_bytes;
  pstats->unmapped_bytes = GC_unmapped_bytes;
  pstats->bytes_allocd_since_gc = GC_bytes_allocd;
  pstats->allocd_bytes_before_gc = GC_bytes_allocd_before_gc;
  pstats->non_gc_bytes = GC_non_gc_bytes;
  pstats->gc_no = GC_gc_no; /*< could be -1 */
#  ifdef PARALLEL_MARK
  {
    GC_signed_word markers_m1 = (GC_signed_word)GC_markers_m1;

#    ifdef LINT2
    if (markers_m1 < 0)
      ABORT("GC_markers_m1 is negative");
#    endif
    pstats->markers_m1 = (word)markers_m1;
  }
#  else
  /* A single marker. */
  pstats->markers_m1 = 0;
#  endif
  pstats->bytes_reclaimed_since_gc
      = GC_bytes_found > 0 ? (word)GC_bytes_found : 0;
  pstats->reclaimed_bytes_before_gc = GC_reclaimed_bytes_before_gc;
  pstats->expl_freed_bytes_since_gc = GC_bytes_freed; /*< since gc-7.7 */
  pstats->obtained_from_os_bytes = GC_our_mem_bytes;  /*< since gc-8.2 */
}

#  include <string.h> /*< for `memset()` */

GC_API size_t GC_CALL
GC_get_prof_stats(struct GC_prof_stats_s *pstats, size_t stats_sz)
{
  struct GC_prof_stats_s stats;

  READER_LOCK();
  fill_prof_stats(stats_sz >= sizeof(stats) ? pstats : &stats);
  READER_UNLOCK();

  if (stats_sz == sizeof(stats)) {
    return sizeof(stats);
  } else if (stats_sz > sizeof(stats)) {
    /* Fill in the remaining part with -1. */
    memset((char *)pstats + sizeof(stats), 0xff, stats_sz - sizeof(stats));
    return sizeof(stats);
  } else {
    if (LIKELY(stats_sz > 0))
      BCOPY(&stats, pstats, stats_sz);
    return stats_sz;
  }
}

#  ifdef THREADS
GC_API size_t GC_CALL
GC_get_prof_stats_unsafe(struct GC_prof_stats_s *pstats, size_t stats_sz)
{
  struct GC_prof_stats_s stats;

  if (stats_sz >= sizeof(stats)) {
    fill_prof_stats(pstats);
    if (stats_sz > sizeof(stats))
      memset((char *)pstats + sizeof(stats), 0xff, stats_sz - sizeof(stats));
    return sizeof(stats);
  } else {
    if (LIKELY(stats_sz > 0)) {
      fill_prof_stats(&stats);
      BCOPY(&stats, pstats, stats_sz);
    }
    return stats_sz;
  }
}
#  endif /* THREADS */

#endif /* !GC_GET_HEAP_USAGE_NOT_NEEDED */

#if defined(THREADS) && !defined(SIGNAL_BASED_STOP_WORLD)
/* The collector does not use signals to suspend and restart threads. */

GC_API void GC_CALL
GC_set_suspend_signal(int sig)
{
  UNUSED_ARG(sig);
}

GC_API void GC_CALL
GC_set_thr_restart_signal(int sig)
{
  UNUSED_ARG(sig);
}

GC_API int GC_CALL
GC_get_suspend_signal(void)
{
  return -1;
}

GC_API int GC_CALL
GC_get_thr_restart_signal(void)
{
  return -1;
}
#endif /* THREADS && !SIGNAL_BASED_STOP_WORLD */

#if !defined(_MAX_PATH) && defined(ANY_MSWIN)
#  define _MAX_PATH MAX_PATH
#endif

#ifdef GC_READ_ENV_FILE
/* This works for Win32/WinCE for now.  Really useful only for WinCE. */

#  ifndef GC_ENVFILE_MAXLEN
#    define GC_ENVFILE_MAXLEN 0x4000
#  endif

#  define GC_ENV_FILE_EXT ".gc.env"

/* The routine initializes `GC_envfile_content` from the `.gc.env` file. */
STATIC void
GC_envfile_init(void)
{
#  ifdef ANY_MSWIN
  HANDLE hFile;
  char *content;
  size_t ofs, len;
  DWORD nBytesRead;
  TCHAR path[_MAX_PATH + 0x10]; /*< buffer for file path with extension */
  size_t bytes_to_get;

  GC_ASSERT(I_HOLD_LOCK());
  len = (size_t)GetModuleFileName(NULL /* `hModule` */, path, _MAX_PATH + 1);
  /* If `GetModuleFileName()` failed, then len is 0. */
  if (len > 4 && path[len - 4] == (TCHAR)'.') {
    /* Strip the executable file extension. */
    len -= 4;
  }
  BCOPY(TEXT(GC_ENV_FILE_EXT), &path[len], sizeof(TEXT(GC_ENV_FILE_EXT)));
  hFile = CreateFile(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                     NULL /* `lpSecurityAttributes` */, OPEN_EXISTING,
                     FILE_ATTRIBUTE_NORMAL, NULL /* `hTemplateFile` */);
  if (hFile == INVALID_HANDLE_VALUE) {
    /* The file is absent or the operation failed. */
    return;
  }
  len = (size_t)GetFileSize(hFile, NULL);
  if (len <= 1 || len >= GC_ENVFILE_MAXLEN) {
    CloseHandle(hFile);
    /* Invalid file length - ignoring the file contents. */
    return;
  }
  /*
   * At this execution point, `GC_setpagesize()` and `GC_init_win32()`
   * must already be called (for `GET_MEM()` to work correctly).
   */
  GC_ASSERT(GC_page_size != 0);
  bytes_to_get = ROUNDUP_PAGESIZE_IF_MMAP(len + 1);
  content = GC_os_get_mem(bytes_to_get);
  if (content == NULL) {
    CloseHandle(hFile);
    /* An allocation failure. */
    return;
  }
  ofs = 0;
  nBytesRead = (DWORD)-1L;
  /* Last `ReadFile()` call should clear `nBytesRead` on success. */
  while (ReadFile(hFile, content + ofs, (DWORD)(len - ofs + 1), &nBytesRead,
                  NULL /* `lpOverlapped` */)
         && nBytesRead != 0) {
    if ((ofs += (size_t)nBytesRead) > len)
      break;
  }
  CloseHandle(hFile);
  if (ofs != len || nBytesRead != 0) {
    /* TODO: Recycle `content`. */
    /* Read operation has failed - ignoring the file contents. */
    return;
  }
  content[ofs] = '\0';
  while (ofs-- > 0) {
    if (content[ofs] == '\r' || content[ofs] == '\n')
      content[ofs] = '\0';
  }
  GC_ASSERT(NULL == GC_envfile_content);
  GC_envfile_length = len + 1;
  GC_envfile_content = content;
#  endif
}

GC_INNER char *
GC_envfile_getenv(const char *name)
{
  char *p;
  const char *end_of_content;
  size_t namelen;

#  ifndef NO_GETENV
  /* Try the standard `getenv()` first. */
  p = getenv(name);
  if (p != NULL)
    return *p != '\0' ? p : NULL;
#  endif
  p = GC_envfile_content;
  if (NULL == p) {
    /* The `.gc.env` file is absent (or empty). */
    return NULL;
  }
  namelen = strlen(name);
  if (0 == namelen) {
    /* A sanity check. */
    return NULL;
  }
  for (end_of_content = p + GC_envfile_length;
       ADDR_LT((ptr_t)p, (ptr_t)end_of_content); p += strlen(p) + 1) {
    if (strncmp(p, name, namelen) == 0 && *(p += namelen) == '=') {
      /* The match is found; skip "=". */
      p++;
      return *p != '\0' ? p : NULL;
    }
    /* If not matching then skip to the next line. */
  }
  GC_ASSERT(p == end_of_content);
  /* No match is found. */
  return NULL;
}
#endif /* GC_READ_ENV_FILE */

GC_API int GC_CALL
GC_is_init_called(void)
{
  return (int)GC_is_initialized;
}

#if defined(GC_WIN32_THREADS) \
    && ((defined(MSWIN32) && !defined(CONSOLE_LOG)) || defined(MSWINCE))
GC_INNER CRITICAL_SECTION GC_write_cs;
#endif

#ifndef DONT_USE_ATEXIT
STATIC void
GC_exit_check(void)
{
  if (GC_find_leak && !GC_skip_collect_atexit) {
#  ifdef THREADS
    /*
     * Check that the thread executing at-exit functions is the same as
     * the one performed the GC initialization, otherwise the latter
     * thread might already be dead but still registered and this, as
     * a consequence, might cause a signal delivery fail when suspending
     * the threads on platforms that do not guarantee `ESRCH` returned
     * if the signal is not delivered.  It should also prevent
     * "Collecting from unknown thread" abort in `GC_push_all_stacks()`.
     */
    if (!GC_is_main_thread() || !GC_thread_is_registered())
      return;
#  endif
    GC_gcollect();
  }
}
#endif /* !DONT_USE_ATEXIT */

#if defined(UNIX_LIKE) && !defined(NO_DEBUGGING)
static void
looping_handler(int sig)
{
  GC_err_printf("Caught signal %d: looping in handler\n", sig);
  for (;;) {
    /* Empty. */
  }
}

static GC_bool installed_looping_handler = FALSE;

static void
maybe_install_looping_handler(void)
{
  /*
   * Install looping handler before the write fault handler,
   * so we handle write faults correctly.
   */
  if (!installed_looping_handler && GETENV("GC_LOOP_ON_ABORT") != NULL) {
    GC_set_and_save_fault_handler(looping_handler);
    installed_looping_handler = TRUE;
  }
}

#else /* !UNIX_LIKE */
#  define maybe_install_looping_handler()
#endif

#define GC_DEFAULT_STDERR_FD 2
#ifdef KOS
#  define GC_DEFAULT_STDOUT_FD GC_DEFAULT_STDERR_FD
#else
#  define GC_DEFAULT_STDOUT_FD 1
#endif

#if !defined(OS2) && !defined(GC_ANDROID_LOG) && !defined(NN_PLATFORM_CTR) \
    && !defined(NINTENDO_SWITCH)                                           \
    && (!defined(MSWIN32) || defined(CONSOLE_LOG)) && !defined(MSWINCE)
STATIC int GC_stdout = GC_DEFAULT_STDOUT_FD;
STATIC int GC_stderr = GC_DEFAULT_STDERR_FD;
STATIC int GC_log = GC_DEFAULT_STDERR_FD;

#  ifndef MSWIN32
GC_API void GC_CALL
GC_set_log_fd(int fd)
{
  GC_log = fd;
}
#  endif
#endif

#ifdef MSGBOX_ON_ERROR
STATIC void
GC_win32_MessageBoxA(const char *msg, const char *caption, unsigned flags)
{
#  ifndef DONT_USE_USER32_DLL
  /* Use static binding to `user32.dll` file. */
  (void)MessageBoxA(NULL, msg, caption, flags);
#  else
  /* This simplifies linking - resolve `MessageBoxA()` at run-time. */
  HINSTANCE hU32 = LoadLibrary(TEXT("user32.dll"));
  if (hU32) {
    FARPROC pfn = GetProcAddress(hU32, "MessageBoxA");
    if (pfn)
      (void)(*(int(WINAPI *)(HWND, LPCSTR, LPCSTR, UINT))(GC_funcptr_uint)pfn)(
          NULL /* `hWnd` */, msg, caption, flags);
    (void)FreeLibrary(hU32);
  }
#  endif
}
#endif /* MSGBOX_ON_ERROR */

#if defined(THREADS) && defined(UNIX_LIKE) && !defined(NO_GETCONTEXT)
static void
callee_saves_pushed_dummy_fn(ptr_t data, void *context)
{
  UNUSED_ARG(data);
  UNUSED_ARG(context);
}
#endif

#ifdef MANUAL_VDB
static GC_bool manual_vdb_allowed = TRUE;
#else
static GC_bool manual_vdb_allowed = FALSE;
#endif

GC_API void GC_CALL
GC_set_manual_vdb_allowed(int value)
{
  manual_vdb_allowed = (GC_bool)value;
}

GC_API int GC_CALL
GC_get_manual_vdb_allowed(void)
{
  return (int)manual_vdb_allowed;
}

GC_API unsigned GC_CALL
GC_get_supported_vdbs(void)
{
#ifdef GC_DISABLE_INCREMENTAL
  return GC_VDB_NONE;
#else
#  if defined(CPPCHECK)
  /* Workaround a warning about redundant `| 0`. */
  volatile unsigned zero = 0;
#  endif
  return
#  if defined(CPPCHECK)
      zero
#  else
      0
#  endif
#  ifndef NO_MANUAL_VDB
      | GC_VDB_MANUAL
#  endif
#  ifdef DEFAULT_VDB
      | GC_VDB_DEFAULT
#  endif
#  ifdef MPROTECT_VDB
      | GC_VDB_MPROTECT
#  endif
#  ifdef GWW_VDB
      | GC_VDB_GWW
#  endif
#  ifdef PROC_VDB
      | GC_VDB_PROC
#  endif
#  ifdef SOFT_VDB
      | GC_VDB_SOFT
#  endif
      ;
#endif
}

#ifndef GC_DISABLE_INCREMENTAL
static void
set_incremental_mode_on(void)
{
  GC_ASSERT(I_HOLD_LOCK());
#  ifndef NO_MANUAL_VDB
  if (manual_vdb_allowed) {
    GC_manual_vdb = TRUE;
    GC_incremental = TRUE;
  } else
#  endif
  /* else */ {
    /*
     * For `GWW_VDB` on Win32, this needs to happen before any heap memory
     * is allocated.
     */
    GC_incremental = GC_dirty_init();
  }
}
#endif /* !GC_DISABLE_INCREMENTAL */

STATIC word
GC_parse_mem_size_arg(const char *str)
{
  word result;
  char *endptr;
  char ch;

  if ('\0' == *str)
    return GC_WORD_MAX; /*< bad value */
  result = (word)STRTOULL(str, &endptr, 10);
  ch = *endptr;
  if (ch != '\0') {
    if (*(endptr + 1) != '\0')
      return GC_WORD_MAX;
    /* Allow "k", "M" or "G" suffix. */
    switch (ch) {
    case 'K':
    case 'k':
      result <<= 10;
      break;
#if CPP_WORDSZ >= 32
    case 'M':
    case 'm':
      result <<= 20;
      break;
    case 'G':
    case 'g':
      result <<= 30;
      break;
#endif
    default:
      result = GC_WORD_MAX;
    }
  }
  return result;
}

#define GC_LOG_STD_NAME "gc.log"

GC_API void GC_CALL
GC_init(void)
{
  word initial_heap_sz;
  IF_CANCEL(int cancel_state;)

  if (LIKELY(GC_is_initialized))
    return;
#ifdef REDIRECT_MALLOC
  {
    static GC_bool init_started = FALSE;

    if (init_started)
      ABORT("Redirected malloc() called during GC init");
    init_started = TRUE;
  }
#endif

#if defined(GC_INITIAL_HEAP_SIZE) && !defined(CPPCHECK)
  initial_heap_sz = GC_INITIAL_HEAP_SIZE;
#else
  initial_heap_sz = MINHINCR * HBLKSIZE;
#endif

  DISABLE_CANCEL(cancel_state);
  /*
   * Note that although we are nominally called with the allocator lock
   * held, now it is only really acquired once a second thread is created.
   * And the initialization code needs to run before then.  Thus we really
   * do not hold any locks, and can safely initialize them here.
   */
#ifdef THREADS
#  ifndef GC_ALWAYS_MULTITHREADED
  GC_ASSERT(!GC_need_to_lock);
#  endif
  {
#  if !defined(GC_BUILTIN_ATOMIC) && defined(HP_PA) \
      && (defined(USE_SPIN_LOCK) || defined(NEED_FAULT_HANDLER_LOCK))
    AO_TS_t ts_init = AO_TS_INITIALIZER;

    /* Arrays can only be initialized when declared. */
#    ifdef USE_SPIN_LOCK
    BCOPY(&ts_init, (/* no volatile */ void *)&GC_allocate_lock,
          sizeof(GC_allocate_lock));
#    endif
#    ifdef NEED_FAULT_HANDLER_LOCK
    BCOPY(&ts_init, (/* no volatile */ void *)&GC_fault_handler_lock,
          sizeof(GC_fault_handler_lock));
#    endif
#  else
#    ifdef USE_SPIN_LOCK
    GC_allocate_lock = AO_TS_INITIALIZER;
#    endif
#    ifdef NEED_FAULT_HANDLER_LOCK
    GC_fault_handler_lock = AO_TS_INITIALIZER;
#    endif
#  endif
  }
#  ifdef SN_TARGET_PS3
  {
    pthread_mutexattr_t mattr;

    if (pthread_mutexattr_init(&mattr) != 0)
      ABORT("pthread_mutexattr_init failed");
    if (pthread_mutex_init(&GC_allocate_ml, &mattr) != 0)
      ABORT("pthread_mutex_init failed");
    (void)pthread_mutexattr_destroy(&mattr);
  }
#  endif
#endif /* THREADS */
#if defined(GC_WIN32_THREADS) && !defined(GC_PTHREADS)
#  ifndef SPIN_COUNT
#    define SPIN_COUNT 4000
#  endif
#  ifdef USE_RWLOCK
  /* TODO: Probably use `SRWLOCK_INIT` instead. */
  InitializeSRWLock(&GC_allocate_ml);
#  elif defined(MSWINRT_FLAVOR)
  InitializeCriticalSectionAndSpinCount(&GC_allocate_ml, SPIN_COUNT);
#  else
  {
#    ifndef MSWINCE
    FARPROC pfn = 0;
    HMODULE hK32 = GetModuleHandle(TEXT("kernel32.dll"));
    if (hK32)
      pfn = GetProcAddress(hK32, "InitializeCriticalSectionAndSpinCount");
    if (pfn) {
      (*(BOOL(WINAPI *)(LPCRITICAL_SECTION, DWORD))(GC_funcptr_uint)pfn)(
          &GC_allocate_ml, SPIN_COUNT);
    } else
#    endif /* !MSWINCE */
      /* else */ InitializeCriticalSection(&GC_allocate_ml);
  }
#  endif
#endif /* GC_WIN32_THREADS && !GC_PTHREADS */
#if defined(GC_WIN32_THREADS) \
    && ((defined(MSWIN32) && !defined(CONSOLE_LOG)) || defined(MSWINCE))
  InitializeCriticalSection(&GC_write_cs);
#endif
#if defined(GC_ASSERTIONS) && defined(GC_ALWAYS_MULTITHREADED)
  /* Just to set `GC_lock_holder`. */
  LOCK();
#endif
#ifdef DYNAMIC_POINTER_MASK
  if (0 == GC_pointer_mask)
    GC_pointer_mask = GC_WORD_MAX;
#endif
  GC_setpagesize();
#if defined(MSWIN32) && !defined(GC_WINNT)
  GC_init_win32();
#endif
#ifdef GC_READ_ENV_FILE
  GC_envfile_init();
#endif
#if !defined(NO_CLOCK) || !defined(SMALL_CONFIG)
#  ifdef GC_PRINT_VERBOSE_STATS
  /*
   * This is useful for debugging and profiling on platforms with
   * missing `getenv()` (like WinCE).
   */
  GC_print_stats = VERBOSE;
#  else
  if (GETENV("GC_PRINT_VERBOSE_STATS") != NULL) {
    GC_print_stats = VERBOSE;
  } else if (GETENV("GC_PRINT_STATS") != NULL) {
    GC_print_stats = 1;
  }
#  endif
#endif
#if ((defined(UNIX_LIKE) && !defined(GC_ANDROID_LOG))                 \
     || (defined(CONSOLE_LOG) && defined(MSWIN32)) || defined(CYGWIN) \
     || defined(SYMBIAN))                                             \
    && !defined(SMALL_CONFIG)
  {
    const char *fname = TRUSTED_STRING(GETENV("GC_LOG_FILE"));
#  ifdef GC_LOG_TO_FILE_ALWAYS
    if (NULL == fname)
      fname = GC_LOG_STD_NAME;
#  else
    if (fname != NULL)
#  endif
    {
#  if defined(_MSC_VER)
      int log_d = _open(fname, O_CREAT | O_WRONLY | O_APPEND);
#  else
      int log_d = open(fname, O_CREAT | O_WRONLY | O_APPEND, 0644);
#  endif
      if (log_d < 0) {
        GC_err_printf("Failed to open %s as log file\n", fname);
      } else {
        const char *str;
        GC_log = log_d;
        str = GETENV("GC_ONLY_LOG_TO_FILE");
#  ifdef GC_ONLY_LOG_TO_FILE
        /*
         * The similar environment variable set to "0"
         * overrides the effect of the macro defined.
         */
        if (str != NULL && str[0] == '0' && str[1] == '\0')
#  else
        /*
         * Otherwise setting the environment variable to anything other
         * than "0" will prevent from redirecting `stdout` and `stderr`
         * to the collector log file.
         */
        if (str == NULL || (str[0] == '0' && str[1] == '\0'))
#  endif
        {
          GC_stdout = log_d;
          GC_stderr = log_d;
        }
      }
    }
  }
#endif
#if !defined(NO_DEBUGGING) && !defined(GC_DUMP_REGULARLY)
  if (GETENV("GC_DUMP_REGULARLY") != NULL) {
    GC_dump_regularly = TRUE;
  }
#endif
#ifdef KEEP_BACK_PTRS
  {
    const char *str = GETENV("GC_BACKTRACES");

    if (str != NULL) {
      GC_backtraces = atol(str);
      if (str[0] == '\0')
        GC_backtraces = 1;
    }
  }
#endif
#ifndef NO_FIND_LEAK
  if (GETENV("GC_FIND_LEAK") != NULL) {
    GC_find_leak = 1;
  }
#  ifndef SHORT_DBG_HDRS
  if (GETENV("GC_FINDLEAK_DELAY_FREE") != NULL) {
    GC_findleak_delay_free = TRUE;
  }
#  endif
#endif
#ifndef NO_ALL_INTERIOR_POINTERS
  if (GETENV("GC_ALL_INTERIOR_POINTERS") != NULL) {
    GC_all_interior_pointers = 1;
  }
#endif
  if (GETENV("GC_DONT_GC") != NULL) {
#if defined(LINT2) \
    && !(defined(GC_ASSERTIONS) && defined(GC_ALWAYS_MULTITHREADED))
    GC_disable();
#else
    GC_dont_gc = 1;
#endif
  }
#if !defined(SMALL_CONFIG) && !defined(GC_PRINT_BACK_HEIGHT)
  if (GETENV("GC_PRINT_BACK_HEIGHT") != NULL) {
#  ifdef MAKE_BACK_GRAPH
    GC_print_back_height = TRUE;
#  else
    GC_err_printf("Back height is not available!\n");
#  endif
  }
#endif
  {
    const char *str = GETENV("GC_TRACE");

    if (str != NULL) {
#ifndef ENABLE_TRACE
      WARN("Tracing not enabled: Ignoring GC_TRACE value\n", 0);
#else
      ptr_t p = MAKE_CPTR(STRTOULL(str, NULL, 16));

      if (ADDR(p) < 0x1000)
        WARN("Unlikely trace address: %p\n", p);
      GC_trace_ptr = p;
#endif
    }
  }
#ifdef GC_COLLECT_AT_MALLOC
  {
    const char *str = GETENV("GC_COLLECT_AT_MALLOC");

    if (str != NULL) {
      size_t min_lb = (size_t)STRTOULL(str, NULL, 10);

      if (min_lb > 0)
        GC_dbg_collect_at_malloc_min_lb = min_lb;
    }
  }
#endif
#if !defined(GC_DISABLE_INCREMENTAL) && !defined(NO_CLOCK)
  {
    const char *str = GETENV("GC_PAUSE_TIME_TARGET");

    if (str != NULL) {
      long time_limit = atol(str);

      if (time_limit > 0) {
        GC_time_limit = (unsigned long)time_limit;
      }
    }
  }
#endif
#ifndef SMALL_CONFIG
  {
    const char *str = GETENV("GC_FULL_FREQUENCY");

    if (str != NULL) {
      int full_freq = atoi(str);

      if (full_freq > 0)
        GC_full_freq = full_freq;
    }
  }
#endif
#ifndef NO_BLACK_LISTING
  {
    char const *str = GETENV("GC_LARGE_ALLOC_WARN_INTERVAL");

    if (str != NULL) {
      long interval = atol(str);

      if (interval <= 0) {
        WARN("GC_LARGE_ALLOC_WARN_INTERVAL environment variable has"
             " bad value - ignoring\n",
             0);
      } else {
        GC_large_alloc_warn_interval = interval;
      }
    }
  }
#endif
  {
    const char *str = GETENV("GC_FREE_SPACE_DIVISOR");

    if (str != NULL) {
      int space_divisor = atoi(str);

      if (space_divisor > 0)
        GC_free_space_divisor = (unsigned)space_divisor;
    }
  }
#ifdef USE_MUNMAP
  {
    const char *str = GETENV("GC_UNMAP_THRESHOLD");

    if (str != NULL) {
      if (str[0] == '0' && str[1] == '\0') {
        /* "0" is used to disable unmapping. */
        GC_unmap_threshold = 0;
      } else {
        int unmap_threshold = atoi(str);

        if (unmap_threshold > 0)
          GC_unmap_threshold = (unsigned)unmap_threshold;
      }
    }
  }
  {
    const char *str = GETENV("GC_FORCE_UNMAP_ON_GCOLLECT");

    if (str != NULL) {
      if (str[0] == '0' && str[1] == '\0') {
        /* "0" is used to turn off the mode. */
        GC_force_unmap_on_gcollect = FALSE;
      } else {
        GC_force_unmap_on_gcollect = TRUE;
      }
    }
  }
  {
    const char *str = GETENV("GC_USE_ENTIRE_HEAP");

    if (str != NULL) {
      if (str[0] == '0' && str[1] == '\0') {
        /* "0" is used to turn off the mode. */
        GC_use_entire_heap = FALSE;
      } else {
        GC_use_entire_heap = TRUE;
      }
    }
  }
#endif
#if !defined(NO_DEBUGGING) && !defined(NO_CLOCK)
  GET_TIME(GC_init_time);
#endif
  maybe_install_looping_handler();
#if ALIGNMENT > GC_DS_TAGS
  /* Adjust normal object descriptor for extra allocation. */
  if (EXTRA_BYTES != 0)
    GC_obj_kinds[NORMAL].ok_descriptor
        = ((~(word)ALIGNMENT) + 1) | GC_DS_LENGTH;
#endif
  GC_exclude_static_roots_inner(beginGC_arrays, endGC_arrays);
  GC_exclude_static_roots_inner(beginGC_obj_kinds, endGC_obj_kinds);
#if defined(USE_PROC_FOR_LIBRARIES) && defined(LINUX) && defined(THREADS)
  /*
   * TODO: `USE_PROC_FOR_LIBRARIES` with LinuxThreads performs poorly!
   * If thread stacks are cached, they tend to be scanned in entirety
   * as part of the root set.  This will grow them to maximum size, and
   * is generally not desirable.
   */
#endif
#if !defined(THREADS) || !(defined(SN_TARGET_PS3) || defined(SN_TARGET_PSP2))
  if (NULL == GC_stackbottom) {
    GC_stackbottom = GC_get_main_stack_base();
#  if (defined(LINUX) || defined(HPUX)) && defined(IA64)
    GC_register_stackbottom = GC_get_register_stack_base();
#  endif
  } else {
#  if (defined(LINUX) || defined(HPUX)) && defined(IA64)
    if (NULL == GC_register_stackbottom) {
      WARN("GC_register_stackbottom should be set with GC_stackbottom\n", 0);
      /*
       * The following may fail, since we may rely on alignment properties
       * that may not hold with `GC_stackbottom` value set by client.
       */
      GC_register_stackbottom = GC_get_register_stack_base();
    }
#  endif
  }
#endif
#if !defined(CPPCHECK)
  GC_STATIC_ASSERT(sizeof(size_t) <= sizeof(ptrdiff_t));
#  ifdef AO_HAVE_store
  /*
   * As of now, `hb_descr`, `mse_descr` and `hb_marks[i]` might be treated
   * as variables of `word` type but might be accessed atomically.
   */
  GC_STATIC_ASSERT(sizeof(AO_t) == sizeof(word));
#  endif
  GC_STATIC_ASSERT(sizeof(ptrdiff_t) == sizeof(word));
  GC_STATIC_ASSERT(sizeof(GC_signed_word) == sizeof(word));
  GC_STATIC_ASSERT(sizeof(word) * 8 == CPP_WORDSZ);
  GC_STATIC_ASSERT(sizeof(ptr_t) * 8 == CPP_PTRSZ);
  GC_STATIC_ASSERT(sizeof(ptr_t) == sizeof(GC_uintptr_t));
  GC_STATIC_ASSERT(sizeof(GC_oom_func) == sizeof(GC_funcptr_uint));
#  ifdef FUNCPTR_IS_DATAPTR
  GC_STATIC_ASSERT(sizeof(ptr_t) == sizeof(GC_funcptr_uint));
#  endif
  GC_STATIC_ASSERT((word)(-1) > (word)0); /*< `word` should be unsigned */
  /*
   * We no longer check for `(void *)-1 > NULL` since all pointers
   * are explicitly cast to `word` in every less/greater comparison.
   */
  GC_STATIC_ASSERT((GC_signed_word)(-1) < (GC_signed_word)0);
#endif
  GC_STATIC_ASSERT(sizeof(struct hblk) == HBLKSIZE);
#ifndef THREADS
  GC_ASSERT(!HOTTER_THAN(GC_stackbottom, GC_approx_sp()));
#endif
  GC_init_headers();
#ifdef SEARCH_FOR_DATA_START
  /*
   * For `MPROTECT_VDB`, the temporary fault handler should be installed
   * first, before the write fault one in `GC_dirty_init`.
   */
  if (GC_REGISTER_MAIN_STATIC_DATA())
    GC_init_linux_data_start();
#endif
#ifndef GC_DISABLE_INCREMENTAL
  if (GC_incremental || GETENV("GC_ENABLE_INCREMENTAL") != NULL) {
    set_incremental_mode_on();
    GC_ASSERT(0 == GC_bytes_allocd);
  }
#endif

  /*
   * Add the initial guess of root sets.  Do this first, since `sbrk(0)`
   * might be used.
   */
  if (GC_REGISTER_MAIN_STATIC_DATA())
    GC_register_data_segments();

  GC_bl_init();
  GC_mark_init();
  {
    const char *str = GETENV("GC_INITIAL_HEAP_SIZE");

    if (str != NULL) {
      word value = GC_parse_mem_size_arg(str);

      if (GC_WORD_MAX == value) {
        WARN("Bad initial heap size %s - ignoring\n", str);
      } else {
        initial_heap_sz = value;
      }
    }
  }
  {
    const char *str = GETENV("GC_MAXIMUM_HEAP_SIZE");

    if (str != NULL) {
      word max_heap_sz = GC_parse_mem_size_arg(str);

      if (max_heap_sz < initial_heap_sz || GC_WORD_MAX == max_heap_sz) {
        WARN("Bad maximum heap size %s - ignoring\n", str);
      } else {
        if (0 == GC_max_retries)
          GC_max_retries = 2;
        GC_set_max_heap_size(max_heap_sz);
      }
    }
  }
  if (initial_heap_sz != 0) {
    if (!GC_expand_hp_inner(divHBLKSZ(initial_heap_sz))) {
      GC_err_printf("Can't start up: not enough memory\n");
      EXIT();
    } else {
      GC_requested_heapsize += initial_heap_sz;
    }
  }
#ifndef NO_ALL_INTERIOR_POINTERS
  if (GC_all_interior_pointers)
    GC_initialize_offsets();
#endif
  GC_register_displacement_inner(0);
#ifdef REDIR_MALLOC_AND_LINUX_THREADS
  if (!GC_all_interior_pointers) {
    /* TLS ABI uses "pointer-sized" offsets for `dtv`. */
    GC_register_displacement_inner(sizeof(void *));
  }
#endif
  GC_init_size_map();
  GC_is_initialized = TRUE;
#ifdef THREADS
#  if defined(LINT2) \
      && !(defined(GC_ASSERTIONS) && defined(GC_ALWAYS_MULTITHREADED))
  LOCK();
  GC_thr_init();
  UNLOCK();
#  else
  GC_thr_init();
#  endif
#endif
  COND_DUMP;
  /* Get black list set up and/or the incremental GC started. */
  if (!GC_dont_precollect || GC_incremental) {
#if defined(DYNAMIC_LOADING) && defined(DARWIN)
    GC_ASSERT(0 == GC_bytes_allocd);
#endif
    GC_gcollect_inner();
  }
#if defined(GC_ASSERTIONS) && defined(GC_ALWAYS_MULTITHREADED)
  UNLOCK();
#endif
#if defined(THREADS) && defined(UNIX_LIKE) && !defined(NO_GETCONTEXT)
  /* Ensure `getcontext_works` is set to avoid potential data race. */
  if (GC_dont_gc || GC_dont_precollect)
    GC_with_callee_saves_pushed(callee_saves_pushed_dummy_fn, NULL);
#endif
#ifndef DONT_USE_ATEXIT
  if (GC_find_leak) {
    /*
     * This is to give us at least one chance to detect leaks.
     * This may report some very benign leaks, but...
     */
    atexit(GC_exit_check);
  }
#endif
  /*
   * The rest of this again assumes we do not really hold the allocator
   * lock.
   */

#ifdef THREADS
  /* Initialize thread-local allocation. */
  GC_init_parallel();
#endif

#if defined(DYNAMIC_LOADING) && defined(DARWIN)
  /*
   * This must be called *without* the allocator lock held and before
   * any threads are created.
   */
  GC_init_dyld();
#endif
  RESTORE_CANCEL(cancel_state);
  /*
   * It is not safe to allocate any object till completion of `GC_init`
   * (in particular by `GC_thr_init`), i.e. before `GC_init_dyld()` call
   * and initialization of the incremental mode (if any).
   */
#if defined(GWW_VDB) && !defined(KEEP_BACK_PTRS)
  GC_ASSERT(GC_bytes_allocd + GC_bytes_allocd_before_gc == 0);
#endif
}

GC_API void GC_CALL
GC_enable_incremental(void)
{
#if !defined(GC_DISABLE_INCREMENTAL) && !defined(KEEP_BACK_PTRS)
  /*
   * If we are keeping back pointers, the collector itself dirties all pages
   * on which objects have been marked, making the incremental collection
   * pointless.
   */
  if (!GC_find_leak_inner && NULL == GETENV("GC_DISABLE_INCREMENTAL")) {
    LOCK();
    if (!GC_incremental) {
      GC_setpagesize();
      /* TODO: Should we skip enabling incremental if win32s? */

      /* Install the looping handler before write fault handler! */
      maybe_install_looping_handler();
      if (!GC_is_initialized) {
        /* Indicate the intention to turn it on. */
        GC_incremental = TRUE;
        UNLOCK();
        GC_init();
        LOCK();
      } else {
        set_incremental_mode_on();
      }
      /* Cannot easily do it if `GC_dont_gc`. */
      if (GC_incremental && !GC_dont_gc) {
        IF_CANCEL(int cancel_state;)

        DISABLE_CANCEL(cancel_state);
        if (GC_bytes_allocd > 0) {
          /* There may be unmarked reachable objects. */
          GC_gcollect_inner();
        } else {
          /*
           * We are OK in assuming everything is clean since nothing can
           * point to an unmarked object.
           */
#  ifdef CHECKSUMS
          GC_read_dirty(FALSE);
#  else
          GC_read_dirty(TRUE);
#  endif
        }
        RESTORE_CANCEL(cancel_state);
      }
    }
    UNLOCK();
    return;
  }
#endif
  GC_init();
}

GC_API void GC_CALL
GC_start_mark_threads(void)
{
#ifdef PARALLEL_MARK
  IF_CANCEL(int cancel_state;)

  DISABLE_CANCEL(cancel_state);
  LOCK();
  GC_start_mark_threads_inner();
  UNLOCK();
  RESTORE_CANCEL(cancel_state);
#else
  /* No action since parallel markers are disabled (or no POSIX `fork`). */
  GC_ASSERT(I_DONT_HOLD_LOCK());
#endif
}

#ifndef GC_NO_DEINIT
GC_API void GC_CALL
GC_deinit(void)
{
  /* Prevent duplicate resource close. */
  if (!GC_is_initialized)
    return;

  BZERO(&GC_arrays, sizeof(GC_arrays)); /*< clears GC_is_initialized */
  GC_gc_no = 0;
  GC_dont_gc = FALSE;
  GC_non_gc_bytes = 0;
  GC_reset_obj_kinds();
  GC_n_mark_procs = GC_RESERVED_MARK_PROCS;
  GC_reset_freelist();
#  ifdef CHECKSUMS
  GC_reset_check_page();
#  endif
#  ifdef THREADS
  GC_reset_threads();
#  endif
#  ifdef THREAD_LOCAL_ALLOC
  GC_reset_thread_local_initialization();
#  endif
#  if defined(GC_WIN32_THREADS) && (defined(MSWIN32) || defined(MSWINCE))
#    if !defined(CONSOLE_LOG) || defined(MSWINCE)
  DeleteCriticalSection(&GC_write_cs);
#    endif
#    if !defined(GC_PTHREADS) && !defined(USE_RWLOCK)
  DeleteCriticalSection(&GC_allocate_ml);
#    endif
#  endif
}
#endif

#if (defined(MSWIN32) && !defined(CONSOLE_LOG)) || defined(MSWINCE)

STATIC HANDLE GC_log = 0;

#  ifdef THREADS
#    if defined(PARALLEL_MARK) && !defined(GC_ALWAYS_MULTITHREADED)
#      define IF_NEED_TO_LOCK(x)            \
        if (GC_parallel || GC_need_to_lock) \
        x
#    else
#      define IF_NEED_TO_LOCK(x) \
        if (GC_need_to_lock)     \
        x
#    endif
#  else
#    define IF_NEED_TO_LOCK(x)
#  endif /* !THREADS */

#  ifdef MSWINRT_FLAVOR
#    include <windows.storage.h>

/*
 * This API function is defined in platform `roapi.h` file, but we cannot
 * include it here since it does not compile in C.
 */
DECLSPEC_IMPORT HRESULT WINAPI RoGetActivationFactory(HSTRING appDataClassName,
                                                      REFIID iid,
                                                      void **factory);

static GC_bool
getWinRTLogPath(wchar_t *buf, size_t bufLen)
{
  static const GUID kIID_IApplicationDataStatics
      = { 0x5612147B, 0xE843, 0x45E3, 0x94, 0xD8, 0x06,
          0x16,       0x9E,   0x3C,   0x8E, 0x17 };
  static const GUID kIID_IStorageItem
      = { 0x4207A996, 0xCA2F, 0x42F7, 0xBD, 0xE8, 0x8B,
          0x10,       0x45,   0x7A,   0x7F, 0x30 };
  GC_bool result = FALSE;
  HSTRING_HEADER appDataClassNameHeader;
  HSTRING appDataClassName;
  __x_ABI_CWindows_CStorage_CIApplicationDataStatics *appDataStatics = 0;

  GC_ASSERT(bufLen > 0);
  if (SUCCEEDED(WindowsCreateStringReference(
          RuntimeClass_Windows_Storage_ApplicationData,
          (sizeof(RuntimeClass_Windows_Storage_ApplicationData) - 1)
              / sizeof(wchar_t),
          &appDataClassNameHeader, &appDataClassName))
      && SUCCEEDED(RoGetActivationFactory(
          appDataClassName, &kIID_IApplicationDataStatics, &appDataStatics))) {
    __x_ABI_CWindows_CStorage_CIApplicationData *appData = NULL;
    __x_ABI_CWindows_CStorage_CIStorageFolder *tempFolder = NULL;
    __x_ABI_CWindows_CStorage_CIStorageItem *tempFolderItem = NULL;
    HSTRING tempPath = NULL;

    if (SUCCEEDED(
            appDataStatics->lpVtbl->get_Current(appDataStatics, &appData))
        && SUCCEEDED(
            appData->lpVtbl->get_TemporaryFolder(appData, &tempFolder))
        && SUCCEEDED(tempFolder->lpVtbl->QueryInterface(
            tempFolder, &kIID_IStorageItem, &tempFolderItem))
        && SUCCEEDED(
            tempFolderItem->lpVtbl->get_Path(tempFolderItem, &tempPath))) {
      UINT32 tempPathLen;
      const wchar_t *tempPathBuf
          = WindowsGetStringRawBuffer(tempPath, &tempPathLen);

      buf[0] = '\0';
      if (wcsncat_s(buf, bufLen, tempPathBuf, tempPathLen) == 0
          && wcscat_s(buf, bufLen, L"\\") == 0
          && wcscat_s(buf, bufLen, TEXT(GC_LOG_STD_NAME)) == 0)
        result = TRUE;
      WindowsDeleteString(tempPath);
    }

    if (tempFolderItem != NULL)
      tempFolderItem->lpVtbl->Release(tempFolderItem);
    if (tempFolder != NULL)
      tempFolder->lpVtbl->Release(tempFolder);
    if (appData != NULL)
      appData->lpVtbl->Release(appData);
    appDataStatics->lpVtbl->Release(appDataStatics);
  }
  return result;
}
#  endif /* MSWINRT_FLAVOR */

STATIC HANDLE
GC_CreateLogFile(void)
{
  HANDLE hFile;
#  ifdef MSWINRT_FLAVOR
  TCHAR pathBuf[_MAX_PATH + 0x10]; /*< buffer for file path plus extension */

  hFile = INVALID_HANDLE_VALUE;
  if (getWinRTLogPath(pathBuf, _MAX_PATH + 1)) {
    CREATEFILE2_EXTENDED_PARAMETERS extParams;

    BZERO(&extParams, sizeof(extParams));
    extParams.dwSize = sizeof(extParams);
    extParams.dwFileAttributes = FILE_ATTRIBUTE_NORMAL;
    extParams.dwFileFlags
        = GC_print_stats == VERBOSE ? 0 : FILE_FLAG_WRITE_THROUGH;
    hFile = CreateFile2(pathBuf, GENERIC_WRITE, FILE_SHARE_READ, CREATE_ALWAYS,
                        &extParams);
  }

#  else
  TCHAR *logPath;
#    if defined(NO_GETENV_WIN32) && defined(CPPCHECK)
#      define appendToFile FALSE
#    else
  BOOL appendToFile = FALSE;
#    endif
#    if !defined(NO_GETENV_WIN32) || !defined(OLD_WIN32_LOG_FILE)
  TCHAR pathBuf[_MAX_PATH + 0x10]; /*< buffer for file path plus extension */

  logPath = pathBuf;
#    endif

  /* Use `GetEnvironmentVariable` instead of `GETENV` for Unicode support. */
#    ifndef NO_GETENV_WIN32
  if (GetEnvironmentVariable(TEXT("GC_LOG_FILE"), pathBuf, _MAX_PATH + 1) - 1U
      < (DWORD)_MAX_PATH) {
    appendToFile = TRUE;
  } else
#    endif
  /* else */ {
    /* Environment var not found or its value too long. */
#    ifdef OLD_WIN32_LOG_FILE
    logPath = TEXT(GC_LOG_STD_NAME);
#    else
    int len
        = (int)GetModuleFileName(NULL /* `hModule` */, pathBuf, _MAX_PATH + 1);
    /* If `GetModuleFileName()` has failed, then len is 0. */
    if (len > 4 && pathBuf[len - 4] == (TCHAR)'.') {
      /* Strip the executable file extension. */
      len -= 4;
    }
    BCOPY(TEXT(".") TEXT(GC_LOG_STD_NAME), &pathBuf[len],
          sizeof(TEXT(".") TEXT(GC_LOG_STD_NAME)));
#    endif
  }

  hFile = CreateFile(logPath, GENERIC_WRITE, FILE_SHARE_READ,
                     NULL /* `lpSecurityAttributes` */,
                     appendToFile ? OPEN_ALWAYS : CREATE_ALWAYS,
                     GC_print_stats == VERBOSE
                         ? FILE_ATTRIBUTE_NORMAL
                         :
                         /* immediately flush writes unless very verbose */
                         FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH,
                     NULL /* `hTemplateFile` */);

#    ifndef NO_GETENV_WIN32
  if (appendToFile && hFile != INVALID_HANDLE_VALUE) {
    LONG posHigh = 0;
    /* Seek to the file end (ignoring any error). */
    (void)SetFilePointer(hFile, 0, &posHigh, FILE_END);
  }
#    endif
#    undef appendToFile
#  endif
  return hFile;
}

STATIC int
GC_write(const char *buf, size_t len)
{
  BOOL res;
  DWORD written;
#  if defined(THREADS) && defined(GC_ASSERTIONS)
  /* This is to prevent infinite recursion at abort. */
  static GC_bool inside_write = FALSE;

  if (inside_write)
    return -1;
#  endif

  if (0 == len)
    return 0;
  IF_NEED_TO_LOCK(EnterCriticalSection(&GC_write_cs));
#  if defined(THREADS) && defined(GC_ASSERTIONS)
  if (GC_write_disabled) {
    inside_write = TRUE;
    ABORT("Assertion failure: GC_write called with write_disabled");
  }
#  endif
  if (0 == GC_log) {
    GC_log = GC_CreateLogFile();
  }
  if (GC_log == INVALID_HANDLE_VALUE) {
    IF_NEED_TO_LOCK(LeaveCriticalSection(&GC_write_cs));
#  ifdef NO_DEBUGGING
    /*
     * Ignore open log failure (e.g., it might be caused by read-only folder
     * of the client application).
     */
    return 0;
#  else
    return -1;
#  endif
  }
  res = WriteFile(GC_log, buf, (DWORD)len, &written, NULL);
#  if defined(_MSC_VER) && defined(_DEBUG) && !defined(NO_CRT) \
      && !defined(NO_CRTDBGREPORT)
#    ifdef MSWINCE
  /* There is no `CrtDbgReport()` in WinCE. */
  {
    WCHAR wbuf[1024];

    /* Always use Unicode variant of `OutputDebugString()`. */
    wbuf[MultiByteToWideChar(CP_ACP, 0 /* `dwFlags` */, buf, len, wbuf,
                             sizeof(wbuf) / sizeof(wbuf[0]) - 1)]
        = 0;
    OutputDebugStringW(wbuf);
  }
#    else
  _CrtDbgReport(_CRT_WARN, NULL, 0, NULL, "%.*s", len, buf);
#    endif
#  endif
  IF_NEED_TO_LOCK(LeaveCriticalSection(&GC_write_cs));
  return res ? (int)written : -1;
}

/* TODO: This is pretty ugly... */
#  define WRITE(f, buf, len) GC_write(buf, len)

#elif defined(OS2)
STATIC FILE *GC_stdout = NULL;
STATIC FILE *GC_stderr = NULL;
STATIC FILE *GC_log = NULL;

/* Initialize `GC_log` (and the friends) passed to `GC_write()`. */
STATIC void
GC_set_files(void)
{
  if (GC_stdout == NULL) {
    GC_stdout = stdout;
  }
  if (GC_stderr == NULL) {
    GC_stderr = stderr;
  }
  if (GC_log == NULL) {
    GC_log = stderr;
  }
}

GC_INLINE int
GC_write(FILE *f, const char *buf, size_t len)
{
  int res = fwrite(buf, 1, len, f);
  fflush(f);
  return res;
}

#  define WRITE(f, buf, len) (GC_set_files(), GC_write(f, buf, len))

#elif defined(GC_ANDROID_LOG)

#  include <android/log.h>

#  ifndef GC_ANDROID_LOG_TAG
#    define GC_ANDROID_LOG_TAG "BDWGC"
#  endif

#  define GC_stdout ANDROID_LOG_DEBUG
#  define GC_stderr ANDROID_LOG_ERROR
#  define GC_log GC_stdout

#  define WRITE(level, buf, unused_len) \
    __android_log_write(level, GC_ANDROID_LOG_TAG, buf)

#elif defined(NN_PLATFORM_CTR)
int n3ds_log_write(const char *text, int length);
#  define WRITE(level, buf, len) n3ds_log_write(buf, len)

#elif defined(NINTENDO_SWITCH)
int switch_log_write(const char *text, int length);
#  define WRITE(level, buf, len) switch_log_write(buf, len)

#else

#  if !defined(ECOS) && !defined(NOSYS) && !defined(PLATFORM_WRITE) \
      && !defined(SN_TARGET_PSP2)
#    include <errno.h>
#  endif

STATIC int
GC_write(int fd, const char *buf, size_t len)
{
#  if defined(ECOS) || defined(PLATFORM_WRITE) || defined(SN_TARGET_PSP2) \
      || defined(NOSYS)
  UNUSED_ARG(fd);
#    ifdef ECOS
  /* FIXME: This seems to be defined nowhere at present. */
  /* `_Jv_diag_write(buf, len);` */
#    else
  /* No writing. */
#    endif
  UNUSED_ARG(buf);
  return (int)len;
#  else
  size_t bytes_written = 0;
  IF_CANCEL(int cancel_state;)

  DISABLE_CANCEL(cancel_state);
  while (bytes_written < len) {
    int result;

#    if defined(SOLARIS) && defined(THREADS)
    result = syscall(SYS_write, fd, buf + bytes_written, len - bytes_written);
#    elif defined(_MSC_VER)
    result = _write(fd, buf + bytes_written, (unsigned)(len - bytes_written));
#    else
    result = (int)write(fd, buf + bytes_written, len - bytes_written);
#    endif
    if (result < 0) {
      if (EAGAIN == errno) {
        /* Resource is temporarily unavailable. */
        continue;
      }
      RESTORE_CANCEL(cancel_state);
      return -1;
    }
#    ifdef LINT2
    if ((unsigned)result > len - bytes_written)
      ABORT("write() result cannot be bigger than requested length");
#    endif
    bytes_written += (unsigned)result;
  }
  RESTORE_CANCEL(cancel_state);
  return (int)bytes_written;
#  endif
}

#  define WRITE(f, buf, len) GC_write(f, buf, len)
#endif /* !MSWINCE && !OS2 && !GC_ANDROID_LOG */

#ifndef GC_DISABLE_SNPRINTF
#  define BUFSZ 1024

#  if defined(DJGPP) || defined(__STRICT_ANSI__)
/* `vsnprintf` is missing in DJGPP (v2.0.3). */
#    define GC_VSNPRINTF(buf, bufsz, format, args) vsprintf(buf, format, args)
#  elif defined(_MSC_VER)
#    ifdef MSWINCE
/* `_vsnprintf` is deprecated in WinCE. */
#      define GC_VSNPRINTF StringCchVPrintfA
#    else
#      define GC_VSNPRINTF _vsnprintf
#    endif
#  else
#    define GC_VSNPRINTF vsnprintf
#  endif

/*
 * A variant of `printf` that is unlikely to call `malloc`, and is thus
 * safer to call from the collector in case `malloc` has been bound to
 * `GC_malloc`.  Floating-point arguments and formats should be avoided,
 * since the conversion is more likely to allocate memory.
 * Assumes that no more than `BUFSZ - 1` characters are written at once.
 */
#  define GC_PRINTF_FILLBUF(buf, format)                      \
    do {                                                      \
      va_list args;                                           \
      va_start(args, format);                                 \
      (buf)[sizeof(buf) - 1] = 0x15; /*< guard */             \
      (void)GC_VSNPRINTF(buf, sizeof(buf) - 1, format, args); \
      va_end(args);                                           \
      if ((buf)[sizeof(buf) - 1] != 0x15)                     \
        ABORT("GC_printf clobbered stack");                   \
    } while (0)

#  define DECL_BUF_AND_PRINTF_TO(buf, format) \
    char buf[BUFSZ + 1];                      \
    GC_PRINTF_FILLBUF(buf, format)
#else
/*
 * At most, when `vsnprintf()` is unavailable, we could only print the
 * format string as is, not handling the format specifiers (if any), thus
 * skipping the rest of the `printf` arguments.
 */
#  define DECL_BUF_AND_PRINTF_TO(buf, format) const char *buf = (format)
#endif /* GC_DISABLE_SNPRINTF */

void
GC_printf(const char *format, ...)
{
  if (!GC_quiet) {
    DECL_BUF_AND_PRINTF_TO(buf, format);
#ifdef NACL
    (void)WRITE(GC_stdout, buf, strlen(buf));
    /* Ignore errors silently. */
#else
    if (WRITE(GC_stdout, buf, strlen(buf)) < 0
#  if (defined(CONSOLE_LOG) && defined(MSWIN32)) || defined(CYGWIN)
        && GC_stdout != GC_DEFAULT_STDOUT_FD
#  endif
    ) {
      ABORT("write to stdout failed");
    }
#endif
  }
}

void
GC_err_printf(const char *format, ...)
{
  DECL_BUF_AND_PRINTF_TO(buf, format);
  GC_err_puts(buf);
}

void
GC_log_printf(const char *format, ...)
{
  DECL_BUF_AND_PRINTF_TO(buf, format);
#ifdef NACL
  (void)WRITE(GC_log, buf, strlen(buf));
#else
  if (WRITE(GC_log, buf, strlen(buf)) < 0
#  if (defined(CONSOLE_LOG) && defined(MSWIN32)) || defined(CYGWIN)
      && GC_log != GC_DEFAULT_STDERR_FD
#  endif
  ) {
    ABORT("write to GC log failed");
  }
#endif
}

#ifndef GC_ANDROID_LOG

#  define GC_warn_printf GC_err_printf

#else

GC_INNER void
GC_info_log_printf(const char *format, ...)
{
  DECL_BUF_AND_PRINTF_TO(buf, format);
  (void)WRITE(ANDROID_LOG_INFO, buf, 0 /* unused */);
}

GC_INNER void
GC_verbose_log_printf(const char *format, ...)
{
  DECL_BUF_AND_PRINTF_TO(buf, format);
  /* Note: write errors are ignored. */
  (void)WRITE(ANDROID_LOG_VERBOSE, buf, 0);
}

STATIC void
GC_warn_printf(const char *format, ...)
{
  DECL_BUF_AND_PRINTF_TO(buf, format);
  (void)WRITE(ANDROID_LOG_WARN, buf, 0);
}

#endif /* GC_ANDROID_LOG */

void
GC_err_puts(const char *s)
{
  /* Note: write errors are ignored. */
  (void)WRITE(GC_stderr, s, strlen(s));
}

STATIC void GC_CALLBACK
GC_default_warn_proc(const char *msg, GC_uintptr_t arg)
{
  /* TODO: Add assertion on argument to comply with `msg` (format). */
  GC_warn_printf(msg, arg);
}

GC_INNER GC_warn_proc GC_current_warn_proc = GC_default_warn_proc;

GC_API void GC_CALLBACK
GC_ignore_warn_proc(const char *msg, GC_uintptr_t arg)
{
  if (GC_print_stats) {
    /* Do not ignore warnings if stats printing is on. */
    GC_default_warn_proc(msg, arg);
  }
}

GC_API void GC_CALL
GC_set_warn_proc(GC_warn_proc p)
{
  GC_ASSERT(NONNULL_ARG_NOT_NULL(p));
  LOCK();
  GC_current_warn_proc = p;
  UNLOCK();
}

GC_API GC_warn_proc GC_CALL
GC_get_warn_proc(void)
{
  GC_warn_proc result;

  READER_LOCK();
  result = GC_current_warn_proc;
  READER_UNLOCK();
  return result;
}

/*
 * Print (or display) a message before abnormal exit (including abort).
 * Invoked from `ABORT(msg)` macro (where `msg` is non-`NULL`) and from
 * `EXIT()` macro (`msg` is `NULL` in that case).
 */
STATIC void GC_CALLBACK
GC_default_on_abort(const char *msg)
{
#if !defined(SMALL_CONFIG)
#  ifndef DONT_USE_ATEXIT
  /* Disable at-exit garbage collection. */
  GC_skip_collect_atexit = TRUE;
#  endif

  if (msg != NULL) {
#  ifdef MSGBOX_ON_ERROR
    if (NULL == GETENV("GC_PROMPT_DISABLED")) {
      GC_win32_MessageBoxA(msg, "Fatal error in GC", MB_ICONERROR | MB_OK);
    }
    /* Also duplicate `msg` to the collector log file. */
#  endif

#  ifndef GC_ANDROID_LOG
    /*
     * Avoid calling `GC_err_printf()` here, as `GC_on_abort()` could
     * be called from it.  Note 1: this is not an atomic output.
     * Note 2: possible write errors are ignored.
     */
#    if defined(GC_WIN32_THREADS) && defined(GC_ASSERTIONS) \
        && ((defined(MSWIN32) && !defined(CONSOLE_LOG)) || defined(MSWINCE))
    if (!GC_write_disabled)
#    endif
    {
      if (WRITE(GC_stderr, msg, strlen(msg)) >= 0)
        (void)WRITE(GC_stderr, "\n", 1);
    }
#  else
    __android_log_assert("*" /* `cond` */, GC_ANDROID_LOG_TAG, "%s\n", msg);
#  endif
#  if defined(HAIKU) && !defined(DONT_CALL_DEBUGGER)
    /*
     * This will cause the crash reason to appear in any debug reports
     * generated (by the default system application crash dialog).
     */
    debugger(msg);
#  endif
  }

#  if !defined(NO_DEBUGGING) && !defined(GC_ANDROID_LOG)
  if (GETENV("GC_LOOP_ON_ABORT") != NULL) {
    /*
     * In many cases it is easier to debug a running process.
     * It is arguably nicer to sleep, but that makes it harder to look
     * at the thread if the debugger does not know much about threads.
     */
    for (;;) {
      /* Empty. */
    }
  }
#  endif
#else
  UNUSED_ARG(msg);
#endif
}

#ifndef SMALL_CONFIG
GC_abort_func GC_on_abort = GC_default_on_abort;
#endif

GC_API void GC_CALL
GC_set_abort_func(GC_abort_func fn)
{
  GC_ASSERT(NONNULL_ARG_NOT_NULL(fn));
  LOCK();
#ifndef SMALL_CONFIG
  GC_on_abort = fn;
#else
  UNUSED_ARG(fn);
#endif
  UNLOCK();
}

GC_API GC_abort_func GC_CALL
GC_get_abort_func(void)
{
  GC_abort_func fn;

  READER_LOCK();
#ifndef SMALL_CONFIG
  fn = GC_on_abort;
  GC_ASSERT(fn != 0);
#else
  fn = GC_default_on_abort;
#endif
  READER_UNLOCK();
  return fn;
}

#if defined(NEED_SNPRINTF_SLDS) /* && GC_DISABLE_SNPRINTF */
GC_INNER void
GC_snprintf_s_ld_s(char *buf, size_t buf_sz, const char *prefix, long lv,
                   const char *suffix)
{
  size_t len = strlen(prefix);

  GC_ASSERT(buf_sz > 0);
  /* Copy the prefix. */
  if (UNLIKELY(len >= buf_sz))
    len = buf_sz - 1;
  BCOPY(prefix, buf, len);
  buf += len;
  buf_sz -= len;

  /* Handle sign of the number. */
  if (lv >= 0) {
    lv = -lv;
  } else if (LIKELY(buf_sz > 1)) {
    *(buf++) = '-';
    buf_sz--;
  }

  /* Convert the decimal number to string.  (A trivial implementation.) */
  {
    char num_buf[20];
    size_t pos = sizeof(num_buf);

    do {
      long r = lv / 10;

      if (UNLIKELY(0 == pos))
        break; /*< overflow */
      num_buf[--pos] = (char)(r * 10 - lv + '0');
      lv = r;
    } while (lv < 0);
    len = sizeof(num_buf) - pos;
    if (UNLIKELY(len >= buf_sz))
      len = buf_sz - 1;
    BCOPY(&num_buf[pos], buf, len);
  }
  buf += len;
  buf_sz -= len;

  /* Copy the suffix (if any). */
  len = strlen(suffix);
  if (len > 0) {
    if (UNLIKELY(len >= buf_sz))
      len = buf_sz - 1;
    BCOPY(suffix, buf, len);
    buf += len;
  }
  *buf = '\0';
}
#endif /* NEED_SNPRINTF_SLDS */

GC_API void GC_CALL
GC_enable(void)
{
  LOCK();
  /* Ensure no counter underflow. */
  GC_ASSERT(GC_dont_gc != 0);
  GC_dont_gc--;
#ifndef NO_WARN_HEAP_GROW_WHEN_GC_DISABLED
  if (!GC_dont_gc && GC_heapsize > GC_heapsize_on_gc_disable)
    WARN("Heap grown by %" WARN_PRIuPTR " KiB while GC was disabled\n",
         (GC_heapsize - GC_heapsize_on_gc_disable) >> 10);
#endif
  UNLOCK();
}

GC_API void GC_CALL
GC_disable(void)
{
  LOCK();
#ifndef NO_WARN_HEAP_GROW_WHEN_GC_DISABLED
  if (!GC_dont_gc)
    GC_heapsize_on_gc_disable = GC_heapsize;
#endif
  GC_dont_gc++;
  UNLOCK();
}

GC_API int GC_CALL
GC_is_disabled(void)
{
  return GC_dont_gc != 0;
}

/* Helper procedures for new kind creation. */

GC_API void **GC_CALL
GC_new_free_list_inner(void)
{
  void *result;

  GC_ASSERT(I_HOLD_LOCK());
  result = GC_INTERNAL_MALLOC((MAXOBJGRANULES + 1) * sizeof(ptr_t), PTRFREE);
  if (NULL == result)
    ABORT("Failed to allocate free list for new kind");
  BZERO(result, (MAXOBJGRANULES + 1) * sizeof(ptr_t));
  return (void **)result;
}

GC_API void **GC_CALL
GC_new_free_list(void)
{
  void **result;

  LOCK();
  result = GC_new_free_list_inner();
  UNLOCK();
  return result;
}

GC_API unsigned GC_CALL
GC_new_kind_inner(void **fl, GC_word descr, int adjust, int clear)
{
  unsigned result = GC_n_kinds;

  GC_ASSERT(NONNULL_ARG_NOT_NULL(fl));
  GC_ASSERT(!adjust || 1 == adjust);
  /*
   * If an object is not needed to be cleared (when moved to the free list),
   * then its descriptor should be zero to denote a pointer-free object
   * (and, as a consequence, the size of the object should not be added to
   * the descriptor template).
   */
  GC_ASSERT(1 == clear || (0 == descr && !adjust && !clear));
  if (result < MAXOBJKINDS) {
    GC_ASSERT(result > 0);
    GC_n_kinds++;
    GC_obj_kinds[result].ok_freelist = fl;
    GC_obj_kinds[result].ok_reclaim_list = 0;
    GC_obj_kinds[result].ok_descriptor = descr;
    GC_obj_kinds[result].ok_relocate_descr = (GC_bool)adjust;
    GC_obj_kinds[result].ok_init = (GC_bool)clear;
#ifdef ENABLE_DISCLAIM
    GC_obj_kinds[result].ok_mark_unconditionally = FALSE;
    GC_obj_kinds[result].ok_disclaim_proc = 0;
#endif
  } else {
    ABORT("Too many kinds");
  }
  return result;
}

GC_API unsigned GC_CALL
GC_new_kind(void **fl, GC_word descr, int adjust, int clear)
{
  unsigned result;

  LOCK();
  result = GC_new_kind_inner(fl, descr, adjust, clear);
  UNLOCK();
  return result;
}

GC_API unsigned GC_CALL
GC_new_proc_inner(GC_mark_proc proc)
{
  unsigned result = GC_n_mark_procs;

  if (result < GC_MAX_MARK_PROCS) {
    GC_n_mark_procs++;
    GC_mark_procs[result] = proc;
  } else {
    ABORT("Too many mark procedures");
  }
  return result;
}

GC_API unsigned GC_CALL
GC_new_proc(GC_mark_proc proc)
{
  unsigned result;

  LOCK();
  result = GC_new_proc_inner(proc);
  UNLOCK();
  return result;
}

GC_API void *GC_CALL
GC_call_with_alloc_lock(GC_fn_type fn, void *client_data)
{
  void *result;

  LOCK();
  result = fn(client_data);
  UNLOCK();
  return result;
}

#ifdef THREADS
GC_API void GC_CALL
GC_alloc_lock(void)
{
  LOCK();
}

GC_API void GC_CALL
GC_alloc_unlock(void)
{
  UNLOCK();
}

GC_API void *GC_CALL
GC_call_with_reader_lock(GC_fn_type fn, void *client_data, int release)
{
  void *result;

  READER_LOCK();
  result = fn(client_data);
#  ifdef HAS_REAL_READER_LOCK
  if (release) {
    READER_UNLOCK_RELEASE();
#    ifdef LINT2
    GC_noop1((unsigned)release);
#    endif
    return result;
  }
#  else
  UNUSED_ARG(release);
#  endif
  READER_UNLOCK();
  return result;
}
#endif /* THREADS */

GC_ATTR_NOINLINE
GC_API void *GC_CALL
GC_call_with_stack_base(GC_stack_base_func fn, void *arg)
{
  struct GC_stack_base base;
  void *result;

  STORE_APPROX_SP_TO(*(volatile ptr_t *)&base.mem_base);
#ifdef IA64
  base.reg_base = GC_save_regs_in_stack();
  /*
   * TODO: Unnecessarily flushes register stack, but that probably
   * does not hurt.
   */
#elif defined(E2K)
  {
    unsigned long long sz_ull;

    GET_PROCEDURE_STACK_SIZE_INNER(&sz_ull);
    base.reg_base = NUMERIC_TO_VPTR(sz_ull);
  }
#endif
  result = (*(GC_stack_base_func volatile *)&fn)(&base, arg);
  /*
   * Strongly discourage the compiler from treating the above as
   * a tail call.
   */
  GC_noop1(COVERT_DATAFLOW(ADDR(&base)));
  return result;
}

#ifndef THREADS
/* This is nearly the same as in `pthread_support.c` file. */
GC_ATTR_NOINLINE
GC_API void *GC_CALL
GC_call_with_gc_active(GC_fn_type fn, void *client_data)
{
  struct GC_traced_stack_sect_s stacksect;
  GC_ASSERT(GC_is_initialized);

  /*
   * Adjust our stack bottom pointer (this could happen if
   * `GC_get_main_stack_base()` is unimplemented or broken for
   * the platform).  Note: `stacksect` variable is reused here.
   */
  STORE_APPROX_SP_TO(*(volatile ptr_t *)&stacksect.saved_stack_ptr);
  if (HOTTER_THAN(GC_stackbottom, stacksect.saved_stack_ptr))
    GC_stackbottom = stacksect.saved_stack_ptr;

  if (NULL == GC_blocked_sp) {
    /* We are not inside `GC_do_blocking()` - do nothing more. */
    client_data = (*(GC_fn_type volatile *)&fn)(client_data);
    /* Prevent treating the above as a tail call. */
    GC_noop1(COVERT_DATAFLOW(ADDR(&stacksect)));
    return client_data; /*< result */
  }

  /* Setup new "stack section". */
  stacksect.saved_stack_ptr = GC_blocked_sp;
#  ifdef IA64
  /* This is the same as in `GC_call_with_stack_base()`. */
  stacksect.backing_store_end = GC_save_regs_in_stack();
  /* Unnecessarily flushes register stack, but that probably does not hurt. */
  stacksect.saved_backing_store_ptr = GC_blocked_register_sp;
#  endif
  stacksect.prev = GC_traced_stack_sect;
  GC_blocked_sp = NULL;
  GC_traced_stack_sect = &stacksect;

  client_data = (*(GC_fn_type volatile *)&fn)(client_data);
  GC_ASSERT(NULL == GC_blocked_sp);
  GC_ASSERT(GC_traced_stack_sect == &stacksect);

#  if defined(CPPCHECK)
  GC_noop1_ptr(GC_traced_stack_sect);
  GC_noop1_ptr(GC_blocked_sp);
#  endif
  /* Restore original "stack section". */
  GC_traced_stack_sect = stacksect.prev;
#  ifdef IA64
  GC_blocked_register_sp = stacksect.saved_backing_store_ptr;
#  endif
  GC_blocked_sp = stacksect.saved_stack_ptr;

  return client_data; /*< result */
}

/* This is nearly the same as in `pthread_support.c` file. */
STATIC void
GC_do_blocking_inner(ptr_t data, void *context)
{
  UNUSED_ARG(context);
  GC_ASSERT(GC_is_initialized);
  GC_ASSERT(NULL == GC_blocked_sp);
#  ifdef SPARC
  GC_blocked_sp = GC_save_regs_in_stack();
#  else
  GC_blocked_sp = GC_approx_sp();
#    ifdef IA64
  GC_blocked_register_sp = GC_save_regs_in_stack();
#    endif
#  endif

  ((struct blocking_data *)data)->client_data /*< result */
      = ((struct blocking_data *)data)
            ->fn(((struct blocking_data *)data)->client_data);

  GC_ASSERT(GC_blocked_sp != NULL);
#  if defined(CPPCHECK)
  GC_noop1_ptr(GC_blocked_sp);
#  endif
  GC_blocked_sp = NULL;
}

GC_API void GC_CALL
GC_set_stackbottom(void *gc_thread_handle, const struct GC_stack_base *sb)
{
  GC_ASSERT(sb->mem_base != NULL);
  GC_ASSERT(NULL == gc_thread_handle || &GC_stackbottom == gc_thread_handle);
  GC_ASSERT(NULL == GC_blocked_sp
            && NULL == GC_traced_stack_sect); /*< for now */
  UNUSED_ARG(gc_thread_handle);

  GC_stackbottom = (char *)sb->mem_base;
#  ifdef IA64
  GC_register_stackbottom = (ptr_t)sb->reg_base;
#  endif
}

GC_API void *GC_CALL
GC_get_my_stackbottom(struct GC_stack_base *sb)
{
  GC_ASSERT(GC_is_initialized);
  sb->mem_base = GC_stackbottom;
#  ifdef IA64
  sb->reg_base = GC_register_stackbottom;
#  elif defined(E2K)
  sb->reg_base = NULL;
#  endif
  return &GC_stackbottom; /*< `gc_thread_handle` */
}
#endif /* !THREADS */

GC_API void *GC_CALL
GC_do_blocking(GC_fn_type fn, void *client_data)
{
  struct blocking_data my_data;

  my_data.fn = fn;
  my_data.client_data = client_data;
  GC_with_callee_saves_pushed(GC_do_blocking_inner, (ptr_t)(&my_data));
  return my_data.client_data; /*< result */
}

#if !defined(NO_DEBUGGING)
GC_API void GC_CALL
GC_dump(void)
{
  READER_LOCK();
  GC_dump_named(NULL);
  READER_UNLOCK();
}

GC_API void GC_CALL
GC_dump_named(const char *name)
{
#  ifndef NO_CLOCK
  CLOCK_TYPE current_time;

  GET_TIME(current_time);
#  endif
  if (name != NULL) {
    GC_printf("\n***GC Dump %s\n", name);
  } else {
    GC_printf("\n***GC Dump collection #%lu\n", (unsigned long)GC_gc_no);
  }
#  ifndef NO_CLOCK
  /* Note that the time is wrapped in ~49 days if `sizeof(long) == 4`. */
  GC_printf("Time since GC init: %lu ms\n",
            MS_TIME_DIFF(current_time, GC_init_time));
#  endif

  GC_printf("\n***Static roots:\n");
  GC_print_static_roots();
  GC_printf("\n***Heap sections:\n");
  GC_print_heap_sects();
  GC_printf("\n***Free blocks:\n");
  GC_print_hblkfreelist();
  GC_printf("\n***Blocks in use:\n");
  GC_print_block_list();
#  ifndef GC_NO_FINALIZATION
  GC_dump_finalization();
#  endif
}
#endif /* !NO_DEBUGGING */

GC_API GC_word GC_CALL
GC_get_memory_use(void)
{
  word bytes;

  READER_LOCK();
  GC_ASSERT(GC_heapsize >= GC_large_free_bytes);
  bytes = GC_heapsize - GC_large_free_bytes;
  READER_UNLOCK();
  return bytes;
}

/* Getter functions for the public read-only variables. */

GC_API GC_word GC_CALL
GC_get_gc_no(void)
{
  return GC_gc_no;
}

#ifndef PARALLEL_MARK
GC_API void GC_CALL
GC_set_markers_count(unsigned markers)
{
  UNUSED_ARG(markers);
}
#endif

GC_API int GC_CALL
GC_get_parallel(void)
{
#ifdef THREADS
  return GC_parallel;
#else
  return 0;
#endif
}

/*
 * Setter and getter functions for the public R/W function variables.
 * These functions are synchronized (like `GC_set_warn_proc()` and
 * `GC_get_warn_proc()`).
 */

GC_API void GC_CALL
GC_set_oom_fn(GC_oom_func fn)
{
  GC_ASSERT(NONNULL_ARG_NOT_NULL(fn));
  LOCK();
  GC_oom_fn = fn;
  UNLOCK();
}

GC_API GC_oom_func GC_CALL
GC_get_oom_fn(void)
{
  GC_oom_func fn;

  READER_LOCK();
  fn = GC_oom_fn;
  READER_UNLOCK();
  return fn;
}

GC_API void GC_CALL
GC_set_on_heap_resize(GC_on_heap_resize_proc fn)
{
  /* `fn` may be 0 (means no event notifier). */
  LOCK();
  GC_on_heap_resize = fn;
  UNLOCK();
}

GC_API GC_on_heap_resize_proc GC_CALL
GC_get_on_heap_resize(void)
{
  GC_on_heap_resize_proc fn;

  READER_LOCK();
  fn = GC_on_heap_resize;
  READER_UNLOCK();
  return fn;
}

GC_API void GC_CALL
GC_set_finalizer_notifier(GC_finalizer_notifier_proc fn)
{
  /* `fn` may be 0 (means no finalizer notifier). */
  LOCK();
  GC_finalizer_notifier = fn;
  UNLOCK();
}

GC_API GC_finalizer_notifier_proc GC_CALL
GC_get_finalizer_notifier(void)
{
  GC_finalizer_notifier_proc fn;

  READER_LOCK();
  fn = GC_finalizer_notifier;
  READER_UNLOCK();
  return fn;
}

/*
 * Setter and getter functions for the public numeric R/W variables.
 * It is safe to call these functions even before `GC_INIT()`.
 * These functions are unsynchronized and, if called after `GC_INIT()`,
 * should be typically invoked inside the context of
 * `GC_call_with_alloc_lock()` (or `GC_call_with_reader_lock()` in case
 * of the getters) to prevent data race (unless it is guaranteed the
 * collector is not multi-threaded at that execution point).
 */

GC_API void GC_CALL
GC_set_find_leak(int value)
{
  /* `value` is of boolean type. */
#ifdef NO_FIND_LEAK
  if (value)
    ABORT("Find-leak mode is unsupported");
#else
  GC_find_leak = value;
#endif
}

GC_API int GC_CALL
GC_get_find_leak(void)
{
  return GC_find_leak_inner;
}

GC_API void GC_CALL
GC_set_all_interior_pointers(int value)
{
#ifdef NO_ALL_INTERIOR_POINTERS
  if (value)
    ABORT("All-interior-pointers mode is unsupported");
#else
  GC_all_interior_pointers = value ? 1 : 0;
  if (GC_is_initialized) {
    /*
     * It is not recommended to change `GC_all_interior_pointers` value
     * after the collector is initialized but it seems it could work
     * correctly even after switching the mode.
     */
    LOCK();
    /* Note: this resets manual offsets as well. */
    GC_initialize_offsets();
#  ifndef NO_BLACK_LISTING
    if (!GC_all_interior_pointers)
      GC_bl_init_no_interiors();
#  endif
    UNLOCK();
  }
#endif
}

GC_API int GC_CALL
GC_get_all_interior_pointers(void)
{
  return GC_all_interior_pointers;
}

GC_API void GC_CALL
GC_set_finalize_on_demand(int value)
{
  /* Note: -1 was used to retrieve old value in gc-7.2. */
  GC_ASSERT(value != -1);
  /* `value` is of boolean type. */
  GC_finalize_on_demand = value;
}

GC_API int GC_CALL
GC_get_finalize_on_demand(void)
{
  return GC_finalize_on_demand;
}

GC_API void GC_CALL
GC_set_java_finalization(int value)
{
  /* Note: -1 was used to retrieve old value in gc-7.2. */
  GC_ASSERT(value != -1);
  /* `value` is of boolean type. */
  GC_java_finalization = value;
}

GC_API int GC_CALL
GC_get_java_finalization(void)
{
  return GC_java_finalization;
}

GC_API void GC_CALL
GC_set_dont_expand(int value)
{
  /* Note: -1 was used to retrieve old value in gc-7.2. */
  GC_ASSERT(value != -1);
  /* `value` is of boolean type. */
  GC_dont_expand = value;
}

GC_API int GC_CALL
GC_get_dont_expand(void)
{
  return GC_dont_expand;
}

GC_API void GC_CALL
GC_set_no_dls(int value)
{
  /* Note: -1 was used to retrieve old value in gc-7.2. */
  GC_ASSERT(value != -1);
  /* `value` is of boolean type. */
  GC_no_dls = value;
}

GC_API int GC_CALL
GC_get_no_dls(void)
{
  return GC_no_dls;
}

GC_API void GC_CALL
GC_set_non_gc_bytes(GC_word value)
{
  GC_non_gc_bytes = value;
}

GC_API GC_word GC_CALL
GC_get_non_gc_bytes(void)
{
  return GC_non_gc_bytes;
}

GC_API void GC_CALL
GC_set_free_space_divisor(GC_word value)
{
  GC_ASSERT(value > 0);
  GC_free_space_divisor = value;
}

GC_API GC_word GC_CALL
GC_get_free_space_divisor(void)
{
  return GC_free_space_divisor;
}

GC_API void GC_CALL
GC_set_max_retries(GC_word value)
{
  /* Note: -1 was used to retrieve old value in gc-7.2. */
  GC_ASSERT((GC_signed_word)value != -1);
  GC_max_retries = value;
}

GC_API GC_word GC_CALL
GC_get_max_retries(void)
{
  return GC_max_retries;
}

GC_API void GC_CALL
GC_set_dont_precollect(int value)
{
  /* Note: -1 was used to retrieve old value in gc-7.2. */
  GC_ASSERT(value != -1);
  /* `value` is of boolean type. */
  GC_dont_precollect = value;
}

GC_API int GC_CALL
GC_get_dont_precollect(void)
{
  return GC_dont_precollect;
}

GC_API void GC_CALL
GC_set_full_freq(int value)
{
  GC_ASSERT(value >= 0);
  GC_full_freq = value;
}

GC_API int GC_CALL
GC_get_full_freq(void)
{
  return GC_full_freq;
}

GC_API void GC_CALL
GC_set_time_limit(unsigned long value)
{
  /* Note: -1 was used to retrieve old value in gc-7.2. */
  GC_ASSERT((long)value != -1L);
  GC_time_limit = value;
}

GC_API unsigned long GC_CALL
GC_get_time_limit(void)
{
  return GC_time_limit;
}

GC_API void GC_CALL
GC_set_force_unmap_on_gcollect(int value)
{
  GC_force_unmap_on_gcollect = (GC_bool)value;
}

GC_API int GC_CALL
GC_get_force_unmap_on_gcollect(void)
{
  return (int)GC_force_unmap_on_gcollect;
}

GC_API GC_OOM_ABORT_THROW_ATTRIBUTE void GC_CALL
GC_abort_on_oom(void)
{
  GC_err_printf("Insufficient memory for the allocation\n");
  EXIT();
}

GC_API size_t GC_CALL
GC_get_hblk_size(void)
{
  return (size_t)HBLKSIZE;
}
