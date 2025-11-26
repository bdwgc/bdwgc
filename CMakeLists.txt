#
# Copyright (c) 1994 by Xerox Corporation.  All rights reserved.
# Copyright (c) 1996 by Silicon Graphics.  All rights reserved.
# Copyright (c) 1998 by Fergus Henderson.  All rights reserved.
# Copyright (c) 2000-2010 by Hewlett-Packard Company.  All rights reserved.
# Copyright (c) 2010-2025 Ivan Maidanski
##
# THIS MATERIAL IS PROVIDED AS IS, WITH ABSOLUTELY NO WARRANTY EXPRESSED
# OR IMPLIED.  ANY USE IS AT YOUR OWN RISK.
##
# Permission is hereby granted to use or copy this program
# for any purpose, provided the above notices are retained on all copies.
# Permission to modify the code and to distribute modified code is granted,
# provided the above notices are retained, and a notice that the code was
# modified is included with the above copyright notice.
##

cmake_minimum_required(VERSION 3.10)

set(PACKAGE_VERSION 8.3.0)
# Version must match that in `AC_INIT` of `configure.ac` file and in README.
# Version must conform to: [0-9]+[.][0-9]+[.][0-9]+

# Info (`current:revision:age`) for the Libtool versioning system.
# These values should match those in `cord/cord.am` and `Makefile.am` files.
set(LIBCORD_VER_INFO    6:1:5)
set(LIBGC_VER_INFO      6:3:5)
set(LIBGCCPP_VER_INFO   6:0:5)

# To be removed in a later version and replaced with a vanilla
# cmake option call with bdgwc_ prefixed option names.
function(bdwgc_option NAME DESCRIPTION DEFAULT_VALUE)
    if(DEFINED ${NAME})
        message(WARNING "The option ${NAME} has been renamed to bdwgc_${NAME} "
                "and will be removed in a later version")
        get_property(VALUE CACHE ${NAME} PROPERTY VALUE)
        set(DEFAULT_VALUE ${VALUE})
    endif()
    option(bdwgc_${NAME} ${DESCRIPTION} ${DEFAULT_VALUE})
endfunction()

bdwgc_option(enable_cplusplus "C++ support" OFF)
if (bdwgc_enable_cplusplus)
  project(gc)
else()
  project(gc C)
endif()

if (POLICY CMP0057)
  # Required for `CheckLinkerFlag`, at least.
  cmake_policy(SET CMP0057 NEW)
endif()

include(CheckCCompilerFlag)
include(CheckCSourceCompiles)
include(CheckFunctionExists)
include(CheckIncludeFile)
include(CheckSymbolExists)
include(CMakePackageConfigHelpers)
include(CTest)
include(GNUInstallDirs)

if (NOT (${CMAKE_VERSION} VERSION_LESS "3.18.0"))
  include(CheckLinkerFlag)
endif()

set(default_enable_threads ON)
find_package(Threads QUIET)
if (NOT (CMAKE_USE_PTHREADS_INIT OR CMAKE_USE_WIN32_THREADS_INIT) OR EMSCRIPTEN OR WASI)
  set(default_enable_threads OFF)
endif()

# Customize the build by passing "-D<option_name>=ON|OFF" in the command line.
bdwgc_option(build_shared_libs "Build shared libraries" ON)
bdwgc_option(build_cord "Build cord library" ON)
bdwgc_option(build_tests "Build tests" OFF)
bdwgc_option(enable_docs "Build and install documentation" ON)
bdwgc_option(enable_threads "Support threads" ${default_enable_threads})
bdwgc_option(enable_parallel_mark "Parallelize marking and free list construction" ON)
bdwgc_option(enable_thread_local_alloc "Turn on thread-local allocation optimization" ON)
bdwgc_option(enable_threads_discovery "Enable threads discovery in GC" ON)
bdwgc_option(enable_rwlock "Enable reader mode of the allocator lock" OFF)
bdwgc_option(enable_throw_bad_alloc_library "Turn on C++ gctba library build" ON)
bdwgc_option(enable_gcj_support "Support for gcj" ON)
bdwgc_option(enable_sigrt_signals "Use SIGRTMIN-based signals for thread suspend/resume" OFF)
bdwgc_option(enable_valgrind_tracking "Support tracking GC_malloc and friends for heap profiling tools" OFF)
bdwgc_option(enable_gc_debug "Support for pointer back-tracing" OFF)
bdwgc_option(disable_gc_debug "Disable debugging like GC_dump and its callees" OFF)
bdwgc_option(enable_java_finalization "Support for java finalization" ON)
bdwgc_option(enable_atomic_uncollectable "Support for atomic uncollectible allocation" ON)
bdwgc_option(enable_redirect_malloc "Redirect malloc and friends to GC routines" OFF)
bdwgc_option(enable_disclaim "Support alternative finalization interface" ON)
bdwgc_option(enable_dynamic_pointer_mask "Support pointer mask/shift set at runtime" OFF)
bdwgc_option(enable_large_config "Optimize for large heap or root set" OFF)
bdwgc_option(enable_gc_assertions "Enable collector-internal assertion checking" OFF)
bdwgc_option(enable_mmap "Use mmap instead of sbrk to expand the heap" OFF)
bdwgc_option(enable_munmap "Return page to the OS if empty for N collections" ON)
bdwgc_option(enable_dynamic_loading "Enable tracing of dynamic library data roots" ON)
bdwgc_option(enable_register_main_static_data "Perform the initial guess of data root sets" ON)
bdwgc_option(enable_checksums "Report erroneously cleared dirty bits" OFF)
bdwgc_option(enable_werror "Pass -Werror to the C compiler (treat warnings as errors)" OFF)
bdwgc_option(enable_single_obj_compilation "Compile all libgc source files into single .o" OFF)
bdwgc_option(disable_single_obj_compilation "Compile each libgc source file independently" OFF)
bdwgc_option(enable_handle_fork "Attempt to ensure a usable collector after fork()" ON)
bdwgc_option(disable_handle_fork "Prohibit installation of pthread_atfork() handlers" OFF)
bdwgc_option(enable_emscripten_asyncify "Use Emscripten asyncify feature" OFF)
bdwgc_option(install_headers "Install header and pkg-config metadata files" ON)
bdwgc_option(with_libatomic_ops "Use an external libatomic_ops" OFF)
bdwgc_option(without_libatomic_ops "Use atomic_ops.h in libatomic_ops/src" OFF)

if(BUILD_SHARED_LIBS)
    set(bdwgc_build_shared_libs ON CACHE BOOL "" FORCE)
endif()

# Override the default build type to `RelWithDebInfo` (this instructs `cmake`
# to pass `-O2 -g -DNDEBUG` options to the compiler by default).
if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE "RelWithDebInfo" CACHE
      STRING "Choose the type of build." FORCE)
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY
               STRINGS "Debug" "Release" "RelWithDebInfo" "MinSizeRel")
endif()

# Convert `VER_INFO` values to `VERSION`, `SOVERSION` ones.
if (bdwgc_build_shared_libs)
  # `cord`:
  string(REGEX REPLACE "(.+):.+:.+"  "\\1" cord_cur ${LIBCORD_VER_INFO})
  string(REGEX REPLACE ".+:(.+):.+"  "\\1" cord_rev ${LIBCORD_VER_INFO})
  string(REGEX REPLACE ".+:.+:(.+)$" "\\1" cord_age ${LIBCORD_VER_INFO})
  math(EXPR CORD_SOVERSION "${cord_cur} - ${cord_age}")
  set(CORD_VERSION_PROP "${CORD_SOVERSION}.${cord_age}.${cord_rev}")
  message(STATUS "CORD_VERSION_PROP = ${CORD_VERSION_PROP}")
  # `gc`:
  string(REGEX REPLACE "(.+):.+:.+"  "\\1" gc_cur ${LIBGC_VER_INFO})
  string(REGEX REPLACE ".+:(.+):.+"  "\\1" gc_rev ${LIBGC_VER_INFO})
  string(REGEX REPLACE ".+:.+:(.+)$" "\\1" gc_age ${LIBGC_VER_INFO})
  math(EXPR GC_SOVERSION "${gc_cur} - ${gc_age}")
  set(GC_VERSION_PROP "${GC_SOVERSION}.${gc_age}.${gc_rev}")
  message(STATUS "GC_VERSION_PROP = ${GC_VERSION_PROP}")
  # `gccpp` and `gctba`:
  string(REGEX REPLACE "(.+):.+:.+"  "\\1" gccpp_cur ${LIBGCCPP_VER_INFO})
  string(REGEX REPLACE ".+:(.+):.+"  "\\1" gccpp_rev ${LIBGCCPP_VER_INFO})
  string(REGEX REPLACE ".+:.+:(.+)$" "\\1" gccpp_age ${LIBGCCPP_VER_INFO})
  math(EXPR GCCPP_SOVERSION "${gccpp_cur} - ${gccpp_age}")
  set(GCCPP_VERSION_PROP "${GCCPP_SOVERSION}.${gccpp_age}.${gccpp_rev}")
  message(STATUS "GCCPP_VERSION_PROP = ${GCCPP_VERSION_PROP}")
endif(bdwgc_build_shared_libs)

add_definitions("-DALL_INTERIOR_POINTERS")
add_definitions("-DNO_EXECUTE_PERMISSION")

# Set struct packing alignment to word (instead of 1-byte).
if (BORLAND)
  add_compile_options(/a4)
elseif (WATCOM)
  add_compile_options(/zp4)
endif()

# Output all warnings.
if (BORLAND)
  # All warnings except for particular ones.
  add_compile_options(/w /w-aus /w-ccc /w-inl /w-pro /w-rch /w-use)
elseif (MSVC)
  # All warnings but ignoring "conditional expression is constant" one.
  add_compile_options(/W4 /wd4127)
elseif (WATCOM)
  add_compile_options(/wx)
  if (bdwgc_enable_gc_assertions)
    # Suppress "unreachable code" warning in `GC_ASSERT()` if some constant
    # nonzero expression is given as the argument.
    add_compile_options(/wcd=201)
  endif()
  if (bdwgc_enable_threads)
    # Suppress "missing return value" wcc warning for `AO_test_and_set_full()`
    # and `AO_char_fetch_and_add_full()` in AO `msftc/x86.h` file.
    add_compile_options($<$<COMPILE_LANGUAGE:C>:/wcd=107>)
  endif(bdwgc_enable_threads)
else()
  add_compile_options(-Wall)
  add_compile_options(-Wextra)
  # TODO: add `-[W]pedantic -Wno-long-long`
endif()

if (WIN32)
  # Disable MS crt security warnings reported e.g. for `getenv`, `strcpy`.
  add_definitions("-D_CRT_SECURE_NO_DEPRECATE")
endif()

include_directories(include)

set(SRC allchblk.c alloc.c blacklst.c dbg_mlc.c dyn_load.c finalize.c
        headers.c mach_dep.c malloc.c mallocx.c mark.c mark_rts.c misc.c
        new_hblk.c os_dep.c ptr_chck.c reclaim.c typd_mlc.c)

set(NODIST_SRC)
set(ATOMIC_OPS_LIBS)
set(ATOMIC_OPS_LIBS_CMAKE)
set(THREADDLLIBS_LIST)
set(NEED_LIB_RT)

set(_HOST ${CMAKE_SYSTEM_PROCESSOR}-unknown-${CMAKE_SYSTEM})
string(TOLOWER ${_HOST} HOST)
message(STATUS "TARGET = ${HOST}")

if (bdwgc_enable_threads)
  find_package(Threads REQUIRED)
  message(STATUS "Thread library: ${CMAKE_THREAD_LIBS_INIT}")
  include_directories(${Threads_INCLUDE_DIR})
  if (bdwgc_with_libatomic_ops)
    if (bdwgc_without_libatomic_ops)
      message(FATAL_ERROR
        "bdwgc_with_libatomic_ops and bdwgc_without_libatomic_ops are mutually exclusive")
    endif()
    set(ATOMIC_OPS_LIBS "-latomic_ops")
    find_package(Atomic_ops CONFIG)
    if (Atomic_ops_FOUND)
      get_target_property(AO_INCLUDE_DIRS Atomic_ops::atomic_ops
                          INTERFACE_INCLUDE_DIRECTORIES)
      include_directories(${AO_INCLUDE_DIRS})
      message(STATUS "AO_INCLUDE_DIRS = ${AO_INCLUDE_DIRS}")
      set(ATOMIC_OPS_LIBS_CMAKE Atomic_ops::atomic_ops)
    else()
      set(ATOMIC_OPS_LIBS_CMAKE ${ATOMIC_OPS_LIBS})
    endif()
  elseif (bdwgc_without_libatomic_ops)
    include_directories(libatomic_ops/src)
    # In the tests we use the source files directly from `libatomic_ops`
    # subtree.
    set(NODIST_SRC libatomic_ops/src/atomic_ops.c)
    if (CMAKE_C_COMPILER_ID STREQUAL "SunPro")
      # SunCC compiler on SunOS (Solaris).
      enable_language(ASM)
      set(NODIST_SRC ${NODIST_SRC} libatomic_ops/src/atomic_ops_sysdeps.S)
    endif()
  elseif (BORLAND OR MSVC OR WATCOM)
    include_directories(libatomic_ops/src)
    # Note: alternatively, use `CFLAGS_EXTRA` to
    # pass `-I<...>/libatomic_ops/src`.
  else()
    # Assume the compiler supports GCC atomic intrinsics.
    add_definitions("-DGC_BUILTIN_ATOMIC")
  endif()
  set(THREADDLLIBS_LIST ${CMAKE_THREAD_LIBS_INIT})
  if (${CMAKE_DL_LIBS} MATCHES ^[^-].*)
    # Some `cmake` versions have a broken nonempty `CMAKE_DL_LIBS`
    # omitting "-l".  Assume `CMAKE_DL_LIBS` contains just one library.
    set(THREADDLLIBS_LIST ${THREADDLLIBS_LIST} -l${CMAKE_DL_LIBS})
  else()
    set(THREADDLLIBS_LIST ${THREADDLLIBS_LIST} ${CMAKE_DL_LIBS})
  endif()
  # Thread support detection.
  if (CMAKE_USE_PTHREADS_INIT)
    set(SRC ${SRC} gc_dlopen.c pthread_start.c pthread_support.c)
    if (CYGWIN OR WIN32)
      set(SRC ${SRC} win32_threads.c)
    else()
      if (APPLE)
        set(SRC ${SRC} darwin_stop_world.c)
      else()
        set(SRC ${SRC} pthread_stop_world.c)
      endif()
    endif()
    if (HOST MATCHES .*-.*-hpux10.*)
      message(FATAL_ERROR "HP/UX 10 POSIX threads are not supported.")
    endif()
    # Common defines for POSIX platforms.
    add_definitions("-DGC_THREADS")
    add_definitions("-D_REENTRANT")
    if (bdwgc_enable_parallel_mark)
      add_definitions("-DPARALLEL_MARK")
    endif()
    if (bdwgc_enable_thread_local_alloc)
      add_definitions("-DTHREAD_LOCAL_ALLOC")
      set(SRC ${SRC} specific.c thread_local_alloc.c)
    endif()
    message("Explicit GC_INIT() calls may be required.")
    if (HOST MATCHES .*-.*-hpux11.*)
      message("Only HP/UX 11 POSIX threads are supported.")
      add_definitions("-D_POSIX_C_SOURCE=199506L")
      set(NEED_LIB_RT ON)
    elseif (HOST MATCHES .*-.*-netbsd.*)
      add_definitions("-D_PTHREADS")
      set(NEED_LIB_RT ON)
    elseif (CMAKE_C_COMPILER_ID STREQUAL "SunPro")
      set(NEED_LIB_RT ON)
    endif()
    if (WIN32) # AND NOT CYGWIN
      # Does not provide process `fork` functionality.
    elseif (bdwgc_enable_handle_fork AND NOT bdwgc_disable_handle_fork)
      add_definitions("-DHANDLE_FORK")
    endif()
    if (bdwgc_enable_sigrt_signals)
      add_definitions("-DGC_USESIGRT_SIGNALS")
    endif()
  elseif (CMAKE_USE_WIN32_THREADS_INIT)
    add_definitions("-DGC_THREADS")
    if (bdwgc_enable_parallel_mark)
      add_definitions("-DPARALLEL_MARK")
    endif()
    if (bdwgc_enable_thread_local_alloc AND (bdwgc_enable_parallel_mark OR NOT bdwgc_build_shared_libs))
      # Imply `THREAD_LOCAL_ALLOC` unless `GC_DLL`.
      add_definitions("-DTHREAD_LOCAL_ALLOC")
      set(SRC ${SRC} thread_local_alloc.c)
    endif()
    add_definitions("-DEMPTY_GETENV_RESULTS")
    # Add `pthread_start.c` file just in case client defines
    # `GC_WIN32_PTHREADS` macro.
    set(SRC ${SRC} pthread_start.c)
    set(SRC ${SRC} pthread_support.c win32_threads.c)
  elseif (CMAKE_HP_PTHREADS_INIT OR CMAKE_USE_SPROC_INIT)
    message(FATAL_ERROR "Unsupported thread package")
  endif()
  if (BORLAND)
    # Workaround "cannot locate assembly file" and "out of hash space"
    # compilation errors, "restarting compile using assembly" warning.
    add_definitions("-DAO_NO_ASM_XADD")
    add_definitions("-DAO_NO_ASM_XCHG")
  endif()
endif(bdwgc_enable_threads)

# Check whether `-lrt` linker option is needed to use `clock_gettime`.
if (NOT NEED_LIB_RT)
  check_function_exists(clock_gettime HAVE_CLOCK_GETTIME_DIRECTLY)
  if (NOT HAVE_CLOCK_GETTIME_DIRECTLY)
    # Use of `clock_gettime` probably requires linking with `rt` library.
    set(NEED_LIB_RT ON)
  endif()
endif()

# Locate and use `rt` library if needed (and the library is available).
if (NEED_LIB_RT)
  find_library(LIBRT rt)
  if (LIBRT)
    set(THREADDLLIBS_LIST ${THREADDLLIBS_LIST} ${LIBRT})
  endif()
endif(NEED_LIB_RT)

if (bdwgc_disable_handle_fork)
  add_definitions("-DNO_HANDLE_FORK")
endif()

if (bdwgc_enable_gcj_support)
  add_definitions("-DGC_GCJ_SUPPORT")
  if (bdwgc_enable_threads AND NOT (bdwgc_enable_thread_local_alloc AND HOST MATCHES .*-.*-kfreebsd.*-gnu))
    # FIXME: For a reason, `gctest` hangs up on kFreeBSD if both of
    # `THREAD_LOCAL_ALLOC` and `GC_ENABLE_SUSPEND_THREAD` are defined.
    add_definitions("-DGC_ENABLE_SUSPEND_THREAD")
  endif()
  set(SRC ${SRC} gcj_mlc.c)
endif(bdwgc_enable_gcj_support)

if (bdwgc_enable_disclaim)
  add_definitions("-DENABLE_DISCLAIM")
  set(SRC ${SRC} fnlz_mlc.c)
endif()

if (bdwgc_enable_dynamic_pointer_mask)
  add_definitions("-DDYNAMIC_POINTER_MASK")
endif()

if (bdwgc_enable_java_finalization)
  add_definitions("-DJAVA_FINALIZATION")
endif()

if (bdwgc_enable_atomic_uncollectable)
  add_definitions("-DGC_ATOMIC_UNCOLLECTABLE")
endif()

if (bdwgc_enable_valgrind_tracking)
  add_definitions("-DVALGRIND_TRACKING")
endif()

if (bdwgc_enable_gc_debug)
  add_definitions("-DDBG_HDRS_ALL")
  add_definitions("-DKEEP_BACK_PTRS")
  if (HOST MATCHES i.86-.*-dgux.*|.*-.*-.*linux.*)
    add_definitions("-DMAKE_BACK_GRAPH")
    if (HOST MATCHES .*-.*-.*linux.* AND NOT (HOST MATCHES e2k-.*-linux.*))
      add_definitions("-DSAVE_CALL_COUNT=8")
    endif()
    set(SRC ${SRC} backgraph.c)
  endif()
endif(bdwgc_enable_gc_debug)

if (bdwgc_disable_gc_debug)
  add_definitions("-DNO_DEBUGGING")
elseif (WINCE)
  # Read environment variables from `<program>.gc.env` file.
  add_definitions("-DGC_READ_ENV_FILE")
endif()

if (bdwgc_enable_redirect_malloc)
  if (bdwgc_enable_gc_debug)
    add_definitions("-DREDIRECT_MALLOC=GC_debug_malloc_replacement")
    add_definitions("-DREDIRECT_REALLOC=GC_debug_realloc_replacement")
    add_definitions("-DREDIRECT_FREE=GC_debug_free")
  else()
    add_definitions("-DREDIRECT_MALLOC=GC_malloc")
  endif()
  if (WIN32)
    add_definitions("-DREDIRECT_MALLOC_IN_HEADER")
  else()
    add_definitions("-DGC_USE_DLOPEN_WRAP")
  endif()
endif(bdwgc_enable_redirect_malloc)

if (bdwgc_enable_mmap OR bdwgc_enable_munmap)
  add_definitions("-DUSE_MMAP")
  if (bdwgc_enable_munmap)
    add_definitions("-DUSE_MUNMAP")
  endif(bdwgc_enable_munmap)
endif()

if (NOT bdwgc_enable_dynamic_loading)
  add_definitions("-DIGNORE_DYNAMIC_LOADING")
endif()

if (NOT bdwgc_enable_register_main_static_data)
  add_definitions("-DGC_DONT_REGISTER_MAIN_STATIC_DATA")
endif()

if (bdwgc_enable_large_config)
  add_definitions("-DLARGE_CONFIG")
endif()

if (bdwgc_enable_gc_assertions)
  add_definitions("-DGC_ASSERTIONS")
  # TODO: pass `-Wno-missing-prototypes` (if supported) to turn off the clang
  # warning for `STATIC` functions.
endif()

if (NOT bdwgc_enable_threads_discovery)
  add_definitions("-DGC_NO_THREADS_DISCOVERY")
endif()

if (bdwgc_enable_rwlock)
  # Use `rwlock` for the allocator lock instead of mutex.
  add_definitions("-DUSE_RWLOCK")
endif()

if (bdwgc_enable_checksums)
  if (bdwgc_enable_munmap OR bdwgc_enable_threads)
    message(FATAL_ERROR "CHECKSUMS not compatible with USE_MUNMAP or threads")
  endif()
  add_definitions("-DCHECKSUMS")
  set(SRC ${SRC} checksums.c)
endif(bdwgc_enable_checksums)

if (bdwgc_enable_werror)
  if (BORLAND)
    add_compile_options(/w!)
  elseif (MSVC)
    add_compile_options(/WX)
    # Workaround "typedef ignored on left of ..." warning reported in
    # `imagehlp.h` file of e.g. Windows Kit 8.1.
    add_compile_options(/wd4091)
  elseif (WATCOM)
    add_compile_options(/we)
  else()
    add_compile_options(-Werror)
  endif()
endif(bdwgc_enable_werror)

if (bdwgc_enable_single_obj_compilation OR (bdwgc_build_shared_libs AND NOT bdwgc_disable_single_obj_compilation))
  set(SRC extra/gc.c) # override `SRC`
  if (bdwgc_enable_threads AND CMAKE_USE_PTHREADS_INIT AND NOT (APPLE OR CYGWIN OR WIN32))
    add_definitions("-DGC_PTHREAD_START_STANDALONE")
    set(SRC ${SRC} pthread_start.c)
  endif()
endif()

# Add implementation of `backtrace` and `backtrace_symbols`.
if (MSVC)
  set(SRC ${SRC} extra/msvc_dbg.c)
endif()

# Declare that the libraries do not refer to external symbols.
if (bdwgc_build_shared_libs AND NOT (APPLE OR ${CMAKE_SYSTEM_NAME} MATCHES "BSD"))
  # Note: performed before `CMAKE_REQUIRED_FLAGS` is updated with "-c".
  if (${CMAKE_VERSION} VERSION_LESS "3.18.0")
    set(WL_NO_UNDEFINED_OPT "-Wl,--no-undefined")
    check_c_compiler_flag(${WL_NO_UNDEFINED_OPT} HAVE_FLAG_WL_NO_UNDEFINED)
  else()
    set(WL_NO_UNDEFINED_OPT "LINKER:--no-undefined")
    check_linker_flag(C "${WL_NO_UNDEFINED_OPT}" HAVE_FLAG_WL_NO_UNDEFINED)
  endif()
endif()

# Instruct `check_c_source_compiles` to skip linking.
# Alternatively, we could set `CMAKE_REQUIRED_LIBRARIES` properly.
SET(CMAKE_REQUIRED_FLAGS "-c")

if (NOT (BORLAND OR MSVC OR WATCOM))
  # Instruct `check_c_source_compiles` and similar `cmake` checks not to
  # ignore compiler warnings (like "implicit declaration of function").
  check_c_compiler_flag(-Werror HAVE_FLAG_WERROR)
  if (HAVE_FLAG_WERROR)
    check_c_compiler_flag(-Wno-unused-command-line-argument
                          HAVE_FLAG_WNO_UNUSED_CMDLINE_ARG)
    SET(CMAKE_REQUIRED_FLAGS "${CMAKE_REQUIRED_FLAGS} -Werror")
    # Prevent "linker input unused" error in further `check_c_compiler_flag`.
    if (HAVE_FLAG_WNO_UNUSED_CMDLINE_ARG)
      SET(CMAKE_REQUIRED_FLAGS
          "${CMAKE_REQUIRED_FLAGS} -Wno-unused-command-line-argument")
    endif()
  endif(HAVE_FLAG_WERROR)
  # Prevent "__builtin_return_address with nonzero argument is unsafe" warning.
  check_c_compiler_flag(-Wno-frame-address HAVE_FLAG_WNO_FRAME_ADDRESS)
  if (HAVE_FLAG_WNO_FRAME_ADDRESS)
    add_compile_options(-Wno-frame-address)
  endif(HAVE_FLAG_WNO_FRAME_ADDRESS)
endif()

if (bdwgc_build_shared_libs)
  add_definitions("-DGC_DLL")
  # Pass `-fvisibility=hidden` option if supported.
  check_c_compiler_flag(-fvisibility=hidden HAVE_FLAG_F_VISIBILITY_HIDDEN)
  if (HAVE_FLAG_F_VISIBILITY_HIDDEN)
    add_definitions("-DGC_VISIBILITY_HIDDEN_SET")
    add_compile_options(-fvisibility=hidden)
  else()
    add_definitions("-DGC_NO_VISIBILITY")
  endif()
else()
  add_definitions("-DGC_NOT_DLL")
  if (WIN32)
    # Do not require the clients to link with `user32` system library.
    add_definitions("-DDONT_USE_USER32_DLL")
  endif(WIN32)
endif()

# Configuration of machine-dependent code.
if (CMAKE_SYSTEM_PROCESSOR MATCHES "^(sparc.*|SPARC.*)")
  # TODO: Handle NetBSD/SPARC (32-bit) specially.
  enable_language(ASM)
  # Note: file extension `.S` (capital S) stands for the assembly code that
  # must still pass through a preprocessor.
  set(SRC ${SRC} sparc_mach_dep.S)
endif()

# Extra user-defined flags to pass both to C and C++ compilers.
if (DEFINED CFLAGS_EXTRA)
  separate_arguments(CFLAGS_EXTRA_LIST UNIX_COMMAND "${CFLAGS_EXTRA}")
  add_compile_options(${CFLAGS_EXTRA_LIST})
endif()

# Check whether platform `execinfo.h` file is present.
check_include_file(execinfo.h HAVE_EXECINFO_H)
if (NOT HAVE_EXECINFO_H)
  add_definitions("-DGC_MISSING_EXECINFO_H")
endif()

check_include_file(sys/types.h HAVE_SYS_TYPES_H)
if (HAVE_SYS_TYPES_H)
  add_definitions("-DHAVE_SYS_TYPES_H")
endif()

if (NOT WIN32)
  check_include_file(unistd.h HAVE_UNISTD_H)
  if (HAVE_UNISTD_H)
    add_definitions("-DHAVE_UNISTD_H")
  endif()
endif()

# Check for `getcontext` (e.g., uClibc can be configured without it).
if (NOT (BORLAND OR MSVC))
  check_function_exists(getcontext HAVE_GETCONTEXT)
  if (HAVE_GETCONTEXT AND NOT APPLE)
    # Double check `getcontext` is available (needed at least on OpenBSD 7.3).
    # Note: OS X is excluded here because the header filename differs.
    check_c_source_compiles("
#include <ucontext.h>\n
int main(void) { ucontext_t ctxt; (void)getcontext(&ctxt); return 0; }"
      HAVE_GETCONTEXT_FUNC)
    if (NOT HAVE_GETCONTEXT_FUNC)
      set(HAVE_GETCONTEXT OFF)
    endif()
  endif()
  if (NOT HAVE_GETCONTEXT)
    add_definitions("-DNO_GETCONTEXT")
  endif()
endif()

# Check whether `dl_iterate_phdr` exists (as a strong symbol).
if (NOT (APPLE OR CYGWIN OR WIN32))
  check_function_exists(dl_iterate_phdr HAVE_DL_ITERATE_PHDR)
  if (HAVE_DL_ITERATE_PHDR)
    add_definitions("-DHAVE_DL_ITERATE_PHDR")
  endif(HAVE_DL_ITERATE_PHDR)
endif()

# Check for `pthread_sigmask` and `sigset_t`.
if (bdwgc_enable_threads AND CMAKE_USE_PTHREADS_INIT)
  if (NOT (APPLE OR CYGWIN OR WIN32))
    check_c_source_compiles("
#define _GNU_SOURCE 1\n
#include <pthread.h>\n
#include <signal.h>\n
int main(void) { sigset_t t; (void)pthread_sigmask(SIG_BLOCK, 0, &t); return 0; }"
      HAVE_PTHREAD_SIGMASK)
    if (HAVE_PTHREAD_SIGMASK)
      # Define to use `pthread_sigmask` function if needed.
      add_definitions("-DGC_HAVE_PTHREAD_SIGMASK")
    endif(HAVE_PTHREAD_SIGMASK)
  endif()
endif()

check_symbol_exists(sigsetjmp setjmp.h HAVE_SIGSETJMP)
if (NOT HAVE_SIGSETJMP)
  add_definitions("-DGC_NO_SIGSETJMP")
endif()

# Build with `GC_wcsdup` support if possible.
check_symbol_exists(wcslen wchar.h HAVE_WCSLEN)
if (HAVE_WCSLEN)
  add_definitions("-DGC_REQUIRE_WCSDUP")
endif()

# `pthread_setname_np`, if available, may have 1, 2 or 3 arguments.
if (bdwgc_enable_threads AND CMAKE_USE_PTHREADS_INIT)
  check_c_source_compiles("
#define _GNU_SOURCE 1\n
#include <pthread.h>\n
int main(void) { (void)pthread_setname_np(\"thread-name\"); return 0; }"
    HAVE_PTHREAD_SETNAME_NP_WITHOUT_TID)
  if (HAVE_PTHREAD_SETNAME_NP_WITHOUT_TID)
    # Define to use `pthread_setname_np(const char *)`.
    add_definitions("-DHAVE_PTHREAD_SETNAME_NP_WITHOUT_TID")
  else()
    check_c_source_compiles("
#define _GNU_SOURCE 1\n
#include <pthread.h>\n
int main(void) {\n
  (void)pthread_setname_np(pthread_self(), \"thread-name-%u\", 0); return 0; }"
      HAVE_PTHREAD_SETNAME_NP_WITH_TID_AND_ARG)
    if (HAVE_PTHREAD_SETNAME_NP_WITH_TID_AND_ARG)
      # Define to use `pthread_setname_np(pthread_t, const char *, void *)`.
      add_definitions("-DHAVE_PTHREAD_SETNAME_NP_WITH_TID_AND_ARG")
    else()
      check_c_source_compiles("
#define _GNU_SOURCE 1\n
#include <pthread.h>\n
int main(void) {\n
  (void)pthread_setname_np(pthread_self(), \"thread-name\"); return 0; }"
        HAVE_PTHREAD_SETNAME_NP_WITH_TID)
      if (HAVE_PTHREAD_SETNAME_NP_WITH_TID)
        # Define to use `pthread_setname_np(pthread_t, const char *)`.
        add_definitions("-DHAVE_PTHREAD_SETNAME_NP_WITH_TID")
      else()
        check_c_source_compiles("
#include <pthread.h>\n
#include <pthread_np.h>\n
int main(void) {\n
  pthread_set_name_np(pthread_self(), \"thread-name\"); return 0; }"
          HAVE_PTHREAD_SET_NAME_NP)
        if (HAVE_PTHREAD_SET_NAME_NP)
          # Define to use `pthread_set_name_np(pthread_t, const char *)`.
          add_definitions("-DHAVE_PTHREAD_SET_NAME_NP")
        endif()
      endif(HAVE_PTHREAD_SETNAME_NP_WITH_TID)
    endif(HAVE_PTHREAD_SETNAME_NP_WITH_TID_AND_ARG)
  endif(HAVE_PTHREAD_SETNAME_NP_WITHOUT_TID)
endif()

# Check for `dladdr` (used for debugging).
check_c_source_compiles("
#define _GNU_SOURCE 1\n
#include <dlfcn.h>\n
int main(void) { Dl_info info; (void)dladdr(\"\", &info); return 0; }"
  HAVE_DLADDR)
if (HAVE_DLADDR)
  # Define to use `dladdr` function.
  add_definitions("-DHAVE_DLADDR")
endif()

# Check for `emscripten`; use `asyncify` feature if requested.
check_c_source_compiles("
#ifndef __EMSCRIPTEN__\n
# error This is not Emscripten\n
#endif\n
int main(void) { return 0; }"
  EMSCRIPTEN)
if (EMSCRIPTEN AND bdwgc_enable_emscripten_asyncify)
  # Use this option if your program is targeting `-sASYNCIFY`.  The latter is
  # required to scan the stack, `ASYNCIFY_STACK_SIZE` is probably needed for
  # `gctest` only.
  add_definitions("-DEMSCRIPTEN_ASYNCIFY")
  set(CMAKE_EXE_LINKER_FLAGS
        "${CMAKE_EXE_LINKER_FLAGS} -sASYNCIFY -sASYNCIFY_STACK_SIZE=128000")
endif()

add_library(gc ${SRC})
add_library(bdwgc::gc ALIAS gc)
target_link_libraries(gc
                PRIVATE ${ATOMIC_OPS_LIBS_CMAKE} ${THREADDLLIBS_LIST})
target_include_directories(gc INTERFACE
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
        "$<INSTALL_INTERFACE:include>")

if (bdwgc_enable_cplusplus)
  if (BORLAND OR MSVC OR WATCOM)
    add_library(gccpp gc_badalc.cpp gc_cpp.cpp)
  else()
    add_library(gccpp gc_badalc.cc gc_cpp.cc)
  endif()
  add_library(bdwgc::gccpp ALIAS gccpp)
  target_link_libraries(gccpp PRIVATE gc)
  target_include_directories(gccpp INTERFACE
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
        "$<INSTALL_INTERFACE:include>")
  if (bdwgc_enable_throw_bad_alloc_library)
    # The same as `gccpp` but contains only `gc_badalc`.
    if (BORLAND OR MSVC OR WATCOM)
      add_library(gctba gc_badalc.cpp)
    else()
      add_library(gctba gc_badalc.cc)
    endif()
    add_library(bdwgc::gctba ALIAS gctba)
    target_link_libraries(gctba PRIVATE gc)
    target_include_directories(gctba INTERFACE
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
        "$<INSTALL_INTERFACE:include>")
  endif(bdwgc_enable_throw_bad_alloc_library)
endif()

if (bdwgc_build_cord)
  set(CORD_SRC cord/cordbscs.c cord/cordprnt.c cord/cordxtra.c)
  add_library(cord ${CORD_SRC})
  add_library(bdwgc::cord ALIAS cord)
  target_link_libraries(cord PRIVATE gc)
  target_include_directories(cord INTERFACE
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
        "$<INSTALL_INTERFACE:include>")
  if (bdwgc_build_shared_libs)
    set_property(TARGET cord PROPERTY VERSION ${CORD_VERSION_PROP})
    set_property(TARGET cord PROPERTY SOVERSION ${CORD_SOVERSION})
  endif()
  install(TARGETS cord EXPORT BDWgcTargets
          LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
          ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
          RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
          INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
endif(bdwgc_build_cord)

if (bdwgc_build_shared_libs AND HAVE_FLAG_WL_NO_UNDEFINED)
  # Declare that the libraries do not refer to external symbols.
  if (${CMAKE_VERSION} VERSION_LESS "3.13.0")
    target_link_libraries(gc PRIVATE ${WL_NO_UNDEFINED_OPT})
    if (bdwgc_enable_cplusplus)
      target_link_libraries(gccpp PRIVATE ${WL_NO_UNDEFINED_OPT})
      if (bdwgc_enable_throw_bad_alloc_library)
        target_link_libraries(gctba PRIVATE ${WL_NO_UNDEFINED_OPT})
      endif(bdwgc_enable_throw_bad_alloc_library)
    endif(bdwgc_enable_cplusplus)
    if (bdwgc_build_cord)
      target_link_libraries(cord PRIVATE ${WL_NO_UNDEFINED_OPT})
    endif(bdwgc_build_cord)
  else()
    target_link_options(gc PRIVATE ${WL_NO_UNDEFINED_OPT})
    if (bdwgc_enable_cplusplus)
      target_link_options(gccpp PRIVATE ${WL_NO_UNDEFINED_OPT})
      if (bdwgc_enable_throw_bad_alloc_library)
        target_link_options(gctba PRIVATE ${WL_NO_UNDEFINED_OPT})
      endif(bdwgc_enable_throw_bad_alloc_library)
    endif(bdwgc_enable_cplusplus)
    if (bdwgc_build_cord)
      target_link_options(cord PRIVATE ${WL_NO_UNDEFINED_OPT})
    endif(bdwgc_build_cord)
  endif()
endif()

if (bdwgc_build_shared_libs)
  set_property(TARGET gc PROPERTY VERSION ${GC_VERSION_PROP})
  set_property(TARGET gc PROPERTY SOVERSION ${GC_SOVERSION})
endif()
install(TARGETS gc EXPORT BDWgcTargets
        LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
        INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")

if (bdwgc_enable_cplusplus)
  if (bdwgc_build_shared_libs)
    set_property(TARGET gccpp PROPERTY VERSION ${GCCPP_VERSION_PROP})
    set_property(TARGET gccpp PROPERTY SOVERSION ${GCCPP_SOVERSION})
  endif()
  install(TARGETS gccpp EXPORT BDWgcTargets
          LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
          ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
          RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
          INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
  if (bdwgc_enable_throw_bad_alloc_library)
    if (bdwgc_build_shared_libs)
      set_property(TARGET gctba PROPERTY VERSION ${GCCPP_VERSION_PROP})
      set_property(TARGET gctba PROPERTY SOVERSION ${GCCPP_SOVERSION})
    endif()
    install(TARGETS gctba EXPORT BDWgcTargets
            LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
            ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
            RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
            INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
  endif(bdwgc_enable_throw_bad_alloc_library)
endif(bdwgc_enable_cplusplus)

if (bdwgc_install_headers)
  install(FILES include/gc/gc.h
                include/gc/gc_backptr.h
                include/gc/gc_config_macros.h
                include/gc/gc_inline.h
                include/gc/gc_mark.h
                include/gc/gc_tiny_fl.h
                include/gc/gc_typed.h
                include/gc/gc_version.h
                include/gc/javaxfc.h
                include/gc/leak_detector.h
          DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/gc")
  install(FILES include/gc.h DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
  if (bdwgc_enable_cplusplus)
    install(FILES include/gc_cpp.h DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
    install(FILES include/gc/gc_allocator.h
                  include/gc/gc_cpp.h
            DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/gc")
  endif()
  if (bdwgc_enable_disclaim)
    install(FILES include/gc/gc_disclaim.h
            DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/gc")
  endif()
  if (bdwgc_enable_gcj_support)
    install(FILES include/gc/gc_gcj.h
            DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/gc")
  endif()
  if (bdwgc_enable_threads)
    install(FILES include/gc/gc_pthread_redirects.h
            DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/gc")
  endif()
  if (bdwgc_build_cord)
    install(FILES include/gc/cord.h
                  include/gc/cord_pos.h
                  include/gc/ec.h
            DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/gc")
  endif()

  # Provide `pkg-config` metadata.
  set(prefix "${CMAKE_INSTALL_PREFIX}")
  set(exec_prefix \${prefix})
  set(includedir "${CMAKE_INSTALL_FULL_INCLUDEDIR}")
  set(libdir "${CMAKE_INSTALL_FULL_LIBDIR}")
  string(REPLACE ";" " " THREADDLLIBS "${THREADDLLIBS_LIST}")
  # `ATOMIC_OPS_LIBS`, `PACKAGE_VERSION` are defined above.
  configure_file(bdw-gc.pc.in bdw-gc.pc @ONLY)
  install(FILES "${CMAKE_CURRENT_BINARY_DIR}/bdw-gc.pc"
          DESTINATION "${CMAKE_INSTALL_LIBDIR}/pkgconfig")
endif(bdwgc_install_headers)

if (bdwgc_build_tests)
  SET(CTEST_ENVIRONMENT
      "GC_PROMPT_DISABLED=1")
  if (bdwgc_build_cord)
    add_executable(cordtest cord/tests/cordtest.c ${NODIST_SRC})
    target_link_libraries(cordtest PRIVATE cord gc)
    add_test(NAME cordtest COMMAND cordtest)

    if (WIN32) # AND NOT CYGWIN
      if (NOT (CMAKE_C_COMPILER_ID STREQUAL "Clang"))
        # Workaround MS Clang failure to compile a resource file.
        set(DE_WIN_RC cord/tests/de_win.rc)
      endif()
      add_executable(de cord/tests/de.c cord/tests/de_win.c
                     ${DE_WIN_RC} ${NODIST_SRC})
      set_target_properties(de PROPERTIES WIN32_EXECUTABLE TRUE)
      target_link_libraries(de PRIVATE cord gc gdi32)
    endif()
  endif(bdwgc_build_cord)

  # Compile some tests as C++ to test `extern "C"` in header files.
  if (bdwgc_enable_cplusplus)
    set_source_files_properties(tests/leak.c PROPERTIES LANGUAGE CXX)
    if (BORLAND OR MSVC OR WATCOM)
      # `WinMain`-based test hangs at startup if compiled by VC as C++ code.
    else()
      set_source_files_properties(tests/gctest.c PROPERTIES LANGUAGE CXX)
      # To avoid "treating 'c' input as 'c++' when in C++ mode" Clang warning.
      add_compile_options(-x c++)
    endif()
  endif(bdwgc_enable_cplusplus)

  add_executable(gctest WIN32 tests/gctest.c ${NODIST_SRC})
  target_link_libraries(gctest
                PRIVATE gc ${ATOMIC_OPS_LIBS_CMAKE} ${THREADDLLIBS_LIST})
  add_test(NAME gctest COMMAND gctest)
  if (WATCOM AND NOT bdwgc_enable_gc_assertions)
    # Suppress "unreachable code" warning in `GC_MALLOC_WORDS()` and
    # `GC_MALLOC_ATOMIC_WORDS()`.
    target_compile_options(gctest PRIVATE /wcd=201)
  endif()

  add_executable(hugetest tests/huge.c ${NODIST_SRC})
  target_link_libraries(hugetest PRIVATE gc)
  add_test(NAME hugetest COMMAND hugetest)

  add_executable(leaktest tests/leak.c ${NODIST_SRC})
  target_link_libraries(leaktest PRIVATE gc)
  add_test(NAME leaktest COMMAND leaktest)

  add_executable(middletest tests/middle.c ${NODIST_SRC})
  target_link_libraries(middletest PRIVATE gc)
  add_test(NAME middletest COMMAND middletest)

  add_executable(realloctest tests/realloc.c ${NODIST_SRC})
  target_link_libraries(realloctest PRIVATE gc)
  add_test(NAME realloctest COMMAND realloctest)

  add_executable(smashtest tests/smash.c ${NODIST_SRC})
  target_link_libraries(smashtest PRIVATE gc)
  add_test(NAME smashtest COMMAND smashtest)

  if (NOT (bdwgc_build_shared_libs AND WIN32))
    add_library(staticroots_lib_test tests/staticroots_lib.c)
    target_link_libraries(staticroots_lib_test PRIVATE gc)
    add_library(staticroots_lib2_test tests/staticroots_lib.c)
    target_compile_options(staticroots_lib2_test PRIVATE "-DSTATICROOTSLIB2")
    target_link_libraries(staticroots_lib2_test PRIVATE gc)
    add_executable(staticrootstest tests/staticroots.c ${NODIST_SRC})
    target_compile_options(staticrootstest PRIVATE "-DSTATICROOTSLIB2")
    target_link_libraries(staticrootstest PRIVATE
                          gc staticroots_lib_test staticroots_lib2_test)
    add_test(NAME staticrootstest COMMAND staticrootstest)
  endif()

  if (bdwgc_enable_gc_debug)
    add_executable(tracetest tests/trace.c ${NODIST_SRC})
    target_link_libraries(tracetest PRIVATE gc)
    add_test(NAME tracetest COMMAND tracetest)
  endif()

  if (bdwgc_enable_threads)
    add_executable(atomicopstest tests/atomicops.c ${NODIST_SRC})
    target_link_libraries(atomicopstest
                PRIVATE ${ATOMIC_OPS_LIBS_CMAKE} ${THREADDLLIBS_LIST})
    add_test(NAME atomicopstest COMMAND atomicopstest)

    add_executable(initfromthreadtest tests/initfromthread.c ${NODIST_SRC})
    target_link_libraries(initfromthreadtest PRIVATE gc ${THREADDLLIBS_LIST})
    add_test(NAME initfromthreadtest COMMAND initfromthreadtest)

    add_executable(subthreadcreatetest tests/subthreadcreate.c ${NODIST_SRC})
    target_link_libraries(subthreadcreatetest
                PRIVATE gc ${ATOMIC_OPS_LIBS_CMAKE} ${THREADDLLIBS_LIST})
    add_test(NAME subthreadcreatetest COMMAND subthreadcreatetest)

    add_executable(threadleaktest tests/threadleak.c ${NODIST_SRC})
    target_link_libraries(threadleaktest PRIVATE gc ${THREADDLLIBS_LIST})
    add_test(NAME threadleaktest COMMAND threadleaktest)

    if (NOT WIN32)
      add_executable(threadkeytest tests/threadkey.c ${NODIST_SRC})
      target_link_libraries(threadkeytest PRIVATE gc ${THREADDLLIBS_LIST})
      add_test(NAME threadkeytest COMMAND threadkeytest)
    endif()
  endif(bdwgc_enable_threads)

  if (bdwgc_enable_cplusplus)
    add_executable(cpptest WIN32 tests/cpp.cc ${NODIST_SRC})
    target_link_libraries(cpptest PRIVATE gc gccpp)
    add_test(NAME cpptest COMMAND cpptest)
    if (bdwgc_enable_throw_bad_alloc_library)
      add_executable(treetest tests/tree.cc ${NODIST_SRC})
      target_link_libraries(treetest PRIVATE gc gctba)
      add_test(NAME treetest COMMAND treetest)
    endif(bdwgc_enable_throw_bad_alloc_library)
  endif()

  if (bdwgc_enable_disclaim)
    add_executable(disclaim_bench tests/disclaim_bench.c ${NODIST_SRC})
    target_link_libraries(disclaim_bench PRIVATE gc)
    add_test(NAME disclaim_bench COMMAND disclaim_bench)

    add_executable(disclaimtest tests/disclaim.c ${NODIST_SRC})
    target_link_libraries(disclaimtest PRIVATE gc ${THREADDLLIBS_LIST})
    add_test(NAME disclaimtest COMMAND disclaimtest)

    add_executable(weakmaptest tests/weakmap.c ${NODIST_SRC})
    target_link_libraries(weakmaptest
                PRIVATE gc ${ATOMIC_OPS_LIBS_CMAKE} ${THREADDLLIBS_LIST})
    add_test(NAME weakmaptest COMMAND weakmaptest)
  endif()
endif(bdwgc_build_tests)

if (bdwgc_enable_docs)
  install(FILES AUTHORS ChangeLog LICENSE README.md
          DESTINATION "${CMAKE_INSTALL_DOCDIR}")
  install(FILES
            docs/autoconf.md
            docs/cmake.md
            docs/cords.md
            docs/debugging.md
            docs/environment.md
            docs/faq.md
            docs/finalization.md
            docs/gcdescr.md
            docs/gcinterface.md
            docs/leak.md
            docs/macros.md
            docs/overview.md
            docs/porting.md
            docs/scale.md
            docs/simple_example.md
            docs/tree.md
          DESTINATION "${CMAKE_INSTALL_DOCDIR}/docs")
  install(FILES
            docs/platforms/README.aix
            docs/platforms/README.arm_cross
            docs/platforms/README.darwin
            docs/platforms/README.dgux386
            docs/platforms/README.emscripten
            docs/platforms/README.ews4800
            docs/platforms/README.hp
            docs/platforms/README.linux
            docs/platforms/README.os2
            docs/platforms/README.sgi
            docs/platforms/README.solaris2
            docs/platforms/README.symbian
            docs/platforms/README.uts
            docs/platforms/README.win32
            docs/platforms/README.win64
          DESTINATION "${CMAKE_INSTALL_DOCDIR}/docs/platforms")

  install(FILES gc.man DESTINATION "${CMAKE_INSTALL_MANDIR}/man3" RENAME gc.3)
endif(bdwgc_enable_docs)

# CMake config/targets files.
install(EXPORT BDWgcTargets FILE BDWgcTargets.cmake
        NAMESPACE BDWgc:: DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/bdwgc")

configure_package_config_file("${CMAKE_CURRENT_SOURCE_DIR}/Config.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/BDWgcConfig.cmake"
        INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/bdwgc"
        NO_SET_AND_CHECK_MACRO)

write_basic_package_version_file(
        "${CMAKE_CURRENT_BINARY_DIR}/BDWgcConfigVersion.cmake"
        VERSION "${PACKAGE_VERSION}" COMPATIBILITY AnyNewerVersion)

install(FILES "${CMAKE_CURRENT_BINARY_DIR}/BDWgcConfig.cmake"
              "${CMAKE_CURRENT_BINARY_DIR}/BDWgcConfigVersion.cmake"
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/bdwgc")

export(EXPORT BDWgcTargets
       FILE "${CMAKE_CURRENT_BINARY_DIR}/BDWgcTargets.cmake")
