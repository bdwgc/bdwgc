/*
 * Copyright (c) 2018 Petter A. Urkedal
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
 * This one tests a case where disclaim notifiers sometimes return a nonzero
 * value in order to protect objects from collection.
 */

#ifdef HAVE_CONFIG_H
/* For `GC_THREADS` and `GC_PTHREADS` macros. */
#  include "config.h"
#endif

#undef GC_NO_THREAD_REDIRECTS
/* This includes `gc.h` file transitively. */
#include "gc/gc_disclaim.h"

#define NOT_GCBUILD
#include "private/gc_priv.h"

#ifdef TEST_NO_THREADS
#  undef GC_PTHREADS
#endif

#include <string.h>

#ifdef GC_PTHREADS
#  include <errno.h> /*< for `EAGAIN`, `EBUSY` */
#  include <pthread.h>
#endif

#undef rand
/* Note: concurrent update of `seed` does not hurt the test. */
static GC_RAND_STATE_T seed;
#define rand() GC_RAND_NEXT(&seed)

/* Note: this should not precede include `gc_priv.h` file. */
#include "gc/gc_mark.h"

#ifdef GC_PTHREADS
#  ifndef NTHREADS
/* This excludes the main thread, which also runs a test. */
#    define NTHREADS 5
#  endif
#  include "private/gc_atomic_ops.h" /*< for `AO_t`, `AO_fetch_and_add1` */
#else
#  undef NTHREADS
#  define NTHREADS 0
#  ifndef AO_HAVE_compiler_barrier
#    define AO_t size_t
#  endif
#endif

#define POP_SIZE 200
#define MUTATE_CNT_BASE 700000

#define MUTATE_CNT (MUTATE_CNT_BASE / (NTHREADS + 1))
#define GROW_LIMIT (MUTATE_CNT / 10)

#define WEAKMAP_CAPACITY 256
#define WEAKMAP_MUTEX_COUNT 32

/* `FINALIZER_CLOSURE_FLAG` definition matches the one in `fnlz_mlc.c` file. */
#if defined(KEEP_BACK_PTRS) || defined(MAKE_BACK_GRAPH)
#  define FINALIZER_CLOSURE_FLAG 0x2
#  define INVALIDATE_FLAG 0x1
#else
#  define FINALIZER_CLOSURE_FLAG 0x1
#  define INVALIDATE_FLAG 0x2
#endif

#define IS_FLAG_SET(p, mask) \
  (((unsigned)(GC_uintptr_t)(p) & (unsigned)(mask)) != 0)

#define my_assert(e)                                                   \
  if (!(e)) {                                                          \
    fflush(stdout);                                                    \
    fprintf(stderr, "Assertion failure, line %d: %s\n", __LINE__, #e); \
    exit(70);                                                          \
  }

#define CHECK_OUT_OF_MEMORY(p)            \
  do {                                    \
    if (NULL == (p)) {                    \
      fprintf(stderr, "Out of memory\n"); \
      exit(69);                           \
    }                                     \
  } while (0)

#ifndef AO_HAVE_fetch_and_add1
/* This is used only to update counters. */
#  define AO_fetch_and_add1(p) ((*(p))++)
#endif

static unsigned
memhash(void *src, size_t len)
{
  unsigned acc = 0;
  size_t i;

  my_assert(len % sizeof(GC_word) == 0);
  for (i = 0; i < len / sizeof(GC_word); ++i) {
    acc = (unsigned)((2003 * (GC_word)acc + ((GC_word *)src)[i]) / 3);
  }
  return acc;
}

static AO_t stat_added;
static AO_t stat_found;
static AO_t stat_removed;
static AO_t stat_skip_locked;
static AO_t stat_skip_marked;

union weakmap_element_u {
  void *p;
  GC_hidden_pointer hidden;
};

struct weakmap_link {
  union weakmap_element_u obj;
  struct weakmap_link *next;
};

struct weakmap {
#ifdef GC_PTHREADS
  pthread_mutex_t mutex[WEAKMAP_MUTEX_COUNT];
#endif
  size_t key_size;
  size_t obj_size;
  size_t capacity;
  unsigned weakobj_kind;
  /* Note: `NULL` mean this `weakmap` instance is destroyed. */
  struct weakmap_link **links;
};

static void
weakmap_lock(struct weakmap *wm, unsigned h)
{
#ifdef GC_PTHREADS
  int err = pthread_mutex_lock(&wm->mutex[h % WEAKMAP_MUTEX_COUNT]);

  my_assert(0 == err);
#else
  (void)wm;
  (void)h;
#endif
}

#ifdef GC_PTHREADS
static int
weakmap_trylock(struct weakmap *wm, unsigned h)
{
  int err = pthread_mutex_trylock(&wm->mutex[h % WEAKMAP_MUTEX_COUNT]);

  if (err != 0 && err != EBUSY) {
    fprintf(stderr, "pthread_mutex_trylock, errno= %d\n", err);
    exit(69);
  }
  return err;
}
#endif /* GC_PTHREADS */

static void
weakmap_unlock(struct weakmap *wm, unsigned h)
{
#ifdef GC_PTHREADS
  int err = pthread_mutex_unlock(&wm->mutex[h % WEAKMAP_MUTEX_COUNT]);

  my_assert(0 == err);
#else
  (void)wm;
  (void)h;
#endif
}

static void *GC_CALLBACK
set_mark_bit(void *obj)
{
  GC_set_mark_bit(obj);
#ifdef CPPCHECK
  GC_noop1_ptr(obj);
#endif
  return NULL;
}

static void *
weakmap_add(struct weakmap *wm, void *obj, size_t obj_size)
{
  struct weakmap_link *link, *new_link, **first;
  void **new_base;
  void *new_obj;
  unsigned h;
  size_t key_size = wm->key_size;

  my_assert(!IS_FLAG_SET(wm, FINALIZER_CLOSURE_FLAG));
  /* Lock and look for an existing entry. */
  my_assert(key_size <= obj_size);
  h = memhash(obj, key_size);
  first = &wm->links[h % wm->capacity];
  weakmap_lock(wm, h);

  for (link = *first; link != NULL; link = link->next) {
    void *old_obj = GC_get_find_leak() ? link->obj.p
                                       : GC_REVEAL_POINTER(link->obj.hidden);

    if (memcmp(old_obj, obj, key_size) == 0) {
      GC_call_with_alloc_lock(set_mark_bit, (void **)old_obj - 1);
      /*
       * Pointers in the key part may have been freed and reused, changing
       * the keys without `memcmp` noticing.  This is OK as long as we update
       * the mapped value.
       */
      if (memcmp((char *)old_obj + key_size, (char *)obj + key_size,
                 wm->obj_size - key_size)
          != 0) {
        memcpy((char *)old_obj + key_size, (char *)obj + key_size,
               wm->obj_size - key_size);
        GC_end_stubborn_change((char *)old_obj + key_size);
      }
      weakmap_unlock(wm, h);
      AO_fetch_and_add1(&stat_found);
#ifdef DEBUG_DISCLAIM_WEAKMAP
      printf("Found %p, hash= 0x%x\n", old_obj, h);
#endif
      return old_obj;
    }
  }

  /* Create new object. */
  new_base = (void **)GC_generic_malloc(sizeof(void *) + wm->obj_size,
                                        (int)wm->weakobj_kind);
  CHECK_OUT_OF_MEMORY(new_base);
  *new_base = CPTR_SET_FLAGS(wm, FINALIZER_CLOSURE_FLAG);
  new_obj = new_base + 1;
  memcpy(new_obj, obj, wm->obj_size);
  GC_end_stubborn_change(new_base);

  /* Add the object to the map. */
  new_link = (struct weakmap_link *)GC_malloc(sizeof(struct weakmap_link));
  CHECK_OUT_OF_MEMORY(new_link);
  if (GC_get_find_leak()) {
    new_link->obj.p = new_obj;
  } else {
    new_link->obj.hidden = GC_HIDE_POINTER(new_obj);
  }
  new_link->next = *first;
  GC_END_STUBBORN_CHANGE(new_link);
  GC_ptr_store_and_dirty(first, new_link);
  weakmap_unlock(wm, h);
  AO_fetch_and_add1(&stat_added);
#ifdef DEBUG_DISCLAIM_WEAKMAP
  printf("Added %p, hash= 0x%x\n", new_obj, h);
#endif
  return new_obj;
}

static int GC_CALLBACK
weakmap_disclaim(void *obj_base)
{
  struct weakmap *wm;
  struct weakmap_link **link;
  void *header;
  void *obj;
  unsigned h;

  /* Decode header word. */
  header = *(void **)obj_base;
  if (!IS_FLAG_SET(header, FINALIZER_CLOSURE_FLAG)) {
    /* On the collector free list, ignore it. */
    return 0;
  }

  my_assert(!IS_FLAG_SET(header, INVALIDATE_FLAG));
  wm = (struct weakmap *)CPTR_CLEAR_FLAGS(header, FINALIZER_CLOSURE_FLAG);
  if (NULL == wm->links) {
    /* The weakmap has been already destroyed. */
    return 0;
  }
  obj = (void **)obj_base + 1;

  /* Lock and check for mark. */
  h = memhash(obj, wm->key_size);
#ifdef GC_PTHREADS
  if (weakmap_trylock(wm, h) != 0) {
    AO_fetch_and_add1(&stat_skip_locked);
#  ifdef DEBUG_DISCLAIM_WEAKMAP
    printf("Skipping locked %p, hash= 0x%x\n", obj, h);
#  endif
    return 1;
  }
#endif
  if (GC_is_marked(obj_base)) {
    weakmap_unlock(wm, h);
    AO_fetch_and_add1(&stat_skip_marked);
#ifdef DEBUG_DISCLAIM_WEAKMAP
    printf("Skipping marked %p, hash= 0x%x\n", obj, h);
#endif
    return 1;
  }

  /* Remove `obj` from `wm`. */
#ifdef DEBUG_DISCLAIM_WEAKMAP
  printf("Removing %p, hash= 0x%x\n", obj, h);
#endif
  my_assert(header == *(void **)obj_base);
  *(void **)obj_base = CPTR_SET_FLAGS(header, INVALIDATE_FLAG);
  AO_fetch_and_add1(&stat_removed);
  for (link = &wm->links[h % wm->capacity];; link = &(*link)->next) {
    const void *old_obj;

    if (NULL == *link) {
      fprintf(stderr, "Did not find %p\n", obj);
      exit(70);
    }
    old_obj = GC_get_find_leak() ? (*link)->obj.p
                                 : GC_REVEAL_POINTER((*link)->obj.hidden);
    if (old_obj == obj)
      break;
    my_assert(memcmp(old_obj, obj, wm->key_size) != 0);
  }
  GC_ptr_store_and_dirty(link, (*link)->next);
  weakmap_unlock(wm, h);
  return 0;
}

static struct weakmap *
weakmap_new(size_t capacity, size_t key_size, size_t obj_size,
            unsigned weakobj_kind)
{
  struct weakmap *wm = (struct weakmap *)GC_malloc(sizeof(struct weakmap));

  CHECK_OUT_OF_MEMORY(wm);
#ifdef GC_PTHREADS
  {
    int i;
    for (i = 0; i < WEAKMAP_MUTEX_COUNT; ++i) {
      int err = pthread_mutex_init(&wm->mutex[i], NULL);

      my_assert(0 == err);
    }
  }
#endif
  wm->key_size = key_size;
  wm->obj_size = obj_size;
  wm->capacity = capacity;
  wm->weakobj_kind = weakobj_kind;
  GC_ptr_store_and_dirty(&wm->links,
                         GC_malloc(sizeof(struct weakmap_link *) * capacity));
  CHECK_OUT_OF_MEMORY(wm->links);
  return wm;
}

static void
weakmap_destroy(struct weakmap *wm)
{
#ifdef GC_PTHREADS
  int i;

  for (i = 0; i < WEAKMAP_MUTEX_COUNT; ++i) {
    (void)pthread_mutex_destroy(&wm->mutex[i]);
  }
#endif
  /* The weakmap is destroyed. */
  wm->links = NULL;
}

static struct weakmap *pair_set;

/* Note: this should not exceed `sizeof(pair_magic)`. */
#define PAIR_MAGIC_SIZE 16

struct pair_key {
  struct pair *car, *cdr;
};

struct pair {
  struct pair *car;
  struct pair *cdr;
  char magic[PAIR_MAGIC_SIZE];
  int checksum;
};

static const char *const pair_magic = "PAIR_MAGIC_BYTES";

#define CSUM_SEED 782

static struct pair *
pair_new(struct pair *car, struct pair *cdr)
{
  struct pair t;

  /* This is to clear the paddings (to avoid a compiler warning). */
  memset(&t, 0, sizeof(t));

  t.car = car;
  t.cdr = cdr;
  memcpy(t.magic, pair_magic, PAIR_MAGIC_SIZE);
  t.checksum = CSUM_SEED + (car != NULL ? car->checksum : 0)
               + (cdr != NULL ? cdr->checksum : 0);
  return (struct pair *)weakmap_add(pair_set, &t, sizeof(t));
}

static void
pair_check_rec(struct pair *p, int line)
{
  while (p != NULL) {
    int checksum = CSUM_SEED;

    if (memcmp(p->magic, pair_magic, PAIR_MAGIC_SIZE) != 0) {
      fprintf(stderr, "Magic bytes wrong for %p at %d\n", (void *)p, line);
      exit(70);
    }
    if (p->car != NULL)
      checksum += p->car->checksum;
    if (p->cdr != NULL)
      checksum += p->cdr->checksum;
    if (p->checksum != checksum) {
      fprintf(stderr, "Checksum failure for %p: (car= %p, cdr= %p) at %d\n",
              (void *)p, (void *)p->car, (void *)p->cdr, line);
      exit(70);
    }
    p = (rand() & 1) != 0 ? p->cdr : p->car;
  }
}

static void *
test(void *data)
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

int
main(void)
{
  unsigned weakobj_kind;
#if NTHREADS > 0
  int i, n;
  pthread_t th[NTHREADS];
#endif

  /* Make the test stricter. */
  GC_set_all_interior_pointers(0);

#ifdef TEST_MANUAL_VDB
  GC_set_manual_vdb_allowed(1);
#endif
  GC_INIT();
  /* Register the displacements. */
  GC_init_finalized_malloc();

#ifndef NO_INCREMENTAL
  GC_enable_incremental();
#endif
  if (GC_get_find_leak())
    printf("This test program is not designed for leak detection mode\n");
  weakobj_kind = GC_new_kind(GC_new_free_list(), /* 0 | */ GC_DS_LENGTH,
                             1 /* `adjust` */, 1 /* `clear` */);
  GC_register_disclaim_proc((int)weakobj_kind, weakmap_disclaim,
                            1 /* `mark_unconditionally` */);
  pair_set = weakmap_new(WEAKMAP_CAPACITY, sizeof(struct pair_key),
                         sizeof(struct pair), weakobj_kind);

#if NTHREADS > 0
  for (i = 0; i < NTHREADS; ++i) {
    int err = pthread_create(&th[i], NULL, test, NULL);

    if (err != 0) {
      fprintf(stderr, "Thread #%d creation failed, errno= %d\n", i, err);
      if (i > 1 && EAGAIN == err)
        break;
      exit(1);
    }
  }
  n = i;
#endif
  (void)test(NULL);
#if NTHREADS > 0
  for (i = 0; i < n; ++i) {
    int err = pthread_join(th[i], NULL);

    if (err != 0) {
      fprintf(stderr, "Thread #%d join failed, errno= %d\n", i, err);
      exit(69);
    }
  }
#endif
  weakmap_destroy(pair_set);
  printf("%u added, %u found; %u removed, %u locked, %u marked; %u remains\n",
         (unsigned)stat_added, (unsigned)stat_found, (unsigned)stat_removed,
         (unsigned)stat_skip_locked, (unsigned)stat_skip_marked,
         (unsigned)stat_added - (unsigned)stat_removed);
  return 0;
}
