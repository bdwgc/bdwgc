/* A dedicated test of `GC_debug_` API functions. */

#include <stdio.h>
#include <stdlib.h>

#ifndef GC_DEBUG
#  define GC_DEBUG
#endif

#include "gc.h"
#include "gc/gc_mark.h"

#ifdef GC_GCJ_SUPPORT
#  include "gc/gc_gcj.h"
#endif

#ifndef NTHREADS
/* A number of times to rerun all the test cases. */
#  define NTHREADS 5
#endif

#define TEST_ASSERT(e)                                                    \
  if (!(e)) {                                                             \
    fprintf(stderr, "Assertion failure: %s:%d, %s\n", __FILE__, __LINE__, \
            #e);                                                          \
    exit(1);                                                              \
  }

#define CHECK_OUT_OF_MEMORY(p)            \
  do {                                    \
    if (NULL == (p)) {                    \
      fprintf(stderr, "Out of memory\n"); \
      exit(69);                           \
    }                                     \
  } while (0)

#ifndef GC_NO_FINALIZATION
/* Test data for the finalizer. */
static int finalizer_called;
static void *finalizer_obj;

/* Simple finalizer function. */
static void GC_CALLBACK
test_finalizer(void *obj, void *client_data)
{
  finalizer_called = 1;
  finalizer_obj = obj;
  /* Store `client_data` for testing. */
  if (NULL != client_data)
    finalizer_obj = client_data;
}
#endif

static void
test_debug_free(void)
{
  void *p;

  /* Test freeing `NULL` - should not crash. */
  GC_FREE(NULL);

  /* Test allocating and freeing a small object. */
  p = GC_MALLOC(100);
  CHECK_OUT_OF_MEMORY(p);
  GC_FREE(p);
}

static void
test_debug_freezero(void)
{
  char *cp;
  int i;

  /* Test freeing `NULL` with clear - should not crash. */
  GC_FREEZERO(NULL, 0);

  /* Test allocating and freeing with clear. */
  cp = (char *)GC_MALLOC(100);
  CHECK_OUT_OF_MEMORY(cp);

  /* Fill with non-zero data. */
  for (i = 0; i < 100; i++)
    cp[i] = (char)i;

  GC_FREEZERO(cp, 100);
}

/* Test finalizer registration (it tests internal closure functionality). */
static void
test_finalizer_closure(void)
{
#ifndef GC_NO_FINALIZATION
  void *obj1, *obj2;
  GC_finalization_proc old_fn = 0;
  void *old_cd = NULL;
  void *test_data = (void *)(GC_uintptr_t)0x1234;

  /* Test registering finalizer on valid objects. */
  obj1 = GC_MALLOC(100);
  CHECK_OUT_OF_MEMORY(obj1);

  obj2 = GC_MALLOC(50);
  CHECK_OUT_OF_MEMORY(obj2);

  /* Reset the finalizer state. */
  finalizer_called = 0;
  finalizer_obj = NULL;

  /* This tests the internal `GC_make_closure` functionality. */
  GC_REGISTER_FINALIZER(obj1, test_finalizer, NULL, NULL, NULL);
  GC_REGISTER_FINALIZER(obj2, test_finalizer, test_data, NULL, NULL);

  /* Test that we can retrieve the old values. */
  GC_REGISTER_FINALIZER(obj2, NULL, NULL, &old_fn, &old_cd);
  if (!GC_get_find_leak()) {
    TEST_ASSERT(old_fn == test_finalizer);
    TEST_ASSERT(old_cd == test_data);
  }
#endif
}

/* Test finalizer registration and invocation indirectly. */
static void
test_finalizer_indirect(void)
{
#ifndef GC_NO_FINALIZATION
  void *obj = GC_MALLOC(100);

  CHECK_OUT_OF_MEMORY(obj);
  /* This tests the internal `GC_debug_invoke_finalizer` functionality. */
  GC_REGISTER_FINALIZER(obj, test_finalizer, (void *)(GC_uintptr_t)0x1111,
                        NULL, NULL);
#endif
}

static void
test_store_old(void)
{
#ifndef GC_NO_FINALIZATION
  void *obj1, *obj2;
  GC_finalization_proc old_fn = 0;
  void *old_cd = GC_get_find_leak() ? NULL : &obj2;

  /* Test registering finalizer on valid object. */
  obj1 = GC_MALLOC(100);
  CHECK_OUT_OF_MEMORY(obj1);

  /* Register first finalizer. */
  GC_REGISTER_FINALIZER(obj1, test_finalizer, NULL, NULL, NULL);

  /* Register second finalizer, should get old one. */
  GC_REGISTER_FINALIZER(obj1, NULL, NULL, &old_fn, &old_cd);

  TEST_ASSERT(old_fn == test_finalizer || GC_get_find_leak());
  TEST_ASSERT(NULL == old_cd);

  /* Test with a non-debug object - should not crash. */
  obj2 = GC_MALLOC(50);
  CHECK_OUT_OF_MEMORY(obj2);

  GC_REGISTER_FINALIZER(obj2, test_finalizer, (void *)(GC_uintptr_t)0x5555,
                        &old_fn, &old_cd);
  TEST_ASSERT(0 == old_fn);
  TEST_ASSERT(NULL == old_cd);
#endif
}

static void
test_debug_register_finalizer(void)
{
#ifndef GC_NO_FINALIZATION
  void *obj = GC_MALLOC(100);
  GC_finalization_proc old_fn = 0;
  void *old_cd = NULL;

  CHECK_OUT_OF_MEMORY(obj);
  /* Register finalizer. */
  GC_REGISTER_FINALIZER(obj, test_finalizer, (void *)(GC_uintptr_t)0x1234,
                        NULL, NULL);

  /* Register `NULL` finalizer to remove it and get old values. */
  GC_REGISTER_FINALIZER(obj, NULL, NULL, &old_fn, &old_cd);

  if (!GC_get_find_leak()) {
    TEST_ASSERT(old_fn == test_finalizer);
    TEST_ASSERT(old_cd == (void *)(GC_uintptr_t)0x1234);
  }
#endif
}

/* Test other debug finalizer registration functions. */
static void
test_debug_register_finalizer_variants(void)
{
#ifndef GC_NO_FINALIZATION
  void *obj = GC_MALLOC(50);
  GC_finalization_proc old_fn = 0;
  void *old_cd = NULL;
  int is_java_mode;

  CHECK_OUT_OF_MEMORY(obj);
  /* Test ignore_self variant. */
  GC_REGISTER_FINALIZER_IGNORE_SELF(obj, test_finalizer,
                                    (void *)(GC_uintptr_t)0x2345, NULL, NULL);
  GC_REGISTER_FINALIZER_IGNORE_SELF(obj, NULL, NULL, &old_fn, &old_cd);
  if (GC_get_find_leak())
    return;

  TEST_ASSERT(old_fn == test_finalizer);
  TEST_ASSERT(old_cd == (void *)0x2345);

  /* Test no_order variant. */
  GC_REGISTER_FINALIZER_NO_ORDER(obj, test_finalizer,
                                 (void *)(GC_uintptr_t)0x3456, NULL, NULL);
  GC_REGISTER_FINALIZER_NO_ORDER(obj, NULL, NULL, &old_fn, &old_cd);
  TEST_ASSERT(old_fn == test_finalizer);
  TEST_ASSERT(old_cd == (void *)0x3456);

  /* Test unreachable variant (if supported). */
  is_java_mode = GC_get_java_finalization();
  GC_set_java_finalization(1);
  GC_REGISTER_FINALIZER_UNREACHABLE(obj, test_finalizer,
                                    (void *)(GC_uintptr_t)0x4567, NULL, NULL);
  GC_REGISTER_FINALIZER_UNREACHABLE(obj, NULL, NULL, &old_fn, &old_cd);
  TEST_ASSERT(old_fn == test_finalizer);
  TEST_ASSERT(old_cd == (void *)0x4567);
  GC_set_java_finalization(is_java_mode); /*< restore */
#endif
}

#ifdef GC_GCJ_SUPPORT
static struct GC_ms_entry *GC_CALLBACK
dummy_gcj_mark_proc(GC_word *addr, struct GC_ms_entry *mark_stack_top,
                    struct GC_ms_entry *mark_stack_limit, GC_word env)
{
  (void)addr;
  (void)mark_stack_limit;
  (void)env;
  return mark_stack_top;
}
#endif

static void
test_debug_gcj_malloc(void)
{
#ifdef GC_GCJ_SUPPORT
  void *vtable_ptr = (void *)(GC_uintptr_t)0xdeadbeef;
  int i;

  GC_init_gcj_malloc_mp(0U, dummy_gcj_mark_proc, GC_GCJ_MARK_DESCR_OFFSET);

  for (i = 0; i < 100; i++) {
    TEST_ASSERT(GC_GCJ_MALLOC(100 + i, vtable_ptr) != NULL);
  }
#endif
}

static void
test_debug_register_displacement(void)
{
  /* This should not crash and should register the displacement properly. */
  GC_REGISTER_DISPLACEMENT(0);
  GC_REGISTER_DISPLACEMENT(sizeof(void *));
  GC_REGISTER_DISPLACEMENT(16);
}

/* Test that finalizers are actually called. */
static void
test_finalizer_execution(void)
{
#ifndef GC_NO_FINALIZATION
  void *obj;

  /* Reset finalizer state. */
  finalizer_called = 0;
  finalizer_obj = NULL;

  /* Allocate object and register finalizer. */
  obj = GC_MALLOC(100);
  CHECK_OUT_OF_MEMORY(obj);

  GC_REGISTER_FINALIZER(obj, test_finalizer, (void *)(GC_uintptr_t)0x7777,
                        NULL, NULL);

  /* Force collection to trigger the finalizer. */
  GC_gcollect();
#endif
}

int
main(void)
{
  int j;

  GC_INIT();
  if (GC_get_find_leak())
    printf("This test program is not designed for leak detection mode\n");

  for (j = 0; j <= NTHREADS; j++) {
    test_debug_free();
    test_debug_freezero();
    test_finalizer_closure();
    test_finalizer_indirect();
    test_store_old();
    test_debug_register_finalizer();
    test_debug_register_finalizer_variants();
    test_debug_gcj_malloc();
    test_debug_register_displacement();
    test_finalizer_execution();
    if (GC_get_find_leak())
      break; /*< to minimize printed list of leaked objects */
  }
  printf("SUCCEEDED\n");
  return 0;
}
