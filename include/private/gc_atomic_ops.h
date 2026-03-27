/*
 * Copyright (c) 2017 Ivan Maidanski
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
 * This is a private collector header which provides an implementation of
 * `libatomic_ops` subset primitives sufficient for the collector assuming
 * that gcc atomic intrinsics are available (and have the correct
 * implementation).  This is enabled by defining `GC_BUILTIN_ATOMIC` macro.
 * Otherwise, `libatomic_ops` library is used to define the primitives.
 */

#ifndef GC_ATOMIC_OPS_H
#define GC_ATOMIC_OPS_H

#ifdef GC_BUILTIN_ATOMIC

#  include "gc/gc.h" /*< for `size_t` */

#  ifdef __cplusplus
extern "C" {
#  endif

typedef size_t AO_t;

#  ifdef GC_PRIVATE_H /*< i.e. have `GC_INLINE` */
#    define AO_INLINE GC_INLINE
#  else
#    define AO_INLINE static __inline
#  endif

#  if !defined(THREAD_SANITIZER) && !defined(GC_PRIVATE_H)
/* Similar to that in `gcconfig.h` file. */
#    if defined(__has_feature) && !defined(CPPCHECK)
#      if __has_feature(thread_sanitizer)
#        define THREAD_SANITIZER
#      endif
#    elif defined(__SANITIZE_THREAD__)
#      define THREAD_SANITIZER
#    endif
#  endif /* !THREAD_SANITIZER && !GC_PRIVATE_H */

typedef unsigned char AO_TS_t;
#  define AO_TS_CLEAR 0
#  define AO_TS_INITIALIZER ((AO_TS_t)AO_TS_CLEAR)
#  if defined(__GCC_ATOMIC_TEST_AND_SET_TRUEVAL) && !defined(CPPCHECK)
#    define AO_TS_SET __GCC_ATOMIC_TEST_AND_SET_TRUEVAL
#  else
#    define AO_TS_SET (AO_TS_t)1 /*< true */
#  endif
#  define AO_CLEAR(p) __atomic_clear(p, __ATOMIC_RELEASE)
#  define AO_test_and_set_acquire(p) \
    (__atomic_test_and_set(p, __ATOMIC_ACQUIRE) ? AO_TS_SET : AO_TS_CLEAR)
#  define AO_HAVE_test_and_set_acquire

#  define AO_compiler_barrier() __atomic_signal_fence(__ATOMIC_SEQ_CST)

#  if defined(THREAD_SANITIZER) && !defined(AO_USE_ATOMIC_THREAD_FENCE)
/*
 * Workaround a compiler warning (reported by gcc-11, at least) that
 * `__atomic_thread_fence` is unsupported with thread sanitizer.
 */
AO_INLINE void
AO_nop_full(void)
{
  volatile AO_TS_t dummy = AO_TS_INITIALIZER;
  (void)__atomic_test_and_set(&dummy, __ATOMIC_SEQ_CST);
}
#  else
#    define AO_nop_full() __atomic_thread_fence(__ATOMIC_SEQ_CST)
#  endif
#  define AO_HAVE_nop_full

#  define AO_fetch_and_add(p, v) __atomic_fetch_add(p, v, __ATOMIC_RELAXED)
#  define AO_HAVE_fetch_and_add
#  define AO_fetch_and_add1(p) AO_fetch_and_add(p, 1)
#  define AO_HAVE_fetch_and_add1
#  define AO_fetch_and_sub1(p) AO_fetch_and_add(p, ~(AO_t)0 /* -1 */)
#  define AO_HAVE_fetch_and_sub1

#  define AO_or(p, v) (void)__atomic_or_fetch(p, v, __ATOMIC_RELAXED)
#  define AO_HAVE_or

#  define AO_load(p) __atomic_load_n(p, __ATOMIC_RELAXED)
#  define AO_HAVE_load
#  define AO_load_acquire(p) __atomic_load_n(p, __ATOMIC_ACQUIRE)
#  define AO_HAVE_load_acquire
/*
 * `AO_load_acquire_read(p)` is not defined as it is unused, but we need
 * its `AO_HAVE_` macro defined.
 */
#  define AO_HAVE_load_acquire_read

#  define AO_store(p, v) __atomic_store_n(p, v, __ATOMIC_RELAXED)
#  define AO_HAVE_store
#  define AO_store_release(p, v) __atomic_store_n(p, v, __ATOMIC_RELEASE)
#  define AO_HAVE_store_release
#  define AO_store_release_write(p, v) AO_store_release(p, v)
#  define AO_HAVE_store_release_write

#  define AO_char_load(p) __atomic_load_n(p, __ATOMIC_RELAXED)
#  define AO_HAVE_char_load
#  define AO_char_store(p, v) __atomic_store_n(p, v, __ATOMIC_RELAXED)
#  define AO_HAVE_char_store
#  define AO_char_fetch_and_add1(p) __atomic_fetch_add(p, 1, __ATOMIC_RELAXED)
#  define AO_HAVE_char_fetch_and_add1

#  ifdef AO_REQUIRE_CAS
AO_INLINE int
AO_compare_and_swap_release(volatile AO_t *p, AO_t ov, AO_t nv)
{
  return (int)__atomic_compare_exchange_n(p, &ov, nv, 0, __ATOMIC_RELEASE,
                                          __ATOMIC_RELAXED /* on fail */);
}
#    define AO_HAVE_compare_and_swap_release
#  endif

#  ifdef __cplusplus
} /* extern "C" */
#  endif

#  ifndef NO_LOCKFREE_AO_OR
/* `__atomic_or_fetch()` is assumed to be lock-free. */
#    define HAVE_LOCKFREE_AO_OR 1
#  endif

#else
/* Fall back to `libatomic_ops`. */
#  include "atomic_ops.h"

/*
 * `AO_compiler_barrier`, `AO_load` and `AO_store` should be defined
 * for all targets; the rest of the primitives are guaranteed to exist
 * only if `AO_REQUIRE_CAS` is defined (or if the corresponding
 * `AO_HAVE_` macro is defined).  i686 and x86_64 targets have
 * `AO_nop_full`, `AO_load_acquire`, `AO_store_release`, at least.
 */
#  if (!defined(AO_HAVE_load) || !defined(AO_HAVE_store)) && !defined(CPPCHECK)
#    error AO_load or AO_store is missing; probably old version of atomic_ops
#  endif

#endif /* !GC_BUILTIN_ATOMIC */

#if defined(GC_BUILTIN_ATOMIC) || defined(__CHERI_PURE_CAPABILITY__)
/*
 * Assume that gcc atomic intrinsics are available (and have correct
 * implementation).  `p` should be of a pointer to `ptr_t` (`char *`) value.
 */
#  define GC_cptr_load(p) __atomic_load_n(p, __ATOMIC_RELAXED)
#  define GC_cptr_load_acquire(p) __atomic_load_n(p, __ATOMIC_ACQUIRE)
#  define GC_cptr_load_acquire_read(p) GC_cptr_load_acquire(p)
#  define GC_cptr_store(p, v) __atomic_store_n(p, v, __ATOMIC_RELAXED)
#  define GC_cptr_store_release(p, v) __atomic_store_n(p, v, __ATOMIC_RELEASE)
#  define GC_cptr_store_release_write(p, v) GC_cptr_store_release(p, v)
#  ifdef AO_REQUIRE_CAS
AO_INLINE int
GC_cptr_compare_and_swap(char *volatile *p, char *ov, char *nv)
{
  return (int)__atomic_compare_exchange_n(p, &ov, nv, 0, __ATOMIC_RELAXED,
                                          __ATOMIC_RELAXED);
}
#  endif
#else
/*
 * Redirect to the `AO_` primitives.  Assume the size of `AO_t` matches
 * that of a pointer.
 */
#  define GC_cptr_load(p) (char *)AO_load((volatile AO_t *)(p))
#  define GC_cptr_load_acquire(p) (char *)AO_load_acquire((volatile AO_t *)(p))
#  define GC_cptr_load_acquire_read(p) \
    (char *)AO_load_acquire_read((volatile AO_t *)(p))
#  define GC_cptr_store(p, v) AO_store((volatile AO_t *)(p), (AO_t)(v))
#  define GC_cptr_store_release(p, v) \
    AO_store_release((volatile AO_t *)(p), (AO_t)(v))
#  define GC_cptr_store_release_write(p, v) \
    AO_store_release_write((volatile AO_t *)(p), (AO_t)(v))
#  ifdef AO_REQUIRE_CAS
#    define GC_cptr_compare_and_swap(p, ov, nv) \
      AO_compare_and_swap((volatile AO_t *)(p), (AO_t)(ov), (AO_t)(nv))
#  endif
#endif /* !GC_BUILTIN_ATOMIC */

#endif /* GC_ATOMIC_OPS_H */
