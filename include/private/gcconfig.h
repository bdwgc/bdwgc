/*
 * Copyright 1988, 1989 Hans-J. Boehm, Alan J. Demers
 * Copyright (c) 1991-1994 by Xerox Corporation.  All rights reserved.
 * Copyright (c) 1996 by Silicon Graphics.  All rights reserved.
 * Copyright (c) 2000-2004 Hewlett-Packard Development Company, L.P.
 * Copyright (c) 2009-2022 Ivan Maidanski
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
 * This header is private to the collector.  It is almost always
 * included from `gc_priv.h` file.  However it is possible to include
 * it by itself if just the configuration macros are needed.  In that
 * case, a few declarations relying on types declared in `gc_priv.h`
 * file will be omitted.
 */

#ifndef GCCONFIG_H
#define GCCONFIG_H

#ifndef GC_H
#  ifdef HAVE_CONFIG_H
#    include "config.h"
#  endif
#  include "gc/gc.h"
#endif

#ifdef CPPCHECK
#  undef CLOCKS_PER_SEC
#  undef FIXUP_POINTER
#  undef POINTER_MASK
#  undef POINTER_SHIFT
#  undef REDIRECT_REALLOC
#  undef _MAX_PATH
#endif

#ifndef PTR_T_DEFINED
typedef char *ptr_t;
#  define PTR_T_DEFINED
#endif

#if !defined(sony_news)
#  include <stddef.h> /*< for `size_t`, etc. */
#endif

/*
 * Note: only wrap our own declarations, and not the included headers;
 * in this case, wrap our entire file, but temporarily unwrap/rewrap
 * around `#include` entities; macros do not need such wrapping.
 */
#ifdef __cplusplus
#  define EXTERN_C_BEGIN extern "C" {
#  define EXTERN_C_END } /* extern "C" */
#else
#  define EXTERN_C_BEGIN
#  define EXTERN_C_END
#endif

EXTERN_C_BEGIN

/* Convenient internal macro to test version of Clang. */
#if defined(__clang__) && defined(__clang_major__)
#  define GC_CLANG_PREREQ(major, minor) \
    ((__clang_major__ << 8) + __clang_minor__ >= ((major) << 8) + (minor))
#  define GC_CLANG_PREREQ_FULL(major, minor, patchlevel)          \
    (GC_CLANG_PREREQ(major, (minor) + 1)                          \
     || (__clang_major__ == (major) && __clang_minor__ == (minor) \
         && __clang_patchlevel__ >= (patchlevel)))
#else
#  define GC_CLANG_PREREQ(major, minor) 0 /*< `FALSE` */
#  define GC_CLANG_PREREQ_FULL(major, minor, patchlevel) 0
#endif

/*
 * Machine-dependent parameters.  Some tuning parameters can be found
 * near the top of `gc_priv.h` file.
 */

/* Machine-specific parts contributed by various people.  See `README` file. */

#if defined(__ANDROID__) && !defined(HOST_ANDROID)
/* A Linux-based OS.  `__ANDROID__` macro is defined by Android NDK gcc. */
#  define HOST_ANDROID 1
#endif

#if defined(TIZEN) && !defined(HOST_TIZEN)
/* A Linux-based OS. */
#  define HOST_TIZEN 1
#endif

#if defined(__SYMBIAN32__) && !defined(SYMBIAN)
#  define SYMBIAN
#  ifdef __WINS__
#    pragma data_seg(".data2")
#  endif
#endif

/* First a unified test for Linux. */
#if (defined(linux) || defined(__linux__) || defined(HOST_ANDROID) \
     || defined(HOST_TIZEN))                                       \
    && !defined(LINUX) && !defined(__native_client__)
#  define LINUX
#endif

/* And one for NetBSD. */
#if defined(__NetBSD__)
#  define NETBSD
#endif

/* And one for OpenBSD. */
#if defined(__OpenBSD__)
#  define OPENBSD
#endif

/* And one for FreeBSD. */
#if (defined(__FreeBSD__) || defined(__DragonFly__) \
     || defined(__FreeBSD_kernel__))                \
    && !defined(FREEBSD)                            \
    && !defined(GC_NO_FREEBSD) /*< Orbis compiler defines `__FreeBSD__` */
#  define FREEBSD
#endif

#if defined(FREEBSD) || defined(NETBSD) || defined(OPENBSD)
#  define ANY_BSD
#endif

#if defined(__COSMOPOLITAN__)
#  define COSMO
#endif

#if defined(__EMBOX__)
#  define EMBOX
#endif

#if defined(__KOS__)
#  define KOS
#endif

#if defined(__QNX__) && !defined(QNX)
#  define QNX
#endif

#if defined(__serenity__)
#  define SERENITY
#endif

/* And one for Darwin. */
#if defined(macosx) || (defined(__APPLE__) && defined(__MACH__))
#  define DARWIN
EXTERN_C_END
#  include <TargetConditionals.h>
EXTERN_C_BEGIN
#endif

/* Determine the machine type. */
#if defined(__native_client__)
#  define NACL
#  if !defined(__portable_native_client__) && !defined(__arm__)
#    define I386
#    define mach_type_known
#  else
/* Here we will rely upon arch-specific defines. */
#  endif
#endif
#if defined(__aarch64__) && !defined(ANY_BSD) && !defined(COSMO) \
    && !defined(DARWIN) && !defined(LINUX) && !defined(KOS)      \
    && !defined(NN_BUILD_TARGET_PLATFORM_NX) && !defined(QNX)    \
    && !defined(SERENITY) && !defined(_WIN32)
#  define AARCH64
#  define NOSYS
#  define mach_type_known
#endif
#if defined(__arm) || defined(__arm__) || defined(__thumb__)
#  define ARM32
#  if defined(NACL) || defined(SYMBIAN)
#    define mach_type_known
#  elif !defined(ANY_BSD) && !defined(DARWIN) && !defined(LINUX)             \
      && !defined(QNX) && !defined(NN_PLATFORM_CTR)                          \
      && !defined(SN_TARGET_PSP2) && !defined(_WIN32) && !defined(__CEGCC__) \
      && !defined(GC_NO_NOSYS)
#    define NOSYS
#    define mach_type_known
#  endif
#endif
#if defined(__riscv) && !defined(ANY_BSD) && !defined(LINUX)
#  define RISCV
#  define NOSYS
#  define mach_type_known
#endif
#if defined(vax) || defined(__vax__)
#  define VAX
#  ifdef ultrix
#    define ULTRIX
#  else
#    define BSD
#  endif
#  define mach_type_known
#endif
#if defined(NETBSD) && defined(__vax__)
#  define VAX
#  define mach_type_known
#endif
#if (defined(mips) || defined(__mips) || defined(_mips)) \
    && !defined(__TANDEM) && !defined(ANY_BSD) && !defined(LINUX)
#  define MIPS
#  if defined(nec_ews) || defined(_nec_ews)
#    define EWS4800
#    define mach_type_known
#  elif defined(ultrix) || defined(__ultrix)
#    define ULTRIX
#    define mach_type_known
#  elif !defined(_WIN32_WCE) && !defined(__CEGCC__) && !defined(__MINGW32CE__)
#    define IRIX5 /*< or Irix 6.x */
#    define mach_type_known
#  endif /* !MSWINCE */
#endif
#if defined(DGUX) && (defined(i386) || defined(__i386__))
#  define I386
#  ifndef _USING_DGUX
#    define _USING_DGUX
#  endif
#  define mach_type_known
#endif
#if defined(sequent) && (defined(i386) || defined(__i386__))
#  define I386
#  define SEQUENT
#  define mach_type_known
#endif
#if (defined(sun) || defined(__sun)) && (defined(i386) || defined(__i386__))
#  define I386
#  define SOLARIS
#  define mach_type_known
#endif
#if (defined(sun) || defined(__sun)) && defined(__amd64)
#  define X86_64
#  define SOLARIS
#  define mach_type_known
#endif
#if (defined(__OS2__) || defined(__EMX__)) && defined(__32BIT__)
#  define I386
#  define OS2
#  define mach_type_known
#endif
#if (defined(sun) || defined(__sun)) && (defined(sparc) || defined(__sparc))
/* SunOS 5.x */
EXTERN_C_END
#  include <errno.h>
EXTERN_C_BEGIN
#  define SPARC
#  define SOLARIS
#  define mach_type_known
#elif defined(sparc) && defined(unix) && !defined(sun) && !defined(linux) \
    && !defined(ANY_BSD)
#  define SPARC
#  define DRSNX
#  define mach_type_known
#endif
#if defined(_IBMR2) /* `&& defined(_AIX)` */
#  define POWERPC
#  define AIX
#  define mach_type_known
#endif
#if defined(_M_XENIX) && defined(_M_SYSV) && defined(_M_I386)
/* TODO: The above test may need refinement. */
#  define I386
#  if defined(_SCO_ELF)
#    define SCO_ELF
#  else
#    define SCO
#  endif
#  define mach_type_known
#endif
#if defined(_PA_RISC1_0) || defined(_PA_RISC1_1) || defined(_PA_RISC2_0) \
    || defined(hppa) || defined(__hppa__)
#  define HP_PA
#  if !defined(LINUX) && !defined(HPUX) && !defined(OPENBSD)
#    define HPUX
#  endif
#  define mach_type_known
#endif
#if defined(__ia64) && (defined(_HPUX_SOURCE) || defined(__HP_aCC))
#  define IA64
#  ifndef HPUX
#    define HPUX
#  endif
#  define mach_type_known
#endif
#if (defined(__BEOS__) || defined(__HAIKU__)) && defined(_X86_)
#  define I386
#  define HAIKU
#  define mach_type_known
#endif
#if defined(__HAIKU__) && (defined(__amd64__) || defined(__x86_64__))
#  define X86_64
#  define HAIKU
#  define mach_type_known
#endif
#if defined(__alpha) || defined(__alpha__)
#  define ALPHA
#  if !defined(ANY_BSD) && !defined(LINUX)
#    define OSF1 /*< Compaq Tru64 UNIX (Digital UNIX) */
#  endif
#  define mach_type_known
#endif
#if defined(__rtems__) && (defined(i386) || defined(__i386__))
#  define I386
#  define RTEMS
#  define mach_type_known
#endif
#if defined(NeXT) && defined(mc68000)
#  define M68K
#  define NEXT
#  define mach_type_known
#endif
#if defined(NeXT) && (defined(i386) || defined(__i386__))
#  define I386
#  define NEXT
#  define mach_type_known
#endif
#if defined(bsdi) && (defined(i386) || defined(__i386__))
#  define I386
#  define BSDI
#  define mach_type_known
#endif
#if defined(__386BSD__) && !defined(mach_type_known)
#  define I386
#  define THREE86BSD
#  define mach_type_known
#endif
#if defined(_CX_UX) && defined(_M88K)
#  define M88K
#  define CX_UX
#  define mach_type_known
#endif
#if defined(DGUX) && defined(m88k)
#  define M88K
/* `DGUX` macro is already defined. */
#  define mach_type_known
#endif
#if defined(_WIN32_WCE) || defined(__CEGCC__) || defined(__MINGW32CE__)
/*
 * `SH3`, `SH4`, `MIPS` macros are already defined for the corresponding
 * architectures.
 */
#  if defined(SH3) || defined(SH4)
#    define SH
#  endif
#  if defined(x86) || defined(__i386__)
#    define I386
#  endif
#  if defined(_M_ARM) || defined(ARM) || defined(_ARM_)
#    define ARM32
#  endif
#  define MSWINCE
#  define mach_type_known
#else
#  if ((defined(_MSDOS) || defined(_MSC_VER)) && (_M_IX86 >= 300))          \
      || (defined(_WIN32) && !defined(__CYGWIN32__) && !defined(__CYGWIN__) \
          && !defined(__INTERIX) && !defined(SYMBIAN))                      \
      || defined(__MINGW32__)
#    if defined(__LP64__) || defined(_M_X64)
#      define X86_64
#    elif defined(_M_ARM)
#      define ARM32
#    elif defined(_M_ARM64)
#      define AARCH64
#    else /* _M_IX86 */
#      define I386
#    endif
#    ifdef _XBOX_ONE
#      define MSWIN_XBOX1
#    else
#      ifndef MSWIN32
#        define MSWIN32 /*< or Win64 */
#      endif
#      if defined(WINAPI_FAMILY) && (WINAPI_FAMILY == WINAPI_FAMILY_APP)
#        define MSWINRT_FLAVOR
#      endif
#    endif
#    define mach_type_known
#  endif
#  if defined(_MSC_VER) && defined(_M_IA64)
#    define IA64
/*
 * Really Win64, but we do not treat 64-bit variants as a different
 * platform.
 */
#    define MSWIN32
#  endif
#endif /* !_WIN32_WCE && !__CEGCC__ && !__MINGW32CE__ */
#if defined(__DJGPP__)
#  define I386
#  ifndef DJGPP
/* MSDOS running the DJGPP port of gcc. */
#    define DJGPP
#  endif
#  define mach_type_known
#endif
#if defined(__CYGWIN32__) || defined(__CYGWIN__)
#  if defined(__LP64__)
#    define X86_64
#  else
#    define I386
#  endif
#  define CYGWIN32
#  define mach_type_known
#endif /* __CYGWIN__ */
#if defined(__INTERIX)
#  define I386
#  define INTERIX
#  define mach_type_known
#endif
#if defined(_UTS) && !defined(mach_type_known)
#  define S370
#  define UTS4
#  define mach_type_known
#endif
#if defined(__embedded__) && defined(PPC)
#  define POWERPC
#  define NOSYS
#  define mach_type_known
#endif
#if defined(__WATCOMC__) && defined(__386__)
#  define I386
#  if !defined(OS2) && !defined(MSWIN32) && !defined(DOS4GW)
#    if defined(__OS2__)
#      define OS2
#    elif defined(__WINDOWS_386__) || defined(__NT__)
#      define MSWIN32
#    else
#      define DOS4GW
#    endif
#  endif
#  define mach_type_known
#endif /* __WATCOMC__ && __386__ */
#if defined(__GNU__) && defined(__i386__)
/* The Debian Hurd running on generic PC. */
#  define HURD
#  define I386
#  define mach_type_known
#endif
#if defined(__GNU__) && defined(__x86_64__)
#  define HURD
#  define X86_64
#  define mach_type_known
#endif
#if defined(__TANDEM)
/* Nonstop S-series. */
/* FIXME: Should recognize Integrity series? */
#  define MIPS
#  define NONSTOP
#  define mach_type_known
#endif
#if defined(__tile__) && defined(LINUX)
#  ifdef __tilegx__
#    define TILEGX
#  else
#    define TILEPRO
#  endif
#  define mach_type_known
#endif
#if defined(NN_BUILD_TARGET_PLATFORM_NX)
#  define AARCH64
#  define NINTENDO_SWITCH
#  define mach_type_known
#endif
#if defined(__EMSCRIPTEN__) || defined(EMSCRIPTEN)
#  define WEBASSEMBLY
#  ifndef EMSCRIPTEN
#    define EMSCRIPTEN
#  endif
#  define mach_type_known
#endif
#if defined(__wasi__)
/* The WebAssembly System Interface (WASI). */
#  define WEBASSEMBLY
#  define WASI
#  define mach_type_known
#endif

#if defined(__aarch64__)                                      \
    && (defined(ANY_BSD) || defined(COSMO) || defined(DARWIN) \
        || defined(LINUX) || defined(KOS) || defined(QNX)     \
        || defined(SERENITY))
#  define AARCH64
#  define mach_type_known
#elif defined(__arc__) && defined(LINUX)
#  define ARC
#  define mach_type_known
#elif (defined(__arm) || defined(__arm__) || defined(__arm32__)               \
       || defined(__ARM__))                                                   \
    && (defined(ANY_BSD) || defined(DARWIN) || defined(LINUX) || defined(QNX) \
        || defined(NN_PLATFORM_CTR) || defined(SN_TARGET_PSP2))
#  define ARM32
#  define mach_type_known
#elif defined(__avr32__) && defined(LINUX)
#  define AVR32
#  define mach_type_known
#elif defined(__cris__) && defined(LINUX)
#  ifndef CRIS
#    define CRIS
#  endif
#  define mach_type_known
#elif defined(__e2k__) && defined(LINUX)
#  define E2K
#  define mach_type_known
#elif defined(__hexagon__) && defined(LINUX)
#  define HEXAGON
#  define mach_type_known
#elif (defined(__i386__) || defined(i386) || defined(__X86__)) \
    && (defined(ANY_BSD) || defined(DARWIN) || defined(EMBOX)  \
        || defined(LINUX) || defined(QNX) || defined(SERENITY))
#  define I386
#  define mach_type_known
#elif (defined(__ia64) || defined(__ia64__)) && defined(LINUX)
#  define IA64
#  define mach_type_known
#elif defined(__loongarch__) && defined(LINUX)
#  define LOONGARCH
#  define mach_type_known
#elif defined(__m32r__) && defined(LINUX)
#  define M32R
#  define mach_type_known
#elif ((defined(__m68k__) || defined(m68k))      \
       && (defined(NETBSD) || defined(OPENBSD))) \
    || (defined(__mc68000__) && defined(LINUX))
#  define M68K
#  define mach_type_known
#elif (defined(__mips) || defined(_mips) || defined(mips)) \
    && (defined(ANY_BSD) || defined(LINUX))
#  define MIPS
#  define mach_type_known
#elif (defined(__NIOS2__) || defined(__NIOS2) || defined(__nios2__)) \
    && defined(LINUX)
#  define NIOS2 /*< Altera NIOS2 */
#  define mach_type_known
#elif defined(__or1k__) && defined(LINUX)
#  define OR1K /*< OpenRISC (or1k) */
#  define mach_type_known
#elif (defined(__powerpc__) || defined(__powerpc64__) || defined(__ppc__) \
       || defined(__ppc64__) || defined(powerpc) || defined(powerpc64))   \
    && (defined(ANY_BSD) || defined(DARWIN) || defined(LINUX))
#  define POWERPC
#  define mach_type_known
#elif defined(__riscv) && (defined(ANY_BSD) || defined(LINUX))
#  define RISCV
#  define mach_type_known
#elif defined(__s390__) && defined(LINUX)
#  define S390
#  define mach_type_known
#elif defined(__sh__) \
    && (defined(LINUX) || defined(NETBSD) || defined(OPENBSD))
#  define SH
#  define mach_type_known
#elif (defined(__sparc) || defined(sparc)) \
    && (defined(ANY_BSD) || defined(LINUX))
#  define SPARC
#  define mach_type_known
#elif defined(__sw_64__) && defined(LINUX)
#  define SW_64
#  define mach_type_known
#elif (defined(__x86_64) || defined(__x86_64__) || defined(__amd64__) \
       || defined(__X86_64__))                                        \
    && (defined(ANY_BSD) || defined(COSMO) || defined(DARWIN)         \
        || defined(LINUX) || defined(QNX) || defined(SERENITY))
#  define X86_64
#  define mach_type_known
#endif

/*
 * Feel free to add more clauses here.  Or manually define the machine
 * type here.  A machine type is characterized by the architecture.
 * Some machine types are further subdivided by OS.  Macros such as
 * `LINUX`, `FREEBSD`, etc. distinguish them.  The distinction in these
 * cases is usually the stack starting address.
 */

#if !defined(mach_type_known) && !defined(CPPCHECK)
#  error The collector has not been ported to this machine/OS combination
#endif

/*
 * The CPU architecture mapping is:
 *   - `AARCH64`: ARM AArch64 ILP32/64-bit (running `COSMO` environment,
 *     `DARWIN` (OS X or iOS), `KOS`, `LINUX`, `MSWIN32`, `NETBSD`,
 *     `NINTENDO_SWITCH`, `NOSYS` environment, `OPENBSD`, `QNX`, `SERENITY`);
 *   - `ALPHA`: DEC Alpha (running `FREEBSD`, `LINUX`, `NETBSD`, `OPENBSD`,
 *     `OSF1`);
 *   - `ARC`: Synopsys ARC (running `LINUX`);
 *   - `ARM32`: ARMv7 (running `DARWIN` (iOS), `FREEBSD`, `LINUX`, `MSWIN32`,
 *     `MSWINCE`, `NETBSD`, `NN_PLATFORM_CTR`, `NOSYS` environment,
 *     `OPENBSD`, `QNX`, `SN_TARGET_PSP2`, `SYMBIAN`);
 *   - `AVR32`: Atmel RISC (running `LINUX`);
 *   - `CRIS`: Axis Etrax (running `LINUX`);
 *   - `E2K`: Elbrus 2000 32/64-bit (running `LINUX`);
 *   - `HEXAGON`: Qualcomm Hexagon (running `LINUX`);
 *   - `HP_PA`: HP9000/700 and HP9000/800 32/64-bit (running `HPUX`, `LINUX`,
 *     `OPENBSD`);
 *   - `I386`: Intel 486/586/686 (running `BSDI`, `CYGWIN32` environment,
 *     `DARWIN` (macOS), `DGUX`, `DJGPP` environment, `DOS4GW` environment,
 *     `EMBOX`, `FREEBSD`, `HAIKU`, `HURD`, `INTERIX`, `LINUX`, `MSWIN32`,
 *     `MSWINCE`, `NACL` environment, `NETBSD`, `NEXT`, `OPENBSD`, `OS2`,
 *     `QNX`, `RTEMS`, `SCO`, `SCO_ELF`, `SEQUENT`, `SERENITY`, `SOLARIS`,
 *     `THREE86BSD`);
 *   - `IA64`: Intel IPF, e.g. Itanium 32/64-bit (running `HPUX`, `LINUX`,
 *     `MSWIN32`);
 *   - `LOONGARCH`: Loongson LoongArch 32/64-bit (running `LINUX`);
 *   - `M32R`: Renesas M32R (running `LINUX`);
 *   - `M68K`: Motorola 680x0 (running `LINUX`, `NETBSD`, `NEXT`, `OPENBSD`);
 *   - `M88K`: Motorola 88xx0 (running `CX_UX`, `DGUX`);
 *   - `MIPS`: R2000+ 32/64-bit (running `EWS4800`, `FREEBSD`, `IRIX5`,
 *     `LINUX`, `MSWINCE`, `NETBSD`, `NONSTOP`, `OPENBSD`, `ULTRIX`);
 *   - `NIOS2`: Altera NIOS2 (running `LINUX`);
 *   - `OR1K`: OpenRISC/or1k (running `LINUX`);
 *   - `POWERPC`: IBM/Apple PowerPC 32/64-bit (running `AIX`, `DARWIN`,
 *     `FREEBSD`, `LINUX`, `NETBSD`, `NOSYS` environment, `OPENBSD`,
 *     `SN_TARGET_PS3`);
 *   - `RISCV`: RISC-V 32/64-bit (running `FREEBSD`, `LINUX`, `NETBSD`,
 *     `NOSYS` environment, `OPENBSD`);
 *   - `S370`: A 370-like machine (running `UTS4`);
 *   - `S390`: A 390-like machine 32/64-bit (running `LINUX`);
 *   - `SH`: Hitachi SuperH (running `LINUX`, `MSWINCE`, `NETBSD`,
 *     `OPENBSD`);
 *   - `SH4`: Hitachi SH4 (running `MSWINCE`);
 *   - `SPARC`: SPARC v7/v8/v9 32/64-bit (running `DRSNX`, `FREEBSD`,
 *     `LINUX`, `NETBSD`, `OPENBSD`, `SOLARIS`);
 *   - `SW_64`: Sunway/Shenwei (running `LINUX`);
 *   - `TILEGX`: Tilera TILE-Gx 32/64-bit (running `LINUX`);
 *   - `TILEPRO`: Tilera TILEPro (running `LINUX`);
 *   - `VAX`: DEC VAX (running `BSD`, `ULTRIX`);
 *   - `WEBASSEMBLY`: WebAssembly/Wasm (running `EMSCRIPTEN` environment,
 *     `WASI` environment);
 *   - `X86_64`: AMD x86-64 ILP32/64-bit (running `COSMO` environment,
 *     `CYGWIN32` environment, `DARWIN` (macOS), `FREEBSD`, `HAIKU`,
 *     `HURD`, `LINUX`, `MSWIN32`, `MSWIN_XBOX1`, `NETBSD`, `OPENBSD`,
 *     `PLATFORM_GETMEM` environment, `QNX`, `SERENITY`, `SOLARIS`).
 */

/*
 * For each architecture and OS, the following need to be defined:
 *
 * `CPP_WORDSZ` is a simple integer constant representing the word size
 * in bits.  We assume byte addressability, where a byte has 8 bits.
 * We also assume `CPP_WORDSZ` is either 32 or 64.
 * (We care about the length of a pointer address, not hardware
 * bus widths.  Thus a 64-bit processor with a C compiler that uses
 * 32-bit pointers should use `CPP_WORDSZ` of 32, not 64.)
 *
 * `CPP_PTRSZ` is the pointer size in bits.  For most of the supported
 * targets, it is equal to `CPP_WORDSZ`.
 *
 * `MACH_TYPE` is a string representation of the machine type.
 * `OS_TYPE` is analogous for the OS.
 *
 * `ALIGNMENT` is the largest `n`, such that all pointer are guaranteed
 * to be aligned on `n`-byte boundary.  Defining it to be 1 will always
 * work, but will perform poorly.  Should not be larger than size of
 * a pointer.
 *
 * `DATASTART` is the beginning of the data segment.  On some platforms
 * `SEARCH_FOR_DATA_START` is defined.  The latter will cause
 * `GC_data_start` to be set to an address determined by accessing data
 * backwards from `_end` until an unmapped page is found.  `DATASTART` will
 * be defined to be `GC_data_start`.   On UNIX-like systems, the collector
 * will scan the area between `DATASTART` and `DATAEND` for root pointers.
 *
 * `DATAEND`, if not `end`, where `end` is defined as `extern int end[]`.
 * RTH suggests gaining access to linker script synth'd values with
 * this idiom instead of `&end`, where `end` is defined as `extern int end`.
 * Otherwise, "gcc will assume these are in .sdata/.sbss" and it will, e.g.,
 * cause failures on `alpha*-*-*` with `-msmall-data` or `-fpic` or
 * `mips-*-*` without any special options.
 *
 * `STACKBOTTOM` is the cold end of the stack, which is usually the
 * highest address in the stack.
 * Under OS/2, we have other ways of finding thread stacks.
 * For each machine, the following should:
 *   1. Define `STACK_GROWS_UP` if the stack grows toward higher addresses;
 *   2. Define exactly one of
 *      - `STACKBOTTOM` (should be defined to be an expression),
 *      - `HEURISTIC1`,
 *      - `SPECIFIC_MAIN_STACKBOTTOM`,
 *      - `HEURISTIC2`.
 *
 * If `STACKBOTTOM` is defined, then its value will be used directly (as
 * the stack bottom).  If `SPECIFIC_MAIN_STACKBOTTOM` is defined, then it
 * will be determined with a specific method appropriate for the operating
 * system.  Currently we look first for `__libc_stack_end` (currently only
 * if `USE_LIBC_PRIVATES` is defined), and if that fails, read it from
 * `/proc` pseudo-file.  (If `USE_LIBC_PRIVATES` is not defined and
 * `NO_PROC_STAT` is defined, we revert to `HEURISTIC2`.)
 * If either of the last two macros are defined, then `STACKBOTTOM` is
 * computed during collector startup using one of the following two
 * heuristics:
 *   - `HEURISTIC1`: Take an address inside `GC_init`'s frame, and round it
 *     up to the next multiple of `STACK_GRAN`;
 *   - `HEURISTIC2`: Take an address inside `GC_init`'s frame, increment it
 *     repeatedly in small steps (decrement if `STACK_GROWS_UP`), and read
 *     the value at each location, remember the value when the first
 *     Segmentation violation or Bus error is signaled, round that to the
 *     nearest plausible page boundary, and use that instead of
 *     `STACKBOTTOM`.
 *
 * Gustavo Rodriguez-Rivera points out that on most (all?) UNIX machines,
 * the value of `environ` is a pointer that can serve as `STACKBOTTOM`.
 * I expect that `HEURISTIC2` can be replaced by this approach, which
 * interferes far less with debugging.  However it has the disadvantage that
 * it is confused by a `putenv()` call before the collector is initialized.
 * This could be dealt with by intercepting `putenv()`...
 *
 * If no expression for `STACKBOTTOM` can be found, and neither of the above
 * heuristics are usable, the collector can still be used with all of the
 * above undefined, provided one of the following is done:
 *   1. `GC_mark_roots` can be changed to somehow mark from the correct
 *      stack(s) without reference to `STACKBOTTOM`.  This is appropriate for
 *      use in conjunction with thread packages, since there will be multiple
 *      stacks.  (Allocating thread stacks in the heap, and treating them as
 *      ordinary heap data objects is also possible as a last resort.
 *      However, this is likely to introduce significant amounts of excess
 *      storage retention unless the dead parts of the thread stacks are
 *      periodically cleared.)
 *   2. Client code may set `GC_stackbottom` before calling any `GC_`
 *      routines.  If the author of the client code owns the main program,
 *      this could be accomplished by introducing a new `main` function,
 *      calling `GC_call_with_gc_active()` which sets `GC_stackbottom` and
 *      then calls the original (real) `main` function.
 *
 * Each architecture may also define the style of virtual dirty bit
 * implementation to be used:
 *   - `GWW_VDB`: use Win32 `GetWriteWatch` primitive;
 *   - `MPROTECT_VDB`: write-protect the heap and catch faults;
 *   - `PROC_VDB`: use the SVR4 `/proc` primitives to read dirty bits;
 *   - `SOFT_VDB`: use the Linux `/proc` primitives to track dirty bits.
 *
 * The first and second one may be combined, in which case a runtime
 * selection will be made, based on `GetWriteWatch` availability.
 *
 * An architecture may define `DYNAMIC_LOADING` if `dyn_load.c` file
 * implements `GC_register_dynamic_libraries()` for the architecture.
 *
 * An architecture may define `PREFETCH(x)` to preload the cache with `*x`.
 * This defaults to gcc built-in operation (or a no-op for other compilers).
 *
 * `GC_PREFETCH_FOR_WRITE(x)` is used if `*x` is about to be written.
 *
 * An architecture may also define `CLEAR_DOUBLE(x)` to be a fast way to
 * clear 2 pointers at `GC_malloc`-aligned address `x`.  The default
 * implementation is just to store two `NULL` pointers.
 *
 * `HEAP_START` may be defined as the initial address hint for `mmap`-based
 * allocation.
 */

#ifdef LINUX
/* TODO: FreeBSD too? */
EXTERN_C_END
#  include <features.h> /*< for `__GLIBC__` and `__GLIBC_MINOR__`, at least */
EXTERN_C_BEGIN
#endif

/* Convenient internal macro to test `glibc` version (if compiled against). */
#if defined(__GLIBC__) && defined(__GLIBC_MINOR__)
#  define GC_GLIBC_PREREQ(major, minor) \
    ((__GLIBC__ << 8) + __GLIBC_MINOR__ >= ((major) << 8) + (minor))
#else
#  define GC_GLIBC_PREREQ(major, minor) 0 /*< `FALSE` */
#endif

/*
 * Align a `ptr_t` pointer down/up to a given boundary.  The latter should
 * be a power of two.
 */
#if GC_CLANG_PREREQ(11, 0)
#  define PTR_ALIGN_DOWN(p, b) __builtin_align_down(p, b)
#  if defined(DARWIN) && defined(GC_SINGLE_OBJ_BUILD) && GC_CLANG_PREREQ(17, 0)
/* Workaround a crash in Apple clang-17. */
/* TODO: Disable for later clang versions when the bug is fixed. */
#    define PTR_ALIGN_UP(p, b) \
      ((ptr_t)__builtin_align_up((GC_uintptr_t)(p), b))
#  else
#    define PTR_ALIGN_UP(p, b) __builtin_align_up(p, b)
#  endif
#else
#  define PTR_ALIGN_DOWN(p, b) \
    ((ptr_t)((GC_uintptr_t)(p) & ~((GC_uintptr_t)(b) - (GC_uintptr_t)1)))
#  define PTR_ALIGN_UP(p, b)                                           \
    ((ptr_t)(((GC_uintptr_t)(p) + (GC_uintptr_t)(b) - (GC_uintptr_t)1) \
             & ~((GC_uintptr_t)(b) - (GC_uintptr_t)1)))
#endif

/*
 * If available, we can use `__builtin_unwind_init()` to push the relevant
 * registers onto the stack.
 */
#if GC_GNUC_PREREQ(2, 8)                                                   \
    && !GC_GNUC_PREREQ(11, 0) /*< broken at least in 11.2.0 on cygwin64 */ \
    && !defined(__INTEL_COMPILER) && !defined(__PATHCC__)                  \
    && !defined(__FUJITSU)                    /*< for FX10 system */       \
    && !(defined(POWERPC) && defined(DARWIN)) /*< for MacOS X 10.3.9 */    \
    && !defined(E2K) && !defined(RTEMS)                                    \
    && !defined(__ARMCC_VERSION) /*< does not exist in armcc gnu emu */    \
    && !(defined(__clang__)                                                \
         && defined(__ARM_ARCH_5TE__) /* clang-19 emits `vpush`/`vpop` */) \
    && (!defined(__clang__)                                                \
        || GC_CLANG_PREREQ(8, 0) /* was no-op in clang-3 at least */)
#  define HAVE_BUILTIN_UNWIND_INIT
#endif

#if (defined(__CC_ARM) || defined(CX_UX) || defined(DJGPP) || defined(EMBOX) \
     || defined(EWS4800) || defined(LINUX) || defined(OS2) || defined(RTEMS) \
     || defined(UTS4) || defined(MSWIN32) || defined(MSWINCE)                \
     || (defined(NOSYS) && defined(RISCV)))                                  \
    && !defined(NO_UNDERSCORE_SETJMP)
#  define NO_UNDERSCORE_SETJMP
#endif

/*
 * The common OS-specific definitions.  Should be applicable to all
 * (or most, at least) supported architectures.
 */

#ifdef CYGWIN32
#  define OS_TYPE "CYGWIN32"
#  define RETRY_GET_THREAD_CONTEXT
#  ifdef USE_WINALLOC
#    define GWW_VDB
#  elif defined(USE_MMAP)
#    define USE_MMAP_ANON
#  endif
#endif /* CYGWIN32 */

#ifdef COSMO
#  define OS_TYPE "COSMO"
#  ifndef USE_GET_STACKBASE_FOR_MAIN
#    define USE_GET_STACKBASE_FOR_MAIN
#  endif
extern int __data_start[] __attribute__((__weak__));
#  define DATASTART ((ptr_t)__data_start)
extern int _end[];
#  define DATAEND ((ptr_t)_end)
#  define USE_MMAP_ANON
#  ifndef HAVE_CLOCK_GETTIME
#    define HAVE_CLOCK_GETTIME 1
#  endif
#  ifndef HAVE_PTHREAD_SETNAME_NP_WITH_TID
/* Normally should be defined by `configure`, etc. */
#    define HAVE_PTHREAD_SETNAME_NP_WITH_TID 1
#  endif
#  if !defined(GC_THREADS) || defined(NO_HANDLE_FORK) \
      || defined(GC_NO_CAN_CALL_ATFORK)
#    define MPROTECT_VDB
/* FIXME: Otherwise `gctest` crashes in child process. */
#  endif
/* FIXME: A deadlock occurs in markers, thus disabled for now. */
#  undef PARALLEL_MARK
#endif /* COSMO */

#ifdef DARWIN
#  define OS_TYPE "DARWIN"
#  define DYNAMIC_LOADING
/*
 * TODO: See `get_end(3)`, `get_etext` and `get_end` should not be used.
 * These are not used when `dyld` support is enabled (it is the default).
 */
#  define DATASTART ((ptr_t)get_etext())
#  define DATAEND ((ptr_t)get_end())
#  define USE_MMAP_ANON
/* There seems to be some issues with try-lock hanging on Darwin. */
/* TODO: This should be looked into some more. */
#  define NO_PTHREAD_TRYLOCK
#  ifndef TARGET_OS_XR
#    define TARGET_OS_XR 0
#  endif
#  ifndef TARGET_OS_VISION
#    define TARGET_OS_VISION 0
#  endif
#endif /* DARWIN */

#ifdef EMBOX
#  define OS_TYPE "EMBOX"
extern int _modules_data_start[], _apps_bss_end[];
#  define DATASTART ((ptr_t)_modules_data_start)
#  define DATAEND ((ptr_t)_apps_bss_end)
/*
 * Note: the designated area might be quite large (several dozens of MBs)
 * as it includes `.data` and `.bss` of all apps and modules of the built
 * binary image.
 */
#endif /* EMBOX */

#ifdef FREEBSD
#  define OS_TYPE "FREEBSD"
#  define SPECIFIC_MAIN_STACKBOTTOM
#  ifdef __ELF__
#    define DYNAMIC_LOADING
#  endif
#  ifndef USE_MMAP
/* `sbrk()` is not available. */
#    define USE_MMAP 1
#  endif
#  if !defined(ALPHA) && !defined(SPARC)
extern char etext[];
#    define DATASTART GC_SysVGetDataStart(0x1000, (ptr_t)etext)
#    define DATASTART_USES_XGETDATASTART
#    ifndef REDIRECT_MALLOC
#      define MPROTECT_VDB
#    else
/* Similar as on Linux, `fread()` might use `malloc()`. */
#    endif
#  endif
#endif /* FREEBSD */

#ifdef HAIKU
#  define OS_TYPE "HAIKU"
#  define DYNAMIC_LOADING
/* Note: `DATASTART` is not used really, see `GC_register_main_static_data`. */
extern int etext[];
#  define DATASTART PTR_ALIGN_UP((ptr_t)etext, 0x1000)
#  ifndef USE_GET_STACKBASE_FOR_MAIN
#    define USE_GET_STACKBASE_FOR_MAIN
#  endif
#  define USE_MMAP_ANON
/*
 * On Haiku R1, at least, `pthreads` locks never spin but always call
 * into the kernel if the lock cannot be acquired with a simple atomic
 * operation.  (Up to 5x overall performance improvement of the
 * collector is observed by forcing use of spin locks.)
 */
#  ifndef USE_SPIN_LOCK
#    define USE_SPIN_LOCK
#  endif
/*
 * TODO: `MPROTECT_VDB` is not working correctly on anything other than
 * recent nightly Haiku OS builds (as of Nov 2024), and also it is
 * considerably slower than regular collecting, so do not enable it for now.
 */
EXTERN_C_END
#  include <OS.h>
EXTERN_C_BEGIN
#  define GETPAGESIZE() (unsigned)B_PAGE_SIZE
#  ifndef HAVE_CLOCK_GETTIME
#    define HAVE_CLOCK_GETTIME 1
#  endif
#endif /* HAIKU */

#ifdef HPUX
#  define OS_TYPE "HPUX"
extern int __data_start[];
#  define DATASTART ((ptr_t)__data_start)
#  ifdef USE_MMAP
#    define USE_MMAP_ANON
#  endif
#  define DYNAMIC_LOADING
#  define GETPAGESIZE() (unsigned)sysconf(_SC_PAGE_SIZE)
#endif /* HPUX */

#ifdef HURD
#  define OS_TYPE "HURD"
#  define HEURISTIC2
#  define SEARCH_FOR_DATA_START
extern int _end[];
#  define DATAEND ((ptr_t)_end)
/* TODO: `MPROTECT_VDB` is not quite working yet? */
#  define DYNAMIC_LOADING
#  define USE_MMAP_ANON
#endif /* HURD */

#ifdef LINUX
#  define OS_TYPE "LINUX"
#  if defined(FORCE_MPROTECT_BEFORE_MADVISE) || defined(PREFER_MMAP_PROT_NONE)
#    define COUNT_UNMAPPED_REGIONS
#  endif
#  define RETRY_TKILL_ON_EAGAIN
#  if !defined(MIPS) && !defined(POWERPC)
#    define SPECIFIC_MAIN_STACKBOTTOM
#  endif
#  if defined(__ELF__) && !defined(IA64)
#    define DYNAMIC_LOADING
#  endif
#  if defined(__ELF__) && !defined(ARC) && !defined(RISCV) && !defined(S390) \
      && !defined(TILEGX) && !defined(TILEPRO)
extern int _end[];
#    define DATAEND ((ptr_t)_end)
#  endif
#  if !defined(REDIRECT_MALLOC) && !defined(E2K)
/* Requires Linux 2.3.47 or later. */
#    define MPROTECT_VDB
#  else
/*
 * We seem to get random errors in the incremental mode, possibly because
 * the Linux threads implementation itself is a `malloc` client and cannot
 * deal with the signals.  `fread()` uses `malloc()` too.
 * In case of e2k, unless `-fsemi-spec-ld` (or `-O0`) option is passed
 * to gcc (both when compiling the collector library and the client),
 * a semi-speculative optimization may lead to `SIGILL` (with `ILL_ILLOPN`
 * `si_code`) instead of `SIGSEGV`.
 */
#  endif
#endif /* LINUX */

#ifdef KOS
#  define OS_TYPE "KOS"
#  ifndef USE_GET_STACKBASE_FOR_MAIN
/* Note: this requires `-lpthread` option. */
#    define USE_GET_STACKBASE_FOR_MAIN
#  endif
extern int __data_start[];
#  define DATASTART ((ptr_t)__data_start)
#endif /* KOS */

#ifdef MSWIN32
#  define OS_TYPE "MSWIN32"
/* `STACKBOTTOM` and `DATASTART` are handled specially in `os_dep.c` file. */
#  if !defined(CPPCHECK)
#    define DATAEND /*< not needed */
#  endif
#  if defined(USE_GLOBAL_ALLOC) && !defined(MSWINRT_FLAVOR)
/* Cannot pass `MEM_WRITE_WATCH` to `GlobalAlloc()`. */
#  else
#    define GWW_VDB
#  endif
#endif

#ifdef MSWINCE
#  define OS_TYPE "MSWINCE"
#  if !defined(CPPCHECK)
#    define DATAEND /*< not needed */
#  endif
#endif

#ifdef NACL
#  define OS_TYPE "NACL"
#  if defined(__GLIBC__)
#    define DYNAMIC_LOADING
#  endif
#  define DATASTART MAKE_CPTR(0x10020000)
extern int _end[];
#  define DATAEND ((ptr_t)_end)
#  define STACK_GRAN 0x10000
#  define HEURISTIC1
#  define NO_PTHREAD_GETATTR_NP
#  define USE_MMAP_ANON
/* FIXME: Not real page size. */
#  define GETPAGESIZE() 65536
#  define MAX_NACL_GC_THREADS 1024
#endif /* NACL */

#ifdef NETBSD
#  define OS_TYPE "NETBSD"
#  define HEURISTIC2
#  ifdef __ELF__
#    define SEARCH_FOR_DATA_START
#    define DYNAMIC_LOADING
#  elif !defined(MIPS)
/* TODO: Probably do not exclude it. */
extern char etext[];
#    define DATASTART ((ptr_t)etext)
#  endif
#  define MPROTECT_VDB
#endif /* NETBSD */

#ifdef NEXT
#  define OS_TYPE "NEXT"
#  define DATASTART ((ptr_t)get_etext())
#  define DATAEND /*< not needed */
#  undef USE_MUNMAP
#endif

#ifdef OPENBSD
#  define OS_TYPE "OPENBSD"
#  ifndef GC_THREADS
#    define HEURISTIC2
#  endif
#  ifdef __ELF__
extern int __data_start[], _end[];
#    define DATASTART ((ptr_t)__data_start)
#    define DATAEND ((ptr_t)_end)
#    define DYNAMIC_LOADING
#  else
extern char etext[];
#    define DATASTART ((ptr_t)etext)
#  endif
#  define MPROTECT_VDB
#endif /* OPENBSD */

#ifdef QNX
#  define OS_TYPE "QNX"
#  define SA_RESTART 0
#  ifndef SPECIFIC_MAIN_STACKBOTTOM
/* TODO: This is not used by default. */
#    define STACK_GRAN 0x1000000
#    define HEURISTIC1
#  endif
extern char etext[];
#  define DATASTART ((ptr_t)etext)
extern int _end[];
#  define DATAEND ((ptr_t)_end)
#endif /* QNX */

#ifdef SERENITY
#  define OS_TYPE "SERENITY"
extern int etext[], _end[];
#  define DATASTART PTR_ALIGN_UP((ptr_t)etext, 0x1000)
#  define DATAEND ((ptr_t)_end)
#  define DYNAMIC_LOADING
/* TODO: Enable `mprotect`-based VDB. */
#  define USE_MMAP_ANON
#endif /* SERENITY */

#ifdef SOLARIS
#  define OS_TYPE "SOLARIS"
extern int _end[];
#  define DATAEND ((ptr_t)_end)
#  if !defined(USE_MMAP) && defined(REDIRECT_MALLOC)
#    define USE_MMAP 1
/*
 * Otherwise we now use `calloc()`.  `mmap()` may result in the heap
 * interleaved with thread stacks, which can result in excessive
 * black-listing.  `sbrk()` is unusable since it does not interact
 * correctly with the system `malloc()`.
 */
#  endif
#  ifdef USE_MMAP
#    define HEAP_START ((word)0x40000000)
#  else
#    define HEAP_START ADDR(DATAEND)
#  endif
#  ifndef GC_THREADS
#    define MPROTECT_VDB
#  endif
#  define DYNAMIC_LOADING
/*
 * Define `STACKBOTTOM` as `(ptr_t)_start` worked through 2.7, but
 * reportedly breaks under 2.8.  It appears that the stack base is
 * a property of the executable, so this should not break old executables.
 * `HEURISTIC1` reportedly no longer works under Solaris 2.7.
 * `HEURISTIC2` probably works, but this appears to be preferable.
 * Apparently `USRSTACK` is defined to be `USERLIMIT`, but in some
 * installations that is undefined.  We work around this with a gross hack.
 */
EXTERN_C_END
#  include <sys/vmparam.h>
EXTERN_C_BEGIN
#  ifdef USERLIMIT
/* This should work everywhere, but does not. */
#    define STACKBOTTOM ((ptr_t)USRSTACK)
#  else
#    define HEURISTIC2
#  endif
#endif /* SOLARIS */

#ifdef SYMBIAN
#  define OS_TYPE "SYMBIAN"
#  define DATASTART ((ptr_t)ALIGNMENT) /*< cannot be `NULL` */
#  define DATAEND ((ptr_t)ALIGNMENT)
#  ifndef USE_MMAP
/* `sbrk()` is not available. */
#    define USE_MMAP 1
#  endif
#endif /* SYMBIAN */

/*
 * Below are the definitions specific to each supported architecture
 * and OS, grouped by the former.
 */

#ifdef M68K
#  define MACH_TYPE "M68K"
#  define CPP_WORDSZ 32
#  define ALIGNMENT 2 /*< not 4 */
#  ifdef OPENBSD
/* Nothing specific. */
#  endif
#  ifdef NETBSD
/* Nothing specific. */
#  endif
#  ifdef LINUX
#    ifdef __ELF__
#      if GC_GLIBC_PREREQ(2, 0)
#        define SEARCH_FOR_DATA_START
#      else
/*
 * Hideous kludge: `__environ` is the first word in platform `crt0.o` file,
 * and delimits the start of the data segment, no matter which `ld` options
 * were passed through.  We could use `_etext` instead, but that would
 * include `.rodata`, which may contain large read-only data tables that
 * we would rather not scan.
 */
extern char **__environ;
#        define DATASTART ((ptr_t)(&__environ))
#      endif
#    else
extern int etext[];
#      define DATASTART PTR_ALIGN_UP((ptr_t)etext, 0x1000)
#    endif
#  endif
#  ifdef NEXT
#    define STACKBOTTOM MAKE_CPTR(0x4000000)
#  endif
#endif

#ifdef POWERPC
#  define MACH_TYPE "POWERPC"
#  ifdef LINUX
#    if defined(__powerpc64__)
#      define CPP_WORDSZ 64
#      ifndef HBLKSIZE
#        define HBLKSIZE 4096
#      endif
#    else
#      define CPP_WORDSZ 32
#    endif
/*
 * `HEURISTIC1` has been reliably reported to fail for a 32-bit executable
 * on a 64-bit kernel.
 */
#    if defined(__bg__)
/*
 * The Linux Compute Node Kernel (used on BlueGene systems) does not
 * support the `SPECIFIC_MAIN_STACKBOTTOM` way.
 */
#      define HEURISTIC2
#      define NO_PTHREAD_GETATTR_NP
#    else
#      define SPECIFIC_MAIN_STACKBOTTOM
#    endif
#    define SEARCH_FOR_DATA_START
#    ifndef SOFT_VDB
#      define SOFT_VDB
#    endif
#  endif
#  ifdef DARWIN
#    if defined(__ppc64__)
#      define CPP_WORDSZ 64
#      define STACKBOTTOM MAKE_CPTR(0x7fff5fc00000)
#      define CACHE_LINE_SIZE 64
#      ifndef HBLKSIZE
#        define HBLKSIZE 4096
#      endif
#    else
#      define CPP_WORDSZ 32
#      define STACKBOTTOM MAKE_CPTR(0xc0000000)
#    endif
#    define MPROTECT_VDB
#    if defined(USE_PPC_PREFETCH) && defined(__GNUC__)
/* The performance impact of prefetches is untested. */
#      define PREFETCH(x) \
        __asm__ __volatile__("dcbt 0,%0" : : "r"((const void *)(x)))
#      define GC_PREFETCH_FOR_WRITE(x) \
        __asm__ __volatile__("dcbtst 0,%0" : : "r"((const void *)(x)))
#    endif
#  endif
#  ifdef OPENBSD
#    if defined(__powerpc64__)
#      define CPP_WORDSZ 64
#    else
#      define CPP_WORDSZ 32
#    endif
#  endif
#  ifdef FREEBSD
#    if defined(__powerpc64__)
#      define CPP_WORDSZ 64
#      ifndef HBLKSIZE
#        define HBLKSIZE 4096
#      endif
#    else
#      define CPP_WORDSZ 32
#    endif
#  endif
#  ifdef NETBSD
#    define CPP_WORDSZ 32
#  endif
#  ifdef SN_TARGET_PS3
#    define OS_TYPE "SN_TARGET_PS3"
#    define CPP_WORDSZ 32
#    define NO_GETENV
extern int _end[], __bss_start;
#    define DATASTART ((ptr_t)__bss_start)
#    define DATAEND ((ptr_t)_end)
#    define STACKBOTTOM ((ptr_t)ps3_get_stack_bottom())
void *ps3_get_mem(size_t lb);
#    define GET_MEM(lb) ps3_get_mem(lb)
/*
 * The current `LOCK()` implementation for PS3 explicitly uses
 * `pthread_mutex_lock()` for some reason.
 */
#    define NO_PTHREAD_TRYLOCK
#  endif
#  ifdef AIX
#    define OS_TYPE "AIX"
#    undef ALIGNMENT /*< in case it is defined */
#    undef IA64
/*
 * DOB: some AIX installs stupidly define `IA64` in platform
 * `sys/systemcfg.h` file.
 */
#    ifdef __64BIT__
#      define CPP_WORDSZ 64
#      define STACKBOTTOM MAKE_CPTR(0x1000000000000000)
#    else
#      define CPP_WORDSZ 32
extern int errno;
#      define STACKBOTTOM ((ptr_t)(&errno))
#    endif
#    define USE_MMAP_ANON
/*
 * From AIX linker man page:
 *   - `_text` specifies the first location of the program;
 *   - `_etext` specifies the first location after the program;
 *   - `_data` specifies the first location of the data;
 *   - `_edata` specifies the first location after the initialized data;
 *   - `_end` (or `end`) specifies the first location after all data.
 */
extern int _data[], _end[];
#    define DATASTART ((ptr_t)_data)
#    define DATAEND ((ptr_t)_end)
#    define MPROTECT_VDB
#    define DYNAMIC_LOADING
/*
 * Note: for really old versions of AIX, `DYNAMIC_LOADING` may have to
 * be removed.
 */
#  endif
#  ifdef NOSYS
#    define OS_TYPE "NOSYS"
#    define CPP_WORDSZ 32
extern void __end[], __dso_handle[];
#    define DATASTART ((ptr_t)__dso_handle) /*< OK, that is ugly */
#    define DATAEND ((ptr_t)__end)
/* Note: stack starts at 0xE0000000 for the simulator. */
#    define STACKBOTTOM PTR_ALIGN_UP(GC_approx_sp(), 0x10000000)
#  endif
#endif /* POWERPC */

#ifdef VAX
#  define MACH_TYPE "VAX"
#  define CPP_WORDSZ 32
/* Pointers are `longword`-aligned by C compiler v4.2. */
extern char etext[];
#  define DATASTART ((ptr_t)etext)
#  ifdef BSD
#    define OS_TYPE "BSD"
#    define STACK_GRAN 0x1000000
#    define HEURISTIC1
/* Note: `HEURISTIC2` may be OK, but it is hard to test. */
#  endif
#  ifdef ULTRIX
#    define OS_TYPE "ULTRIX"
#    define STACKBOTTOM MAKE_CPTR(0x7fffc800)
#  endif
#endif /* VAX */

#ifdef SPARC
#  define MACH_TYPE "SPARC"
#  if defined(__arch64__) || defined(__sparcv9)
#    define CPP_WORDSZ 64
#    define ELF_CLASS ELFCLASS64
#  else
#    define CPP_WORDSZ 32
#    define ALIGNMENT 4 /*< required by hardware */
#  endif
#  ifdef SOLARIS
extern int _etext[];
#    define DATASTART GC_SysVGetDataStart(0x10000, (ptr_t)_etext)
#    define PROC_VDB
/*
 * `getpagesize()` appeared to be missing from at least one Solaris 5.4
 * installation.  Weird.
 */
#    define GETPAGESIZE() (unsigned)sysconf(_SC_PAGESIZE)
#  endif
#  ifdef DRSNX
#    define OS_TYPE "DRSNX"
extern int etext[];
#    define DATASTART GC_SysVGetDataStart(0x10000, (ptr_t)etext)
#    define MPROTECT_VDB
#    define STACKBOTTOM MAKE_CPTR(0xdfff0000)
#    define DYNAMIC_LOADING
#  endif
#  ifdef LINUX
extern int _etext[];
#    ifdef __arch64__
#      define DATASTART GC_SysVGetDataStart(0x100000, (ptr_t)_etext)
#    else
#      define DATASTART GC_SysVGetDataStart(0x10000, (ptr_t)_etext)
#    endif
#  endif
#  ifdef OPENBSD
/* Nothing specific. */
#  endif
#  ifdef NETBSD
/* Nothing specific. */
#  endif
#  ifdef FREEBSD
extern char etext[];
#    define DATASTART ((ptr_t)(&etext))
#    define DATAEND ((ptr_t)GC_find_limit(DATASTART, TRUE))
#    define DATAEND_IS_FUNC
#    define GC_HAVE_DATAREGION2
extern char edata[], end[];
#    define DATASTART2 ((ptr_t)(&edata))
#    define DATAEND2 ((ptr_t)(&end))
#  endif
#endif /* SPARC */

#ifdef I386
#  define MACH_TYPE "I386"
#  if (defined(__LP64__) || defined(_WIN64)) && !defined(CPPCHECK)
#    error This should be handled as X86_64
#  endif
#  define CPP_WORDSZ 32
/*
 * The 4-byte alignment appears to hold for all 32-bit compilers
 * except Borland and Watcom.  If using the Borland (bcc32) or
 * Watcom (wcc386) compiler, `-a4` or `-zp4` option, respectively,
 * should be passed to the compiler, both for building the library
 * and client code.  (The alternate solution is to define
 * `FORCE_ALIGNMENT_ONE` macro but this would have significant
 * negative performance implications.)
 */
#  if defined(FORCE_ALIGNMENT_ONE) \
      && (defined(__BORLANDC__) || defined(__WATCOMC__))
#    define ALIGNMENT 1
#  endif
#  ifdef SEQUENT
#    define OS_TYPE "SEQUENT"
extern int etext[];
#    define DATASTART PTR_ALIGN_UP((ptr_t)etext, 0x1000)
#    define STACKBOTTOM MAKE_CPTR(0x3ffff000)
#  endif
#  ifdef HAIKU
/* Nothing specific. */
#  endif
#  ifdef HURD
/* Nothing specific. */
#  endif
#  ifdef EMBOX
/* Nothing specific. */
#  endif
#  ifdef NACL
/* Nothing specific. */
#  endif
#  ifdef QNX
/* Nothing specific. */
#  endif
#  ifdef SERENITY
/* Nothing specific. */
#  endif
#  ifdef SOLARIS
extern int _etext[];
#    define DATASTART GC_SysVGetDataStart(0x1000, (ptr_t)_etext)
#    define PROC_VDB
#  endif
#  ifdef SCO
#    define OS_TYPE "SCO"
extern int etext[];
#    define DATASTART \
      (PTR_ALIGN_UP((ptr_t)etext, 0x400000) + (ADDR(etext) & 0xfff))
#    define STACKBOTTOM MAKE_CPTR(0x7ffffffc)
#  endif
#  ifdef SCO_ELF
#    define OS_TYPE "SCO_ELF"
extern int etext[];
#    define DATASTART ((ptr_t)etext)
#    define STACKBOTTOM MAKE_CPTR(0x8048000)
#    define DYNAMIC_LOADING
#    define ELF_CLASS ELFCLASS32
#  endif
#  ifdef DGUX
#    define OS_TYPE "DGUX"
extern int _etext, _end;
#    define DATASTART GC_SysVGetDataStart(0x1000, (ptr_t)(&_etext))
#    define DATASTART_USES_XGETDATASTART
#    define DATAEND ((ptr_t)(&_end))
#    define HEURISTIC2
#    define DYNAMIC_LOADING
#    ifndef USE_MMAP
#      define USE_MMAP 1
#    endif
#    define MAP_FAILED ((void *)(~(GC_uintptr_t)0))
#    define HEAP_START ((word)0x40000000)
#  endif /* DGUX */
#  ifdef LINUX
/*
 * This encourages `mmap()` to give us low addresses, thus allowing the
 * heap to grow to ~3 GB.
 */
#    define HEAP_START ((word)0x1000)
#    ifdef __ELF__
#      if GC_GLIBC_PREREQ(2, 0) || defined(HOST_ANDROID)
#        define SEARCH_FOR_DATA_START
#      else
/* See the comment of the Linux/m68k case. */
extern char **__environ;
#        define DATASTART ((ptr_t)(&__environ))
#      endif
#      if !defined(GC_NO_SIGSETJMP)                                  \
          && (defined(HOST_TIZEN)                                    \
              || (defined(HOST_ANDROID)                              \
                  && !(GC_GNUC_PREREQ(4, 8) || GC_CLANG_PREREQ(3, 2) \
                       || __ANDROID_API__ >= 18)))
/*
 * Older Android NDK releases lack `sigsetjmp` in x86 `libc` (`setjmp` is
 * used instead to find `data_start`).  The bug is fixed in Android NDK r8e
 * (so, it is OK to use `sigsetjmp` if gcc-4.8+, clang-3.2+ or Android
 * API level 18+).
 */
#        define GC_NO_SIGSETJMP 1
#      endif
#    else
extern int etext[];
#      define DATASTART PTR_ALIGN_UP((ptr_t)etext, 0x1000)
#    endif
#    ifdef USE_I686_PREFETCH
/*
 * Empirically `prefetcht0` is much more effective at reducing cache miss
 * stalls for the targeted load instructions.  But it seems to interfere
 * enough with other cache traffic that the net result is worse than
 * `prefetchnta`.
 */
#      define PREFETCH(x) \
        __asm__ __volatile__("prefetchnta %0" : : "m"(*(char *)(x)))
#      ifdef FORCE_WRITE_PREFETCH
/*
 * Using prefetches for write seems to have a slight negative impact on
 * performance, at least for a PIII/500.
 */
#        define GC_PREFETCH_FOR_WRITE(x) \
          __asm__ __volatile__("prefetcht0 %0" : : "m"(*(char *)(x)))
#      else
#        define GC_NO_PREFETCH_FOR_WRITE
#      endif
#    elif defined(USE_3DNOW_PREFETCH)
#      define PREFETCH(x) \
        __asm__ __volatile__("prefetch %0" : : "m"(*(char *)(x)))
#      define GC_PREFETCH_FOR_WRITE(x) \
        __asm__ __volatile__("prefetchw %0" : : "m"(*(char *)(x)))
#    endif
#    if defined(__GLIBC__) && !defined(__UCLIBC__) \
        && !defined(GLIBC_TSX_BUG_FIXED)
/* Workaround lock elision implementation for some `glibc`. */
#      define GLIBC_2_19_TSX_BUG
EXTERN_C_END
#      include <gnu/libc-version.h> /*< for `gnu_get_libc_version()` */
EXTERN_C_BEGIN
#    endif
#    ifndef SOFT_VDB
#      define SOFT_VDB
#    endif
#  endif
#  ifdef CYGWIN32
#    define WOW64_THREAD_CONTEXT_WORKAROUND
#    define DATASTART ((ptr_t)GC_DATASTART) /*< defined in `gc.h` file */
#    define DATAEND ((ptr_t)GC_DATAEND)
#    ifndef USE_WINALLOC
/* `MPROTECT_VDB` does not work, it leads to a spurious exit. */
#    endif
#  endif
#  ifdef INTERIX
#    define OS_TYPE "INTERIX"
extern int _data_start__[], _bss_end__[];
#    define DATASTART ((ptr_t)_data_start__)
#    define DATAEND ((ptr_t)_bss_end__)
#    define STACKBOTTOM                                        \
      ({                                                       \
        ptr_t rv;                                              \
        __asm__ __volatile__("movl %%fs:4, %%eax" : "=a"(rv)); \
        rv;                                                    \
      })
#    define USE_MMAP_ANON
#  endif
#  ifdef OS2
#    define OS_TYPE "OS2"
/*
 * `STACKBOTTOM` and `DATASTART` are handled specially in `os_dep.c`
 * file.  OS/2 actually has the right system call!
 */
#    define DATAEND /*< not needed */
#    undef USE_MUNMAP
#    define GETPAGESIZE() os2_getpagesize()
#  endif
#  ifdef MSWIN32
#    define WOW64_THREAD_CONTEXT_WORKAROUND
#    define RETRY_GET_THREAD_CONTEXT
#    if defined(__BORLANDC__)
/*
 * TODO: VDB based on `VirtualProtect` and `SetUnhandledExceptionFilter`
 * does not work correctly.
 */
#    else
#      define MPROTECT_VDB
#    endif
#  endif
#  ifdef MSWINCE
/* Nothing specific. */
#  endif
#  ifdef DJGPP
#    define OS_TYPE "DJGPP"
EXTERN_C_END
#    include "stubinfo.h"
EXTERN_C_BEGIN
extern int etext[];
#    define DATASTART PTR_ALIGN_UP((ptr_t)etext, 0x200)
extern int __djgpp_stack_limit, _stklen;
#    define STACKBOTTOM (MAKE_CPTR(__djgpp_stack_limit) + _stklen)
#  endif
#  ifdef OPENBSD
/* Nothing specific. */
#  endif
#  ifdef FREEBSD
#    if defined(__GLIBC__)
extern int _end[];
#      define DATAEND ((ptr_t)_end)
#    endif
#  endif
#  ifdef NETBSD
/* Nothing specific. */
#  endif
#  ifdef THREE86BSD
#    define OS_TYPE "THREE86BSD"
#    define HEURISTIC2
extern char etext[];
#    define DATASTART ((ptr_t)etext)
#  endif
#  ifdef BSDI
#    define OS_TYPE "BSDI"
#    define HEURISTIC2
extern char etext[];
#    define DATASTART ((ptr_t)etext)
#  endif
#  ifdef NEXT
#    define STACKBOTTOM MAKE_CPTR(0xc0000000)
#  endif
#  ifdef RTEMS
#    define OS_TYPE "RTEMS"
EXTERN_C_END
#    include <sys/unistd.h>
EXTERN_C_BEGIN
extern int etext[];
#    define DATASTART ((ptr_t)etext)
void *rtems_get_stack_bottom(void);
#    define InitStackBottom rtems_get_stack_bottom()
#    define STACKBOTTOM ((ptr_t)InitStackBottom)
#    undef USE_MUNMAP
#  endif
#  ifdef DOS4GW
#    define OS_TYPE "DOS4GW"
extern long __nullarea;
extern char _end;
extern char *_STACKTOP;
/*
 * Depending on calling conventions Watcom C either precedes or does not
 * precede the names of the C variables with an underscore.
 * Make sure the startup code variables always have the same name.
 */
#    pragma aux __nullarea "*";
#    pragma aux _end "*";
#    define STACKBOTTOM ((ptr_t)_STACKTOP) /*< confused? me too */
#    define DATASTART ((ptr_t)(&__nullarea))
#    define DATAEND ((ptr_t)(&_end))
#    undef USE_MUNMAP
#    define GETPAGESIZE() 4096
#  endif
#  ifdef DARWIN
#    define DARWIN_DONT_PARSE_STACK 1
#    define STACKBOTTOM MAKE_CPTR(0xc0000000)
#    define MPROTECT_VDB
#  endif
#endif /* I386 */

#ifdef LOONGARCH
#  define MACH_TYPE "LOONGARCH"
#  define CPP_WORDSZ (__SIZEOF_SIZE_T__ * 8)
#  ifdef LINUX
#    pragma weak __data_start
extern int __data_start[];
#    define DATASTART ((ptr_t)__data_start)
#  endif
#endif /* LOONGARCH */

#ifdef SW_64
#  define MACH_TYPE "SW_64"
#  define CPP_WORDSZ 64
#  ifdef LINUX
/* Nothing specific. */
#  endif
#endif /* SW_64 */

#ifdef MIPS
#  define MACH_TYPE "MIPS"
#  ifdef LINUX
#    ifdef _MIPS_SZPTR
#      define CPP_WORDSZ _MIPS_SZPTR
#    else
#      define CPP_WORDSZ 32
#    endif
#    pragma weak __data_start
extern int __data_start[];
#    define DATASTART ((ptr_t)__data_start)
#    ifndef HBLKSIZE
#      define HBLKSIZE 4096
#    endif
#    if GC_GLIBC_PREREQ(2, 2)
#      define SPECIFIC_MAIN_STACKBOTTOM
#    else
#      define STACKBOTTOM MAKE_CPTR(0x7fff8000)
#    endif
#  endif
#  ifdef EWS4800
#    define OS_TYPE "EWS4800"
#    define HEURISTIC2
#    if defined(_MIPS_SZPTR) && (_MIPS_SZPTR == 64)
#      define CPP_WORDSZ _MIPS_SZPTR
extern int _fdata[], _end[];
#      define DATASTART ((ptr_t)_fdata)
#      define DATAEND ((ptr_t)_end)
#    else
#      define CPP_WORDSZ 32
extern int etext[], edata[];
#      define DATASTART \
        (PTR_ALIGN_UP((ptr_t)etext, 0x40000) + (ADDR(etext) & 0xffff))
#      define DATAEND ((ptr_t)edata)
#      define GC_HAVE_DATAREGION2
extern int _DYNAMIC_LINKING[], _gp[];
#      define DATASTART2                                               \
        (_DYNAMIC_LINKING ? PTR_ALIGN_UP((ptr_t)_gp + 0x8000, 0x40000) \
                          : (ptr_t)edata)
extern int end[];
#      define DATAEND2 ((ptr_t)end)
#    endif
#  endif
#  ifdef ULTRIX
#    define OS_TYPE "ULTRIX"
#    define CPP_WORDSZ 32
#    define HEURISTIC2
/*
 * Note: the actual beginning of the data segment could probably be
 * slightly higher since startup code allocates lots of stuff.
 */
#    define DATASTART MAKE_CPTR(0x10000000)
#  endif
#  ifdef IRIX5
#    define OS_TYPE "IRIX5"
#    ifdef _MIPS_SZPTR
#      define CPP_WORDSZ _MIPS_SZPTR
#    else
#      define CPP_WORDSZ 32
#    endif
#    define HEURISTIC2
extern int _fdata[];
#    define DATASTART ((ptr_t)_fdata)
/*
 * Lowest plausible heap address.  In the `USE_MMAP` case, we map there.
 * In either case it is used to identify heap sections so they are not
 * considered as roots.
 */
#    ifdef USE_MMAP
#      define HEAP_START ((word)0x30000000)
#    else
#      define HEAP_START ADDR(DATASTART)
#    endif
/* `MPROTECT_VDB` should work, but there is evidence of a breakage. */
#    define DYNAMIC_LOADING
#  endif
#  ifdef MSWINCE
#    define CPP_WORDSZ 32
#  endif
#  ifdef NETBSD
#    define CPP_WORDSZ 32
#    ifndef __ELF__
#      define DATASTART MAKE_CPTR(0x10000000)
#      define STACKBOTTOM MAKE_CPTR(0x7ffff000)
#    endif
#  endif
#  ifdef OPENBSD
#    define CPP_WORDSZ 64 /*< all OpenBSD/mips platforms are 64-bit */
#  endif
#  ifdef FREEBSD
#    define CPP_WORDSZ 32
#  endif
#  ifdef NONSTOP
#    define OS_TYPE "NONSTOP"
#    define CPP_WORDSZ 32
#    define DATASTART MAKE_CPTR(0x8000000)
extern char **environ;
#    define DATAEND ((ptr_t)(environ - 0x10))
#    define STACKBOTTOM MAKE_CPTR(0x4fffffff)
#    undef USE_MUNMAP
#  endif
#endif /* MIPS */

#ifdef NIOS2
#  define MACH_TYPE "NIOS2"
#  define CPP_WORDSZ 32
#  ifndef HBLKSIZE
#    define HBLKSIZE 4096
#  endif
#  ifdef LINUX
extern int __data_start[];
#    define DATASTART ((ptr_t)__data_start)
#  endif
#endif /* NIOS2 */

#ifdef OR1K
#  define MACH_TYPE "OR1K"
#  define CPP_WORDSZ 32
#  ifndef HBLKSIZE
#    define HBLKSIZE 4096
#  endif
#  ifdef LINUX
extern int __data_start[];
#    define DATASTART ((ptr_t)__data_start)
#  endif
#endif /* OR1K */

#ifdef HP_PA
#  define MACH_TYPE "HP_PA"
#  ifdef __LP64__
#    define CPP_WORDSZ 64
#  else
#    define CPP_WORDSZ 32
#  endif
#  define STACK_GROWS_UP
#  ifdef HPUX
#    ifndef GC_THREADS
#      define MPROTECT_VDB
#    endif
#    ifdef USE_HPUX_FIXED_STACKBOTTOM
/*
 * The following appears to work for 7xx systems running HP/UX 9.xx.
 * Furthermore, it might result in much faster collections than `HEURISTIC2`,
 * which may involve scanning segments that directly precede the stack.
 * It is not the default, since it may not work on older machine/OS
 * combinations. (Thanks to Raymond X.T. Nijssen for uncovering this.)
 * This technique also does not work with HP/UX 11.xx.  The stack size is
 * settable using the kernel `maxssiz` variable, and the size can be set
 * dynamically in HP/UX 11.23 and later.  It also does not handle
 * `SHMEM_MAGIC` binaries that have stack and data in the first quadrant.
 * This is from platform `/etc/conf/h/param.h` file.
 */
#      define STACKBOTTOM MAKE_CPTR(0x7b033000)
#    elif defined(USE_ENVIRON_POINTER)
/*
 * Gustavo Rodriguez-Rivera suggested changing `HEURISTIC2` to this.
 * Note that the collector must be initialized before the first `putenv()`
 * call.  Unfortunately, some clients do not obey.
 */
extern char **environ;
#      define STACKBOTTOM ((ptr_t)environ)
#    elif !defined(HEURISTIC2)
/* This uses `pst_vm_status` support. */
#      define SPECIFIC_MAIN_STACKBOTTOM
#    endif
#    ifndef __GNUC__
#      define PREFETCH(x)                   \
        do {                                \
          register long addr = (long)(x);   \
          (void)_asm("LDW", 0, 0, addr, 0); \
        } while (0)
#    endif
#  endif /* HPUX */
#  ifdef LINUX
#    define SEARCH_FOR_DATA_START
#  endif
#  ifdef OPENBSD
/* Nothing specific. */
#  endif
#endif /* HP_PA */

#ifdef ALPHA
#  define MACH_TYPE "ALPHA"
#  define CPP_WORDSZ 64
#  ifdef NETBSD
#    define ELFCLASS32 32
#    define ELFCLASS64 64
#    define ELF_CLASS ELFCLASS64
#  endif
#  ifdef OPENBSD
/* Nothing specific. */
#  endif
#  ifdef FREEBSD
extern char etext[];
#    define DATASTART ((ptr_t)(&etext))
#    define DATAEND ((ptr_t)GC_find_limit(DATASTART, TRUE))
#    define DATAEND_IS_FUNC
/*
 * Handle unmapped hole which `alpha*-*-freebsd[45]*` puts between
 * `etext` and `edata`.
 */
#    define GC_HAVE_DATAREGION2
extern char edata[], end[];
#    define DATASTART2 ((ptr_t)(&edata))
#    define DATAEND2 ((ptr_t)(&end))
/* `MPROTECT_VDB` is not yet supported at all on FreeBSD/alpha. */
#  endif
#  ifdef OSF1
#    define OS_TYPE "OSF1"
#    define DATASTART MAKE_CPTR(0x140000000)
extern int _end[];
#    define DATAEND ((ptr_t)(&_end))
extern char **environ;
/*
 * Round up from the value of `environ` to the nearest page boundary.
 * Probably this is broken if `putenv()` is called before the collector
 * initialization.
 */
#    define STACKBOTTOM PTR_ALIGN_UP((ptr_t)environ, getpagesize())
/*
 * Normally `HEURISTIC2` is too conservative, since the text segment
 * immediately follows the stack.  Hence we give an upper bound.
 * This is currently unused, since `HEURISTIC2` is not defined.
 */
extern int __start[];
#    define HEURISTIC2_LIMIT PTR_ALIGN_DOWN((ptr_t)__start, getpagesize())
#    ifndef GC_THREADS
/* FIXME: Unresolved signal issues with threads. */
#      define MPROTECT_VDB
#    endif
#    define DYNAMIC_LOADING
#  endif
#  ifdef LINUX
#    ifdef __ELF__
#      define SEARCH_FOR_DATA_START
#    else
#      define DATASTART MAKE_CPTR(0x140000000)
extern int _end[];
#      define DATAEND ((ptr_t)_end)
#    endif
#  endif
#endif /* ALPHA */

#ifdef IA64
#  define MACH_TYPE "IA64"
#  ifdef HPUX
#    ifdef _ILP32
#      define CPP_WORDSZ 32
/* Note: requires 8-byte alignment (granularity) for `malloc()`. */
#      define ALIGNMENT 4
#    else
#      if !defined(_LP64) && !defined(CPPCHECK)
#        error Unknown ABI
#      endif
#      define CPP_WORDSZ 64
/* Note: requires 16-byte alignment (granularity) for `malloc()`. */
#      define ALIGNMENT 8
#    endif
/*
 * Note that the collector must be initialized before the 1st `putenv`
 * call.
 */
extern char **environ;
#    define STACKBOTTOM ((ptr_t)environ)
/*
 * The following was empirically determined, and is probably not very
 * robust.  Note that the backing store base seems to be at a nice address
 * minus one page.
 */
#    define BACKING_STORE_DISPLACEMENT 0x1000000
#    define BACKING_STORE_ALIGNMENT 0x1000
/* Known to be wrong for recent HP/UX versions!!! */
#  endif
#  ifdef LINUX
#    define CPP_WORDSZ 64
/*
 * The following works on NUE and older kernels:
 * `define STACKBOTTOM MAKE_CPTR(0xa000000000000000l)`.
 */
/* TODO: `SPECIFIC_MAIN_STACKBOTTOM` does not work on NUE. */
/* We also need the base address of the register stack backing store. */
#    define SEARCH_FOR_DATA_START
#    ifdef __GNUC__
#      define DYNAMIC_LOADING
#    else
/*
 * In the Intel compiler environment, we seem to end up with statically
 * linked executables and an undefined reference to `_DYNAMIC`.
 */
#    endif
#    ifdef __GNUC__
#      ifndef __INTEL_COMPILER
#        define PREFETCH(x) __asm__("        lfetch  [%0]" : : "r"(x))
#        define GC_PREFETCH_FOR_WRITE(x) \
          __asm__("        lfetch.excl     [%0]" : : "r"(x))
#        define CLEAR_DOUBLE(x) \
          __asm__("        stf.spill       [%0]=f0" : : "r"((void *)(x)))
#      else
EXTERN_C_END
#        include <ia64intrin.h>
EXTERN_C_BEGIN
#        define PREFETCH(x) __lfetch(__lfhint_none, (x))
#        define GC_PREFETCH_FOR_WRITE(x) __lfetch(__lfhint_nta, (x))
#        define CLEAR_DOUBLE(x) __stf_spill((void *)(x), 0)
#      endif /* __INTEL_COMPILER */
#    endif
#  endif
#  ifdef MSWIN32
/* FIXME: This is a very partial guess.  There is no port, yet. */
#    if defined(_WIN64)
#      define CPP_WORDSZ 64
#    else
/* TODO: Is this possible? */
#      define CPP_WORDSZ 32
#    endif
#  endif
#endif /* IA64 */

#ifdef E2K
#  define MACH_TYPE "E2K"
#  ifdef __LP64__
#    define CPP_WORDSZ 64
#  else
#    define CPP_WORDSZ 32
#  endif
#  ifndef HBLKSIZE
#    define HBLKSIZE 4096
#  endif
#  ifdef LINUX
extern int __dso_handle[];
#    define DATASTART ((ptr_t)__dso_handle)
#    ifdef REDIRECT_MALLOC
#      define NO_PROC_FOR_LIBRARIES
#    endif
#  endif
#endif /* E2K */

#ifdef M88K
#  define MACH_TYPE "M88K"
#  define CPP_WORDSZ 32
#  define STACKBOTTOM MAKE_CPTR(0xf0000000) /*< determined empirically */
extern int etext[];
#  ifdef CX_UX
#    define OS_TYPE "CX_UX"
#    define DATASTART (PTR_ALIGN_UP((ptr_t)etext, 0x400000) + 0x10000)
#  endif
#  ifdef DGUX
#    define OS_TYPE "DGUX"
#    define DATASTART GC_SysVGetDataStart(0x10000, (ptr_t)etext)
#    define DATASTART_USES_XGETDATASTART
#  endif
#endif /* M88K */

#ifdef S370
/*
 * If this still works, and if anyone cares, this should probably be moved
 * to the `S390` category.
 */
#  define MACH_TYPE "S370"
#  define CPP_WORDSZ 32
#  define ALIGNMENT 4 /*< required by hardware */
#  ifdef UTS4
#    define OS_TYPE "UTS4"
extern int _etext[], _end[];
#    define DATASTART GC_SysVGetDataStart(0x10000, (ptr_t)_etext)
#    define DATAEND ((ptr_t)_end)
#    define HEURISTIC2
#  endif
#endif /* S370 */

#ifdef S390
#  define MACH_TYPE "S390"
#  ifndef __s390x__
#    define CPP_WORDSZ 32
#  else
#    define CPP_WORDSZ 64
#    ifndef HBLKSIZE
#      define HBLKSIZE 4096
#    endif
#  endif
#  ifdef LINUX
extern int __data_start[] __attribute__((__weak__));
extern int _end[] __attribute__((__weak__));
#    define DATASTART ((ptr_t)__data_start)
#    define DATAEND ((ptr_t)_end)
#    define CACHE_LINE_SIZE 256
#    define GETPAGESIZE() 4096
#    ifndef SOFT_VDB
#      define SOFT_VDB
#    endif
#  endif
#endif /* S390 */

#ifdef AARCH64
#  define MACH_TYPE "AARCH64"
#  ifdef __ILP32__
#    define CPP_WORDSZ 32
#  else
#    define CPP_WORDSZ 64
#  endif
#  ifndef HBLKSIZE
#    define HBLKSIZE 4096
#  endif
#  ifdef LINUX
#    if defined(HOST_ANDROID)
#      define SEARCH_FOR_DATA_START
#    else
extern int __data_start[] __attribute__((__weak__));
#      define DATASTART ((ptr_t)__data_start)
#    endif
#  endif
#  ifdef COSMO
/* Empty. */
#  endif
#  ifdef DARWIN
/* OS X, iOS, visionOS */
#    define DARWIN_DONT_PARSE_STACK 1
#    define STACKBOTTOM MAKE_CPTR(0x16fdfffff)
#    if (TARGET_OS_IPHONE || TARGET_OS_XR || TARGET_OS_VISION)
/*
 * `MPROTECT_VDB` causes use of non-public API like `exc_server`, this
 * could be a reason for blocking the client application in the store.
 */
#    elif TARGET_OS_OSX
#      define MPROTECT_VDB
#    endif
#  endif
#  ifdef FREEBSD
/* Nothing specific. */
#  endif
#  ifdef NETBSD
#    define ELF_CLASS ELFCLASS64
#  endif
#  ifdef OPENBSD
/* Nothing specific. */
#  endif
#  ifdef NINTENDO_SWITCH
#    define OS_TYPE "NINTENDO_SWITCH"
#    define NO_HANDLE_FORK 1
extern int __bss_end[];
#    define DATASTART ((ptr_t)ALIGNMENT) /*< cannot be `NULL` */
#    define DATAEND ((ptr_t)(&__bss_end))
void *switch_get_stack_bottom(void);
#    define STACKBOTTOM ((ptr_t)switch_get_stack_bottom())
void *switch_get_mem(size_t lb);
#    define GET_MEM(lb) switch_get_mem(lb)
#    ifndef HAVE_CLOCK_GETTIME
#      define HAVE_CLOCK_GETTIME 1
#    endif
#  endif
#  ifdef KOS
/* Nothing specific. */
#  endif
#  ifdef QNX
/* Nothing specific. */
#  endif
#  ifdef SERENITY
/* Nothing specific. */
#  endif
#  ifdef MSWIN32
/* UWP */
/* TODO: Enable `MPROTECT_VDB`. */
#  endif
#  ifdef NOSYS
#    define OS_TYPE "NOSYS"
/* `__data_start` is usually defined in the target linker script. */
extern int __data_start[];
#    define DATASTART ((ptr_t)__data_start)
extern void *__stack_base__;
#    define STACKBOTTOM ((ptr_t)__stack_base__)
#  endif
#endif /* AARCH64 */

#ifdef ARM32
#  define MACH_TYPE "ARM32"
#  define CPP_WORDSZ 32
#  ifdef LINUX
#    if GC_GLIBC_PREREQ(2, 0) || defined(HOST_ANDROID)
#      define SEARCH_FOR_DATA_START
#    else
/* See the comment of the Linux/m68k case. */
extern char **__environ;
#      define DATASTART ((ptr_t)(&__environ))
#    endif
#  endif
#  ifdef MSWINCE
/* Nothing specific. */
#  endif
#  ifdef FREEBSD
/* Nothing specific. */
#  endif
#  ifdef DARWIN
/* iOS */
#    define DARWIN_DONT_PARSE_STACK 1
#    define STACKBOTTOM MAKE_CPTR(0x30000000)
/* `MPROTECT_VDB` causes use of non-public API. */
#  endif
#  ifdef NETBSD
/* Nothing specific. */
#  endif
#  ifdef OPENBSD
/* Nothing specific. */
#  endif
#  ifdef QNX
/* Nothing specific. */
#  endif
#  ifdef SN_TARGET_PSP2
#    define OS_TYPE "SN_TARGET_PSP2"
#    define NO_HANDLE_FORK 1
#    ifndef HBLKSIZE
#      define HBLKSIZE 65536 /*< page size is 64 KB */
#    endif
#    define DATASTART ((ptr_t)ALIGNMENT)
#    define DATAEND ((ptr_t)ALIGNMENT)
void *psp2_get_stack_bottom(void);
#    define STACKBOTTOM ((ptr_t)psp2_get_stack_bottom())
void *psp2_get_mem(size_t lb);
#    define GET_MEM(lb) psp2_get_mem(lb)
#  endif
#  ifdef NN_PLATFORM_CTR
#    define OS_TYPE "NN_PLATFORM_CTR"
extern unsigned char Image$$ZI$$ZI$$Base[];
#    define DATASTART ((ptr_t)Image$$ZI$$ZI$$Base)
extern unsigned char Image$$ZI$$ZI$$Limit[];
#    define DATAEND ((ptr_t)Image$$ZI$$ZI$$Limit)
void *n3ds_get_stack_bottom(void);
#    define STACKBOTTOM ((ptr_t)n3ds_get_stack_bottom())
#  endif
#  ifdef MSWIN32
/* UWP */
/* TODO: Enable `MPROTECT_VDB`. */
#  endif
#  ifdef NOSYS
#    define OS_TYPE "NOSYS"
/* `__data_start` is usually defined in the target linker script. */
extern int __data_start[];
#    define DATASTART ((ptr_t)__data_start)
/* `__stack_base__` is set in platform `newlib/libc/sys/arm/crt0.S` file. */
extern void *__stack_base__;
#    define STACKBOTTOM ((ptr_t)__stack_base__)
#  endif
#  ifdef SYMBIAN
/* Nothing specific. */
#  endif
#endif /* ARM32 */

#ifdef CRIS
#  define MACH_TYPE "CRIS"
#  define CPP_WORDSZ 32
#  define ALIGNMENT 1
#  ifdef LINUX
#    define SEARCH_FOR_DATA_START
#  endif
#endif /* CRIS */

#if defined(SH) && !defined(SH4)
#  define MACH_TYPE "SH"
#  define CPP_WORDSZ 32
#  ifdef LINUX
#    define SEARCH_FOR_DATA_START
#  endif
#  ifdef NETBSD
/* Nothing specific. */
#  endif
#  ifdef OPENBSD
/* Nothing specific. */
#  endif
#  ifdef MSWINCE
/* Nothing specific. */
#  endif
#endif

#ifdef SH4
#  define MACH_TYPE "SH4"
#  define CPP_WORDSZ 32
#  ifdef MSWINCE
/* Nothing specific. */
#  endif
#endif /* SH4 */

#ifdef AVR32
#  define MACH_TYPE "AVR32"
#  define CPP_WORDSZ 32
#  ifdef LINUX
#    define SEARCH_FOR_DATA_START
#  endif
#endif /* AVR32 */

#ifdef M32R
#  define MACH_TYPE "M32R"
#  define CPP_WORDSZ 32
#  ifdef LINUX
#    define SEARCH_FOR_DATA_START
#  endif
#endif /* M32R */

#ifdef X86_64
#  define MACH_TYPE "X86_64"
#  ifdef __ILP32__
#    define CPP_WORDSZ 32
#  else
#    define CPP_WORDSZ 64
#  endif
#  ifndef HBLKSIZE
#    define HBLKSIZE 4096
#  endif
#  ifndef CACHE_LINE_SIZE
#    define CACHE_LINE_SIZE 64
#  endif
#  ifdef PLATFORM_GETMEM
#    define OS_TYPE "PLATFORM_GETMEM"
#    define DATASTART ((ptr_t)ALIGNMENT)
#    define DATAEND ((ptr_t)ALIGNMENT)
EXTERN_C_END
#    include <pthread.h>
EXTERN_C_BEGIN
void *platform_get_stack_bottom(void);
#    define STACKBOTTOM ((ptr_t)platform_get_stack_bottom())
void *platform_get_mem(size_t lb);
#    define GET_MEM(lb) platform_get_mem(lb)
#  endif
#  ifdef LINUX
#    define SEARCH_FOR_DATA_START
#    if defined(__GLIBC__) && !defined(__UCLIBC__)
/*
 * A workaround for GCF (Google Cloud Function) which does not support
 * `mmap()` for `/dev/zero` pseudo-file.  Should not cause any harm to
 * other targets.
 */
#      define USE_MMAP_ANON
#    endif
#    if defined(__GLIBC__) && !defined(__UCLIBC__) \
        && !defined(GETCONTEXT_FPU_BUG_FIXED)
/*
 * At present, there is a bug in `glibc` `getcontext()` on Linux/x86_64
 * (it clears FPU exception mask).  We define this macro to workaround it.
 */
/* TODO: This seems to be fixed in `glibc` 2.14. */
#      define GETCONTEXT_FPU_EXCMASK_BUG
#    endif
#    if defined(__GLIBC__) && !defined(__UCLIBC__) \
        && !defined(GLIBC_TSX_BUG_FIXED)
/* Workaround lock elision implementation for some `glibc`. */
#      define GLIBC_2_19_TSX_BUG
EXTERN_C_END
#      include <gnu/libc-version.h> /*< for `gnu_get_libc_version()` */
EXTERN_C_BEGIN
#    endif
#    ifndef SOFT_VDB
#      define SOFT_VDB
#    endif
#  endif
#  ifdef COSMO
/* Empty. */
#  endif
#  ifdef DARWIN
#    define DARWIN_DONT_PARSE_STACK 1
#    define STACKBOTTOM MAKE_CPTR(0x7fff5fc00000)
#    define MPROTECT_VDB
#  endif
#  ifdef FREEBSD
#    if defined(__GLIBC__)
extern int _end[];
#      define DATAEND ((ptr_t)_end)
#    endif
#    if defined(__DragonFly__)
/*
 * DragonFly BSD still has `vm.max_proc_mmap`, according to its
 * `mmap(2)` man page.
 */
#      define COUNT_UNMAPPED_REGIONS
#    endif
#  endif
#  ifdef NETBSD
/* Nothing specific. */
#  endif
#  ifdef OPENBSD
/* Nothing specific. */
#  endif
#  ifdef HAIKU
/* Nothing specific. */
#  endif
#  ifdef HURD
/* Nothing specific. */
#  endif
#  ifdef QNX
/* Nothing specific. */
#  endif
#  ifdef SERENITY
/* Nothing specific. */
#  endif
#  ifdef SOLARIS
#    define ELF_CLASS ELFCLASS64
extern int _etext[];
#    define DATASTART GC_SysVGetDataStart(0x1000, (ptr_t)_etext)
#    define PROC_VDB
#  endif
#  ifdef CYGWIN32
#    ifndef USE_WINALLOC
#      if defined(THREAD_LOCAL_ALLOC)
/*
 * TODO: For an unknown reason, thread-local allocations lead to spurious
 * process exit after the fault handler is once invoked.
 */
#      else
#        define MPROTECT_VDB
#      endif
#    endif
#  endif
#  ifdef MSWIN_XBOX1
#    define OS_TYPE "MSWIN_XBOX1"
#    define NO_GETENV
#    define DATASTART ((ptr_t)ALIGNMENT)
#    define DATAEND ((ptr_t)ALIGNMENT)
LONG64 durango_get_stack_bottom(void);
#    define STACKBOTTOM ((ptr_t)durango_get_stack_bottom())
#    define GETPAGESIZE() 4096
#    ifndef USE_MMAP
#      define USE_MMAP 1
#    endif
/* The following is from platform `sys/mman.h` file. */
#    define PROT_NONE 0
#    define PROT_READ 1
#    define PROT_WRITE 2
#    define PROT_EXEC 4
#    define MAP_PRIVATE 2
#    define MAP_FIXED 0x10
#    define MAP_FAILED ((void *)(~(GC_uintptr_t)0))
#  endif
#  ifdef MSWIN32
#    define RETRY_GET_THREAD_CONTEXT
#    if !defined(__GNUC__) || defined(__INTEL_COMPILER) \
        || (GC_GNUC_PREREQ(4, 7) && !defined(__MINGW64__))
/*
 * Older gcc and Mingw-w64 (both gcc and clang) do not support
 * `SetUnhandledExceptionFilter()` properly on x86_64.
 */
#      define MPROTECT_VDB
#    endif
#  endif
#endif /* X86_64 */

#ifdef ARC
#  define MACH_TYPE "ARC"
#  define CPP_WORDSZ 32
#  define CACHE_LINE_SIZE 64
#  ifdef LINUX
extern int __data_start[] __attribute__((__weak__));
#    define DATASTART ((ptr_t)__data_start)
#  endif
#endif /* ARC */

#ifdef HEXAGON
#  define MACH_TYPE "HEXAGON"
#  define CPP_WORDSZ 32
#  ifdef LINUX
#    if defined(__GLIBC__)
#      define SEARCH_FOR_DATA_START
#    elif !defined(CPPCHECK)
#      error Unknown Hexagon libc configuration
#    endif
#  endif
#endif /* HEXAGON */

#ifdef TILEPRO
#  define MACH_TYPE "TILEPRO"
#  define CPP_WORDSZ 32
#  define PREFETCH(x) __insn_prefetch(x)
#  define CACHE_LINE_SIZE 64
#  ifdef LINUX
extern int __data_start[];
#    define DATASTART ((ptr_t)__data_start)
#  endif
#endif /* TILEPRO */

#ifdef TILEGX
#  define MACH_TYPE "TILEGX"
#  define CPP_WORDSZ (__SIZEOF_PTRDIFF_T__ * 8)
#  if CPP_WORDSZ == 32
#    define CLEAR_DOUBLE(x) (void)(*(long long *)(x) = 0)
#  endif
#  define PREFETCH(x) __insn_prefetch_l1(x)
#  define CACHE_LINE_SIZE 64
#  ifdef LINUX
extern int __data_start[];
#    define DATASTART ((ptr_t)__data_start)
#  endif
#endif /* TILEGX */

#ifdef RISCV
#  define MACH_TYPE "RISCV"
#  define CPP_WORDSZ (__SIZEOF_SIZE_T__ * 8) /*< 32 or 64 */
#  ifdef FREEBSD
/* Nothing specific. */
#  endif
#  ifdef LINUX
extern int __data_start[] __attribute__((__weak__));
#    define DATASTART ((ptr_t)__data_start)
#  endif
#  ifdef NETBSD
/* Nothing specific. */
#  endif
#  ifdef OPENBSD
/* Nothing specific. */
#  endif
#  ifdef NOSYS
#    define OS_TYPE "NOSYS"
extern char etext[];
#    define DATASTART ((ptr_t)etext)
/* FIXME: `STACKBOTTOM` is wrong! */
extern char **environ;
#    define STACKBOTTOM ((ptr_t)environ)
/* TODO: Support 64K page size. */
#    define GETPAGESIZE() 4096
#  endif
#endif /* RISCV */

#ifdef WEBASSEMBLY
#  define MACH_TYPE "WEBASSEMBLY"
#  if defined(__wasm64__) && !defined(CPPCHECK)
#    error 64-bit WebAssembly is not yet supported
#  endif
#  define CPP_WORDSZ 32
/*
 * Emscripten does emulate `mmap` and `munmap`, but those should not be
 * used in the collector, since WebAssembly lacks the native support of
 * memory mapping.  Use `sbrk()` instead (by default).
 */
#  undef USE_MMAP
#  undef USE_MUNMAP
#  ifdef EMSCRIPTEN_TINY
void *emmalloc_memalign(size_t align, size_t lb);
#    define GET_MEM(lb) emmalloc_memalign(GC_page_size, lb)
#  endif
#  ifdef EMSCRIPTEN
#    define OS_TYPE "EMSCRIPTEN"
#    define DATASTART ((ptr_t)ALIGNMENT)
#    define DATAEND ((ptr_t)ALIGNMENT)
#    if defined(GC_THREADS) && !defined(CPPCHECK)
#      error No thread support yet
#    endif
#  endif
#  ifdef WASI
#    define OS_TYPE "WASI"
extern char __global_base, __heap_base;
#    define DATASTART ((ptr_t)(&__global_base))
#    define DATAEND ((ptr_t)(&__heap_base))
#    define STACKBOTTOM DATASTART
#    ifndef GC_NO_SIGSETJMP
#      define GC_NO_SIGSETJMP 1 /*< no support of signals */
#    endif
#    ifndef NO_CLOCK
#      define NO_CLOCK 1 /*< no support of `clock()` */
#    endif
#    if defined(GC_THREADS) && !defined(CPPCHECK)
#      error No thread support yet
#    endif
#  endif
#endif /* WEBASSEMBLY */

#if defined(CYGWIN32) || defined(MSWIN32) || defined(MSWINCE)
/* Note: it does not include Xbox One. */
#  define ANY_MSWIN
#endif

#if defined(GC_PTHREADS) || defined(GC_WIN32_THREADS)          \
    || ((defined(NN_PLATFORM_CTR) || defined(NINTENDO_SWITCH)  \
         || defined(SN_TARGET_PS3) || defined(SN_TARGET_PSP2)) \
        && defined(GC_THREADS))
#  define THREADS
#endif

/*
 * If the client app is known not to create threads (even indirectly in
 * the used libraries) and the collector is not multi-threaded, then the
 * collector could be built with `SINGLE_THREADED_PROCESS` macro defined.
 * But in practice the macro should never be defined.
 */
#if defined(THREADS) && defined(SINGLE_THREADED_PROCESS)
#  undef SINGLE_THREADED_PROCESS
#endif

#if defined(__CHERI_PURE_CAPABILITY__)
#  define CHERI_PURECAP
#endif

#if defined(__GLIBC__) && !defined(DONT_USE_LIBC_PRIVATES)
/* Use the stack-end marker of `glibc`. */
#  define USE_LIBC_PRIVATES
#endif

#ifdef NO_RETRY_GET_THREAD_CONTEXT
#  undef RETRY_GET_THREAD_CONTEXT
#endif

#if defined(LINUX) && defined(SPECIFIC_MAIN_STACKBOTTOM) \
    && defined(NO_PROC_STAT) && !defined(USE_LIBC_PRIVATES)
/*
 * This combination will fail, since we have no way to get the stack bottom.
 * Use `HEURISTIC2` instead.
 */
#  undef SPECIFIC_MAIN_STACKBOTTOM
#  define HEURISTIC2
/* This may still fail on some architectures like `IA64`.  We tried... */
#endif

#if defined(USE_MMAP_ANON) && !defined(USE_MMAP)
#  define USE_MMAP 1
#elif (defined(LINUX) || defined(OPENBSD)) && defined(USE_MMAP)
/*
 * The OS kernel may do a somewhat better job merging mappings with
 * anonymous mappings.
 */
#  define USE_MMAP_ANON
#endif

#if defined(CHERI_PURECAP) && defined(USE_MMAP)
/* TODO: Currently turned off to avoid downgrading permissions on CHERI. */
#  undef USE_MUNMAP
#endif

#if (defined(E2K) && defined(USE_PTR_HWTAG) || defined(CHERI_PURECAP)) \
    && !defined(NO_BLACK_LISTING)
/*
 * Misinterpreting of an integer is not possible on the platforms with
 * H/W-tagged pointers, thus the black-listing mechanism is redundant.
 */
#  define NO_BLACK_LISTING
#endif

#if defined(REDIRECT_MALLOC) && defined(THREADS) \
    && (defined(LINUX) || defined(NACL))
/* TODO: Unclear if NaCl really needs this. */
#  define REDIR_MALLOC_AND_LINUXTHREADS
#endif

#if defined(REDIR_MALLOC_AND_LINUXTHREADS) && !defined(NO_PROC_FOR_LIBRARIES) \
    && !defined(USE_PROC_FOR_LIBRARIES)
/*
 * NPTL allocates thread stacks with `mmap`, which is fine.  But it
 * keeps a cache of thread stacks.  Each thread stack contains a thread
 * control block (TCB).  The latter, in turn, contains a pointer to
 * (`sizeof(void*)` from the beginning of) the `dtv` for thread-local
 * storage, which is `calloc`-allocated.  If we do not scan the cached
 * thread stacks, we appear to lose the `dtv`.  This tends to result in
 * something that looks like a bogus `dtv` count, which tends to result
 * in a `memset()` call on a block that is way too large.  Sometimes
 * we are lucky and the process just dies...  There seems to be
 * a similar issue with some other memory allocated by the dynamic
 * loader.  This should be avoidable by either:
 *   - Defining `USE_PROC_FOR_LIBRARIES` here (that performs very poorly,
 *     precisely because we end up scanning cached stacks);
 *   - Have `calloc()` look at its callers (in spite of the fact that it
 *     is gross and disgusting).
 *
 * In fact, neither seems to suffice, probably in part because even
 * with `USE_PROC_FOR_LIBRARIES`, we do not scan parts of stack
 * segments that appear to be out of bounds.  Thus we actually do both,
 * which seems to yield the best results.
 */
#  define USE_PROC_FOR_LIBRARIES
#endif

#ifndef OS_TYPE
#  define OS_TYPE ""
#endif

#ifndef MACH_TYPE
#  define MACH_TYPE ""
#endif

#ifndef DATAEND
extern int end[];
#  define DATAEND ((ptr_t)end)
#endif

/*
 * Workaround for Android NDK clang-3.5+ (as of NDK r10e) which does
 * not provide correct `_end` symbol.  Unfortunately, alternate `__end__`
 * symbol is provided only by NDK `bfd` linker.
 */
#if defined(HOST_ANDROID) && defined(__clang__) && !defined(BROKEN_UUENDUU_SYM)
#  undef DATAEND
#  pragma weak __end__
extern int __end__[];
#  define DATAEND (__end__ != 0 ? (ptr_t)__end__ : (ptr_t)_end)
#endif

#if defined(SOLARIS) || defined(DRSNX) || defined(UTS4) \
    || (defined(LINUX) && defined(SPARC))
/* OS has SVR4 generic features.  Probably others also qualify. */
#  define SVR4
#  define DATASTART_USES_XGETDATASTART
#endif

#if defined(HAVE_SYS_TYPES_H)                                      \
    || !(defined(__CC_ARM) || defined(GC_NO_TYPES) || defined(OS2) \
         || defined(MSWINCE) || defined(SN_TARGET_PSP2))
EXTERN_C_END
#  if defined(COSMO) && defined(MPROTECT_VDB) && !defined(_GNU_SOURCE)
#    define _GNU_SOURCE 1
#  endif
#  include <sys/types.h>
EXTERN_C_BEGIN
#endif /* HAVE_SYS_TYPES_H */

#if defined(HAVE_UNISTD_H)                                                \
    || !(defined(GC_NO_TYPES) || defined(MSWIN32) || defined(MSWINCE)     \
         || defined(MSWIN_XBOX1) || defined(NINTENDO_SWITCH)              \
         || defined(NN_PLATFORM_CTR) || defined(OS2) || defined(SERENITY) \
         || defined(SN_TARGET_PSP2) || defined(__CC_ARM))
EXTERN_C_END
#  include <unistd.h>
EXTERN_C_BEGIN
#endif /* HAVE_UNISTD_H */

#if !defined(ANY_MSWIN) && !defined(GETPAGESIZE)
#  if defined(DGUX) || defined(HOST_ANDROID) || defined(HOST_TIZEN) \
      || defined(KOS) || defined(SERENITY)                          \
      || (defined(LINUX) && defined(SPARC))
#    define GETPAGESIZE() (unsigned)sysconf(_SC_PAGESIZE)
#  else
#    define GETPAGESIZE() (unsigned)getpagesize()
#  endif
#endif /* !ANY_MSWIN && !GETPAGESIZE */

#if defined(HOST_ANDROID) && !(__ANDROID_API__ >= 23)           \
    && ((defined(MIPS) && (CPP_WORDSZ == 32)) || defined(ARM32) \
        || defined(I386) /* but not x32 */)
/*
 * `tkill()` exists only on arm32/mips(32)/x86.
 * NDK r11+ deprecates `tkill()` but keeps it for Mono clients.
 */
#  define USE_TKILL_ON_ANDROID
#endif

#if defined(MPROTECT_VDB) && defined(__GLIBC__) && !GC_GLIBC_PREREQ(2, 2)
#  error glibc too old?
#endif

#if defined(SOLARIS) || defined(DRSNX)
/* OS has Solaris-style semi-undocumented interface to dynamic loader. */
#  define SOLARISDL
/* OS has Solaris-style signal handlers. */
#  define SUNOS5SIGS
#endif

#if (defined(FREEBSD)                                     \
     && (defined(__DragonFly__) || defined(__GLIBC__)     \
         || __FreeBSD_kernel__ >= 4 || __FreeBSD__ >= 4)) \
    || defined(HPUX)
#  define SUNOS5SIGS
#endif

#if defined(COSMO) || defined(HPUX) || defined(HURD) || defined(NETBSD) \
    || defined(SERENITY) || (defined(FREEBSD) && defined(SUNOS5SIGS))   \
    || (defined(IRIX5) && defined(_sigargs)) /*< Irix 5.x, not 6.x */
#  define USE_SEGV_SIGACT
/* We may also get `SIGBUS`. */
#  define USE_BUS_SIGACT
#elif defined(ANY_BSD) || defined(HAIKU) || defined(IRIX5) || defined(OSF1) \
    || defined(SUNOS5SIGS)
#  define USE_SEGV_SIGACT
#endif

#if !defined(GC_EXPLICIT_SIGNALS_UNBLOCK) && defined(SUNOS5SIGS) \
    && !defined(GC_NO_PTHREAD_SIGMASK)
#  define GC_EXPLICIT_SIGNALS_UNBLOCK
#endif

#if !defined(NO_SIGNALS_UNBLOCK_IN_MAIN) && defined(GC_NO_PTHREAD_SIGMASK)
#  define NO_SIGNALS_UNBLOCK_IN_MAIN
#endif

#ifndef PARALLEL_MARK
#  undef GC_PTHREADS_PARAMARK /*< just in case it is defined by client */
#elif defined(GC_PTHREADS) && !defined(GC_PTHREADS_PARAMARK) \
    && !defined(__MINGW32__)
/*
 * Use `pthreads`-based parallel mark implementation.  Except for
 * MinGW 32/64 to workaround a deadlock in winpthreads-3.0b internals.
 */
#  define GC_PTHREADS_PARAMARK
#endif

#if !defined(NO_MARKER_SPECIAL_SIGMASK)                                 \
    && (defined(NACL) || defined(GC_WIN32_PTHREADS)                     \
        || (defined(GC_PTHREADS_PARAMARK) && defined(GC_WIN32_THREADS)) \
        || defined(GC_NO_PTHREAD_SIGMASK))
/*
 * Either there is no `pthread_sigmask()`, or the GC marker thread cannot
 * steal and drop user signal calls.
 */
#  define NO_MARKER_SPECIAL_SIGMASK
#endif

#if defined(NETBSD) && defined(THREADS)
#  define SIGRTMIN 33
#  define SIGRTMAX 63
/*
 * It seems to be necessary to wait until threads have restarted.
 * But it is unclear why that is the case.
 */
#  define GC_NETBSD_THREADS_WORKAROUND
#endif

#if defined(OPENBSD) && defined(THREADS)
EXTERN_C_END
#  include <sys/param.h>
EXTERN_C_BEGIN
#endif

#if defined(AIX) || defined(ANY_BSD) || defined(BSD) || defined(COSMO)     \
    || defined(DARWIN) || defined(DGUX) || defined(HAIKU) || defined(HPUX) \
    || defined(HURD) || defined(IRIX5) || defined(LINUX) || defined(OSF1)  \
    || defined(QNX) || defined(SERENITY) || defined(SVR4)
/* Basic UNIX-like system calls work. */
#  define UNIX_LIKE
#endif

#if defined(CPPCHECK)
#  undef CPP_WORDSZ
#  define CPP_WORDSZ (__SIZEOF_PTRDIFF_T__ * 8)
#elif CPP_WORDSZ != 32 && CPP_WORDSZ != 64
#  error Bad word size
#endif

#ifndef CPP_PTRSZ
#  ifdef CHERI_PURECAP
#    define CPP_PTRSZ (__SIZEOF_POINTER__ * 8)
#  else
#    define CPP_PTRSZ CPP_WORDSZ
#  endif
#endif

#ifndef CPPCHECK
#  if GC_SIZEOF_PTR * 8 != CPP_PTRSZ
#    error Bad pointer size
#  endif
#endif /* !CPPCHECK */

#ifndef ALIGNMENT
#  define ALIGNMENT (CPP_PTRSZ >> 3)
#endif

#if !defined(STACKBOTTOM) && (defined(ECOS) || defined(NOSYS)) \
    && !defined(CPPCHECK)
#  error Undefined STACKBOTTOM
#endif

#ifdef IGNORE_DYNAMIC_LOADING
#  undef DYNAMIC_LOADING
#endif

#if defined(SMALL_CONFIG) && !defined(GC_DISABLE_INCREMENTAL)
/* Presumably not worth the space it takes. */
#  define GC_DISABLE_INCREMENTAL
#endif

/* `USE_WINALLOC` is only an option for Cygwin. */
#ifndef CYGWIN32
#  undef USE_WINALLOC
#endif
#if defined(MSWIN32) || defined(MSWINCE)
#  define USE_WINALLOC 1
#endif

#ifdef USE_WINALLOC
#  undef USE_MMAP
#endif

#if defined(ANY_BSD) || defined(DARWIN) || defined(IRIX5) || defined(LINUX) \
    || defined(SERENITY) || defined(SOLARIS)                                \
    || ((defined(CYGWIN32) || defined(USE_MMAP) || defined(USE_MUNMAP))     \
        && !defined(USE_WINALLOC))
/* Try both `sbrk` and `mmap`, in that order. */
#  define MMAP_SUPPORTED
#endif

/*
 * Xbox One (DURANGO) may not need to be this aggressive, but the
 * default is likely too lax under heavy allocation pressure.
 * The platform does not have a virtual paging system, so it does not
 * have a large virtual address space that a standard x86_64 platform has.
 */
#if defined(USE_MUNMAP) && !defined(MUNMAP_THRESHOLD)     \
    && (defined(SN_TARGET_PS3) || defined(SN_TARGET_PSP2) \
        || defined(MSWIN_XBOX1))
#  define MUNMAP_THRESHOLD 3
#endif

#if defined(GC_DISABLE_INCREMENTAL) || defined(DEFAULT_VDB)
#  undef GWW_VDB
#  undef MPROTECT_VDB
#  undef PROC_VDB
#  undef SOFT_VDB
#endif

#ifdef NO_GWW_VDB
#  undef GWW_VDB
#endif

#ifdef NO_MPROTECT_VDB
#  undef MPROTECT_VDB
#endif

#ifdef NO_SOFT_VDB
#  undef SOFT_VDB
#endif

#if defined(SOFT_VDB) && defined(SOFT_VDB_LINUX_VER_STATIC_CHECK)
EXTERN_C_END
#  include <linux/version.h> /*< for `LINUX_VERSION`, `LINUX_VERSION_CODE` */
EXTERN_C_BEGIN
#  if LINUX_VERSION_CODE < KERNEL_VERSION(3, 18, 0)
/* Not reliable in Linux kernels prior to v3.18. */
#    undef SOFT_VDB
#  endif
#endif /* SOFT_VDB */

#ifdef GC_DISABLE_INCREMENTAL
#  undef CHECKSUMS
#endif

#if defined(BASE_ATOMIC_OPS_EMULATED)
/*
 * `GC_write_fault_handler()` cannot use lock-based atomic primitives
 * as this could lead to a deadlock.
 */
#  undef MPROTECT_VDB
#endif

#if defined(USE_PROC_FOR_LIBRARIES) && defined(LINUX) && defined(THREADS)
/* Incremental GC based on `mprotect` is incompatible with `/proc` roots. */
#  undef MPROTECT_VDB
#endif

#if defined(MPROTECT_VDB) && defined(GC_PREFER_MPROTECT_VDB)
/* Choose `MPROTECT_VDB` manually (if multiple strategies available). */
#  undef PROC_VDB
/* `GWW_VDB`, `SOFT_VDB` are handled in `os_dep.c` file. */
#endif

#ifdef PROC_VDB
/* Mutually exclusive VDB implementations (for now). */
#  undef MPROTECT_VDB
/* For a test purpose only. */
#  undef SOFT_VDB
#endif

#if defined(MPROTECT_VDB) && !defined(MSWIN32) && !defined(MSWINCE)
EXTERN_C_END
#  include <signal.h> /*< for `SA_SIGINFO`, `SIGBUS` */
EXTERN_C_BEGIN
#endif

#if defined(SIGBUS) && !defined(HAVE_SIGBUS) && !defined(CPPCHECK)
#  define HAVE_SIGBUS
#endif

#ifndef SA_SIGINFO
#  define NO_SA_SIGACTION
#endif

#if (defined(NO_SA_SIGACTION) || defined(GC_NO_SIGSETJMP))            \
    && defined(MPROTECT_VDB) && !defined(DARWIN) && !defined(MSWIN32) \
    && !defined(MSWINCE)
#  undef MPROTECT_VDB
#endif

#if !defined(DEFAULT_VDB) && !defined(GWW_VDB) && !defined(MPROTECT_VDB) \
    && !defined(PROC_VDB) && !defined(SOFT_VDB)                          \
    && !defined(GC_DISABLE_INCREMENTAL)
#  define DEFAULT_VDB
#endif

#if defined(CHECK_SOFT_VDB) && !defined(CPPCHECK)             \
    && (defined(GC_PREFER_MPROTECT_VDB) || !defined(SOFT_VDB) \
        || !defined(MPROTECT_VDB))
#  error Invalid config for CHECK_SOFT_VDB
#endif

#if (defined(GC_DISABLE_INCREMENTAL) || defined(BASE_ATOMIC_OPS_EMULATED) \
     || defined(REDIRECT_MALLOC) || defined(SMALL_CONFIG)                 \
     || defined(REDIRECT_MALLOC_IN_HEADER) || defined(CHECKSUMS))         \
    && !defined(NO_MANUAL_VDB)
/* TODO: Implement `CHECKSUMS` for manual VDB. */
#  define NO_MANUAL_VDB
#endif

#if !defined(PROC_VDB) && !defined(SOFT_VDB) \
    && !defined(NO_VDB_FOR_STATIC_ROOTS)
/* Cannot determine whether a static root page is dirty. */
#  define NO_VDB_FOR_STATIC_ROOTS
#endif

#if defined(MPROTECT_VDB) && !defined(DONT_COUNT_PROTECTED_REGIONS) \
    && !defined(COUNT_PROTECTED_REGIONS)                            \
    && (defined(LINUX) || defined(__DragonFly__))
#  define COUNT_PROTECTED_REGIONS
#endif

#if (defined(COUNT_PROTECTED_REGIONS) || defined(COUNT_UNMAPPED_REGIONS)) \
    && !defined(GC_UNMAPPED_REGIONS_SOFT_LIMIT)
/*
 * The default limit of `vm.max_map_count` on Linux is ~65530.
 * There is approximately one mapped region to every protected or
 * unmapped region.  Therefore if we aim to use up to half of
 * `vm.max_map_count` for the collector (leaving half for the rest
 * of the process), then the number of such regions should be one
 * quarter of `vm.max_map_count`.
 */
#  if defined(__DragonFly__)
#    define GC_UNMAPPED_REGIONS_SOFT_LIMIT (1000000 / 4)
#  else
#    define GC_UNMAPPED_REGIONS_SOFT_LIMIT 16384
#  endif
#endif

#if (((defined(ARM32) || defined(AVR32) || defined(MIPS) || defined(NIOS2) \
       || defined(OR1K))                                                   \
      && defined(UNIX_LIKE))                                               \
     || defined(DARWIN) || defined(HAIKU) || defined(HURD)                 \
     || defined(OPENBSD) || defined(QNX) || defined(RTEMS)                 \
     || defined(SERENITY) || defined(HOST_ANDROID)                         \
     || (defined(LINUX) && !defined(__gnu_linux__)))                       \
    && !defined(NO_GETCONTEXT)
#  define NO_GETCONTEXT 1
#endif

#if defined(MSWIN32) && !defined(CONSOLE_LOG) && defined(_MSC_VER) \
    && defined(_DEBUG) && !defined(NO_CRT)
/*
 * This should be included before the platform `intrin.h` file to
 * workaround some bug in Windows Kit (as of 10.0.17763) headers
 * causing redefinition of `_malloca` macro.
 */
EXTERN_C_END
#  include <crtdbg.h> /*< for `_CrtDbgReport` */
EXTERN_C_BEGIN
#endif

#ifndef PREFETCH
#  if (GC_GNUC_PREREQ(3, 0) || defined(__clang__)) && !defined(NO_PREFETCH)
#    define PREFETCH(x) __builtin_prefetch((x), 0, 0)
#  elif defined(_MSC_VER) && !defined(NO_PREFETCH)                      \
      && (defined(_M_IX86) || defined(_M_X64)) && !defined(_CHPE_ONLY_) \
      && (_MSC_VER >= 1900 /* VS 2015+ */)
EXTERN_C_END
#    include <intrin.h>
EXTERN_C_BEGIN
#    define PREFETCH(x) _mm_prefetch((const char *)(x), _MM_HINT_T0)
/* TODO: Support also `_M_ARM` and `_M_ARM64` (`__prefetch`). */
#  else
#    define PREFETCH(x) (void)0
#  endif
#endif /* !PREFETCH */

#ifndef GC_PREFETCH_FOR_WRITE
/*
 * The default `GC_PREFETCH_FOR_WRITE(x)` is defined in `gc_inline.h` file,
 * the latter one is included from `gc_priv.h` file.
 */
#endif

#ifndef CACHE_LINE_SIZE
#  define CACHE_LINE_SIZE 32 /*< wild guess */
#endif

#ifndef STATIC
#  ifdef GC_ASSERTIONS
#    define STATIC /*< ignore to aid debugging (or profiling) */
#  else
#    define STATIC static
#  endif
#endif

/*
 * Do we need the `GC_find_limit` machinery to find the end of
 * a data segment (or the backing store base)?
 */
#if defined(HEURISTIC2) || defined(SEARCH_FOR_DATA_START) || defined(IA64)    \
    || defined(DGUX) || defined(FREEBSD) || defined(OPENBSD) || defined(SVR4) \
    || (defined(HPUX) && defined(SPECIFIC_MAIN_STACKBOTTOM))                  \
    || (defined(CYGWIN32) && defined(I386) && defined(USE_MMAP)               \
        && !defined(USE_WINALLOC))                                            \
    || (defined(NETBSD) && defined(__ELF__))
#  define NEED_FIND_LIMIT
#endif

#if defined(LINUX)                                       \
    && (defined(USE_PROC_FOR_LIBRARIES) || defined(IA64) \
        || !defined(SMALL_CONFIG))
#  define NEED_PROC_MAPS
#endif

#if defined(LINUX) || defined(HURD) || defined(__GLIBC__)
#  define REGISTER_LIBRARIES_EARLY
/*
 * We sometimes use `dl_iterate_phdr`, which may acquire an internal lock.
 * This is not safe after the world has stopped.  So we must call
 * `GC_register_dynamic_libraries` before stopping the world.
 * For performance reasons, this may be beneficial on other platforms
 * as well, though it should be avoided on Windows.
 */
#endif /* LINUX */

#if defined(SEARCH_FOR_DATA_START)
extern ptr_t GC_data_start;
#  define DATASTART GC_data_start
#endif

#ifndef HEAP_START
#  define HEAP_START 0
#endif

#ifndef CLEAR_DOUBLE
#  define CLEAR_DOUBLE(x) \
    (void)(((ptr_t *)(x))[0] = NULL, ((ptr_t *)(x))[1] = NULL)
#endif

/*
 * Some `libc` implementations like `bionic`, `musl` and `glibc` 2.34+
 * do not have `libpthread.so` file because the `pthreads`-related code
 * is located in `libc.so` file, thus potential `calloc()` calls from
 * such code are forwarded to real (`libc`) `calloc()` without any
 * special handling on the collector side.  Checking `glibc` version at
 * compile time for the purpose seems to be fine.
 */
#if defined(REDIR_MALLOC_AND_LINUXTHREADS) && !defined(HAVE_LIBPTHREAD_SO) \
    && defined(__GLIBC__) && !GC_GLIBC_PREREQ(2, 34)
#  define HAVE_LIBPTHREAD_SO
#endif

#if defined(REDIR_MALLOC_AND_LINUXTHREADS) \
    && !defined(INCLUDE_LINUX_THREAD_DESCR)
/*
 * Will not work, since `libc` and the dynamic loader use thread locals,
 * sometimes as the only reference.
 */
#  define INCLUDE_LINUX_THREAD_DESCR
#endif

#ifndef CPPCHECK
#  if defined(GC_AIX_THREADS) && !defined(AIX)                            \
      || (defined(GC_DARWIN_THREADS) && !defined(DARWIN))                 \
      || (defined(GC_DGUX386_THREADS) && !defined(DGUX))                  \
      || (defined(GC_FREEBSD_THREADS) && !defined(FREEBSD))               \
      || (defined(GC_HAIKU_THREADS) && !defined(HAIKU))                   \
      || (defined(GC_HPUX_THREADS) && !defined(HPUX))                     \
      || (defined(GC_IRIX_THREADS) && !defined(IRIX5))                    \
      || (defined(GC_LINUX_THREADS) && !defined(LINUX) && !defined(NACL)) \
      || (defined(GC_NETBSD_THREADS) && !defined(NETBSD))                 \
      || (defined(GC_OPENBSD_THREADS) && !defined(OPENBSD))               \
      || (defined(GC_OSF1_THREADS) && !defined(OSF1))                     \
      || (defined(GC_RTEMS_PTHREADS) && !defined(RTEMS))                  \
      || (defined(GC_SOLARIS_THREADS) && !defined(SOLARIS))               \
      || (defined(GC_WIN32_THREADS) && !defined(ANY_MSWIN)                \
          && !defined(MSWIN_XBOX1))
#    error Inconsistent configuration
#  elif defined(GC_WIN32_PTHREADS) && defined(CYGWIN32)
#    error Inconsistent configuration (GC_PTHREADS)
#  endif
#  if defined(PARALLEL_MARK) && !defined(THREADS)
#    error Invalid config: PARALLEL_MARK requires GC_THREADS
#  endif
#  if defined(GWW_VDB) && !defined(USE_WINALLOC)
#    error Invalid config: GWW_VDB requires USE_WINALLOC
#  endif
#  if (defined(GC_FINDLEAK_DELAY_FREE) && defined(SHORT_DBG_HDRS)) \
      || ((defined(FIND_LEAK) || defined(GC_FINDLEAK_DELAY_FREE))  \
          && defined(NO_FIND_LEAK))
#    error Invalid config: FIND_LEAK and NO_FIND_LEAK are mutually exclusive
#  endif
#endif /* !CPPCHECK */

#if defined(NO_FIND_LEAK) && !defined(DONT_USE_ATEXIT)
#  define DONT_USE_ATEXIT
#endif

/* Whether `GC_page_size` is to be set to a value other than page size. */
#if defined(CYGWIN32) && (defined(MPROTECT_VDB) || defined(USE_MUNMAP)) \
    || (!defined(ANY_MSWIN) && !defined(WASI) && !defined(USE_MMAP)     \
        && (defined(GC_DISABLE_INCREMENTAL) || defined(DEFAULT_VDB)))
/*
 * Cygwin: use the allocation granularity instead.
 * Other than WASI and Windows: use `HBLKSIZE` instead (unless `mmap()`
 * is used).
 */
#  define ALT_PAGESIZE_USED
#  ifndef GC_NO_VALLOC
/* Nonetheless, we need the real page size is some extra functions. */
#    define REAL_PAGESIZE_NEEDED
#  endif
#endif

#if defined(GC_PTHREADS) && !defined(DARWIN) && !defined(GC_WIN32_THREADS) \
    && !defined(PLATFORM_STOP_WORLD) && !defined(SN_TARGET_PSP2)
#  define PTHREAD_STOP_WORLD_IMPL
#endif

#if defined(PTHREAD_STOP_WORLD_IMPL) && !defined(NACL)
#  define SIGNAL_BASED_STOP_WORLD
#endif

#if (defined(E2K) || defined(HP_PA) || defined(IA64) || defined(M68K) \
     || defined(NO_SA_SIGACTION))                                     \
    && defined(SIGNAL_BASED_STOP_WORLD)
#  define SUSPEND_HANDLER_NO_CONTEXT
#endif

#if (defined(MSWIN32) || defined(MSWINCE)                      \
     || (defined(USE_PROC_FOR_LIBRARIES) && defined(THREADS))) \
    && !defined(NO_CRT) && !defined(NO_WRAP_MARK_SOME)
/*
 * Under rare conditions, we may end up marking from nonexistent memory.
 * Hence we need to be prepared to recover by running `GC_mark_some` with
 * a suitable handler in place.
 */
/* TODO: Should we also define it for Cygwin? */
#  define WRAP_MARK_SOME
#endif

#if !defined(MSWIN32) && !defined(MSWINCE) || defined(__GNUC__) \
    || defined(NO_CRT)
#  define NO_SEH_AVAILABLE
#endif

#ifdef GC_WIN32_THREADS
/* The number of copied registers in `copy_ptr_regs()`. */
#  if defined(I386)
#    ifdef WOW64_THREAD_CONTEXT_WORKAROUND
#      define PUSHED_REGS_COUNT 9
#    else
#      define PUSHED_REGS_COUNT 7
#    endif
#  elif defined(X86_64)
#    ifdef XMM_CANT_STORE_PTRS
/* If pointers cannot be located in Xmm registers. */
#      define PUSHED_REGS_COUNT 15
#    else
/*
 * gcc-13 may store pointers into SIMD registers when certain compiler
 * optimizations are enabled.
 */
#      define PUSHED_REGS_COUNT (15 + 32)
#    endif
#  elif defined(SHx)
#    define PUSHED_REGS_COUNT 15
#  elif defined(ARM32)
#    define PUSHED_REGS_COUNT 13
#  elif defined(AARCH64)
#    define PUSHED_REGS_COUNT 30
#  elif defined(MIPS) || defined(ALPHA)
#    define PUSHED_REGS_COUNT 28
#  elif defined(PPC)
#    define PUSHED_REGS_COUNT 29
#  endif
#endif /* GC_WIN32_THREADS */

#if !defined(GC_PTHREADS) && !defined(GC_PTHREADS_PARAMARK)
#  undef HAVE_PTHREAD_SETNAME_NP_WITH_TID
#  undef HAVE_PTHREAD_SETNAME_NP_WITH_TID_AND_ARG
#  undef HAVE_PTHREAD_SET_NAME_NP
#endif

#if !(defined(GC_PTHREADS) || defined(GC_PTHREADS_PARAMARK) \
      || (defined(MPROTECT_VDB) && defined(DARWIN)))
#  undef HAVE_PTHREAD_SETNAME_NP_WITHOUT_TID
#endif

#if defined(USE_RWLOCK) || defined(GC_DISABLE_SUSPEND_THREAD)
/*
 * At least in the Linux threads implementation, `rwlock` primitives are
 * not atomic in respect to signals, and suspending externally a thread
 * which is running inside `pthread_rwlock_rdlock()` may lead to a deadlock.
 */
/* TODO: As a workaround `GC_suspend_thread()` API is disabled. */
#  undef GC_ENABLE_SUSPEND_THREAD
#endif

#ifndef GC_NO_THREADS_DISCOVERY
#  if defined(DARWIN) && defined(THREADS)
/* Task-based thread registration requires stack-frame-walking code. */
#    if defined(DARWIN_DONT_PARSE_STACK)
#      define GC_NO_THREADS_DISCOVERY
#    endif
#  elif defined(GC_WIN32_THREADS)
/*
 * `DllMain`-based thread registration is currently incompatible with
 * thread-local allocation, `pthreads` and WinCE.
 */
#    if (!defined(GC_DLL) && !defined(GC_INSIDE_DLL)) || defined(GC_PTHREADS) \
        || defined(MSWINCE) || defined(NO_CRT) || defined(THREAD_LOCAL_ALLOC)
#      define GC_NO_THREADS_DISCOVERY
#    endif
#  else
#    define GC_NO_THREADS_DISCOVERY
#  endif
#endif /* !GC_NO_THREADS_DISCOVERY */

#if defined(GC_DISCOVER_TASK_THREADS) && defined(GC_NO_THREADS_DISCOVERY) \
    && !defined(CPPCHECK)
#  error Defined both GC_DISCOVER_TASK_THREADS and GC_NO_THREADS_DISCOVERY
#endif

#if defined(PARALLEL_MARK) && !defined(DEFAULT_STACK_MAYBE_SMALL) \
    && (defined(DGUX) || defined(HPUX)                            \
        || defined(NO_GETCONTEXT) /* e.g. musl */)
/* TODO: Test default stack size in configure. */
#  define DEFAULT_STACK_MAYBE_SMALL
#endif

#ifdef PARALLEL_MARK
/* The minimum stack size for a marker thread. */
#  define MIN_STACK_SIZE (8 * HBLKSIZE * sizeof(ptr_t))
#endif

#if defined(HOST_ANDROID) && !defined(THREADS) \
    && !defined(USE_GET_STACKBASE_FOR_MAIN)
/*
 * Always use `pthread_attr_getstack` on Android (`-lpthread` option
 * is not needed to be specified manually) since Linux-specific
 * `os_main_stackbottom()` causes application crash if invoked inside
 * Dalvik VM.
 */
#  define USE_GET_STACKBASE_FOR_MAIN
#endif

/*
 * Outline `pthreads` primitives to use in `GC_get_stack_base()` and
 * `GC_get_main_stack_base()`.
 */
#if ((defined(FREEBSD) && defined(__GLIBC__) /* kFreeBSD */)               \
     || defined(COSMO) || defined(HAIKU) || defined(LINUX) || defined(KOS) \
     || defined(NETBSD))                                                   \
    && !defined(NO_PTHREAD_GETATTR_NP)
#  define HAVE_PTHREAD_GETATTR_NP 1
#elif defined(FREEBSD) && !defined(__GLIBC__) \
    && !defined(NO_PTHREAD_ATTR_GET_NP)
#  define HAVE_PTHREAD_NP_H 1 /*< requires include `pthread_np.h` file */
#  define HAVE_PTHREAD_ATTR_GET_NP 1
#endif

#if (defined(HAVE_PTHREAD_ATTR_GET_NP) || defined(HAVE_PTHREAD_GETATTR_NP)) \
    && defined(USE_GET_STACKBASE_FOR_MAIN) && !defined(STACKBOTTOM)         \
    && !defined(HEURISTIC1) && !defined(HEURISTIC2) && !defined(STACK_GRAN) \
    && !defined(SPECIFIC_MAIN_STACKBOTTOM)
/* Dummy definitions; rely on `pthread_attr_getstack` actually. */
#  define HEURISTIC1
#  define STACK_GRAN 0x1000000
#endif

#if !defined(HAVE_CLOCK_GETTIME) && defined(_POSIX_TIMERS) \
    && (defined(CYGWIN32) || (defined(LINUX) && defined(__USE_POSIX199309)))
#  define HAVE_CLOCK_GETTIME 1
#endif

#if defined(GC_PTHREADS) && !defined(E2K) && !defined(IA64)   \
    && (!defined(DARWIN) || defined(DARWIN_DONT_PARSE_STACK)) \
    && !defined(SN_TARGET_PSP2) && !defined(REDIRECT_MALLOC)
/*
 * Note: unimplemented in case of redirection of `malloc()` because
 * the client-provided function might call some `pthreads` primitive
 * which, in turn, may use `malloc()` internally.
 */
#  define STACKPTR_CORRECTOR_AVAILABLE
#endif

#if defined(UNIX_LIKE) && defined(THREADS) && !defined(NO_CANCEL_SAFE) \
    && !defined(HOST_ANDROID)
/*
 * Make the code cancellation-safe.  This basically means that we ensure
 * that cancellation requests are ignored while we are in the collector.
 * This applies only to POSIX deferred cancellation; we do not handle POSIX
 * asynchronous cancellation.  Note that this only works if
 * `pthread_setcancelstate` is async-signal-safe, at least in the absence of
 * asynchronous cancellation.  This appears to be true for `glibc`, though it
 * is not documented.  Without that assumption, there seems to be no way to
 * safely wait in a signal handler, which we need to do for thread suspension.
 * Also note that little other code appears to be cancellation-safe.
 * Hence it may make sense to turn this off for performance.
 */
#  define CANCEL_SAFE
#endif

#ifdef CANCEL_SAFE
#  define IF_CANCEL(x) x
#else
#  define IF_CANCEL(x) /*< empty */
#endif

#if defined(DARWIN) && defined(MPROTECT_VDB)   \
    && !defined(NO_DESC_CATCH_EXCEPTION_RAISE) \
    && !defined(FORCE_DESC_CATCH_EXCEPTION_RAISE) && GC_CLANG_PREREQ(17, 0)
/*
 * Workaround "REFERENCED_DYNAMICALLY flag on _catch_exception_raise"
 * linker deprecation warnings on macOS 15.4.
 */
#  define NO_DESC_CATCH_EXCEPTION_RAISE
#endif

#if !defined(CAN_HANDLE_FORK) && !defined(NO_HANDLE_FORK)                 \
    && !defined(HAVE_NO_FORK)                                             \
    && ((defined(GC_PTHREADS) && !defined(NACL)                           \
         && !defined(GC_WIN32_PTHREADS) && !defined(USE_WINALLOC))        \
        || (defined(DARWIN) && defined(MPROTECT_VDB) /* `&& !THREADS` */) \
        || (defined(HANDLE_FORK) && defined(GC_PTHREADS)))
/*
 * Attempts (where supported and requested) to make `GC_malloc` work in
 * a child process forked from a multi-threaded parent process.
 */
#  define CAN_HANDLE_FORK
#endif

/*
 * Workaround "failed to create new win32 semaphore" Cygwin fatal error
 * during semaphores fixup-after-fork.
 */
#if defined(CYGWIN32) && defined(THREADS) && defined(CAN_HANDLE_FORK) \
    && !defined(CYGWIN_SEM_FIXUP_AFTER_FORK_BUG_FIXED)                \
    && !defined(EMULATE_PTHREAD_SEMAPHORE)
#  define EMULATE_PTHREAD_SEMAPHORE
#endif

#if defined(CAN_HANDLE_FORK) && !defined(CAN_CALL_ATFORK)      \
    && !defined(GC_NO_CAN_CALL_ATFORK) && !defined(HOST_TIZEN) \
    && !defined(HURD) && (!defined(HOST_ANDROID) || __ANDROID_API__ >= 21)
/* Have working `pthread_atfork()`. */
#  define CAN_CALL_ATFORK
#endif

#if !defined(CAN_HANDLE_FORK) && !defined(HAVE_NO_FORK) \
    && !(defined(CYGWIN32) || defined(SOLARIS) || defined(UNIX_LIKE))
#  define HAVE_NO_FORK
#endif

#if !defined(USE_MARK_BITS) && !defined(USE_MARK_BYTES) \
    && defined(PARALLEL_MARK)
/* Minimize compare-and-swap usage. */
#  define USE_MARK_BYTES
#endif

#if (defined(MSWINCE) && !defined(__CEGCC__) || defined(MSWINRT_FLAVOR)) \
    && !defined(NO_GETENV)
#  define NO_GETENV
#endif

#if (defined(NO_GETENV) || defined(MSWINCE)) && !defined(NO_GETENV_WIN32)
#  define NO_GETENV_WIN32
#endif

#if !defined(MSGBOX_ON_ERROR) && !defined(NO_MSGBOX_ON_ERROR)                \
    && defined(MSWIN32) && !defined(MSWINRT_FLAVOR) && !defined(MSWIN_XBOX1) \
    && !defined(SMALL_CONFIG)
/*
 * Show a Windows message box with "OK" button on a GC fatal error.
 * Client application is terminated once the user clicks the button.
 */
#  define MSGBOX_ON_ERROR
#endif

#ifndef STRTOULL
#  if defined(_WIN64) && !defined(__GNUC__)
#    define STRTOULL _strtoui64
#  elif defined(_LLP64) || defined(__LLP64__) || defined(_WIN64)
#    define STRTOULL strtoull
#  else
/* `strtoul()` fits since `sizeof(long)` is not less than `sizeof(word)`. */
#    define STRTOULL strtoul
#  endif
#endif /* !STRTOULL */

#ifndef GC_WORD_C
#  if defined(_WIN64) && !defined(__GNUC__)
#    define GC_WORD_C(val) val##ui64
#  elif defined(_LLP64) || defined(__LLP64__) || defined(_WIN64)
#    define GC_WORD_C(val) val##ULL
#  else
#    define GC_WORD_C(val) ((word)val##UL)
#  endif
#endif /* !GC_WORD_C */

#if defined(__has_feature)
/* `__has_feature()` is supported. */
#  if __has_feature(address_sanitizer)
#    define ADDRESS_SANITIZER
#  endif
#  if __has_feature(memory_sanitizer)
#    define MEMORY_SANITIZER
#  endif
#  if __has_feature(thread_sanitizer) && defined(THREADS)
#    define THREAD_SANITIZER
#  endif
#else
#  ifdef __SANITIZE_ADDRESS__
/* gcc v4.8+ */
#    define ADDRESS_SANITIZER
#  endif
#  if defined(__SANITIZE_THREAD__) && defined(THREADS)
/* gcc v7.1+ */
#    define THREAD_SANITIZER
#  endif
#endif /* !__has_feature */

#if defined(SPARC)
/* Stack clearing is crucial, and we include assembly code to do it well. */
#  define ASM_CLEAR_CODE
#endif

/*
 * Can we save call chain in objects for debugging?  Set `NFRAMES`
 * (number of saved frames) and `NARGS` (number of arguments for each
 * frame) to reasonable values for the platform.
 * Define `SAVE_CALL_CHAIN` if we can.  `SAVE_CALL_COUNT` can be
 * specified at build time, though we feel free to adjust it slightly.
 * Define `NEED_CALLINFO` if we either save the call stack or
 * `GC_ADD_CALLER` is defined.  Note: `GC_CAN_SAVE_CALL_STACKS` is
 * defined (for certain platforms) in `gc_config_macros.h` file.
 */
#if defined(SPARC)                         \
    || ((defined(I386) || defined(X86_64)) \
        && (defined(LINUX) || defined(__GLIBC__)))
/*
 * Linux/x86: `SAVE_CALL_CHAIN` is supported if the code is compiled to save
 * frame pointers by default, i.e. no `-fomit-frame-pointer` flag is given.
 */
#  define CAN_SAVE_CALL_ARGS
#endif

#if defined(SAVE_CALL_COUNT) && !defined(GC_ADD_CALLER) \
    && defined(GC_CAN_SAVE_CALL_STACKS)
#  define SAVE_CALL_CHAIN
#endif

#ifdef SAVE_CALL_CHAIN
/* Number of arguments to save for each call. */
#  if defined(SAVE_CALL_NARGS) && defined(CAN_SAVE_CALL_ARGS)
#    define NARGS SAVE_CALL_NARGS
#  else
#    define NARGS 0
#  endif
/* Number of frames to save.  Even for alignment reasons. */
#  if !defined(SAVE_CALL_COUNT) || defined(CPPCHECK)
#    define NFRAMES 6
#  else
#    define NFRAMES ((SAVE_CALL_COUNT + 1) & ~1)
#  endif
#  define NEED_CALLINFO
#elif defined(GC_ADD_CALLER)
#  define NFRAMES 1
#  define NARGS 0
#  define NEED_CALLINFO
#endif

#if (defined(FREEBSD) || (defined(DARWIN) && !defined(_POSIX_C_SOURCE)) \
     || (defined(SOLARIS)                                               \
         && (!defined(_XOPEN_SOURCE) || defined(__EXTENSIONS__)))       \
     || defined(LINUX))                                                 \
    && !defined(HAVE_DLADDR)
#  define HAVE_DLADDR 1
#endif

#if defined(MAKE_BACK_GRAPH) && !defined(DBG_HDRS_ALL)
#  define DBG_HDRS_ALL 1
#endif

#if defined(POINTER_MASK) && !defined(POINTER_SHIFT)
#  define POINTER_SHIFT 0
#elif !defined(POINTER_MASK) && defined(POINTER_SHIFT)
#  define POINTER_MASK GC_WORD_MAX
#endif

#if defined(FIXUP_POINTER)
/* Custom `FIXUP_POINTER(p)`. */
#  define NEED_FIXUP_POINTER
#elif defined(DYNAMIC_POINTER_MASK)
#  define FIXUP_POINTER(p) \
    (p = (ptr_t)((((word)(p)) & GC_pointer_mask) << GC_pointer_shift))
#  undef POINTER_MASK
#  undef POINTER_SHIFT
#  define NEED_FIXUP_POINTER
#elif defined(POINTER_MASK)
/*
 * Note: extra parentheses around custom-defined `POINTER_MASK` and
 *`POINTER_SHIFT` are intentional.
 */
#  define FIXUP_POINTER(p) \
    (p = (ptr_t)(((word)(p) & (POINTER_MASK)) << (POINTER_SHIFT)))
#  define NEED_FIXUP_POINTER
#else
#  define FIXUP_POINTER(p) (void)(p)
#endif

#ifdef LINT2
/*
 * A macro (based on a tricky expression) to prevent false warnings
 * like "Array compared to 0", "Comparison of identical expressions",
 * "Untrusted loop bound" output by some static code analysis tools.
 * The argument should not be a literal value.  The result is
 * converted to `word` type.  (Actually, `GC_word` is used instead of
 * `word` type as the latter might be undefined at the place of use.)
 */
#  define COVERT_DATAFLOW(w) (~(GC_word)(w) ^ (~(GC_word)0))
#else
#  define COVERT_DATAFLOW(w) ((GC_word)(w))
#endif

#if CPP_PTRSZ > CPP_WORDSZ
/* TODO: Cannot use tricky operations on a pointer. */
#  define COVERT_DATAFLOW_P(p) ((ptr_t)(p))
#else
#  define COVERT_DATAFLOW_P(p) ((ptr_t)COVERT_DATAFLOW(p))
#endif

#if defined(REDIRECT_MALLOC) && defined(THREADS) && !defined(LINUX) \
    && !defined(REDIRECT_MALLOC_IN_HEADER)
/*
 * May work on other platforms (e.g. Darwin) provided the client
 * ensures all the client threads are registered with the collector,
 * e.g. by using the preprocessor-based interception of the thread
 * primitives (i.e., define `GC_THREADS` and include `gc.h` file from
 * all the client files those are using `pthread_create` and friends).
 */
#endif

EXTERN_C_END

#endif /* GCCONFIG_H */
