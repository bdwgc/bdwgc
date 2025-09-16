/*
 * Copyright 1988, 1989 Hans-J. Boehm, Alan J. Demers
 * Copyright (c) 1991-1994 by Xerox Corporation.  All rights reserved.
 * Copyright (c) 2008-2022 Ivan Maidanski
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

#include "private/gc_priv.h"

#if !defined(PLATFORM_MACH_DEP) && !defined(SN_TARGET_PSP2)

#  if defined(IA64) && !defined(THREADS)
GC_INNER ptr_t GC_save_regs_ret_val = NULL;
#  endif

#  if defined(UNIX_LIKE) && !defined(STACK_NOT_SCANNED)
#    include <signal.h>
#    ifndef NO_GETCONTEXT
#      if defined(DARWIN)                  \
          && (MAC_OS_X_VERSION_MAX_ALLOWED \
              >= 1060 /* `MAC_OS_X_VERSION_10_6` */)
#        include <sys/ucontext.h>
#      else
#        include <ucontext.h>
#      endif /* !DARWIN */
#      ifdef GETCONTEXT_FPU_EXCMASK_BUG
#        include <fenv.h>
#      endif
#    endif /* !NO_GETCONTEXT */
#  endif

GC_ATTR_NOINLINE GC_ATTR_NO_SANITIZE_ADDR GC_INNER void
GC_with_callee_saves_pushed(GC_with_callee_saves_func fn, ptr_t arg)
{
  volatile int dummy;
  volatile ptr_t context = 0;
#  if defined(EMSCRIPTEN) || defined(HAVE_BUILTIN_UNWIND_INIT)               \
      || defined(STACK_NOT_SCANNED) || (defined(NO_CRT) && defined(MSWIN32)) \
      || !defined(NO_UNDERSCORE_SETJMP)
#    define volatile_arg arg
#  else
  /*
   * Note: `volatile` to avoid "arg might be clobbered by setjmp" warning
   * produced by some compilers.
   */
  volatile ptr_t volatile_arg = arg;
#  endif

#  if defined(EMSCRIPTEN) || defined(STACK_NOT_SCANNED)
  /* No-op, "registers" are pushed in `GC_push_other_roots()`. */
#  else
#    if defined(UNIX_LIKE) && !defined(NO_GETCONTEXT)
  /*
   * Older versions of Darwin seem to lack `getcontext()`.
   * Linux on ARM and MIPS often does not provide a real `getcontext()`.
   */

  /* The variable is set to -1 (means broken) or 1 (means it works). */
  static signed char getcontext_works = 0;
  ucontext_t ctxt;
#      ifdef GETCONTEXT_FPU_EXCMASK_BUG
  /*
   * Workaround a bug (clearing the FPU exception mask) in `getcontext`
   * on Linux/x86_64.
   */
#        ifdef X86_64
  /*
   * We manipulate FPU control word here just not to force the client
   * application to use `-lm` linker option.
   */
  unsigned short old_fcw;

#          if defined(CPPCHECK)
  GC_noop1_ptr(&old_fcw);
#          endif
  __asm__ __volatile__("fstcw %0" : "=m"(*&old_fcw));
#        else
  int except_mask = fegetexcept();
#        endif
#      endif

  if (getcontext_works >= 0) {
    if (getcontext(&ctxt) < 0) {
      WARN("getcontext failed:"
           " using another register retrieval method...\n",
           0);
      /*
       * `getcontext()` is broken, do not try again.
       * E.g., to workaround a bug in Docker `ubuntu_32bit`.
       */
    } else {
      context = (ptr_t)&ctxt;
    }
    if (UNLIKELY(0 == getcontext_works))
      getcontext_works = context != NULL ? 1 : -1;
  }
#      ifdef GETCONTEXT_FPU_EXCMASK_BUG
#        ifdef X86_64
  __asm__ __volatile__("fldcw %0" : : "m"(*&old_fcw));
  {
    unsigned mxcsr;
    /* And now correct the exception mask in SSE `mxcsr`. */
    __asm__ __volatile__("stmxcsr %0" : "=m"(*&mxcsr));
    mxcsr = (mxcsr & ~(FE_ALL_EXCEPT << 7)) | ((old_fcw & FE_ALL_EXCEPT) << 7);
    __asm__ __volatile__("ldmxcsr %0" : : "m"(*&mxcsr));
  }
#        else /* !X86_64 */
  if (feenableexcept(except_mask) < 0)
    ABORT("feenableexcept failed");
#        endif
#      endif /* GETCONTEXT_FPU_EXCMASK_BUG */
#      if defined(IA64) || defined(SPARC)
  /*
   * On a register-window machine, we need to save register contents on
   * the stack for this to work.  This may already be subsumed by the
   * `getcontext()` call.
   */
#        if defined(IA64) && !defined(THREADS)
  GC_save_regs_ret_val =
#        endif
      GC_save_regs_in_stack();
#      endif
  if (NULL == context) /*< `getcontext` failed */
#    endif /* !NO_GETCONTEXT */
  {
#    if defined(HAVE_BUILTIN_UNWIND_INIT)
    /*
     * This was suggested as the way to force callee-save registers and
     * register windows onto the stack.
     */
    __builtin_unwind_init();
#    elif defined(NO_CRT) && defined(MSWIN32)
    CONTEXT ctx;

    RtlCaptureContext(&ctx);
#    else
    /* Generic code. */
    jmp_buf regs;

    /*
     * `setjmp()` does not always clear all of the buffer.
     * That tends to preserve garbage.  Clear it.
     */
    BZERO(regs, sizeof(regs));
#      ifdef NO_UNDERSCORE_SETJMP
    (void)setjmp(regs);
#      else
    /*
     * We do not want to mess with signals.  According to the SUSv3
     * (Single UNIX Specification v3), `setjmp` may or may not save
     * signal mask.  `_setjmp` will not, but is less portable.
     */
    (void)_setjmp(regs);
#      endif
#    endif
  }
#  endif
  /*
   * TODO: `context` here is sometimes just zero.  At the moment, the
   * callees do not really need it.  Cast `fn` to a `volatile` type
   * to prevent call inlining.
   */
  (*(GC_with_callee_saves_func volatile *)&fn)(
      volatile_arg, CAST_AWAY_VOLATILE_PVOID(context));
  /*
   * Strongly discourage the compiler from treating the above as a tail-call,
   * since that would pop the register contents before we get a chance to
   * look at them.
   */
  GC_noop1(COVERT_DATAFLOW(ADDR(&dummy)));
#  undef volatile_arg
}

#endif /* !PLATFORM_MACH_DEP && !SN_TARGET_PSP2 */
