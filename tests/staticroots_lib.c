
/* This test file is intended to be compiled into a DLL. */

#include <stdio.h>
#include <stdlib.h>

#ifndef GC_DEBUG
#  define GC_DEBUG
#endif

#include "gc.h"

#ifndef GC_TEST_EXPORT_API
#  if defined(GC_VISIBILITY_HIDDEN_SET) && !defined(__CEGCC__) \
      && !defined(__CYGWIN__) && !defined(__MINGW32__)
#    define GC_TEST_EXPORT_API \
      extern __attribute__((__visibility__("default")))
#  else
#    define GC_TEST_EXPORT_API extern
#  endif
#endif

#define CHECK_OUT_OF_MEMORY(p)            \
  do {                                    \
    if (NULL == (p)) {                    \
      fprintf(stderr, "Out of memory\n"); \
      exit(69);                           \
    }                                     \
  } while (0)

struct treenode {
  struct treenode *x;
  struct treenode *y;
};

static struct treenode *root[10] = { 0 };
static struct treenode *root_nz[10] = { (struct treenode *)(GC_uintptr_t)2 };

/* Declare it to avoid "no previous prototype" clang warning. */
GC_TEST_EXPORT_API struct treenode **libsrl_get_p_element(int i, int j);

#ifdef STATICROOTSLIB2
#  define libsrl_get_p_element libsrl_get_p_element2
#else

GC_TEST_EXPORT_API struct treenode *libsrl_mktree(int i);
GC_TEST_EXPORT_API void *libsrl_init(void);

GC_TEST_EXPORT_API struct treenode *
libsrl_mktree(int i)
{
  struct treenode *r = GC_NEW(struct treenode);
  struct treenode *x, *y;

  CHECK_OUT_OF_MEMORY(r);
  if (0 == i)
    return 0;
  if (1 == i) {
    r = (struct treenode *)GC_MALLOC_ATOMIC(sizeof(struct treenode));
    CHECK_OUT_OF_MEMORY(r);
  }
  x = libsrl_mktree(i - 1);
  y = libsrl_mktree(i - 1);
  r->x = x;
  r->y = y;
  if (i != 1) {
    GC_END_STUBBORN_CHANGE(r);
    GC_reachable_here(x);
    GC_reachable_here(y);
  }
  return r;
}

GC_TEST_EXPORT_API void *
libsrl_init(void)
{
#  ifdef TEST_MANUAL_VDB
  GC_set_manual_vdb_allowed(1);
#  endif
#  ifndef STATICROOTSLIB_INIT_IN_MAIN
  GC_INIT();
#  endif
#  ifndef NO_INCREMENTAL
  GC_enable_incremental();
#  endif
  return GC_MALLOC(sizeof(struct treenode));
}

#endif /* !STATICROOTSLIB2 */

GC_TEST_EXPORT_API struct treenode **
libsrl_get_p_element(int i, int j)
{
#if defined(CPPCHECK)
  struct treenode node = { NULL, NULL };

  GC_noop1_ptr(node.x);
  GC_noop1_ptr(node.y);
#endif
  return &((j & 1) != 0 ? root_nz : root)[i];
}
