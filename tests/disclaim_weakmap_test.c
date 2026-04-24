/*
 * Copyright (c) 2018 Petter A. Urkedal
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

/* This tests a case where disclaim notifiers sometimes return non-zero */
/* in order to protect objects from collection.                         */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef HAVE_CONFIG_H
  /* For GC_[P]THREADS */
# include "config.h"
#endif

#include "gc_disclaim.h" /* includes gc.h */

#if defined(GC_PTHREADS) || defined(LINT2)
# define NOT_GCBUILD
# include "private/gc_priv.h"

  GC_ATTR_NO_SANITIZE_THREAD
  static int GC_rand(void) /* same as in disclaim_test.c */
  {
    static unsigned seed; /* concurrent update does not hurt the test */

    seed = (seed * 1103515245U + 12345) & (~0U >> 1);
    return (int)seed;
  }

# undef rand
# define rand() GC_rand()
#endif /* GC_PTHREADS || LINT2 */

#include "gc_mark.h" /* should not precede include gc_priv.h */

#ifdef GC_PTHREADS
# ifndef NTHREADS
#   define NTHREADS 8
# endif
# include <errno.h>
# include <pthread.h>
# include "private/gc_atomic_ops.h" /* for AO_t and AO_fetch_and_add1 */
#else
# undef NTHREADS
# define NTHREADS 1
# define AO_t GC_word
#endif

#define POP_SIZE 200
#define MUTATE_CNT (5000000 / NTHREADS)
#define GROW_LIMIT (MUTATE_CNT / 10)

#define WEAKMAP_CAPACITY 256
#define WEAKMAP_MUTEX_COUNT 32

/* FINALIZER_CLOSURE_FLAG definition matches the one in fnlz_mlc.c. */
#if defined(KEEP_BACK_PTRS) || defined(MAKE_BACK_GRAPH)
# define FINALIZER_CLOSURE_FLAG 0x2
# define INVALIDATE_FLAG 0x1
#else
# define FINALIZER_CLOSURE_FLAG 0x1
# define INVALIDATE_FLAG 0x2
#endif

#define my_assert(e) \
    if (!(e)) { \
      fflush(stdout); \
      fprintf(stderr, "Assertion failure, line %d: %s\n", __LINE__, #e); \
      exit(70); \
    }

#define CHECK_OOM(p) \
    do { \
        if (NULL == (p)) { \
            fprintf(stderr, "Out of memory\n"); \
            exit(69); \
        } \
    } while (0)

#ifndef AO_HAVE_fetch_and_add1
# define AO_fetch_and_add1(p) ((*(p))++)
                /* This is used only to update counters.        */
#endif

unsigned memhash(void *src, size_t len)
{
  unsigned acc = 0;
  size_t i;

  my_assert(len % sizeof(GC_word) == 0);
  for (i = 0; i < len / sizeof(GC_word); ++i) {
    acc = (unsigned)((2003 * (GC_word)acc + ((GC_word *)src)[i]) / 3);
  }
  return acc;
}

static volatile AO_t stat_added;
static volatile AO_t stat_found;
static volatile AO_t stat_removed;
static volatile AO_t stat_skip_locked;
static volatile AO_t stat_skip_marked;

struct weakmap_link {
  GC_hidden_pointer obj;
  struct weakmap_link *next;
};

struct weakmap {
# ifdef GC_PTHREADS
    pthread_mutex_t mutex[WEAKMAP_MUTEX_COUNT];
# endif
  size_t key_size;
  size_t obj_size;
  size_t capacity;
  unsigned weakobj_kind;
  struct weakmap_link **links; /* NULL means weakmap is destroyed */
};

void weakmap_lock(struct weakmap *wm, unsigned h)
{
# ifdef GC_PTHREADS
    int err = pthread_mutex_lock(&wm->mutex[h % WEAKMAP_MUTEX_COUNT]);
    my_assert(0 == err);
# else
    (void)wm; (void)h;
# endif
}

int weakmap_trylock(struct weakmap *wm, unsigned h)
{
# ifdef GC_PTHREADS
    int err = pthread_mutex_trylock(&wm->mutex[h % WEAKMAP_MUTEX_COUNT]);
    if (err != 0 && err != EBUSY) {
      fprintf(stderr, "pthread_mutex_trylock: %s\n", strerror(err));
      exit(69);
    }
    return err;
# else
    (void)wm; (void)h;
    return 0;
# endif
}

void weakmap_unlock(struct weakmap *wm, unsigned h)
{
# ifdef GC_PTHREADS
    int err = pthread_mutex_unlock(&wm->mutex[h % WEAKMAP_MUTEX_COUNT]);
    my_assert(0 == err);
# else
    (void)wm; (void)h;
# endif
}

void *GC_CALLBACK set_mark_bit(void *obj)
{
  GC_set_mark_bit(obj);
  return NULL;
}

void *weakmap_add(struct weakmap *wm, void *obj)
{
  struct weakmap_link *link, *new_link, **first;
  GC_word *new_base;
  void *new_obj;
  unsigned h;
  size_t key_size = wm->key_size;

  /* Lock and look for an existing entry.       */
  h = memhash(obj, key_size);
  first = &wm->links[h % wm->capacity];
  weakmap_lock(wm, h);

  for (link = *first; link != NULL; link = link->next) {
    void *old_obj = GC_get_find_leak() ? (void *)link->obj
                        : GC_REVEAL_POINTER(link->obj);

    if (memcmp(old_obj, obj, key_size) == 0) {
      GC_call_with_alloc_lock(set_mark_bit, (GC_word *)old_obj - 1);
      /* Pointers in the key part may have been freed and reused,   */
      /* changing the keys without memcmp noticing.  This is okay   */
      /* as long as we update the mapped value.                     */
      if (memcmp((char *)old_obj + key_size, (char *)obj + key_size,
                 wm->obj_size - key_size) != 0) {
        memcpy((char *)old_obj + key_size, (char *)obj + key_size,
               wm->obj_size - key_size);
        GC_end_stubborn_change((char *)old_obj + key_size);
      }
      weakmap_unlock(wm, h);
      AO_fetch_and_add1(&stat_found);
#     ifdef DEBUG_DISCLAIM_WEAKMAP
        printf("Found %p, hash=%p\n", old_obj, (void *)(GC_word)h);
#     endif
      return old_obj;
    }
  }

  /* Create new object. */
  new_base = (GC_word *)GC_generic_malloc(sizeof(GC_word) + wm->obj_size,
                                          wm->weakobj_kind);
  CHECK_OOM(new_base);
  *new_base = (GC_word)wm | FINALIZER_CLOSURE_FLAG;
  new_obj = (void *)(new_base + 1);
  memcpy(new_obj, obj, wm->obj_size);
  GC_end_stubborn_change(new_base);

  /* Add the object to the map. */
  new_link = (struct weakmap_link *)GC_malloc(sizeof(struct weakmap_link));
  CHECK_OOM(new_link);
  new_link->obj = GC_get_find_leak() ? (GC_word)new_obj
                        : GC_HIDE_POINTER(new_obj);
  new_link->next = *first;
  GC_END_STUBBORN_CHANGE(new_link);
  GC_ptr_store_and_dirty(first, new_link);
  weakmap_unlock(wm, h);
  AO_fetch_and_add1(&stat_added);
# ifdef DEBUG_DISCLAIM_WEAKMAP
    printf("Added %p, hash=%p\n", new_obj, (void *)(GC_word)h);
# endif
  return new_obj;
}

int GC_CALLBACK weakmap_disclaim(void *obj_base)
{
  struct weakmap *wm;
  struct weakmap_link **link;
  GC_word hdr;
  void *obj;
  unsigned h;

  /* Decode header word.    */
  hdr = *(GC_word *)obj_base;
  if ((hdr & FINALIZER_CLOSURE_FLAG) == 0)
    return 0;   /* on GC free list, ignore it.  */

  my_assert((hdr & INVALIDATE_FLAG) == 0);
  wm = (struct weakmap *)(hdr & ~(GC_word)FINALIZER_CLOSURE_FLAG);
  if (NULL == wm->links)
    return 0;   /* weakmap has been already destroyed */
  obj = (GC_word *)obj_base + 1;

  /* Lock and check for mark.   */
  h = memhash(obj, wm->key_size);
  if (weakmap_trylock(wm, h) != 0) {
    AO_fetch_and_add1(&stat_skip_locked);
#   ifdef DEBUG_DISCLAIM_WEAKMAP
      printf("Skipping locked %p, hash=%p\n", obj, (void *)(GC_word)h);
#   endif
    return 1;
  }
  if (GC_is_marked(obj_base)) {
    weakmap_unlock(wm, h);
    AO_fetch_and_add1(&stat_skip_marked);
#   ifdef DEBUG_DISCLAIM_WEAKMAP
      printf("Skipping marked %p, hash=%p\n", obj, (void *)(GC_word)h);
#   endif
    return 1;
  }

  /* Remove obj from wm.        */
  AO_fetch_and_add1(&stat_removed);
# ifdef DEBUG_DISCLAIM_WEAKMAP
    printf("Removing %p, hash=%p\n", obj, (void *)(GC_word)h);
# endif
  *(GC_word *)obj_base |= INVALIDATE_FLAG;
  for (link = &wm->links[h % wm->capacity];; link = &(*link)->next) {
    void *old_obj;

    if (NULL == *link) {
      fprintf(stderr, "Did not find %p\n", obj);
      exit(70);
    }
    old_obj = GC_get_find_leak() ? (void *)(*link)->obj
                : GC_REVEAL_POINTER((*link)->obj);
    if (old_obj == obj)
      break;
    my_assert(memcmp(old_obj, obj, wm->key_size) != 0);
  }
  GC_ptr_store_and_dirty(link, (*link)->next);
  weakmap_unlock(wm, h);
  return 0;
}

struct weakmap *weakmap_new(size_t capacity, size_t key_size, size_t obj_size,
                            unsigned weakobj_kind)
{
  struct weakmap *wm = (struct weakmap *)GC_malloc(sizeof(struct weakmap));

  CHECK_OOM(wm);
# ifdef GC_PTHREADS
    {
      int i;
      for (i = 0; i < WEAKMAP_MUTEX_COUNT; ++i) {
        int err = pthread_mutex_init(&wm->mutex[i], NULL);
        my_assert(err == 0);
      }
    }
# endif
  wm->key_size = key_size;
  wm->obj_size = obj_size;
  wm->capacity = capacity;
  wm->weakobj_kind = weakobj_kind;
  GC_ptr_store_and_dirty(&wm->links,
                         GC_malloc(sizeof(struct weakmap_link *) * capacity));
  CHECK_OOM(wm->links);
  return wm;
}

void weakmap_destroy(struct weakmap *wm)
{
# ifdef GC_PTHREADS
    int i;

    for (i = 0; i < WEAKMAP_MUTEX_COUNT; ++i) {
      (void)pthread_mutex_destroy(&wm->mutex[i]);
    }
# endif
  wm->links = NULL; /* weakmap is destroyed */
}

struct weakmap *pair_hcset;

#define PAIR_MAGIC_SIZE 16 /* should not exceed sizeof(pair_magic) */

struct pair_key {
  struct pair *car, *cdr;
};

struct pair {
  struct pair *car;
  struct pair *cdr;
  char magic[PAIR_MAGIC_SIZE];
  int checksum;
};

static const char * const pair_magic = "PAIR_MAGIC_BYTES";

struct pair *pair_new(struct pair *car, struct pair *cdr)
{
  struct pair tmpl;

  memset(&tmpl, 0, sizeof(tmpl));   /* To clear the paddings (to avoid  */
                                    /* a compiler warning).             */
  tmpl.car = car;
  tmpl.cdr = cdr;
  memcpy(tmpl.magic, pair_magic, PAIR_MAGIC_SIZE);
  tmpl.checksum = 782 + (car? car->checksum : 0) + (cdr? cdr->checksum : 0);
  return (struct pair *)weakmap_add(pair_hcset, &tmpl);
}

void pair_check_rec(struct pair *p, int line)
{
  while (p != NULL) {
    int checksum = 782;

    if (memcmp(p->magic, pair_magic, PAIR_MAGIC_SIZE) != 0) {
      fprintf(stderr, "Magic bytes wrong for %p at %d\n", (void *)p, line);
      exit(70);
    }
    if (p->car != NULL)
      checksum += p->car->checksum;
    if (p->cdr != NULL)
      checksum += p->cdr->checksum;
    if (p->checksum != checksum) {
      fprintf(stderr, "Checksum failure for %p = (%p, %p) at %d\n",
              (void *)p, (void *)p->car, (void *)p->cdr, line);
      exit(70);
    }
    p = (rand() & 1) != 0 ? p->cdr : p->car;
  }
}

void *test(void *data)
{
  int i;
  struct pair *p0, *p1;
  struct pair *pop[POP_SIZE];

  memset(pop, 0, sizeof(pop));
  for (i = 0; i < MUTATE_CNT; ++i) {
    int bits = rand();
    int t = (bits >> 3) % POP_SIZE;

    switch (bits % (i > GROW_LIMIT ? 5 : 3)) {
    case 0:
    case 3:
      if (pop[t] != NULL)
        pop[t] = pop[t]->car;
      break;
    case 1:
    case 4:
      if (pop[t] != NULL)
        pop[t] = pop[t]->cdr;
      break;
    case 2:
      p0 = pop[rand() % POP_SIZE];
      p1 = pop[rand() % POP_SIZE];
      pop[t] = pair_new(p0, p1);
      my_assert(pair_new(p0, p1) == pop[t]);
      my_assert(pop[t]->car == p0);
      my_assert(pop[t]->cdr == p1);
      break;
    }
    pair_check_rec(pop[rand() % POP_SIZE], __LINE__);
  }
  return data;
}

int main(void)
{
  unsigned weakobj_kind;
  void **weakobj_free_list;
# ifdef GC_PTHREADS
    int i;
    pthread_t th[NTHREADS];
# endif

  GC_set_all_interior_pointers(0); /* for a stricter test */
# ifdef TEST_MANUAL_VDB
    GC_set_manual_vdb_allowed(1);
# endif
  GC_INIT();
  GC_init_finalized_malloc(); /* to register the displacements */
# ifndef NO_INCREMENTAL
    GC_enable_incremental();
# endif
  if (GC_get_find_leak())
    printf("This test program is not designed for leak detection mode\n");

  weakobj_free_list = GC_new_free_list();
  CHECK_OOM(weakobj_free_list);
  weakobj_kind = GC_new_kind(weakobj_free_list, /* 0 | */ GC_DS_LENGTH,
                             1 /* adjust */, 1 /* clear */);
  GC_register_disclaim_proc(weakobj_kind, weakmap_disclaim,
                            1 /* mark_unconditionally */);
  pair_hcset = weakmap_new(WEAKMAP_CAPACITY, sizeof(struct pair_key),
                           sizeof(struct pair), weakobj_kind);

# ifdef GC_PTHREADS
    for (i = 0; i < NTHREADS; ++i) {
      int err = pthread_create(&th[i], NULL, test, NULL);
      if (err != 0) {
        fprintf(stderr, "Failed to create thread #%d: %s\n",
                i, strerror(err));
        exit(1);
      }
    }
    for (i = 0; i < NTHREADS; ++i) {
      int err = pthread_join(th[i], NULL);
      if (err != 0) {
        fprintf(stderr, "Failed to join thread #%d: %s\n", i, strerror(err));
        exit(69);
      }
    }
# else
    (void)test(NULL);
# endif
  weakmap_destroy(pair_hcset);
  printf("%u added, %u found; %u removed, %u locked, %u marked; %u remains\n",
         (unsigned)stat_added, (unsigned)stat_found, (unsigned)stat_removed,
         (unsigned)stat_skip_locked, (unsigned)stat_skip_marked,
         (unsigned)stat_added - (unsigned)stat_removed);
  return 0;
}
