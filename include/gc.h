/*
 * Copyright 1988, 1989 Hans-J. Boehm, Alan J. Demers
 * Copyright (c) 1991-1995 by Xerox Corporation.  All rights reserved.
 * Copyright 1996-1999 by Silicon Graphics.  All rights reserved.
 * Copyright 1999 by Hewlett-Packard Company.  All rights reserved.
 * Copyright (C) 2007 Free Software Foundation, Inc
 * Copyright (c) 2000-2011 by Hewlett-Packard Development Company.
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

/*
 * Note that this defines a large number of tuning hooks, which can
 * safely be ignored in nearly all cases.  For normal use it suffices
 * to call only GC_MALLOC and perhaps GC_REALLOC.
 * For better performance, also look at GC_MALLOC_ATOMIC, and
 * GC_enable_incremental.  If you need an action to be performed
 * immediately before an object is collected, look at GC_register_finalizer.
 * Everything else is best ignored unless you encounter performance
 * problems.
 */

#ifndef GC_H
#define GC_H

#include "gc_version.h"
        /* Define version numbers here to allow test on build machine   */
        /* for cross-builds.  Note that this defines the header         */
        /* version number, which may or may not match that of the       */
        /* dynamic library.  GC_get_version() can be used to obtain     */
        /* the latter.                                                  */

#include "gc_config_macros.h"

#ifdef __cplusplus
  extern "C" {
#endif

typedef void * GC_PTR;  /* preserved only for backward compatibility    */

/* Define word and signed_word to be unsigned and signed types of the   */
/* size as char * or void *.  There seems to be no way to do this       */
/* even semi-portably.  The following is probably no better/worse       */
/* than almost anything else.                                           */
/* The ANSI standard suggests that size_t and ptrdiff_t might be        */
/* better choices.  But those had incorrect definitions on some older   */
/* systems.  Notably "typedef int size_t" is WRONG.                     */
#ifdef _WIN64
# ifdef __int64
    typedef unsigned __int64 GC_word;
    typedef __int64 GC_signed_word;
# else
    typedef unsigned long long GC_word;
    typedef long long GC_signed_word;
# endif
#else
  typedef unsigned long GC_word;
  typedef long GC_signed_word;
#endif

/* Get the GC library version. The returned value is a constant in the  */
/* form: ((version_major<<16) | (version_minor<<8) | alpha_version).    */
GC_API unsigned GC_CALL GC_get_version(void);

/* Public read-only variables */
/* The supplied getter functions are preferred for new code.            */

GC_API GC_word GC_gc_no;/* Counter incremented per collection.          */
                        /* Includes empty GCs at startup.               */
GC_API GC_word GC_CALL GC_get_gc_no(void);
                        /* GC_get_gc_no() is unsynchronized, so         */
                        /* it requires GC_call_with_alloc_lock() to     */
                        /* avoid data races on multiprocessors.         */

#ifdef GC_THREADS
  GC_API int GC_parallel;
                        /* GC is parallelized for performance on        */
                        /* multiprocessors.  Currently set only         */
                        /* implicitly if collector is built with        */
                        /* PARALLEL_MARK defined and if either:         */
                        /*  Env variable GC_NPROCS is set to > 1, or    */
                        /*  GC_NPROCS is not set and this is an MP.     */
                        /* If GC_parallel is set, incremental           */
                        /* collection is only partially functional,     */
                        /* and may not be desirable. This getter does   */
                        /* not use or need synchronization (i.e.        */
                        /* acquiring the GC lock).                      */
  GC_API int GC_CALL GC_get_parallel(void);
#endif


/* Public R/W variables */
/* The supplied setter and getter functions are preferred for new code. */

typedef void * (GC_CALLBACK * GC_oom_func)(size_t /* bytes_requested */);
GC_API GC_oom_func GC_oom_fn;
                        /* When there is insufficient memory to satisfy */
                        /* an allocation request, we return             */
                        /* (*GC_oom_fn)(size).  By default this just    */
                        /* returns NULL.                                */
                        /* If it returns, it must return 0 or a valid   */
                        /* pointer to a previously allocated heap       */
                        /* object.  GC_oom_fn must not be 0.            */
                        /* Both the supplied setter and the getter      */
                        /* acquire the GC lock (to avoid data races).   */
GC_API void GC_CALL GC_set_oom_fn(GC_oom_func);
GC_API GC_oom_func GC_CALL GC_get_oom_fn(void);

GC_API int GC_find_leak;
                        /* Set to true to turn on the find-leak mode    */
                        /* (do not actually garbage collect, but simply */
                        /* report inaccessible memory that was not      */
                        /* deallocated with GC_FREE).  Initial value    */
                        /* is determined by FIND_LEAK macro.            */
                        /* The value should not typically be modified   */
                        /* after GC initialization (and, thus, it does  */
                        /* not use or need synchronization).            */
GC_API void GC_CALL GC_set_find_leak(int);
GC_API int GC_CALL GC_get_find_leak(void);

GC_API int GC_all_interior_pointers;
                        /* Arrange for pointers to object interiors to  */
                        /* be recognized as valid.  Typically should    */
                        /* not be changed after GC initialization (in   */
                        /* case of calling it after the GC is           */
                        /* initialized, the setter acquires the GC lock */
                        /* to avoid data races).  The initial value     */
                        /* depends on whether the GC is built with      */
                        /* ALL_INTERIOR_POINTERS macro defined or not.  */
                        /* Unless DONT_ADD_BYTE_AT_END is defined, this */
                        /* also affects whether sizes are increased by  */
                        /* at least a byte to allow "off the end"       */
                        /* pointer recognition.  Must be only 0 or 1.   */
GC_API void GC_CALL GC_set_all_interior_pointers(int);
GC_API int GC_CALL GC_get_all_interior_pointers(void);

GC_API int GC_finalize_on_demand;
                        /* If nonzero, finalizers will only be run in   */
                        /* response to an explicit GC_invoke_finalizers */
                        /* call.  The default is determined by whether  */
                        /* the FINALIZE_ON_DEMAND macro is defined      */
                        /* when the collector is built.                 */
                        /* The setter and getter are unsynchronized.    */
GC_API void GC_CALL GC_set_finalize_on_demand(int);
GC_API int GC_CALL GC_get_finalize_on_demand(void);

GC_API int GC_java_finalization;
                        /* Mark objects reachable from finalizable      */
                        /* objects in a separate post-pass.  This makes */
                        /* it a bit safer to use non-topologically-     */
                        /* ordered finalization.  Default value is      */
                        /* determined by JAVA_FINALIZATION macro.       */
                        /* Enables GC_register_finalizer_unreachable to */
                        /* work correctly.                              */
                        /* The setter and getter are unsynchronized.    */
GC_API void GC_CALL GC_set_java_finalization(int);
GC_API int GC_CALL GC_get_java_finalization(void);

typedef void (GC_CALLBACK * GC_finalizer_notifier_proc)(void);
GC_API GC_finalizer_notifier_proc GC_finalizer_notifier;
                        /* Invoked by the collector when there are      */
                        /* objects to be finalized.  Invoked at most    */
                        /* once per GC cycle.  Never invoked unless     */
                        /* GC_finalize_on_demand is set.                */
                        /* Typically this will notify a finalization    */
                        /* thread, which will call GC_invoke_finalizers */
                        /* in response.  May be 0 (means no notifier).  */
                        /* Both the supplied setter and the getter      */
                        /* acquire the GC lock (to avoid data races).   */
GC_API void GC_CALL GC_set_finalizer_notifier(GC_finalizer_notifier_proc);
GC_API GC_finalizer_notifier_proc GC_CALL GC_get_finalizer_notifier(void);

GC_API int GC_dont_gc;  /* != 0 ==> Don't collect.  In versions 6.2a1+, */
                        /* this overrides explicit GC_gcollect() calls. */
                        /* Used as a counter, so that nested enabling   */
                        /* and disabling work correctly.  Should        */
                        /* normally be updated with GC_enable() and     */
                        /* GC_disable() calls.  Direct assignment to    */
                        /* GC_dont_gc is deprecated.  To check whether  */
                        /* GC is disabled, GC_is_disabled() is          */
                        /* preferred for new code.                      */

GC_API int GC_dont_expand;
                        /* Don't expand the heap unless explicitly      */
                        /* requested or forced to.  The setter and      */
                        /* getter are unsynchronized.                   */
GC_API void GC_CALL GC_set_dont_expand(int);
GC_API int GC_CALL GC_get_dont_expand(void);

GC_API int GC_use_entire_heap;
                /* Causes the non-incremental collector to use the      */
                /* entire heap before collecting.  This was the only    */
                /* option for GC versions < 5.0.  This sometimes        */
                /* results in more large-block fragmentation, since     */
                /* very large blocks will tend to get broken up         */
                /* during each GC cycle.  It is likely to result in a   */
                /* larger working set, but lower collection             */
                /* frequencies, and hence fewer instructions executed   */
                /* in the collector.                                    */

GC_API int GC_full_freq;    /* Number of partial collections between    */
                            /* full collections.  Matters only if       */
                            /* GC_incremental is set.                   */
                            /* Full collections are also triggered if   */
                            /* the collector detects a substantial      */
                            /* increase in the number of in-use heap    */
                            /* blocks.  Values in the tens are now      */
                            /* perfectly reasonable, unlike for         */
                            /* earlier GC versions.                     */
                        /* The setter and getter are unsynchronized, so */
                        /* GC_call_with_alloc_lock() is required to     */
                        /* avoid data races (if the value is modified   */
                        /* after the GC is put to multi-threaded mode). */
GC_API void GC_CALL GC_set_full_freq(int);
GC_API int GC_CALL GC_get_full_freq(void);

GC_API GC_word GC_non_gc_bytes;
                        /* Bytes not considered candidates for          */
                        /* collection.  Used only to control scheduling */
                        /* of collections.  Updated by                  */
                        /* GC_malloc_uncollectable and GC_free.         */
                        /* Wizards only.                                */
                        /* The setter and getter are unsynchronized, so */
                        /* GC_call_with_alloc_lock() is required to     */
                        /* avoid data races (if the value is modified   */
                        /* after the GC is put to multi-threaded mode). */
GC_API void GC_CALL GC_set_non_gc_bytes(GC_word);
GC_API GC_word GC_CALL GC_get_non_gc_bytes(void);

GC_API int GC_no_dls;
                        /* Don't register dynamic library data segments. */
                        /* Wizards only.  Should be used only if the     */
                        /* application explicitly registers all roots.   */
                        /* (In some environments like Microsoft Windows  */
                        /* and Apple's Darwin, this may also prevent     */
                        /* registration of the main data segment as part */
                        /* of the root set.)                             */
                        /* The setter and getter are unsynchronized.     */
GC_API void GC_CALL GC_set_no_dls(int);
GC_API int GC_CALL GC_get_no_dls(void);

GC_API GC_word GC_free_space_divisor;
                        /* We try to make sure that we allocate at      */
                        /* least N/GC_free_space_divisor bytes between  */
                        /* collections, where N is twice the number     */
                        /* of traced bytes, plus the number of untraced */
                        /* bytes (bytes in "atomic" objects), plus      */
                        /* a rough estimate of the root set size.       */
                        /* N approximates GC tracing work per GC.       */
                        /* Initially, GC_free_space_divisor = 3.        */
                        /* Increasing its value will use less space     */
                        /* but more collection time.  Decreasing it     */
                        /* will appreciably decrease collection time    */
                        /* at the expense of space.                     */
                        /* The setter and getter are unsynchronized, so */
                        /* GC_call_with_alloc_lock() is required to     */
                        /* avoid data races (if the value is modified   */
                        /* after the GC is put to multi-threaded mode). */
GC_API void GC_CALL GC_set_free_space_divisor(GC_word);
GC_API GC_word GC_CALL GC_get_free_space_divisor(void);

GC_API GC_word GC_max_retries;
                        /* The maximum number of GCs attempted before   */
                        /* reporting out of memory after heap           */
                        /* expansion fails.  Initially 0.               */
                        /* The setter and getter are unsynchronized, so */
                        /* GC_call_with_alloc_lock() is required to     */
                        /* avoid data races (if the value is modified   */
                        /* after the GC is put to multi-threaded mode). */
GC_API void GC_CALL GC_set_max_retries(GC_word);
GC_API GC_word GC_CALL GC_get_max_retries(void);


GC_API char *GC_stackbottom;    /* Cool end of user stack.              */
                                /* May be set in the client prior to    */
                                /* calling any GC_ routines.  This      */
                                /* avoids some overhead, and            */
                                /* potentially some signals that can    */
                                /* confuse debuggers.  Otherwise the    */
                                /* collector attempts to set it         */
                                /* automatically.                       */
                                /* For multi-threaded code, this is the */
                                /* cold end of the stack for the        */
                                /* primordial thread.  Portable clients */
                                /* should use GC_get_stack_base(),      */
                                /* GC_call_with_gc_active() and         */
                                /* GC_register_my_thread() instead.     */

GC_API int GC_dont_precollect;  /* Don't collect as part of GC          */
                                /* initialization.  Should be set only  */
                                /* if the client wants a chance to      */
                                /* manually initialize the root set     */
                                /* before the first collection.         */
                                /* Interferes with blacklisting.        */
                                /* Wizards only.  The setter and getter */
                                /* are unsynchronized (and no external  */
                                /* locking is needed since the value is */
                                /* accessed at GC initialization only). */
GC_API void GC_CALL GC_set_dont_precollect(int);
GC_API int GC_CALL GC_get_dont_precollect(void);

GC_API unsigned long GC_time_limit;
                               /* If incremental collection is enabled, */
                               /* We try to terminate collections       */
                               /* after this many milliseconds.  Not a  */
                               /* hard time bound.  Setting this to     */
                               /* GC_TIME_UNLIMITED will essentially    */
                               /* disable incremental collection while  */
                               /* leaving generational collection       */
                               /* enabled.                              */
#define GC_TIME_UNLIMITED 999999
                               /* Setting GC_time_limit to this value   */
                               /* will disable the "pause time exceeded"*/
                               /* tests.                                */
                        /* The setter and getter are unsynchronized, so */
                        /* GC_call_with_alloc_lock() is required to     */
                        /* avoid data races (if the value is modified   */
                        /* after the GC is put to multi-threaded mode). */
GC_API void GC_CALL GC_set_time_limit(unsigned long);
GC_API unsigned long GC_CALL GC_get_time_limit(void);

/* Public procedures */

/* Set whether the GC will allocate executable memory pages or not.     */
/* A non-zero argument instructs the collector to allocate memory with  */
/* the executable flag on.  Must be called before the collector is      */
/* initialized.  May have no effect on some platforms.  The default     */
/* value is controlled by NO_EXECUTE_PERMISSION macro (if present then  */
/* the flag is off).  Portable clients should have                      */
/* GC_set_pages_executable(1) call (before GC_INIT) provided they are   */
/* going to execute code on any of the GC-allocated memory objects.     */
GC_API void GC_CALL GC_set_pages_executable(int);

/* Returns non-zero value if the GC is set to the allocate-executable   */
/* mode.  The mode could be changed by GC_set_pages_executable (before  */
/* GC_INIT) unless the former has no effect on the platform.  Does not  */
/* use or need synchronization (i.e. acquiring the allocator lock).     */
GC_API int GC_CALL GC_get_pages_executable(void);

/* Overrides the default handle-fork mode.  Non-zero value means GC     */
/* should install proper pthread_atfork handlers.  Has effect only if   */
/* called before GC_INIT.  Clients should invoke GC_set_handle_fork(1)  */
/* only if going to use fork with GC functions called in the forked     */
/* child.  (Note that such client and atfork handlers activities are    */
/* not fully POSIX-compliant.)                                          */
GC_API void GC_CALL GC_set_handle_fork(int);

/* Initialize the collector.  Portable clients should call GC_INIT()    */
/* from the main program instead.                                       */
GC_API void GC_CALL GC_init(void);

/* General purpose allocation routines, with roughly malloc calling     */
/* conv.  The atomic versions promise that no relevant pointers are     */
/* contained in the object.  The non-atomic versions guarantee that the */
/* new object is cleared.  GC_malloc_stubborn promises that no changes  */
/* to the object will occur after GC_end_stubborn_change has been       */
/* called on the result of GC_malloc_stubborn.  GC_malloc_uncollectable */
/* allocates an object that is scanned for pointers to collectible      */
/* objects, but is not itself collectible.  The object is scanned even  */
/* if it does not appear to be reachable.  GC_malloc_uncollectable and  */
/* GC_free called on the resulting object implicitly update             */
/* GC_non_gc_bytes appropriately.                                       */
/* Note that the GC_malloc_stubborn support doesn't really exist        */
/* anymore.  MANUAL_VDB provides comparable functionality.              */
GC_API void * GC_CALL GC_malloc(size_t /* size_in_bytes */)
                        GC_ATTR_MALLOC GC_ATTR_ALLOC_SIZE(1);
GC_API void * GC_CALL GC_malloc_atomic(size_t /* size_in_bytes */)
                        GC_ATTR_MALLOC GC_ATTR_ALLOC_SIZE(1);
GC_API char * GC_CALL GC_strdup(const char *) GC_ATTR_MALLOC;
GC_API char * GC_CALL GC_strndup(const char *, size_t) GC_ATTR_MALLOC;
GC_API void * GC_CALL GC_malloc_uncollectable(size_t /* size_in_bytes */)
                        GC_ATTR_MALLOC GC_ATTR_ALLOC_SIZE(1);
GC_API void * GC_CALL GC_malloc_stubborn(size_t /* size_in_bytes */)
                        GC_ATTR_MALLOC GC_ATTR_ALLOC_SIZE(1);

/* GC_memalign() is not well tested.                                    */
GC_API void * GC_CALL GC_memalign(size_t /* align */, size_t /* lb */)
                        GC_ATTR_MALLOC GC_ATTR_ALLOC_SIZE(2);
GC_API int GC_CALL GC_posix_memalign(void ** /* memptr */, size_t /* align */,
                        size_t /* lb */);

/* Explicitly deallocate an object.  Dangerous if used incorrectly.     */
/* Requires a pointer to the base of an object.                         */
/* If the argument is stubborn, it should not be changeable when freed. */
/* An object should not be enabled for finalization when it is          */
/* explicitly deallocated.                                              */
/* GC_free(0) is a no-op, as required by ANSI C for free.               */
GC_API void GC_CALL GC_free(void *);

/* Stubborn objects may be changed only if the collector is explicitly  */
/* informed.  The collector is implicitly informed of coming change     */
/* when such an object is first allocated.  The following routines      */
/* inform the collector that an object will no longer be changed, or    */
/* that it will once again be changed.  Only non-NULL pointer stores    */
/* into the object are considered to be changes.  The argument to       */
/* GC_end_stubborn_change must be exactly the value returned by         */
/* GC_malloc_stubborn or passed to GC_change_stubborn.  (In the second  */
/* case, it may be an interior pointer within 512 bytes of the          */
/* beginning of the objects.)  There is a performance penalty for       */
/* allowing more than one stubborn object to be changed at once, but it */
/* is acceptable to do so.  The same applies to dropping stubborn       */
/* objects that are still changeable.                                   */
GC_API void GC_CALL GC_change_stubborn(void *);
GC_API void GC_CALL GC_end_stubborn_change(void *);

/* Return a pointer to the base (lowest address) of an object given     */
/* a pointer to a location within the object.                           */
/* I.e., map an interior pointer to the corresponding base pointer.     */
/* Note that with debugging allocation, this returns a pointer to the   */
/* actual base of the object, i.e. the debug information, not to        */
/* the base of the user object.                                         */
/* Return 0 if displaced_pointer doesn't point to within a valid        */
/* object.                                                              */
/* Note that a deallocated object in the garbage collected heap         */
/* may be considered valid, even if it has been deallocated with        */
/* GC_free.                                                             */
GC_API void * GC_CALL GC_base(void * /* displaced_pointer */);

/* Given a pointer to the base of an object, return its size in bytes.  */
/* The returned size may be slightly larger than what was originally    */
/* requested.                                                           */
GC_API size_t GC_CALL GC_size(const void * /* object_addr */);

/* For compatibility with C library.  This is occasionally faster than  */
/* a malloc followed by a bcopy.  But if you rely on that, either here  */
/* or with the standard C library, your code is broken.  In my          */
/* opinion, it shouldn't have been invented, but now we're stuck. -HB   */
/* The resulting object has the same kind as the original.              */
/* If the argument is stubborn, the result will have changes enabled.   */
/* It is an error to have changes enabled for the original object.      */
/* Follows ANSI conventions for NULL old_object.                        */
GC_API void * GC_CALL GC_realloc(void * /* old_object */,
                                 size_t /* new_size_in_bytes */)
                        /* 'realloc' attr */ GC_ATTR_ALLOC_SIZE(2);

/* Explicitly increase the heap size.   */
/* Returns 0 on failure, 1 on success.  */
GC_API int GC_CALL GC_expand_hp(size_t /* number_of_bytes */);

/* Limit the heap size to n bytes.  Useful when you're debugging,       */
/* especially on systems that don't handle running out of memory well.  */
/* n == 0 ==> unbounded.  This is the default.  This setter function is */
/* unsynchronized (so it might require GC_call_with_alloc_lock to avoid */
/* data races).                                                         */
GC_API void GC_CALL GC_set_max_heap_size(GC_word /* n */);

/* Inform the collector that a certain section of statically allocated  */
/* memory contains no pointers to garbage collected memory.  Thus it    */
/* need not be scanned.  This is sometimes important if the application */
/* maps large read/write files into the address space, which could be   */
/* mistaken for dynamic library data segments on some systems.          */
/* The section (referred to by low_address) must be pointer-aligned.    */
/* low_address must not be greater than high_address_plus_1.            */
GC_API void GC_CALL GC_exclude_static_roots(void * /* low_address */,
                                        void * /* high_address_plus_1 */);

/* Clear the set of root segments.  Wizards only.                       */
GC_API void GC_CALL GC_clear_roots(void);

/* Add a root segment.  Wizards only.                                   */
/* Both segment start and end are not needed to be pointer-aligned.     */
/* low_address must not be greater than high_address_plus_1.            */
GC_API void GC_CALL GC_add_roots(void * /* low_address */,
                                 void * /* high_address_plus_1 */);

/* Remove a root segment.  Wizards only.                                */
/* May be unimplemented on some platforms.                              */
GC_API void GC_CALL GC_remove_roots(void * /* low_address */,
                                    void * /* high_address_plus_1 */);

/* Add a displacement to the set of those considered valid by the       */
/* collector.  GC_register_displacement(n) means that if p was returned */
/* by GC_malloc, then (char *)p + n will be considered to be a valid    */
/* pointer to p.  N must be small and less than the size of p.          */
/* (All pointers to the interior of objects from the stack are          */
/* considered valid in any case.  This applies to heap objects and      */
/* static data.)                                                        */
/* Preferably, this should be called before any other GC procedures.    */
/* Calling it later adds to the probability of excess memory            */
/* retention.                                                           */
/* This is a no-op if the collector has recognition of                  */
/* arbitrary interior pointers enabled, which is now the default.       */
GC_API void GC_CALL GC_register_displacement(size_t /* n */);

/* The following version should be used if any debugging allocation is  */
/* being done.                                                          */
GC_API void GC_CALL GC_debug_register_displacement(size_t /* n */);

/* Explicitly trigger a full, world-stop collection.    */
GC_API void GC_CALL GC_gcollect(void);

/* Same as above but ignores the default stop_func setting and tries to */
/* unmap as much memory as possible (regardless of the corresponding    */
/* switch setting).  The recommended usage: on receiving a system       */
/* low-memory event; before retrying a system call failed because of    */
/* the system is running out of resources.                              */
GC_API void GC_CALL GC_gcollect_and_unmap(void);

/* Trigger a full world-stopped collection.  Abort the collection if    */
/* and when stop_func returns a nonzero value.  Stop_func will be       */
/* called frequently, and should be reasonably fast.  (stop_func is     */
/* called with the allocation lock held and the world might be stopped; */
/* it's not allowed for stop_func to manipulate pointers to the garbage */
/* collected heap or call most of GC functions.)  This works even       */
/* if no virtual dirty bits, and hence incremental collection is not    */
/* available for this architecture.  Collections can be aborted faster  */
/* than normal pause times for incremental collection.  However,        */
/* aborted collections do no useful work; the next collection needs     */
/* to start from the beginning.  stop_func must not be 0.               */
/* GC_try_to_collect() returns 0 if the collection was aborted (or the  */
/* collections are disabled), 1 if it succeeded.                        */
typedef int (GC_CALLBACK * GC_stop_func)(void);
GC_API int GC_CALL GC_try_to_collect(GC_stop_func /* stop_func */);

/* Set and get the default stop_func.  The default stop_func is used by */
/* GC_gcollect() and by implicitly triggered collections (except for    */
/* the case when handling out of memory).  Must not be 0.               */
/* Both the setter and getter acquire the GC lock to avoid data races.  */
GC_API void GC_CALL GC_set_stop_func(GC_stop_func /* stop_func */);
GC_API GC_stop_func GC_CALL GC_get_stop_func(void);

/* Return the number of bytes in the heap.  Excludes collector private  */
/* data structures.  Excludes the unmapped memory (returned to the OS). */
/* Includes empty blocks and fragmentation loss.  Includes some pages   */
/* that were allocated but never written.                               */
/* This is an unsynchronized getter, so it should be called typically   */
/* with the GC lock held to avoid data races on multiprocessors (the    */
/* alternative is to use GC_get_heap_usage_safe API call instead).      */
/* This getter remains lock-free (unsynchronized) for compatibility     */
/* reason since some existing clients call it from a GC callback        */
/* holding the allocator lock.  (This API function and the following    */
/* four ones below were made thread-safe in GC v7.2alpha1 and          */
/* reverted back in v7.2alpha7 for the reason described.)               */
GC_API size_t GC_CALL GC_get_heap_size(void);

/* Return a lower bound on the number of free bytes in the heap         */
/* (excluding the unmapped memory space).  This is an unsynchronized    */
/* getter (see GC_get_heap_size comment regarding thread-safety).       */
GC_API size_t GC_CALL GC_get_free_bytes(void);

/* Return the size (in bytes) of the unmapped memory (which is returned */
/* to the OS but could be remapped back by the collector later unless   */
/* the OS runs out of system/virtual memory). This is an unsynchronized */
/* getter (see GC_get_heap_size comment regarding thread-safety).       */
GC_API size_t GC_CALL GC_get_unmapped_bytes(void);

/* Return the number of bytes allocated since the last collection.      */
/* This is an unsynchronized getter (see GC_get_heap_size comment       */
/* regarding thread-safety).                                            */
GC_API size_t GC_CALL GC_get_bytes_since_gc(void);

/* Return the total number of bytes allocated in this process.          */
/* Never decreases, except due to wrapping.  This is an unsynchronized  */
/* getter (see GC_get_heap_size comment regarding thread-safety).       */
GC_API size_t GC_CALL GC_get_total_bytes(void);

/* Return the heap usage information.  This is a thread-safe (atomic)   */
/* alternative for the five above getters.   (This function acquires    */
/* the allocator lock thus preventing data racing and returning the     */
/* consistent result.)  Passing NULL pointer is allowed for any         */
/* argument.  Returned (filled in) values are of word type.             */
/* (This API function was introduced in GC v7.2alpha7 at the same time  */
/* when GC_get_heap_size and the friends were made lock-free again.)    */
GC_API void GC_CALL GC_get_heap_usage_safe(GC_word * /* pheap_size */,
                                           GC_word * /* pfree_bytes */,
                                           GC_word * /* punmapped_bytes */,
                                           GC_word * /* pbytes_since_gc */,
                                           GC_word * /* ptotal_bytes */);

/* Disable garbage collection.  Even GC_gcollect calls will be          */
/* ineffective.                                                         */
GC_API void GC_CALL GC_disable(void);

/* Return non-zero (TRUE) if and only if garbage collection is disabled */
/* (i.e., GC_dont_gc value is non-zero).  Does not acquire the lock.    */
GC_API int GC_CALL GC_is_disabled(void);

/* Try to re-enable garbage collection.  GC_disable() and GC_enable()   */
/* calls nest.  Garbage collection is enabled if the number of calls to */
/* both functions is equal.                                             */
GC_API void GC_CALL GC_enable(void);

/* Enable incremental/generational collection.  Not advisable unless    */
/* dirty bits are available or most heap objects are pointer-free       */
/* (atomic) or immutable.  Don't use in leak finding mode.  Ignored if  */
/* GC_dont_gc is non-zero.  Only the generational piece of this is      */
/* functional if GC_parallel is TRUE or if GC_time_limit is             */
/* GC_TIME_UNLIMITED.  Causes thread-local variant of GC_gcj_malloc()   */
/* to revert to locked allocation.  Must be called before any such      */
/* GC_gcj_malloc() calls.  For best performance, should be called as    */
/* early as possible.  On some platforms, calling it later may have     */
/* adverse effects.                                                     */
/* Safe to call before GC_INIT().  Includes a  GC_init() call.          */
GC_API void GC_CALL GC_enable_incremental(void);

/* Does incremental mode write-protect pages?  Returns zero or  */
/* more of the following, or'ed together:                       */
#define GC_PROTECTS_POINTER_HEAP  1 /* May protect non-atomic objects.  */
#define GC_PROTECTS_PTRFREE_HEAP  2
#define GC_PROTECTS_STATIC_DATA   4 /* Currently never.                 */
#define GC_PROTECTS_STACK         8 /* Probably impractical.            */

#define GC_PROTECTS_NONE 0
GC_API int GC_CALL GC_incremental_protection_needs(void);

/* Perform some garbage collection work, if appropriate.        */
/* Return 0 if there is no more work to be done.                */
/* Typically performs an amount of work corresponding roughly   */
/* to marking from one page.  May do more work if further       */
/* progress requires it, e.g. if incremental collection is      */
/* disabled.  It is reasonable to call this in a wait loop      */
/* until it returns 0.                                          */
GC_API int GC_CALL GC_collect_a_little(void);

/* Allocate an object of size lb bytes.  The client guarantees that     */
/* as long as the object is live, it will be referenced by a pointer    */
/* that points to somewhere within the first 256 bytes of the object.   */
/* (This should normally be declared volatile to prevent the compiler   */
/* from invalidating this assertion.)  This routine is only useful      */
/* if a large array is being allocated.  It reduces the chance of       */
/* accidentally retaining such an array as a result of scanning an      */
/* integer that happens to be an address inside the array.  (Actually,  */
/* it reduces the chance of the allocator not finding space for such    */
/* an array, since it will try hard to avoid introducing such a false   */
/* reference.)  On a SunOS 4.X or MS Windows system this is recommended */
/* for arrays likely to be larger than 100K or so.  For other systems,  */
/* or if the collector is not configured to recognize all interior      */
/* pointers, the threshold is normally much higher.                     */
GC_API void * GC_CALL GC_malloc_ignore_off_page(size_t /* lb */)
                        GC_ATTR_MALLOC GC_ATTR_ALLOC_SIZE(1);
GC_API void * GC_CALL GC_malloc_atomic_ignore_off_page(size_t /* lb */)
                        GC_ATTR_MALLOC GC_ATTR_ALLOC_SIZE(1);

#ifdef GC_ADD_CALLER
# define GC_EXTRAS GC_RETURN_ADDR, __FILE__, __LINE__
# define GC_EXTRA_PARAMS GC_word ra, const char * s, int i
#else
# define GC_EXTRAS __FILE__, __LINE__
# define GC_EXTRA_PARAMS const char * s, int i
#endif

/* The following is only defined if the library has been suitably       */
/* compiled:                                                            */
GC_API void * GC_CALL GC_malloc_atomic_uncollectable(
                                                size_t /* size_in_bytes */)
                        GC_ATTR_MALLOC GC_ATTR_ALLOC_SIZE(1);
GC_API void * GC_CALL GC_debug_malloc_atomic_uncollectable(size_t,
                                                           GC_EXTRA_PARAMS)
                        GC_ATTR_MALLOC GC_ATTR_ALLOC_SIZE(1);

/* Debugging (annotated) allocation.  GC_gcollect will check            */
/* objects allocated in this way for overwrites, etc.                   */
GC_API void * GC_CALL GC_debug_malloc(size_t /* size_in_bytes */,
                                      GC_EXTRA_PARAMS)
                        GC_ATTR_MALLOC GC_ATTR_ALLOC_SIZE(1);
GC_API void * GC_CALL GC_debug_malloc_atomic(size_t /* size_in_bytes */,
                                             GC_EXTRA_PARAMS)
                        GC_ATTR_MALLOC GC_ATTR_ALLOC_SIZE(1);
GC_API char * GC_CALL GC_debug_strdup(const char *,
                                      GC_EXTRA_PARAMS) GC_ATTR_MALLOC;
GC_API char * GC_CALL GC_debug_strndup(const char *, size_t,
                                       GC_EXTRA_PARAMS) GC_ATTR_MALLOC;
GC_API void * GC_CALL GC_debug_malloc_uncollectable(
                        size_t /* size_in_bytes */, GC_EXTRA_PARAMS)
                        GC_ATTR_MALLOC GC_ATTR_ALLOC_SIZE(1);
GC_API void * GC_CALL GC_debug_malloc_stubborn(size_t /* size_in_bytes */,
                                               GC_EXTRA_PARAMS)
                        GC_ATTR_MALLOC GC_ATTR_ALLOC_SIZE(1);
GC_API void * GC_CALL GC_debug_malloc_ignore_off_page(
                        size_t /* size_in_bytes */, GC_EXTRA_PARAMS)
                        GC_ATTR_MALLOC GC_ATTR_ALLOC_SIZE(1);
GC_API void * GC_CALL GC_debug_malloc_atomic_ignore_off_page(
                        size_t /* size_in_bytes */, GC_EXTRA_PARAMS)
                        GC_ATTR_MALLOC GC_ATTR_ALLOC_SIZE(1);
GC_API void GC_CALL GC_debug_free(void *);
GC_API void * GC_CALL GC_debug_realloc(void * /* old_object */,
                        size_t /* new_size_in_bytes */, GC_EXTRA_PARAMS)
                        /* 'realloc' attr */ GC_ATTR_ALLOC_SIZE(2);
GC_API void GC_CALL GC_debug_change_stubborn(void *);
GC_API void GC_CALL GC_debug_end_stubborn_change(void *);

/* Routines that allocate objects with debug information (like the      */
/* above), but just fill in dummy file and line number information.     */
/* Thus they can serve as drop-in malloc/realloc replacements.  This    */
/* can be useful for two reasons:                                       */
/* 1) It allows the collector to be built with DBG_HDRS_ALL defined     */
/*    even if some allocation calls come from 3rd party libraries       */
/*    that can't be recompiled.                                         */
/* 2) On some platforms, the file and line information is redundant,    */
/*    since it can be reconstructed from a stack trace.  On such        */
/*    platforms it may be more convenient not to recompile, e.g. for    */
/*    leak detection.  This can be accomplished by instructing the      */
/*    linker to replace malloc/realloc with these.                      */
GC_API void * GC_CALL GC_debug_malloc_replacement(size_t /* size_in_bytes */)
                        GC_ATTR_MALLOC GC_ATTR_ALLOC_SIZE(1);
GC_API void * GC_CALL GC_debug_realloc_replacement(void * /* object_addr */,
                                                   size_t /* size_in_bytes */)
                        /* 'realloc' attr */ GC_ATTR_ALLOC_SIZE(2);

#ifdef GC_DEBUG_REPLACEMENT
# define GC_MALLOC(sz) GC_debug_malloc_replacement(sz)
# define GC_REALLOC(old, sz) GC_debug_realloc_replacement(old, sz)
#elif defined(GC_DEBUG)
# define GC_MALLOC(sz) GC_debug_malloc(sz, GC_EXTRAS)
# define GC_REALLOC(old, sz) GC_debug_realloc(old, sz, GC_EXTRAS)
#else
# define GC_MALLOC(sz) GC_malloc(sz)
# define GC_REALLOC(old, sz) GC_realloc(old, sz)
#endif /* !GC_DEBUG_REPLACEMENT && !GC_DEBUG */

#ifdef GC_DEBUG
# define GC_MALLOC_ATOMIC(sz) GC_debug_malloc_atomic(sz, GC_EXTRAS)
# define GC_STRDUP(s) GC_debug_strdup(s, GC_EXTRAS)
# define GC_STRNDUP(s, sz) GC_debug_strndup(s, sz, GC_EXTRAS)
# define GC_MALLOC_ATOMIC_UNCOLLECTABLE(sz) \
                        GC_debug_malloc_atomic_uncollectable(sz, GC_EXTRAS)
# define GC_MALLOC_UNCOLLECTABLE(sz) \
                        GC_debug_malloc_uncollectable(sz, GC_EXTRAS)
# define GC_MALLOC_IGNORE_OFF_PAGE(sz) \
                        GC_debug_malloc_ignore_off_page(sz, GC_EXTRAS)
# define GC_MALLOC_ATOMIC_IGNORE_OFF_PAGE(sz) \
                        GC_debug_malloc_atomic_ignore_off_page(sz, GC_EXTRAS)
# define GC_FREE(p) GC_debug_free(p)
# define GC_REGISTER_FINALIZER(p, f, d, of, od) \
      GC_debug_register_finalizer(p, f, d, of, od)
# define GC_REGISTER_FINALIZER_IGNORE_SELF(p, f, d, of, od) \
      GC_debug_register_finalizer_ignore_self(p, f, d, of, od)
# define GC_REGISTER_FINALIZER_NO_ORDER(p, f, d, of, od) \
      GC_debug_register_finalizer_no_order(p, f, d, of, od)
# define GC_REGISTER_FINALIZER_UNREACHABLE(p, f, d, of, od) \
      GC_debug_register_finalizer_unreachable(p, f, d, of, od)
# define GC_MALLOC_STUBBORN(sz) GC_debug_malloc_stubborn(sz, GC_EXTRAS)
# define GC_CHANGE_STUBBORN(p) GC_debug_change_stubborn(p)
# define GC_END_STUBBORN_CHANGE(p) GC_debug_end_stubborn_change(p)
# define GC_GENERAL_REGISTER_DISAPPEARING_LINK(link, obj) \
      GC_general_register_disappearing_link(link, GC_base(obj))
# define GC_REGISTER_DISPLACEMENT(n) GC_debug_register_displacement(n)
#else
# define GC_MALLOC_ATOMIC(sz) GC_malloc_atomic(sz)
# define GC_STRDUP(s) GC_strdup(s)
# define GC_STRNDUP(s, sz) GC_strndup(s, sz)
# define GC_MALLOC_ATOMIC_UNCOLLECTABLE(sz) GC_malloc_atomic_uncollectable(sz)
# define GC_MALLOC_UNCOLLECTABLE(sz) GC_malloc_uncollectable(sz)
# define GC_MALLOC_IGNORE_OFF_PAGE(sz) \
                        GC_malloc_ignore_off_page(sz)
# define GC_MALLOC_ATOMIC_IGNORE_OFF_PAGE(sz) \
                        GC_malloc_atomic_ignore_off_page(sz)
# define GC_FREE(p) GC_free(p)
# define GC_REGISTER_FINALIZER(p, f, d, of, od) \
      GC_register_finalizer(p, f, d, of, od)
# define GC_REGISTER_FINALIZER_IGNORE_SELF(p, f, d, of, od) \
      GC_register_finalizer_ignore_self(p, f, d, of, od)
# define GC_REGISTER_FINALIZER_NO_ORDER(p, f, d, of, od) \
      GC_register_finalizer_no_order(p, f, d, of, od)
# define GC_REGISTER_FINALIZER_UNREACHABLE(p, f, d, of, od) \
      GC_register_finalizer_unreachable(p, f, d, of, od)
# define GC_MALLOC_STUBBORN(sz) GC_malloc_stubborn(sz)
# define GC_CHANGE_STUBBORN(p) GC_change_stubborn(p)
# define GC_END_STUBBORN_CHANGE(p) GC_end_stubborn_change(p)
# define GC_GENERAL_REGISTER_DISAPPEARING_LINK(link, obj) \
      GC_general_register_disappearing_link(link, obj)
# define GC_REGISTER_DISPLACEMENT(n) GC_register_displacement(n)
#endif /* !GC_DEBUG */

/* The following are included because they are often convenient, and    */
/* reduce the chance for a misspecified size argument.  But calls may   */
/* expand to something syntactically incorrect if t is a complicated    */
/* type expression.  Note that, unlike C++ new operator, these ones     */
/* may return NULL (if out of memory).                                  */
#define GC_NEW(t)               ((t*)GC_MALLOC(sizeof(t)))
#define GC_NEW_ATOMIC(t)        ((t*)GC_MALLOC_ATOMIC(sizeof(t)))
#define GC_NEW_STUBBORN(t)      ((t*)GC_MALLOC_STUBBORN(sizeof(t)))
#define GC_NEW_UNCOLLECTABLE(t) ((t*)GC_MALLOC_UNCOLLECTABLE(sizeof(t)))

#ifdef GC_REQUIRE_WCSDUP
  /* This might be unavailable on some targets (or not needed). */
  /* wchar_t should be defined in stddef.h */
  GC_API wchar_t * GC_CALL GC_wcsdup(const wchar_t *) GC_ATTR_MALLOC;
  GC_API wchar_t * GC_CALL GC_debug_wcsdup(const wchar_t *,
                                           GC_EXTRA_PARAMS) GC_ATTR_MALLOC;
# ifdef GC_DEBUG
#   define GC_WCSDUP(s) GC_debug_wcsdup(s, GC_EXTRAS)
# else
#   define GC_WCSDUP(s) GC_wcsdup(s)
# endif
#endif /* GC_REQUIRE_WCSDUP */

/* Finalization.  Some of these primitives are grossly unsafe.          */
/* The idea is to make them both cheap, and sufficient to build         */
/* a safer layer, closer to Modula-3, Java, or PCedar finalization.     */
/* The interface represents my conclusions from a long discussion       */
/* with Alan Demers, Dan Greene, Carl Hauser, Barry Hayes,              */
/* Christian Jacobi, and Russ Atkinson.  It's not perfect, and          */
/* probably nobody else agrees with it.     Hans-J. Boehm  3/13/92      */
typedef void (GC_CALLBACK * GC_finalization_proc)(void * /* obj */,
                                                  void * /* client_data */);

GC_API void GC_CALL GC_register_finalizer(void * /* obj */,
                        GC_finalization_proc /* fn */, void * /* cd */,
                        GC_finalization_proc * /* ofn */, void ** /* ocd */);
GC_API void GC_CALL GC_debug_register_finalizer(void * /* obj */,
                        GC_finalization_proc /* fn */, void * /* cd */,
                        GC_finalization_proc * /* ofn */, void ** /* ocd */);
        /* When obj is no longer accessible, invoke             */
        /* (*fn)(obj, cd).  If a and b are inaccessible, and    */
        /* a points to b (after disappearing links have been    */
        /* made to disappear), then only a will be              */
        /* finalized.  (If this does not create any new         */
        /* pointers to b, then b will be finalized after the    */
        /* next collection.)  Any finalizable object that       */
        /* is reachable from itself by following one or more    */
        /* pointers will not be finalized (or collected).       */
        /* Thus cycles involving finalizable objects should     */
        /* be avoided, or broken by disappearing links.         */
        /* All but the last finalizer registered for an object  */
        /* is ignored.  No-op in the find-leak mode.            */
        /* Finalization may be removed by passing 0 as fn.      */
        /* Finalizers are implicitly unregistered when they are */
        /* enqueued for finalization (i.e. become ready to be   */
        /* finalized).                                          */
        /* The old finalizer and client data are stored in      */
        /* *ofn and *ocd.  (ofn and/or ocd may be NULL.         */
        /* The allocation lock is held while *ofn and *ocd are  */
        /* updated.  In case of error (no memory to register    */
        /* new finalizer), *ofn and *ocd remain unchanged.)     */
        /* Fn is never invoked on an accessible object,         */
        /* provided hidden pointers are converted to real       */
        /* pointers only if the allocation lock is held, and    */
        /* such conversions are not performed by finalization   */
        /* routines.                                            */
        /* If GC_register_finalizer is aborted as a result of   */
        /* a signal, the object may be left with no             */
        /* finalization, even if neither the old nor new        */
        /* finalizer were NULL.                                 */
        /* Obj should be the starting address of an object      */
        /* allocated by GC_malloc or friends. Obj may also be   */
        /* NULL or point to something outside GC heap (in this  */
        /* case, fn is ignored, *ofn and *ocd are set to NULL). */
        /* Note that any garbage collectible object referenced  */
        /* by cd will be considered accessible until the        */
        /* finalizer is invoked.                                */

/* Another version of the above follows.  It ignores            */
/* self-cycles, i.e. pointers from a finalizable object to      */
/* itself.  There is a stylistic argument that this is wrong,   */
/* but it's unavoidable for C++, since the compiler may         */
/* silently introduce these.  It's also benign in that specific */
/* case.  And it helps if finalizable objects are split to      */
/* avoid cycles.                                                */
/* Note that cd will still be viewed as accessible, even if it  */
/* refers to the object itself.                                 */
GC_API void GC_CALL GC_register_finalizer_ignore_self(void * /* obj */,
                        GC_finalization_proc /* fn */, void * /* cd */,
                        GC_finalization_proc * /* ofn */, void ** /* ocd */);
GC_API void GC_CALL GC_debug_register_finalizer_ignore_self(void * /* obj */,
                        GC_finalization_proc /* fn */, void * /* cd */,
                        GC_finalization_proc * /* ofn */, void ** /* ocd */);

/* Another version of the above.  It ignores all cycles.        */
/* It should probably only be used by Java implementations.     */
/* Note that cd will still be viewed as accessible, even if it  */
/* refers to the object itself.                                 */
GC_API void GC_CALL GC_register_finalizer_no_order(void * /* obj */,
                        GC_finalization_proc /* fn */, void * /* cd */,
                        GC_finalization_proc * /* ofn */, void ** /* ocd */);
GC_API void GC_CALL GC_debug_register_finalizer_no_order(void * /* obj */,
                        GC_finalization_proc /* fn */, void * /* cd */,
                        GC_finalization_proc * /* ofn */, void ** /* ocd */);

/* This is a special finalizer that is useful when an object's  */
/* finalizer must be run when the object is known to be no      */
/* longer reachable, not even from other finalizable objects.   */
/* It behaves like "normal" finalization, except that the       */
/* finalizer is not run while the object is reachable from      */
/* other objects specifying unordered finalization.             */
/* Effectively it allows an object referenced, possibly         */
/* indirectly, from an unordered finalizable object to override */
/* the unordered finalization request.                          */
/* This can be used in combination with finalizer_no_order so   */
/* as to release resources that must not be released while an   */
/* object can still be brought back to life by other            */
/* finalizers.                                                  */
/* Only works if GC_java_finalization is set.  Probably only    */
/* of interest when implementing a language that requires       */
/* unordered finalization (e.g. Java, C#).                      */
GC_API void GC_CALL GC_register_finalizer_unreachable(void * /* obj */,
                        GC_finalization_proc /* fn */, void * /* cd */,
                        GC_finalization_proc * /* ofn */, void ** /* ocd */);
GC_API void GC_CALL GC_debug_register_finalizer_unreachable(void * /* obj */,
                        GC_finalization_proc /* fn */, void * /* cd */,
                        GC_finalization_proc * /* ofn */, void ** /* ocd */);

#define GC_NO_MEMORY 2  /* Failure due to lack of memory.       */

/* The following routine may be used to break cycles between    */
/* finalizable objects, thus causing cyclic finalizable         */
/* objects to be finalized in the correct order.  Standard      */
/* use involves calling GC_register_disappearing_link(&p),      */
/* where p is a pointer that is not followed by finalization    */
/* code, and should not be considered in determining            */
/* finalization order.                                          */
GC_API int GC_CALL GC_register_disappearing_link(void ** /* link */);
        /* Link should point to a field of a heap allocated     */
        /* object obj.  *link will be cleared when obj is       */
        /* found to be inaccessible.  This happens BEFORE any   */
        /* finalization code is invoked, and BEFORE any         */
        /* decisions about finalization order are made.         */
        /* This is useful in telling the finalizer that         */
        /* some pointers are not essential for proper           */
        /* finalization.  This may avoid finalization cycles.   */
        /* Note that obj may be resurrected by another          */
        /* finalizer, and thus the clearing of *link may        */
        /* be visible to non-finalization code.                 */
        /* There's an argument that an arbitrary action should  */
        /* be allowed here, instead of just clearing a pointer. */
        /* But this causes problems if that action alters, or   */
        /* examines connectivity.  Returns GC_DUPLICATE if link */
        /* was already registered, GC_SUCCESS if registration   */
        /* succeeded, GC_NO_MEMORY if it failed for lack of     */
        /* memory, and GC_oom_fn did not handle the problem.    */
        /* Only exists for backward compatibility.  See below:  */

GC_API int GC_CALL GC_general_register_disappearing_link(void ** /* link */,
                                                         void * /* obj */);
        /* A slight generalization of the above. *link is       */
        /* cleared when obj first becomes inaccessible.  This   */
        /* can be used to implement weak pointers easily and    */
        /* safely. Typically link will point to a location      */
        /* holding a disguised pointer to obj.  (A pointer      */
        /* inside an "atomic" object is effectively disguised.) */
        /* In this way, weak pointers are broken before any     */
        /* object reachable from them gets finalized.           */
        /* Each link may be registered only with one obj value, */
        /* i.e. all objects but the last one (link registered   */
        /* with) are ignored.  This was added after a long      */
        /* email discussion with John Ellis.                    */
        /* link must be non-NULL (and be properly aligned).     */
        /* obj must be a pointer to the first word of an object */
        /* allocated by GC_malloc or friends.  It is unsafe to  */
        /* explicitly deallocate the object containing link.    */
        /* Explicit deallocation of obj may or may not cause    */
        /* link to eventually be cleared.                       */
        /* No-op in the find-leak mode.                         */
        /* This function can be used to implement certain types */
        /* of weak pointers.  Note, however, this generally     */
        /* requires that the allocation lock is held (see       */
        /* GC_call_with_alloc_lock() below) when the disguised  */
        /* pointer is accessed.  Otherwise a strong pointer     */
        /* could be recreated between the time the collector    */
        /* decides to reclaim the object and the link is        */
        /* cleared.  Returns GC_SUCCESS if registration         */
        /* succeeded (a new link is registered), GC_DUPLICATE   */
        /* if link was already registered (with some object),   */
        /* GC_NO_MEMORY if registration failed for lack of      */
        /* memory (and GC_oom_fn did not handle the problem),   */
        /* GC_UNIMPLEMENTED if GC_find_leak is true.            */

GC_API int GC_CALL GC_unregister_disappearing_link(void ** /* link */);
        /* Undoes a registration by either of the above two     */
        /* routines.  Returns 0 if link was not actually        */
        /* registered (otherwise returns 1).                    */

/* Returns !=0 if GC_invoke_finalizers has something to do.     */
GC_API int GC_CALL GC_should_invoke_finalizers(void);

GC_API int GC_CALL GC_invoke_finalizers(void);
        /* Run finalizers for all objects that are ready to     */
        /* be finalized.  Return the number of finalizers       */
        /* that were run.  Normally this is also called         */
        /* implicitly during some allocations.  If              */
        /* GC_finalize_on_demand is nonzero, it must be called  */
        /* explicitly.                                          */

/* Explicitly tell the collector that an object is reachable    */
/* at a particular program point.  This prevents the argument   */
/* pointer from being optimized away, even it is otherwise no   */
/* longer needed.  It should have no visible effect in the      */
/* absence of finalizers or disappearing links.  But it may be  */
/* needed to prevent finalizers from running while the          */
/* associated external resource is still in use.                */
/* The function is sometimes called keep_alive in other         */
/* settings.                                                    */
#if defined(__GNUC__) && !defined(__INTEL_COMPILER)
# define GC_reachable_here(ptr) \
                __asm__ __volatile__(" " : : "X"(ptr) : "memory")
#else
  GC_API void GC_CALL GC_noop1(GC_word);
# define GC_reachable_here(ptr) GC_noop1((GC_word)(ptr))
#endif

/* GC_set_warn_proc can be used to redirect or filter warning messages. */
/* p may not be a NULL pointer.  Both the setter and the getter acquire */
/* the GC lock (to avoid data races).                                   */
typedef void (GC_CALLBACK * GC_warn_proc)(char * /* msg */,
                                          GC_word /* arg */);
GC_API void GC_CALL GC_set_warn_proc(GC_warn_proc /* p */);
/* GC_get_warn_proc returns the current warn_proc.                      */
GC_API GC_warn_proc GC_CALL GC_get_warn_proc(void);

/* GC_ignore_warn_proc may be used as an argument for GC_set_warn_proc  */
/* to suppress all warnings (unless statistics printing is turned on).  */
GC_API void GC_CALLBACK GC_ignore_warn_proc(char *, GC_word);

/* The following is intended to be used by a higher level       */
/* (e.g. Java-like) finalization facility.  It is expected      */
/* that finalization code will arrange for hidden pointers to   */
/* disappear.  Otherwise objects can be accessed after they     */
/* have been collected.                                         */
/* Note that putting pointers in atomic objects or in           */
/* non-pointer slots of "typed" objects is equivalent to        */
/* disguising them in this way, and may have other advantages.  */
typedef GC_word GC_hidden_pointer;
#define GC_HIDE_POINTER(p) (~(GC_hidden_pointer)(p))
/* Converting a hidden pointer to a real pointer requires verifying     */
/* that the object still exists.  This involves acquiring the           */
/* allocator lock to avoid a race with the collector.                   */
#define GC_REVEAL_POINTER(p) ((void *)GC_HIDE_POINTER(p))

#if defined(I_HIDE_POINTERS) || defined(GC_I_HIDE_POINTERS)
  /* This exists only for compatibility (the GC-prefixed symbols are    */
  /* preferred for new code).                                           */
# define HIDE_POINTER(p) GC_HIDE_POINTER(p)
# define REVEAL_POINTER(p) GC_REVEAL_POINTER(p)
#endif

typedef void * (GC_CALLBACK * GC_fn_type)(void * /* client_data */);
GC_API void * GC_CALL GC_call_with_alloc_lock(GC_fn_type /* fn */,
                                                void * /* client_data */);

/* These routines are intended to explicitly notify the collector       */
/* of new threads.  Often this is unnecessary because thread creation   */
/* is implicitly intercepted by the collector, using header-file        */
/* defines, or linker-based interception.  In the long run the intent   */
/* is to always make redundant registration safe.  In the short run,    */
/* this is being implemented a platform at a time.                      */
/* The interface is complicated by the fact that we probably will not   */
/* ever be able to automatically determine the stack base for thread    */
/* stacks on all platforms.                                             */

/* Structure representing the base of a thread stack.  On most          */
/* platforms this contains just a single address.                       */
struct GC_stack_base {
  void * mem_base; /* Base of memory stack. */
# if defined(__ia64) || defined(__ia64__) || defined(_M_IA64)
    void * reg_base; /* Base of separate register stack. */
# endif
};

typedef void * (GC_CALLBACK * GC_stack_base_func)(
                struct GC_stack_base * /* sb */, void * /* arg */);

/* Call a function with a stack base structure corresponding to         */
/* somewhere in the GC_call_with_stack_base frame.  This often can      */
/* be used to provide a sufficiently accurate stack base.  And we       */
/* implement it everywhere.                                             */
GC_API void * GC_CALL GC_call_with_stack_base(GC_stack_base_func /* fn */,
                                              void * /* arg */);

#define GC_SUCCESS 0
#define GC_DUPLICATE 1          /* Was already registered.              */
#define GC_NO_THREADS 2         /* No thread support in GC.             */
        /* GC_NO_THREADS is not returned by any GC function anymore.    */
#define GC_UNIMPLEMENTED 3 /* Not yet implemented on this platform.     */

#if defined(GC_DARWIN_THREADS) || defined(GC_WIN32_THREADS)
  /* Use implicit thread registration and processing (via Win32 DllMain */
  /* or Darwin task_threads).  Deprecated.  Must be called before       */
  /* GC_INIT() and other GC routines.  Should be avoided if             */
  /* GC_pthread_create, GC_beginthreadex (or GC_CreateThread) could be  */
  /* called instead.  Disables parallelized GC on Win32.                */
  GC_API void GC_CALL GC_use_threads_discovery(void);
#endif

#ifdef GC_THREADS
  /* Return the signal number (constant) used by the garbage collector  */
  /* to suspend threads on POSIX systems.  Return -1 otherwise.         */
  GC_API int GC_CALL GC_get_suspend_signal(void);

  /* Explicitly enable GC_register_my_thread() invocation.              */
  /* Done implicitly if a GC thread-creation function is called (or     */
  /* implicit thread registration is activated).  Otherwise, it must    */
  /* be called from the main (or any previously registered) thread      */
  /* between the collector initialization and the first explicit        */
  /* registering of a thread (it should be called as late as possible). */
  GC_API void GC_CALL GC_allow_register_threads(void);

  /* Register the current thread, with the indicated stack base, as     */
  /* a new thread whose stack(s) should be traced by the GC.  If it     */
  /* is not implicitly called by the GC, this must be called before a   */
  /* thread can allocate garbage collected memory, or assign pointers   */
  /* to the garbage collected heap.  Once registered, a thread will be  */
  /* stopped during garbage collections.                                */
  /* This call must be previously enabled (see above).                  */
  /* This should never be called from the main thread, where it is      */
  /* always done implicitly.  This is normally done implicitly if GC_   */
  /* functions are called to create the thread, e.g. by including gc.h  */
  /* (which redefines some system functions) before calling the system  */
  /* thread creation function.  Nonetheless, thread cleanup routines    */
  /* (e.g., pthread key destructor) typically require manual thread     */
  /* registering (and unregistering) if pointers to GC-allocated        */
  /* objects are manipulated inside.                                    */
  /* It is also always done implicitly on some platforms if             */
  /* GC_use_threads_discovery() is called at start-up.  Except for the  */
  /* latter case, the explicit call is normally required for threads    */
  /* created by third-party libraries.                                  */
  /* A manually registered thread requires manual unregistering.        */
  GC_API int GC_CALL GC_register_my_thread(const struct GC_stack_base *);

  /* Unregister the current thread.  Only an explicitly registered      */
  /* thread (i.e. for which GC_register_my_thread() returns GC_SUCCESS) */
  /* is allowed (and required) to call this function.  (As a special    */
  /* exception, it is also allowed to once unregister the main thread.) */
  /* The thread may no longer allocate garbage collected memory or      */
  /* manipulate pointers to the garbage collected heap after making     */
  /* this call.  Specifically, if it wants to return or otherwise       */
  /* communicate a pointer to the garbage-collected heap to another     */
  /* thread, it must do this before calling GC_unregister_my_thread,    */
  /* most probably by saving it in a global data structure.  Must not   */
  /* be called inside a GC callback function (except for                */
  /* GC_call_with_stack_base() one).                                    */
  GC_API int GC_CALL GC_unregister_my_thread(void);
#endif /* GC_THREADS */

/* Wrapper for functions that are likely to block (or, at least, do not */
/* allocate garbage collected memory and/or manipulate pointers to the  */
/* garbage collected heap) for an appreciable length of time.  While fn */
/* is running, the collector is said to be in the "inactive" state for  */
/* the current thread (this means that the thread is not suspended and  */
/* the thread's stack frames "belonging" to the functions in the        */
/* "inactive" state are not scanned during garbage collections).  It is */
/* allowed for fn to call GC_call_with_gc_active() (even recursively),  */
/* thus temporarily toggling the collector's state back to "active".    */
GC_API void * GC_CALL GC_do_blocking(GC_fn_type /* fn */,
                                     void * /* client_data */);

/* Call a function switching to the "active" state of the collector for */
/* the current thread (i.e. the user function is allowed to call any    */
/* GC function and/or manipulate pointers to the garbage collected      */
/* heap).  GC_call_with_gc_active() has the functionality opposite to   */
/* GC_do_blocking() one.  It is assumed that the collector is already   */
/* initialized and the current thread is registered.  fn may toggle     */
/* the collector thread's state temporarily to "inactive" one by using  */
/* GC_do_blocking.  GC_call_with_gc_active() often can be used to       */
/* provide a sufficiently accurate stack base.                          */
GC_API void * GC_CALL GC_call_with_gc_active(GC_fn_type /* fn */,
                                             void * /* client_data */);

/* Attempt to fill in the GC_stack_base structure with the stack base   */
/* for this thread.  This appears to be required to implement anything  */
/* like the JNI AttachCurrentThread in an environment in which new      */
/* threads are not automatically registered with the collector.         */
/* It is also unfortunately hard to implement well on many platforms.   */
/* Returns GC_SUCCESS or GC_UNIMPLEMENTED.  This function acquires the  */
/* GC lock on some platforms.                                           */
GC_API int GC_CALL GC_get_stack_base(struct GC_stack_base *);

/* The following routines are primarily intended for use with a         */
/* preprocessor which inserts calls to check C pointer arithmetic.      */
/* They indicate failure by invoking the corresponding _print_proc.     */

/* Check that p and q point to the same object.                 */
/* Fail conspicuously if they don't.                            */
/* Returns the first argument.                                  */
/* Succeeds if neither p nor q points to the heap.              */
/* May succeed if both p and q point to between heap objects.   */
GC_API void * GC_CALL GC_same_obj(void * /* p */, void * /* q */);

/* Checked pointer pre- and post- increment operations.  Note that      */
/* the second argument is in units of bytes, not multiples of the       */
/* object size.  This should either be invoked from a macro, or the     */
/* call should be automatically generated.                              */
GC_API void * GC_CALL GC_pre_incr(void **, ptrdiff_t /* how_much */);
GC_API void * GC_CALL GC_post_incr(void **, ptrdiff_t /* how_much */);

/* Check that p is visible                                              */
/* to the collector as a possibly pointer containing location.          */
/* If it isn't fail conspicuously.                                      */
/* Returns the argument in all cases.  May erroneously succeed          */
/* in hard cases.  (This is intended for debugging use with             */
/* untyped allocations.  The idea is that it should be possible, though */
/* slow, to add such a call to all indirect pointer stores.)            */
/* Currently useless for multi-threaded worlds.                         */
GC_API void * GC_CALL GC_is_visible(void * /* p */);

/* Check that if p is a pointer to a heap page, then it points to       */
/* a valid displacement within a heap object.                           */
/* Fail conspicuously if this property does not hold.                   */
/* Uninteresting with GC_all_interior_pointers.                         */
/* Always returns its argument.                                         */
GC_API void * GC_CALL GC_is_valid_displacement(void * /* p */);

/* Explicitly dump the GC state.  This is most often called from the    */
/* debugger, or by setting the GC_DUMP_REGULARLY environment variable,  */
/* but it may be useful to call it from client code during debugging.   */
/* Defined only if the library has been compiled without NO_DEBUGGING.  */
GC_API void GC_CALL GC_dump(void);

/* Safer, but slow, pointer addition.  Probably useful mainly with      */
/* a preprocessor.  Useful only for heap pointers.                      */
/* Only the macros without trailing digits are meant to be used         */
/* by clients.  These are designed to model the available C pointer     */
/* arithmetic expressions.                                              */
/* Even then, these are probably more useful as                         */
/* documentation than as part of the API.                               */
/* Note that GC_PTR_ADD evaluates the first argument more than once.    */
#if defined(GC_DEBUG) && defined(__GNUC__)
# define GC_PTR_ADD3(x, n, type_of_result) \
        ((type_of_result)GC_same_obj((x)+(n), (x)))
# define GC_PRE_INCR3(x, n, type_of_result) \
        ((type_of_result)GC_pre_incr((void **)(&(x)), (n)*sizeof(*x)))
# define GC_POST_INCR3(x, n, type_of_result) \
        ((type_of_result)GC_post_incr((void **)(&(x)), (n)*sizeof(*x)))
# define GC_PTR_ADD(x, n) GC_PTR_ADD3(x, n, typeof(x))
# define GC_PRE_INCR(x, n) GC_PRE_INCR3(x, n, typeof(x))
# define GC_POST_INCR(x) GC_POST_INCR3(x, 1, typeof(x))
# define GC_POST_DECR(x) GC_POST_INCR3(x, -1, typeof(x))
#else /* !GC_DEBUG || !__GNUC__ */
  /* We can't do this right without typeof, which ANSI decided was not    */
  /* sufficiently useful.  Without it we resort to the non-debug version. */
  /* FIXME: This should eventually support C++0x decltype.                */
# define GC_PTR_ADD(x, n) ((x)+(n))
# define GC_PRE_INCR(x, n) ((x) += (n))
# define GC_POST_INCR(x) ((x)++)
# define GC_POST_DECR(x) ((x)--)
#endif /* !GC_DEBUG || !__GNUC__ */

/* Safer assignment of a pointer to a non-stack location.       */
#ifdef GC_DEBUG
# define GC_PTR_STORE(p, q) \
        (*(void **)GC_is_visible(p) = GC_is_valid_displacement(q))
#else
# define GC_PTR_STORE(p, q) (*(p) = (q))
#endif

/* Functions called to report pointer checking errors */
GC_API void (GC_CALLBACK * GC_same_obj_print_proc)(void * /* p */,
                                                   void * /* q */);
GC_API void (GC_CALLBACK * GC_is_valid_displacement_print_proc)(void *);
GC_API void (GC_CALLBACK * GC_is_visible_print_proc)(void *);

#ifdef GC_PTHREADS
  /* For pthread support, we generally need to intercept a number of    */
  /* thread library calls.  We do that here by macro defining them.     */
# include "gc_pthread_redirects.h"
#endif

/* This returns a list of objects, linked through their first word.     */
/* Its use can greatly reduce lock contention problems, since the       */
/* allocation lock can be acquired and released many fewer times.       */
GC_API void * GC_CALL GC_malloc_many(size_t /* lb */);
#define GC_NEXT(p) (*(void * *)(p))     /* Retrieve the next element    */
                                        /* in returned list.            */

/* A filter function to control the scanning of dynamic libraries.      */
/* If implemented, called by GC before registering a dynamic library    */
/* (discovered by GC) section as a static data root (called only as     */
/* a last reason not to register).  The filename of the library, the    */
/* address and the length of the memory region (section) are passed.    */
/* This routine should return nonzero if that region should be scanned. */
/* Always called with the allocation lock held.  Depending on the       */
/* platform, might be called with the "world" stopped.                  */
typedef int (GC_CALLBACK * GC_has_static_roots_func)(
                                        const char * /* dlpi_name */,
                                        void * /* section_start */,
                                        size_t /* section_size */);

/* Register a new callback (a user-supplied filter) to control the      */
/* scanning of dynamic libraries.  Replaces any previously registered   */
/* callback.  May be 0 (means no filtering).  May be unused on some     */
/* platforms (if the filtering is unimplemented or inappropriate).      */
GC_API void GC_CALL GC_register_has_static_roots_callback(
                                        GC_has_static_roots_func);

#if defined(GC_WIN32_THREADS) && !defined(GC_PTHREADS)

# if !defined(GC_NO_THREAD_DECLS) || defined(GC_BUILD)

#   ifdef __cplusplus
      } /* Including windows.h in an extern "C" context no longer works. */
#   endif

#   if !defined(_WIN32_WCE) && !defined(__CEGCC__)
#     include <process.h> /* For _beginthreadex, _endthreadex */
#   endif

#   include <windows.h>

#   ifdef __cplusplus
      extern "C" {
#   endif

#   ifdef GC_UNDERSCORE_STDCALL
      /* Explicitly prefix exported/imported WINAPI (__stdcall) symbols */
      /* with '_' (underscore).  Might be useful if MinGW/x86 is used.  */
#     define GC_CreateThread _GC_CreateThread
#     define GC_ExitThread _GC_ExitThread
#   endif

#   if !defined(_UINTPTR_T) && !defined(_UINTPTR_T_DEFINED) \
       && !defined(UINTPTR_MAX)
      typedef GC_word GC_uintptr_t;
#   else
      typedef uintptr_t GC_uintptr_t;
#   endif
#   define GC_WIN32_SIZE_T GC_uintptr_t

    /* All threads must be created using GC_CreateThread or             */
    /* GC_beginthreadex, or must explicitly call GC_register_my_thread  */
    /* (and call GC_unregister_my_thread before thread termination), so */
    /* that they will be recorded in the thread table.  For backward    */
    /* compatibility, it is possible to build the GC with GC_DLL        */
    /* defined, and to call GC_use_threads_discovery.  This implicitly  */
    /* registers all created threads, but appears to be less robust.    */
    /* Currently the collector expects all threads to fall through and  */
    /* terminate normally, or call GC_endthreadex() or GC_ExitThread,   */
    /* so that the thread is properly unregistered.                     */
    GC_API HANDLE WINAPI GC_CreateThread(
                LPSECURITY_ATTRIBUTES /* lpThreadAttributes */,
                GC_WIN32_SIZE_T /* dwStackSize */,
                LPTHREAD_START_ROUTINE /* lpStartAddress */,
                LPVOID /* lpParameter */, DWORD /* dwCreationFlags */,
                LPDWORD /* lpThreadId */);

#   ifndef DECLSPEC_NORETURN
      /* Typically defined in winnt.h. */
#     define DECLSPEC_NORETURN /* empty */
#   endif

    GC_API DECLSPEC_NORETURN void WINAPI GC_ExitThread(
                                                DWORD /* dwExitCode */);

#   if !defined(_WIN32_WCE) && !defined(__CEGCC__)
      GC_API GC_uintptr_t GC_CALL GC_beginthreadex(
                        void * /* security */, unsigned /* stack_size */,
                        unsigned (__stdcall *)(void *),
                        void * /* arglist */, unsigned /* initflag */,
                        unsigned * /* thrdaddr */);

      /* Note: _endthreadex() is not currently marked as no-return in   */
      /* VC++ and MinGW headers, so we don't mark it neither.           */
      GC_API void GC_CALL GC_endthreadex(unsigned /* retval */);
#   endif /* !_WIN32_WCE */

# endif /* !GC_NO_THREAD_DECLS */

# ifdef GC_WINMAIN_REDIRECT
    /* win32_threads.c implements the real WinMain(), which will start  */
    /* a new thread to call GC_WinMain() after initializing the garbage */
    /* collector.                                                       */
#   define WinMain GC_WinMain
# endif

  /* For compatibility only. */
# define GC_use_DllMain GC_use_threads_discovery

# ifndef GC_NO_THREAD_REDIRECTS
#   define CreateThread GC_CreateThread
#   define ExitThread GC_ExitThread
#   undef _beginthreadex
#   define _beginthreadex GC_beginthreadex
#   undef _endthreadex
#   define _endthreadex GC_endthreadex
/* #define _beginthread { > "Please use _beginthreadex instead of _beginthread" < } */
# endif /* !GC_NO_THREAD_REDIRECTS */

#endif /* GC_WIN32_THREADS */

/* Public setter and getter for switching "unmap as much as possible"   */
/* mode on(1) and off(0).  Has no effect unless unmapping is turned on. */
/* Has no effect on implicitly-initiated garbage collections.  Initial  */
/* value is controlled by GC_FORCE_UNMAP_ON_GCOLLECT.  The setter and   */
/* getter are unsynchronized.                                           */
GC_API void GC_CALL GC_set_force_unmap_on_gcollect(int);
GC_API int GC_CALL GC_get_force_unmap_on_gcollect(void);

/* Fully portable code should call GC_INIT() from the main program      */
/* before making any other GC_ calls.  On most platforms this is a      */
/* no-op and the collector self-initializes.  But a number of           */
/* platforms make that too hard.                                        */
/* A GC_INIT call is required if the collector is built with            */
/* THREAD_LOCAL_ALLOC defined and the initial allocation call is not    */
/* to GC_malloc() or GC_malloc_atomic().                                */

#if defined(__CYGWIN32__) || defined(__CYGWIN__)
  /* Similarly gnu-win32 DLLs need explicit initialization from the     */
  /* main program, as does AIX.                                         */
# ifdef __x86_64__
    /* Cygwin/x64 does not add leading underscore to symbols anymore.   */
    extern int __data_start__[], __data_end__[];
    extern int __bss_start__[], __bss_end__[];
#   define GC_DATASTART (__data_start__ < __bss_start__ \
                         ? (void *)__data_start__ : (void *)__bss_start__)
#   define GC_DATAEND (__data_end__ > __bss_end__ \
                       ? (void *)__data_end__ : (void *)__bss_end__)
# else
    extern int _data_start__[], _data_end__[], _bss_start__[], _bss_end__[];
#   define GC_DATASTART (_data_start__ < _bss_start__ \
                         ? (void *)_data_start__ : (void *)_bss_start__)
#   define GC_DATAEND (_data_end__ > _bss_end__ \
                      ? (void *)_data_end__ : (void *)_bss_end__)
# endif /* !__x86_64__ */
# define GC_INIT_CONF_ROOTS GC_add_roots(GC_DATASTART, GC_DATAEND); \
                                 GC_gcollect() /* For blacklisting. */
        /* Required at least if GC is in a DLL.  And doesn't hurt. */
#elif defined(_AIX)
  extern int _data[], _end[];
# define GC_DATASTART ((void *)((ulong)_data))
# define GC_DATAEND ((void *)((ulong)_end))
# define GC_INIT_CONF_ROOTS GC_add_roots(GC_DATASTART, GC_DATAEND)
#else
# define GC_INIT_CONF_ROOTS /* empty */
#endif

#ifdef GC_DONT_EXPAND
  /* Set GC_dont_expand to TRUE at start-up */
# define GC_INIT_CONF_DONT_EXPAND GC_set_dont_expand(1)
#else
# define GC_INIT_CONF_DONT_EXPAND /* empty */
#endif

#ifdef GC_FORCE_UNMAP_ON_GCOLLECT
  /* Turn on "unmap as much as possible on explicit GC" mode at start-up */
# define GC_INIT_CONF_FORCE_UNMAP_ON_GCOLLECT \
                GC_set_force_unmap_on_gcollect(1)
#else
# define GC_INIT_CONF_FORCE_UNMAP_ON_GCOLLECT /* empty */
#endif

#ifdef GC_MAX_RETRIES
  /* Set GC_max_retries to the desired value at start-up */
# define GC_INIT_CONF_MAX_RETRIES GC_set_max_retries(GC_MAX_RETRIES)
#else
# define GC_INIT_CONF_MAX_RETRIES /* empty */
#endif

#ifdef GC_FREE_SPACE_DIVISOR
  /* Set GC_free_space_divisor to the desired value at start-up */
# define GC_INIT_CONF_FREE_SPACE_DIVISOR \
                GC_set_free_space_divisor(GC_FREE_SPACE_DIVISOR)
#else
# define GC_INIT_CONF_FREE_SPACE_DIVISOR /* empty */
#endif

#ifdef GC_FULL_FREQ
  /* Set GC_full_freq to the desired value at start-up */
# define GC_INIT_CONF_FULL_FREQ GC_set_full_freq(GC_FULL_FREQ)
#else
# define GC_INIT_CONF_FULL_FREQ /* empty */
#endif

#ifdef GC_TIME_LIMIT
  /* Set GC_time_limit to the desired value at start-up */
# define GC_INIT_CONF_TIME_LIMIT GC_set_time_limit(GC_TIME_LIMIT)
#else
# define GC_INIT_CONF_TIME_LIMIT /* empty */
#endif

#ifdef GC_MAXIMUM_HEAP_SIZE
  /* Limit the heap size to the desired value (useful for debugging).   */
  /* The limit could be overridden either at the program start-up by    */
  /* the similar environment variable or anytime later by the           */
  /* corresponding API function call.                                   */
# define GC_INIT_CONF_MAXIMUM_HEAP_SIZE \
                GC_set_max_heap_size(GC_MAXIMUM_HEAP_SIZE)
#else
# define GC_INIT_CONF_MAXIMUM_HEAP_SIZE /* empty */
#endif

#ifdef GC_IGNORE_WARN
  /* Turn off all warnings at start-up (after GC initialization) */
# define GC_INIT_CONF_IGNORE_WARN GC_set_warn_proc(GC_ignore_warn_proc)
#else
# define GC_INIT_CONF_IGNORE_WARN /* empty */
#endif

#ifdef GC_INITIAL_HEAP_SIZE
  /* Set heap size to the desired value at start-up */
# define GC_INIT_CONF_INITIAL_HEAP_SIZE \
                { size_t heap_size = GC_get_heap_size(); \
                  if (heap_size < (GC_INITIAL_HEAP_SIZE)) \
                    (void)GC_expand_hp((GC_INITIAL_HEAP_SIZE) - heap_size); }
#else
# define GC_INIT_CONF_INITIAL_HEAP_SIZE /* empty */
#endif

/* Portable clients should call this at the program start-up.  More     */
/* over, some platforms require this call to be done strictly from the  */
/* primordial thread.                                                   */
#define GC_INIT() { GC_INIT_CONF_DONT_EXPAND; /* pre-init */ \
                    GC_INIT_CONF_FORCE_UNMAP_ON_GCOLLECT; \
                    GC_INIT_CONF_MAX_RETRIES; \
                    GC_INIT_CONF_FREE_SPACE_DIVISOR; \
                    GC_INIT_CONF_FULL_FREQ; \
                    GC_INIT_CONF_TIME_LIMIT; \
                    GC_INIT_CONF_MAXIMUM_HEAP_SIZE; \
                    GC_init(); /* real GC initialization */ \
                    GC_INIT_CONF_ROOTS; /* post-init */ \
                    GC_INIT_CONF_IGNORE_WARN; \
                    GC_INIT_CONF_INITIAL_HEAP_SIZE; }

/* win32S may not free all resources on process exit.   */
/* This explicitly deallocates the heap.                */
GC_API void GC_CALL GC_win32_free_heap(void);

#if defined(_AMIGA) && !defined(GC_AMIGA_MAKINGLIB)
  /* Allocation really goes through GC_amiga_allocwrapper_do    */
# include "gc_amiga_redirects.h"
#endif

#ifdef __cplusplus
  }  /* end of extern "C" */
#endif

#endif /* GC_H */
