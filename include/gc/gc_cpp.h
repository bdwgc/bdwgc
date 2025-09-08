/*
 * Copyright (c) 1994 by Xerox Corporation.  All rights reserved.
 *
 * THIS MATERIAL IS PROVIDED AS IS, WITH ABSOLUTELY NO WARRANTY EXPRESSED
 * OR IMPLIED.  ANY USE IS AT YOUR OWN RISK.
 *
 * Permission is hereby granted to use or copy this program for any
 * purpose, provided the above notices are retained on all copies.
 * Permission to modify the code and to distribute modified code is
 * granted, provided the above notices are retained, and a notice that
 * the code was modified is included with the above copyright notice.
 */

#ifndef GC_CPP_H
#define GC_CPP_H

/*
C++ Interface to the Boehm Collector

    John R. Ellis and Jesse Hull

This interface provides access to the Boehm collector.
It provides basic facilities similar to those described in
"Safe, Efficient Garbage Collection for C++", by John R. Ellis and
David L. Detlefs.

All heap-allocated objects are either "collectible" or
"uncollectible".  Programs must explicitly delete uncollectible
objects, whereas the garbage collector will automatically delete
collectible objects when it discovers them to be inaccessible.
Collectible objects may freely point at uncollectible objects and vice
versa.

Objects allocated with the built-in `::operator new` are uncollectible.

Objects derived from class `gc` are collectible.  E.g.:

```
  class A: public gc {...};
  A *a = new A; // a is collectible
```

Collectible instances of non-class types can be allocated using `GC`
(or `UseGC`) placement:

```
  typedef int A[10];
  A *a = new (GC) A;
```

Uncollectible instances of classes derived from `gc` can be allocated
using `NoGC` placement:

```
    class A: public gc {...};
    A *a = new (NoGC) A; // a is uncollectible
```

The `new(PointerFreeGC)` syntax allows the allocation of collectible
objects that are not scanned by the collector.  This useful if you
are allocating compressed data, bitmaps, or network packets.  (In
the latter case, it may remove danger of unfriendly network packets
intentionally containing values that cause spurious memory retention.)

Both uncollectible and collectible objects can be explicitly deleted
with `operator delete`, which invokes an object's destructors and frees
its storage immediately.

A collectible object may have a cleanup function, which will be
invoked when the collector discovers the object to be inaccessible.
An object derived from `gc_cleanup` or containing a member derived
from `gc_cleanup` has a default cleanup function that invokes the
object's destructors.  Explicit cleanup functions may be specified as
an additional placement argument:

```
    A *a = ::new (GC, MyCleanup) A;
```

An object is considered "accessible" by the collector if it can be
reached by a path of pointers from `static` variables, automatic
variables of active functions, or from some object with cleanup
enabled; pointers from an object to itself are ignored.

Thus, if objects A and B both have cleanup functions, and A points at
B, B is considered accessible.  After A's cleanup is invoked and its
storage released, B will then become inaccessible and will have its
cleanup invoked.  If A points at B and B points to A, forming a
cycle, then that is considered a storage leak, and neither will be
collectible.  See the interface in `gc.h` file for low-level facilities
for handling such cycles of objects with cleanup.

The collector cannot guarantee that it will find all inaccessible
objects.  In practice, it finds almost all of them.

Cautions:

  1. Be sure the collector is compiled with the C++ support
     (e.g. `--enable-cplusplus` option is passed to `configure`).

  2. If the compiler does not support `operator new[]`, beware that an
     array of type `T`, where `T` is derived from `gc`, may or may not be
     allocated as a collectible object (it depends on the compiler).  Use
     the explicit GC placement to make the array collectible.  E.g.:

     ```
       class A: public gc {...};
       A *a1 = new A[10];      // collectible or uncollectible?
       A *a2 = new (GC) A[10]; // collectible
     ```

  3. The destructors of collectible arrays of objects derived from
     `gc_cleanup` will not be invoked properly.  E.g.:

     ```
       class A: public gc_cleanup {...};
       A *a = new (GC) A[10]; // destructors not invoked correctly
     ```
     Typically, only the destructor for the first element of the array will
     be invoked when the array is garbage-collected.  To get all the
     destructors of any array executed, you must supply an explicit
     cleanup function:

     ```
       A *a = new (GC, MyCleanUp) A[10];
     ```
     (Implementing cleanup of arrays correctly, portably, and in a way
     that preserves the correct exception semantics, requires a language
     extension, e.g. the `gc` keyword.)

  4. GC name conflicts: Many other systems seem to use the identifier `GC`
     as an abbreviation for "Graphics Context".  Thus, `GC` placement has
     been replaced by `UseGC`.  `GC` is an alias for `UseGC`, unless
     `GC_NAME_CONFLICT` macro is defined.
*/

#include "gc.h"

#ifdef GC_INCLUDE_NEW
#  include <new> // for `std`, `bad_alloc`
#endif

#if defined(GC_INCLUDE_NEW) && (__cplusplus >= 201103L)
#  define GC_PTRDIFF_T std::ptrdiff_t
#  define GC_SIZE_T std::size_t
#else
#  define GC_PTRDIFF_T ptrdiff_t
#  define GC_SIZE_T size_t
#endif

#ifdef GC_NAMESPACE
#  define GC_NS_QUALIFY(T) boehmgc::T
#else
#  define GC_NS_QUALIFY(T) T
#endif

#define GC_cdecl GC_CALLBACK

#if !defined(GC_NO_OPERATOR_NEW_ARRAY)                  \
    && !defined(_ENABLE_ARRAYNEW) /*< Digital Mars */   \
    && (defined(__BORLANDC__) && (__BORLANDC__ < 0x450) \
        || (defined(__GNUC__) && !GC_GNUC_PREREQ(2, 6)) \
        || (defined(_MSC_VER) && _MSC_VER <= 1020)      \
        || (defined(__WATCOMC__) && __WATCOMC__ < 1050))
#  define GC_NO_OPERATOR_NEW_ARRAY
#endif

#if !defined(GC_NO_OPERATOR_NEW_ARRAY) && !defined(GC_OPERATOR_NEW_ARRAY)
#  define GC_OPERATOR_NEW_ARRAY
#endif

#if !defined(GC_NO_INLINE_STD_NEW) && !defined(GC_INLINE_STD_NEW) \
    && (defined(_MSC_VER) || defined(__DMC__)                     \
        || ((defined(__BORLANDC__) || defined(__CYGWIN__)         \
             || defined(__CYGWIN32__) || defined(__MINGW32__)     \
             || defined(__WATCOMC__))                             \
            && !defined(GC_BUILD) && !defined(GC_NOT_DLL)))
// Inlining done to avoid mix up of `new` and `delete` operators by VC++ 9
// (due to arbitrary ordering during linking).
#  define GC_INLINE_STD_NEW
#endif

#if (!defined(__BORLANDC__) || __BORLANDC__ > 0x0620) && !defined(__sgi) \
    && !defined(__WATCOMC__) && (!defined(_MSC_VER) || _MSC_VER > 1020)
#  define GC_PLACEMENT_DELETE
#endif

#if !defined(GC_OPERATOR_SIZED_DELETE)       \
    && !defined(GC_NO_OPERATOR_SIZED_DELETE) \
    && (__cplusplus >= 201402L || _MSVC_LANG >= 201402L) // C++14
#  define GC_OPERATOR_SIZED_DELETE
#endif

#if !defined(GC_OPERATOR_NEW_NOTHROW) && !defined(GC_NO_OPERATOR_NEW_NOTHROW) \
    && ((defined(GC_INCLUDE_NEW)                                              \
         && (__cplusplus >= 201103L || _MSVC_LANG >= 201103L))                \
        || defined(__NOTHROW_T_DEFINED))
// Note: this might require defining `GC_INCLUDE_NEW` macro by client
// before include `gc_cpp.h` file (on Windows).
#  define GC_OPERATOR_NEW_NOTHROW
#endif

#if !defined(GC_NEW_DELETE_THROW_NOT_NEEDED)                      \
    && !defined(GC_NEW_DELETE_NEED_THROW) && GC_GNUC_PREREQ(4, 2) \
    && (__cplusplus < 201103L || defined(__clang__))
#  define GC_NEW_DELETE_NEED_THROW
#endif

#ifndef GC_NEW_DELETE_NEED_THROW
#  define GC_DECL_NEW_THROW /*< empty */
#elif __cplusplus >= 201703L || _MSVC_LANG >= 201703L
// The "dynamic exception" syntax had been deprecated in C++11
// and was removed in C++17.
#  define GC_DECL_NEW_THROW noexcept(false)
#elif defined(GC_INCLUDE_NEW)
#  define GC_DECL_NEW_THROW throw(std::bad_alloc)
#else
#  define GC_DECL_NEW_THROW /*< empty (as `bad_alloc` might be undeclared) */
#endif

#if defined(GC_NEW_ABORTS_ON_OOM) || defined(_LIBCPP_NO_EXCEPTIONS)
#  define GC_OP_NEW_OOM_CHECK(obj) \
    do {                           \
      if (!(obj))                  \
        GC_abort_on_oom();         \
    } while (0)
#elif defined(GC_INCLUDE_NEW)
#  define GC_OP_NEW_OOM_CHECK(obj) \
    if (obj) {                     \
    } else                         \
      throw std::bad_alloc()
#else
// The platform `new` header file is not included, so `bad_alloc` cannot
// be thrown directly.
GC_API GC_OOM_ABORT_THROW_ATTRIBUTE void GC_CALL GC_throw_bad_alloc();
#  define GC_OP_NEW_OOM_CHECK(obj) \
    if (obj) {                     \
    } else                         \
      GC_throw_bad_alloc()
#endif // !GC_NEW_ABORTS_ON_OOM && !GC_INCLUDE_NEW

#ifdef GC_NAMESPACE
namespace boehmgc
{
#endif

enum GCPlacement {
  UseGC,
#ifndef GC_NAME_CONFLICT
  GC = UseGC,
#endif
  NoGC,
  PointerFreeGC
#ifdef GC_ATOMIC_UNCOLLECTABLE
  ,
  PointerFreeNoGC
#endif
};

// Instances of classes derived from `gc` will be allocated in the collected
// heap by default, unless an explicit `NoGC` placement is specified.
class gc
{
public:
  inline void *operator new(GC_SIZE_T);
  inline void *operator new(GC_SIZE_T, GCPlacement);

  // Must be redefined here, since the other overloadings hide
  // the global definition.
  inline void *operator new(GC_SIZE_T, void *) GC_NOEXCEPT;

  inline void operator delete(void *) GC_NOEXCEPT;
#ifdef GC_OPERATOR_SIZED_DELETE
  inline void operator delete(void *, GC_SIZE_T) GC_NOEXCEPT;
#endif

#ifdef GC_PLACEMENT_DELETE
  // Called if construction fails.
  inline void operator delete(void *, GCPlacement) GC_NOEXCEPT;

  inline void operator delete(void *, void *) GC_NOEXCEPT;
#endif // GC_PLACEMENT_DELETE

#ifdef GC_OPERATOR_NEW_ARRAY
  inline void *operator new[](GC_SIZE_T);
  inline void *operator new[](GC_SIZE_T, GCPlacement);
  inline void *operator new[](GC_SIZE_T, void *) GC_NOEXCEPT;
  inline void operator delete[](void *) GC_NOEXCEPT;
#  ifdef GC_OPERATOR_SIZED_DELETE
  inline void operator delete[](void *, GC_SIZE_T) GC_NOEXCEPT;
#  endif
#  ifdef GC_PLACEMENT_DELETE
  inline void operator delete[](void *, GCPlacement) GC_NOEXCEPT;
  inline void operator delete[](void *, void *) GC_NOEXCEPT;
#  endif
#endif
};

// Instances of classes derived from gc_cleanup will be allocated in the
// collected heap by default.  When the collector discovers an inaccessible
// object derived from `gc_cleanup` or containing a member derived from
// `gc_cleanup`, its destructors will be invoked.
class gc_cleanup : virtual public gc
{
public:
  inline gc_cleanup();
  inline virtual ~gc_cleanup();

private:
  inline static void GC_cdecl cleanup(void *obj, void *clientData);
};

extern "C" {
typedef void(GC_CALLBACK *GCCleanUpFunc)(void *obj, void *clientData);
}

#ifdef GC_NAMESPACE
}
#endif

#ifdef _MSC_VER
// Disable warning that "no matching operator delete found; memory will
// not be freed if initialization throws an exception"
#  pragma warning(disable : 4291)
// TODO: "non-member operator new or delete may not be declared inline"
// warning is disabled for now.
#  pragma warning(disable : 4595)
#endif

// Allocates a collectible or uncollectible object, according to the
// value of `gcp`.
// For collectible objects, if `cleanup` is non-null, then when the
// allocated object `obj` becomes inaccessible, the collector will
// invoke `cleanup(obj, clientData)` but will not invoke the object's
// destructors.  It is an error to explicitly `delete` an object
// allocated with a non-null `cleanup`.
// It is an error to specify a non-null `cleanup` with `NoGC` or for
// classes derived from `gc_cleanup` or containing members derived
// from `gc_cleanup`.
inline void *operator new(GC_SIZE_T, GC_NS_QUALIFY(GCPlacement) /* `gcp` */,
                          GC_NS_QUALIFY(GCCleanUpFunc) = 0 /* `cleanup` */,
                          void * /* `clientData` */ = 0);

#ifdef GC_PLACEMENT_DELETE
inline void operator delete(void *, GC_NS_QUALIFY(GCPlacement),
                            GC_NS_QUALIFY(GCCleanUpFunc), void *) GC_NOEXCEPT;
#endif

#ifdef GC_INLINE_STD_NEW

#  ifdef GC_OPERATOR_NEW_ARRAY
inline void *
operator new[](GC_SIZE_T size) GC_DECL_NEW_THROW
{
  void *obj = GC_MALLOC_UNCOLLECTABLE(size);
  GC_OP_NEW_OOM_CHECK(obj);
  return obj;
}

inline void
operator delete[](void *obj) GC_NOEXCEPT
{
  GC_FREE(obj);
}

#    ifdef GC_OPERATOR_NEW_NOTHROW
inline /* `GC_ATTR_MALLOC` */ void *
operator new[](GC_SIZE_T size, const std::nothrow_t &) GC_NOEXCEPT
{
  return GC_MALLOC_UNCOLLECTABLE(size);
}

inline void
operator delete[](void *obj, const std::nothrow_t &) GC_NOEXCEPT
{
  GC_FREE(obj);
}
#    endif
#  endif // GC_OPERATOR_NEW_ARRAY

inline void *
operator new(GC_SIZE_T size) GC_DECL_NEW_THROW
{
  void *obj = GC_MALLOC_UNCOLLECTABLE(size);
  GC_OP_NEW_OOM_CHECK(obj);
  return obj;
}

inline void
operator delete(void *obj) GC_NOEXCEPT
{
  GC_FREE(obj);
}

#  ifdef GC_OPERATOR_NEW_NOTHROW
inline /* `GC_ATTR_MALLOC` */ void *
operator new(GC_SIZE_T size, const std::nothrow_t &) GC_NOEXCEPT
{
  return GC_MALLOC_UNCOLLECTABLE(size);
}

inline void
operator delete(void *obj, const std::nothrow_t &) GC_NOEXCEPT
{
  GC_FREE(obj);
}
#  endif // GC_OPERATOR_NEW_NOTHROW

#  ifdef GC_OPERATOR_SIZED_DELETE
inline void
operator delete(void *obj, GC_SIZE_T) GC_NOEXCEPT
{
  GC_FREE(obj);
}

#    ifdef GC_OPERATOR_NEW_ARRAY
inline void
operator delete[](void *obj, GC_SIZE_T) GC_NOEXCEPT
{
  GC_FREE(obj);
}
#    endif
#  endif // GC_OPERATOR_SIZED_DELETE

#  ifdef _MSC_VER
// This new operator is used by VC++ in case of Debug builds.
inline void *
operator new(GC_SIZE_T size, int /* `nBlockUse` */, const char *szFileName,
             int nLine)
{
#    ifdef GC_DEBUG
  void *obj = GC_debug_malloc_uncollectable(size, szFileName, nLine);
#    else
  void *obj = GC_MALLOC_UNCOLLECTABLE(size);
  (void)szFileName;
  (void)nLine;
#    endif
  GC_OP_NEW_OOM_CHECK(obj);
  return obj;
}

#    ifdef GC_OPERATOR_NEW_ARRAY
// This new operator is used by VC++ 7+ in Debug builds.
inline void *
operator new[](GC_SIZE_T size, int nBlockUse, const char *szFileName,
               int nLine)
{
  return operator new(size, nBlockUse, szFileName, nLine);
}
#    endif
#  endif // _MSC_VER

#elif defined(GC_NO_INLINE_STD_NEW) && defined(_MSC_VER)

// The following ensures that the system default `operator new[]` does not
// get undefined, which is what seems to happen on VC++ 6 for some reason
// if we define a multi-argument `operator new[]`.
// There seems to be no way to redirect new in this environment without
// including this everywhere.
#  ifdef GC_OPERATOR_NEW_ARRAY
void *operator new[](GC_SIZE_T) GC_DECL_NEW_THROW;
void operator delete[](void *) GC_NOEXCEPT;
#    ifdef GC_OPERATOR_NEW_NOTHROW
/* `GC_ATTR_MALLOC` */ void *
operator new[](GC_SIZE_T, const std::nothrow_t &) GC_NOEXCEPT;
void operator delete[](void *, const std::nothrow_t &) GC_NOEXCEPT;
#    endif
#    ifdef GC_OPERATOR_SIZED_DELETE
void operator delete[](void *, GC_SIZE_T) GC_NOEXCEPT;
#    endif

void *operator new[](GC_SIZE_T, int /* `nBlockUse` */,
                     const char * /* `szFileName` */, int /* `nLine` */);
#  endif // GC_OPERATOR_NEW_ARRAY

void *operator new(GC_SIZE_T) GC_DECL_NEW_THROW;
void operator delete(void *) GC_NOEXCEPT;
#  ifdef GC_OPERATOR_NEW_NOTHROW
/* GC_ATTR_MALLOC */ void *operator new(GC_SIZE_T,
                                        const std::nothrow_t &) GC_NOEXCEPT;
void operator delete(void *, const std::nothrow_t &) GC_NOEXCEPT;
#  endif
#  ifdef GC_OPERATOR_SIZED_DELETE
void operator delete(void *, GC_SIZE_T) GC_NOEXCEPT;
#  endif

void *operator new(GC_SIZE_T, int /* `nBlockUse` */,
                   const char * /* `szFileName` */, int /* `nLine` */);

#endif // GC_NO_INLINE_STD_NEW && _MSC_VER

#ifdef GC_OPERATOR_NEW_ARRAY
// The `operator new` for arrays, identical to the above.
inline void *operator new[](GC_SIZE_T, GC_NS_QUALIFY(GCPlacement),
                            GC_NS_QUALIFY(GCCleanUpFunc) = 0,
                            void * /* `clientData` */ = 0);
#endif // GC_OPERATOR_NEW_ARRAY

// Inline implementation.

#ifdef GC_NAMESPACE
namespace boehmgc
{
#endif

inline void *
gc::operator new(GC_SIZE_T size)
{
  void *obj = GC_MALLOC(size);
  GC_OP_NEW_OOM_CHECK(obj);
  return obj;
}

inline void *
gc::operator new(GC_SIZE_T size, GCPlacement gcp)
{
  void *obj;
  switch (gcp) {
  case UseGC:
    obj = GC_MALLOC(size);
    break;
  case PointerFreeGC:
    obj = GC_MALLOC_ATOMIC(size);
    break;
#ifdef GC_ATOMIC_UNCOLLECTABLE
  case PointerFreeNoGC:
    obj = GC_MALLOC_ATOMIC_UNCOLLECTABLE(size);
    break;
#endif
  case NoGC:
  default:
    obj = GC_MALLOC_UNCOLLECTABLE(size);
  }
  GC_OP_NEW_OOM_CHECK(obj);
  return obj;
}

inline void *
gc::operator new(GC_SIZE_T, void *p) GC_NOEXCEPT
{
  return p;
}

inline void
gc::operator delete(void *obj) GC_NOEXCEPT
{
  GC_FREE(obj);
}

#ifdef GC_OPERATOR_SIZED_DELETE
inline void
gc::operator delete(void *obj, GC_SIZE_T) GC_NOEXCEPT
{
  GC_FREE(obj);
}
#endif

#ifdef GC_PLACEMENT_DELETE
inline void
gc::operator delete(void *, void *) GC_NOEXCEPT
{
}

inline void
gc::operator delete(void *obj, GCPlacement) GC_NOEXCEPT
{
  GC_FREE(obj);
}
#endif // GC_PLACEMENT_DELETE

#ifdef GC_OPERATOR_NEW_ARRAY
inline void *
gc::operator new[](GC_SIZE_T size)
{
  return gc::operator new(size);
}

inline void *
gc::operator new[](GC_SIZE_T size, GCPlacement gcp)
{
  return gc::operator new(size, gcp);
}

inline void *
gc::operator new[](GC_SIZE_T, void *p) GC_NOEXCEPT
{
  return p;
}

inline void
gc::operator delete[](void *obj) GC_NOEXCEPT
{
  gc::operator delete(obj);
}

#  ifdef GC_OPERATOR_SIZED_DELETE
inline void
gc::operator delete[](void *obj, GC_SIZE_T size) GC_NOEXCEPT
{
  gc::operator delete(obj, size);
}
#  endif

#  ifdef GC_PLACEMENT_DELETE
inline void
gc::operator delete[](void *, void *) GC_NOEXCEPT
{
}

inline void
gc::operator delete[](void *p, GCPlacement) GC_NOEXCEPT
{
  gc::operator delete(p);
}
#  endif
#endif // GC_OPERATOR_NEW_ARRAY

inline gc_cleanup::~gc_cleanup()
{
#ifndef GC_NO_FINALIZATION
  void *base = GC_base(this);
  if (0 == base)
    return; // Non-heap object.
  GC_register_finalizer_ignore_self(base, 0, 0, 0, 0);
#endif
}

inline void GC_CALLBACK
gc_cleanup::cleanup(void *obj, void *displ)
{
  reinterpret_cast<gc_cleanup *>(
      reinterpret_cast<char *>(obj)
      + static_cast<GC_PTRDIFF_T>(reinterpret_cast<GC_uintptr_t>(displ)))
      ->~gc_cleanup();
}

inline gc_cleanup::gc_cleanup()
{
#ifndef GC_NO_FINALIZATION
  GC_finalization_proc oldProc = 0;
  void *oldData = 0; // to avoid "might be uninitialized" compiler warning
  void *this_ptr = reinterpret_cast<void *>(this);
  void *base = GC_base(this_ptr);
  if (base != 0) {
    // Do not call the debug variant, since this is a real base address.
    GC_register_finalizer_ignore_self(
        base, reinterpret_cast<GC_finalization_proc>(cleanup),
        reinterpret_cast<void *>(
            static_cast<GC_uintptr_t>(reinterpret_cast<char *>(this_ptr)
                                      - reinterpret_cast<char *>(base))),
        &oldProc, &oldData);
    if (oldProc != 0) {
      GC_register_finalizer_ignore_self(base, oldProc, oldData, 0, 0);
    }
  }
#elif defined(CPPCHECK)
  (void)cleanup;
#endif
}

#ifdef GC_NAMESPACE
}
#endif

inline void *
operator new(GC_SIZE_T size, GC_NS_QUALIFY(GCPlacement) gcp,
             GC_NS_QUALIFY(GCCleanUpFunc) cleanup, void *clientData)
{
  void *obj;
  switch (gcp) {
  case GC_NS_QUALIFY(UseGC):
    obj = GC_MALLOC(size);
#ifndef GC_NO_FINALIZATION
    if (cleanup != 0 && obj != 0) {
      GC_REGISTER_FINALIZER_IGNORE_SELF(obj, cleanup, clientData, 0, 0);
    }
#else
    (void)cleanup;
    (void)clientData;
#endif
    break;
  case GC_NS_QUALIFY(PointerFreeGC):
    obj = GC_MALLOC_ATOMIC(size);
    break;
#ifdef GC_ATOMIC_UNCOLLECTABLE
  case GC_NS_QUALIFY(PointerFreeNoGC):
    obj = GC_MALLOC_ATOMIC_UNCOLLECTABLE(size);
    break;
#endif
  case GC_NS_QUALIFY(NoGC):
  default:
    obj = GC_MALLOC_UNCOLLECTABLE(size);
  }
  GC_OP_NEW_OOM_CHECK(obj);
  return obj;
}

#ifdef GC_PLACEMENT_DELETE
inline void
operator delete(void *obj, GC_NS_QUALIFY(GCPlacement),
                GC_NS_QUALIFY(GCCleanUpFunc),
                void * /* `clientData` */) GC_NOEXCEPT
{
  GC_FREE(obj);
}
#endif // GC_PLACEMENT_DELETE

#ifdef GC_OPERATOR_NEW_ARRAY
inline void *
operator new[](GC_SIZE_T size, GC_NS_QUALIFY(GCPlacement) gcp,
               GC_NS_QUALIFY(GCCleanUpFunc) cleanup, void *clientData)
{
  return ::operator new(size, gcp, cleanup, clientData);
}
#endif

#endif /* GC_CPP_H */
