# Copyright (c) 1999-2001 by Red Hat, Inc. All rights reserved.
# Copyright (c) 2005-2009 Hewlett-Packard Development Company, L.P.
# Copyright (c) 2009-2025 Ivan Maidanski
#
# THIS MATERIAL IS PROVIDED AS IS, WITH ABSOLUTELY NO WARRANTY EXPRESSED
# OR IMPLIED.  ANY USE IS AT YOUR OWN RISK.
#
# Permission is hereby granted to use or copy this program
# for any purpose,  provided the above notices are retained on all copies.
# Permission to modify the code and to distribute modified code is granted,
# provided the above notices are retained, and a notice that the code was
# modified is included with the above copyright notice.

dnl Process this file with autoconf to produce configure.

dnl Initialization.
AC_INIT(gc,8.2.10,https://github.com/bdwgc/bdwgc/issues)
dnl Version must conform to: [0-9]+[.][0-9]+[.][0-9]+

AC_CONFIG_SRCDIR(gcj_mlc.c)
AC_CONFIG_MACRO_DIR([m4])
AC_CANONICAL_TARGET
AC_PREREQ(2.61)
GC_SET_VERSION
AM_INIT_AUTOMAKE([foreign nostdinc subdir-objects])
AC_CONFIG_HEADERS([include/config.h])
AM_MAINTAINER_MODE

AC_SUBST(PACKAGE)
AC_SUBST(GC_VERSION)

AM_PROG_CC_C_O
AC_PROG_CXX
AM_PROG_AS
AC_PROG_INSTALL
LT_INIT([disable-static])
# Only the shared libraries are produced by default, use "--enable-static"
# option to override it.
dnl Note: If Autoconf reports that LIBTOOL (or AC_ENABLE_SHARED, or
dnl AC_PROG_LIBTOOL) is undefined, Libtool installation should be checked.

# Special CFLAGS to use when building
gc_cflags=""

# Set to "yes" on platforms where mmap should be used instead of sbrk.
# This will define USE_MMAP.
gc_use_mmap=""

# We should set -fexceptions if we are using gcc and might be used
# inside something like gcj.  This is the zeroth approximation:
if test :"$GCC": = :yes: ; then
    gc_cflags="${gc_cflags} -fexceptions"
else
    case "$host" in
      hppa*-*-hpux* )
        if test :$GCC: != :"yes": ; then
            gc_cflags="${gc_cflags} +ESdbgasm"
        fi
        # :TODO: actually we should check using Autoconf if
        #     the compiler supports this option.
        ;;
    esac
fi

#   target_optspace     --enable-target-optspace ("yes", "no", "")
case "${target_optspace}:${host}" in
  yes:*)
    gc_cflags="${gc_cflags} -Os"
    ;;
  :m32r-* | :d10v-* | :d30v-*)
    gc_cflags="${gc_cflags} -Os"
    ;;
  no:* | :*)
    # Nothing.
    ;;
esac

# Set any host dependent compiler flags.
case "${host}" in
  mips-tx39-*|mipstx39-unknown-*)
        gc_cflags="${gc_cflags} -G 0"
        ;;
  *)
        ;;
esac

AC_MSG_CHECKING([for emscripten])
AC_COMPILE_IFELSE([AC_LANG_SOURCE([[
#   ifndef __EMSCRIPTEN__
#     error This is not Emscripten
#   endif
  ]])], [emscripten=yes], [emscripten=no])
AM_CONDITIONAL(EMSCRIPTEN, test x$emscripten = xyes)
AC_MSG_RESULT([$emscripten])

AC_ARG_ENABLE(emscripten-asyncify,
    [AS_HELP_STRING([--enable-emscripten-asyncify],
                    [use Emscripten asyncify feature])])
# Use this option if your program is targeting -sASYNCIFY.  The latter is
# required to scan the stack, ASYNCIFY_STACK_SIZE is probably needed for
# gctest only.
AS_IF([test "${emscripten}" = yes -a "${enable_emscripten_asyncify}" = yes],
      [gc_cflags="${gc_cflags} -DEMSCRIPTEN_ASYNCIFY"
       gc_cflags="${gc_cflags} -sASYNCIFY -sASYNCIFY_STACK_SIZE=128000"])

GC_CFLAGS=${gc_cflags}
AC_SUBST(GC_CFLAGS)

dnl Extra user-defined flags to pass both to C and C++ compilers.
AC_SUBST([CFLAGS_EXTRA])

AC_ARG_ENABLE(threads,
  [AS_HELP_STRING([--enable-threads=TYPE], [choose threading package])],
  THREADS=$enableval,
  [ AC_MSG_CHECKING([for thread model used by GCC])
    THREADS=`$CC -v 2>&1 | sed -n 's/^Thread model: //p'`
    if test -z "$THREADS" -o "x$emscripten" = "xyes"; then
      THREADS=no
    fi
    if test "$THREADS" = "posix"; then
      case "$host" in
        *-*-mingw*)
          # Adjust thread model if cross-compiling for MinGW.
          THREADS=win32
          ;;
      esac
    fi
    AC_MSG_RESULT([$THREADS]) ])

AC_ARG_ENABLE(parallel-mark,
    [AS_HELP_STRING([--disable-parallel-mark],
        [do not parallelize marking and free list construction])],
    [case "$THREADS" in
      no | none | single)
        if test "${enable_parallel_mark}" != no; then
          AC_MSG_ERROR([Parallel mark requires --enable-threads=x spec])
        fi
        ;;
     esac])

AC_ARG_ENABLE(thread-local-alloc,
    [AS_HELP_STRING([--disable-thread-local-alloc],
        [turn off thread-local allocation optimization])],
    [case "$THREADS" in
      no | none | single)
        if test "${enable_thread_local_alloc}" = yes; then
          AC_MSG_ERROR(
                [Thread-local allocation requires --enable-threads=x spec])
        fi
        ;;
     esac])

AC_ARG_ENABLE(threads-discovery,
    [AS_HELP_STRING([--disable-threads-discovery],
                    [disable threads discovery in GC])])
if test "${enable_threads_discovery}" = no; then
    AC_DEFINE([GC_NO_THREADS_DISCOVERY], 1,
              [Disable threads discovery in GC.])
fi

AC_ARG_ENABLE(cplusplus,
    [AS_HELP_STRING([--enable-cplusplus], [install C++ support])])

dnl Features which may be selected in the following thread-detection switch.
AH_TEMPLATE([PARALLEL_MARK], [Define to enable parallel marking.])
AH_TEMPLATE([THREAD_LOCAL_ALLOC],
            [Define to enable thread-local allocation optimization.])
AH_TEMPLATE([USE_COMPILER_TLS],
            [Define to use of compiler-support for thread-local variables.])

dnl Thread selection macros.
AH_TEMPLATE([GC_THREADS],           [Define to support platform-specific
                                     threads.])
AH_TEMPLATE([GC_WIN32_PTHREADS],
                [Define to support pthreads-win32 or winpthreads.])

dnl System header feature requests.
AH_TEMPLATE([_POSIX_C_SOURCE], [The POSIX feature macro.])
AH_TEMPLATE([_PTHREADS], [Indicates the use of pthreads (NetBSD).])

dnl Win32-specific API usage controls.
AH_TEMPLATE([GC_UNDERSCORE_STDCALL],
        [Explicitly prefix exported/imported WINAPI symbols with '_'.])
AH_TEMPLATE([UNICODE],
        [Use Unicode (W) variant of Win32 API instead of ASCII (A) one.])

dnl GC API symbols export control.
AH_TEMPLATE([GC_DLL],
        [Define to build dynamic libraries with only API symbols exposed.])

dnl Check for a flavor of supported inline keyword.
old_CFLAGS="$CFLAGS"
CFLAGS="$CFLAGS $CFLAGS_EXTRA"
AC_C_INLINE
CFLAGS="$old_CFLAGS"

THREADDLLIBS=
need_atomic_ops_asm=false
need_lib_rt=false
compile_asm=false
use_parallel_mark=no
use_thread_local_alloc=no
# Libraries needed to support dynamic loading and/or threads.
case "$THREADS" in
 no | none | single)
    THREADS=none
    ;;
 posix | pthreads)
    THREADS=posix
    default_threadlibs=false
    # Common defines for most POSIX platforms.
    case "$host" in
      *-*-aix* | *-*-android* | *-*-cygwin* | *-*-darwin* | *-*-dragonfly* | \
      *-*-freebsd* | *-*-haiku* | *-*-hpux11* | *-*-irix* | \
      *-*-kfreebsd*-gnu | *-*-gnu* | *-*-*linux* | *-*-msys* | *-*-nacl* | \
      *-*-netbsd* | *-*-openbsd* | *-*-osf* | *-*-serenity* | *-*-solaris*)
        AC_DEFINE(GC_THREADS)
        AC_DEFINE([_REENTRANT], [1],
                  [Required define if using POSIX threads.])
        use_parallel_mark=$enable_parallel_mark
        use_thread_local_alloc=$enable_thread_local_alloc
        default_threadlibs=true
        AC_MSG_WARN("Explicit GC_INIT() calls may be required.")
        ;;
    esac
    AC_CHECK_LIB(pthread, pthread_self, THREADDLLIBS="-lpthread",,)
    case "$host" in
      *-*-hpux11*)
        AC_MSG_WARN("Only HP/UX 11 POSIX threads are supported.")
        AC_DEFINE(_POSIX_C_SOURCE,199506L)
        THREADDLLIBS="-lpthread"
        # HPUX needs REENTRANT for the _r calls.
        need_lib_rt=true
        ;;
      *-*-openbsd*)
        AM_CFLAGS="$AM_CFLAGS -pthread"
        THREADDLLIBS=-pthread
        ;;
      *-*-freebsd*)
        AM_CFLAGS="$AM_CFLAGS -pthread"
        ;;
      *-*-kfreebsd*-gnu)
        AM_CFLAGS="$AM_CFLAGS -pthread"
        THREADDLLIBS=-pthread
        ;;
      *-*-gnu*) # E.g. linux but excluding kfreebsd.
        # The default THREADDLLIBS.
        ;;
      *-*-netbsd*)
        AC_MSG_WARN("Only on NetBSD 2.0 or later.")
        AC_DEFINE(_PTHREADS)
        THREADDLLIBS="-lpthread"
        need_lib_rt=true
        ;;
      *-*-solaris*)
        THREADDLLIBS="-lpthread"
        need_lib_rt=true
        ;;
      *-*-cygwin* | *-*-msys*)
        # Cygwin doesn't have a real libpthread, so Libtool can't link
        # against it.
        THREADDLLIBS=""
        win32_threads=true
        ;;
      *-*-mingw*)
        AC_DEFINE(GC_WIN32_PTHREADS)
        # Using pthreads-win32 (or other non-Cygwin pthreads) library.
        THREADDLLIBS="-lpthread"
        use_parallel_mark=$enable_parallel_mark
        use_thread_local_alloc=$enable_thread_local_alloc
        win32_threads=true
        ;;
      *-*-darwin*)
        darwin_threads=true
        ;;
      *-*-osf*)
        AM_CFLAGS="$AM_CFLAGS -pthread"
        THREADDLLIBS="-lpthread"
        need_lib_rt=true
        ;;
      *)
        AS_IF([test x$default_threadlibs != xtrue],
              [ AC_MSG_ERROR(
                    [Pthreads not supported by the GC on this platform]) ])
        # The default THREADDLLIBS.
        ;;
    esac
    case "$host" in
      sparc*-*-solaris*)
        if test "$GCC" != yes; then
          need_atomic_ops_asm=true
        fi
        ;;
    esac
    ;;
 mcf | win32)
    AC_DEFINE(GC_THREADS)
    use_parallel_mark=$enable_parallel_mark
    if test "${enable_parallel_mark}" != no \
            -o "${enable_shared}" != yes -o "${enable_static}" != no; then
      # Imply THREAD_LOCAL_ALLOC unless GC_DLL.
      use_thread_local_alloc=$enable_thread_local_alloc
    fi
    if test "${enable_win32_dllmain}" = yes; then
      AC_DEFINE(GC_INSIDE_DLL, 1,
                [Enable Win32 DllMain-based approach of threads registering.])
    fi
    win32_threads=true
    AC_DEFINE([EMPTY_GETENV_RESULTS], [1],
              [Wine getenv may not return NULL for missing entry.])
    ;;
 dgux386)
    AC_DEFINE(GC_THREADS)
    # Use pthread GCC switch
    THREADDLLIBS=-pthread
    use_parallel_mark=$enable_parallel_mark
    use_thread_local_alloc=$enable_thread_local_alloc
    AC_MSG_WARN("Explicit GC_INIT() calls may be required.")
    AM_CFLAGS="-pthread $AM_CFLAGS"
    ;;
 aix)
    THREADS=posix
    THREADDLLIBS=-lpthread
    AC_DEFINE(GC_THREADS)
    AC_DEFINE(_REENTRANT)
    use_parallel_mark=$enable_parallel_mark
    use_thread_local_alloc=$enable_thread_local_alloc
    ;;
 rtems)
    THREADS=posix
    AC_DEFINE(GC_THREADS)
    use_parallel_mark=$enable_parallel_mark
    use_thread_local_alloc=$enable_thread_local_alloc
    ;;
 decosf1 | irix | mach | os2 | solaris | dce | vxworks)
    AC_MSG_ERROR(thread package $THREADS not yet supported)
    ;;
 *)
    AC_MSG_ERROR($THREADS is an unknown thread package)
    ;;
esac

# Check whether -lrt linker option is needed to use clock_gettime.
if test "x$need_lib_rt" != xtrue; then
  AC_MSG_CHECKING(for clock_gettime without additional libraries)
  AC_LINK_IFELSE([AC_LANG_PROGRAM([#include <time.h>],
                    [struct timespec t; clock_gettime(CLOCK_REALTIME, &t)])],
    [AC_MSG_RESULT(yes)],
    [AC_MSG_RESULT(no)
     AC_CHECK_LIB(rt, clock_gettime, [need_lib_rt=true])])
fi

if test "x$need_lib_rt" = xtrue; then
  THREADDLLIBS="$THREADDLLIBS -lrt"
fi
AC_SUBST(THREADDLLIBS)

AM_CONDITIONAL(THREADS, test x$THREADS != xnone)
AM_CONDITIONAL(PTHREADS, test x$THREADS = xposix)
AM_CONDITIONAL(DARWIN_THREADS, test x$darwin_threads = xtrue)
AM_CONDITIONAL(WIN32_THREADS, test x$win32_threads = xtrue)

compiler_suncc=no
pthread_start_standalone=no
case "$host" in
  *-*-*linux*)
      # Turn on the workaround described in pthread_start.c.
      AS_IF([test "$THREADS" = posix], [pthread_start_standalone=yes])
      ;;
  powerpc-*-darwin*)
      powerpc_darwin=true
      ;;
  *-*-solaris*)
      if test "$GCC" != yes; then
        # Solaris SunCC
        compiler_suncc=yes
        CFLAGS="-O $CFLAGS"
      fi
      ;;
  *-*-wince*)
      if test "$enable_gc_debug" != "no"; then
        AC_DEFINE([GC_READ_ENV_FILE], 1,
                  [Read environment variables from the GC 'env' file.])
      fi
      ;;
esac
AM_CONDITIONAL(PTHREAD_START_STANDALONE,
               test x$pthread_start_standalone = xyes)

if test "$GCC" = yes; then
  # Output all warnings.
  AC_MSG_CHECKING([whether compiler supports -Wextra])
  old_CFLAGS="$CFLAGS"
  CFLAGS="-Wextra $CFLAGS"
  AC_TRY_COMPILE([],[], [ac_cv_cc_wextra=yes], [ac_cv_cc_wextra=no])
  CFLAGS="$old_CFLAGS"
  AC_MSG_RESULT($ac_cv_cc_wextra)
  AS_IF([test "$ac_cv_cc_wextra" = yes], [WEXTRA="-Wextra"], [WEXTRA="-W"])
  AC_MSG_CHECKING([whether compiler supports -Wpedantic])
  CFLAGS="-Wpedantic -Wno-long-long $CFLAGS"
  AC_TRY_COMPILE([],[
    extern int quiet;
  ], [ac_cv_cc_pedantic=yes], [ac_cv_cc_pedantic=no])
  CFLAGS="$old_CFLAGS"
  AC_MSG_RESULT($ac_cv_cc_pedantic)
  WPEDANTIC=
  AS_IF([test "$ac_cv_cc_pedantic" = yes],
        [WPEDANTIC="-Wpedantic -Wno-long-long"])
  CFLAGS="-Wall $WEXTRA $WPEDANTIC $CFLAGS"
  CXXFLAGS="-Wall $WEXTRA $WPEDANTIC $CXXFLAGS"
fi

AC_MSG_CHECKING(for xlc)
AC_TRY_COMPILE([],[
 #ifndef __xlC__
 # error
 #endif
], [compiler_xlc=yes], [compiler_xlc=no])
AC_MSG_RESULT($compiler_xlc)
if test $compiler_xlc = yes -a "$powerpc_darwin" = true; then
  # the darwin stack-frame-walking code is completely broken on xlc
  AC_DEFINE([DARWIN_DONT_PARSE_STACK], 1, [See doc/README.macros.])
fi

# XLC neither requires nor tolerates the unnecessary assembler goop.
# Similar for the Sun C compiler.
AM_CONDITIONAL([ASM_WITH_CPP_UNSUPPORTED],
    [test $compiler_xlc = yes -o $compiler_suncc = yes])

if test "$GCC" = yes; then
  # Disable aliasing optimization unless forced to.
  AC_MSG_CHECKING([whether compiler supports -fno-strict-aliasing])
  ac_cv_fno_strict_aliasing=no
  for cflag in $CFLAGS; do
    case "$cflag" in
      -fstrict-aliasing)
        # Opposite option already present
        ac_cv_fno_strict_aliasing=skipped
        break
        ;;
    esac
  done
  if test "$ac_cv_fno_strict_aliasing" != skipped; then
    old_CFLAGS="$CFLAGS"
    CFLAGS="$CFLAGS -fno-strict-aliasing"
    AC_TRY_COMPILE([],[], [ac_cv_fno_strict_aliasing=yes], [])
    CFLAGS="$old_CFLAGS"
    AS_IF([test "$ac_cv_fno_strict_aliasing" = yes],
          [CFLAGS="$CFLAGS -fno-strict-aliasing"], [])
  fi
  AC_MSG_RESULT($ac_cv_fno_strict_aliasing)
fi

# Check for getcontext (uClibc can be configured without it, for example)
AC_CHECK_FUNC([getcontext], [],
    [ AC_DEFINE([NO_GETCONTEXT], [1], [Missing getcontext function.]) ])

# Check whether dl_iterate_phdr exists (as a strong symbol).
AC_CHECK_FUNCS([dl_iterate_phdr])

case "$host" in
# While IRIX 6 has libdl for the O32 and N32 ABIs, it's missing for N64
# and unnecessary everywhere.
  mips-sgi-irix6*) ;;
# We never want libdl on darwin. It is a fake libdl that just ends up making
# dyld calls anyway.  The same applies to Cygwin.
  *-*-cygwin* | *-*-darwin* | *-*-msys*)
    ;;
  *)
    AC_CHECK_LIB(dl, dlopen, THREADDLLIBS="$THREADDLLIBS -ldl")
    ;;
esac

case "$host" in
  *-*-hpux*)
    avoid_cpp_lib=yes;;
  *)
    avoid_cpp_lib=no;
    ;;
esac
AM_CONDITIONAL(AVOID_CPP_LIB,test $avoid_cpp_lib = yes)

# Check for various headers.
AC_CHECK_HEADER([execinfo.h], [],
    [ AC_DEFINE([GC_MISSING_EXECINFO_H], [1], [Missing execinfo.h header.]) ])

# extra LD Flags which are required for targets
case "${host}" in
  *-*-darwin*)
    extra_ldflags_libgc=-Wl,-single_module
    ;;
esac
AC_SUBST(extra_ldflags_libgc)

AC_SUBST(EXTRA_TEST_LIBS)

target_all=libgc.la
AC_SUBST(target_all)

dnl If the target is an eCos system, use the appropriate eCos
dnl I/O routines.
dnl FIXME: this should not be a local option but a global target
dnl system; at present there is no eCos target.
TARGET_ECOS="no"
AC_ARG_WITH(ecos,
[  --with-ecos             enable runtime eCos target support],
TARGET_ECOS="$with_ecos"
)

addobjs=
addlibs=
CXXLIBS=

case "$TARGET_ECOS" in
   no)
      ;;
   *)
      AC_DEFINE([ECOS], 1, [Define to enable eCos target support.])
      AM_CPPFLAGS="-I${TARGET_ECOS}/include $AM_CPPFLAGS"
      addobjs="$addobjs ecos.lo"
      ;;
esac

AM_CONDITIONAL(CPLUSPLUS, test "${enable_cplusplus}" = yes)

AC_ARG_ENABLE(throw-bad-alloc-library,
    [AS_HELP_STRING([--disable-throw-bad-alloc-library],
                    [do not build C++ gctba library])])
AM_CONDITIONAL(GC_TBA_LIBRARY,
               test "${enable_cplusplus}" = yes \
                    -a "${enable_throw_bad_alloc_library}" != no)

if test "$GCC" = yes; then
  if test "${enable_cplusplus}" = yes; then
    case "$host" in
      *-*-cygwin* | *-*-mingw* | *-*-msys*)
        AC_MSG_CHECKING([whether libsupc++ required])
        SUPC="`$CXX -print-file-name=libsupc++.a 2>/dev/null`"
        if test -n "$SUPC" -a "$SUPC" != "libsupc++.a"; then
          AC_MSG_RESULT(yes)
          CXXLIBS="-lsupc++"
        else
          AC_MSG_RESULT(no)
        fi
        ;;
    esac
  fi
fi

AC_SUBST(CXX)
AC_SUBST(AM_CFLAGS)
AC_SUBST(AM_CPPFLAGS)
AC_SUBST(CXXLIBS)

# Configuration of shared libraries
#
AC_MSG_CHECKING(whether to build shared libraries)
AC_ENABLE_SHARED

case "$host" in
 alpha-*-openbsd*)
     enable_shared=no
     ;;
 *)
     ;;
esac

AC_MSG_RESULT($enable_shared)

# Compile with GC_DLL defined unless building static libraries.
if test "${enable_shared}" != no -a "${enable_static}" != yes; then
    AC_DEFINE(GC_DLL)
    if test "$GCC" = yes; then
      # Pass -fvisibility=hidden option if supported
      AC_MSG_CHECKING([whether compiler supports -fvisibility])
      old_CFLAGS="$CFLAGS"
      CFLAGS="-Werror -fvisibility=hidden $CFLAGS"
      AC_TRY_COMPILE([],[], [ac_cv_fvisibility_hidden=yes],
                     [ac_cv_fvisibility_hidden=no])
      CFLAGS="$old_CFLAGS"
      AS_IF([test "$ac_cv_fvisibility_hidden" = yes],
            [CFLAGS="-DGC_VISIBILITY_HIDDEN_SET -fvisibility=hidden $CFLAGS"],
            [CFLAGS="-DGC_NO_VISIBILITY $CFLAGS"])
      AC_MSG_RESULT($ac_cv_fvisibility_hidden)
    fi
else

  case "$host" in
    *-*-cygwin* | *-*-mingw* | *-*-msys*)
      # Do not require the clients to link with "user32" system library.
      AC_DEFINE([DONT_USE_USER32_DLL], 1,
                [Do not use user32.dll import library (Win32).])
      # Use inline version of GC new and delete operators in test_cpp
      # otherwise the system ones might be used instead because of arbitrary
      # ordering of object files when linking.
      CXXFLAGS="$CXXFLAGS -DGC_NOT_DLL"
      ;;
  esac
fi

# Configuration of machine-dependent code
#
AC_MSG_CHECKING(which machine-dependent code should be used)
machdep=
case "$host" in
 alpha-*-openbsd*)
    if test x"${ac_cv_lib_dl_dlopen}" != xyes ; then
       AC_MSG_WARN(
         "OpenBSD/Alpha without dlopen(). Shared library support is disabled.")
    fi
    ;;
 i?86-*-solaris2.[[89]])
    # PROC_VDB appears to work in 2.8 and 2.9 but not in 2.10+ (for now).
    AC_DEFINE([SOLARIS25_PROC_VDB_BUG_FIXED], 1,
              [See the comment in gcconfig.h.])
    ;;
 sparc-*-netbsd*)
    machdep="sparc_netbsd_mach_dep.lo"
    compile_asm=true
    ;;
 sparc*-*-linux* | sparc*-*-openbsd* | sparc64-*-freebsd* | sparc64-*-netbsd*)
    machdep="sparc_mach_dep.lo"
    compile_asm=true
    ;;
 sparc-sun-solaris2.3)
    machdep="sparc_mach_dep.lo"
    compile_asm=true
    AC_DEFINE(SUNOS53_SHARED_LIB, 1,
              [Define to work around a Solaris 5.3 bug (see dyn_load.c).])
    ;;
 sparc*-sun-solaris*)
    machdep="sparc_mach_dep.lo"
    compile_asm=true
    ;;
 ia64-*-*)
    machdep="ia64_save_regs_in_stack.lo"
    ;;
esac
AC_MSG_RESULT($machdep)
addobjs="$addobjs $machdep"
AC_SUBST(addobjs)
AC_SUBST(addlibs)

AC_PROG_LIBTOOL

# Suppress "extension used" clang warning (when compiling .S files).
if test x$compile_asm = xtrue -a "$GCC" = yes; then
  AC_MSG_CHECKING([whether compiler supports -Wno-language-extension-token])
  old_CFLAGS="$CFLAGS"
  CFLAGS="$CFLAGS -Werror -Wno-language-extension-token"
  AC_COMPILE_IFELSE([AC_LANG_PROGRAM([], [])],
                    [ac_cv_lang_ext_token=yes], [ac_cv_lang_ext_token=no])
  CFLAGS="$old_CFLAGS"
  AS_IF([test "$ac_cv_lang_ext_token" = yes],
        [CFLAGS="$CFLAGS -Wno-language-extension-token"])
  AC_MSG_RESULT($ac_cv_lang_ext_token)
fi

dnl We use these options to decide which functions to include.
AC_ARG_WITH(target-subdir,
[  --with-target-subdir=SUBDIR
                          configuring with a cross compiler])
AC_ARG_WITH(cross-host,
[  --with-cross-host=HOST  configuring with a cross compiler])

dnl automake wants to see AC_EXEEXT.  But we don't need it.  And having
dnl it is actually a problem, because the compiler we're passed can't
dnl necessarily do a full link.  So we fool automake here.
if false; then
  dnl autoconf 2.50 runs AC_EXEEXT by default, and the macro expands
  dnl to nothing, so nothing would remain between `then' and `fi' if it
  dnl were not for the `:' below.
  :
  AC_EXEEXT
fi

dnl The collector might not properly work on IBM AIX when
dnl built with gcc and -O.  So we remove -O in the appropriate case.
AC_MSG_CHECKING(whether AIX gcc optimization fix is necessary)
case "$host" in
 *aix*)
    if test "$GCC" = yes; then
       AC_MSG_RESULT(yes)
       new_CFLAGS=
       for i in $CFLAGS; do
          case "$i" in
           -O*)
              ;;
           *)
              new_CFLAGS="$new_CFLAGS $i"
              ;;
          esac
       done
       CFLAGS="$new_CFLAGS"
    else
       AC_MSG_RESULT(no)
    fi
    ;;
 *) AC_MSG_RESULT(no) ;;
esac

dnl Include defines that have become de facto standard.
dnl ALL_INTERIOR_POINTERS and NO_EXECUTE_PERMISSION can be overridden
dnl in the startup code.
AC_DEFINE([NO_EXECUTE_PERMISSION], [1],
          [Define to make the collector not allocate executable memory
           by default.])
AC_DEFINE([ALL_INTERIOR_POINTERS], [1],
          [Define to recognise all pointers to the interior of objects.])


dnl Interface Selection
dnl -------------------
dnl
dnl By default, make the library as general as possible.
AC_ARG_ENABLE(gcj-support,
    [AS_HELP_STRING([--disable-gcj-support], [disable support for gcj])])
if test x"$enable_gcj_support" != xno; then
    AC_DEFINE(GC_GCJ_SUPPORT, 1, [Define to include support for gcj.])
    case "$host" in
      *-*-kfreebsd*-gnu)
        # FIXME: For a reason, gctest hangs up on kFreeBSD if both of
        # THREAD_LOCAL_ALLOC and GC_ENABLE_SUSPEND_THREAD are defined.
        if test "${enable_thread_local_alloc}" = no; then
          AC_DEFINE(GC_ENABLE_SUSPEND_THREAD)
        fi
        ;;
      *)
        AC_DEFINE([GC_ENABLE_SUSPEND_THREAD], 1,
              [Define to turn on GC_suspend_thread support.])
        ;;
    esac
fi
AM_CONDITIONAL(ENABLE_GCJ_SUPPORT, [test x"enable_gcj_support" != xno])

dnl Interaction with other programs that might use signals.
AC_ARG_ENABLE(sigrt-signals,
    [AS_HELP_STRING([--enable-sigrt-signals],
        [force GC to use SIGRTMIN-based signals for thread suspend/resume])])
if test x"${enable_sigrt_signals}" = xyes; then
    AC_DEFINE([GC_USESIGRT_SIGNALS], 1,
              [Force the GC to use signals based on SIGRTMIN+k.])
fi


dnl Debugging
dnl ---------

AH_TEMPLATE([GC_HAVE_BUILTIN_BACKTRACE],
            [Define if backtrace information is supported.])
AH_TEMPLATE([MAKE_BACK_GRAPH], [See doc/README.macros.])
AH_TEMPLATE([SAVE_CALL_COUNT],
            [The number of caller frames saved when allocating with the
             debugging API.])
UNWINDLIBS=
AC_ARG_ENABLE(gc-debug,
              [AS_HELP_STRING([--enable-gc-debug],
                    [include full support for pointer backtracing etc.])],
[ if test "$enable_gc_debug" = "yes"; then
    AC_MSG_WARN("Should define GC_DEBUG and use debug alloc in clients.")
    AC_DEFINE([KEEP_BACK_PTRS], 1,
              [Define to save back-pointers in debugging headers.])
    keep_back_ptrs=true
    AC_DEFINE([DBG_HDRS_ALL], 1,
              [Define to force debug headers on all objects.])
    AH_TEMPLATE([SHORT_DBG_HDRS],
                [Shorten the headers to minimize object size at the expense
                 of checking for writes past the end (see doc/README.macros).])

    case $host in
      ia64-*-linux* )
        AC_DEFINE(MAKE_BACK_GRAPH)
        AC_DEFINE(SAVE_CALL_COUNT, 8)
        AC_CHECK_LIB(unwind, backtrace, [
          AC_DEFINE(GC_HAVE_BUILTIN_BACKTRACE)
          UNWINDLIBS=-lunwind
          AC_MSG_WARN("Client code may need to link against libunwind.")
        ])
      ;;
      x86-*-linux* | i586-*-linux* | i686-*-linux* | x86_64-*-linux* )
        AC_DEFINE(MAKE_BACK_GRAPH)
        AC_MSG_WARN("Client must not use -fomit-frame-pointer.")
        AC_DEFINE(SAVE_CALL_COUNT, 8)
      ;;
      i[3456]86-*-dgux*)
        AC_DEFINE(MAKE_BACK_GRAPH)
      ;;
    esac
  fi ])
AM_CONDITIONAL([MAKE_BACK_GRAPH], [test x"$enable_gc_debug" = xyes])
AM_CONDITIONAL([KEEP_BACK_PTRS], [test x"$keep_back_ptrs" = xtrue])

# Check whether a compiler warning of unsafe __builtin_return_address(1)
# could be suppressed by -Wno-frame-address option.
# __builtin_return_address(1) is used by libgc for debugging purposes only.
AC_MSG_CHECKING([whether -Wno-frame-address works])
use_wno_error_frame_address=no
old_CFLAGS="$CFLAGS"
CFLAGS="$CFLAGS -Werror -Wno-frame-address $CFLAGS_EXTRA"
AC_TRY_COMPILE([], [{
    if (!__builtin_return_address(1)) return 1;
}], [ use_wno_error_frame_address=yes ])
CFLAGS="$old_CFLAGS"
AC_MSG_RESULT($use_wno_error_frame_address)
if test x"$use_wno_error_frame_address" = xyes; then
  CFLAGS="$CFLAGS -Wno-frame-address"
fi

# Check for dladdr (used for debugging).
AC_MSG_CHECKING(for dladdr)
have_dladdr=no
old_CFLAGS="$CFLAGS"
CFLAGS="$CFLAGS $CFLAGS_EXTRA"
AC_TRY_COMPILE([
#define _GNU_SOURCE 1
#include <dlfcn.h>], [{
    Dl_info info;
    (void)dladdr("", &info);
}], [ have_dladdr=yes ])
CFLAGS="$old_CFLAGS"
AC_MSG_RESULT($have_dladdr)
if test x"$have_dladdr" = xyes; then
  AC_DEFINE([HAVE_DLADDR], 1, [Define to use 'dladdr' function.])
fi

# sigsetjmp could be a macro (thus AC_CHECK_FUNCS cannot be used).
AC_MSG_CHECKING(for sigsetjmp)
old_CFLAGS="$CFLAGS"
CFLAGS="$CFLAGS $CFLAGS_EXTRA"
AC_TRY_LINK([#include <setjmp.h>],
            [sigjmp_buf t; sigsetjmp(t, 0)],
  [AC_MSG_RESULT(yes)],
  [AC_MSG_RESULT(no)
   AC_DEFINE([GC_NO_SIGSETJMP], [1], [Missing sigsetjmp function.])])
CFLAGS="$old_CFLAGS"

# pthread_setname_np, if available, may have 1, 2 or 3 arguments.
AS_IF([test "$THREADS" = posix],
  [AC_MSG_CHECKING(for pthread_setname_np)
   old_CFLAGS="$CFLAGS"
   CFLAGS="$CFLAGS $CFLAGS_EXTRA -Werror"
   AC_TRY_COMPILE([
#         define _GNU_SOURCE 1
#         include <pthread.h>
        ], [pthread_setname_np("thread-name")],
     [AC_MSG_RESULT([yes (w/o tid)])
      AC_DEFINE([HAVE_PTHREAD_SETNAME_NP_WITHOUT_TID], [1],
                [Define to use 'pthread_setname_np(const char*)' function.])],
     [AC_TRY_COMPILE([
#         define _GNU_SOURCE 1
#         include <pthread.h>
        ], [pthread_setname_np(pthread_self(), "thread-name-%u", 0)],
       [AC_MSG_RESULT([yes (with tid and arg)])
        AC_DEFINE([HAVE_PTHREAD_SETNAME_NP_WITH_TID_AND_ARG], [1],
         [Define to use 'pthread_setname_np(pthread_t, const char*, void *)'
          function.])],
       [AC_TRY_COMPILE([
#             define _GNU_SOURCE 1
#             include <pthread.h>
            ], [pthread_setname_np(pthread_self(), "thread-name")],
         [AC_MSG_RESULT([yes (with tid)])
          AC_DEFINE([HAVE_PTHREAD_SETNAME_NP_WITH_TID], [1],
                    [Define to use 'pthread_setname_np(pthread_t, const char*)'
                     function.])],
         [AC_MSG_RESULT(no)])])])
   CFLAGS="$old_CFLAGS"])

# Check for AViiON Machines running DGUX
ac_is_dgux=no
AC_CHECK_HEADER(sys/dg_sys_info.h,
[ac_is_dgux=yes;])

dnl :GOTCHA: we do not check anything but sys/dg_sys_info.h
if test $ac_is_dgux = yes; then
    dgux_spec_opts="-DDGUX -D_DGUX_SOURCE -Di386 -mno-legend -O2"
    CFLAGS="$dgux_spec_opts $CFLAGS"
    CXXFLAGS="$dgux_spec_opts $CXXFLAGS"
    if test "$enable_gc_debug" = "yes"; then
      CFLAGS="-g -mstandard $CFLAGS"
      CXXFLAGS="-g -mstandard $CXXFLAGS"
    fi
    AC_SUBST(CFLAGS)
    AC_SUBST(CXXFLAGS)
fi

AC_ARG_ENABLE(java-finalization,
    [AS_HELP_STRING([--disable-java-finalization],
                    [disable support for java finalization])])
if test x"$enable_java_finalization" != xno; then
    AC_DEFINE([JAVA_FINALIZATION], 1, [See doc/README.macros.])
fi

AC_ARG_ENABLE(atomic-uncollectable,
    [AS_HELP_STRING([--disable-atomic-uncollectible],
                    [disable support for atomic uncollectible allocation])])
if test x"$enable_atomic_uncollectible" != x"no"; then
    AC_DEFINE([GC_ATOMIC_UNCOLLECTABLE], 1,
        [Define to enable atomic uncollectible allocation.])
fi

AC_ARG_ENABLE(redirect-malloc,
    [AS_HELP_STRING([--enable-redirect-malloc],
                    [redirect malloc and friends to GC routines])])

if test "${enable_redirect_malloc}" = yes; then
    if test "${enable_gc_debug}" = yes; then
        AC_DEFINE([REDIRECT_MALLOC], GC_debug_malloc_replacement,
                  [If defined, redirect malloc to this function.])
        AC_DEFINE([REDIRECT_REALLOC], GC_debug_realloc_replacement,
                  [If defined, redirect realloc to this function.])
        AC_DEFINE([REDIRECT_FREE], GC_debug_free,
                  [If defined, redirect free to this function.])
    else
        AC_DEFINE(REDIRECT_MALLOC, GC_malloc)
    fi
    AC_DEFINE([GC_USE_DLOPEN_WRAP], 1, [See doc/README.macros.])
fi

AC_ARG_ENABLE(disclaim,
    [AS_HELP_STRING([--disable-disclaim],
        [disable alternative (more efficient) finalization interface])])
if test x"$enable_disclaim" != xno; then
    AC_DEFINE(ENABLE_DISCLAIM, 1,
        [Define to enable alternative finalization interface.])
fi
AM_CONDITIONAL(ENABLE_DISCLAIM,
    [test x"$enable_disclaim" != xno])

AC_ARG_ENABLE(large-config,
    [AS_HELP_STRING([--enable-large-config],
        [optimize for large (> 100 MB) heap or root set])])
if test "${enable_large_config}" = yes; then
    AC_DEFINE(LARGE_CONFIG, 1,
              [Define to optimize for large heaps or root sets.])
fi

dnl This is something of a hack.  When cross-compiling we turn off
dnl some functionality.  We also enable the "small" configuration.
dnl These is only correct when targeting an embedded system.  FIXME.
if test -n "${with_cross_host}"; then
   AC_DEFINE([NO_CLOCK], 1, [Define to not use system clock (cross compiling).])
   AC_DEFINE([SMALL_CONFIG], 1,
             [Define to tune the collector for small heap sizes.])
fi

if test "$enable_gc_debug" = "no"; then
   AC_DEFINE([NO_DEBUGGING], 1,
             [Disable debugging, like GC_dump and its callees.])
fi

AC_SUBST(UNWINDLIBS)

AC_ARG_ENABLE(gc-assertions,
    [AS_HELP_STRING([--enable-gc-assertions],
        [collector-internal assertion checking])])
if test "${enable_gc_assertions}" = yes; then
    AC_DEFINE([GC_ASSERTIONS], 1,
              [Define to enable internal debug assertions.])
fi

AC_ARG_ENABLE(mmap,
    [AS_HELP_STRING([--enable-mmap],
                    [use mmap instead of sbrk to expand the heap])],
    gc_use_mmap=$enableval)

AC_ARG_ENABLE(munmap,
    [AS_HELP_STRING([--enable-munmap=N],
                    [return page to the OS if empty for N collections
                     (default: 6)])],
    MUNMAP_THRESHOLD=$enableval)
if test x$enable_munmap != xno; then
    AC_DEFINE([USE_MMAP], 1,
              [Define to use mmap instead of sbrk to expand the heap.])
    AH_TEMPLATE([USE_WINALLOC],
                  [Define to use Win32 VirtualAlloc (instead of sbrk or
                   mmap) to expand the heap.])
    AC_DEFINE([USE_MUNMAP], 1,
              [Define to return memory to OS with munmap calls
               (see doc/README.macros).])
    if test x$MUNMAP_THRESHOLD = x -o x$MUNMAP_THRESHOLD = xyes; then
      MUNMAP_THRESHOLD=6
    fi
    AC_DEFINE_UNQUOTED([MUNMAP_THRESHOLD], [${MUNMAP_THRESHOLD}],
        [Number of GC cycles to wait before unmapping an unused block.])
else
    if test "${gc_use_mmap}" = "yes"; then
      AC_DEFINE([USE_MMAP], 1,
                [Define to use mmap instead of sbrk to expand the heap.])
    fi
fi

AC_ARG_ENABLE(dynamic-loading,
    [AS_HELP_STRING([--disable-dynamic-loading],
                    [build the collector with disabled tracing
                     of dynamic library data roots])])
if test "${enable_dynamic_loading}" = "no"; then
  AC_DEFINE([IGNORE_DYNAMIC_LOADING], 1,
            [Do not define DYNAMIC_LOADING even if supported (i.e., build the
             collector with disabled tracing of dynamic library data roots).])
fi

AC_ARG_ENABLE(register-main-static-data,
    [AS_HELP_STRING([--disable-register-main-static-data],
                    [skip the initial guess of data root sets])])
if test "${enable_register_main_static_data}" = "no"; then
    AC_DEFINE([GC_DONT_REGISTER_MAIN_STATIC_DATA], 1,
              [Skip the initial guess of data root sets.])
fi

AC_ARG_ENABLE(checksums,
    [AS_HELP_STRING([--enable-checksums],
                    [report on erroneously cleared dirty bits at
                     substantial performance cost; use only for
                     debugging of the incremental collector])])
if test x$enable_checksums = xyes; then
    if test x$enable_munmap != xno -o x$THREADS != xnone; then
        AC_MSG_ERROR([CHECKSUMS not compatible with USE_MUNMAP or threads])
    fi
    AC_DEFINE([CHECKSUMS], 1,
              [Erroneously cleared dirty bits checking.  Use only for
               debugging of the incremental collector.])
fi
AM_CONDITIONAL([CHECKSUMS], test x$enable_checksums = xyes)

AM_CONDITIONAL(USE_LIBDIR, test -z "$with_cross_host")

AC_ARG_ENABLE(werror,
    [AS_HELP_STRING([--enable-werror], [pass -Werror to the C compiler])],
    werror_flag=$enableval, werror_flag=no)
if test x$werror_flag = xyes; then
    WERROR_CFLAGS="-Werror"
fi
AC_SUBST([WERROR_CFLAGS])

AC_ARG_ENABLE(single-obj-compilation,
    [AS_HELP_STRING([--enable-single-obj-compilation],
                    [compile all libgc source files into single .o
                     (default: yes if static libraries are disabled)])],
    [], [ AS_IF([test x"$enable_static" = xno],
                [enable_single_obj_compilation=yes]) ])
AM_CONDITIONAL([SINGLE_GC_OBJ],
               [test x"$enable_single_obj_compilation" = xyes])

AC_ARG_ENABLE(gcov,
    [AS_HELP_STRING([--enable-gcov], [turn on code coverage analysis])])
if test "$enable_gcov" = "yes"; then
  CFLAGS="-D NTHREADS=20 $CFLAGS --coverage"
  if test "${enable_shared}" = no; then
    # FIXME: As of g++-4.8.4/x64, in case of shared library build, test_cpp
    # linkage fails with "hidden symbol atexit is referenced by DSO" message.
    CXXFLAGS="$CXXFLAGS --coverage"
  fi
  # Turn off optimization to get accurate line numbers.
  CFLAGS=`echo "$CFLAGS" | sed -e 's/-O\(1\|2\|3\|4\|s\|fast\)\?//g'`
  CXXFLAGS=`echo "$CXXFLAGS" | sed -e 's/-O\(1\|2\|3\|4\|s\|fast\)\?//g'`
fi

AC_ARG_ENABLE(docs,
        [AS_HELP_STRING([--disable-docs],
                        [do not build and install documentation])])
AM_CONDITIONAL(ENABLE_DOCS, test x$enable_docs != xno)

AM_CONDITIONAL(ENABLE_SHARED, test x$enable_shared = xyes)

# Atomic_ops
# ----------

# Do we want to use an external libatomic_ops?  By default use it if it's
# found.
AC_ARG_WITH([libatomic-ops],
    [AS_HELP_STRING([--with-libatomic-ops[=yes|no|check|none]],
                    [use an external libatomic_ops? (default: check;
                     none: use compiler intrinsics or no thread support)])],
    [], [ AS_IF([test x"$THREADS" != xnone],
                [with_libatomic_ops=check], [with_libatomic_ops=none]) ])

# Check whether compiler atomic intrinsics can be used.
if test x"$with_libatomic_ops" = xcheck; then
  AC_MSG_CHECKING(for compiler intrinsics support)
  old_CFLAGS="$CFLAGS"
  CFLAGS="$CFLAGS $CFLAGS_EXTRA -DGC_BUILTIN_ATOMIC"
  CFLAGS="$CFLAGS -I${srcdir}/include -I${srcdir}/tests"
  AC_TRY_RUN([#include "test_atomic_ops.c"],
    [AC_MSG_RESULT(yes)
     with_libatomic_ops=none],
    [AC_MSG_RESULT(no)], [AC_MSG_RESULT(skipped because cross-compiling)])
  CFLAGS="$old_CFLAGS"
fi

# Check for an external libatomic_ops if the above answer is "yes" or "check".
# If not found, fail on "yes", and convert "check" to "no".
# First, check that libatomic_ops usage is not disabled explicitly.
missing_libatomic_ops=false
AS_IF([test x"$with_libatomic_ops" != xno -a x"$with_libatomic_ops" != xnone],
  [ missing_libatomic_ops=true ])

dnl To avoid "syntax error near unexpected token ATOMIC_OPS" configure error
dnl observed by some clients, the following 3 code lines are commented out:
dnl
dnl AS_IF([test x$missing_libatomic_ops = xtrue],
dnl  [ PKG_CHECK_MODULES([ATOMIC_OPS], [atomic_ops],
dnl    [ missing_libatomic_ops=false ], [ [] ]) ])

dnl Retry with AC_CHECK_HEADER if PKG_CHECK_MODULES failed.
AS_IF([test x$missing_libatomic_ops = xtrue],
      [ AC_CHECK_HEADER([atomic_ops.h], [missing_libatomic_ops=false]) ])
AS_IF([test x$missing_libatomic_ops = xtrue],
      [ AS_IF([test x"$with_libatomic_ops" != xcheck],
              [ AC_MSG_ERROR([An external libatomic_ops was not found]) ])
        with_libatomic_ops=no ])

# If we have neither an external or an internal version, offer a useful hint
# and exit.
AS_IF([test x"$with_libatomic_ops" = xno \
            -a ! -e "$srcdir/libatomic_ops/src/atomic_ops.h"],
  [ AC_MSG_ERROR([libatomic_ops is required.  You can either install it on
                  your system, or fetch and unpack a recent version into the
                  source directory and link or rename it to libatomic_ops.]) ])

# Finally, emit the definitions for bundled or external AO.
AC_MSG_CHECKING([which libatomic_ops to use])
AS_IF([test x"$with_libatomic_ops" != xno],
  [ AS_IF([test x"$with_libatomic_ops" != xnone -a x"$THREADS" != xnone],
          [ AC_MSG_RESULT([external])
            ATOMIC_OPS_LIBS="-latomic_ops"
            AC_SUBST([ATOMIC_OPS_LIBS]) ],
          [ AC_MSG_RESULT([none])
            AS_IF([test x"$THREADS" != xnone],
                  [ AC_DEFINE([GC_BUILTIN_ATOMIC], [1],
                              [Use GCC atomic intrinsics instead of
                               libatomic_ops primitives.]) ]) ])
    AO_TRYLINK_CFLAGS="" ],
  [ AC_MSG_RESULT([internal])
    AO_TRYLINK_CFLAGS="-I${srcdir}/libatomic_ops/src"
    ATOMIC_OPS_CFLAGS='-I$(top_builddir)/libatomic_ops/src -I$(top_srcdir)/libatomic_ops/src'
    ATOMIC_OPS_LIBS=""
    AC_SUBST([ATOMIC_OPS_CFLAGS])
    AC_CONFIG_SUBDIRS([libatomic_ops])
  ])
AM_CONDITIONAL([USE_INTERNAL_LIBATOMIC_OPS],
    [test x$with_libatomic_ops = xno -a x"$THREADS" != xnone])
AM_CONDITIONAL([NEED_ATOMIC_OPS_ASM],
    [test x$with_libatomic_ops = xno -a x$need_atomic_ops_asm = xtrue])

# Check whether particular AO primitives are emulated with locks.
# The check below is based on the fact that linking with the libatomic_ops
# binary file is not needed in case of absence of the emulation (except for
# Solaris SPARC).
AS_IF([test x$with_libatomic_ops != xnone -a x$need_atomic_ops_asm != xtrue],
    [ old_CFLAGS="$CFLAGS"
      CFLAGS="$CFLAGS $AO_TRYLINK_CFLAGS $CFLAGS_EXTRA"
      AC_MSG_CHECKING([for lock-free AO_or primitive])
      AC_TRY_LINK([#include "atomic_ops.h"],
                  [AO_t x=0;AO_or(&x,1)],
        [ AC_MSG_RESULT(yes)
          AC_DEFINE([HAVE_LOCKFREE_AO_OR], [1],
            [libatomic_ops AO_or primitive implementation is lock-free.]) ],
        [ AC_MSG_RESULT(no) ])
      AC_MSG_CHECKING([for lock-free AO load/store, test-and-set primitives])
      AC_TRY_LINK([#include "atomic_ops.h"],
 [AO_t x=0;unsigned char c=0;AO_TS_t z=AO_TS_INITIALIZER;
  (void)AO_test_and_set_acquire(&z);AO_CLEAR(&z);AO_compiler_barrier();
  AO_store(&x,AO_load(&x)+1);AO_char_store(&c,AO_char_load(&c)+1);
  AO_store_release(&x,AO_load_acquire(&x)+1)],
        [ AC_MSG_RESULT(yes) ],
        [ AC_MSG_RESULT(no)
          use_thread_local_alloc=no
          AC_DEFINE([BASE_ATOMIC_OPS_EMULATED], [1],
                    [AO load, store and/or test-and-set primitives are
                     implemented in libatomic_ops using locks.]) ])
      AS_IF([test x$use_parallel_mark != xno],
        [ AC_MSG_CHECKING(
                [for lock-free compare-and-swap and fetch-and-add primitives])
          AC_TRY_LINK(
 [#define AO_REQUIRE_CAS
  #include "atomic_ops.h"],
 [AO_t x=0;(void)AO_fetch_and_add(&x,1);(void)AO_compare_and_swap(&x,1,2)],
                    [ AC_MSG_RESULT(yes) ],
                    [ AC_MSG_RESULT(no)
                      use_parallel_mark=no ]) ])
      CFLAGS="$old_CFLAGS" ])

AS_IF([test x$use_parallel_mark != xno],
      [ AC_DEFINE(PARALLEL_MARK) ])
AS_IF([test x$use_thread_local_alloc != xno],
      [ AC_DEFINE(THREAD_LOCAL_ALLOC) ])
AM_CONDITIONAL(THREAD_LOCAL_ALLOC, test x$use_thread_local_alloc != xno)

AC_ARG_ENABLE(handle-fork,
    [AS_HELP_STRING([--enable-handle-fork[=yes|no|auto|manual]],
                    [attempt to ensure a usable collector after fork()
                     in multi-threaded programs (default: auto;
                     manual: GC_atfork_prepare/parent/child should be
                     called by the client)])])
if test "${enable_handle_fork}" = yes; then
    AC_DEFINE(HANDLE_FORK, 1,
              [Define to install pthread_atfork() handlers by default.])
elif test "${enable_handle_fork}" = no; then
    AC_DEFINE(NO_HANDLE_FORK, 1,
              [Prohibit installation of pthread_atfork() handlers.])
elif test "${enable_handle_fork}" != manual -a x$THREADS = xposix; then
    # If the option is omitted, pthread_atfork handlers are installed
    # by default for the targets where pthread_atfork is known to work.
    case "$host" in
      *-*-aix* | *-*-android* | *-*-cygwin* | *-*-darwin* | *-*-freebsd* | \
      *-*-haiku* | *-*-hpux11* | *-*-irix* | *-*-kfreebsd*-gnu | \
      *-*-*linux* | *-*-netbsd* | *-*-openbsd* | *-*-osf* | *-*-solaris*)
        AC_DEFINE(HANDLE_FORK)
        ;;
    esac
fi

dnl Produce the Files
dnl -----------------

AC_CONFIG_FILES([Makefile bdw-gc.pc])

AC_CONFIG_COMMANDS([default],,
  [ srcdir="${srcdir}"
    host=${host}
    CONFIG_SHELL=${CONFIG_SHELL-/bin/sh}
    CC="${CC}"
    DEFS="$DEFS" ])

AC_OUTPUT
