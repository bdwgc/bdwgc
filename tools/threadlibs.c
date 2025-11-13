/*
 * Copyright (c) 1994 by Xerox Corporation.  All rights reserved.
 * Copyright (c) 1996 by Silicon Graphics.  All rights reserved.
 * Copyright (c) 1998 by Fergus Henderson.  All rights reserved.
 * Copyright (c) 2000-2010 by Hewlett-Packard Development Company.
 * All rights reserved.
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
 * A build-time utility used by `Makefile.direct` file.
 * Determines proper linker flags.
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "private/gcconfig.h"

#include <stdio.h>

int
main(void)
{
#if defined(GC_USE_LD_WRAP)
  printf("-Wl,--wrap -Wl,dlopen "
         "-Wl,--wrap -Wl,pthread_create -Wl,--wrap -Wl,pthread_join "
         "-Wl,--wrap -Wl,pthread_detach -Wl,--wrap -Wl,pthread_sigmask "
         "-Wl,--wrap -Wl,pthread_exit -Wl,--wrap -Wl,pthread_cancel\n");
#endif
#ifdef THREADS
#  if defined(AIX) || defined(DARWIN) || defined(HURD) || defined(IRIX5) \
      || (defined(LINUX) && !defined(HOST_ANDROID)) || defined(NACL)
#    ifdef GC_USE_DLOPEN_WRAP
  printf("-ldl -lpthread\n");
#    else
  printf("-lpthread\n");
#    endif
#  endif
#  ifdef OPENBSD
  printf("-pthread\n");
#  endif
#  ifdef FREEBSD
#    ifdef GC_USE_DLOPEN_WRAP
  printf("-ldl ");
#    endif
#    if (__FREEBSD_version < 500000)
  printf("-pthread\n");
#    else /* __FREEBSD__ || __DragonFly__ */
  printf("-lpthread\n");
#    endif
#  endif
#  if defined(HPUX) || defined(NETBSD)
  printf("-lpthread -lrt\n");
#  endif
#  if defined(OSF1)
  /* DOB: must be `-pthread`, not `-lpthread`. */
  printf("-pthread -lrt\n");
#  endif
#  ifdef SOLARIS
  /* Is this right for recent versions? */
  printf("-lthread -lposix4\n");
#  endif
#  ifdef CYGWIN
  printf("-lpthread\n");
#  endif
#  if defined(GC_WIN32_PTHREADS)
#    ifdef PTW32_STATIC_LIB
  /* Assume suffix "s" for static variant of the pthreads-win32 library. */
  printf("-lpthreadGC2s -lws2_32\n");
#    else
  printf("-lpthreadGC2\n");
#    endif
#  endif
#  ifdef DGUX
  /*
   * You need gcc-3.0.3 to build this one!
   * DG/UX native gcc does not know what `-pthread` is.
   */
  printf("-ldl -pthread\n");
#  endif
#endif
  return 0;
}
