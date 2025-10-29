/*
 * Copyright 1988, 1989 Hans-J. Boehm, Alan J. Demers
 * Copyright (c) 1991-1995 by Xerox Corporation.  All rights reserved.
 * Copyright (c) 1997 by Silicon Graphics.  All rights reserved.
 * Copyright (c) 1999-2004 Hewlett-Packard Development Company, L.P.
 * Copyright (c) 2007 Free Software Foundation, Inc.
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

#include "private/dbg_mlc.h"

#ifndef MSWINCE
#  include <errno.h>
#endif
#include <string.h>

#ifdef KEEP_BACK_PTRS

/*
 * Use a custom trivial `random()` implementation as the standard one might
 * lead to crashes (if used from a multi-threaded code) or to a compiler
 * warning about the deterministic result.
 */
static int
GC_rand(void)
{
  static GC_RAND_STATE_T seed;

  return GC_RAND_NEXT(&seed);
}

#  define RANDOM() (long)GC_rand()

GC_INNER void
GC_store_back_pointer(ptr_t source, ptr_t dest)
{
  if (GC_HAS_DEBUG_INFO(dest)) {
#  ifdef PARALLEL_MARK
    GC_cptr_store((volatile ptr_t *)&((oh *)dest)->oh_back_ptr,
                  (ptr_t)HIDE_BACK_PTR(source));
#  else
    ((oh *)dest)->oh_back_ptr = HIDE_BACK_PTR(source);
#  endif
  }
}

GC_INNER void
GC_marked_for_finalization(ptr_t dest)
{
  GC_store_back_pointer(MARKED_FOR_FINALIZATION, dest);
}

GC_API GC_ref_kind GC_CALL
GC_get_back_ptr_info(void *dest, void **base_p, size_t *offset_p)
{
  oh *ohdr = (oh *)GC_base(dest);
  ptr_t bp, bp_base;

#  ifdef LINT2
  /*
   * Explicitly instruct the code analysis tool that `GC_get_back_ptr_info`
   * is not expected to be called with an incorrect `dest` value.
   */
  if (!ohdr)
    ABORT("Invalid GC_get_back_ptr_info argument");
#  endif
  if (!GC_HAS_DEBUG_INFO((ptr_t)ohdr))
    return GC_NO_SPACE;
  bp = (ptr_t)GC_REVEAL_POINTER(ohdr->oh_back_ptr);
  if (MARKED_FOR_FINALIZATION == bp)
    return GC_FINALIZER_REFD;
  if (MARKED_FROM_REGISTER == bp)
    return GC_REFD_FROM_REG;
  if (NOT_MARKED == bp)
    return GC_UNREFERENCED;
#  if ALIGNMENT == 1
  /*
   * Heuristically try to fix off-by-one errors we introduced by
   * insisting on even addresses.
   */
  {
    ptr_t alternate_ptr = bp + 1;
    ptr_t target = *(ptr_t *)bp;
    ptr_t alternate_target = *(ptr_t *)alternate_ptr;

    if (GC_least_real_heap_addr < ADDR(alternate_target)
        && ADDR(alternate_target) < GC_greatest_real_heap_addr
        && (GC_least_real_heap_addr >= ADDR(target)
            || ADDR(target) >= GC_greatest_real_heap_addr)) {
      bp = alternate_ptr;
    }
  }
#  endif
  bp_base = (ptr_t)GC_base(bp);
  if (NULL == bp_base) {
    *base_p = bp;
    *offset_p = 0;
    return GC_REFD_FROM_ROOT;
  } else {
    if (GC_HAS_DEBUG_INFO(bp_base))
      bp_base += sizeof(oh);
    *base_p = bp_base;
    *offset_p = (size_t)(bp - bp_base);
    return GC_REFD_FROM_HEAP;
  }
}

GC_API void *GC_CALL
GC_generate_random_heap_address(void)
{
  size_t i;
  word heap_offset = (word)RANDOM();

  if (GC_heapsize > (word)GC_RAND_MAX) {
    heap_offset *= GC_RAND_MAX;
    heap_offset += (word)RANDOM();
  }

  /*
   * This does not yield a uniform distribution, especially if e.g.
   * `RAND_MAX` is `1.5 * GC_heapsize`.  But for typical cases,  it is
   * not too bad.
   */
  heap_offset %= GC_heapsize;

  for (i = 0;; ++i) {
    size_t size;

    if (i >= GC_n_heap_sects)
      ABORT("GC_generate_random_heap_address: size inconsistency");

    size = GC_heap_sects[i].hs_bytes;
    if (heap_offset < size)
      break;
    heap_offset -= size;
  }
  return GC_heap_sects[i].hs_start + heap_offset;
}

GC_API void *GC_CALL
GC_generate_random_valid_address(void)
{
  ptr_t result;
  ptr_t base;

  do {
    result = (ptr_t)GC_generate_random_heap_address();
    base = (ptr_t)GC_base(result);
  } while (NULL == base || !GC_is_marked(base));
  return result;
}

GC_API void GC_CALL
GC_print_backtrace(void *p)
{
  void *current = p;
  int i;

  GC_ASSERT(I_DONT_HOLD_LOCK());
  GC_print_heap_obj((ptr_t)GC_base(current));

  for (i = 0;; ++i) {
    void *base;
    size_t offset;
    GC_ref_kind source = GC_get_back_ptr_info(current, &base, &offset);

    if (GC_UNREFERENCED == source) {
      GC_err_printf("Reference could not be found\n");
      break;
    }
    if (GC_NO_SPACE == source) {
      GC_err_printf("No debug info in object: Can't find reference\n");
      break;
    }
    GC_err_printf("Reachable via %d levels of pointers from ", i);
    switch (source) {
    case GC_REFD_FROM_ROOT:
      GC_err_printf("root at %p\n\n", base);
      return;
    case GC_REFD_FROM_REG:
      GC_err_printf("root in register\n\n");
      return;
    case GC_FINALIZER_REFD:
      GC_err_printf("list of finalizable objects\n\n");
      return;
    case GC_REFD_FROM_HEAP:
      GC_err_printf("offset %ld in object:\n", (long)offset);
      /* Take `GC_base(base)` to get real base, i.e. header. */
      GC_print_heap_obj((ptr_t)GC_base(base));
      break;
    default:
      GC_err_printf("INTERNAL ERROR: UNEXPECTED SOURCE!!!!\n");
      return;
    }
    current = base;
  }
}

GC_API void GC_CALL
GC_generate_random_backtrace(void)
{
  void *current;

  GC_ASSERT(I_DONT_HOLD_LOCK());
  if (GC_try_to_collect(GC_never_stop_func) == 0) {
    GC_err_printf("Cannot generate a backtrace: "
                  "garbage collection is disabled!\n");
    return;
  }

  /* Generate/print a backtrace from a random heap address. */
  LOCK();
  current = GC_generate_random_valid_address();
  UNLOCK();
  GC_printf("\n***Chosen address %p in object\n", current);
  GC_print_backtrace(current);
}

#endif /* KEEP_BACK_PTRS */

#define CROSSES_HBLK(p, sz) \
  ((ADDR((p) + (sizeof(oh) - 1) + (sz)) ^ ADDR(p)) >= HBLKSIZE)

/*
 * Store debugging info into `p`.  Return displaced pointer.  Assume we hold
 * the allocator lock.
 */
STATIC void *
GC_store_debug_info_inner(void *base, size_t sz, const char *string,
                          int linenum)
{
  GC_uintptr_t *result = (GC_uintptr_t *)((oh *)base + 1);

  GC_ASSERT(I_HOLD_LOCK());
  GC_ASSERT(GC_size(base) >= sizeof(oh) + sz);
  GC_ASSERT(!(SMALL_OBJ(sz) && CROSSES_HBLK((ptr_t)base, sz)));
#ifdef KEEP_BACK_PTRS
  ((oh *)base)->oh_back_ptr = HIDE_BACK_PTR(NOT_MARKED);
#endif
#ifdef MAKE_BACK_GRAPH
  ((oh *)base)->oh_bg_ptr = HIDE_BACK_PTR((ptr_t)0);
#endif
  ((oh *)base)->oh_string = string;
  ((oh *)base)->oh_int = linenum;
#ifdef SHORT_DBG_HDRS
  UNUSED_ARG(sz);
#else
  ((oh *)base)->oh_sz = (GC_uintptr_t)sz;
  ((oh *)base)->oh_sf = START_FLAG ^ (GC_uintptr_t)result;
  ((GC_uintptr_t *)base)[BYTES_TO_PTRS(GC_size(base)) - 1]
      = result[BYTES_TO_PTRS_ROUNDUP(sz)] = END_FLAG ^ (GC_uintptr_t)result;
#endif
  return result;
}

#ifndef SHORT_DBG_HDRS
/*
 * Check the object with debugging info at `ohdr`.  Return `NULL` if it
 * is OK.  Else return clobbered address.
 */
STATIC ptr_t
GC_check_annotated_obj(oh *ohdr)
{
  ptr_t body = (ptr_t)(ohdr + 1);
  size_t gc_sz = GC_size(ohdr);
  size_t lpw_up;

  if (ohdr->oh_sz + DEBUG_BYTES > (GC_uintptr_t)gc_sz) {
    return (ptr_t)(&ohdr->oh_sz);
  }
  if (ohdr->oh_sf != (START_FLAG ^ (GC_uintptr_t)body)) {
    return (ptr_t)(&ohdr->oh_sf);
  }

  {
    size_t lpw_m1 = BYTES_TO_PTRS(gc_sz) - 1;

    if (((GC_uintptr_t *)ohdr)[lpw_m1] != (END_FLAG ^ (GC_uintptr_t)body)) {
      return (ptr_t)(&((GC_uintptr_t *)ohdr)[lpw_m1]);
    }
  }
  lpw_up = BYTES_TO_PTRS_ROUNDUP((size_t)ohdr->oh_sz);
  if (((GC_uintptr_t *)body)[lpw_up] != (END_FLAG ^ (GC_uintptr_t)body)) {
    return (ptr_t)(&((GC_uintptr_t *)body)[lpw_up]);
  }
  return NULL;
}
#endif /* !SHORT_DBG_HDRS */

STATIC GC_describe_type_fn GC_describe_type_fns[MAXOBJKINDS] = { 0 };

GC_API void GC_CALL
GC_register_describe_type_fn(int kind, GC_describe_type_fn fn)
{
  GC_ASSERT((unsigned)kind < MAXOBJKINDS);
  GC_describe_type_fns[kind] = fn;
}

#ifndef SHORT_DBG_HDRS
#  define IF_NOT_SHORTDBG_HDRS(x) x
#  define COMMA_IFNOT_SHORTDBG_HDRS(x) /* comma */ , x
#else
#  define IF_NOT_SHORTDBG_HDRS(x)
#  define COMMA_IFNOT_SHORTDBG_HDRS(x)
#endif

/*
 * Print a human-readable description of the object to `stderr`.
 * The object is assumed to have the debugging info.
 */
STATIC void
GC_print_obj(ptr_t base)
{
  oh *ohdr = (oh *)base;
  ptr_t q;
  hdr *hhdr;
  int kind;
  const char *kind_str;
  char buffer[GC_TYPE_DESCR_LEN + 1];

  GC_ASSERT(I_DONT_HOLD_LOCK());
#ifdef LINT2
  if (!ohdr)
    ABORT("Invalid GC_print_obj argument");
#endif

  q = (ptr_t)(ohdr + 1);
  /*
   * Print a type description for the object whose client-visible address
   * is `q`.
   */
  hhdr = GC_find_header(q);
  kind = hhdr->hb_obj_kind;
  if (GC_describe_type_fns[kind] != 0 && GC_is_marked(ohdr)) {
    /*
     * This should preclude free-list objects except with thread-local
     * allocation.
     */
    buffer[GC_TYPE_DESCR_LEN] = 0;
    (*GC_describe_type_fns[kind])(q, buffer);
    GC_ASSERT(buffer[GC_TYPE_DESCR_LEN] == 0);
    kind_str = buffer;
  } else {
    switch (kind) {
    case PTRFREE:
      kind_str = "PTRFREE";
      break;
    case NORMAL:
      kind_str = "NORMAL";
      break;
    case UNCOLLECTABLE:
      kind_str = "UNCOLLECTABLE";
      break;
#ifdef GC_ATOMIC_UNCOLLECTABLE
    case AUNCOLLECTABLE:
      kind_str = "ATOMIC_UNCOLLECTABLE";
      break;
#endif
    default:
      kind_str = NULL;
      /*
       * The alternative is to use `snprintf(buffer)` but the latter is
       * not quite portable (see `vsnprintf` in `misc.c` file).
       */
    }
  }

  if (NULL != kind_str) {
    GC_err_printf("%p (%s:%d," IF_NOT_SHORTDBG_HDRS(" sz= %lu,") " %s)\n",
                  (void *)((ptr_t)ohdr + sizeof(oh)), ohdr->oh_string,
                  GET_OH_LINENUM(ohdr) /*, */
                  COMMA_IFNOT_SHORTDBG_HDRS((unsigned long)ohdr->oh_sz),
                  kind_str);
  } else {
    GC_err_printf("%p (%s:%d," IF_NOT_SHORTDBG_HDRS(
                      " sz= %lu,") " kind= %d, descr= 0x%lx)\n",
                  (void *)((ptr_t)ohdr + sizeof(oh)), ohdr->oh_string,
                  GET_OH_LINENUM(ohdr) /*, */
                  COMMA_IFNOT_SHORTDBG_HDRS((unsigned long)ohdr->oh_sz),
                  kind, (unsigned long)hhdr->hb_descr);
  }
  PRINT_CALL_CHAIN(ohdr);
}

STATIC void
GC_debug_print_heap_obj_proc(ptr_t base)
{
  GC_ASSERT(I_DONT_HOLD_LOCK());
  if (GC_HAS_DEBUG_INFO(base)) {
    GC_print_obj(base);
  } else {
    GC_default_print_heap_obj_proc(base);
  }
}

#ifndef SHORT_DBG_HDRS
STATIC void GC_check_heap_proc(void);
#elif !defined(NO_FIND_LEAK)
static void
do_nothing(void)
{
}
#endif /* SHORT_DBG_HDRS */

/*
 * Turn on the debugging mode.  Should not be called if
 * `GC_debugging_initialized` is already set.
 */
STATIC void
GC_start_debugging_inner(void)
{
  GC_ASSERT(I_HOLD_LOCK());
#ifndef SHORT_DBG_HDRS
  GC_check_heap = GC_check_heap_proc;
  GC_print_all_smashed = GC_print_all_smashed_proc;
#elif !defined(NO_FIND_LEAK)
  GC_check_heap = do_nothing;
  GC_print_all_smashed = do_nothing;
#endif
  GC_print_heap_obj = GC_debug_print_heap_obj_proc;
  GC_debugging_initialized = TRUE;
  GC_register_displacement_inner(sizeof(oh));
#if defined(CPPCHECK)
  GC_noop1(GC_debug_header_size);
#endif
}

/*
 * Check the allocation is successful, store debugging info into `base`,
 * start the debugging mode (if not yet), and return displaced pointer.
 */
static void *
store_debug_info(void *base, size_t lb, const char *fn, GC_EXTRA_PARAMS)
{
  void *result;

  if (NULL == base) {
    GC_err_printf("%s(%lu) returning NULL (%s:%d)\n", fn, (unsigned long)lb, s,
                  i);
    return NULL;
  }
  LOCK();
  if (!GC_debugging_initialized)
    GC_start_debugging_inner();
  result = GC_store_debug_info_inner(base, lb, s, i);
  ADD_CALL_CHAIN(base, ra);
  UNLOCK();
  return result;
}

const size_t GC_debug_header_size = sizeof(oh);

GC_API size_t GC_CALL
GC_get_debug_header_size(void)
{
  return sizeof(oh);
}

GC_API void GC_CALL
GC_debug_register_displacement(size_t offset)
{
  LOCK();
  GC_register_displacement_inner(offset);
  GC_register_displacement_inner(sizeof(oh) + offset);
  UNLOCK();
}

#ifdef GC_ADD_CALLER
#  if defined(HAVE_DLADDR) && defined(GC_HAVE_RETURN_ADDR_PARENT) \
      && defined(FUNCPTR_IS_DATAPTR)
#    include <dlfcn.h>

STATIC void
GC_caller_func_offset(GC_return_addr_t ra, const char **symp, int *offp)
{
  Dl_info caller;

  if (ra != 0 && dladdr((void *)ra, &caller) && caller.dli_sname != NULL) {
    *symp = caller.dli_sname;
    *offp = (int)((ptr_t)ra - (ptr_t)caller.dli_saddr);
  }
  if (NULL == *symp) {
    *symp = "unknown";
    /* Note: `*offp` is unchanged. */
  }
}
#  else
#    define GC_caller_func_offset(ra, symp, offp) (void)(*(symp) = "unknown")
#  endif
#endif /* GC_ADD_CALLER */

GC_API GC_ATTR_MALLOC void *GC_CALL
GC_debug_malloc(size_t lb, GC_EXTRA_PARAMS)
{
  void *base;

  /*
   * Note that according to `malloc()` specification, if size (`lb`) is
   * zero, then `malloc()` returns either `NULL`, or a unique pointer
   * value that can later be successfully passed to `free()`.
   * We always do the latter.
   */
#if defined(_FORTIFY_SOURCE) && !defined(__clang__)
  /* Workaround to avoid "exceeds maximum object size" gcc warning. */
  base = GC_malloc(lb < GC_SIZE_MAX - DEBUG_BYTES ? lb + DEBUG_BYTES
                                                  : GC_SIZE_MAX >> 1);
#else
  base = GC_malloc(SIZET_SAT_ADD(lb, DEBUG_BYTES));
#endif
#ifdef GC_ADD_CALLER
  if (NULL == s) {
    GC_caller_func_offset(ra, &s, &i);
  }
#endif
  return store_debug_info(base, lb, "GC_debug_malloc", OPT_RA s, i);
}

GC_API GC_ATTR_MALLOC void *GC_CALL
GC_debug_malloc_ignore_off_page(size_t lb, GC_EXTRA_PARAMS)
{
  void *base = GC_malloc_ignore_off_page(SIZET_SAT_ADD(lb, DEBUG_BYTES));

  return store_debug_info(base, lb, "GC_debug_malloc_ignore_off_page",
                          OPT_RA s, i);
}

GC_API GC_ATTR_MALLOC void *GC_CALL
GC_debug_malloc_atomic_ignore_off_page(size_t lb, GC_EXTRA_PARAMS)
{
  void *base
      = GC_malloc_atomic_ignore_off_page(SIZET_SAT_ADD(lb, DEBUG_BYTES));

  return store_debug_info(base, lb, "GC_debug_malloc_atomic_ignore_off_page",
                          OPT_RA s, i);
}

STATIC void *
GC_debug_generic_malloc(size_t lb, int kind, GC_EXTRA_PARAMS)
{
  void *base = GC_generic_malloc_aligned(SIZET_SAT_ADD(lb, DEBUG_BYTES), kind,
                                         0 /* `flags` */, 0 /* `align_m1` */);

  return store_debug_info(base, lb, "GC_debug_generic_malloc", OPT_RA s, i);
}

#ifdef DBG_HDRS_ALL
GC_INNER void *
GC_debug_generic_malloc_inner(size_t lb, int kind, unsigned flags)
{
  void *base, *result;

  GC_ASSERT(I_HOLD_LOCK());
  base = GC_generic_malloc_inner(SIZET_SAT_ADD(lb, DEBUG_BYTES), kind, flags);
  if (NULL == base) {
    GC_err_printf("GC internal allocation (%lu bytes) returning NULL\n",
                  (unsigned long)lb);
    return NULL;
  }
  if (!GC_debugging_initialized)
    GC_start_debugging_inner();
  result = GC_store_debug_info_inner(base, lb, "INTERNAL", 0);
  ADD_CALL_CHAIN_INNER(base);
  return result;
}
#endif /* DBG_HDRS_ALL */

#ifndef CPPCHECK
GC_API void *GC_CALL
GC_debug_malloc_stubborn(size_t lb, GC_EXTRA_PARAMS)
{
  return GC_debug_malloc(lb, OPT_RA s, i);
}

GC_API void GC_CALL
GC_debug_change_stubborn(const void *p)
{
  UNUSED_ARG(p);
}
#endif /* !CPPCHECK */

GC_API void GC_CALL
GC_debug_end_stubborn_change(const void *p)
{
  const void *q = GC_base_C(p);

  if (NULL == q) {
    ABORT_ARG1("GC_debug_end_stubborn_change: bad arg", ": %p", p);
  }
  GC_end_stubborn_change(q);
}

GC_API void GC_CALL
GC_debug_ptr_store_and_dirty(void *p, const void *q)
{
  *(void **)GC_is_visible(p)
      = GC_is_valid_displacement(GC_CAST_AWAY_CONST_PVOID(q));
  GC_debug_end_stubborn_change(p);
  REACHABLE_AFTER_DIRTY(q);
}

GC_API GC_ATTR_MALLOC void *GC_CALL
GC_debug_malloc_atomic(size_t lb, GC_EXTRA_PARAMS)
{
  void *base = GC_malloc_atomic(SIZET_SAT_ADD(lb, DEBUG_BYTES));

  return store_debug_info(base, lb, "GC_debug_malloc_atomic", OPT_RA s, i);
}

GC_API GC_ATTR_MALLOC char *GC_CALL
GC_debug_strdup(const char *str, GC_EXTRA_PARAMS)
{
  char *copy;
  size_t lb;
  if (str == NULL) {
    if (GC_find_leak_inner)
      GC_err_printf("strdup(NULL) behavior is undefined\n");
    return NULL;
  }

  lb = strlen(str) + 1;
  copy = (char *)GC_debug_malloc_atomic(lb, OPT_RA s, i);
  if (copy == NULL) {
#ifndef MSWINCE
    errno = ENOMEM;
#endif
    return NULL;
  }
  BCOPY(str, copy, lb);
  return copy;
}

GC_API GC_ATTR_MALLOC char *GC_CALL
GC_debug_strndup(const char *str, size_t size, GC_EXTRA_PARAMS)
{
  char *copy;
  /* `str` is expected to be non-`NULL`. */
  size_t len = strlen(str);

  if (len > size)
    len = size;
  copy = (char *)GC_debug_malloc_atomic(len + 1, OPT_RA s, i);
  if (copy == NULL) {
#ifndef MSWINCE
    errno = ENOMEM;
#endif
    return NULL;
  }
  if (len > 0)
    BCOPY(str, copy, len);
  copy[len] = '\0';
  return copy;
}

#ifdef GC_REQUIRE_WCSDUP
#  include <wchar.h> /*< for `wcslen()` */

GC_API GC_ATTR_MALLOC wchar_t *GC_CALL
GC_debug_wcsdup(const wchar_t *str, GC_EXTRA_PARAMS)
{
  size_t lb = (wcslen(str) + 1) * sizeof(wchar_t);
  wchar_t *copy = (wchar_t *)GC_debug_malloc_atomic(lb, OPT_RA s, i);
  if (copy == NULL) {
#  ifndef MSWINCE
    errno = ENOMEM;
#  endif
    return NULL;
  }
  BCOPY(str, copy, lb);
  return copy;
}
#endif /* GC_REQUIRE_WCSDUP */

GC_API GC_ATTR_MALLOC void *GC_CALL
GC_debug_malloc_uncollectable(size_t lb, GC_EXTRA_PARAMS)
{
  void *base
      = GC_malloc_uncollectable(SIZET_SAT_ADD(lb, UNCOLLECTABLE_DEBUG_BYTES));

  return store_debug_info(base, lb, "GC_debug_malloc_uncollectable", OPT_RA s,
                          i);
}

#ifdef GC_ATOMIC_UNCOLLECTABLE
GC_API GC_ATTR_MALLOC void *GC_CALL
GC_debug_malloc_atomic_uncollectable(size_t lb, GC_EXTRA_PARAMS)
{
  void *base = GC_malloc_atomic_uncollectable(
      SIZET_SAT_ADD(lb, UNCOLLECTABLE_DEBUG_BYTES));

  return store_debug_info(base, lb, "GC_debug_malloc_atomic_uncollectable",
                          OPT_RA s, i);
}
#endif /* GC_ATOMIC_UNCOLLECTABLE */

#ifdef LINT2
#  include "private/gc_alloc_ptrs.h"
#endif

GC_API void GC_CALL
GC_debug_free(void *p)
{
  ptr_t base;
  if (0 == p)
    return;

  base = (ptr_t)GC_base(p);
  if (NULL == base) {
#if defined(REDIRECT_MALLOC)                                           \
    && ((defined(NEED_CALLINFO) && defined(GC_HAVE_BUILTIN_BACKTRACE)) \
        || defined(REDIR_MALLOC_AND_LINUX_THREADS)                     \
        || (defined(SOLARIS) && defined(THREADS)) || defined(MSWIN32))
    /*
     * In some cases, we should ignore objects that do not belong to
     * the collector heap.  See the comment in `GC_free()`.
     */
    if (!GC_is_heap_ptr(p))
      return;
#endif
    ABORT_ARG1("Invalid pointer passed to free()", ": %p", p);
  }
  if ((word)((ptr_t)p - base) != sizeof(oh)) {
#if defined(REDIRECT_FREE) && defined(USE_PROC_FOR_LIBRARIES)
    /*
     * TODO: Suppress the warning if `free()` caller is in `libpthread`
     * or `libdl`.
     */
#endif
    /*
     * TODO: Suppress the warning for objects allocated by `GC_memalign`
     * and friends (these ones do not have the debugging counterpart).
     */
    GC_err_printf("GC_debug_free called on pointer %p w/o debugging info\n",
                  p);
  } else {
#ifndef SHORT_DBG_HDRS
    ptr_t clobbered = GC_check_annotated_obj((oh *)base);
    size_t sz = GC_size(base);

    if (clobbered != NULL) {
      /* No "release" barrier is needed. */
      GC_SET_HAVE_ERRORS();
      if (((oh *)base)->oh_sz == (GC_uintptr_t)sz) {
        GC_print_smashed_obj(
            "GC_debug_free: found previously deallocated (?) object at", p,
            clobbered);
        /* Ignore double free. */
        return;
      } else {
        GC_print_smashed_obj("GC_debug_free: found smashed location at", p,
                             clobbered);
      }
    }
    /* Invalidate the size (mark the object as deallocated). */
    ((oh *)base)->oh_sz = (GC_uintptr_t)sz;
#endif /* !SHORT_DBG_HDRS */
  }
#ifndef NO_FIND_LEAK
  if (GC_find_leak_inner
#  ifndef SHORT_DBG_HDRS
      && ((word)((ptr_t)p - base) != sizeof(oh) || !GC_findleak_delay_free)
#  endif
  ) {
    GC_free(base);
  } else
#endif
  /* else */ {
    const hdr *hhdr;

    LOCK();
#ifdef LOG_ALLOCS
    GC_log_printf("GC_debug_free(%p) after GC #%lu\n", p,
                  (unsigned long)GC_gc_no);
#endif
    hhdr = HDR(p);
    if (hhdr->hb_obj_kind == UNCOLLECTABLE
#ifdef GC_ATOMIC_UNCOLLECTABLE
        || hhdr->hb_obj_kind == AUNCOLLECTABLE
#endif
    ) {
      GC_free_inner(base);
      UNLOCK();
    } else {
      size_t sz = hhdr->hb_sz;
      size_t i, lpw;

      /*
       * Update the counter even though the real deallocation
       * is deferred.
       */
#ifdef LINT2
      GC_incr_bytes_freed(sz);
#else
      GC_bytes_freed += sz;
#endif
      UNLOCK();
      lpw = BYTES_TO_PTRS(sz - sizeof(oh));
      for (i = 0; i < lpw; ++i)
        ((GC_uintptr_t *)p)[i] = GC_FREED_MEM_MARKER;
      GC_ASSERT((GC_uintptr_t *)p + i == (GC_uintptr_t *)(base + sz));
    }
  }
}

#if defined(THREADS) && defined(DBG_HDRS_ALL)
GC_INNER void
GC_debug_free_inner(void *p)
{
  ptr_t base = (ptr_t)GC_base(p);

  GC_ASSERT(I_HOLD_LOCK());
  GC_ASSERT((word)((ptr_t)p - base) == sizeof(oh));
#  ifdef LINT2
  if (!base)
    ABORT("Invalid GC_debug_free_inner argument");
#  endif
#  ifndef SHORT_DBG_HDRS
  /* Invalidate the size. */
  ((oh *)base)->oh_sz = (GC_uintptr_t)GC_size(base);
#  endif
  GC_free_inner(base);
}
#endif

GC_API void *GC_CALL
GC_debug_realloc(void *p, size_t lb, GC_EXTRA_PARAMS)
{
  ptr_t base;
  void *result;
  const hdr *hhdr;

  if (NULL == p) {
    return GC_debug_malloc(lb, OPT_RA s, i);
  }
  if (0 == lb) /* `&& p != NULL` */ {
    GC_debug_free(p);
    return NULL;
  }

#ifdef GC_ADD_CALLER
  if (NULL == s) {
    GC_caller_func_offset(ra, &s, &i);
  }
#endif
  base = (ptr_t)GC_base(p);
  if (NULL == base) {
    ABORT_ARG1("Invalid pointer passed to realloc()", ": %p", p);
  }
  if ((word)((ptr_t)p - base) != sizeof(oh)) {
    GC_err_printf("GC_debug_realloc called on pointer %p w/o debugging info\n",
                  p);
    return GC_realloc(p, lb);
  }
  hhdr = HDR(base);
  result
      = GC_debug_generic_or_special_malloc(lb, hhdr->hb_obj_kind, OPT_RA s, i);
  if (result != NULL) {
    size_t old_sz;
#ifdef SHORT_DBG_HDRS
    old_sz = GC_size(base) - sizeof(oh);
#else
    old_sz = (size_t)(((oh *)base)->oh_sz);
#endif
    if (old_sz > 0)
      BCOPY(p, result, old_sz < lb ? old_sz : lb);
    GC_debug_free(p);
  }
  return result;
}

GC_API GC_ATTR_MALLOC void *GC_CALL
GC_debug_generic_or_special_malloc(size_t lb, int kind, GC_EXTRA_PARAMS)
{
  switch (kind) {
  case PTRFREE:
    return GC_debug_malloc_atomic(lb, OPT_RA s, i);
  case NORMAL:
    return GC_debug_malloc(lb, OPT_RA s, i);
  case UNCOLLECTABLE:
    return GC_debug_malloc_uncollectable(lb, OPT_RA s, i);
#ifdef GC_ATOMIC_UNCOLLECTABLE
  case AUNCOLLECTABLE:
    return GC_debug_malloc_atomic_uncollectable(lb, OPT_RA s, i);
#endif
  default:
    return GC_debug_generic_malloc(lb, kind, OPT_RA s, i);
  }
}

#ifndef SHORT_DBG_HDRS

/*
 * Check all marked objects in the given block for validity.
 * Note: avoid `GC_apply_to_each_object` for performance reasons.
 */
STATIC void GC_CALLBACK
GC_check_heap_block(struct hblk *hbp, void *dummy)
{
  const hdr *hhdr = HDR(hbp);
  ptr_t p = hbp->hb_body;
  ptr_t plim;
  size_t sz = hhdr->hb_sz;
  size_t bit_no;

  UNUSED_ARG(dummy);
  GC_ASSERT((ptr_t)hhdr->hb_block == p);
  plim = sz > MAXOBJBYTES ? p : p + HBLKSIZE - sz;
  /* Go through all objects in block. */
  for (bit_no = 0; ADDR_GE(plim, p); bit_no += MARK_BIT_OFFSET(sz), p += sz) {
    if (mark_bit_from_hdr(hhdr, bit_no) && GC_HAS_DEBUG_INFO(p)) {
      ptr_t clobbered = GC_check_annotated_obj((oh *)p);

      if (clobbered != NULL)
        GC_add_smashed(clobbered);
    }
  }
}

/*
 * This assumes that all accessible objects are marked.
 * Normally called by collector.
 */
STATIC void
GC_check_heap_proc(void)
{
  GC_ASSERT(I_HOLD_LOCK());
  GC_STATIC_ASSERT((sizeof(oh) & (GC_GRANULE_BYTES - 1)) == 0);
  /* FIXME: Should we check for twice that alignment? */
  GC_apply_to_all_blocks(GC_check_heap_block, NULL);
}

#endif /* !SHORT_DBG_HDRS */

#ifndef GC_NO_FINALIZATION

struct closure {
  GC_finalization_proc cl_fn;
  void *cl_data;
};

STATIC void *
GC_make_closure(GC_finalization_proc fn, void *data)
{
  struct closure *result =
#  ifdef DBG_HDRS_ALL
      (struct closure *)GC_debug_malloc(sizeof(struct closure), GC_EXTRAS);
#  else
      (struct closure *)GC_malloc(sizeof(struct closure));
#  endif
  if (result != NULL) {
    result->cl_fn = fn;
    result->cl_data = data;
  }
  return result;
}

/*
 * An auxiliary function to make finalization work correctly with
 * displaced pointers introduced by the debugging allocators.
 */
STATIC void GC_CALLBACK
GC_debug_invoke_finalizer(void *obj, void *data)
{
  struct closure *cl = (struct closure *)data;

  cl->cl_fn((ptr_t)obj + sizeof(oh), cl->cl_data);
}

/* Special `finalizer_proc` value to detect `GC_register_finalizer` failure. */
#  define OFN_UNSET ((GC_finalization_proc)(~(GC_funcptr_uint)0))

/* Set `ofn` and `ocd` to reflect the values we got back. */
static void
store_old(void *obj, GC_finalization_proc my_old_fn, struct closure *my_old_cd,
          GC_finalization_proc *ofn, void **ocd)
{
  if (my_old_fn != 0) {
    if (my_old_fn == OFN_UNSET) {
      /* `GC_register_finalizer()` failed; `*ofn` and `*ocd` are unchanged. */
      return;
    }
    if (my_old_fn != GC_debug_invoke_finalizer) {
      GC_err_printf("Debuggable object at %p had a non-debug finalizer\n",
                    obj);
      /* This should probably be fatal. */
    } else {
      if (ofn)
        *ofn = my_old_cd->cl_fn;
      if (ocd)
        *ocd = my_old_cd->cl_data;
    }
  } else {
    if (ofn)
      *ofn = 0;
    if (ocd)
      *ocd = NULL;
  }
}

GC_API void GC_CALL
GC_debug_register_finalizer(void *obj, GC_finalization_proc fn, void *cd,
                            GC_finalization_proc *ofn, void **ocd)
{
  GC_finalization_proc my_old_fn = OFN_UNSET;
  void *my_old_cd = NULL; /*< to avoid "might be uninitialized" warning */
  ptr_t base = (ptr_t)GC_base(obj);

  if (NULL == base) {
    /* We will not collect it, hence finalizer would not be run. */
    if (ocd)
      *ocd = NULL;
    if (ofn)
      *ofn = 0;
    return;
  }
  if ((ptr_t)obj - base != sizeof(oh)) {
    GC_err_printf("GC_debug_register_finalizer called with"
                  " non-base-pointer %p\n",
                  obj);
  }
  if (0 == fn) {
    GC_register_finalizer(base, 0, NULL, &my_old_fn, &my_old_cd);
  } else {
    cd = GC_make_closure(fn, cd);
    if (NULL == cd) {
      /* Out of memory; `*ofn` and `*ocd` are unchanged. */
      return;
    }
    GC_register_finalizer(base, GC_debug_invoke_finalizer, cd, &my_old_fn,
                          &my_old_cd);
  }
  store_old(obj, my_old_fn, (struct closure *)my_old_cd, ofn, ocd);
}

GC_API void GC_CALL
GC_debug_register_finalizer_no_order(void *obj, GC_finalization_proc fn,
                                     void *cd, GC_finalization_proc *ofn,
                                     void **ocd)
{
  GC_finalization_proc my_old_fn = OFN_UNSET;
  void *my_old_cd = NULL;
  ptr_t base = (ptr_t)GC_base(obj);
  if (NULL == base) {
    if (ocd)
      *ocd = NULL;
    if (ofn)
      *ofn = 0;
    return;
  }
  if ((ptr_t)obj - base != sizeof(oh)) {
    GC_err_printf("GC_debug_register_finalizer_no_order called with"
                  " non-base-pointer %p\n",
                  obj);
  }
  if (0 == fn) {
    GC_register_finalizer_no_order(base, 0, NULL, &my_old_fn, &my_old_cd);
  } else {
    cd = GC_make_closure(fn, cd);
    if (NULL == cd) {
      /* Out of memory. */
      return;
    }
    GC_register_finalizer_no_order(base, GC_debug_invoke_finalizer, cd,
                                   &my_old_fn, &my_old_cd);
  }
  store_old(obj, my_old_fn, (struct closure *)my_old_cd, ofn, ocd);
}

GC_API void GC_CALL
GC_debug_register_finalizer_unreachable(void *obj, GC_finalization_proc fn,
                                        void *cd, GC_finalization_proc *ofn,
                                        void **ocd)
{
  GC_finalization_proc my_old_fn = OFN_UNSET;
  void *my_old_cd = NULL;
  ptr_t base = (ptr_t)GC_base(obj);
  if (NULL == base) {
    if (ocd)
      *ocd = NULL;
    if (ofn)
      *ofn = 0;
    return;
  }
  if ((ptr_t)obj - base != sizeof(oh)) {
    GC_err_printf("GC_debug_register_finalizer_unreachable called with"
                  " non-base-pointer %p\n",
                  obj);
  }
  if (0 == fn) {
    GC_register_finalizer_unreachable(base, 0, NULL, &my_old_fn, &my_old_cd);
  } else {
    cd = GC_make_closure(fn, cd);
    if (NULL == cd) {
      /* Out of memory. */
      return;
    }
    GC_register_finalizer_unreachable(base, GC_debug_invoke_finalizer, cd,
                                      &my_old_fn, &my_old_cd);
  }
  store_old(obj, my_old_fn, (struct closure *)my_old_cd, ofn, ocd);
}

GC_API void GC_CALL
GC_debug_register_finalizer_ignore_self(void *obj, GC_finalization_proc fn,
                                        void *cd, GC_finalization_proc *ofn,
                                        void **ocd)
{
  GC_finalization_proc my_old_fn = OFN_UNSET;
  void *my_old_cd = NULL;
  ptr_t base = (ptr_t)GC_base(obj);
  if (NULL == base) {
    if (ocd)
      *ocd = NULL;
    if (ofn)
      *ofn = 0;
    return;
  }
  if ((ptr_t)obj - base != sizeof(oh)) {
    GC_err_printf("GC_debug_register_finalizer_ignore_self called with"
                  " non-base-pointer %p\n",
                  obj);
  }
  if (0 == fn) {
    GC_register_finalizer_ignore_self(base, 0, NULL, &my_old_fn, &my_old_cd);
  } else {
    cd = GC_make_closure(fn, cd);
    if (NULL == cd) {
      /* Out of memory. */
      return;
    }
    GC_register_finalizer_ignore_self(base, GC_debug_invoke_finalizer, cd,
                                      &my_old_fn, &my_old_cd);
  }
  store_old(obj, my_old_fn, (struct closure *)my_old_cd, ofn, ocd);
}

#  ifndef GC_TOGGLE_REFS_NOT_NEEDED
GC_API int GC_CALL
GC_debug_toggleref_add(void *obj, int is_strong_ref)
{
  ptr_t base = (ptr_t)GC_base(obj);

  if ((ptr_t)obj - base != sizeof(oh)) {
    GC_err_printf("GC_debug_toggleref_add called with"
                  " non-base-pointer %p\n",
                  obj);
  }
  return GC_toggleref_add(base, is_strong_ref);
}
#  endif /* !GC_TOGGLE_REFS_NOT_NEEDED */

#endif /* !GC_NO_FINALIZATION */

GC_API GC_ATTR_MALLOC void *GC_CALL
GC_debug_malloc_replacement(size_t lb)
{
  return GC_debug_malloc(lb, GC_DBG_EXTRAS);
}

GC_API void *GC_CALL
GC_debug_realloc_replacement(void *p, size_t lb)
{
  return GC_debug_realloc(p, lb, GC_DBG_EXTRAS);
}

#ifdef GC_GCJ_SUPPORT
#  include "gc/gc_gcj.h"

GC_API GC_ATTR_MALLOC void *GC_CALL
GC_debug_gcj_malloc(size_t lb, const void *vtable_ptr, GC_EXTRA_PARAMS)
{
  void *base, *result;

  /* We are careful to avoid extra calls those could confuse the backtrace. */
  LOCK();
  /* A mechanism to invoke finalizers (same as in `GC_core_gcj_malloc`). */
  if (GC_gc_no != GC_last_finalized_no) {
    UNLOCK();
    GC_notify_or_invoke_finalizers();
    LOCK();
    GC_last_finalized_no = GC_gc_no;
  }

  base = GC_generic_malloc_inner(SIZET_SAT_ADD(lb, DEBUG_BYTES),
                                 GC_gcj_debug_kind, 0 /* `flags` */);
  if (NULL == base) {
    GC_oom_func oom_fn = GC_oom_fn;
    UNLOCK();
    GC_err_printf("GC_debug_gcj_malloc(%lu, %p) returning NULL (%s:%d)\n",
                  (unsigned long)lb, vtable_ptr, s, i);
    return (*oom_fn)(lb);
  }
  *((const void **)((ptr_t)base + sizeof(oh))) = vtable_ptr;
  if (!GC_debugging_initialized)
    GC_start_debugging_inner();
  result = GC_store_debug_info_inner(base, lb, s, i);
  ADD_CALL_CHAIN(base, ra);
  UNLOCK();
  GC_dirty(result);
  REACHABLE_AFTER_DIRTY(vtable_ptr);
  return result;
}
#endif /* GC_GCJ_SUPPORT */
