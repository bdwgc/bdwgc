/*
 * Copyright (c) 1994 by Xerox Corporation.  All rights reserved.
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

// This program tries to test the specific C++ functionality provided by
// `gc_cpp.h` file that is not tested by the more general test routines
// of the collector.

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#undef GC_BUILD

#define GC_DONT_INCL_WINDOWS_H
#include "gc_cpp.h"

#include <stdlib.h>
#include <string.h>

#define GC_NAMESPACE_ALLOCATOR
#include "gc/gc_allocator.h"
using boehmgc::gc_allocator;
using boehmgc::gc_allocator_ignore_off_page;
using boehmgc::traceable_allocator;

#include "private/gcconfig.h"

#ifndef GC_API_PRIV
#  define GC_API_PRIV GC_API
#endif
extern "C" {
// Use the collector private output to reach the same log file.
// Do not include `gc_priv.h` file, since that may include Windows
// system header files that do not take kindly to this context.
GC_API_PRIV void GC_printf(const char *format, ...);
}

#ifdef MSWIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN 1
#  endif
#  define NOSERVICE
#  include <windows.h>
#endif

#ifdef GC_NAME_CONFLICT
#  define USE_GC GC_NS_QUALIFY(UseGC)
struct foo *GC;
#else
#  define USE_GC GC_NS_QUALIFY(GC)
#endif

#if __cplusplus >= 201103L
#  define GC_OVERRIDE override
#else
#  define GC_OVERRIDE /*< empty */
#endif

#define my_assert(e)                                                  \
  if (!(e)) {                                                         \
    GC_printf("Assertion failure in " __FILE__ ", line %d: " #e "\n", \
              __LINE__);                                              \
    exit(1);                                                          \
  }

#if defined(__powerpc64__) && !defined(__clang__) && GC_GNUC_PREREQ(10, 0)
// Suppress "layout of aggregates ... has changed" GCC note.
#  define A_I_TYPE short
#else
#  define A_I_TYPE int
#endif

#define LARGE_CPP_ITER_CNT 1000000

// An uncollectible class.
class A
{
public:
  GC_ATTR_EXPLICIT
  A(int iArg) : i(static_cast<A_I_TYPE>(iArg)) {}
  void
  Test(int iArg)
  {
    my_assert(i == iArg);
  }
  virtual ~A() {}
  A_I_TYPE i;
};

// A collectible class.
class B : public GC_NS_QUALIFY(gc), public A
{
public:
  GC_ATTR_EXPLICIT
  B(int j) : A(j) {}
  virtual ~B() GC_OVERRIDE { my_assert(deleting); }
  static void
  Deleting(int on)
  {
    deleting = on;
  }
  static int deleting;
};

int B::deleting = 0;

#define C_INIT_LEFT_RIGHT(arg_l, arg_r) \
  {                                     \
    C *l = new C(arg_l);                \
    C *r = new C(arg_r);                \
    left = l;                           \
    right = r;                          \
    if (GC_is_heap_ptr(this)) {         \
      GC_END_STUBBORN_CHANGE(this);     \
      GC_reachable_here(l);             \
      GC_reachable_here(r);             \
    }                                   \
  }

// A collectible class with cleanup and virtual multiple inheritance.
class C : public GC_NS_QUALIFY(gc_cleanup), public A
{
public:
  // The class uses dynamic memory/resource allocation, so provide both
  // a copy constructor and an assignment operator to workaround a cppcheck
  // warning.
  C(const C &c) : A(c.i), level(c.level), left(0), right(0)
  {
    if (level > 0)
      C_INIT_LEFT_RIGHT(*c.left, *c.right);
  }

  C &
  operator=(const C &c)
  {
    if (this != &c) {
      delete left;
      delete right;
      i = c.i;
      level = c.level;
      left = 0;
      right = 0;
      if (level > 0)
        C_INIT_LEFT_RIGHT(*c.left, *c.right);
    }
    return *this;
  }

  GC_ATTR_EXPLICIT
  C(int levelArg) : A(levelArg), level(levelArg)
  {
    nAllocated++;
    if (level > 0) {
      C_INIT_LEFT_RIGHT(level - 1, level - 1);
    } else {
      left = right = 0;
    }
  }
  ~C() GC_OVERRIDE
  {
    this->A::Test(level);
    nFreed++;
    my_assert(level == 0
                  ? left == 0 && right == 0
                  : level == left->level + 1 && level == right->level + 1);
    left = right = 0;
    level = -32456;
  }
  static void
  Test()
  {
    if (GC_is_incremental_mode() && nFreed < (nAllocated / 5) * 4) {
      // An explicit garbage collection might be needed to reach the expected
      // number of the finalized objects.
      GC_gcollect();
    }
    my_assert(nFreed <= nAllocated);
#ifndef GC_NO_FINALIZATION
    my_assert(nFreed >= (nAllocated / 5) * 4 || GC_get_find_leak());
#endif
  }

  static int nFreed;
  static int nAllocated;
  int level;
  C *left;
  C *right;
};

int C::nFreed = 0;
int C::nAllocated = 0;

// A collectible class with a static member function to be used as
// an explicit cleanup function supplied to operator `::new`.
class D : public GC_NS_QUALIFY(gc)
{
public:
  GC_ATTR_EXPLICIT
  D(int iArg) : i(iArg) { nAllocated++; }
  static void
  CleanUp(void *obj, void *data)
  {
    const D *self = static_cast<D *>(obj);
    nFreed++;
    my_assert(static_cast<GC_uintptr_t>(self->i)
              == reinterpret_cast<GC_uintptr_t>(data));
  }
  static void
  Test()
  {
#ifndef GC_NO_FINALIZATION
    my_assert(nFreed >= (nAllocated / 5) * 4 || GC_get_find_leak());
#endif
  }

  int i;
  static int nFreed;
  static int nAllocated;
};

int D::nFreed = 0;
int D::nAllocated = 0;

// A collectible class with cleanup for use by `F`.
class E : public GC_NS_QUALIFY(gc_cleanup)
{
public:
  E() { nAllocated++; }
  ~E() GC_OVERRIDE { nFreed++; }

  static int nFreed;
  static int nAllocated;
};

int E::nFreed = 0;
int E::nAllocated = 0;

// A collectible class with cleanup, a base with cleanup, and
// a member with cleanup.
class F : public E
{
public:
  F() { nAllocatedF++; }

  ~F() GC_OVERRIDE { nFreedF++; }

  static void
  Test()
  {
#ifndef GC_NO_FINALIZATION
    my_assert(nFreedF >= (nAllocatedF / 5) * 4 || GC_get_find_leak());
#endif
    my_assert(2 * nFreedF == nFreed);
  }

  E e;
  static int nFreedF;
  static int nAllocatedF;
};

int F::nFreedF = 0;
int F::nAllocatedF = 0;

GC_uintptr_t
Disguise(void *p)
{
  return GC_HIDE_NZ_POINTER(p);
}

void *
Undisguise(GC_uintptr_t v)
{
  return GC_REVEAL_NZ_POINTER(v);
}

// Note: `delete p` should invoke `GC_FREE()`.
#define GC_CHECKED_DELETE(p)                                  \
  {                                                           \
    size_t freed_before = GC_get_expl_freed_bytes_since_gc(); \
    delete p;                                                 \
    size_t freed_after = GC_get_expl_freed_bytes_since_gc();  \
    my_assert(freed_before != freed_after);                   \
  }

#define N_TESTS 7

#if ((defined(MSWIN32) && !defined(__MINGW32__)) || defined(MSWINCE)) \
    && !defined(NO_WINMAIN_ENTRY)
int APIENTRY
WinMain(HINSTANCE /* `instance` */, HINSTANCE /* `prev` */, LPSTR cmd,
        int /* `cmdShow` */)
{
  int argc = 0;
  char *argv[3];

#  if defined(CPPCHECK)
  GC_noop1(static_cast<GC_word>(reinterpret_cast<GC_uintptr_t>(&WinMain)));
#  endif
  if (cmd != 0)
    for (argc = 1; argc < static_cast<int>(sizeof(argv) / sizeof(argv[0]));
         argc++) {
      // Parse the command-line string.  Non-reentrant `strtok()` is not used
      // to avoid complains of static analysis tools.  (And, `strtok_r()` is
      // not available on some platforms.)  The code is equivalent to:
      // `if (!(argv[argc] = strtok(argc == 1 ? cmd : 0, " \t"))) break;`.
      if (NULL == cmd) {
        argv[argc] = NULL;
        break;
      }
      for (; *cmd != '\0'; cmd++) {
        if (*cmd != ' ' && *cmd != '\t')
          break;
      }
      if ('\0' == *cmd) {
        argv[argc] = NULL;
        break;
      }
      argv[argc] = cmd;
      while (*(++cmd) != '\0') {
        if (*cmd == ' ' || *cmd == '\t')
          break;
      }
      if (*cmd != '\0') {
        *(cmd++) = '\0';
      } else {
        cmd = NULL;
      }
    }
#else
int
main(int argc, const char *argv[])
{
#endif

#ifdef NO_ALL_INTERIOR_POINTERS
  (void)argc;
  (void)argv;
  GC_printf("test skipped\n");
#else
  // This is needed due to C++ multiple inheritance used.
  GC_set_all_interior_pointers(1);

#  ifdef TEST_MANUAL_VDB
  GC_set_manual_vdb_allowed(1);
#  endif
#  if !defined(CPPCHECK)
  GC_INIT();
#  endif
#  ifndef NO_INCREMENTAL
  GC_enable_incremental();
#  endif
  if (GC_get_find_leak())
    GC_printf("This test program is not designed for leak detection mode\n");

  int i, iters, n;
  int *x = gc_allocator<int>().allocate(1);
  const int *xio;
  xio = gc_allocator_ignore_off_page<int>().allocate(1);
  GC_reachable_here(xio);
  int **xptr = traceable_allocator<int *>().allocate(1);
  *x = 29;
  GC_PTR_STORE_AND_DIRTY(xptr, x);
  x = 0;
  if (argc != 2 || (n = atoi(argv[1])) <= 0) {
    GC_printf("Usage: cpptest <number_of_iterations>\n"
              "Assuming %d iterations\n",
              N_TESTS);
    n = N_TESTS;
  }
#  ifdef LINT2
  if (n > 30 * 1000)
    n = 30 * 1000;
#  endif

  for (iters = 1; iters <= n; iters++) {
    GC_printf("Starting iteration %d\n", iters);

    // Allocate some uncollectible objects and disguise their pointers.
    // Later we will check to see if the objects are still there.
    // We are checking to make sure these objects are uncollectible really.
    GC_uintptr_t as[1000];
    GC_uintptr_t bs[1000];
    for (i = 0; i < 1000; i++) {
      as[i] = Disguise(new (GC_NS_QUALIFY(NoGC)) A(i));
      bs[i] = Disguise(new (GC_NS_QUALIFY(NoGC)) B(i));
    }

    // Allocate a fair number of finalizable objects.
    // Later we will check to make sure they have gone away.
    for (i = 0; i < 1000; i++) {
      C *c = new C(2);
      // Stack allocation should work too.
      C c1(2);
      F *f;
#  if !defined(CPPCHECK)
      D *d;
      d = ::new (USE_GC, D::CleanUp,
                 reinterpret_cast<void *>(static_cast<GC_uintptr_t>(i))) D(i);
      GC_reachable_here(d);
#  endif
      f = new F;
      F **fa = new F *[1];
      fa[0] = f;
      (void)fa;
      delete[] fa;
      if (0 == i % 10)
        GC_CHECKED_DELETE(c);
    }

    // Allocate a very large number of collectible objects and drop
    // the references to them immediately, forcing many collections.
    for (i = 0; i < LARGE_CPP_ITER_CNT; i++) {
      const A *a;
      a = new (USE_GC) A(i);
      GC_reachable_here(a);
      B *b;
      b = new B(i);
      (void)b;
      b = new (USE_GC) B(i);
      if (0 == i % 10) {
        B::Deleting(1);
        GC_CHECKED_DELETE(b);
        B::Deleting(0);
      }
#  if defined(FINALIZE_ON_DEMAND) && !defined(GC_NO_FINALIZATION)
      GC_invoke_finalizers();
#  endif
    }

    // Make sure the uncollectible objects are still there.
    for (i = 0; i < 1000; i++) {
      A *a = static_cast<A *>(Undisguise(as[i]));
      B *b = static_cast<B *>(Undisguise(bs[i]));
      a->Test(i);
#  if defined(ADDRESS_SANITIZER) || defined(MEMORY_SANITIZER)
      // Workaround for ASan/MSan: the linker uses operator `delete`
      // implementation from `libclang_rt` instead of `gccpp` library (thus
      // causing incompatible `alloc`/`free`).
      GC_FREE(a);
#  else
      GC_CHECKED_DELETE(a);
#  endif
      b->Test(i);
      B::Deleting(1);
      GC_CHECKED_DELETE(b);
      B::Deleting(0);
#  if defined(FINALIZE_ON_DEMAND) && !defined(GC_NO_FINALIZATION)
      GC_invoke_finalizers();
#  endif
    }

    // Make sure most of the finalizable objects have gone away.
    C::Test();
    D::Test();
    F::Test();
  }

  x = *xptr;
  my_assert(29 == x[0]);
  GC_printf("The test appears to have succeeded.\n");
#endif
  return 0;
}
