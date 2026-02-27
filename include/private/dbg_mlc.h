/*
 * Copyright 1988, 1989 Hans-J. Boehm, Alan J. Demers
 * Copyright (c) 1991-1995 by Xerox Corporation.  All rights reserved.
 * Copyright (c) 1997 by Silicon Graphics.  All rights reserved.
 * Copyright (c) 1999 by Hewlett-Packard Company.  All rights reserved.
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
 * This is mostly an internal header file.  Typical clients should
 * not use it.  Clients that define their own object kinds with
 * debugging allocators will probably want to include this, however.
 * No attempt is made to keep the namespace clean.  This should not be
 * included from header files that are frequently included by clients.
 */

#ifndef GC_DBG_MLC_H
#define GC_DBG_MLC_H

#include "gc_priv.h"
#ifdef KEEP_BACK_PTRS
#  include "gc/gc_backptr.h"
#endif

EXTERN_C_BEGIN

#ifndef GC_FREED_MEM_MARKER
#  if CPP_WORDSZ == 32
#    define GC_FREED_MEM_MARKER (GC_uintptr_t)0xdeadbeef
#  else
#    define GC_FREED_MEM_MARKER ((GC_uintptr_t)GC_WORD_C(0xEFBEADDEdeadbeef))
#  endif
#endif /* !GC_FREED_MEM_MARKER */

/*
 * Stored both one past the end of user object, and one before the end of
 * the object as seen by the allocator.
 */
#if CPP_WORDSZ == 32
#  define START_FLAG (GC_uintptr_t)0xfedcedcb
#  define END_FLAG (GC_uintptr_t)0xbcdecdef
#else
#  define START_FLAG ((GC_uintptr_t)GC_WORD_C(0xFEDCEDCBfedcedcb))
#  define END_FLAG ((GC_uintptr_t)GC_WORD_C(0xBCDECDEFbcdecdef))
#endif

#if defined(KEEP_BACK_PTRS) || defined(PRINT_BLACK_LIST)
/*
 * Pointer "source" variants that are not real locations.  Used in
 * `oh_back_ptr` fields and as `source` argument to some marking functions.
 */

/* Object was marked because it is finalizable. */
#  define MARKED_FOR_FINALIZATION ((ptr_t)NUMERIC_TO_VPTR(2))

/*
 * Object was marked from a register.  Hence the "source" of the reference
 * does not have an address.
 */
#  define MARKED_FROM_REGISTER ((ptr_t)NUMERIC_TO_VPTR(4))

#  define NOT_MARKED ((ptr_t)NUMERIC_TO_VPTR(8))
#endif /* KEEP_BACK_PTRS || PRINT_BLACK_LIST */

/*
 * Object debug header.  The size of the structure is assumed not to
 * de-align things, and to be a multiple of a double-pointer length.
 */
typedef struct {
#if defined(KEEP_BACK_PTRS) || defined(MAKE_BACK_GRAPH)
  /*
   * We potentially keep two different kinds of back pointers.
   * `KEEP_BACK_PTRS` stores a single back pointer in each reachable
   * object to allow reporting of why an object was retained.
   * `MAKE_BACK_GRAPH` builds a graph containing the inverse of all
   * "points-to" edges including those involving objects that have just
   * become unreachable.  This allows detection of growing chains of
   * unreachable objects.  It may be possible to eventually combine both,
   * but for now we keep them separate.  Both kinds of back pointers are
   * hidden using the following macros.  In both cases, the plain variant
   * is constrained to have the least significant bit of 1, to allow it
   * to be distinguished from a free-list link.  This means the plain
   * variant must have the least significant bit of zero.
   * Note that blocks dropped by black-listing will also have the least
   * significant bit clear once debugging has started; we are careful never
   * to overwrite such a value.
   */
#  if ALIGNMENT == 1
  /* Fudge back pointer to be even. */
#    define HIDE_BACK_PTR(p) \
      GC_HIDE_POINTER((ptr_t)(~(GC_uintptr_t)1 & (GC_uintptr_t)(p)))
#  else
#    define HIDE_BACK_PTR(p) GC_HIDE_POINTER(p)
#  endif
  /*
   * Always define either none or both of the fields to ensure
   * double-pointer alignment.
   */
  GC_hidden_pointer oh_back_ptr;
  GC_hidden_pointer oh_bg_ptr;
#endif
  const char *oh_string; /*< object descriptor string (file name) */
  GC_signed_word oh_int; /*< object descriptor integer (line number) */
#ifdef NEED_CALLINFO
  struct callinfo oh_ci[NFRAMES];
#endif
#ifndef SHORT_DBG_HDRS
  GC_uintptr_t oh_sz; /*< the original `malloc` argument */
  GC_uintptr_t oh_sf; /*< the "start" flag (marker) */
#endif
} oh;

#define GET_OH_LINENUM(ohdr) ((int)(ohdr)->oh_int)

#ifdef SHORT_DBG_HDRS
#  define DEBUG_BYTES sizeof(oh)
#  define UNCOLLECTABLE_DEBUG_BYTES DEBUG_BYTES
#else
/*
 * Add space for `END_FLAG`, but use any extra space that was already
 * added to catch off-the-end pointers.  For uncollectible objects, the
 * extra byte is not added.
 */
#  define UNCOLLECTABLE_DEBUG_BYTES (sizeof(oh) + sizeof(GC_uintptr_t))
#  define DEBUG_BYTES (UNCOLLECTABLE_DEBUG_BYTES - EXTRA_BYTES)
#endif

/*
 * `ADD_CALL_CHAIN` stores a (partial) call chain into an object header;
 * it should be called with the allocator lock held.
 * `PRINT_CALL_CHAIN` prints the call chain stored in an object to `stderr`;
 * it requires we do not hold the allocator lock.
 */
#if defined(SAVE_CALL_CHAIN)
#  define ADD_CALL_CHAIN(base, ra) GC_save_callers(((oh *)(base))->oh_ci)
#  if defined(REDIRECT_MALLOC)                                     \
      && (defined(DBG_HDRS_ALL) || defined(REDIRECT_MALLOC_DEBUG)) \
      && NARGS == 0 && NFRAMES % 2 == 0 && defined(GC_HAVE_BUILTIN_BACKTRACE)
/*
 * A dummy variant of `GC_save_callers()` which does not call
 * `backtrace()`.
 */
GC_INNER void GC_save_callers_safe(struct callinfo info[NFRAMES]);
#    define ADD_CALL_CHAIN_IS_UNSAFE
/*
 * A variant of `ADD_CALL_CHAIN()` which does not call `libc` functions that
 * might allocate memory.
 */
#    define ADD_CALL_CHAIN_SAFE(base, ra) \
      GC_save_callers_safe(((oh *)(base))->oh_ci)
#  endif
#  define PRINT_CALL_CHAIN(base) GC_print_callers(((oh *)(base))->oh_ci)
#elif defined(GC_ADD_CALLER)
#  define ADD_CALL_CHAIN(base, ra) ((oh *)(base))->oh_ci[0].ci_pc = (ra)
#  define PRINT_CALL_CHAIN(base) GC_print_callers(((oh *)(base))->oh_ci)
#else
#  define ADD_CALL_CHAIN(base, ra)
#  define PRINT_CALL_CHAIN(base)
#endif

#ifndef ADD_CALL_CHAIN_IS_UNSAFE
#  define ADD_CALL_CHAIN_SAFE(base, ra) ADD_CALL_CHAIN(base, ra)
#endif

#ifdef GC_ADD_CALLER
#  define PASS_EXTRA_PARAMS ra, s, i
#else
#  define PASS_EXTRA_PARAMS s, i
#endif

GC_INNER void *GC_debug_malloc_inner(size_t lb, GC_bool is_redirect,
                                     GC_EXTRA_PARAMS);
GC_INNER void *GC_debug_malloc_uncollectable_inner(size_t lb,
                                                   GC_bool is_redirect,
                                                   GC_EXTRA_PARAMS);
GC_INNER void *GC_debug_realloc_inner(void *p, size_t lb, GC_bool free_on_fail,
                                      GC_bool is_redirect, GC_EXTRA_PARAMS);

/*
 * Check whether object given by its base pointer has debugging info.
 * The argument (`base`) is assumed to point to a legitimate object in the
 * collector heap.  This excludes the check as to whether the back pointer
 * is odd, which is added by the `GC_HAS_DEBUG_INFO` macro.  Note that
 * if `DBG_HDRS_ALL` is defined, uncollectible objects on free lists
 * may not have debug information set.  Thus, it is not always safe to
 * return 1 (true), even if the client does its part.  Return -1 if the
 * object with debug info has been marked as deallocated.
 */
#ifdef SHORT_DBG_HDRS
#  define GC_has_other_debug_info(base) 1
#else
GC_INNER int GC_has_other_debug_info(ptr_t base);

GC_INNER void GC_add_smashed(ptr_t smashed);

/*
 * Use `GC_err_printf()` and friends to print a description of the object
 * whose client-visible address is `p`, and which was smashed at memory
 * location pointed by `clobbered`.
 */
GC_INNER void GC_print_smashed_obj(const char *msg, void *p, ptr_t clobbered);

/* Print all objects on the list.  Clear the list. */
GC_INNER void GC_print_all_smashed_proc(void);
#endif /* !SHORT_DBG_HDRS */

#if defined(KEEP_BACK_PTRS) || defined(MAKE_BACK_GRAPH)
#  if defined(SHORT_DBG_HDRS) && !defined(CPPCHECK)
#    error Non-ptr stored in object results in GC_HAS_DEBUG_INFO malfunction
/* We may mistakenly conclude that base has a debugging wrapper. */
#  endif
#  if defined(PARALLEL_MARK) && defined(KEEP_BACK_PTRS)
/*
 * Note: the atomic load is used as `GC_store_back_pointer` stores
 * `oh_back_ptr` atomically (`base` might point to the field); this prevents
 * a TSan warning.
 */
#    define GC_HAS_DEBUG_INFO(base)                                    \
      (((GC_uintptr_t)GC_cptr_load((volatile ptr_t *)(base)) & 1) != 0 \
       && GC_has_other_debug_info(base) > 0)
#  else
#    define GC_HAS_DEBUG_INFO(base)         \
      (((*(GC_uintptr_t *)(base)) & 1) != 0 \
       && GC_has_other_debug_info(base) > 0)
#  endif
#else
#  define GC_HAS_DEBUG_INFO(base) (GC_has_other_debug_info(base) > 0)
#endif /* !KEEP_BACK_PTRS && !MAKE_BACK_GRAPH */

EXTERN_C_END

#endif /* GC_DBG_MLC_H */
