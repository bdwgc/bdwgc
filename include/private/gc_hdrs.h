/*
 * Copyright 1988, 1989 Hans-J. Boehm, Alan J. Demers
 * Copyright (c) 1991-1994 by Xerox Corporation.  All rights reserved.
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

#ifndef GC_HEADERS_H
#define GC_HEADERS_H

#if !defined(GC_PRIVATE_H) && !defined(CPPCHECK)
#  error gc_hdrs.h should be included from gc_priv.h
#endif

#if CPP_WORDSZ != 32 && CPP_WORDSZ < 36 && !defined(CPPCHECK)
#  error Get a real machine
#endif

EXTERN_C_BEGIN

/*
 * The 2-level tree data structure that is used to find block headers.
 * If there are more than 32 bits in a pointer, the top level is a hash
 * table.
 *
 * This defines `HDR()`, `GET_HDR()`, and `SET_HDR()`, the main macros
 * used to retrieve and set object headers.
 *
 * We take advantage of a header lookup cache.  This is a locally declared
 * direct mapped cache, used inside the marker.  The `HC_GET_HDR()` macro
 * uses and maintains this cache.  Assuming we get reasonable hit rates,
 * this saves a few memory references from each pointer validation.
 */

#if CPP_WORDSZ > 32
#  define HASH_TL
#endif

/* Define appropriate out-degrees for each of the two tree levels. */
#if defined(LARGE_CONFIG) || !defined(SMALL_CONFIG)
#  define LOG_BOTTOM_SZ 10
#else
/* Keep top index size reasonable with smaller blocks. */
#  define LOG_BOTTOM_SZ 11
#endif
#define BOTTOM_SZ (1 << LOG_BOTTOM_SZ)

#ifndef HASH_TL
#  define LOG_TOP_SZ (CPP_WORDSZ - LOG_BOTTOM_SZ - LOG_HBLKSIZE)
#else
#  define LOG_TOP_SZ 11
#endif
#define TOP_SZ (1 << LOG_TOP_SZ)

#ifdef COUNT_HDR_CACHE_HITS
extern word GC_hdr_cache_hits; /*< used for debugging/profiling */
extern word GC_hdr_cache_misses;
#  define HC_HIT() (void)(++GC_hdr_cache_hits)
#  define HC_MISS() (void)(++GC_hdr_cache_misses)
#else
#  define HC_HIT() (void)0
#  define HC_MISS() (void)0
#endif

typedef struct hce {
  word block_addr; /*< right-shifted by `LOG_HBLKSIZE` */
  hdr *hce_hdr;
} hdr_cache_entry;

#define HDR_CACHE_SIZE 8 /*< a power of two */

#define DECLARE_HDR_CACHE hdr_cache_entry hdr_cache[HDR_CACHE_SIZE]

#define INIT_HDR_CACHE BZERO(hdr_cache, sizeof(hdr_cache))

#define HCE(h) (hdr_cache + ((ADDR(h) >> LOG_HBLKSIZE) & (HDR_CACHE_SIZE - 1)))

#define HCE_VALID_FOR(hce, h) ((hce)->block_addr == (ADDR(h) >> LOG_HBLKSIZE))

#define HCE_HDR(h) ((hce)->hce_hdr)

#ifdef PRINT_BLACK_LIST
/*
 * Handle a header cache miss.  Returns a pointer to the header
 * corresponding to `p`, if the latter can possibly be a valid object
 * pointer, and `NULL` otherwise.  Guaranteed to return `NULL` for
 * a pointer past the first page of an object unless both
 * `GC_all_interior_pointers` is set and `p` is in fact a valid object
 * pointer.  Never returns a pointer to a free `hblk`.
 */
GC_INNER hdr *GC_header_cache_miss(ptr_t p, hdr_cache_entry *hce,
                                   ptr_t source);

#  define HEADER_CACHE_MISS(p, hce, source) \
    GC_header_cache_miss(p, hce, source)
#else
GC_INNER hdr *GC_header_cache_miss(ptr_t p, hdr_cache_entry *hce);
#  define HEADER_CACHE_MISS(p, hce, source) GC_header_cache_miss(p, hce)
#endif

/*
 * Set `hhdr` to the header for `p`.  Analogous to `GET_HDR()` below,
 * except that in the case of large objects, it gets the header for the
 * object beginning if `GC_all_interior_pointers` is true.  Sets `hhdr`
 * to `NULL` if `p` points to somewhere other than the first page of an
 * object, and it is not a valid pointer to the object.
 */
#define HC_GET_HDR(p, hhdr, source)                \
  { /*< cannot use `do ... while (0)` here */      \
    hdr_cache_entry *hce = HCE(p);                 \
    if (EXPECT(HCE_VALID_FOR(hce, p), TRUE)) {     \
      HC_HIT();                                    \
      hhdr = hce->hce_hdr;                         \
    } else {                                       \
      hhdr = HEADER_CACHE_MISS(p, hce, source);    \
      if (NULL == hhdr)                            \
        break; /*< go to the enclosing loop end */ \
    }                                              \
  }

typedef struct bi {
  /*
   * The bottom-level index contains one of three kinds of values:
   *   - 0 means we are not responsible for this block, or this is
   *     a block other than the first one in a free block;
   *   - 1 < (long)`x` <= `MAX_JUMP` means the block starts at least
   *     `x * HBLKSIZE` bytes before the current address;
   *   - a valid pointer points to a `hdr` structure (the above cannot
   *     be valid pointers due to the `GET_MEM()` return convention).
   */
  hdr *index[BOTTOM_SZ];

  /*
   * All indices are linked in the ascending and descending orders,
   * respectively.
   */
  struct bi *asc_link;
  struct bi *desc_link;

  word key; /*< high-order address bits */
#ifdef HASH_TL
  struct bi *hash_link; /*< hash chain link */
#endif
} bottom_index;

#define MAX_JUMP (HBLKSIZE - 1)

#define HDR_FROM_BI(bi, p) \
  (bi)->index[(ADDR(p) >> LOG_HBLKSIZE) & (BOTTOM_SZ - 1)]
#ifndef HASH_TL
#  define BI(p) GC_top_index[ADDR(p) >> (LOG_BOTTOM_SZ + LOG_HBLKSIZE)]
#  define HDR_INNER(p) HDR_FROM_BI(BI(p), p)
#  ifdef SMALL_CONFIG
#    define HDR(p) GC_find_header(p)
#  else
#    define HDR(p) HDR_INNER(p)
#  endif
#  define GET_BI(p, bottom_indx) (void)((bottom_indx) = BI(p))
#  define GET_HDR(p, hhdr) (void)((hhdr) = HDR(p))
#  define SET_HDR(p, hhdr) (void)(HDR_INNER(p) = (hhdr))
#  define GET_HDR_ADDR(p, ha) (void)((ha) = &HDR_INNER(p))
#else
/* A hash function for the tree top level. */
#  define TL_HASH(hi) ((hi) & (TOP_SZ - 1))
/* Set `bottom_indx` to point to the bottom index for address `p`. */
#  define GET_BI(p, bottom_indx)                                    \
    do {                                                            \
      REGISTER word hi = ADDR(p) >> (LOG_BOTTOM_SZ + LOG_HBLKSIZE); \
      REGISTER bottom_index *_bi = GC_top_index[TL_HASH(hi)];       \
      while (_bi->key != hi && _bi != GC_all_nils)                  \
        _bi = _bi->hash_link;                                       \
      (bottom_indx) = _bi;                                          \
    } while (0)
#  define GET_HDR_ADDR(p, ha)     \
    do {                          \
      REGISTER bottom_index *bi;  \
      GET_BI(p, bi);              \
      (ha) = &HDR_FROM_BI(bi, p); \
    } while (0)
#  define GET_HDR(p, hhdr)  \
    do {                    \
      REGISTER hdr **_ha;   \
      GET_HDR_ADDR(p, _ha); \
      (hhdr) = *_ha;        \
    } while (0)
#  define SET_HDR(p, hhdr)          \
    do {                            \
      REGISTER bottom_index *bi;    \
      GET_BI(p, bi);                \
      GC_ASSERT(bi != GC_all_nils); \
      HDR_FROM_BI(bi, p) = (hhdr);  \
    } while (0)
#  define HDR(p) GC_find_header(p)
#endif

/*
 * Is the result a forwarding address to someplace closer to the
 * beginning of the block or `NULL`?
 */
#define IS_FORWARDING_ADDR_OR_NIL(hhdr) ((size_t)ADDR(hhdr) <= MAX_JUMP)

/*
 * Get an `HBLKSIZE`-aligned address closer to the beginning of the
 * block `h`.  Assumes that `hhdr` is equal to `HDR(h)`,
 * `IS_FORWARDING_ADDR(hhdr)` is true and `hhdr` is not `NULL`.
 * `HDR(result)` is expected to be non-`NULL`.
 */
#define FORWARDED_ADDR(h, hhdr) \
  ((struct hblk *)(h) - (size_t)(GC_uintptr_t)(hhdr))

EXTERN_C_END

#endif /* GC_HEADERS_H */
