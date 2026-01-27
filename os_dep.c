/*
 * Copyright 1988, 1989 Hans-J. Boehm, Alan J. Demers
 * Copyright (c) 1991-1995 by Xerox Corporation.  All rights reserved.
 * Copyright (c) 1996-1999 by Silicon Graphics.  All rights reserved.
 * Copyright (c) 1999 by Hewlett-Packard Company.  All rights reserved.
 * Copyright (c) 2008-2025 Ivan Maidanski
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

#include "private/gc_priv.h"

#if !defined(OS2) && !defined(PCR) && !defined(AMIGA) && !defined(MACOS) \
    && !defined(MSWINCE) && !defined(SN_TARGET_ORBIS) \
    && !defined(SN_TARGET_PSP2) && !defined(__CC_ARM)
# include <sys/types.h>
# if !defined(MSWIN32) && !defined(MSWIN_XBOX1)
#   include <unistd.h>
# endif
#endif

#include <stdio.h>
#if defined(MSWINCE) || defined(SN_TARGET_PS3)
# define SIGSEGV 0 /* value is irrelevant */
#else
# include <signal.h>
#endif

#if defined(UNIX_LIKE) || defined(CYGWIN32) || defined(NACL) \
    || defined(SYMBIAN)
# include <fcntl.h>
#endif

#if defined(LINUX) || defined(LINUX_STACKBOTTOM)
# include <ctype.h>
#endif

/* Blatantly OS dependent routines, except for those that are related   */
/* to dynamic loading.                                                  */

#ifdef AMIGA
# define GC_AMIGA_DEF
# include "extra/AmigaOS.c"
# undef GC_AMIGA_DEF
#endif

#ifdef MACOS
# include <Processes.h>
#endif

#ifdef IRIX5
# include <sys/uio.h>
# include <malloc.h>   /* for locking */
#endif

#if defined(MMAP_SUPPORTED) || defined(ADD_HEAP_GUARD_PAGES)
# if defined(USE_MUNMAP) && !defined(USE_MMAP) && !defined(CPPCHECK)
#   error Invalid config: USE_MUNMAP requires USE_MMAP
# endif
# include <sys/types.h>
# include <sys/mman.h>
# include <sys/stat.h>
#endif

#if defined(ADD_HEAP_GUARD_PAGES) || defined(LINUX_STACKBOTTOM) \
    || defined(MMAP_SUPPORTED) || defined(NEED_PROC_MAPS)
# include <errno.h>
#endif

#if defined(DARWIN) && !defined(DYNAMIC_LOADING) \
    && !defined(GC_DONT_REGISTER_MAIN_STATIC_DATA)
  /* for get_etext and friends */
# include <mach-o/getsect.h>
#endif

#ifdef DJGPP
  /* Apparently necessary for djgpp 2.01.  May cause problems with      */
  /* other versions.                                                    */
  typedef long unsigned int caddr_t;
#endif

#ifdef PCR
# include "il/PCR_IL.h"
# include "th/PCR_ThCtl.h"
# include "mm/PCR_MM.h"
#endif

#if defined(GC_DARWIN_THREADS) && defined(MPROTECT_VDB)
  /* Declare GC_mprotect_stop and GC_mprotect_resume as extern "C".     */
# include "private/darwin_stop_world.h"
#endif

#if !defined(NO_EXECUTE_PERMISSION)
  STATIC GC_bool GC_pages_executable = TRUE;
#else
  STATIC GC_bool GC_pages_executable = FALSE;
#endif
#define IGNORE_PAGES_EXECUTABLE 1
                        /* Undefined on GC_pages_executable real use.   */

#if ((defined(LINUX_STACKBOTTOM) || defined(NEED_PROC_MAPS) \
      || defined(PROC_VDB) || defined(SOFT_VDB)) && !defined(PROC_READ)) \
    || defined(CPPCHECK)
# define PROC_READ read
          /* Should probably call the real read, if read is wrapped.    */
#endif

#if defined(LINUX_STACKBOTTOM) || defined(NEED_PROC_MAPS)
  /* Repeatedly perform a read call until the buffer is filled  */
  /* up, or we encounter EOF or an error.                       */
  STATIC ssize_t GC_repeat_read(int fd, char *buf, size_t count)
  {
    size_t num_read = 0;

    ASSERT_CANCEL_DISABLED();
    while (num_read < count) {
        ssize_t result = PROC_READ(fd, buf + num_read, count - num_read);

        if (result < 0) return result;
        if (result == 0) break;
        num_read += result;
    }
    return num_read;
  }
#endif /* LINUX_STACKBOTTOM || NEED_PROC_MAPS */

#ifdef NEED_PROC_MAPS
/* We need to parse /proc/self/maps, either to find dynamic libraries,  */
/* and/or to find the register backing store base (IA64).  Do it once   */
/* here.                                                                */

#ifndef SINGLE_THREADED_PROCESS
  /* Determine the length of a file by incrementally reading it into a  */
  /* buffer.  This would be silly to use it on a file supporting lseek, */
  /* but Linux /proc files usually do not.                              */
  /* As of Linux 4.15.0, lseek(SEEK_END) fails for /proc/self/maps.     */
  STATIC size_t GC_get_file_len(int f)
  {
    size_t total = 0;
    ssize_t result;
#   define GET_FILE_LEN_BUF_SZ 500
    char buf[GET_FILE_LEN_BUF_SZ];

    do {
        result = PROC_READ(f, buf, sizeof(buf));
        if (result == -1) return 0;
        total += result;
    } while (result > 0);
    return total;
  }

  STATIC size_t GC_get_maps_len(void)
  {
    int f = open("/proc/self/maps", O_RDONLY);
    size_t result;
    if (f < 0) return 0; /* treat missing file as empty */
    result = GC_get_file_len(f);
    close(f);
    return result;
  }
#endif /* !SINGLE_THREADED_PROCESS */

/* Copy the contents of /proc/self/maps to a buffer in our address      */
/* space.  Return the address of the buffer.                            */
GC_INNER const char * GC_get_maps(void)
{
    ssize_t result;
    static char *maps_buf = NULL;
    static size_t maps_buf_sz = 1;
    size_t maps_size;
#   ifndef SINGLE_THREADED_PROCESS
      size_t old_maps_size = 0;
#   endif

    /* The buffer is essentially static, so there must be a single client. */
    GC_ASSERT(I_HOLD_LOCK());

    /* Note that in the presence of threads in the process      */
    /* (even if the collector itself is built single-threaded), */
    /* the maps file can essentially shrink asynchronously and  */
    /* unexpectedly as threads that we already think of as dead */
    /* release their stacks.  And there is no easy way to read  */
    /* the entire file atomically.  This is arguably            */
    /* a misfeature of the /proc/self/maps interface.           */
    /* Since we expect the file can grow asynchronously in rare */
    /* cases, it should suffice to first determine              */
    /* the size (using read), and then to reread the file.      */
    /* If the size is inconsistent we have to retry.            */
    /* This only matters with threads enabled, and if we use    */
    /* this to locate roots (not the default).                  */

#   ifndef SINGLE_THREADED_PROCESS
        /* Determine the initial size of /proc/self/maps.       */
        maps_size = GC_get_maps_len();
        if (0 == maps_size)
          ABORT("Cannot determine length of /proc/self/maps");
#   else
        maps_size = 4000;       /* Guess */
#   endif

    /* Read /proc/self/maps, growing maps_buf as necessary.     */
    /* Note that we may not allocate conventionally, and        */
    /* thus can't use stdio.                                    */
        do {
            int f;

            while (maps_size >= maps_buf_sz) {
#             ifdef LINT2
                /* Workaround passing tainted maps_buf to a tainted sink. */
                GC_noop1((word)maps_buf);
#             else
                GC_scratch_recycle_no_gww(maps_buf, maps_buf_sz);
#             endif
              /* Grow only by powers of 2, since we leak "too small" buffers.*/
              while (maps_size >= maps_buf_sz) maps_buf_sz *= 2;
              maps_buf = GC_scratch_alloc(maps_buf_sz);
              if (NULL == maps_buf)
                ABORT_ARG1("Insufficient space for /proc/self/maps buffer",
                        ", %lu bytes requested", (unsigned long)maps_buf_sz);
#             ifndef SINGLE_THREADED_PROCESS
                /* Recompute initial length, since we allocated.        */
                /* This can only happen a few times per program         */
                /* execution.                                           */
                maps_size = GC_get_maps_len();
                if (0 == maps_size)
                  ABORT("Cannot determine length of /proc/self/maps");
#             endif
            }
            GC_ASSERT(maps_buf_sz >= maps_size + 1);
            f = open("/proc/self/maps", O_RDONLY);
            if (-1 == f)
              ABORT_ARG1("Cannot open /proc/self/maps",
                         ": errno= %d", errno);
#           ifndef SINGLE_THREADED_PROCESS
              old_maps_size = maps_size;
#           endif
            maps_size = 0;
            do {
                result = GC_repeat_read(f, maps_buf, maps_buf_sz-1);
                if (result < 0) {
                  ABORT_ARG1("Failed to read /proc/self/maps",
                             ": errno= %d", errno);
                }
                maps_size += result;
            } while ((size_t)result == maps_buf_sz-1);
            close(f);
            if (0 == maps_size)
              ABORT("Empty /proc/self/maps");
#           ifndef SINGLE_THREADED_PROCESS
              if (maps_size > old_maps_size) {
                /* This might be caused by e.g. thread creation. */
                WARN("Unexpected asynchronous /proc/self/maps growth"
                     " (to %" WARN_PRIuPTR " bytes)\n", maps_size);
              }
#           endif
        } while (maps_size >= maps_buf_sz
#                ifndef SINGLE_THREADED_PROCESS
                   || maps_size < old_maps_size
#                endif
                );
        maps_buf[maps_size] = '\0';
        return maps_buf;
}

/*
 *  GC_parse_map_entry parses an entry from /proc/self/maps so we can
 *  locate all writable data segments that belong to shared libraries.
 *  The format of one of these entries and the fields we care about
 *  is as follows:
 *  XXXXXXXX-XXXXXXXX r-xp 00000000 30:05 260537     name of mapping...\n
 *  ^^^^^^^^ ^^^^^^^^ ^^^^          ^^
 *  start    end      prot          maj_dev
 *
 *  Note that since about august 2003 kernels, the columns no longer have
 *  fixed offsets on 64-bit kernels.  Hence we no longer rely on fixed offsets
 *  anywhere, which is safer anyway.
 */

/* Assign various fields of the first line in maps_ptr to (*start),     */
/* (*end), (*prot), (*maj_dev) and (*mapping_name).  mapping_name may   */
/* be NULL. (*prot) and (*mapping_name) are assigned pointers into the  */
/* original buffer.                                                     */
#if (defined(DYNAMIC_LOADING) && defined(USE_PROC_FOR_LIBRARIES)) \
    || defined(IA64) || defined(INCLUDE_LINUX_THREAD_DESCR) \
    || (defined(REDIRECT_MALLOC) && defined(GC_LINUX_THREADS))
  GC_INNER const char *GC_parse_map_entry(const char *maps_ptr,
                                          ptr_t *start, ptr_t *end,
                                          const char **prot, unsigned *maj_dev,
                                          const char **mapping_name)
  {
    const unsigned char *start_start, *end_start, *maj_dev_start;
    const unsigned char *p; /* unsigned for isspace, isxdigit */

    if (maps_ptr == NULL || *maps_ptr == '\0') {
        return NULL;
    }

    p = (const unsigned char *)maps_ptr;
    while (isspace(*p)) ++p;
    start_start = p;
    GC_ASSERT(isxdigit(*start_start));
    *start = (ptr_t)strtoul((const char *)start_start, (char **)&p, 16);
    GC_ASSERT(*p=='-');

    ++p;
    end_start = p;
    GC_ASSERT(isxdigit(*end_start));
    *end = (ptr_t)strtoul((const char *)end_start, (char **)&p, 16);
    GC_ASSERT(isspace(*p));

    while (isspace(*p)) ++p;
    GC_ASSERT(*p == 'r' || *p == '-');
    *prot = (const char *)p;
    /* Skip past protection field to offset field */
    while (!isspace(*p)) ++p;
    while (isspace(*p)) p++;
    GC_ASSERT(isxdigit(*p));
    /* Skip past offset field, which we ignore */
    while (!isspace(*p)) ++p;
    while (isspace(*p)) p++;
    maj_dev_start = p;
    GC_ASSERT(isxdigit(*maj_dev_start));
    *maj_dev = strtoul((const char *)maj_dev_start, NULL, 16);

    if (mapping_name != NULL) {
      while (*p && *p != '\n' && *p != '/' && *p != '[') p++;
      *mapping_name = (const char *)p;
    }
    while (*p && *p++ != '\n');
    return (const char *)p;
  }
#endif /* REDIRECT_MALLOC || DYNAMIC_LOADING || IA64 || ... */

#if defined(IA64) || defined(INCLUDE_LINUX_THREAD_DESCR)
  /* Try to read the backing store base from /proc/self/maps.           */
  /* Return the bounds of the writable mapping with a 0 major device,   */
  /* which includes the address passed as data.                         */
  /* Return FALSE if there is no such mapping.                          */
  GC_INNER GC_bool GC_enclosing_mapping(ptr_t addr, ptr_t *startp,
                                        ptr_t *endp)
  {
    const char *prot;
    ptr_t my_start, my_end;
    unsigned int maj_dev;
    const char *maps_ptr = GC_get_maps();

    for (;;) {
      maps_ptr = GC_parse_map_entry(maps_ptr, &my_start, &my_end,
                                    &prot, &maj_dev, 0);
      if (NULL == maps_ptr) break;

      if (prot[1] == 'w' && maj_dev == 0
          && (word)my_end > (word)addr && (word)my_start <= (word)addr) {
            *startp = my_start;
            *endp = my_end;
            return TRUE;
      }
    }
    return FALSE;
  }
#endif /* IA64 || INCLUDE_LINUX_THREAD_DESCR */

#if defined(REDIRECT_MALLOC) && defined(GC_LINUX_THREADS)
  /* Find the text(code) mapping for the library whose name, after      */
  /* stripping the directory part, starts with nm.                      */
  GC_INNER GC_bool GC_text_mapping(char *nm, ptr_t *startp, ptr_t *endp)
  {
    size_t nm_len = strlen(nm);
    const char *prot, *map_path;
    ptr_t my_start, my_end;
    unsigned int maj_dev;
    const char *maps_ptr = GC_get_maps();

    for (;;) {
      maps_ptr = GC_parse_map_entry(maps_ptr, &my_start, &my_end,
                                    &prot, &maj_dev, &map_path);
      if (NULL == maps_ptr) break;

      if (prot[0] == 'r' && prot[1] == '-' && prot[2] == 'x') {
          const char *p = map_path;

          /* Set p to point just past last slash, if any. */
            while (*p != '\0' && *p != '\n' && *p != ' ' && *p != '\t') ++p;
            while ((word)p >= (word)map_path && *p != '/') --p;
            ++p;
          if (strncmp(nm, p, nm_len) == 0) {
            *startp = my_start;
            *endp = my_end;
            return TRUE;
          }
      }
    }
    return FALSE;
  }
#endif /* REDIRECT_MALLOC */

#ifdef IA64
  static ptr_t backing_store_base_from_proc(void)
  {
    ptr_t my_start, my_end;
    if (!GC_enclosing_mapping(GC_save_regs_in_stack(), &my_start, &my_end)) {
        GC_COND_LOG_PRINTF("Failed to find backing store base from /proc\n");
        return 0;
    }
    return my_start;
  }
#endif

#endif /* NEED_PROC_MAPS */

#if defined(SEARCH_FOR_DATA_START)
  /* The x86 case can be handled without a search.  The Alpha case      */
  /* used to be handled differently as well, but the rules changed      */
  /* for recent Linux versions.  This seems to be the easiest way to    */
  /* cover all versions.                                                */

# if defined(LINUX) || defined(HURD)
    /* Some Linux distributions arrange to define __data_start.  Some   */
    /* define data_start as a weak symbol.  The latter is technically   */
    /* broken, since the user program may define data_start, in which   */
    /* case we lose.  Nonetheless, we try both, preferring __data_start.*/
    /* We assume gcc-compatible pragmas.                                */
    EXTERN_C_BEGIN
#   pragma weak __data_start
#   pragma weak data_start
    extern int __data_start[], data_start[];
    EXTERN_C_END
# endif /* LINUX */

  ptr_t GC_data_start = NULL;

  GC_INNER void GC_init_linux_data_start(void)
  {
    ptr_t data_end = DATAEND;

#   if (defined(LINUX) || defined(HURD)) && defined(USE_PROG_DATA_START)
      /* Try the easy approaches first: */
      /* However, this may lead to wrong data start value if libgc  */
      /* code is put into a shared library (directly or indirectly) */
      /* which is linked with -Bsymbolic-functions option.  Thus,   */
      /* the following is not used by default.                      */
      if (COVERT_DATAFLOW(__data_start) != 0) {
        GC_data_start = (ptr_t)(__data_start);
      } else {
        GC_data_start = (ptr_t)(data_start);
      }
      if (COVERT_DATAFLOW(GC_data_start) != 0) {
        if ((word)GC_data_start > (word)data_end)
          ABORT_ARG2("Wrong __data_start/_end pair",
                     ": %p .. %p", (void *)GC_data_start, (void *)data_end);
        return;
      }
#     ifdef DEBUG_ADD_DEL_ROOTS
        GC_log_printf("__data_start not provided\n");
#     endif
#   endif /* LINUX */

    if (GC_no_dls) {
      /* Not needed, avoids the SIGSEGV caused by       */
      /* GC_find_limit which complicates debugging.     */
      GC_data_start = data_end; /* set data root size to 0 */
      return;
    }

    GC_data_start = (ptr_t)GC_find_limit(data_end, FALSE);
  }
#endif /* SEARCH_FOR_DATA_START */

#ifdef ECOS

# ifndef ECOS_GC_MEMORY_SIZE
#   define ECOS_GC_MEMORY_SIZE (448 * 1024)
# endif /* ECOS_GC_MEMORY_SIZE */

  /* TODO: This is a simple way of allocating memory which is           */
  /* compatible with ECOS early releases.  Later releases use a more    */
  /* sophisticated means of allocating memory than this simple static   */
  /* allocator, but this method is at least bound to work.              */
  static char ecos_gc_memory[ECOS_GC_MEMORY_SIZE];
  static char *ecos_gc_brk = ecos_gc_memory;

  static void *tiny_sbrk(ptrdiff_t increment)
  {
    void *p = ecos_gc_brk;
    ecos_gc_brk += increment;
    if ((word)ecos_gc_brk > (word)(ecos_gc_memory + sizeof(ecos_gc_memory))) {
      ecos_gc_brk -= increment;
      return NULL;
    }
    return p;
  }
# define sbrk tiny_sbrk
#endif /* ECOS */

#if defined(NETBSD) && defined(__ELF__)
  ptr_t GC_data_start = NULL;

  EXTERN_C_BEGIN
  extern char **environ;
  EXTERN_C_END

  GC_INNER void GC_init_netbsd_elf(void)
  {
        /* This may need to be environ, without the underscore, for     */
        /* some versions.                                               */
    GC_data_start = (ptr_t)GC_find_limit(&environ, FALSE);
  }
#endif /* NETBSD */

#if defined(ADDRESS_SANITIZER) && !defined(CUSTOM_ASAN_DEF_OPTIONS)
  EXTERN_C_BEGIN
  GC_API const char *__asan_default_options(void);
  EXTERN_C_END

  /* Provide the Address Sanitizer runtime configuration.               */
  /* The function is exported just to be visible to the ASan library.   */
  /* FIXME: It might not work if the collector is built as a shared     */
  /* library unless the client provides own __asan_default_options()    */
  /* or sets ASAN_OPTIONS environment variable with the below flags.    */
  GC_API const char *__asan_default_options(void)
  {
    /* TODO: support ASan fake stacks properly. */
    return (
#     if defined(MPROTECT_VDB) || defined(NEED_FIND_LIMIT) \
         || defined(UNIX_LIKE)
        /* Use its own SIGBUS/SEGV handlers. */
        "allow_user_segv_handler=1:"
#     endif
      /* Do not to use fake stacks. */
      "detect_stack_use_after_return=0");
  }
#endif

#ifdef OPENBSD
  static struct sigaction old_segv_act;
  STATIC JMP_BUF GC_jmp_buf_openbsd;

  STATIC void GC_fault_handler_openbsd(int sig GC_ATTR_UNUSED)
  {
     LONGJMP(GC_jmp_buf_openbsd, 1);
  }

# ifdef GC_OPENBSD_UTHREADS
#   include <sys/syscall.h>
    EXTERN_C_BEGIN
    extern sigset_t __syscall(quad_t, ...);
    EXTERN_C_END

  /* Don't use GC_find_limit() because siglongjmp() outside of the      */
  /* signal handler by-passes our userland pthreads lib, leaving        */
  /* SIGSEGV and SIGPROF masked.  Instead, use this custom one that     */
  /* works-around the issues.                                           */

  /* Return the first non-addressable location > p or bound.    */
  /* Requires the allocation lock.                              */
  STATIC ptr_t GC_find_limit_openbsd(ptr_t p, ptr_t bound)
  {
    static volatile ptr_t result;
             /* Safer if static, since otherwise it may not be  */
             /* preserved across the longjmp.  Can safely be    */
             /* static since it's only called with the          */
             /* allocation lock held.                           */

    struct sigaction act;
    word pgsz = (word)sysconf(_SC_PAGESIZE);

    GC_ASSERT((word)bound >= pgsz);
    GC_ASSERT(I_HOLD_LOCK());

    act.sa_handler = GC_fault_handler_openbsd;
    sigemptyset(&act.sa_mask);
    act.sa_flags = SA_NODEFER | SA_RESTART;
    /* act.sa_restorer is deprecated and should not be initialized. */
    sigaction(SIGSEGV, &act, &old_segv_act);

    if (SETJMP(GC_jmp_buf_openbsd) == 0) {
      result = (ptr_t)((word)p & ~(pgsz-1));
      for (;;) {
        if ((word)result >= (word)bound - pgsz) {
          result = bound;
          break;
        }
        result += pgsz; /* no overflow expected */
        GC_noop1((word)(*result));
      }
    }

#   ifdef THREADS
      /* Due to the siglongjump we need to manually unmask SIGPROF.     */
      __syscall(SYS_sigprocmask, SIG_UNBLOCK, sigmask(SIGPROF));
#   endif

    sigaction(SIGSEGV, &old_segv_act, 0);
    return(result);
  }
# endif /* GC_OPENBSD_UTHREADS */

  static volatile int firstpass;

  /* Return first addressable location > p or bound.    */
  /* Requires the allocation lock.                      */
  STATIC ptr_t GC_skip_hole_openbsd(ptr_t p, ptr_t bound)
  {
    static volatile ptr_t result;

    struct sigaction act;
    word pgsz = (word)sysconf(_SC_PAGESIZE);

    GC_ASSERT((word)bound >= pgsz);
    GC_ASSERT(I_HOLD_LOCK());

    act.sa_handler = GC_fault_handler_openbsd;
    sigemptyset(&act.sa_mask);
    act.sa_flags = SA_NODEFER | SA_RESTART;
    /* act.sa_restorer is deprecated and should not be initialized. */
    sigaction(SIGSEGV, &act, &old_segv_act);

    firstpass = 1;
    result = (ptr_t)((word)p & ~(pgsz-1));
    if (SETJMP(GC_jmp_buf_openbsd) != 0 || firstpass) {
      firstpass = 0;
      if ((word)result >= (word)bound - pgsz) {
        result = bound;
      } else {
        result += pgsz; /* no overflow expected */
        GC_noop1((word)(unsigned char)(*result));
      }
    }

    sigaction(SIGSEGV, &old_segv_act, 0);
    return(result);
  }
#endif /* OPENBSD */

# ifdef OS2

# include <stddef.h>

# if !defined(__IBMC__) && !defined(__WATCOMC__) /* e.g. EMX */

struct exe_hdr {
    unsigned short      magic_number;
    unsigned short      padding[29];
    long                new_exe_offset;
};

#define E_MAGIC(x)      (x).magic_number
#define EMAGIC          0x5A4D
#define E_LFANEW(x)     (x).new_exe_offset

struct e32_exe {
    unsigned char       magic_number[2];
    unsigned char       byte_order;
    unsigned char       word_order;
    unsigned long       exe_format_level;
    unsigned short      cpu;
    unsigned short      os;
    unsigned long       padding1[13];
    unsigned long       object_table_offset;
    unsigned long       object_count;
    unsigned long       padding2[31];
};

#define E32_MAGIC1(x)   (x).magic_number[0]
#define E32MAGIC1       'L'
#define E32_MAGIC2(x)   (x).magic_number[1]
#define E32MAGIC2       'X'
#define E32_BORDER(x)   (x).byte_order
#define E32LEBO         0
#define E32_WORDER(x)   (x).word_order
#define E32LEWO         0
#define E32_CPU(x)      (x).cpu
#define E32CPU286       1
#define E32_OBJTAB(x)   (x).object_table_offset
#define E32_OBJCNT(x)   (x).object_count

struct o32_obj {
    unsigned long       size;
    unsigned long       base;
    unsigned long       flags;
    unsigned long       pagemap;
    unsigned long       mapsize;
    unsigned long       reserved;
};

#define O32_FLAGS(x)    (x).flags
#define OBJREAD         0x0001L
#define OBJWRITE        0x0002L
#define OBJINVALID      0x0080L
#define O32_SIZE(x)     (x).size
#define O32_BASE(x)     (x).base

# else  /* IBM's compiler */

/* A kludge to get around what appears to be a header file bug */
# ifndef WORD
#   define WORD unsigned short
# endif
# ifndef DWORD
#   define DWORD unsigned long
# endif

# define EXE386 1
# include <newexe.h>
# include <exe386.h>

# endif  /* __IBMC__ */

# define INCL_DOSEXCEPTIONS
# define INCL_DOSPROCESS
# define INCL_DOSERRORS
# define INCL_DOSMODULEMGR
# define INCL_DOSMEMMGR
# include <os2.h>

# endif /* OS/2 */

/* Find the page size */
GC_INNER size_t GC_page_size = 0;

#if defined(MSWIN32) || defined(MSWINCE) || defined(CYGWIN32)
# ifndef VER_PLATFORM_WIN32_CE
#   define VER_PLATFORM_WIN32_CE 3
# endif

# if defined(MSWINCE) && defined(THREADS)
    GC_INNER GC_bool GC_dont_query_stack_min = FALSE;
# endif

  GC_INNER SYSTEM_INFO GC_sysinfo;

  GC_INNER void GC_setpagesize(void)
  {
    GetSystemInfo(&GC_sysinfo);
#   if defined(CYGWIN32) && (defined(MPROTECT_VDB) || defined(USE_MUNMAP))
      /* Allocations made with mmap() are aligned to the allocation     */
      /* granularity, which (at least on Win64) is not the              */
      /* same as the page size.  Probably a separate variable could     */
      /* be added to distinguish the allocation granularity from the    */
      /* actual page size, but in practice there is no good reason to   */
      /* make allocations smaller than dwAllocationGranularity, so we   */
      /* just use it instead of the actual page size here (as Cygwin    */
      /* itself does in many cases).                                    */
      GC_page_size = (size_t)GC_sysinfo.dwAllocationGranularity;
      GC_ASSERT(GC_page_size >= (size_t)GC_sysinfo.dwPageSize);
#   else
      GC_page_size = (size_t)GC_sysinfo.dwPageSize;
#   endif
#   if defined(MSWINCE) && !defined(_WIN32_WCE_EMULATION)
      {
        OSVERSIONINFO verInfo;
        /* Check the current WinCE version.     */
        verInfo.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);
        if (!GetVersionEx(&verInfo))
          ABORT("GetVersionEx failed");
        if (verInfo.dwPlatformId == VER_PLATFORM_WIN32_CE &&
            verInfo.dwMajorVersion < 6) {
          /* Only the first 32 MB of address space belongs to the       */
          /* current process (unless WinCE 6.0+ or emulation).          */
          GC_sysinfo.lpMaximumApplicationAddress = (LPVOID)((word)32 << 20);
#         ifdef THREADS
            /* On some old WinCE versions, it's observed that           */
            /* VirtualQuery calls don't work properly when used to      */
            /* get thread current stack committed minimum.              */
            if (verInfo.dwMajorVersion < 5)
              GC_dont_query_stack_min = TRUE;
#         endif
        }
      }
#   endif
  }

# ifndef CYGWIN32
#   define is_writable(prot) ((prot) == PAGE_READWRITE \
                            || (prot) == PAGE_WRITECOPY \
                            || (prot) == PAGE_EXECUTE_READWRITE \
                            || (prot) == PAGE_EXECUTE_WRITECOPY)
    /* Return the number of bytes that are writable starting at p.      */
    /* The pointer p is assumed to be page aligned.                     */
    /* If base is not 0, *base becomes the beginning of the             */
    /* allocation region containing p.                                  */
    STATIC word GC_get_writable_length(ptr_t p, ptr_t *base)
    {
      MEMORY_BASIC_INFORMATION buf;
      word result;
      word protect;

      result = VirtualQuery(p, &buf, sizeof(buf));
      if (result != sizeof(buf)) ABORT("Weird VirtualQuery result");
      if (base != 0) *base = (ptr_t)(buf.AllocationBase);
      protect = buf.Protect & ~(word)(PAGE_GUARD | PAGE_NOCACHE);
      if (!is_writable(protect)) {
        return(0);
      }
      if (buf.State != MEM_COMMIT) return(0);
      return(buf.RegionSize);
    }

    GC_API int GC_CALL GC_get_stack_base(struct GC_stack_base *sb)
    {
      ptr_t trunc_sp;
      word size;

      /* Set page size if it is not ready (so client can use this       */
      /* function even before GC is initialized).                       */
      if (!GC_page_size) GC_setpagesize();

      trunc_sp = (ptr_t)((word)GC_approx_sp() & ~(word)(GC_page_size-1));
      /* FIXME: This won't work if called from a deeply recursive       */
      /* client code (and the committed stack space has grown).         */
      size = GC_get_writable_length(trunc_sp, 0);
      GC_ASSERT(size != 0);
      sb -> mem_base = trunc_sp + size;
      return GC_SUCCESS;
    }
# else /* CYGWIN32 */
    /* An alternate version for Cygwin (adapted from Dave Korn's        */
    /* gcc version of boehm-gc).                                        */
    GC_API int GC_CALL GC_get_stack_base(struct GC_stack_base *sb)
    {
#     ifdef X86_64
        sb -> mem_base = ((NT_TIB*)NtCurrentTeb())->StackBase;
#     else
        void * _tlsbase;

        __asm__ ("movl %%fs:4, %0"
                 : "=r" (_tlsbase));
        sb -> mem_base = _tlsbase;
#     endif
      return GC_SUCCESS;
    }
# endif /* CYGWIN32 */
# define HAVE_GET_STACK_BASE

#else /* !MSWIN32 */
  GC_INNER void GC_setpagesize(void)
  {
#   if defined(MPROTECT_VDB) || defined(PROC_VDB) || defined(SOFT_VDB) \
       || defined(USE_MMAP)
      GC_page_size = (size_t)GETPAGESIZE();
#     if !defined(CPPCHECK)
        if (0 == GC_page_size)
          ABORT("getpagesize failed");
#     endif
#   else
      /* It's acceptable to fake it.    */
      GC_page_size = HBLKSIZE;
#   endif
  }
#endif /* !MSWIN32 */

#ifdef OS2
  GC_API int GC_CALL GC_get_stack_base(struct GC_stack_base *sb)
  {
    PTIB ptib; /* thread information block */
    PPIB ppib;
    if (DosGetInfoBlocks(&ptib, &ppib) != NO_ERROR) {
      WARN("DosGetInfoBlocks failed\n", 0);
      return GC_UNIMPLEMENTED;
    }
    sb->mem_base = ptib->tib_pstacklimit;
    return GC_SUCCESS;
  }
# define HAVE_GET_STACK_BASE
#endif /* OS2 */

#ifdef SERENITY
# include <serenity.h>

GC_API int GC_CALL
GC_get_stack_base(struct GC_stack_base *sb)
{
  uintptr_t base;
  size_t size;

  if (get_stack_bounds(&base, &size) < 0) {
    WARN("get_stack_bounds failed\n", 0);
    return GC_UNIMPLEMENTED;
  }
  sb->mem_base = (void *)(base + size);
  return GC_SUCCESS;
}
# define HAVE_GET_STACK_BASE
#endif /* SERENITY */

# ifdef AMIGA
#   define GC_AMIGA_SB
#   include "extra/AmigaOS.c"
#   undef GC_AMIGA_SB
#   define GET_MAIN_STACKBASE_SPECIAL
# endif /* AMIGA */

# if defined(NEED_FIND_LIMIT) \
     || (defined(UNIX_LIKE) && !defined(NO_DEBUGGING)) \
     || (defined(USE_PROC_FOR_LIBRARIES) && defined(THREADS)) \
     || (defined(WRAP_MARK_SOME) && defined(__GNUC__))

    typedef void (*GC_fault_handler_t)(int);

#   if defined(SUNOS5SIGS) || defined(IRIX5) || defined(OSF1) \
       || defined(HAIKU) || defined(HURD) || defined(FREEBSD) \
       || defined(NETBSD) || defined(SERENITY)
        static struct sigaction old_segv_act;
#       if defined(_sigargs) /* !Irix6.x */ || defined(HPUX) \
           || defined(HURD) || defined(NETBSD) || defined(FREEBSD) \
           || defined(SERENITY)
            static struct sigaction old_bus_act;
#       endif
#   elif !defined(OPENBSD)
      static GC_fault_handler_t old_segv_handler;
#     ifdef HAVE_SIGBUS
        static GC_fault_handler_t old_bus_handler;
#     endif
#   endif

    GC_INNER void GC_set_and_save_fault_handler(GC_fault_handler_t h)
    {
#       if defined(SUNOS5SIGS) || defined(IRIX5) || defined(OSF1) \
           || defined(HAIKU) || defined(HURD) || defined(FREEBSD) \
           || defined(NETBSD) || defined(OPENBSD) || defined(SERENITY)
          struct sigaction act;

          act.sa_handler = h;
#         ifdef SIGACTION_FLAGS_NODEFER_HACK
            /* Was necessary for Solaris 2.3 and very temporary */
            /* NetBSD bugs.                                     */
            act.sa_flags = SA_RESTART | SA_NODEFER;
#         else
            act.sa_flags = SA_RESTART;
#         endif

          (void) sigemptyset(&act.sa_mask);
          /* act.sa_restorer is deprecated and should not be initialized. */
#         ifdef GC_IRIX_THREADS
            /* Older versions have a bug related to retrieving and      */
            /* setting a handler at the same time.                      */
            (void) sigaction(SIGSEGV, 0, &old_segv_act);
            (void) sigaction(SIGSEGV, &act, 0);
#         else
            (void) sigaction(SIGSEGV, &act, &old_segv_act);
#           if defined(IRIX5) && defined(_sigargs) /* Irix 5.x, not 6.x */ \
               || defined(HPUX) || defined(HURD) || defined(NETBSD) \
               || defined(FREEBSD) || defined(SERENITY)
              /* Under Irix 5.x or HP/UX, we may get SIGBUS.    */
              /* Pthreads doesn't exist under Irix 5.x, so we   */
              /* don't have to worry in the threads case.       */
              (void) sigaction(SIGBUS, &act, &old_bus_act);
#           endif
#         endif /* !GC_IRIX_THREADS */
#       else
          old_segv_handler = signal(SIGSEGV, h);
#         ifdef HAVE_SIGBUS
            old_bus_handler = signal(SIGBUS, h);
#         endif
#       endif
#       if defined(CPPCHECK) && defined(ADDRESS_SANITIZER)
          GC_noop1((word)&__asan_default_options);
#       endif
    }
# endif /* NEED_FIND_LIMIT || UNIX_LIKE */

# if defined(NEED_FIND_LIMIT) \
     || (defined(WRAP_MARK_SOME) && defined(__GNUC__)) \
     || (defined(USE_PROC_FOR_LIBRARIES) && defined(THREADS))
  /* Some tools to implement HEURISTIC2 */
#   define MIN_PAGE_SIZE 256    /* Smallest conceivable page size, bytes */

    GC_INNER JMP_BUF GC_jmp_buf;

    STATIC void GC_fault_handler(int sig GC_ATTR_UNUSED)
    {
        LONGJMP(GC_jmp_buf, 1);
    }

    GC_INNER void GC_setup_temporary_fault_handler(void)
    {
        /* Handler is process-wide, so this should only happen in       */
        /* one thread at a time.                                        */
        GC_ASSERT(I_HOLD_LOCK());
        GC_set_and_save_fault_handler(GC_fault_handler);
    }

    GC_INNER void GC_reset_fault_handler(void)
    {
#       if defined(SUNOS5SIGS) || defined(IRIX5) || defined(OSF1) \
           || defined(HAIKU) || defined(HURD) || defined(FREEBSD) \
           || defined(NETBSD) || defined(OPENBSD) || defined(SERENITY)
          (void) sigaction(SIGSEGV, &old_segv_act, 0);
#         if defined(IRIX5) && defined(_sigargs) /* Irix 5.x, not 6.x */ \
             || defined(HPUX) || defined(HURD) || defined(NETBSD) \
             || defined(FREEBSD) || defined(SERENITY)
              (void) sigaction(SIGBUS, &old_bus_act, 0);
#         endif
#       else
          (void) signal(SIGSEGV, old_segv_handler);
#         ifdef HAVE_SIGBUS
            (void) signal(SIGBUS, old_bus_handler);
#         endif
#       endif
    }

    /* Return the first non-addressable location > p (up) or    */
    /* the smallest location q s.t. [q,p) is addressable (!up). */
    /* We assume that p (up) or p-1 (!up) is addressable.       */
    /* Requires allocation lock.                                */
    GC_ATTR_NO_SANITIZE_ADDR
    STATIC ptr_t GC_find_limit_with_bound(ptr_t p, GC_bool up, ptr_t bound)
    {
        static volatile ptr_t result;
                /* Safer if static, since otherwise it may not be       */
                /* preserved across the longjmp.  Can safely be         */
                /* static since it's only called with the               */
                /* allocation lock held.                                */

        GC_ASSERT(up ? (word)bound >= MIN_PAGE_SIZE
                     : (word)bound <= ~(word)MIN_PAGE_SIZE);
        GC_ASSERT(I_HOLD_LOCK());
        GC_setup_temporary_fault_handler();
        if (SETJMP(GC_jmp_buf) == 0) {
            result = (ptr_t)((word)p & ~(word)(MIN_PAGE_SIZE-1));
            for (;;) {
                if (up) {
                    if ((word)result >= (word)bound - MIN_PAGE_SIZE) {
                      result = bound;
                      break;
                    }
                    result += MIN_PAGE_SIZE; /* no overflow expected */
                } else {
                    if ((word)result <= (word)bound + MIN_PAGE_SIZE) {
                      result = bound - MIN_PAGE_SIZE;
                                        /* This is to compensate        */
                                        /* further result increment (we */
                                        /* do not modify "up" variable  */
                                        /* since it might be clobbered  */
                                        /* by setjmp otherwise).        */
                      break;
                    }
                    result -= MIN_PAGE_SIZE; /* no underflow expected */
                }
                GC_noop1((word)(unsigned char)(*result));
            }
        }
        GC_reset_fault_handler();
        if (!up) {
            result += MIN_PAGE_SIZE;
        }
        return(result);
    }

    void * GC_find_limit(void * p, int up)
    {
        return GC_find_limit_with_bound((ptr_t)p, (GC_bool)up,
                                        up ? (ptr_t)GC_WORD_MAX : 0);
    }
# endif /* NEED_FIND_LIMIT || USE_PROC_FOR_LIBRARIES */

#ifdef HPUX_MAIN_STACKBOTTOM
# include <sys/param.h>
# include <sys/pstat.h>

  STATIC ptr_t GC_hpux_main_stack_base(void)
  {
    struct pst_vm_status vm_status;
    int i = 0;

    while (pstat_getprocvm(&vm_status, sizeof(vm_status), 0, i++) == 1) {
      if (vm_status.pst_type == PS_STACK)
        return (ptr_t)vm_status.pst_vaddr;
    }

    /* Old way to get the stack bottom. */
#   ifdef STACK_GROWS_UP
      return (ptr_t)GC_find_limit(GC_approx_sp(), /* up= */ FALSE);
#   else /* not HP_PA */
      return (ptr_t)GC_find_limit(GC_approx_sp(), TRUE);
#   endif
  }
#endif /* HPUX_MAIN_STACKBOTTOM */

#ifdef HPUX_STACKBOTTOM

#include <sys/param.h>
#include <sys/pstat.h>

  GC_INNER ptr_t GC_get_register_stack_base(void)
  {
    struct pst_vm_status vm_status;

    int i = 0;
    while (pstat_getprocvm(&vm_status, sizeof(vm_status), 0, i++) == 1) {
      if (vm_status.pst_type == PS_RSESTACK) {
        return (ptr_t) vm_status.pst_vaddr;
      }
    }

    /* old way to get the register stackbottom */
    return (ptr_t)(((word)GC_stackbottom - BACKING_STORE_DISPLACEMENT - 1)
                   & ~(word)(BACKING_STORE_ALIGNMENT-1));
  }

#endif /* HPUX_STACKBOTTOM */

#ifdef LINUX_STACKBOTTOM

# include <sys/types.h>
# include <sys/stat.h>

# define STAT_SKIP 27   /* Number of fields preceding startstack        */
                        /* field in /proc/self/stat                     */

# ifdef USE_LIBC_PRIVATES
    EXTERN_C_BEGIN
#   pragma weak __libc_stack_end
    extern ptr_t __libc_stack_end;
#   ifdef IA64
#     pragma weak __libc_ia64_register_backing_store_base
      extern ptr_t __libc_ia64_register_backing_store_base;
#   endif
    EXTERN_C_END
# endif

# ifdef IA64
    GC_INNER ptr_t GC_get_register_stack_base(void)
    {
      ptr_t result;

#     ifdef USE_LIBC_PRIVATES
        if (0 != &__libc_ia64_register_backing_store_base
            && 0 != __libc_ia64_register_backing_store_base) {
          /* Glibc 2.2.4 has a bug such that for dynamically linked     */
          /* executables __libc_ia64_register_backing_store_base is     */
          /* defined but uninitialized during constructor calls.        */
          /* Hence we check for both nonzero address and value.         */
          return __libc_ia64_register_backing_store_base;
        }
#     endif
      result = backing_store_base_from_proc();
      if (0 == result) {
          result = (ptr_t)GC_find_limit(GC_save_regs_in_stack(), FALSE);
          /* This works better than a constant displacement heuristic.  */
      }
      return result;
    }
# endif /* IA64 */

  STATIC ptr_t GC_linux_main_stack_base(void)
  {
    /* We read the stack bottom value from /proc/self/stat.  We do this */
    /* using direct I/O system calls in order to avoid calling malloc   */
    /* in case REDIRECT_MALLOC is defined.                              */
#     define STAT_BUF_SIZE 4096
    char stat_buf[STAT_BUF_SIZE];
    int f;
    word result;
    ssize_t i, buf_offset = 0, len;

    /* First try the easy way.  This should work for glibc 2.2  */
    /* This fails in a prelinked ("prelink" command) executable */
    /* since the correct value of __libc_stack_end never        */
    /* becomes visible to us.  The second test works around     */
    /* this.                                                    */
#   ifdef USE_LIBC_PRIVATES
      if (0 != &__libc_stack_end && 0 != __libc_stack_end ) {
#       if defined(IA64)
          /* Some versions of glibc set the address 16 bytes too        */
          /* low while the initialization code is running.              */
          if (((word)__libc_stack_end & 0xfff) + 0x10 < 0x1000) {
            return __libc_stack_end + 0x10;
          } /* Otherwise it's not safe to add 16 bytes and we fall      */
            /* back to using /proc.                                     */
#       elif defined(SPARC)
          /* Older versions of glibc for 64-bit SPARC do not set this   */
          /* variable correctly, it gets set to either zero or one.     */
          if (__libc_stack_end != (ptr_t) (unsigned long)0x1)
            return __libc_stack_end;
#       else
          return __libc_stack_end;
#       endif
      }
#   endif

    f = open("/proc/self/stat", O_RDONLY);
    if (-1 == f)
      ABORT_ARG1("Could not open /proc/self/stat", ": errno= %d", errno);
    len = GC_repeat_read(f, stat_buf, sizeof(stat_buf));
    if (len < 0)
      ABORT_ARG1("Failed to read /proc/self/stat",
                 ": errno= %d", errno);
    close(f);

    /* Skip the required number of fields.  This number is hopefully    */
    /* constant across all Linux implementations.                       */
    for (i = 0; i < STAT_SKIP; ++i) {
      while (buf_offset < len && isspace(stat_buf[buf_offset++])) {
        /* empty */
      }
      while (buf_offset < len && !isspace(stat_buf[buf_offset++])) {
        /* empty */
      }
    }
    /* Skip spaces.     */
    while (buf_offset < len && isspace(stat_buf[buf_offset])) {
      buf_offset++;
    }
    /* Find the end of the number and cut the buffer there.     */
    for (i = 0; buf_offset + i < len; i++) {
      if (!isdigit(stat_buf[buf_offset + i])) break;
    }
    if (buf_offset + i >= len) ABORT("Could not parse /proc/self/stat");
    stat_buf[buf_offset + i] = '\0';

    result = (word)STRTOULL(&stat_buf[buf_offset], NULL, 10);
    if (result < 0x100000 || (result & (ALIGNMENT - 1)) != 0)
      ABORT_ARG1("Absurd stack bottom value",
                 ": 0x%lx", (unsigned long)result);
    return (ptr_t)result;
  }
#endif /* LINUX_STACKBOTTOM */

#ifdef FREEBSD_STACKBOTTOM
  /* This uses an undocumented sysctl call, but at least one expert     */
  /* believes it will stay.                                             */

# include <unistd.h>
# include <sys/types.h>
# include <sys/sysctl.h>

  STATIC ptr_t GC_freebsd_main_stack_base(void)
  {
    int nm[2] = {CTL_KERN, KERN_USRSTACK};
    ptr_t base;
    size_t len = sizeof(ptr_t);
    int r = sysctl(nm, 2, &base, &len, NULL, 0);
    if (r) ABORT("Error getting main stack base");
    return base;
  }
#endif /* FREEBSD_STACKBOTTOM */

#if defined(ECOS) || defined(NOSYS)
  ptr_t GC_get_main_stack_base(void)
  {
    return STACKBOTTOM;
  }
# define GET_MAIN_STACKBASE_SPECIAL
#elif defined(SYMBIAN)
  EXTERN_C_BEGIN
  extern int GC_get_main_symbian_stack_base(void);
  EXTERN_C_END

  ptr_t GC_get_main_stack_base(void)
  {
    return (ptr_t)GC_get_main_symbian_stack_base();
  }
# define GET_MAIN_STACKBASE_SPECIAL
#elif defined(EMSCRIPTEN)

# if defined(USE_EMSCRIPTEN_SCAN_STACK) && defined(EMSCRIPTEN_ASYNCIFY)
    /* According to the documentation, emscripten_scan_stack() is only  */
    /* guaranteed to be available when building with ASYNCIFY.          */
#   include <emscripten.h>

    static void *emscripten_stack_base;

    static void scan_stack_cb(void *begin, void *end)
    {
      (void)begin;
      emscripten_stack_base = end;
    }
# else
#   include <emscripten/stack.h>
# endif

  ptr_t GC_get_main_stack_base(void)
  {
#   if defined(USE_EMSCRIPTEN_SCAN_STACK) && defined(EMSCRIPTEN_ASYNCIFY)
      emscripten_scan_stack(scan_stack_cb);
      return (ptr_t)emscripten_stack_base;
#   else
      return (ptr_t)emscripten_stack_get_base();
#   endif
  }
# define GET_MAIN_STACKBASE_SPECIAL
#elif !defined(AMIGA) && !defined(OS2) && !defined(MSWIN32) \
      && !defined(MSWINCE) && !defined(CYGWIN32) \
      && !defined(GC_OPENBSD_THREADS) && !defined(SERENITY) \
      && (!defined(GC_SOLARIS_THREADS) || defined(_STRICT_STDC))

# if (defined(HAVE_PTHREAD_ATTR_GET_NP) || defined(HAVE_PTHREAD_GETATTR_NP)) \
     && (defined(THREADS) || defined(USE_GET_STACKBASE_FOR_MAIN))
#   include <pthread.h>
#   ifdef HAVE_PTHREAD_NP_H
#     include <pthread_np.h> /* for pthread_attr_get_np() */
#   endif
# elif defined(DARWIN) && !defined(NO_PTHREAD_GET_STACKADDR_NP)
    /* We could use pthread_get_stackaddr_np even in case of a  */
    /* single-threaded gclib (there is no -lpthread on Darwin). */
#   include <pthread.h>
#   undef STACKBOTTOM
#   define STACKBOTTOM (ptr_t)pthread_get_stackaddr_np(pthread_self())
# endif

  ptr_t GC_get_main_stack_base(void)
  {
    ptr_t result;
#   if (defined(HAVE_PTHREAD_ATTR_GET_NP) \
        || defined(HAVE_PTHREAD_GETATTR_NP)) \
       && (defined(USE_GET_STACKBASE_FOR_MAIN) \
           || (defined(THREADS) && !defined(REDIRECT_MALLOC)))
      pthread_attr_t attr;
      void *stackaddr;
      size_t size;

#     ifdef HAVE_PTHREAD_ATTR_GET_NP
        if (pthread_attr_init(&attr) == 0
            && (pthread_attr_get_np(pthread_self(), &attr) == 0
                ? TRUE : (pthread_attr_destroy(&attr), FALSE)))
#     else /* HAVE_PTHREAD_GETATTR_NP */
        if (pthread_getattr_np(pthread_self(), &attr) == 0)
#     endif
      {
        if (pthread_attr_getstack(&attr, &stackaddr, &size) == 0
            && stackaddr != NULL) {
          (void)pthread_attr_destroy(&attr);
#         ifdef STACK_GROWS_DOWN
            stackaddr = (char *)stackaddr + size;
#         endif
          return (ptr_t)stackaddr;
        }
        (void)pthread_attr_destroy(&attr);
      }
      WARN("pthread_getattr_np or pthread_attr_getstack failed"
           " for main thread\n", 0);
#   endif
#   ifdef STACKBOTTOM
      result = STACKBOTTOM;
#   else
#     ifdef HEURISTIC1
#       define STACKBOTTOM_ALIGNMENT_M1 ((word)STACK_GRAN - 1)
#       ifdef STACK_GROWS_DOWN
          result = (ptr_t)(((word)GC_approx_sp() + STACKBOTTOM_ALIGNMENT_M1)
                           & ~(word)STACKBOTTOM_ALIGNMENT_M1);
#       else
          result = (ptr_t)((word)GC_approx_sp()
                           & ~(word)STACKBOTTOM_ALIGNMENT_M1);
#       endif
#     elif defined(HPUX_MAIN_STACKBOTTOM)
        result = GC_hpux_main_stack_base();
#     elif defined(LINUX_STACKBOTTOM)
        result = GC_linux_main_stack_base();
#     elif defined(FREEBSD_STACKBOTTOM)
        result = GC_freebsd_main_stack_base();
#     elif defined(HEURISTIC2)
        {
          ptr_t sp = GC_approx_sp();
#         ifdef STACK_GROWS_DOWN
            result = (ptr_t)GC_find_limit(sp, TRUE);
#           if defined(HEURISTIC2_LIMIT) && !defined(CPPCHECK)
              if ((word)result > (word)HEURISTIC2_LIMIT
                  && (word)sp < (word)HEURISTIC2_LIMIT) {
                result = HEURISTIC2_LIMIT;
              }
#           endif
#         else
            result = (ptr_t)GC_find_limit(sp, FALSE);
#           if defined(HEURISTIC2_LIMIT) && !defined(CPPCHECK)
              if ((word)result < (word)HEURISTIC2_LIMIT
                  && (word)sp > (word)HEURISTIC2_LIMIT) {
                result = HEURISTIC2_LIMIT;
              }
#           endif
#         endif
        }
#     elif defined(STACK_NOT_SCANNED) || defined(CPPCHECK)
        result = NULL;
#     else
#       error None of HEURISTIC* and *STACKBOTTOM defined!
#     endif
#     if defined(STACK_GROWS_DOWN) && !defined(CPPCHECK)
        if (result == 0)
          result = (ptr_t)(signed_word)(-sizeof(ptr_t));
#     endif
#   endif
#   if !defined(CPPCHECK)
      GC_ASSERT((word)GC_approx_sp() HOTTER_THAN (word)result);
#   endif
    return(result);
  }
# define GET_MAIN_STACKBASE_SPECIAL
#endif /* !AMIGA, !OPENBSD, !OS2, !SERENITY, !Windows */

#if (defined(HAVE_PTHREAD_ATTR_GET_NP) || defined(HAVE_PTHREAD_GETATTR_NP)) \
    && defined(THREADS) && !defined(HAVE_GET_STACK_BASE)
# include <pthread.h>
# ifdef HAVE_PTHREAD_NP_H
#   include <pthread_np.h>
# endif

  GC_API int GC_CALL GC_get_stack_base(struct GC_stack_base *b)
  {
    pthread_attr_t attr;
    size_t size;
#   ifdef IA64
      DCL_LOCK_STATE;
#   endif

#   ifdef HAVE_PTHREAD_ATTR_GET_NP
      if (pthread_attr_init(&attr) != 0)
        ABORT("pthread_attr_init failed");
      if (pthread_attr_get_np(pthread_self(), &attr) != 0) {
        WARN("pthread_attr_get_np failed\n", 0);
        (void)pthread_attr_destroy(&attr);
        return GC_UNIMPLEMENTED;
      }
#   else /* HAVE_PTHREAD_GETATTR_NP */
      if (pthread_getattr_np(pthread_self(), &attr) != 0) {
        WARN("pthread_getattr_np failed\n", 0);
        return GC_UNIMPLEMENTED;
      }
#   endif
    if (pthread_attr_getstack(&attr, &(b -> mem_base), &size) != 0) {
        ABORT("pthread_attr_getstack failed");
    }
    (void)pthread_attr_destroy(&attr);
#   ifdef STACK_GROWS_DOWN
        b -> mem_base = (char *)(b -> mem_base) + size;
#   endif
#   ifdef IA64
      /* We could try backing_store_base_from_proc, but that's safe     */
      /* only if no mappings are being asynchronously created.          */
      /* Subtracting the size from the stack base doesn't work for at   */
      /* least the main thread.                                         */
      LOCK();
      {
        IF_CANCEL(int cancel_state;)
        ptr_t bsp;
        ptr_t next_stack;

        DISABLE_CANCEL(cancel_state);
        bsp = GC_save_regs_in_stack();
        next_stack = GC_greatest_stack_base_below(bsp);
        if (0 == next_stack) {
          b -> reg_base = GC_find_limit(bsp, FALSE);
        } else {
          /* Avoid walking backwards into preceding memory stack and    */
          /* growing it.                                                */
          b -> reg_base = GC_find_limit_with_bound(bsp, FALSE, next_stack);
        }
        RESTORE_CANCEL(cancel_state);
      }
      UNLOCK();
#   elif defined(E2K)
      b -> reg_base = NULL;
#   endif
    return GC_SUCCESS;
  }
# define HAVE_GET_STACK_BASE
#endif /* THREADS && (HAVE_PTHREAD_ATTR_GET_NP || HAVE_PTHREAD_GETATTR_NP) */

#if defined(GC_DARWIN_THREADS) && !defined(NO_PTHREAD_GET_STACKADDR_NP)
# include <pthread.h>

  GC_API int GC_CALL GC_get_stack_base(struct GC_stack_base *b)
  {
    /* pthread_get_stackaddr_np() should return stack bottom (highest   */
    /* stack address plus 1).                                           */
    b->mem_base = pthread_get_stackaddr_np(pthread_self());
    GC_ASSERT((word)GC_approx_sp() HOTTER_THAN (word)b->mem_base);
    return GC_SUCCESS;
  }
# define HAVE_GET_STACK_BASE
#endif /* GC_DARWIN_THREADS */

#ifdef GC_OPENBSD_THREADS
# include <sys/signal.h>
# include <pthread.h>
# include <pthread_np.h>

  /* Find the stack using pthread_stackseg_np(). */
  GC_API int GC_CALL GC_get_stack_base(struct GC_stack_base *sb)
  {
    stack_t stack;
    if (pthread_stackseg_np(pthread_self(), &stack))
      ABORT("pthread_stackseg_np(self) failed");
    sb->mem_base = stack.ss_sp;
    return GC_SUCCESS;
  }
# define HAVE_GET_STACK_BASE
#endif /* GC_OPENBSD_THREADS */

#if defined(GC_SOLARIS_THREADS) && !defined(_STRICT_STDC)

# include <thread.h>
# include <signal.h>
# include <pthread.h>

  /* These variables are used to cache ss_sp value for the primordial   */
  /* thread (it's better not to call thr_stksegment() twice for this    */
  /* thread - see JDK bug #4352906).                                    */
  static pthread_t stackbase_main_self = 0;
                        /* 0 means stackbase_main_ss_sp value is unset. */
  static void *stackbase_main_ss_sp = NULL;

  GC_API int GC_CALL GC_get_stack_base(struct GC_stack_base *b)
  {
    stack_t s;
    pthread_t self = pthread_self();

    if (self == stackbase_main_self)
      {
        /* If the client calls GC_get_stack_base() from the main thread */
        /* then just return the cached value.                           */
        b -> mem_base = stackbase_main_ss_sp;
        GC_ASSERT(b -> mem_base != NULL);
        return GC_SUCCESS;
      }

    if (thr_stksegment(&s)) {
      /* According to the manual, the only failure error code returned  */
      /* is EAGAIN meaning "the information is not available due to the */
      /* thread is not yet completely initialized or it is an internal  */
      /* thread" - this shouldn't happen here.                          */
      ABORT("thr_stksegment failed");
    }
    /* s.ss_sp holds the pointer to the stack bottom. */
    GC_ASSERT((word)GC_approx_sp() HOTTER_THAN (word)s.ss_sp);

    if (!stackbase_main_self && thr_main() != 0)
      {
        /* Cache the stack bottom pointer for the primordial thread     */
        /* (this is done during GC_init, so there is no race).          */
        stackbase_main_ss_sp = s.ss_sp;
        stackbase_main_self = self;
      }

    b -> mem_base = s.ss_sp;
    return GC_SUCCESS;
  }
# define HAVE_GET_STACK_BASE
#endif /* GC_SOLARIS_THREADS */

#ifdef GC_RTEMS_PTHREADS
  GC_API int GC_CALL GC_get_stack_base(struct GC_stack_base *sb)
  {
    sb->mem_base = rtems_get_stack_bottom();
    return GC_SUCCESS;
  }
# define HAVE_GET_STACK_BASE
#endif /* GC_RTEMS_PTHREADS */

#ifndef HAVE_GET_STACK_BASE
# ifdef NEED_FIND_LIMIT
    /* Retrieve the stack bottom.                                       */
    /* Using the GC_find_limit version is risky.                        */
    /* On IA64, for example, there is no guard page between the         */
    /* stack of one thread and the register backing store of the        */
    /* next.  Thus this is likely to identify way too large a           */
    /* "stack" and thus at least result in disastrous performance.      */
    /* TODO: Implement better strategies here. */
    GC_API int GC_CALL GC_get_stack_base(struct GC_stack_base *b)
    {
      IF_CANCEL(int cancel_state;)
      DCL_LOCK_STATE;

      LOCK();
      DISABLE_CANCEL(cancel_state);  /* May be unnecessary? */
#     ifdef STACK_GROWS_DOWN
        b -> mem_base = GC_find_limit(GC_approx_sp(), TRUE);
#     else
        b -> mem_base = GC_find_limit(GC_approx_sp(), FALSE);
#     endif
#     ifdef IA64
        b -> reg_base = GC_find_limit(GC_save_regs_in_stack(), FALSE);
#     elif defined(E2K)
        b -> reg_base = NULL;
#     endif
      RESTORE_CANCEL(cancel_state);
      UNLOCK();
      return GC_SUCCESS;
    }
# else
    GC_API int GC_CALL GC_get_stack_base(
                                struct GC_stack_base *b GC_ATTR_UNUSED)
    {
#     if defined(GET_MAIN_STACKBASE_SPECIAL) && !defined(THREADS) \
         && !defined(IA64)
        b->mem_base = GC_get_main_stack_base();
        return GC_SUCCESS;
#     else
        return GC_UNIMPLEMENTED;
#     endif
    }
# endif /* !NEED_FIND_LIMIT */
#endif /* !HAVE_GET_STACK_BASE */

#ifndef GET_MAIN_STACKBASE_SPECIAL
  /* This is always called from the main thread.  Default implementation. */
  ptr_t GC_get_main_stack_base(void)
  {
    struct GC_stack_base sb;

    if (GC_get_stack_base(&sb) != GC_SUCCESS)
      ABORT("GC_get_stack_base failed");
    GC_ASSERT((word)GC_approx_sp() HOTTER_THAN (word)sb.mem_base);
    return (ptr_t)sb.mem_base;
  }
#endif /* !GET_MAIN_STACKBASE_SPECIAL */

/* Register static data segment(s) as roots.  If more data segments are */
/* added later then they need to be registered at that point (as we do  */
/* with SunOS dynamic loading), or GC_mark_roots needs to check for     */
/* them (as we do with PCR).  Called with allocator lock held.          */
# ifdef OS2

void GC_register_data_segments(void)
{
    PTIB ptib;
    PPIB ppib;
    HMODULE module_handle;
#   define PBUFSIZ 512
    UCHAR path[PBUFSIZ];
    FILE * myexefile;
    struct exe_hdr hdrdos;      /* MSDOS header.        */
    struct e32_exe hdr386;      /* Real header for my executable */
    struct o32_obj seg;         /* Current segment */
    int nsegs;

#   if defined(CPPCHECK)
        hdrdos.padding[0] = 0; /* to prevent "field unused" warnings */
        hdr386.exe_format_level = 0;
        hdr386.os = 0;
        hdr386.padding1[0] = 0;
        hdr386.padding2[0] = 0;
        seg.pagemap = 0;
        seg.mapsize = 0;
        seg.reserved = 0;
#   endif
    if (DosGetInfoBlocks(&ptib, &ppib) != NO_ERROR) {
        ABORT("DosGetInfoBlocks failed");
    }
    module_handle = ppib -> pib_hmte;
    if (DosQueryModuleName(module_handle, PBUFSIZ, path) != NO_ERROR) {
        ABORT("DosQueryModuleName failed");
    }
    myexefile = fopen(path, "rb");
    if (myexefile == 0) {
        ABORT_ARG1("Failed to open executable", ": %s", path);
    }
    if (fread((char *)(&hdrdos), 1, sizeof(hdrdos), myexefile)
          < sizeof(hdrdos)) {
        ABORT_ARG1("Could not read MSDOS header", " from: %s", path);
    }
    if (E_MAGIC(hdrdos) != EMAGIC) {
        ABORT_ARG1("Bad DOS magic number", " in file: %s", path);
    }
    if (fseek(myexefile, E_LFANEW(hdrdos), SEEK_SET) != 0) {
        ABORT_ARG1("Bad DOS magic number", " in file: %s", path);
    }
    if (fread((char *)(&hdr386), 1, sizeof(hdr386), myexefile)
          < sizeof(hdr386)) {
        ABORT_ARG1("Could not read OS/2 header", " from: %s", path);
    }
    if (E32_MAGIC1(hdr386) != E32MAGIC1 || E32_MAGIC2(hdr386) != E32MAGIC2) {
        ABORT_ARG1("Bad OS/2 magic number", " in file: %s", path);
    }
    if (E32_BORDER(hdr386) != E32LEBO || E32_WORDER(hdr386) != E32LEWO) {
        ABORT_ARG1("Bad byte order in executable", " file: %s", path);
    }
    if (E32_CPU(hdr386) == E32CPU286) {
        ABORT_ARG1("GC cannot handle 80286 executables", ": %s", path);
    }
    if (fseek(myexefile, E_LFANEW(hdrdos) + E32_OBJTAB(hdr386),
              SEEK_SET) != 0) {
        ABORT_ARG1("Seek to object table failed", " in file: %s", path);
    }
    for (nsegs = E32_OBJCNT(hdr386); nsegs > 0; nsegs--) {
      int flags;
      if (fread((char *)(&seg), 1, sizeof(seg), myexefile) < sizeof(seg)) {
        ABORT_ARG1("Could not read obj table entry", " from file: %s", path);
      }
      flags = O32_FLAGS(seg);
      if (!(flags & OBJWRITE)) continue;
      if (!(flags & OBJREAD)) continue;
      if (flags & OBJINVALID) {
          GC_err_printf("Object with invalid pages?\n");
          continue;
      }
      GC_add_roots_inner((ptr_t)O32_BASE(seg),
                         (ptr_t)(O32_BASE(seg)+O32_SIZE(seg)), FALSE);
    }
    (void)fclose(myexefile);
}

# else /* !OS2 */

# if defined(GWW_VDB)
#   ifndef MEM_WRITE_WATCH
#     define MEM_WRITE_WATCH 0x200000
#   endif
#   ifndef WRITE_WATCH_FLAG_RESET
#     define WRITE_WATCH_FLAG_RESET 1
#   endif

    /* Since we can't easily check whether ULONG_PTR and SIZE_T are     */
    /* defined in Win32 basetsd.h, we define own ULONG_PTR.             */
#   define GC_ULONG_PTR word

    typedef UINT (WINAPI * GetWriteWatch_type)(
                                DWORD, PVOID, GC_ULONG_PTR /* SIZE_T */,
                                PVOID *, GC_ULONG_PTR *, PULONG);
    static FARPROC GetWriteWatch_func;
    static DWORD GetWriteWatch_alloc_flag;

#   define GC_GWW_AVAILABLE() (GetWriteWatch_func != 0)

    static void detect_GetWriteWatch(void)
    {
      static GC_bool done;
      HMODULE hK32;
      if (done)
        return;

#     if defined(MPROTECT_VDB)
        {
          char * str = GETENV("GC_USE_GETWRITEWATCH");
#         if defined(GC_PREFER_MPROTECT_VDB)
            if (str == NULL || (*str == '0' && *(str + 1) == '\0')) {
              /* GC_USE_GETWRITEWATCH is unset or set to "0".           */
              done = TRUE; /* falling back to MPROTECT_VDB strategy.    */
              /* This should work as if GWW_VDB is undefined. */
              return;
            }
#         else
            if (str != NULL && *str == '0' && *(str + 1) == '\0') {
              /* GC_USE_GETWRITEWATCH is set "0".                       */
              done = TRUE; /* falling back to MPROTECT_VDB strategy.    */
              return;
            }
#         endif
        }
#     endif

#     ifdef MSWINRT_FLAVOR
        {
          MEMORY_BASIC_INFORMATION memInfo;
          SIZE_T result = VirtualQuery((void*)(word)GetProcAddress,
                                       &memInfo, sizeof(memInfo));
          if (result != sizeof(memInfo))
            ABORT("Weird VirtualQuery result");
          hK32 = (HMODULE)memInfo.AllocationBase;
        }
#     else
        hK32 = GetModuleHandle(TEXT("kernel32.dll"));
#     endif
      if (hK32 != (HMODULE)0 &&
          (GetWriteWatch_func = GetProcAddress(hK32, "GetWriteWatch")) != 0) {
        /* Also check whether VirtualAlloc accepts MEM_WRITE_WATCH,   */
        /* as some versions of kernel32.dll have one but not the      */
        /* other, making the feature completely broken.               */
        void * page;

        GC_ASSERT(GC_page_size != 0);
        page = VirtualAlloc(NULL, GC_page_size, MEM_WRITE_WATCH | MEM_RESERVE,
                            PAGE_READWRITE);
        if (page != NULL) {
          PVOID pages[16];
          GC_ULONG_PTR count = 16;
          DWORD page_size;
          /* Check that it actually works.  In spite of some            */
          /* documentation it actually seems to exist on Win2K.         */
          /* This test may be unnecessary, but ...                      */
          if ((*(GetWriteWatch_type)(word)GetWriteWatch_func)(
                                        WRITE_WATCH_FLAG_RESET, page,
                                        GC_page_size, pages, &count,
                                        &page_size) != 0) {
            /* GetWriteWatch always fails. */
            GetWriteWatch_func = 0;
          } else {
            GetWriteWatch_alloc_flag = MEM_WRITE_WATCH;
          }
          VirtualFree(page, 0 /* dwSize */, MEM_RELEASE);
        } else {
          /* GetWriteWatch will be useless. */
          GetWriteWatch_func = 0;
        }
      }
      done = TRUE;
    }

# else
#   define GetWriteWatch_alloc_flag 0
# endif /* !GWW_VDB */

# if defined(MSWIN32) || defined(MSWINCE) || defined(CYGWIN32)

# ifdef MSWIN32
  /* Unfortunately, we have to handle win32s very differently from NT,  */
  /* Since VirtualQuery has very different semantics.  In particular,   */
  /* under win32s a VirtualQuery call on an unmapped page returns an    */
  /* invalid result.  Under NT, GC_register_data_segments is a no-op    */
  /* and all real work is done by GC_register_dynamic_libraries.  Under */
  /* win32s, we cannot find the data segments associated with dll's.    */
  /* We register the main data segment here.                            */
  GC_INNER GC_bool GC_no_win32_dlls = FALSE;
        /* This used to be set for gcc, to avoid dealing with           */
        /* the structured exception handling issues.  But we now have   */
        /* assembly code to do that right.                              */

  GC_INNER GC_bool GC_wnt = FALSE;
         /* This is a Windows NT derivative, i.e. NT, Win2K, XP or later. */

  GC_INNER void GC_init_win32(void)
  {
#   if defined(_WIN64) || (defined(_MSC_VER) && _MSC_VER >= 1800)
      /* MS Visual Studio 2013 deprecates GetVersion, but on the other  */
      /* hand it cannot be used to target pre-Win2K.                    */
      GC_wnt = TRUE;
#   else
      /* Set GC_wnt.  If we're running under win32s, assume that no     */
      /* DLLs will be loaded.  I doubt anyone still runs win32s, but... */
      DWORD v = GetVersion();

      GC_wnt = !(v & 0x80000000);
      GC_no_win32_dlls |= ((!GC_wnt) && (v & 0xff) <= 3);
#   endif
#   ifdef USE_MUNMAP
      if (GC_no_win32_dlls) {
        /* Turn off unmapping for safety (since may not work well with  */
        /* GlobalAlloc).                                                */
        GC_unmap_threshold = 0;
      }
#   endif
  }

  /* Return the smallest address a such that VirtualQuery               */
  /* returns correct results for all addresses between a and start.     */
  /* Assumes VirtualQuery returns correct information for start.        */
  STATIC ptr_t GC_least_described_address(ptr_t start)
  {
    MEMORY_BASIC_INFORMATION buf;
    LPVOID limit = GC_sysinfo.lpMinimumApplicationAddress;
    ptr_t p = (ptr_t)((word)start & ~(word)(GC_page_size-1));

    GC_ASSERT(GC_page_size != 0);
    for (;;) {
        size_t result;
        LPVOID q = (LPVOID)(p - GC_page_size);

        if ((word)q > (word)p /* underflow */ || (word)q < (word)limit) break;
        result = VirtualQuery(q, &buf, sizeof(buf));
        if (result != sizeof(buf) || buf.AllocationBase == 0) break;
        p = (ptr_t)(buf.AllocationBase);
    }
    return p;
  }
# endif /* MSWIN32 */

# if defined(USE_WINALLOC) && !defined(REDIRECT_MALLOC)
  /* We maintain a linked list of AllocationBase values that we know    */
  /* correspond to malloc heap sections.  Currently this is only called */
  /* during a GC.  But there is some hope that for long running         */
  /* programs we will eventually see most heap sections.                */

  /* In the long run, it would be more reliable to occasionally walk    */
  /* the malloc heap with HeapWalk on the default heap.  But that       */
  /* apparently works only for NT-based Windows.                        */

  STATIC size_t GC_max_root_size = 100000; /* Appr. largest root size.  */

  /* In the long run, a better data structure would also be nice ...    */
  STATIC struct GC_malloc_heap_list {
    void * allocation_base;
    struct GC_malloc_heap_list *next;
  } *GC_malloc_heap_l = 0;

  /* Is p the base of one of the malloc heap sections we already know   */
  /* about?                                                             */
  STATIC GC_bool GC_is_malloc_heap_base(void *p)
  {
    struct GC_malloc_heap_list *q = GC_malloc_heap_l;

    while (0 != q) {
      if (q -> allocation_base == p) return TRUE;
      q = q -> next;
    }
    return FALSE;
  }

  STATIC void *GC_get_allocation_base(void *p)
  {
    MEMORY_BASIC_INFORMATION buf;
    size_t result = VirtualQuery(p, &buf, sizeof(buf));
    if (result != sizeof(buf)) {
      ABORT("Weird VirtualQuery result");
    }
    return buf.AllocationBase;
  }

  GC_INNER void GC_add_current_malloc_heap(void)
  {
    struct GC_malloc_heap_list *new_l = (struct GC_malloc_heap_list *)
                 malloc(sizeof(struct GC_malloc_heap_list));
    void *candidate;

    if (NULL == new_l) return;
    new_l -> allocation_base = NULL;
                        /* to suppress maybe-uninitialized gcc warning  */

    candidate = GC_get_allocation_base(new_l);
    if (GC_is_malloc_heap_base(candidate)) {
      /* Try a little harder to find malloc heap.                       */
        size_t req_size = 10000;
        do {
          void *p = malloc(req_size);
          if (0 == p) {
            free(new_l);
            return;
          }
          candidate = GC_get_allocation_base(p);
          free(p);
          req_size *= 2;
        } while (GC_is_malloc_heap_base(candidate)
                 && req_size < GC_max_root_size/10 && req_size < 500000);
        if (GC_is_malloc_heap_base(candidate)) {
          free(new_l);
          return;
        }
    }
    GC_COND_LOG_PRINTF("Found new system malloc AllocationBase at %p\n",
                       candidate);
    new_l -> allocation_base = candidate;
    new_l -> next = GC_malloc_heap_l;
    GC_malloc_heap_l = new_l;
  }

  /* Free all the linked list nodes. Could be invoked at process exit   */
  /* to avoid memory leak complains of a dynamic code analysis tool.    */
  STATIC void GC_free_malloc_heap_list(void)
  {
    struct GC_malloc_heap_list *q = GC_malloc_heap_l;

    GC_malloc_heap_l = NULL;
    while (q != NULL) {
      struct GC_malloc_heap_list *next = q -> next;
      free(q);
      q = next;
    }
  }
# endif /* USE_WINALLOC && !REDIRECT_MALLOC */

  /* Is p the start of either the malloc heap, or of one of our */
  /* heap sections?                                             */
  GC_INNER GC_bool GC_is_heap_base(void *p)
  {
     int i;

#    if defined(USE_WINALLOC) && !defined(REDIRECT_MALLOC)
       if (GC_root_size > GC_max_root_size)
         GC_max_root_size = GC_root_size;
       if (GC_is_malloc_heap_base(p))
         return TRUE;
#    endif
     for (i = 0; i < (int)GC_n_heap_bases; i++) {
         if (GC_heap_bases[i] == p) return TRUE;
     }
     return FALSE;
  }

#ifdef MSWIN32
  STATIC void GC_register_root_section(ptr_t static_root)
  {
      MEMORY_BASIC_INFORMATION buf;
      LPVOID p;
      char * base;
      char * limit;

      if (!GC_no_win32_dlls) return;
      p = base = limit = GC_least_described_address(static_root);
      while ((word)p < (word)GC_sysinfo.lpMaximumApplicationAddress) {
        size_t result = VirtualQuery(p, &buf, sizeof(buf));
        DWORD protect;

        if (result != sizeof(buf) || buf.AllocationBase == 0
            || GC_is_heap_base(buf.AllocationBase)) break;
        if ((word)p > GC_WORD_MAX - buf.RegionSize) break; /* overflow */
        protect = buf.Protect;
        if (buf.State == MEM_COMMIT
            && is_writable(protect)) {
            if ((char *)p != limit) {
                if (base != limit) GC_add_roots_inner(base, limit, FALSE);
                base = (char *)p;
            }
            limit = (char *)p + buf.RegionSize;
        }
        p = (char *)p + buf.RegionSize;
      }
      if (base != limit) GC_add_roots_inner(base, limit, FALSE);
  }
#endif /* MSWIN32 */

  void GC_register_data_segments(void)
  {
#   ifdef MSWIN32
      GC_register_root_section((ptr_t)&GC_pages_executable);
                            /* any other GC global variable would fit too. */
#   endif
  }

# else /* !OS2 && !Windows */

# if (defined(SVR4) || defined(DGUX) \
      || (defined(LINUX) && defined(SPARC))) && !defined(PCR)
  ptr_t GC_SysVGetDataStart(size_t max_page_size, ptr_t etext_addr)
  {
    word text_end = ((word)(etext_addr) + ALIGNMENT - 1)
                    & ~(word)(ALIGNMENT - 1);
        /* etext rounded to pointer alignment boundary */
    word next_page = ((text_end + (word)max_page_size - 1)
                      & ~((word)max_page_size - 1));
    word page_offset = (text_end & ((word)max_page_size - 1));
    volatile ptr_t result = (char *)(next_page + page_offset);
    /* Note that this isn't equivalent to just adding           */
    /* max_page_size to &etext if &etext is at a page boundary  */

    GC_setup_temporary_fault_handler();
    if (SETJMP(GC_jmp_buf) == 0) {
        /* Try writing to the address.  */
#       ifdef AO_HAVE_fetch_and_add
          volatile AO_t zero = 0;
          (void)AO_fetch_and_add((volatile AO_t *)result, zero);
#       else
          /* Fallback to non-atomic fetch-and-store.    */
          char v = *result;
#         if defined(CPPCHECK)
            GC_noop1((word)&v);
#         endif
          *result = v;
#       endif
        GC_reset_fault_handler();
    } else {
        GC_reset_fault_handler();
        /* We got here via a longjmp.  The address is not readable.     */
        /* This is known to happen under Solaris 2.4 + gcc, which       */
        /* places string constants in the text segment, but after       */
        /* etext.  Use plan B.  Note that we now know there is a gap    */
        /* between text and data segments, so plan A brought us         */
        /* something.                                                   */
        result = (char *)GC_find_limit(DATAEND, FALSE);
    }
    return (/* no volatile */ ptr_t)result;
  }
# endif

#ifdef DATASTART_USES_BSDGETDATASTART
/* It's unclear whether this should be identical to the above, or       */
/* whether it should apply to non-x86 architectures.                    */
/* For now we don't assume that there is always an empty page after     */
/* etext.  But in some cases there actually seems to be slightly more.  */
/* This also deals with holes between read-only data and writable data. */
  GC_INNER ptr_t GC_FreeBSDGetDataStart(size_t max_page_size,
                                        ptr_t etext_addr)
  {
    word text_end = ((word)(etext_addr) + ALIGNMENT - 1)
                     & ~(word)(ALIGNMENT - 1);
        /* etext rounded to pointer alignment boundary */
    volatile word next_page = (text_end + (word)max_page_size - 1)
                              & ~((word)max_page_size - 1);
    volatile ptr_t result = (ptr_t)text_end;
    GC_setup_temporary_fault_handler();
    if (SETJMP(GC_jmp_buf) == 0) {
        /* Try reading at the address.                          */
        /* This should happen before there is another thread.   */
        for (; next_page < (word)DATAEND; next_page += (word)max_page_size)
            GC_noop1((word)(*(volatile unsigned char *)next_page));
        GC_reset_fault_handler();
    } else {
        GC_reset_fault_handler();
        /* As above, we go to plan B    */
        result = (ptr_t)GC_find_limit(DATAEND, FALSE);
    }
    return(result);
  }
#endif /* DATASTART_USES_BSDGETDATASTART */

#ifdef AMIGA

#  define GC_AMIGA_DS
#  include "extra/AmigaOS.c"
#  undef GC_AMIGA_DS

#elif defined(OPENBSD)

/* Depending on arch alignment, there can be multiple holes     */
/* between DATASTART and DATAEND.  Scan in DATASTART .. DATAEND */
/* and register each region.                                    */
void GC_register_data_segments(void)
{
  ptr_t region_start = DATASTART;

  if ((word)region_start - 1U >= (word)DATAEND)
    ABORT_ARG2("Wrong DATASTART/END pair",
               ": %p .. %p", (void *)region_start, (void *)DATAEND);
  for (;;) {
#   ifdef GC_OPENBSD_UTHREADS
      ptr_t region_end = GC_find_limit_openbsd(region_start, DATAEND);
#   else
      ptr_t region_end = GC_find_limit_with_bound(region_start, TRUE, DATAEND);
#   endif

    GC_add_roots_inner(region_start, region_end, FALSE);
    if ((word)region_end >= (word)DATAEND)
      break;
    region_start = GC_skip_hole_openbsd(region_end, DATAEND);
  }
}

# else /* !OS2 && !Windows && !AMIGA && !OPENBSD */

  void GC_register_data_segments(void)
  {
#   if !defined(DYNAMIC_LOADING) && defined(GC_DONT_REGISTER_MAIN_STATIC_DATA)
      /* Avoid even referencing DATASTART and DATAEND as they are       */
      /* unnecessary and cause linker errors when bitcode is enabled.   */
      /* GC_register_data_segments() is not called anyway.              */
#   elif defined(DYNAMIC_LOADING) && (defined(DARWIN) || defined(HAIKU))
      /* No-op.  GC_register_main_static_data() always returns false.   */
#   elif !defined(PCR) && !defined(MACOS)
#     if defined(REDIRECT_MALLOC) && defined(GC_SOLARIS_THREADS)
        /* As of Solaris 2.3, the Solaris threads implementation        */
        /* allocates the data structure for the initial thread with     */
        /* sbrk at process startup.  It needs to be scanned, so that    */
        /* we don't lose some malloc allocated data structures          */
        /* hanging from it.  We're on thin ice here ...                 */
        GC_ASSERT(DATASTART);
        {
          ptr_t p = (ptr_t)sbrk(0);
          if ((word)DATASTART < (word)p)
            GC_add_roots_inner(DATASTART, p, FALSE);
        }
#     else
        if ((word)DATASTART - 1U >= (word)DATAEND) {
                                /* Subtract one to check also for NULL  */
                                /* without a compiler warning.          */
          ABORT_ARG2("Wrong DATASTART/END pair",
                     ": %p .. %p", (void *)DATASTART, (void *)DATAEND);
        }
        GC_add_roots_inner(DATASTART, DATAEND, FALSE);
#       ifdef GC_HAVE_DATAREGION2
          if ((word)DATASTART2 - 1U >= (word)DATAEND2)
            ABORT_ARG2("Wrong DATASTART/END2 pair",
                       ": %p .. %p", (void *)DATASTART2, (void *)DATAEND2);
          GC_add_roots_inner(DATASTART2, DATAEND2, FALSE);
#       endif
#     endif
#   endif
#   if defined(MACOS)
    {
#   if defined(THINK_C)
        extern void* GC_MacGetDataStart(void);
        /* globals begin above stack and end at a5. */
        GC_add_roots_inner((ptr_t)GC_MacGetDataStart(),
                           (ptr_t)LMGetCurrentA5(), FALSE);
#   else
#     if defined(__MWERKS__)
#       if !__POWERPC__
          extern void* GC_MacGetDataStart(void);
          /* MATTHEW: Function to handle Far Globals (CW Pro 3) */
#         if __option(far_data)
          extern void* GC_MacGetDataEnd(void);
#         endif
          /* globals begin above stack and end at a5. */
          GC_add_roots_inner((ptr_t)GC_MacGetDataStart(),
                             (ptr_t)LMGetCurrentA5(), FALSE);
          /* MATTHEW: Handle Far Globals */
#         if __option(far_data)
      /* Far globals follow the QD globals: */
          GC_add_roots_inner((ptr_t)LMGetCurrentA5(),
                             (ptr_t)GC_MacGetDataEnd(), FALSE);
#         endif
#       else
          extern char __data_start__[], __data_end__[];
          GC_add_roots_inner((ptr_t)&__data_start__,
                             (ptr_t)&__data_end__, FALSE);
#       endif /* __POWERPC__ */
#     endif /* __MWERKS__ */
#   endif /* !THINK_C */
    }
#   endif /* MACOS */

    /* Dynamic libraries are added at every collection, since they may  */
    /* change.                                                          */
  }

# endif /* !AMIGA */
# endif /* !MSWIN32 && !MSWINCE */
# endif /* !OS2 */

/*
 * Auxiliary routines for obtaining memory from OS.
 */

# if !defined(OS2) && !defined(PCR) && !defined(AMIGA) \
     && !defined(USE_WINALLOC) && !defined(MACOS) && !defined(DOS4GW) \
     && !defined(NINTENDO_SWITCH) && !defined(NONSTOP) \
     && !defined(SN_TARGET_ORBIS) && !defined(SN_TARGET_PS3) \
     && !defined(SN_TARGET_PSP2) && !defined(RTEMS) && !defined(__CC_ARM)

# define SBRK_ARG_T ptrdiff_t

#if defined(MMAP_SUPPORTED)

#ifdef USE_MMAP_FIXED
#   define GC_MMAP_FLAGS MAP_FIXED | MAP_PRIVATE
        /* Seems to yield better performance on Solaris 2, but can      */
        /* be unreliable if something is already mapped at the address. */
#else
#   define GC_MMAP_FLAGS MAP_PRIVATE
#endif

#ifdef USE_MMAP_ANON
# define zero_fd -1
# if defined(MAP_ANONYMOUS) && !defined(CPPCHECK)
#   define OPT_MAP_ANON MAP_ANONYMOUS
# else
#   define OPT_MAP_ANON MAP_ANON
# endif
#else
  static int zero_fd = -1;
# define OPT_MAP_ANON 0
#endif

# ifndef MSWIN_XBOX1
#   if defined(SYMBIAN) && !defined(USE_MMAP_ANON)
      EXTERN_C_BEGIN
      extern char *GC_get_private_path_and_zero_file(void);
      EXTERN_C_END
#   endif

  STATIC ptr_t GC_unix_mmap_get_mem(size_t bytes)
  {
    void *result;
    static ptr_t last_addr = HEAP_START;

#   ifndef USE_MMAP_ANON
      static GC_bool initialized = FALSE;

      if (!EXPECT(initialized, TRUE)) {
#       ifdef SYMBIAN
          char *path = GC_get_private_path_and_zero_file();
          if (path != NULL) {
            zero_fd = open(path, O_RDWR | O_CREAT, 0644);
            free(path);
          }
#       else
          zero_fd = open("/dev/zero", O_RDONLY);
#       endif
          if (zero_fd == -1)
            ABORT("Could not open /dev/zero");
          if (fcntl(zero_fd, F_SETFD, FD_CLOEXEC) == -1)
            WARN("Could not set FD_CLOEXEC for /dev/zero\n", 0);

          initialized = TRUE;
      }
#   endif

    GC_ASSERT(GC_page_size != 0);
    if (bytes & (GC_page_size - 1)) ABORT("Bad GET_MEM arg");
    result = mmap(last_addr, bytes, (PROT_READ | PROT_WRITE)
                                    | (GC_pages_executable ? PROT_EXEC : 0),
                  GC_MMAP_FLAGS | OPT_MAP_ANON, zero_fd, 0/* offset */);
#   undef IGNORE_PAGES_EXECUTABLE

    if (EXPECT(MAP_FAILED == result, FALSE)) {
      if (HEAP_START == last_addr && GC_pages_executable
          && (EACCES == errno || EPERM == errno))
        ABORT("Cannot allocate executable pages");
      return NULL;
    }
    last_addr = (ptr_t)(((word)result + bytes + GC_page_size - 1)
                        & ~(word)(GC_page_size - 1));
#   if !defined(LINUX)
      if (last_addr == 0) {
        /* Oops.  We got the end of the address space.  This isn't      */
        /* usable by arbitrary C code, since one-past-end pointers      */
        /* don't work, so we discard it and try again.                  */
        munmap(result, ~GC_page_size - (size_t)result + 1);
                        /* Leave last page mapped, so we can't repeat.  */
        return GC_unix_mmap_get_mem(bytes);
      }
#   else
      GC_ASSERT(last_addr != 0);
#   endif
    if (((word)result % HBLKSIZE) != 0)
      ABORT(
       "GC_unix_get_mem: Memory returned by mmap is not aligned to HBLKSIZE.");
    return((ptr_t)result);
  }
# endif  /* !MSWIN_XBOX1 */

#endif  /* MMAP_SUPPORTED */

#if defined(USE_MMAP)
  ptr_t GC_unix_get_mem(size_t bytes)
  {
    return GC_unix_mmap_get_mem(bytes);
  }
#else /* !USE_MMAP */

STATIC ptr_t GC_unix_sbrk_get_mem(size_t bytes)
{
  ptr_t result;
# ifdef IRIX5
    /* Bare sbrk isn't thread safe.  Play by malloc rules.      */
    /* The equivalent may be needed on other systems as well.   */
    __LOCK_MALLOC();
# endif
  {
    ptr_t cur_brk = (ptr_t)sbrk(0);
    SBRK_ARG_T lsbs = (word)cur_brk & (GC_page_size-1);

    GC_ASSERT(GC_page_size != 0);
    if ((SBRK_ARG_T)bytes < 0) {
        result = 0; /* too big */
        goto out;
    }
    if (lsbs != 0) {
        if ((ptr_t)sbrk((SBRK_ARG_T)GC_page_size - lsbs) == (ptr_t)(-1)) {
            result = 0;
            goto out;
        }
    }
#   ifdef ADD_HEAP_GUARD_PAGES
      /* This is useful for catching severe memory overwrite problems that */
      /* span heap sections.  It shouldn't otherwise be turned on.         */
      {
        ptr_t guard = (ptr_t)sbrk((SBRK_ARG_T)GC_page_size);
        if (mprotect(guard, GC_page_size, PROT_NONE) != 0)
            ABORT("ADD_HEAP_GUARD_PAGES: mprotect failed");
      }
#   endif /* ADD_HEAP_GUARD_PAGES */
    result = (ptr_t)sbrk((SBRK_ARG_T)bytes);
    if (result == (ptr_t)(-1)) result = 0;
  }
 out:
# ifdef IRIX5
    __UNLOCK_MALLOC();
# endif
  return(result);
}

ptr_t GC_unix_get_mem(size_t bytes)
{
# if defined(MMAP_SUPPORTED)
    /* By default, we try both sbrk and mmap, in that order.    */
    static GC_bool sbrk_failed = FALSE;
    ptr_t result = 0;

    if (GC_pages_executable) {
        /* If the allocated memory should have the execute permission   */
        /* then sbrk() cannot be used.                                  */
        return GC_unix_mmap_get_mem(bytes);
    }
    if (!sbrk_failed) result = GC_unix_sbrk_get_mem(bytes);
    if (0 == result) {
        sbrk_failed = TRUE;
        result = GC_unix_mmap_get_mem(bytes);
    }
    if (0 == result) {
        /* Try sbrk again, in case sbrk memory became available.        */
        result = GC_unix_sbrk_get_mem(bytes);
    }
    return result;
# else /* !MMAP_SUPPORTED */
    return GC_unix_sbrk_get_mem(bytes);
# endif
}

#endif /* !USE_MMAP */

# endif /* UN*X */

# ifdef OS2

void * os2_alloc(size_t bytes)
{
    void * result;

    if (DosAllocMem(&result, bytes, (PAG_READ | PAG_WRITE | PAG_COMMIT)
                                    | (GC_pages_executable ? PAG_EXECUTE : 0))
                    != NO_ERROR) {
        return(0);
    }
    /* FIXME: What's the purpose of this recursion?  (Probably, if      */
    /* DosAllocMem returns memory at 0 address then just retry once.)   */
    if (result == 0) return(os2_alloc(bytes));
    return(result);
}

# endif /* OS2 */

#ifdef MSWIN_XBOX1
    ptr_t GC_durango_get_mem(size_t bytes)
    {
      if (0 == bytes) return NULL;
      return (ptr_t)VirtualAlloc(NULL, bytes, MEM_COMMIT | MEM_TOP_DOWN,
                                 PAGE_READWRITE);
    }
#elif defined(MSWINCE)
  ptr_t GC_wince_get_mem(size_t bytes)
  {
    ptr_t result = 0; /* initialized to prevent warning. */
    word i;

    GC_ASSERT(GC_page_size != 0);
    bytes = ROUNDUP_PAGESIZE(bytes);

    /* Try to find reserved, uncommitted pages */
    for (i = 0; i < GC_n_heap_bases; i++) {
        if (((word)(-(signed_word)GC_heap_lengths[i])
             & (GC_sysinfo.dwAllocationGranularity-1))
            >= bytes) {
            result = GC_heap_bases[i] + GC_heap_lengths[i];
            break;
        }
    }

    if (i == GC_n_heap_bases) {
        /* Reserve more pages */
        size_t res_bytes =
            SIZET_SAT_ADD(bytes, (size_t)GC_sysinfo.dwAllocationGranularity-1)
            & ~((size_t)GC_sysinfo.dwAllocationGranularity-1);
        /* If we ever support MPROTECT_VDB here, we will probably need to    */
        /* ensure that res_bytes is strictly > bytes, so that VirtualProtect */
        /* never spans regions.  It seems to be OK for a VirtualFree         */
        /* argument to span regions, so we should be OK for now.             */
        result = (ptr_t) VirtualAlloc(NULL, res_bytes,
                                MEM_RESERVE | MEM_TOP_DOWN,
                                GC_pages_executable ? PAGE_EXECUTE_READWRITE :
                                                      PAGE_READWRITE);
        if (HBLKDISPL(result) != 0) ABORT("Bad VirtualAlloc result");
            /* If I read the documentation correctly, this can          */
            /* only happen if HBLKSIZE > 64 KB or not a power of 2.     */
        if (GC_n_heap_bases >= MAX_HEAP_SECTS) ABORT("Too many heap sections");
        if (result == NULL) return NULL;
        GC_heap_bases[GC_n_heap_bases] = result;
        GC_heap_lengths[GC_n_heap_bases] = 0;
        GC_n_heap_bases++;
    }

    /* Commit pages */
    result = (ptr_t) VirtualAlloc(result, bytes, MEM_COMMIT,
                              GC_pages_executable ? PAGE_EXECUTE_READWRITE :
                                                    PAGE_READWRITE);
#   undef IGNORE_PAGES_EXECUTABLE

    if (result != NULL) {
        if (HBLKDISPL(result) != 0) ABORT("Bad VirtualAlloc result");
        GC_heap_lengths[i] += bytes;
    }

    return(result);
  }

#elif defined(USE_WINALLOC) /* && !MSWIN_XBOX1 */ || defined(CYGWIN32)

# ifdef USE_GLOBAL_ALLOC
#   define GLOBAL_ALLOC_TEST 1
# else
#   define GLOBAL_ALLOC_TEST GC_no_win32_dlls
# endif

# if (defined(GC_USE_MEM_TOP_DOWN) && defined(USE_WINALLOC)) \
     || defined(CPPCHECK)
    DWORD GC_mem_top_down = MEM_TOP_DOWN;
                           /* Use GC_USE_MEM_TOP_DOWN for better 64-bit */
                           /* testing.  Otherwise all addresses tend to */
                           /* end up in first 4 GB, hiding bugs.        */
# else
#   define GC_mem_top_down 0
# endif /* !GC_USE_MEM_TOP_DOWN */

  ptr_t GC_win32_get_mem(size_t bytes)
  {
    ptr_t result;

# ifndef USE_WINALLOC
    result = GC_unix_get_mem(bytes);
# else
#   if defined(MSWIN32) && !defined(MSWINRT_FLAVOR)
      if (GLOBAL_ALLOC_TEST) {
        /* VirtualAlloc doesn't like PAGE_EXECUTE_READWRITE.    */
        /* There are also unconfirmed rumors of other           */
        /* problems, so we dodge the issue.                     */
        result = (ptr_t)GlobalAlloc(0, SIZET_SAT_ADD(bytes, HBLKSIZE));
        /* Align it at HBLKSIZE boundary.       */
        result = (ptr_t)(((word)result + HBLKSIZE - 1)
                         & ~(word)(HBLKSIZE - 1));
      } else
#   endif
    /* else */ {
        /* VirtualProtect only works on regions returned by a   */
        /* single VirtualAlloc call.  Thus we allocate one      */
        /* extra page, which will prevent merging of blocks     */
        /* in separate regions, and eliminate any temptation    */
        /* to call VirtualProtect on a range spanning regions.  */
        /* This wastes a small amount of memory, and risks      */
        /* increased fragmentation.  But better alternatives    */
        /* would require effort.                                */
#       ifdef MPROTECT_VDB
          /* We can't check for GC_incremental here (because    */
          /* GC_enable_incremental() might be called some time  */
          /* later after the GC initialization).                */
#         ifdef GWW_VDB
#           define VIRTUAL_ALLOC_PAD (GC_GWW_AVAILABLE() ? 0 : 1)
#         else
#           define VIRTUAL_ALLOC_PAD 1
#         endif
#       else
#         define VIRTUAL_ALLOC_PAD 0
#       endif
        /* Pass the MEM_WRITE_WATCH only if GetWriteWatch-based */
        /* VDBs are enabled and the GetWriteWatch function is   */
        /* available.  Otherwise we waste resources or possibly */
        /* cause VirtualAlloc to fail (observed in Windows 2000 */
        /* SP2).                                                */
        result = (ptr_t) VirtualAlloc(NULL,
                            SIZET_SAT_ADD(bytes, VIRTUAL_ALLOC_PAD),
                            GetWriteWatch_alloc_flag
                                | (MEM_COMMIT | MEM_RESERVE)
                                | GC_mem_top_down,
                            GC_pages_executable ? PAGE_EXECUTE_READWRITE :
                                                  PAGE_READWRITE);
#       undef IGNORE_PAGES_EXECUTABLE
    }
# endif /* USE_WINALLOC */
    if (HBLKDISPL(result) != 0) ABORT("Bad VirtualAlloc result");
        /* If I read the documentation correctly, this can      */
        /* only happen if HBLKSIZE > 64 KB or not a power of 2. */
    if (GC_n_heap_bases >= MAX_HEAP_SECTS) ABORT("Too many heap sections");
    if (0 != result) GC_heap_bases[GC_n_heap_bases++] = result;
    return(result);
  }
#endif /* USE_WINALLOC || CYGWIN32 */

#if defined(MSWIN32) || defined(MSWINCE) || defined(CYGWIN32) \
    || defined(MSWIN_XBOX1)
  GC_API void GC_CALL GC_win32_free_heap(void)
  {
#   if defined(USE_WINALLOC) && !defined(REDIRECT_MALLOC) \
       && !defined(MSWIN_XBOX1)
      GC_free_malloc_heap_list();
#   endif
#   if (defined(USE_WINALLOC) && !defined(MSWIN_XBOX1) \
        && !defined(MSWINCE)) || defined(CYGWIN32)
#     ifndef MSWINRT_FLAVOR
#       ifndef CYGWIN32
          if (GLOBAL_ALLOC_TEST)
#       endif
        {
          while (GC_n_heap_bases-- > 0) {
#           ifdef CYGWIN32
              /* FIXME: Is it OK to use non-GC free() here? */
#           else
              GlobalFree(GC_heap_bases[GC_n_heap_bases]);
#           endif
            GC_heap_bases[GC_n_heap_bases] = 0;
          }
          return;
        }
#     endif /* !MSWINRT_FLAVOR */
#     ifndef CYGWIN32
        /* Avoiding VirtualAlloc leak.  */
        while (GC_n_heap_bases > 0) {
          VirtualFree(GC_heap_bases[--GC_n_heap_bases], 0, MEM_RELEASE);
          GC_heap_bases[GC_n_heap_bases] = 0;
        }
#     endif
#   endif /* USE_WINALLOC || CYGWIN32 */
  }
#endif /* Windows */

#ifdef AMIGA
# define GC_AMIGA_AM
# include "extra/AmigaOS.c"
# undef GC_AMIGA_AM
#endif

#if (defined(USE_MUNMAP) || defined(MPROTECT_VDB)) && !defined(USE_WINALLOC)
# define ABORT_ON_REMAP_FAIL(C_msg_prefix, start_addr, len) \
        ABORT_ARG3(C_msg_prefix " failed", \
                   " at %p (length %lu), errno= %d", \
                   (void *)(start_addr), (unsigned long)(len), errno)
#endif

#ifdef USE_MUNMAP

#if !defined(NN_PLATFORM_CTR) && !defined(MSWIN32) && !defined(MSWINCE) \
    && !defined(MSWIN_XBOX1)
# include <unistd.h>
# ifdef SN_TARGET_PS3
#   include <sys/memory.h>
# else
#   include <sys/mman.h>
# endif
# include <sys/stat.h>
# include <sys/types.h>
#endif

/* Compute a page aligned starting address for the unmap        */
/* operation on a block of size bytes starting at start.        */
/* Return 0 if the block is too small to make this feasible.    */
STATIC ptr_t GC_unmap_start(ptr_t start, size_t bytes)
{
    ptr_t result = (ptr_t)(((word)start + GC_page_size - 1)
                            & ~(word)(GC_page_size - 1));

    GC_ASSERT(GC_page_size != 0);
    if ((word)(result + GC_page_size) > (word)(start + bytes)) return 0;
    return result;
}

/* Under Win32/WinCE we commit (map) and decommit (unmap)       */
/* memory using VirtualAlloc and VirtualFree.  These functions  */
/* work on individual allocations of virtual memory, made       */
/* previously using VirtualAlloc with the MEM_RESERVE flag.     */
/* The ranges we need to (de)commit may span several of these   */
/* allocations; therefore we use VirtualQuery to check          */
/* allocation lengths, and split up the range as necessary.     */

/* We assume that GC_remap is called on exactly the same range  */
/* as a previous call to GC_unmap.  It is safe to consistently  */
/* round the endpoints in both places.                          */

static void block_unmap_inner(ptr_t start_addr, size_t len)
{
    if (0 == start_addr) return;

#   ifdef USE_WINALLOC
      while (len != 0) {
          MEMORY_BASIC_INFORMATION mem_info;
          word free_len;

          if (VirtualQuery(start_addr, &mem_info, sizeof(mem_info))
              != sizeof(mem_info))
              ABORT("Weird VirtualQuery result");
          free_len = (len < mem_info.RegionSize) ? len : mem_info.RegionSize;
          if (!VirtualFree(start_addr, free_len, MEM_DECOMMIT))
              ABORT("VirtualFree failed");
          GC_unmapped_bytes += free_len;
          start_addr += free_len;
          len -= free_len;
      }
#   else
      /* We immediately remap it to prevent an intervening mmap from    */
      /* accidentally grabbing the same address space.                  */
      if (len != 0) {
#       ifdef SN_TARGET_PS3
          ps3_free_mem(start_addr, len);
#       elif defined(AIX) || defined(CYGWIN32) \
             || (defined(LINUX) && !defined(PREFER_MMAP_PROT_NONE)) \
             || defined(HPUX)
          /* On AIX, mmap(PROT_NONE) fails with ENOMEM unless the       */
          /* environment variable XPG_SUS_ENV is set to ON.             */
          /* On Cygwin, calling mmap() with the new protection flags on */
          /* an existing memory map with MAP_FIXED is broken.           */
          /* However, calling mprotect() on the given address range     */
          /* with PROT_NONE seems to work fine.                         */
          /* On Linux, low RLIMIT_AS value may lead to mmap failure.    */
#         if defined(LINUX) && !defined(FORCE_MPROTECT_BEFORE_MADVISE)
            /* On Linux, at least, madvise() should be sufficient.      */
#         else
            if (mprotect(start_addr, len, PROT_NONE))
              ABORT_ON_REMAP_FAIL("unmap: mprotect", start_addr, len);
#         endif
#         if !defined(CYGWIN32)
            /* On Linux (and some other platforms probably),    */
            /* mprotect(PROT_NONE) is just disabling access to  */
            /* the pages but not returning them to OS.          */
            if (madvise(start_addr, len, MADV_DONTNEED) == -1)
              ABORT_ON_REMAP_FAIL("unmap: madvise", start_addr, len);
#         endif
#       elif defined(EMSCRIPTEN)
          /* Nothing to do, mmap(PROT_NONE) is not supported and        */
          /* mprotect() is just a no-op.                                */
#       else
          void * result = mmap(start_addr, len, PROT_NONE,
                               MAP_PRIVATE | MAP_FIXED | OPT_MAP_ANON,
                               zero_fd, 0/* offset */);

          if (EXPECT(MAP_FAILED == result, FALSE))
            ABORT_ON_REMAP_FAIL("unmap: mmap", start_addr, len);
          if (result != (void *)start_addr)
            ABORT("unmap: mmap() result differs from start_addr");
#         if defined(CPPCHECK) || defined(LINT2)
            /* Explicitly store the resource handle to a global variable. */
            GC_noop1((word)result);
#         endif
#       endif
        GC_unmapped_bytes += len;
      }
#   endif
}

GC_INNER void GC_unmap(ptr_t start, size_t bytes)
{
    ptr_t start_addr = GC_unmap_start(start, bytes);
    ptr_t end_addr = GC_unmap_end(start, bytes);

    block_unmap_inner(start_addr, (size_t)(end_addr - start_addr));
}

GC_INNER void GC_remap(ptr_t start, size_t bytes)
{
    ptr_t start_addr = GC_unmap_start(start, bytes);
    ptr_t end_addr = GC_unmap_end(start, bytes);
    word len = end_addr - start_addr;
    if (0 == start_addr) return;

    /* FIXME: Handle out-of-memory correctly (at least for Win32)       */
#   ifdef USE_WINALLOC
      while (len != 0) {
          MEMORY_BASIC_INFORMATION mem_info;
          word alloc_len;
          ptr_t result;

          if (VirtualQuery(start_addr, &mem_info, sizeof(mem_info))
              != sizeof(mem_info))
              ABORT("Weird VirtualQuery result");
          alloc_len = (len < mem_info.RegionSize) ? len : mem_info.RegionSize;
          result = (ptr_t)VirtualAlloc(start_addr, alloc_len, MEM_COMMIT,
                                       GC_pages_executable
                                                ? PAGE_EXECUTE_READWRITE
                                                : PAGE_READWRITE);
          if (result != start_addr) {
              if (GetLastError() == ERROR_NOT_ENOUGH_MEMORY ||
                  GetLastError() == ERROR_OUTOFMEMORY) {
                  ABORT("Not enough memory to process remapping");
              } else {
                  ABORT("VirtualAlloc remapping failed");
              }
          }
#         ifdef LINT2
            GC_noop1((word)result);
#         endif
          GC_unmapped_bytes -= alloc_len;
          start_addr += alloc_len;
          len -= alloc_len;
      }
#     undef IGNORE_PAGES_EXECUTABLE
#   else
      /* It was already remapped with PROT_NONE. */
      {
#       if !defined(SN_TARGET_PS3) && !defined(FORCE_MPROTECT_BEFORE_MADVISE) \
           && defined(LINUX) && !defined(PREFER_MMAP_PROT_NONE)
          /* Nothing to unprotect as madvise() is just a hint.  */
#       elif defined(NACL) || defined(NETBSD)
          /* NaCl does not expose mprotect, but mmap should work fine.  */
          /* In case of NetBSD, mprotect fails (unlike mmap) even       */
          /* without PROT_EXEC if PaX MPROTECT feature is enabled.      */
          void *result = mmap(start_addr, len, (PROT_READ | PROT_WRITE)
                                    | (GC_pages_executable ? PROT_EXEC : 0),
                                   MAP_PRIVATE | MAP_FIXED | OPT_MAP_ANON,
                                   zero_fd, 0 /* offset */);
          if (EXPECT(MAP_FAILED == result, FALSE))
            ABORT_ON_REMAP_FAIL("remap: mmap", start_addr, len);
          if (result != (void *)start_addr)
            ABORT("remap: mmap() result differs from start_addr");
#         if defined(CPPCHECK) || defined(LINT2)
            GC_noop1((word)result);
#         endif
#         undef IGNORE_PAGES_EXECUTABLE
#       else
          if (mprotect(start_addr, len, (PROT_READ | PROT_WRITE)
                            | (GC_pages_executable ? PROT_EXEC : 0)))
            ABORT_ON_REMAP_FAIL("remap: mprotect", start_addr, len);
#         undef IGNORE_PAGES_EXECUTABLE
#       endif /* !NACL */
      }
      GC_unmapped_bytes -= len;
#   endif
}

/* Two adjacent blocks have already been unmapped and are about to      */
/* be merged.  Unmap the whole block.  This typically requires          */
/* that we unmap a small section in the middle that was not previously  */
/* unmapped due to alignment constraints.                               */
GC_INNER void GC_unmap_gap(ptr_t start1, size_t bytes1, ptr_t start2,
                           size_t bytes2)
{
    ptr_t start1_addr = GC_unmap_start(start1, bytes1);
    ptr_t end1_addr = GC_unmap_end(start1, bytes1);
    ptr_t start2_addr = GC_unmap_start(start2, bytes2);
    ptr_t start_addr = end1_addr;
    ptr_t end_addr = start2_addr;

    GC_ASSERT(start1 + bytes1 == start2);
    if (0 == start1_addr) start_addr = GC_unmap_start(start1, bytes1 + bytes2);
    if (0 == start2_addr) end_addr = GC_unmap_end(start1, bytes1 + bytes2);
    block_unmap_inner(start_addr, (size_t)(end_addr - start_addr));
}

#endif /* USE_MUNMAP */

/* Routine for pushing any additional roots.  In THREADS        */
/* environment, this is also responsible for marking from       */
/* thread stacks.                                               */
#ifndef THREADS

# if defined(EMSCRIPTEN) && defined(EMSCRIPTEN_ASYNCIFY)
#   include <emscripten.h>

    static void scan_regs_cb(void *begin, void *end)
    {
      GC_push_all_stack(begin, end);
    }

    STATIC void GC_CALLBACK GC_default_push_other_roots(void)
    {
      /* Note: this needs -sASYNCIFY linker flag. */
      emscripten_scan_registers(scan_regs_cb);
    }

# else
#   define GC_default_push_other_roots 0
# endif

#else /* THREADS */

# ifdef PCR
PCR_ERes GC_push_thread_stack(PCR_Th_T *t, PCR_Any dummy)
{
    struct PCR_ThCtl_TInfoRep info;
    PCR_ERes result;

    info.ti_stkLow = info.ti_stkHi = 0;
    result = PCR_ThCtl_GetInfo(t, &info);
    GC_push_all_stack(info.ti_stkLow, info.ti_stkHi);
    return(result);
}

/* Push the contents of an old object. We treat this as stack   */
/* data only because that makes it robust against mark stack    */
/* overflow.                                                    */
PCR_ERes GC_push_old_obj(void *p, size_t size, PCR_Any data)
{
    GC_push_all_stack(p, (ptr_t)p + size);
    return(PCR_ERes_okay);
}

extern struct PCR_MM_ProcsRep * GC_old_allocator;
                                        /* defined in pcr_interface.c.  */

STATIC void GC_CALLBACK GC_default_push_other_roots(void)
{
    /* Traverse data allocated by previous memory managers.             */
          if ((*(GC_old_allocator->mmp_enumerate))(PCR_Bool_false,
                                                   GC_push_old_obj, 0)
              != PCR_ERes_okay) {
              ABORT("Old object enumeration failed");
          }
    /* Traverse all thread stacks. */
        if (PCR_ERes_IsErr(
                PCR_ThCtl_ApplyToAllOtherThreads(GC_push_thread_stack,0))
            || PCR_ERes_IsErr(GC_push_thread_stack(PCR_Th_CurrThread(), 0))) {
          ABORT("Thread stack marking failed");
        }
}

# elif defined(SN_TARGET_PS3)
    STATIC void GC_CALLBACK GC_default_push_other_roots(void)
    {
      ABORT("GC_default_push_other_roots is not implemented");
    }

    void GC_push_thread_structures(void)
    {
      ABORT("GC_push_thread_structures is not implemented");
    }

# else /* GC_PTHREADS, or GC_WIN32_THREADS, etc.        */
    STATIC void GC_CALLBACK GC_default_push_other_roots(void)
    {
      GC_push_all_stacks();
    }
# endif

#endif /* THREADS */

GC_push_other_roots_proc GC_push_other_roots = GC_default_push_other_roots;

GC_API void GC_CALL GC_set_push_other_roots(GC_push_other_roots_proc fn)
{
    GC_push_other_roots = fn;
}

GC_API GC_push_other_roots_proc GC_CALL GC_get_push_other_roots(void)
{
    return GC_push_other_roots;
}

#if defined(SOFT_VDB) && !defined(NO_SOFT_VDB_LINUX_VER_RUNTIME_CHECK) \
    || (defined(GLIBC_2_19_TSX_BUG) && defined(PARALLEL_MARK))
  GC_INNER int GC_parse_version(int *pminor, const char *pverstr) {
    char *endp;
    unsigned long value = strtoul(pverstr, &endp, 10);
    int major = (int)value;

    if (major < 0 || (char *)pverstr == endp || (unsigned)major != value) {
      /* Parse error.   */
      return -1;
    }
    if (*endp != '.') {
      /* No minor part. */
      *pminor = -1;
    } else {
      value = strtoul(endp + 1, &endp, 10);
      *pminor = (int)value;
      if (*pminor < 0 || (unsigned)(*pminor) != value) {
        return -1;
      }
    }
    return major;
  }
#endif

/*
 * Routines for accessing dirty bits on virtual pages.
 * There are six ways to maintain this information:
 * DEFAULT_VDB: A simple dummy implementation that treats every page
 *              as possibly dirty.  This makes incremental collection
 *              useless, but the implementation is still correct.
 * Manual VDB:  Stacks and static data are always considered dirty.
 *              Heap pages are considered dirty if GC_dirty(p) has been
 *              called on some pointer p pointing to somewhere inside
 *              an object on that page.  A GC_dirty() call on a large
 *              object directly dirties only a single page, but for the
 *              manual VDB we are careful to treat an object with a dirty
 *              page as completely dirty.
 *              In order to avoid races, an object must be marked dirty
 *              after it is written, and a reference to the object
 *              must be kept on a stack or in a register in the interim.
 *              With threads enabled, an object directly reachable from the
 *              stack at the time of a collection is treated as dirty.
 *              In single-threaded mode, it suffices to ensure that no
 *              collection can take place between the pointer assignment
 *              and the GC_dirty() call.
 * PCR_VDB:     Use PPCRs virtual dirty bit facility.
 * PROC_VDB:    Use the /proc facility for reading dirty bits.  Only
 *              works under some SVR4 variants.  Even then, it may be
 *              too slow to be entirely satisfactory.  Requires reading
 *              dirty bits for entire address space.  Implementations tend
 *              to assume that the client is a (slow) debugger.
 * SOFT_VDB:    Use the /proc facility for reading soft-dirty PTEs.
 *              Works on Linux 3.18+ if the kernel is properly configured.
 *              The proposed implementation iterates over GC_heap_sects and
 *              GC_static_roots examining the soft-dirty bit of the words
 *              in /proc/self/pagemap corresponding to the pages of the
 *              sections; finally all soft-dirty bits of the process are
 *              cleared (by writing some special value to
 *              /proc/self/clear_refs file).  In case the soft-dirty bit is
 *              not supported by the kernel, MPROTECT_VDB may be defined as
 *              a fallback strategy.
 * MPROTECT_VDB:Protect pages and then catch the faults to keep track of
 *              dirtied pages.  The implementation (and implementability)
 *              is highly system dependent.  This usually fails when system
 *              calls write to a protected page.  We prevent the read system
 *              call from doing so.  It is the clients responsibility to
 *              make sure that other system calls are similarly protected
 *              or write only to the stack.
 * GWW_VDB:     Use the Win32 GetWriteWatch functions, if available, to
 *              read dirty bits.  In case it is not available (because we
 *              are running on Windows 95, Windows 2000 or earlier),
 *              MPROTECT_VDB may be defined as a fallback strategy.
 */

#if (defined(CHECKSUMS) && (defined(GWW_VDB) || defined(SOFT_VDB))) \
    || defined(PROC_VDB)
    /* Add all pages in pht2 to pht1.   */
    STATIC void GC_or_pages(page_hash_table pht1, page_hash_table pht2)
    {
      unsigned i;
      for (i = 0; i < PHT_SIZE; i++) pht1[i] |= pht2[i];
    }
#endif /* CHECKSUMS && (GWW_VDB || SOFT_VDB) || PROC_VDB */

#ifdef GWW_VDB

# define GC_GWW_BUF_LEN (MAXHINCR * HBLKSIZE / 4096 /* x86 page size */)
  /* Still susceptible to overflow, if there are very large allocations, */
  /* and everything is dirty.                                            */
  static PVOID gww_buf[GC_GWW_BUF_LEN];

#   ifndef MPROTECT_VDB
#     define GC_gww_dirty_init GC_dirty_init
#   endif

    GC_INNER GC_bool GC_gww_dirty_init(void)
    {
      detect_GetWriteWatch();
      return GC_GWW_AVAILABLE();
    }

  GC_INLINE void GC_gww_read_dirty(GC_bool output_unneeded)
  {
    word i;

    if (!output_unneeded)
      BZERO(GC_grungy_pages, sizeof(GC_grungy_pages));

    for (i = 0; i != GC_n_heap_sects; ++i) {
      GC_ULONG_PTR count;

      do {
        PVOID * pages = gww_buf;
        DWORD page_size;

        count = GC_GWW_BUF_LEN;
        /* GetWriteWatch is documented as returning non-zero when it    */
        /* fails, but the documentation doesn't explicitly say why it   */
        /* would fail or what its behavior will be if it fails.  It     */
        /* does appear to fail, at least on recent Win2K instances, if  */
        /* the underlying memory was not allocated with the appropriate */
        /* flag.  This is common if GC_enable_incremental is called     */
        /* shortly after GC initialization.  To avoid modifying the     */
        /* interface, we silently work around such a failure, it only   */
        /* affects the initial (small) heap allocation. If there are    */
        /* more dirty pages than will fit in the buffer, this is not    */
        /* treated as a failure; we must check the page count in the    */
        /* loop condition. Since each partial call will reset the       */
        /* status of some pages, this should eventually terminate even  */
        /* in the overflow case.                                        */
        if ((*(GetWriteWatch_type)(word)GetWriteWatch_func)(
                                        WRITE_WATCH_FLAG_RESET,
                                        GC_heap_sects[i].hs_start,
                                        GC_heap_sects[i].hs_bytes,
                                        pages, &count, &page_size) != 0) {
          static int warn_count = 0;
          struct hblk * start = (struct hblk *)GC_heap_sects[i].hs_start;
          static struct hblk *last_warned = 0;
          size_t nblocks = divHBLKSZ(GC_heap_sects[i].hs_bytes);

          if (i != 0 && last_warned != start && warn_count++ < 5) {
            last_warned = start;
            WARN("GC_gww_read_dirty unexpectedly failed at %p: "
                 "Falling back to marking all pages dirty\n", start);
          }
          if (!output_unneeded) {
            unsigned j;

            for (j = 0; j < nblocks; ++j) {
              word hash = PHT_HASH(start + j);
              set_pht_entry_from_index(GC_grungy_pages, hash);
            }
          }
          count = 1;  /* Done with this section. */
        } else /* succeeded */ if (!output_unneeded) {
          PVOID * pages_end = pages + count;

          while (pages != pages_end) {
            struct hblk * h = (struct hblk *) *pages++;
            struct hblk * h_end = (struct hblk *) ((char *) h + page_size);
            do {
              set_pht_entry_from_index(GC_grungy_pages, PHT_HASH(h));
            } while ((word)(++h) < (word)h_end);
          }
        }
      } while (count == GC_GWW_BUF_LEN);
      /* FIXME: It's unclear from Microsoft's documentation if this loop */
      /* is useful.  We suspect the call just fails if the buffer fills  */
      /* up.  But that should still be handled correctly.                */
    }

#   ifdef CHECKSUMS
      GC_ASSERT(!output_unneeded);
      GC_or_pages(GC_written_pages, GC_grungy_pages);
#   endif
  }

#elif defined(SOFT_VDB)
  static int clear_refs_fd = -1;
# define GC_GWW_AVAILABLE() (clear_refs_fd != -1)
#else
# define GC_GWW_AVAILABLE() FALSE
#endif /* !GWW_VDB && !SOFT_VDB */

#ifdef DEFAULT_VDB
  /* All of the following assume the allocation lock is held.   */

  /* The client asserts that unallocated pages in the heap are never    */
  /* written.                                                           */

  /* Initialize virtual dirty bit implementation.       */
  GC_INNER GC_bool GC_dirty_init(void)
  {
    GC_VERBOSE_LOG_PRINTF("Initializing DEFAULT_VDB...\n");
    /* GC_dirty_pages and GC_grungy_pages are already cleared.  */
    return TRUE;
  }
#endif /* DEFAULT_VDB */

#ifndef GC_DISABLE_INCREMENTAL
# if !defined(THREADS) || defined(HAVE_LOCKFREE_AO_OR)
#   define async_set_pht_entry_from_index(db, index) \
                        set_pht_entry_from_index_concurrent(db, index)
# elif defined(NEED_FAULT_HANDLER_LOCK)
    /* We need to lock around the bitmap update (in the write fault     */
    /* handler or GC_dirty) in order to avoid the risk of losing a bit. */
    /* We do this with a test-and-set spin lock if possible.            */
    static void async_set_pht_entry_from_index(volatile page_hash_table db,
                                               size_t index)
    {
      GC_acquire_dirty_lock();
      set_pht_entry_from_index(db, index);
      GC_release_dirty_lock();
    }
# else
#   error No test_and_set operation: Introduces a race.
# endif /* THREADS && !NEED_FAULT_HANDLER_LOCK */
#endif /* !GC_DISABLE_INCREMENTAL */

#ifdef MPROTECT_VDB
  /*
   * This implementation maintains dirty bits itself by catching write
   * faults and keeping track of them.  We assume nobody else catches
   * SIGBUS or SIGSEGV.  We assume no write faults occur in system calls.
   * This means that clients must ensure that system calls don't write
   * to the write-protected heap.  Probably the best way to do this is to
   * ensure that system calls write at most to pointer-free objects in the
   * heap, and do even that only if we are on a platform on which those
   * are not protected.
   * We assume the page size is a multiple of HBLKSIZE.
   * We prefer them to be the same.  We avoid protecting pointer-free
   * objects only if they are the same.
   */
# ifdef DARWIN
    /* Using vm_protect (mach syscall) over mprotect (BSD syscall) seems to
       decrease the likelihood of some of the problems described below. */
#   include <mach/vm_map.h>
    STATIC mach_port_t GC_task_self = 0;
#   define PROTECT_INNER(addr, len, allow_write, C_msg_prefix) \
        if (vm_protect(GC_task_self, (vm_address_t)(addr), (vm_size_t)(len), \
                       FALSE, VM_PROT_READ \
                              | ((allow_write) ? VM_PROT_WRITE : 0) \
                              | (GC_pages_executable ? VM_PROT_EXECUTE : 0)) \
                == KERN_SUCCESS) {} else ABORT(C_msg_prefix \
                                               "vm_protect() failed")

# elif !defined(USE_WINALLOC)
#   include <sys/mman.h>
#   include <signal.h>
#   if !defined(CYGWIN32) && !defined(HAIKU)
#     include <sys/syscall.h>
#   endif

#   define PROTECT_INNER(addr, len, allow_write, C_msg_prefix) \
        if (mprotect((caddr_t)(addr), (size_t)(len), \
                     PROT_READ | ((allow_write) ? PROT_WRITE : 0) \
                     | (GC_pages_executable ? PROT_EXEC : 0)) >= 0) { \
        } else if (GC_pages_executable) { \
            ABORT_ON_REMAP_FAIL(C_msg_prefix \
                                    "mprotect vdb executable pages", \
                                addr, len); \
        } else ABORT_ON_REMAP_FAIL(C_msg_prefix "mprotect vdb", addr, len)
#   undef IGNORE_PAGES_EXECUTABLE

# else /* USE_WINALLOC */
#   ifndef MSWINCE
#     include <signal.h>
#   endif

    static DWORD protect_junk;
#   define PROTECT_INNER(addr, len, allow_write, C_msg_prefix) \
        if (VirtualProtect(addr, len, \
                           GC_pages_executable ? \
                                ((allow_write) ? PAGE_EXECUTE_READWRITE : \
                                                 PAGE_EXECUTE_READ) : \
                                 (allow_write) ? PAGE_READWRITE : \
                                                 PAGE_READONLY, \
                           &protect_junk)) { \
        } else ABORT_ARG1(C_msg_prefix "VirtualProtect failed", \
                          ": errcode= 0x%X", (unsigned)GetLastError())
# endif /* USE_WINALLOC */

# define PROTECT(addr, len) PROTECT_INNER(addr, len, FALSE, "")
# define UNPROTECT(addr, len) PROTECT_INNER(addr, len, TRUE, "un-")

# if defined(MSWIN32)
    typedef LPTOP_LEVEL_EXCEPTION_FILTER SIG_HNDLR_PTR;
#   undef SIG_DFL
#   define SIG_DFL (LPTOP_LEVEL_EXCEPTION_FILTER)((signed_word)-1)
# elif defined(MSWINCE)
    typedef LONG (WINAPI *SIG_HNDLR_PTR)(struct _EXCEPTION_POINTERS *);
#   undef SIG_DFL
#   define SIG_DFL (SIG_HNDLR_PTR) (-1)
# elif defined(DARWIN)
    typedef void (* SIG_HNDLR_PTR)();
# else
    typedef void (* SIG_HNDLR_PTR)(int, siginfo_t *, void *);
    typedef void (* PLAIN_HNDLR_PTR)(int);
# endif

#ifndef DARWIN
  STATIC SIG_HNDLR_PTR GC_old_segv_handler = 0;
                        /* Also old MSWIN32 ACCESS_VIOLATION filter */
# if defined(FREEBSD) || defined(HPUX) || defined(HURD) || defined(LINUX)
    STATIC SIG_HNDLR_PTR GC_old_bus_handler = 0;
#   ifndef LINUX
      STATIC GC_bool GC_old_bus_handler_used_si = FALSE;
#   endif
# endif
# if !defined(MSWIN32) && !defined(MSWINCE)
    STATIC GC_bool GC_old_segv_handler_used_si = FALSE;
# endif /* !MSWIN32 */
#endif /* !DARWIN */

#ifdef THREADS
  /* This function is used only by the fault handler.  Potential data   */
  /* race between this function and GC_install_header, GC_remove_header */
  /* should not be harmful because the added or removed header should   */
  /* be already unprotected.                                            */
  GC_ATTR_NO_SANITIZE_THREAD
  static GC_bool is_header_found_async(void *addr)
  {
#   ifdef HASH_TL
      hdr *result;
      GET_HDR((ptr_t)addr, result);
      return result != NULL;
#   else
      return HDR_INNER(addr) != NULL;
#   endif
  }
#else
# define is_header_found_async(addr) (HDR(addr) != NULL)
#endif /* !THREADS */

#ifndef DARWIN

# if !defined(MSWIN32) && !defined(MSWINCE)
#   include <errno.h>
#   if defined(FREEBSD) || defined(HURD) || defined(HPUX)
#     define SIG_OK (sig == SIGBUS || sig == SIGSEGV)
#   else
#     define SIG_OK (sig == SIGSEGV)
                            /* Catch SIGSEGV but ignore SIGBUS. */
#   endif
#   if defined(FREEBSD)
#     ifndef SEGV_ACCERR
#       define SEGV_ACCERR 2
#     endif
#     if defined(AARCH64) || defined(ARM32) || defined(MIPS) \
         || __FreeBSD__ >= 7
#       define CODE_OK (si -> si_code == SEGV_ACCERR)
#     elif defined(POWERPC)
#       define AIM  /* Pretend that we're AIM. */
#       include <machine/trap.h>
#       define CODE_OK (si -> si_code == EXC_DSI \
                        || si -> si_code == SEGV_ACCERR)
#     else
#       define CODE_OK (si -> si_code == BUS_PAGE_FAULT \
                        || si -> si_code == SEGV_ACCERR)
#     endif
#   elif defined(OSF1)
#     define CODE_OK (si -> si_code == 2 /* experimentally determined */)
#   elif defined(IRIX5)
#     define CODE_OK (si -> si_code == EACCES)
#   elif defined(CYGWIN32) || defined(HAIKU) || defined(HURD)
#     define CODE_OK TRUE
#   elif defined(LINUX)
#     define CODE_OK TRUE
      /* Empirically c.trapno == 14, on IA32, but is that useful?       */
      /* Should probably consider alignment issues on other             */
      /* architectures.                                                 */
#   elif defined(HPUX)
#     define CODE_OK (si -> si_code == SEGV_ACCERR \
                      || si -> si_code == BUS_ADRERR \
                      || si -> si_code == BUS_UNKNOWN \
                      || si -> si_code == SEGV_UNKNOWN \
                      || si -> si_code == BUS_OBJERR)
#   elif defined(SUNOS5SIGS)
#     define CODE_OK (si -> si_code == SEGV_ACCERR)
#   endif
#   ifndef NO_GETCONTEXT
#     include <ucontext.h>
#   endif
    STATIC void GC_write_fault_handler(int sig, siginfo_t *si, void *raw_sc)
# else
#   define SIG_OK (exc_info -> ExceptionRecord -> ExceptionCode \
                     == STATUS_ACCESS_VIOLATION)
#   define CODE_OK (exc_info -> ExceptionRecord -> ExceptionInformation[0] \
                      == 1) /* Write fault */
    STATIC LONG WINAPI GC_write_fault_handler(
                                struct _EXCEPTION_POINTERS *exc_info)
# endif /* MSWIN32 || MSWINCE */
  {
#   if !defined(MSWIN32) && !defined(MSWINCE)
        char *addr = (char *)si->si_addr;
#   else
        char * addr = (char *) (exc_info -> ExceptionRecord
                                -> ExceptionInformation[1]);
#   endif

    if (SIG_OK && CODE_OK) {
        struct hblk * h = (struct hblk *)((word)addr
                                & ~(word)(GC_page_size-1));
        GC_bool in_allocd_block;
        size_t i;

        GC_ASSERT(GC_page_size != 0);
#       ifdef CHECKSUMS
          GC_record_fault(h);
#       endif
#       ifdef SUNOS5SIGS
            /* Address is only within the correct physical page.        */
            in_allocd_block = FALSE;
            for (i = 0; i < divHBLKSZ(GC_page_size); i++) {
              if (is_header_found_async(&h[i])) {
                in_allocd_block = TRUE;
                break;
              }
            }
#       else
            in_allocd_block = is_header_found_async(addr);
#       endif
        if (!in_allocd_block) {
            /* FIXME - We should make sure that we invoke the   */
            /* old handler with the appropriate calling         */
            /* sequence, which often depends on SA_SIGINFO.     */

            /* Heap blocks now begin and end on page boundaries */
            SIG_HNDLR_PTR old_handler;

#           if defined(MSWIN32) || defined(MSWINCE)
                old_handler = GC_old_segv_handler;
#           else
                GC_bool used_si;

#             if defined(FREEBSD) || defined(HURD) || defined(HPUX)
                if (sig == SIGBUS) {
                   old_handler = GC_old_bus_handler;
                   used_si = GC_old_bus_handler_used_si;
                } else
#             endif
                /* else */ {
                   old_handler = GC_old_segv_handler;
                   used_si = GC_old_segv_handler_used_si;
                }
#           endif

            if (old_handler == (SIG_HNDLR_PTR)(signed_word)SIG_DFL) {
#               if !defined(MSWIN32) && !defined(MSWINCE)
                    ABORT_ARG1("Unexpected bus error or segmentation fault",
                               " at %p", (void *)addr);
#               else
                    return(EXCEPTION_CONTINUE_SEARCH);
#               endif
            } else {
                /*
                 * FIXME: This code should probably check if the
                 * old signal handler used the traditional style and
                 * if so call it using that style.
                 */
#               if defined(MSWIN32) || defined(MSWINCE)
                    return((*old_handler)(exc_info));
#               else
                    if (used_si)
                      ((SIG_HNDLR_PTR)old_handler) (sig, si, raw_sc);
                    else
                      /* FIXME: should pass nonstandard args as well. */
                      ((PLAIN_HNDLR_PTR)(signed_word)old_handler)(sig);
                    return;
#               endif
            }
        }
        UNPROTECT(h, GC_page_size);
        /* We need to make sure that no collection occurs between       */
        /* the UNPROTECT and the setting of the dirty bit.  Otherwise   */
        /* a write by a third thread might go unnoticed.  Reversing     */
        /* the order is just as bad, since we would end up unprotecting */
        /* a page in a GC cycle during which it's not marked.           */
        /* Currently we do this by disabling the thread stopping        */
        /* signals while this handler is running.  An alternative might */
        /* be to record the fact that we're about to unprotect, or      */
        /* have just unprotected a page in the GC's thread structure,   */
        /* and then to have the thread stopping code set the dirty      */
        /* flag, if necessary.                                          */
        for (i = 0; i < divHBLKSZ(GC_page_size); i++) {
            word index = PHT_HASH(h+i);

            async_set_pht_entry_from_index(GC_dirty_pages, index);
        }
        /* The write may not take place before dirty bits are read.     */
        /* But then we'll fault again ...                               */
#       if defined(MSWIN32) || defined(MSWINCE)
            return(EXCEPTION_CONTINUE_EXECUTION);
#       else
            return;
#       endif
    }
#   if defined(MSWIN32) || defined(MSWINCE)
      return EXCEPTION_CONTINUE_SEARCH;
#   else
      ABORT_ARG1("Unexpected bus error or segmentation fault",
                 " at %p", (void *)addr);
#   endif
  }

# if defined(GC_WIN32_THREADS) && !defined(CYGWIN32)
    GC_INNER void GC_set_write_fault_handler(void)
    {
      SetUnhandledExceptionFilter(GC_write_fault_handler);
    }
# endif

# ifdef SOFT_VDB
    static GC_bool soft_dirty_init(void);
# endif

  GC_INNER GC_bool GC_dirty_init(void)
  {
#   if !defined(MSWIN32) && !defined(MSWINCE)
      struct sigaction act, oldact;
#   endif

#   ifdef COUNT_PROTECTED_REGIONS
      GC_ASSERT(GC_page_size != 0);
      if ((signed_word)(GC_heapsize / (word)GC_page_size)
                >= ((signed_word)GC_UNMAPPED_REGIONS_SOFT_LIMIT
                    - GC_num_unmapped_regions) * 2) {
        GC_COND_LOG_PRINTF("Cannot turn on GC incremental mode"
                           " as heap contains too many pages\n");
        return FALSE;
      }
#   endif
#   if !defined(MSWIN32) && !defined(MSWINCE)
      act.sa_flags = SA_RESTART | SA_SIGINFO;
      act.sa_sigaction = GC_write_fault_handler;
      (void)sigemptyset(&act.sa_mask);
#     if defined(THREADS) && !defined(GC_OPENBSD_UTHREADS) \
         && !defined(GC_WIN32_THREADS) && !defined(NACL)
        /* Arrange to postpone the signal while we are in a write fault */
        /* handler.  This effectively makes the handler atomic w.r.t.   */
        /* stopping the world for GC.                                   */
        (void)sigaddset(&act.sa_mask, GC_get_suspend_signal());
#     endif
#   endif /* !MSWIN32 */
    GC_VERBOSE_LOG_PRINTF(
                "Initializing mprotect virtual dirty bit implementation\n");
    if (GC_page_size % HBLKSIZE != 0) {
        ABORT("Page size not multiple of HBLKSIZE");
    }
#   ifdef GWW_VDB
      if (GC_gww_dirty_init()) {
        GC_COND_LOG_PRINTF("Using GetWriteWatch()\n");
        return TRUE;
      }
#   elif defined(SOFT_VDB)
      if (soft_dirty_init()) {
        GC_COND_LOG_PRINTF("Using soft-dirty bit feature\n");
        return TRUE;
      }
#   endif
#   ifdef MSWIN32
      GC_old_segv_handler = SetUnhandledExceptionFilter(
                                        GC_write_fault_handler);
      if (GC_old_segv_handler != NULL) {
        GC_COND_LOG_PRINTF("Replaced other UnhandledExceptionFilter\n");
      } else {
          GC_old_segv_handler = SIG_DFL;
      }
#   elif defined(MSWINCE)
      /* MPROTECT_VDB is unsupported for WinCE at present.      */
      /* FIXME: implement it (if possible). */
#   else
      /* act.sa_restorer is deprecated and should not be initialized. */
#     if defined(GC_IRIX_THREADS)
        sigaction(SIGSEGV, 0, &oldact);
        sigaction(SIGSEGV, &act, 0);
#     else
        {
          int res = sigaction(SIGSEGV, &act, &oldact);
          if (res != 0) ABORT("Sigaction failed");
        }
#     endif
      if (oldact.sa_flags & SA_SIGINFO) {
        GC_old_segv_handler = oldact.sa_sigaction;
        GC_old_segv_handler_used_si = TRUE;
      } else {
        GC_old_segv_handler = (SIG_HNDLR_PTR)(signed_word)oldact.sa_handler;
        GC_old_segv_handler_used_si = FALSE;
      }
      if (GC_old_segv_handler == (SIG_HNDLR_PTR)(signed_word)SIG_IGN) {
        WARN("Previously ignored segmentation violation!?\n", 0);
        GC_old_segv_handler = (SIG_HNDLR_PTR)(signed_word)SIG_DFL;
      }
      if (GC_old_segv_handler != (SIG_HNDLR_PTR)(signed_word)SIG_DFL) {
        GC_VERBOSE_LOG_PRINTF("Replaced other SIGSEGV handler\n");
      }
#   if defined(HPUX) || defined(LINUX) || defined(HURD) \
       || (defined(FREEBSD) && defined(SUNOS5SIGS))
      sigaction(SIGBUS, &act, &oldact);
      if ((oldact.sa_flags & SA_SIGINFO) != 0) {
        GC_old_bus_handler = oldact.sa_sigaction;
#       if !defined(LINUX)
          GC_old_bus_handler_used_si = TRUE;
#       endif
      } else {
        GC_old_bus_handler = (SIG_HNDLR_PTR)(signed_word)oldact.sa_handler;
      }
      if (GC_old_bus_handler == (SIG_HNDLR_PTR)(signed_word)SIG_IGN) {
        WARN("Previously ignored bus error!?\n", 0);
#       if !defined(LINUX)
          GC_old_bus_handler = (SIG_HNDLR_PTR)(signed_word)SIG_DFL;
#       else
          /* GC_old_bus_handler is not used by GC_write_fault_handler.  */
#       endif
      } else if (GC_old_bus_handler != (SIG_HNDLR_PTR)(signed_word)SIG_DFL) {
          GC_VERBOSE_LOG_PRINTF("Replaced other SIGBUS handler\n");
      }
#   endif /* HPUX || LINUX || HURD || (FREEBSD && SUNOS5SIGS) */
#   endif /* !MSWIN32 && !MSWINCE */
#   if defined(CPPCHECK) && defined(ADDRESS_SANITIZER)
      GC_noop1((word)&__asan_default_options);
#   endif
    return TRUE;
  }
#endif /* !DARWIN */

GC_API int GC_CALL GC_incremental_protection_needs(void)
{
    GC_ASSERT(GC_is_initialized);
#   if defined(GWW_VDB) || defined(SOFT_VDB)
      /* Only if the incremental mode is already switched on.   */
      if (GC_GWW_AVAILABLE())
        return GC_PROTECTS_NONE;
#   endif
    if (GC_page_size == HBLKSIZE) {
        return GC_PROTECTS_POINTER_HEAP;
    } else {
        return GC_PROTECTS_POINTER_HEAP | GC_PROTECTS_PTRFREE_HEAP;
    }
}
#define HAVE_INCREMENTAL_PROTECTION_NEEDS

#define IS_PTRFREE(hhdr) ((hhdr)->hb_descr == 0)
#define PAGE_ALIGNED(x) !((word)(x) & (GC_page_size - 1))

STATIC void GC_protect_heap(void)
{
    unsigned i;
    GC_bool protect_all =
        (0 != (GC_incremental_protection_needs() & GC_PROTECTS_PTRFREE_HEAP));

    GC_ASSERT(GC_page_size != 0);
    for (i = 0; i < GC_n_heap_sects; i++) {
        ptr_t start = GC_heap_sects[i].hs_start;
        size_t len = GC_heap_sects[i].hs_bytes;

        if (protect_all) {
          PROTECT(start, len);
        } else {
          struct hblk * current;
          struct hblk * current_start; /* Start of block to be protected. */
          struct hblk * limit;

          GC_ASSERT(PAGE_ALIGNED(len));
          GC_ASSERT(PAGE_ALIGNED(start));
          current_start = current = (struct hblk *)start;
          limit = (struct hblk *)(start + len);
          while ((word)current < (word)limit) {
            hdr * hhdr;
            word nhblks;
            GC_bool is_ptrfree;

            GC_ASSERT(PAGE_ALIGNED(current));
            GET_HDR(current, hhdr);
            if (IS_FORWARDING_ADDR_OR_NIL(hhdr)) {
              /* This can happen only if we're at the beginning of a    */
              /* heap segment, and a block spans heap segments.         */
              /* We will handle that block as part of the preceding     */
              /* segment.                                               */
              GC_ASSERT(current_start == current);
              current_start = ++current;
              continue;
            }
            if (HBLK_IS_FREE(hhdr)) {
              GC_ASSERT(PAGE_ALIGNED(hhdr -> hb_sz));
              nhblks = divHBLKSZ(hhdr -> hb_sz);
              is_ptrfree = TRUE;        /* dirty on alloc */
            } else {
              nhblks = OBJ_SZ_TO_BLOCKS(hhdr -> hb_sz);
              is_ptrfree = IS_PTRFREE(hhdr);
            }
            if (is_ptrfree) {
              if ((word)current_start < (word)current) {
                PROTECT(current_start, (ptr_t)current - (ptr_t)current_start);
              }
              current_start = (current += nhblks);
            } else {
              current += nhblks;
            }
          }
          if ((word)current_start < (word)current) {
            PROTECT(current_start, (ptr_t)current - (ptr_t)current_start);
          }
        }
    }
}

# if defined(CAN_HANDLE_FORK) && defined(DARWIN) && defined(THREADS) \
     || defined(COUNT_PROTECTED_REGIONS)
    /* Remove protection for the entire heap not updating GC_dirty_pages. */
    STATIC void GC_unprotect_all_heap(void)
    {
      unsigned i;

      GC_ASSERT(I_HOLD_LOCK());
      GC_ASSERT(GC_auto_incremental);
      for (i = 0; i < GC_n_heap_sects; i++) {
        UNPROTECT(GC_heap_sects[i].hs_start, GC_heap_sects[i].hs_bytes);
      }
    }
# endif /* CAN_HANDLE_FORK && DARWIN && THREADS || COUNT_PROTECTED_REGIONS */

# ifdef COUNT_PROTECTED_REGIONS
    GC_INNER void GC_handle_protected_regions_limit(void)
    {
      GC_ASSERT(GC_page_size != 0);
      /* To prevent exceeding the limit of vm.max_map_count, the most */
      /* trivial (though highly restrictive) way is to turn off the   */
      /* incremental collection mode (based on mprotect) once the     */
      /* number of pages in the heap reaches that limit.              */
      if (GC_auto_incremental && !GC_GWW_AVAILABLE()
          && (signed_word)(GC_heapsize / (word)GC_page_size)
                >= ((signed_word)GC_UNMAPPED_REGIONS_SOFT_LIMIT
                    - GC_num_unmapped_regions) * 2) {
        GC_unprotect_all_heap();
#       ifdef DARWIN
          GC_task_self = 0;
#       endif
        GC_incremental = FALSE;
        WARN("GC incremental mode is turned off"
             " to prevent hitting VM maps limit\n", 0);
      }
    }
# endif /* COUNT_PROTECTED_REGIONS */

#endif /* MPROTECT_VDB */

#if !defined(THREADS) && (defined(PROC_VDB) || defined(SOFT_VDB))
  static pid_t saved_proc_pid; /* pid used to compose /proc file names */
#endif

#ifdef PROC_VDB
/* This implementation assumes the Solaris new structured /proc         */
/* pseudo-file-system from which we can read page modified bits.  This  */
/* facility is far from optimal (e.g. we would like to get the info for */
/* only some of the address space), but it avoids intercepting system   */
/* calls.                                                               */

# include <errno.h>
# include <sys/types.h>
# include <sys/signal.h>
# include <sys/syscall.h>
# include <sys/stat.h>

# ifdef GC_NO_SYS_FAULT_H
    /* This exists only to check PROC_VDB code compilation (on Linux).  */
#   define PG_MODIFIED 1
    struct prpageheader {
      int dummy[2]; /* pr_tstamp */
      unsigned long pr_nmap;
      unsigned long pr_npage;
    };
    struct prasmap {
      char *pr_vaddr;
      size_t pr_npage;
      char dummy1[64+8]; /* pr_mapname, pr_offset */
      unsigned pr_mflags;
      unsigned pr_pagesize;
      int dummy2[2];
    };
# else
    /* Use the new structured /proc definitions. */
#   include <procfs.h>
# endif

# define INITIAL_BUF_SZ 16384
  STATIC size_t GC_proc_buf_size = INITIAL_BUF_SZ;
  STATIC char *GC_proc_buf = NULL;
  STATIC int GC_proc_fd = -1;

  static GC_bool proc_dirty_open_files(void)
  {
    char buf[40];
    pid_t pid = getpid();

    (void)snprintf(buf, sizeof(buf), "/proc/%ld/pagedata", (long)pid);
    buf[sizeof(buf) - 1] = '\0';
    GC_proc_fd = open(buf, O_RDONLY);
    if (-1 == GC_proc_fd) {
      WARN("/proc open failed; cannot enable GC incremental mode\n", 0);
      return FALSE;
    }
    if (syscall(SYS_fcntl, GC_proc_fd, F_SETFD, FD_CLOEXEC) == -1)
      WARN("Could not set FD_CLOEXEC for /proc\n", 0);
#   ifndef THREADS
      saved_proc_pid = pid; /* updated on success only */
#   endif
    return TRUE;
  }

# ifdef CAN_HANDLE_FORK
    GC_INNER void GC_dirty_update_child(void)
    {
      if (-1 == GC_proc_fd)
        return; /* GC incremental mode is off */

      close(GC_proc_fd);
      if (!proc_dirty_open_files())
        GC_incremental = FALSE; /* should be safe to turn it off */
    }
# endif /* CAN_HANDLE_FORK */

GC_INNER GC_bool GC_dirty_init(void)
{
    if (GC_bytes_allocd != 0 || GC_bytes_allocd_before_gc != 0) {
      memset(GC_written_pages, 0xff, sizeof(page_hash_table));
      GC_VERBOSE_LOG_PRINTF(
                "Allocated %lu bytes: all pages may have been written\n",
                (unsigned long)(GC_bytes_allocd + GC_bytes_allocd_before_gc));
    }
    if (!proc_dirty_open_files())
      return FALSE;
    GC_proc_buf = GC_scratch_alloc(GC_proc_buf_size);
    if (GC_proc_buf == NULL)
      ABORT("Insufficient space for /proc read");
    return TRUE;
}

GC_INLINE void GC_proc_read_dirty(GC_bool output_unneeded)
{
    int nmaps;
    ssize_t pagedata_len;
    char * bufp = GC_proc_buf;
    int i;

#   ifndef THREADS
      /* If the current pid differs from the saved one, then we are in  */
      /* the forked (child) process, the current /proc file should be   */
      /* closed, the new one should be opened with the updated path.    */
      /* Note, this is not needed for multi-threaded case because       */
      /* fork_child_proc() reopens the file right after fork.           */
      if (getpid() != saved_proc_pid
          && (-1 == GC_proc_fd /* no need to retry */
              || (close(GC_proc_fd), !proc_dirty_open_files()))) {
        /* Failed to reopen the file.  Punt!    */
        if (!output_unneeded)
          memset(GC_grungy_pages, 0xff, sizeof(page_hash_table));
        memset(GC_written_pages, 0xff, sizeof(page_hash_table));
        return;
      }
#   endif

    for (;;) {
        char *new_buf;
        size_t new_size;

        pagedata_len = PROC_READ(GC_proc_fd, bufp, GC_proc_buf_size);
        if (EXPECT(pagedata_len != -1, TRUE))
            break;
        if (errno != E2BIG) {
            WARN("read /proc failed, errno= %" WARN_PRIdPTR "\n",
                 (signed_word)errno);
            /* Punt: */
            if (!output_unneeded)
              memset(GC_grungy_pages, 0xff, sizeof (page_hash_table));
            memset(GC_written_pages, 0xff, sizeof(page_hash_table));
            return;
        }
        /* Retry with larger buffer.    */
        new_size = 2 * GC_proc_buf_size;
        /* Alternatively, we could use fstat() to determine the required    */
        /* buffer size.                                                     */
#       ifdef DEBUG_DIRTY_BITS
          GC_log_printf("Growing proc buf to %lu bytes at collection #%lu\n",
                        (unsigned long)new_size, (unsigned long)GC_gc_no + 1);
#       endif
        new_buf = GC_scratch_alloc(new_size);
        if (new_buf != NULL) {
            GC_scratch_recycle_no_gww(bufp, GC_proc_buf_size);
            GC_proc_buf = bufp = new_buf;
            GC_proc_buf_size = new_size;
        }
    }
    GC_ASSERT((size_t)pagedata_len <= GC_proc_buf_size);

    /* Copy dirty bits into GC_grungy_pages     */
    BZERO(GC_grungy_pages, sizeof(GC_grungy_pages));
    nmaps = ((struct prpageheader *)bufp) -> pr_nmap;
#   ifdef DEBUG_DIRTY_BITS
      GC_log_printf("Proc VDB read: pr_nmap= %u, pr_npage= %lu\n",
                    nmaps, ((struct prpageheader *)bufp)->pr_npage);
#   endif
#   if defined(GC_NO_SYS_FAULT_H) && defined(CPPCHECK)
      GC_noop1(((struct prpageheader *)bufp)->dummy[0]);
#   endif
    bufp += sizeof(struct prpageheader);
    for (i = 0; i < nmaps; i++) {
        struct prasmap * map = (struct prasmap *)bufp;
        ptr_t vaddr, limit;
        unsigned long npages = 0;
        unsigned pagesize;

        bufp += sizeof(struct prasmap);
        /* Ensure no buffer overrun. */
        if (bufp - GC_proc_buf < pagedata_len)
          npages = map -> pr_npage;
        if (bufp - GC_proc_buf > pagedata_len - (ssize_t)npages)
          ABORT("Wrong pr_nmap or pr_npage read from /proc");

        vaddr = (ptr_t)(map -> pr_vaddr);
        pagesize = map -> pr_pagesize;
#       if defined(GC_NO_SYS_FAULT_H) && defined(CPPCHECK)
          GC_noop1(map->dummy1[0] + map->dummy2[0]);
#       endif
#       ifdef DEBUG_DIRTY_BITS
          GC_log_printf(
                "pr_vaddr= %p, npage= %lu, mflags= 0x%x, pagesize= 0x%x\n",
                (void *)vaddr, npages, map->pr_mflags, pagesize);
#       endif

        limit = vaddr + pagesize * npages;
        for (; (word)vaddr < (word)limit; vaddr += pagesize) {
            if ((*bufp++) & PG_MODIFIED) {
                struct hblk * h;
                ptr_t next_vaddr = vaddr + pagesize;
#               ifdef DEBUG_DIRTY_BITS
                  GC_log_printf("dirty page at: %p\n", (void *)vaddr);
#               endif
                for (h = (struct hblk *)vaddr;
                     (word)h < (word)next_vaddr; h++) {
                    word index = PHT_HASH(h);

                    set_pht_entry_from_index(GC_grungy_pages, index);
                }
            }
        }
        /* According to the new structured "pagedata" file format,  */
        /* an 8-byte alignment is enforced (preceding the next      */
        /* struct prasmap) regardless of the pointer size.          */
        bufp = (char *)(((word)bufp + 7) & ~(word)7);
    }
#   ifdef DEBUG_DIRTY_BITS
      GC_log_printf("Proc VDB read done\n");
#   endif

    /* Update GC_written_pages (even if output_unneeded).       */
    GC_or_pages(GC_written_pages, GC_grungy_pages);
}

#endif /* PROC_VDB */

#ifdef SOFT_VDB
# ifndef VDB_BUF_SZ
#   define VDB_BUF_SZ 16384
# endif

  static int open_proc_fd(pid_t pid, const char *proc_filename, int mode)
  {
    int f;
    char buf[40];

    (void)snprintf(buf, sizeof(buf), "/proc/%ld/%s", (long)pid,
                   proc_filename);
    buf[sizeof(buf) - 1] = '\0';
    f = open(buf, mode);
    if (-1 == f) {
      WARN("/proc/self/%s open failed; cannot enable GC incremental mode\n",
           proc_filename);
    } else if (fcntl(f, F_SETFD, FD_CLOEXEC) == -1) {
      WARN("Could not set FD_CLOEXEC for /proc\n", 0);
    }
    return f;
  }

# include <stdint.h> /* for uint64_t */

  typedef uint64_t pagemap_elem_t;

  static pagemap_elem_t *soft_vdb_buf;
  static int pagemap_fd;

  static GC_bool soft_dirty_open_files(void)
  {
    pid_t pid = getpid();

    clear_refs_fd = open_proc_fd(pid, "clear_refs", O_WRONLY);
    if (-1 == clear_refs_fd)
      return FALSE;
    pagemap_fd = open_proc_fd(pid, "pagemap", O_RDONLY);
    if (-1 == pagemap_fd) {
      close(clear_refs_fd);
      clear_refs_fd = -1;
      return FALSE;
    }
#   ifndef THREADS
      saved_proc_pid = pid; /* updated on success only */
#   endif
    return TRUE;
  }

# ifdef CAN_HANDLE_FORK
    GC_INNER void GC_dirty_update_child(void)
    {
      if (-1 == clear_refs_fd)
        return; /* GC incremental mode is off */

      close(clear_refs_fd);
      close(pagemap_fd);
      if (!soft_dirty_open_files())
        GC_incremental = FALSE;
    }
# endif /* CAN_HANDLE_FORK */

  /* Clear soft-dirty bits from the task's PTEs.        */
  static void clear_soft_dirty_bits(void)
  {
    ssize_t res = write(clear_refs_fd, "4\n", 2);

    if (res != 2)
      ABORT_ARG1("Failed to write to /proc/self/clear_refs",
                 ": errno= %d", res < 0 ? errno : 0);
  }

  /* The bit 55 of the 64-bit qword of pagemap file is the soft-dirty one. */
# define PM_SOFTDIRTY_MASK ((pagemap_elem_t)1 << 55)

  static GC_bool detect_soft_dirty_supported(ptr_t vaddr)
  {
    off_t fpos;
    pagemap_elem_t buf[1];

    GC_ASSERT(GC_page_size != 0);
    *vaddr = 1; /* make it dirty */
    fpos = (off_t)((word)vaddr / GC_page_size * sizeof(pagemap_elem_t));

    for (;;) {
      /* Read the relevant PTE from the pagemap file.   */
      if (lseek(pagemap_fd, fpos, SEEK_SET) == (off_t)(-1))
        return FALSE;
      if (PROC_READ(pagemap_fd, buf, sizeof(buf)) != (int)sizeof(buf))
        return FALSE;

      /* Is the soft-dirty bit unset?   */
      if ((buf[0] & PM_SOFTDIRTY_MASK) == 0) return FALSE;

      if (0 == *vaddr) break;
      /* Retry to check that writing to clear_refs works as expected.   */
      /* This malfunction of the soft-dirty bits implementation is      */
      /* observed on some Linux kernels on Power9 (e.g. in Fedora 36).  */
      clear_soft_dirty_bits();
      *vaddr = 0;
    }
    return TRUE; /* success */
  }

# ifndef NO_SOFT_VDB_LINUX_VER_RUNTIME_CHECK
#   include <sys/utsname.h>
#   include <string.h> /* for strcmp() */

    /* Ensure the linux (kernel) major/minor version is as given or higher. */
    static GC_bool ensure_min_linux_ver(int major, int minor) {
      struct utsname info;
      int actual_major;
      int actual_minor = -1;

      if (uname(&info) == -1) {
        return FALSE; /* uname() failed, should not happen actually. */
      }
      if (strcmp(info.sysname, "Linux")) {
        WARN("Cannot ensure Linux version as running on other OS: %s\n",
             info.sysname);
        return FALSE;
      }
      actual_major = GC_parse_version(&actual_minor, info.release);
      return actual_major > major
             || (actual_major == major && actual_minor >= minor);
    }
# endif

# ifdef MPROTECT_VDB
    static GC_bool soft_dirty_init(void)
# else
    GC_INNER GC_bool GC_dirty_init(void)
# endif
  {
#   ifdef MPROTECT_VDB
      char * str = GETENV("GC_USE_GETWRITEWATCH");

#     ifdef GC_PREFER_MPROTECT_VDB
        if (str == NULL || (*str == '0' && *(str + 1) == '\0'))
          return FALSE; /* the environment variable is unset or set to "0" */
#     else
        if (str != NULL && *str == '0' && *(str + 1) == '\0')
          return FALSE; /* the environment variable is set "0" */
#     endif
#   endif
    GC_ASSERT(NULL == soft_vdb_buf);
#   ifndef NO_SOFT_VDB_LINUX_VER_RUNTIME_CHECK
      if (!ensure_min_linux_ver(3, 18)) {
        GC_COND_LOG_PRINTF(
            "Running on old kernel lacking correct soft-dirty bit support\n");
        return FALSE;
      }
#   endif
    if (!soft_dirty_open_files())
      return FALSE;
    soft_vdb_buf = (pagemap_elem_t *)GC_scratch_alloc(VDB_BUF_SZ);
    if (NULL == soft_vdb_buf)
      ABORT("Insufficient space for /proc pagemap buffer");
    if (!detect_soft_dirty_supported((ptr_t)soft_vdb_buf)) {
      GC_COND_LOG_PRINTF("Soft-dirty bit is not supported by kernel\n");
      /* Release the resources. */
      GC_scratch_recycle_no_gww(soft_vdb_buf, VDB_BUF_SZ);
      soft_vdb_buf = NULL;
      close(clear_refs_fd);
      clear_refs_fd = -1;
      close(pagemap_fd);
      return FALSE;
    }
    return TRUE;
  }

  static off_t pagemap_buf_fpos; /* valid only if pagemap_buf_len > 0 */
  static size_t pagemap_buf_len;

  /* Read bytes from /proc/self/pagemap at given file position.         */
  /* len - the maximum number of bytes to read; (*pres) - amount of     */
  /* bytes actually read, always bigger than 0 but never exceeds len;   */
  /* next_fpos_hint - the file position of the next bytes block to read */
  /* ahead if possible (0 means no information provided).               */
  static const pagemap_elem_t *pagemap_buffered_read(size_t *pres,
                                                     off_t fpos, size_t len,
                                                     off_t next_fpos_hint)
  {
    ssize_t res;
    size_t ofs;

    GC_ASSERT(len > 0);
    if (pagemap_buf_fpos <= fpos
        && fpos < pagemap_buf_fpos + (off_t)pagemap_buf_len) {
      /* The requested data is already in the buffer.   */
      ofs = (size_t)(fpos - pagemap_buf_fpos);
      res = (ssize_t)(pagemap_buf_fpos + pagemap_buf_len - fpos);
    } else {
      off_t aligned_pos = fpos & ~(off_t)(GC_page_size < VDB_BUF_SZ
                                            ? GC_page_size-1 : VDB_BUF_SZ-1);

      for (;;) {
        size_t count;

        if ((0 == pagemap_buf_len
             || pagemap_buf_fpos + (off_t)pagemap_buf_len != aligned_pos)
            && lseek(pagemap_fd, aligned_pos, SEEK_SET) == (off_t)(-1))
          ABORT_ARG2("Failed to lseek /proc/self/pagemap",
                     ": offset= %lu, errno= %d", (unsigned long)fpos, errno);

        /* How much to read at once?    */
        ofs = (size_t)(fpos - aligned_pos);
        GC_ASSERT(ofs < VDB_BUF_SZ);
        if (next_fpos_hint > aligned_pos
            && next_fpos_hint - aligned_pos < VDB_BUF_SZ) {
          count = VDB_BUF_SZ;
        } else {
          count = len + ofs;
          if (count > VDB_BUF_SZ)
            count = VDB_BUF_SZ;
        }

        GC_ASSERT(count % sizeof(pagemap_elem_t) == 0);
        res = PROC_READ(pagemap_fd, soft_vdb_buf, count);
        if (res > (ssize_t)ofs)
          break;
        if (res <= 0)
          ABORT_ARG1("Failed to read /proc/self/pagemap",
                     ": errno= %d", res < 0 ? errno : 0);
        /* Retry (once) w/o page-alignment.     */
        aligned_pos = fpos;
      }

      /* Save the buffer (file window) position and size.       */
      pagemap_buf_fpos = aligned_pos;
      pagemap_buf_len = (size_t)res;
      res -= (ssize_t)ofs;
    }

    GC_ASSERT(ofs % sizeof(pagemap_elem_t) == 0);
    *pres = (size_t)res < len ? (size_t)res : len;
    return &soft_vdb_buf[ofs / sizeof(pagemap_elem_t)];
  }

  static void soft_set_grungy_pages(ptr_t start, ptr_t limit,
                                    ptr_t next_start_hint)
  {
    word vaddr = (word)start & ~(word)(GC_page_size-1);
    off_t next_fpos_hint;

    GC_ASSERT(modHBLKSZ((word)start) == 0);
    GC_ASSERT(GC_page_size != 0);
    next_fpos_hint = (off_t)((word)next_start_hint / GC_page_size
                                * sizeof(pagemap_elem_t));
    while (vaddr < (word)limit) {
      size_t res;
      word limit_buf;
      const pagemap_elem_t *bufp = pagemap_buffered_read(&res,
                (off_t)(vaddr / GC_page_size * sizeof(pagemap_elem_t)),
                (size_t)(((word)limit - vaddr + GC_page_size-1) / GC_page_size
                         * sizeof(pagemap_elem_t)),
                next_fpos_hint);

      if (res % sizeof(pagemap_elem_t) != 0) {
        /* Punt: */
        memset(GC_grungy_pages, 0xff, sizeof(page_hash_table));
        WARN("Incomplete read of pagemap, not multiple of entry size\n", 0);
        break;
      }

      limit_buf = vaddr + (res / sizeof(pagemap_elem_t)) * GC_page_size;
      for (; vaddr < limit_buf; vaddr += GC_page_size, bufp++)
        if ((*bufp & PM_SOFTDIRTY_MASK) != 0) {
          struct hblk * h;
          word next_vaddr = vaddr + GC_page_size;

          if (EXPECT(next_vaddr > (word)limit, FALSE))
            next_vaddr = (word)limit;
          /* If the bit is set, the respective PTE was written to       */
          /* since clearing the soft-dirty bits.                        */
#         ifdef DEBUG_DIRTY_BITS
            GC_log_printf("dirty page at: %p\n", (void *)vaddr);
#         endif
          h = (struct hblk *)vaddr;
          if (EXPECT(vaddr < (word)start, FALSE))
            h = (struct hblk *)start;
          for (; (word)h < next_vaddr; h++) {
            word index = PHT_HASH(h);
            set_pht_entry_from_index(GC_grungy_pages, index);
          }
        }
      /* Read the next portion of pagemap file if incomplete.   */
    }
  }

  GC_INLINE void GC_soft_read_dirty(GC_bool output_unneeded)
  {
#   ifndef THREADS
      /* Similar as for GC_proc_read_dirty.     */
      if (getpid() != saved_proc_pid
          && (-1 == clear_refs_fd /* no need to retry */
              || (close(clear_refs_fd), close(pagemap_fd),
                  !soft_dirty_open_files()))) {
        /* Failed to reopen the files.  */
        if (!output_unneeded) {
          /* Punt: */
          memset(GC_grungy_pages, 0xff, sizeof(page_hash_table));
#         ifdef CHECKSUMS
            memset(GC_written_pages, 0xff, sizeof(page_hash_table));
#         endif
        }
        return;
      }
#   endif

    if (!output_unneeded) {
      word i;

      BZERO(GC_grungy_pages, sizeof(GC_grungy_pages));
      pagemap_buf_len = 0; /* invalidate soft_vdb_buf */

      for (i = 0; i != GC_n_heap_sects; ++i) {
        ptr_t start = GC_heap_sects[i].hs_start;

        soft_set_grungy_pages(start, start + GC_heap_sects[i].hs_bytes,
                              i < GC_n_heap_sects-1 ?
                                    GC_heap_sects[i+1].hs_start : NULL);
      }
#     ifdef CHECKSUMS
        GC_or_pages(GC_written_pages, GC_grungy_pages);
#     endif

#     ifndef NO_VDB_FOR_STATIC_ROOTS
        for (i = 0; (int)i < n_root_sets; ++i) {
          soft_set_grungy_pages((ptr_t)HBLKPTR(GC_static_roots[i].r_start),
                                GC_static_roots[i].r_end,
                                (int)i < n_root_sets-1 ?
                                    GC_static_roots[i+1].r_start : NULL);
        }
#     endif
    }

    clear_soft_dirty_bits();
  }
#endif /* SOFT_VDB */

#ifdef PCR_VDB

# include "vd/PCR_VD.h"

# define NPAGES (32*1024)       /* 128 MB */

PCR_VD_DB GC_grungy_bits[NPAGES];

STATIC ptr_t GC_vd_base = NULL;
                        /* Address corresponding to GC_grungy_bits[0]   */
                        /* HBLKSIZE aligned.                            */

GC_INNER GC_bool GC_dirty_init(void)
{
    /* For the time being, we assume the heap generally grows up */
    GC_vd_base = GC_heap_sects[0].hs_start;
    if (GC_vd_base == 0) {
        ABORT("Bad initial heap segment");
    }
    if (PCR_VD_Start(HBLKSIZE, GC_vd_base, NPAGES*HBLKSIZE)
        != PCR_ERes_okay) {
        ABORT("Dirty bit initialization failed");
    }
    return TRUE;
}
#endif /* PCR_VDB */

#ifndef GC_DISABLE_INCREMENTAL
  GC_INNER GC_bool GC_manual_vdb = FALSE;

  /* Manually mark the page containing p as dirty.  Logically, this     */
  /* dirties the entire object.                                         */
  GC_INNER void GC_dirty_inner(const void *p)
  {
    word index = PHT_HASH(p);

#   if defined(MPROTECT_VDB)
      /* Do not update GC_dirty_pages if it should be followed by the   */
      /* page unprotection.                                             */
      GC_ASSERT(GC_manual_vdb);
#   endif
    async_set_pht_entry_from_index(GC_dirty_pages, index);
  }

  /* Retrieve system dirty bits for the heap to a local buffer (unless  */
  /* output_unneeded).  Restore the systems notion of which pages are   */
  /* dirty.  We assume that either the world is stopped or it is OK to  */
  /* lose dirty bits while it's happening (as in GC_enable_incremental).*/
  GC_INNER void GC_read_dirty(GC_bool output_unneeded)
  {
    if (GC_manual_vdb
#       if defined(MPROTECT_VDB)
          || !GC_GWW_AVAILABLE()
#       endif
        ) {
      if (!output_unneeded)
        BCOPY((/* no volatile */ void *)GC_dirty_pages, GC_grungy_pages,
              sizeof(GC_dirty_pages));
      BZERO((/* no volatile */ void *)GC_dirty_pages,
            sizeof(GC_dirty_pages));
#     ifdef MPROTECT_VDB
        if (!GC_manual_vdb)
          GC_protect_heap();
#     endif
      return;
    }

#   ifdef GWW_VDB
      GC_gww_read_dirty(output_unneeded);
#   elif defined(PROC_VDB)
      GC_proc_read_dirty(output_unneeded);
#   elif defined(SOFT_VDB)
      GC_soft_read_dirty(output_unneeded);
#   elif defined(PCR_VDB)
      /* lazily enable dirty bits on newly added heap sects */
      {
        static int onhs = 0;
        int nhs = GC_n_heap_sects;
        for (; onhs < nhs; onhs++) {
            PCR_VD_WriteProtectEnable(
                    GC_heap_sects[onhs].hs_start,
                    GC_heap_sects[onhs].hs_bytes);
        }
      }
      if (PCR_VD_Clear(GC_vd_base, NPAGES*HBLKSIZE, GC_grungy_bits)
          != PCR_ERes_okay) {
        ABORT("Dirty bit read failed");
      }
#   endif
  }

# if !defined(NO_VDB_FOR_STATIC_ROOTS) && !defined(PROC_VDB)
    GC_INNER GC_bool GC_is_vdb_for_static_roots(void)
    {
      if (GC_manual_vdb) return FALSE;
#     if defined(MPROTECT_VDB)
        /* Currently used only in conjunction with SOFT_VDB.    */
        return GC_GWW_AVAILABLE();
#     else
        GC_ASSERT(GC_incremental);
        return TRUE;
#     endif
    }
# endif

  /* Is the HBLKSIZE sized page at h marked dirty in the local buffer?  */
  /* If the actual page size is different, this returns TRUE if any     */
  /* of the pages overlapping h are dirty.  This routine may err on the */
  /* side of labeling pages as dirty (and this implementation does).    */
  GC_INNER GC_bool GC_page_was_dirty(struct hblk *h)
  {
    word index;

#   ifdef PCR_VDB
      if (!GC_manual_vdb) {
        if ((word)h < (word)GC_vd_base
            || (word)h >= (word)(GC_vd_base + NPAGES * HBLKSIZE)) {
          return TRUE;
        }
        return GC_grungy_bits[h-(struct hblk*)GC_vd_base] & PCR_VD_DB_dirtyBit;
      }
#   elif defined(DEFAULT_VDB)
      if (!GC_manual_vdb)
        return TRUE;
#   elif defined(PROC_VDB)
      /* Unless manual VDB is on, the bitmap covers all process memory. */
      if (GC_manual_vdb)
#   endif
      {
        if (NULL == HDR(h))
          return TRUE;
      }
    index = PHT_HASH(h);
    return get_pht_entry_from_index(GC_grungy_pages, index);
  }

# if defined(CHECKSUMS) || defined(PROC_VDB)
    /* Could any valid GC heap pointer ever have been written to this page? */
    GC_INNER GC_bool GC_page_was_ever_dirty(struct hblk *h)
    {
#     if defined(GWW_VDB) || defined(PROC_VDB) || defined(SOFT_VDB)
        word index;

#       ifdef MPROTECT_VDB
          if (!GC_GWW_AVAILABLE())
            return TRUE;
#       endif
#       if defined(PROC_VDB)
          if (GC_manual_vdb)
#       endif
        {
          if (NULL == HDR(h))
            return TRUE;
        }
        index = PHT_HASH(h);
        return get_pht_entry_from_index(GC_written_pages, index);
#     else
        /* TODO: implement me for MANUAL_VDB. */
        (void)h;
        return TRUE;
#     endif
    }
# endif /* CHECKSUMS || PROC_VDB */

  /* We expect block h to be written shortly.  Ensure that all pages    */
  /* containing any part of the n hblks starting at h are no longer     */
  /* protected.  If is_ptrfree is false, also ensure that they will     */
  /* subsequently appear to be dirty.  Not allowed to call GC_printf    */
  /* (and the friends) here, see Win32 GC_stop_world for the details.   */
  GC_INNER void GC_remove_protection(struct hblk *h, word nblocks,
                                     GC_bool is_ptrfree)
  {
#   ifdef PCR_VDB
      (void)is_ptrfree;
      if (!GC_auto_incremental)
        return;
      PCR_VD_WriteProtectDisable(h, nblocks*HBLKSIZE);
      PCR_VD_WriteProtectEnable(h, nblocks*HBLKSIZE);
#   elif defined(MPROTECT_VDB)
      struct hblk * h_trunc;    /* Truncated to page boundary */
      struct hblk * h_end;      /* Page boundary following block end */
      struct hblk * current;

      if (!GC_auto_incremental || GC_GWW_AVAILABLE())
        return;
      GC_ASSERT(GC_page_size != 0);
      h_trunc = (struct hblk *)((word)h & ~(word)(GC_page_size-1));
      h_end = (struct hblk *)(((word)(h + nblocks) + GC_page_size - 1)
                              & ~(word)(GC_page_size - 1));
      /* Note that we cannot examine GC_dirty_pages to check    */
      /* whether the page at h_trunc has already been marked    */
      /* dirty as there could be a hash collision.              */
      for (current = h_trunc; (word)current < (word)h_end; ++current) {
        word index = PHT_HASH(current);

        if (!is_ptrfree || (word)current < (word)h
            || (word)current >= (word)(h + nblocks)) {
          async_set_pht_entry_from_index(GC_dirty_pages, index);
        }
      }
      UNPROTECT(h_trunc, (ptr_t)h_end - (ptr_t)h_trunc);
#   else
      /* Ignore write hints.  They don't help us here.  */
      (void)h; (void)nblocks; (void)is_ptrfree;
#   endif
  }
#endif /* !GC_DISABLE_INCREMENTAL */

#if defined(MPROTECT_VDB) && defined(DARWIN)
/* The following sources were used as a "reference" for this exception
   handling code:
      1. Apple's mach/xnu documentation
      2. Timothy J. Wood's "Mach Exception Handlers 101" post to the
         omnigroup's macosx-dev list.
         www.omnigroup.com/mailman/archive/macosx-dev/2000-June/014178.html
      3. macosx-nat.c from Apple's GDB source code.
*/

/* The bug that caused all this trouble should now be fixed. This should
   eventually be removed if all goes well. */

/* #define BROKEN_EXCEPTION_HANDLING */

#include <mach/mach.h>
#include <mach/mach_error.h>
#include <mach/exception.h>
#include <mach/task.h>

EXTERN_C_BEGIN

/* Some of the following prototypes are missing in any header, although */
/* they are documented.  Some are in mach/exc.h file.                   */
extern boolean_t
exc_server(mach_msg_header_t *, mach_msg_header_t *);

extern kern_return_t
exception_raise(mach_port_t, mach_port_t, mach_port_t, exception_type_t,
                exception_data_t, mach_msg_type_number_t);

extern kern_return_t
exception_raise_state(mach_port_t, mach_port_t, mach_port_t, exception_type_t,
                      exception_data_t, mach_msg_type_number_t,
                      thread_state_flavor_t*, thread_state_t,
                      mach_msg_type_number_t, thread_state_t,
                      mach_msg_type_number_t*);

extern kern_return_t
exception_raise_state_identity(mach_port_t, mach_port_t, mach_port_t,
                               exception_type_t, exception_data_t,
                               mach_msg_type_number_t, thread_state_flavor_t*,
                               thread_state_t, mach_msg_type_number_t,
                               thread_state_t, mach_msg_type_number_t*);

GC_API_OSCALL kern_return_t
catch_exception_raise(mach_port_t exception_port, mach_port_t thread,
                      mach_port_t task, exception_type_t exception,
                      exception_data_t code,
                      mach_msg_type_number_t code_count);

GC_API_OSCALL kern_return_t
catch_exception_raise_state(mach_port_name_t exception_port,
                int exception, exception_data_t code,
                mach_msg_type_number_t codeCnt, int flavor,
                thread_state_t old_state, int old_stateCnt,
                thread_state_t new_state, int new_stateCnt);

GC_API_OSCALL kern_return_t
catch_exception_raise_state_identity(mach_port_name_t exception_port,
                mach_port_t thread, mach_port_t task, int exception,
                exception_data_t code, mach_msg_type_number_t codeCnt,
                int flavor, thread_state_t old_state, int old_stateCnt,
                thread_state_t new_state, int new_stateCnt);

EXTERN_C_END

/* These should never be called, but just in case...  */
GC_API_OSCALL kern_return_t
catch_exception_raise_state(mach_port_name_t exception_port GC_ATTR_UNUSED,
    int exception GC_ATTR_UNUSED, exception_data_t code GC_ATTR_UNUSED,
    mach_msg_type_number_t codeCnt GC_ATTR_UNUSED, int flavor GC_ATTR_UNUSED,
    thread_state_t old_state GC_ATTR_UNUSED, int old_stateCnt GC_ATTR_UNUSED,
    thread_state_t new_state GC_ATTR_UNUSED, int new_stateCnt GC_ATTR_UNUSED)
{
  ABORT_RET("Unexpected catch_exception_raise_state invocation");
  return(KERN_INVALID_ARGUMENT);
}

GC_API_OSCALL kern_return_t
catch_exception_raise_state_identity(
    mach_port_name_t exception_port GC_ATTR_UNUSED,
    mach_port_t thread GC_ATTR_UNUSED, mach_port_t task GC_ATTR_UNUSED,
    int exception GC_ATTR_UNUSED, exception_data_t code GC_ATTR_UNUSED,
    mach_msg_type_number_t codeCnt GC_ATTR_UNUSED, int flavor GC_ATTR_UNUSED,
    thread_state_t old_state GC_ATTR_UNUSED, int old_stateCnt GC_ATTR_UNUSED,
    thread_state_t new_state GC_ATTR_UNUSED, int new_stateCnt GC_ATTR_UNUSED)
{
  ABORT_RET("Unexpected catch_exception_raise_state_identity invocation");
  return(KERN_INVALID_ARGUMENT);
}

#define MAX_EXCEPTION_PORTS 16

static struct {
  mach_msg_type_number_t count;
  exception_mask_t      masks[MAX_EXCEPTION_PORTS];
  exception_handler_t   ports[MAX_EXCEPTION_PORTS];
  exception_behavior_t  behaviors[MAX_EXCEPTION_PORTS];
  thread_state_flavor_t flavors[MAX_EXCEPTION_PORTS];
} GC_old_exc_ports;

STATIC struct ports_s {
  void (*volatile os_callback[3])(void);
  mach_port_t exception;
# if defined(THREADS)
    mach_port_t reply;
# endif
} GC_ports = {
  {
    /* This is to prevent stripping these routines as dead.     */
    (void (*)(void))catch_exception_raise,
    (void (*)(void))catch_exception_raise_state,
    (void (*)(void))catch_exception_raise_state_identity
  },
# ifdef THREADS
    0, /* for 'exception' */
# endif
  0
};

typedef struct {
    mach_msg_header_t head;
} GC_msg_t;

typedef enum {
    GC_MP_NORMAL,
    GC_MP_DISCARDING,
    GC_MP_STOPPED
} GC_mprotect_state_t;

#ifdef THREADS
  /* FIXME: 1 and 2 seem to be safe to use in the msgh_id field, but it */
  /* is not documented.  Use the source and see if they should be OK.   */
# define ID_STOP 1
# define ID_RESUME 2

  /* This value is only used on the reply port. */
# define ID_ACK 3

  STATIC GC_mprotect_state_t GC_mprotect_state = GC_MP_NORMAL;

  /* The following should ONLY be called when the world is stopped.     */
  STATIC void GC_mprotect_thread_notify(mach_msg_id_t id)
  {
    struct buf_s {
      GC_msg_t msg;
      mach_msg_trailer_t trailer;
    } buf;
    mach_msg_return_t r;

    /* remote, local */
    buf.msg.head.msgh_bits = MACH_MSGH_BITS(MACH_MSG_TYPE_MAKE_SEND, 0);
    buf.msg.head.msgh_size = sizeof(buf.msg);
    buf.msg.head.msgh_remote_port = GC_ports.exception;
    buf.msg.head.msgh_local_port = MACH_PORT_NULL;
    buf.msg.head.msgh_id = id;

    r = mach_msg(&buf.msg.head, MACH_SEND_MSG | MACH_RCV_MSG | MACH_RCV_LARGE,
                 sizeof(buf.msg), sizeof(buf), GC_ports.reply,
                 MACH_MSG_TIMEOUT_NONE, MACH_PORT_NULL);
    if (r != MACH_MSG_SUCCESS)
      ABORT("mach_msg failed in GC_mprotect_thread_notify");
    if (buf.msg.head.msgh_id != ID_ACK)
      ABORT("Invalid ack in GC_mprotect_thread_notify");
  }

  /* Should only be called by the mprotect thread */
  STATIC void GC_mprotect_thread_reply(void)
  {
    GC_msg_t msg;
    mach_msg_return_t r;
    /* remote, local */

    msg.head.msgh_bits = MACH_MSGH_BITS(MACH_MSG_TYPE_MAKE_SEND, 0);
    msg.head.msgh_size = sizeof(msg);
    msg.head.msgh_remote_port = GC_ports.reply;
    msg.head.msgh_local_port = MACH_PORT_NULL;
    msg.head.msgh_id = ID_ACK;

    r = mach_msg(&msg.head, MACH_SEND_MSG, sizeof(msg), 0, MACH_PORT_NULL,
                 MACH_MSG_TIMEOUT_NONE, MACH_PORT_NULL);
    if (r != MACH_MSG_SUCCESS)
      ABORT("mach_msg failed in GC_mprotect_thread_reply");
  }

  GC_INNER void GC_mprotect_stop(void)
  {
    GC_mprotect_thread_notify(ID_STOP);
  }

  GC_INNER void GC_mprotect_resume(void)
  {
    GC_mprotect_thread_notify(ID_RESUME);
  }

# ifdef CAN_HANDLE_FORK
    GC_INNER void GC_dirty_update_child(void)
    {
      GC_ASSERT(I_HOLD_LOCK());
      if (0 == GC_task_self) return; /* GC incremental mode is off */

      GC_ASSERT(GC_mprotect_state == GC_MP_NORMAL);
      GC_task_self = mach_task_self(); /* needed by UNPROTECT() */
      GC_unprotect_all_heap();

      /* Restore the old task exception ports.  */
      /* TODO: Should we do it in fork_prepare/parent_proc? */
      if (GC_old_exc_ports.count > 0) {
        /* TODO: Should we check GC_old_exc_ports.count<=1? */
        if (task_set_exception_ports(GC_task_self, GC_old_exc_ports.masks[0],
                    GC_old_exc_ports.ports[0], GC_old_exc_ports.behaviors[0],
                    GC_old_exc_ports.flavors[0]) != KERN_SUCCESS)
          ABORT("task_set_exception_ports failed (in child)");
      }

      /* TODO: Re-enable incremental mode in child. */
      GC_task_self = 0;
      GC_incremental = FALSE;
    }
# endif /* CAN_HANDLE_FORK */

#else
  /* The compiler should optimize away any GC_mprotect_state computations */
# define GC_mprotect_state GC_MP_NORMAL
#endif /* !THREADS */

struct mp_reply_s {
  mach_msg_header_t head;
  char data[256];
};

struct mp_msg_s {
  mach_msg_header_t head;
  mach_msg_body_t msgh_body;
  char data[1024];
};

STATIC void *GC_mprotect_thread(void *arg)
{
  mach_msg_return_t r;
  /* These two structures contain some private kernel data.  We don't   */
  /* need to access any of it so we don't bother defining a proper      */
  /* struct.  The correct definitions are in the xnu source code.       */
  struct mp_reply_s reply;
  struct mp_msg_s msg;
  mach_msg_id_t id;

  if ((word)arg == GC_WORD_MAX) return 0; /* to prevent a compiler warning */
# if defined(CPPCHECK)
    reply.data[0] = 0; /* to prevent "field unused" warnings */
    msg.data[0] = 0;
# endif

# if defined(HAVE_PTHREAD_SETNAME_NP_WITHOUT_TID)
    (void)pthread_setname_np("GC-mprotect");
# endif
# if defined(THREADS) && !defined(GC_NO_THREADS_DISCOVERY)
    GC_darwin_register_mach_handler_thread(mach_thread_self());
# endif

  for (;;) {
    r = mach_msg(&msg.head, MACH_RCV_MSG | MACH_RCV_LARGE |
                 (GC_mprotect_state == GC_MP_DISCARDING ? MACH_RCV_TIMEOUT
                  : 0), 0, sizeof(msg), GC_ports.exception,
                 GC_mprotect_state == GC_MP_DISCARDING ? 0
                 : MACH_MSG_TIMEOUT_NONE, MACH_PORT_NULL);
    id = r == MACH_MSG_SUCCESS ? msg.head.msgh_id : -1;

#   if defined(THREADS)
      if (GC_mprotect_state == GC_MP_DISCARDING) {
        if (r == MACH_RCV_TIMED_OUT) {
          GC_mprotect_state = GC_MP_STOPPED;
          GC_mprotect_thread_reply();
          continue;
        }
        if (r == MACH_MSG_SUCCESS && (id == ID_STOP || id == ID_RESUME))
          ABORT("Out of order mprotect thread request");
      }
#   endif /* THREADS */

    if (r != MACH_MSG_SUCCESS) {
      ABORT_ARG2("mach_msg failed",
                 ": errcode= %d (%s)", (int)r, mach_error_string(r));
    }

    switch (id) {
#     if defined(THREADS)
        case ID_STOP:
          if (GC_mprotect_state != GC_MP_NORMAL)
            ABORT("Called mprotect_stop when state wasn't normal");
          GC_mprotect_state = GC_MP_DISCARDING;
          break;
        case ID_RESUME:
          if (GC_mprotect_state != GC_MP_STOPPED)
            ABORT("Called mprotect_resume when state wasn't stopped");
          GC_mprotect_state = GC_MP_NORMAL;
          GC_mprotect_thread_reply();
          break;
#     endif /* THREADS */
        default:
          /* Handle the message (calls catch_exception_raise) */
          if (!exc_server(&msg.head, &reply.head))
            ABORT("exc_server failed");
          /* Send the reply */
          r = mach_msg(&reply.head, MACH_SEND_MSG, reply.head.msgh_size, 0,
                       MACH_PORT_NULL, MACH_MSG_TIMEOUT_NONE,
                       MACH_PORT_NULL);
          if (r != MACH_MSG_SUCCESS) {
            /* This will fail if the thread dies, but the thread */
            /* shouldn't die... */
#           ifdef BROKEN_EXCEPTION_HANDLING
              GC_err_printf("mach_msg failed with %d %s while sending "
                            "exc reply\n", (int)r, mach_error_string(r));
#           else
              ABORT("mach_msg failed while sending exception reply");
#           endif
          }
    } /* switch */
  } /* for */
}

/* All this SIGBUS code shouldn't be necessary. All protection faults should
   be going through the mach exception handler. However, it seems a SIGBUS is
   occasionally sent for some unknown reason. Even more odd, it seems to be
   meaningless and safe to ignore. */
#ifdef BROKEN_EXCEPTION_HANDLING

  /* Updates to this aren't atomic, but the SIGBUS'es seem pretty rare.    */
  /* Even if this doesn't get updated property, it isn't really a problem. */
  STATIC int GC_sigbus_count = 0;

  STATIC void GC_darwin_sigbus(int num, siginfo_t *sip GC_ATTR_UNUSED,
                               void *context GC_ATTR_UNUSED)
  {
    if (num != SIGBUS)
      ABORT("Got a non-sigbus signal in the sigbus handler");

    /* Ugh... some seem safe to ignore, but too many in a row probably means
       trouble. GC_sigbus_count is reset for each mach exception that is
       handled */
    if (GC_sigbus_count >= 8) {
      ABORT("Got more than 8 SIGBUSs in a row!");
    } else {
      GC_sigbus_count++;
      WARN("Ignoring SIGBUS\n", 0);
    }
  }
#endif /* BROKEN_EXCEPTION_HANDLING */

GC_INNER GC_bool GC_dirty_init(void)
{
  kern_return_t r;
  mach_port_t me;
  pthread_t thread;
  pthread_attr_t attr;
  exception_mask_t mask;

# if defined(CAN_HANDLE_FORK) && !defined(THREADS)
    if (GC_handle_fork) {
      /* To both support GC incremental mode and GC functions usage in  */
      /* the forked child, pthread_atfork should be used to install     */
      /* handlers that switch off GC_incremental in the child           */
      /* gracefully (unprotecting all pages and clearing                */
      /* GC_mach_handler_thread).  For now, we just disable incremental */
      /* mode if fork() handling is requested by the client.            */
      WARN("Can't turn on GC incremental mode as fork()"
           " handling requested\n", 0);
      return FALSE;
    }
# endif

  GC_VERBOSE_LOG_PRINTF("Initializing mach/darwin mprotect"
                        " virtual dirty bit implementation\n");
# ifdef BROKEN_EXCEPTION_HANDLING
    WARN("Enabling workarounds for various darwin "
         "exception handling bugs\n", 0);
# endif
  if (GC_page_size % HBLKSIZE != 0) {
    ABORT("Page size not multiple of HBLKSIZE");
  }

  GC_task_self = me = mach_task_self();
  GC_ASSERT(me != 0);

  r = mach_port_allocate(me, MACH_PORT_RIGHT_RECEIVE, &GC_ports.exception);
  /* TODO: WARN and return FALSE in case of a failure. */
  if (r != KERN_SUCCESS)
    ABORT("mach_port_allocate failed (exception port)");

  r = mach_port_insert_right(me, GC_ports.exception, GC_ports.exception,
                             MACH_MSG_TYPE_MAKE_SEND);
  if (r != KERN_SUCCESS)
    ABORT("mach_port_insert_right failed (exception port)");

#  if defined(THREADS)
     r = mach_port_allocate(me, MACH_PORT_RIGHT_RECEIVE, &GC_ports.reply);
     if(r != KERN_SUCCESS)
       ABORT("mach_port_allocate failed (reply port)");
#  endif

  /* The exceptions we want to catch. */
  mask = EXC_MASK_BAD_ACCESS;
  r = task_get_exception_ports(me, mask, GC_old_exc_ports.masks,
                               &GC_old_exc_ports.count, GC_old_exc_ports.ports,
                               GC_old_exc_ports.behaviors,
                               GC_old_exc_ports.flavors);
  if (r != KERN_SUCCESS)
    ABORT("task_get_exception_ports failed");

  r = task_set_exception_ports(me, mask, GC_ports.exception, EXCEPTION_DEFAULT,
                               GC_MACH_THREAD_STATE);
  if (r != KERN_SUCCESS)
    ABORT("task_set_exception_ports failed");

  if (pthread_attr_init(&attr) != 0)
    ABORT("pthread_attr_init failed");
  if (pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED) != 0)
    ABORT("pthread_attr_setdetachedstate failed");
  /* This will call the real pthread function, not our wrapper. */
  if (GC_inner_pthread_create(&thread, &attr, GC_mprotect_thread, NULL) != 0)
    ABORT("pthread_create failed");
  (void)pthread_attr_destroy(&attr);

  /* Setup the sigbus handler for ignoring the meaningless SIGBUSs */
# ifdef BROKEN_EXCEPTION_HANDLING
    {
      struct sigaction sa, oldsa;
      sa.sa_handler = (SIG_HNDLR_PTR)GC_darwin_sigbus;
      sigemptyset(&sa.sa_mask);
      sa.sa_flags = SA_RESTART|SA_SIGINFO;
      /* sa.sa_restorer is deprecated and should not be initialized. */
      if (sigaction(SIGBUS, &sa, &oldsa) < 0)
        ABORT("sigaction failed");
      if (oldsa.sa_handler != (SIG_HNDLR_PTR)(signed_word)SIG_DFL) {
        GC_VERBOSE_LOG_PRINTF("Replaced other SIGBUS handler\n");
      }
    }
# endif /* BROKEN_EXCEPTION_HANDLING  */
# if defined(CPPCHECK)
    GC_noop1((word)GC_ports.os_callback[0]);
# endif
  return TRUE;
}

/* The source code for Apple's GDB was used as a reference for the      */
/* exception forwarding code.  This code is similar to be GDB code only */
/* because there is only one way to do it.                              */
STATIC kern_return_t GC_forward_exception(mach_port_t thread, mach_port_t task,
                                          exception_type_t exception,
                                          exception_data_t data,
                                          mach_msg_type_number_t data_count)
{
  unsigned int i;
  kern_return_t r;
  mach_port_t port;
  exception_behavior_t behavior;
  thread_state_flavor_t flavor;

  thread_state_data_t thread_state;
  mach_msg_type_number_t thread_state_count = THREAD_STATE_MAX;

  for (i = 0; i < GC_old_exc_ports.count; i++) {
    if ((GC_old_exc_ports.masks[i] & ((exception_mask_t)1 << exception)) != 0)
      break;
  }
  if (i == GC_old_exc_ports.count)
    ABORT("No handler for exception!");

  port = GC_old_exc_ports.ports[i];
  behavior = GC_old_exc_ports.behaviors[i];
  flavor = GC_old_exc_ports.flavors[i];

  if (behavior == EXCEPTION_STATE || behavior == EXCEPTION_STATE_IDENTITY) {
    r = thread_get_state(thread, flavor, thread_state, &thread_state_count);
    if (r != KERN_SUCCESS)
      ABORT("thread_get_state failed in forward_exception");
  }

  switch (behavior) {
    case EXCEPTION_STATE:
      r = exception_raise_state(port, thread, task, exception, data,
                                data_count, &flavor, thread_state,
                                thread_state_count, thread_state,
                                &thread_state_count);
      break;
    case EXCEPTION_STATE_IDENTITY:
      r = exception_raise_state_identity(port, thread, task, exception, data,
                                         data_count, &flavor, thread_state,
                                         thread_state_count, thread_state,
                                         &thread_state_count);
      break;
    /* case EXCEPTION_DEFAULT: */ /* default signal handlers */
    default: /* user-supplied signal handlers */
      r = exception_raise(port, thread, task, exception, data, data_count);
  }

  if (behavior == EXCEPTION_STATE || behavior == EXCEPTION_STATE_IDENTITY) {
    r = thread_set_state(thread, flavor, thread_state, thread_state_count);
    if (r != KERN_SUCCESS)
      ABORT("thread_set_state failed in forward_exception");
  }
  return r;
}

#define FWD() GC_forward_exception(thread, task, exception, code, code_count)

#ifdef ARM32
# define DARWIN_EXC_STATE         ARM_EXCEPTION_STATE
# define DARWIN_EXC_STATE_COUNT   ARM_EXCEPTION_STATE_COUNT
# define DARWIN_EXC_STATE_T       arm_exception_state_t
# define DARWIN_EXC_STATE_DAR     THREAD_FLD_NAME(far)
#elif defined(AARCH64)
# define DARWIN_EXC_STATE         ARM_EXCEPTION_STATE64
# define DARWIN_EXC_STATE_COUNT   ARM_EXCEPTION_STATE64_COUNT
# define DARWIN_EXC_STATE_T       arm_exception_state64_t
# define DARWIN_EXC_STATE_DAR     THREAD_FLD_NAME(far)
#elif defined(POWERPC)
# if CPP_WORDSZ == 32
#   define DARWIN_EXC_STATE       PPC_EXCEPTION_STATE
#   define DARWIN_EXC_STATE_COUNT PPC_EXCEPTION_STATE_COUNT
#   define DARWIN_EXC_STATE_T     ppc_exception_state_t
# else
#   define DARWIN_EXC_STATE       PPC_EXCEPTION_STATE64
#   define DARWIN_EXC_STATE_COUNT PPC_EXCEPTION_STATE64_COUNT
#   define DARWIN_EXC_STATE_T     ppc_exception_state64_t
# endif
# define DARWIN_EXC_STATE_DAR     THREAD_FLD_NAME(dar)
#elif defined(I386) || defined(X86_64)
# if CPP_WORDSZ == 32
#   if defined(i386_EXCEPTION_STATE_COUNT) \
       && !defined(x86_EXCEPTION_STATE32_COUNT)
      /* Use old naming convention for 32-bit x86.      */
#     define DARWIN_EXC_STATE           i386_EXCEPTION_STATE
#     define DARWIN_EXC_STATE_COUNT     i386_EXCEPTION_STATE_COUNT
#     define DARWIN_EXC_STATE_T         i386_exception_state_t
#   else
#     define DARWIN_EXC_STATE           x86_EXCEPTION_STATE32
#     define DARWIN_EXC_STATE_COUNT     x86_EXCEPTION_STATE32_COUNT
#     define DARWIN_EXC_STATE_T         x86_exception_state32_t
#   endif
# else
#   define DARWIN_EXC_STATE       x86_EXCEPTION_STATE64
#   define DARWIN_EXC_STATE_COUNT x86_EXCEPTION_STATE64_COUNT
#   define DARWIN_EXC_STATE_T     x86_exception_state64_t
# endif
# define DARWIN_EXC_STATE_DAR     THREAD_FLD_NAME(faultvaddr)
#elif !defined(CPPCHECK)
# error FIXME for non-arm/ppc/x86 darwin
#endif

/* This violates the namespace rules but there isn't anything that can  */
/* be done about it.  The exception handling stuff is hard coded to     */
/* call this.  catch_exception_raise, catch_exception_raise_state and   */
/* catch_exception_raise_state_identity are called from OS.             */
GC_API_OSCALL kern_return_t
catch_exception_raise(mach_port_t exception_port GC_ATTR_UNUSED,
                      mach_port_t thread, mach_port_t task GC_ATTR_UNUSED,
                      exception_type_t exception, exception_data_t code,
                      mach_msg_type_number_t code_count GC_ATTR_UNUSED)
{
  kern_return_t r;
  char *addr;
  thread_state_flavor_t flavor = DARWIN_EXC_STATE;
  mach_msg_type_number_t exc_state_count = DARWIN_EXC_STATE_COUNT;
  DARWIN_EXC_STATE_T exc_state;

  if (exception != EXC_BAD_ACCESS || code[0] != KERN_PROTECTION_FAILURE) {
#   ifdef DEBUG_EXCEPTION_HANDLING
      /* We aren't interested, pass it on to the old handler */
      GC_log_printf("Exception: 0x%x Code: 0x%x 0x%x in catch...\n",
                    exception, code_count > 0 ? code[0] : -1,
                    code_count > 1 ? code[1] : -1);
#   endif
    return FWD();
  }

  r = thread_get_state(thread, flavor, (natural_t*)&exc_state,
                       &exc_state_count);
  if (r != KERN_SUCCESS) {
    /* The thread is supposed to be suspended while the exception       */
    /* handler is called.  This shouldn't fail.                         */
#   ifdef BROKEN_EXCEPTION_HANDLING
      GC_err_printf("thread_get_state failed in catch_exception_raise\n");
      return KERN_SUCCESS;
#   else
      ABORT("thread_get_state failed in catch_exception_raise");
#   endif
  }

  /* This is the address that caused the fault */
  addr = (char*) exc_state.DARWIN_EXC_STATE_DAR;
  if (!is_header_found_async(addr)) {
    /* Ugh... just like the SIGBUS problem above, it seems we get       */
    /* a bogus KERN_PROTECTION_FAILURE every once and a while.  We wait */
    /* till we get a bunch in a row before doing anything about it.     */
    /* If a "real" fault ever occurs it'll just keep faulting over and  */
    /* over and we'll hit the limit pretty quickly.                     */
#   ifdef BROKEN_EXCEPTION_HANDLING
      static char *last_fault;
      static int last_fault_count;

      if (addr != last_fault) {
        last_fault = addr;
        last_fault_count = 0;
      }
      if (++last_fault_count < 32) {
        if (last_fault_count == 1)
          WARN("Ignoring KERN_PROTECTION_FAILURE at %p\n", addr);
        return KERN_SUCCESS;
      }

      GC_err_printf("Unexpected KERN_PROTECTION_FAILURE at %p; aborting...\n",
                    (void *)addr);
      /* Can't pass it along to the signal handler because that is      */
      /* ignoring SIGBUS signals.  We also shouldn't call ABORT here as */
      /* signals don't always work too well from the exception handler. */
      EXIT();
#   else /* BROKEN_EXCEPTION_HANDLING */
      /* Pass it along to the next exception handler
         (which should call SIGBUS/SIGSEGV) */
      return FWD();
#   endif /* !BROKEN_EXCEPTION_HANDLING */
  }

# ifdef BROKEN_EXCEPTION_HANDLING
    /* Reset the number of consecutive SIGBUSs */
    GC_sigbus_count = 0;
# endif

  GC_ASSERT(GC_page_size != 0);
  if (GC_mprotect_state == GC_MP_NORMAL) { /* common case */
    struct hblk * h = (struct hblk *)((word)addr & ~(word)(GC_page_size-1));
    size_t i;

#   ifdef CHECKSUMS
      GC_record_fault(h);
#   endif
    UNPROTECT(h, GC_page_size);
    for (i = 0; i < divHBLKSZ(GC_page_size); i++) {
      word index = PHT_HASH(h+i);
      async_set_pht_entry_from_index(GC_dirty_pages, index);
    }
  } else {
    /* Lie to the thread for now. No sense UNPROTECT()ing the memory
       when we're just going to PROTECT() it again later. The thread
       will just fault again once it resumes */
    /* Could happen (but rarely) even if the state is GC_MP_STOPPED. */
  }
  return KERN_SUCCESS;
}
#undef FWD

#ifndef NO_DESC_CATCH_EXCEPTION_RAISE
  /* These symbols should have REFERENCED_DYNAMICALLY (0x10) bit set to */
  /* let strip know they are not to be stripped.                        */
  __asm__(".desc _catch_exception_raise, 0x10");
  __asm__(".desc _catch_exception_raise_state, 0x10");
  __asm__(".desc _catch_exception_raise_state_identity, 0x10");
#endif

#endif /* DARWIN && MPROTECT_VDB */

#ifndef HAVE_INCREMENTAL_PROTECTION_NEEDS
  GC_API int GC_CALL GC_incremental_protection_needs(void)
  {
    GC_ASSERT(GC_is_initialized);
    return GC_PROTECTS_NONE;
  }
#endif /* !HAVE_INCREMENTAL_PROTECTION_NEEDS */

#ifdef ECOS
  /* Undo sbrk() redirection. */
# undef sbrk
#endif

/* If value is non-zero then allocate executable memory.        */
GC_API void GC_CALL GC_set_pages_executable(int value)
{
  GC_ASSERT(!GC_is_initialized);
  /* Even if IGNORE_PAGES_EXECUTABLE is defined, GC_pages_executable is */
  /* touched here to prevent a compiler warning.                        */
  GC_pages_executable = (GC_bool)(value != 0);
}

/* Returns non-zero if the GC-allocated memory is executable.   */
/* GC_get_pages_executable is defined after all the places      */
/* where GC_get_pages_executable is undefined.                  */
GC_API int GC_CALL GC_get_pages_executable(void)
{
# ifdef IGNORE_PAGES_EXECUTABLE
    return 1;   /* Always allocate executable memory. */
# else
    return (int)GC_pages_executable;
# endif
}

/* Call stack save code for debugging.  Should probably be in           */
/* mach_dep.c, but that requires reorganization.                        */

/* I suspect the following works for most *nix x86 variants, so         */
/* long as the frame pointer is explicitly stored.  In the case of gcc, */
/* compiler flags (e.g. -fomit-frame-pointer) determine whether it is.  */
#if defined(I386) && defined(LINUX) && defined(SAVE_CALL_CHAIN)
#   include <features.h>

    struct frame {
        struct frame *fr_savfp;
        long    fr_savpc;
#       if NARGS > 0
          long  fr_arg[NARGS];  /* All the arguments go here.   */
#       endif
    };
#endif

#if defined(SPARC)
#  if defined(LINUX)
#    include <features.h>

#   if defined(SAVE_CALL_CHAIN)
     struct frame {
        long    fr_local[8];
        long    fr_arg[6];
        struct frame *fr_savfp;
        long    fr_savpc;
#       ifndef __arch64__
          char  *fr_stret;
#       endif
        long    fr_argd[6];
        long    fr_argx[0];
     };
#   endif
#  elif defined (DRSNX)
#    include <sys/sparc/frame.h>
#  elif defined(OPENBSD)
#    include <frame.h>
#  elif defined(FREEBSD) || defined(NETBSD)
#    include <machine/frame.h>
#  else
#    include <sys/frame.h>
#  endif
#  if NARGS > 6
#    error We only know how to get the first 6 arguments
#  endif
#endif /* SPARC */

#ifdef NEED_CALLINFO
/* Fill in the pc and argument information for up to NFRAMES of my      */
/* callers.  Ignore my frame and my callers frame.                      */

#ifdef LINUX
#   include <unistd.h>
#endif

#endif /* NEED_CALLINFO */

#if defined(GC_HAVE_BUILTIN_BACKTRACE)
# ifdef _MSC_VER
#  include "private/msvc_dbg.h"
# else
#  include <execinfo.h>
# endif
#endif

#ifdef SAVE_CALL_CHAIN

#if NARGS == 0 && NFRAMES % 2 == 0 /* No padding */ \
    && defined(GC_HAVE_BUILTIN_BACKTRACE)

#ifdef REDIRECT_MALLOC
  /* Deal with possible malloc calls in backtrace by omitting   */
  /* the infinitely recursing backtrace.                        */
# ifdef THREADS
    __thread    /* If your compiler doesn't understand this             */
                /* you could use something like pthread_getspecific.    */
# endif
    GC_bool GC_in_save_callers = FALSE;

# if defined(THREADS) && defined(DBG_HDRS_ALL)
#   include "private/dbg_mlc.h"

    /* A dummy version of GC_save_callers() which does not call */
    /* backtrace().                                             */
    GC_INNER void GC_save_callers_no_unlock(struct callinfo info[NFRAMES])
    {
      GC_ASSERT(I_HOLD_LOCK());
      info[0].ci_pc = (word)(&GC_save_callers_no_unlock);
      BZERO(&info[1], sizeof(void *) * (NFRAMES - 1));
    }
# endif
#endif /* REDIRECT_MALLOC */

GC_INNER void GC_save_callers(struct callinfo info[NFRAMES])
{
  void * tmp_info[NFRAMES + 1];
  int npcs, i;

  GC_ASSERT(I_HOLD_LOCK());
                /* backtrace may call dl_iterate_phdr which is also     */
                /* used by GC_register_dynamic_libraries, and           */
                /* dl_iterate_phdr is not guaranteed to be reentrant.   */

  GC_STATIC_ASSERT(sizeof(struct callinfo) == sizeof(void *));
# ifdef REDIRECT_MALLOC
    if (GC_in_save_callers) {
      info[0].ci_pc = (word)(&GC_save_callers);
      for (i = 1; i < NFRAMES; ++i) info[i].ci_pc = 0;
      return;
    }
    GC_in_save_callers = TRUE;
    /* backtrace() might call a redirected malloc. */
    UNLOCK();
    npcs = backtrace((void **)tmp_info, NFRAMES + 1);
    LOCK();
# else
    npcs = backtrace((void **)tmp_info, NFRAMES + 1);
# endif
  /* We retrieve NFRAMES+1 pc values, but discard the first one, since  */
  /* it points to our own frame.                                        */
  i = 0;
  if (npcs > 1) {
    i = npcs - 1;
    BCOPY(&tmp_info[1], info, (unsigned)i * sizeof(void *));
  }
  for (; i < NFRAMES; ++i) info[i].ci_pc = 0;
# ifdef REDIRECT_MALLOC
    GC_in_save_callers = FALSE;
# endif
}

#else /* No builtin backtrace; do it ourselves */

#if (defined(OPENBSD) || defined(NETBSD) || defined(FREEBSD)) && defined(SPARC)
#  define FR_SAVFP fr_fp
#  define FR_SAVPC fr_pc
#else
#  define FR_SAVFP fr_savfp
#  define FR_SAVPC fr_savpc
#endif

#if defined(SPARC) && (defined(__arch64__) || defined(__sparcv9))
#   define BIAS 2047
#else
#   define BIAS 0
#endif

GC_INNER void GC_save_callers(struct callinfo info[NFRAMES])
{
  struct frame *frame;
  struct frame *fp;
  int nframes = 0;
# ifdef I386
    /* We assume this is turned on only with gcc as the compiler. */
    asm("movl %%ebp,%0" : "=r"(frame));
    fp = frame;
# else
    frame = (struct frame *)GC_save_regs_in_stack();
    fp = (struct frame *)((long) frame -> FR_SAVFP + BIAS);
#endif

   for (; !((word)fp HOTTER_THAN (word)frame)
#         ifndef THREADS
            && !((word)GC_stackbottom HOTTER_THAN (word)fp)
#         elif defined(STACK_GROWS_UP)
            && fp != NULL
#         endif
          && nframes < NFRAMES;
        fp = (struct frame *)((long) fp -> FR_SAVFP + BIAS), nframes++) {
#     if NARGS > 0
        int i;
#     endif

      info[nframes].ci_pc = fp->FR_SAVPC;
#     if NARGS > 0
        for (i = 0; i < NARGS; i++) {
          info[nframes].ci_arg[i] = ~(fp->fr_arg[i]);
        }
#     endif /* NARGS > 0 */
  }
  if (nframes < NFRAMES) info[nframes].ci_pc = 0;
}

#endif /* No builtin backtrace */

#endif /* SAVE_CALL_CHAIN */

#ifdef NEED_CALLINFO

/* Print info to stderr.  We do NOT hold the allocation lock */
GC_INNER void GC_print_callers(struct callinfo info[NFRAMES])
{
    int i;
    static int reentry_count = 0;
    DCL_LOCK_STATE;

    /* FIXME: This should probably use a different lock, so that we     */
    /* become callable with or without the allocation lock.             */
    LOCK();
      ++reentry_count;
    UNLOCK();

#   if NFRAMES == 1
      GC_err_printf("\tCaller at allocation:\n");
#   else
      GC_err_printf("\tCall chain at allocation:\n");
#   endif
    for (i = 0; i < NFRAMES; i++) {
#       ifdef CALLINFO_USE_ADDR2LINE
          GC_bool stop = FALSE;
#       endif

        if (0 == info[i].ci_pc)
          break;
#       if NARGS > 0
        {
          int j;

          GC_err_printf("\t\targs: ");
          for (j = 0; j < NARGS; j++) {
            if (j != 0) GC_err_printf(", ");
            GC_err_printf("%d (0x%X)", ~(info[i].ci_arg[j]),
                                        ~(info[i].ci_arg[j]));
          }
          GC_err_printf("\n");
        }
#       endif
        if (reentry_count > 1) {
            /* We were called during an allocation during       */
            /* a previous GC_print_callers call; punt.          */
            GC_err_printf("\t\t##PC##= 0x%lx\n",
                          (unsigned long)info[i].ci_pc);
            continue;
        }
        {
          char buf[40];
          char *name;
#         if defined(GC_HAVE_BUILTIN_BACKTRACE) \
             && !defined(GC_BACKTRACE_SYMBOLS_BROKEN)
            char **sym_name =
              backtrace_symbols((void **)(&(info[i].ci_pc)), 1);
            if (sym_name != NULL) {
              name = sym_name[0];
            } else
#         endif
          /* else */ {
            (void)snprintf(buf, sizeof(buf), "##PC##= 0x%lx",
                           (unsigned long)info[i].ci_pc);
            buf[sizeof(buf) - 1] = '\0';
            name = buf;
          }
#         ifdef CALLINFO_USE_ADDR2LINE
            /* Try for a line number. */
            do {
                FILE *pipe;
#               define EXE_SZ 100
                static char exe_name[EXE_SZ];
#               define CMD_SZ 200
                char cmd_buf[CMD_SZ];
#               define RESULT_SZ 200
                static char result_buf[RESULT_SZ];
                size_t result_len;
                char *old_preload;
#               define PRELOAD_SZ 200
                char preload_buf[PRELOAD_SZ];
                static GC_bool found_exe_name = FALSE;
                static GC_bool will_fail = FALSE;

                /* Try to get it via a hairy and expensive scheme.      */
                /* First we get the name of the executable:             */
                if (will_fail)
                  break;
                if (!found_exe_name) {
                  int ret_code = readlink("/proc/self/exe", exe_name, EXE_SZ);

                  if (ret_code < 0 || ret_code >= EXE_SZ
                      || exe_name[0] != '/') {
                    will_fail = TRUE;   /* Don't try again. */
                    break;
                  }
                  exe_name[ret_code] = '\0';
                  found_exe_name = TRUE;
                }
                /* Then we use popen to start addr2line -e <exe> <addr> */
                /* There are faster ways to do this, but hopefully this */
                /* isn't time critical.                                 */
                (void)snprintf(cmd_buf, sizeof(cmd_buf),
                               "/usr/bin/addr2line -f -e %s 0x%lx",
                               exe_name, (unsigned long)info[i].ci_pc);
                cmd_buf[sizeof(cmd_buf) - 1] = '\0';
                old_preload = GETENV("LD_PRELOAD");
                if (0 != old_preload) {
                  size_t old_len = strlen(old_preload);
                  if (old_len >= PRELOAD_SZ) {
                    will_fail = TRUE;
                    break;
                  }
                  BCOPY(old_preload, preload_buf, old_len + 1);
                  unsetenv ("LD_PRELOAD");
                }
                pipe = popen(cmd_buf, "r");
                if (0 != old_preload
                    && 0 != setenv ("LD_PRELOAD", preload_buf, 0)) {
                  WARN("Failed to reset LD_PRELOAD\n", 0);
                }
                if (NULL == pipe) {
                  will_fail = TRUE;
                  break;
                }
                result_len = fread(result_buf, 1, RESULT_SZ - 1, pipe);
                (void)pclose(pipe);
                if (0 == result_len) {
                  will_fail = TRUE;
                  break;
                }
                if (result_buf[result_len - 1] == '\n') --result_len;
                result_buf[result_len] = 0;
                if (result_buf[0] == '?'
                    || (result_buf[result_len-2] == ':'
                        && result_buf[result_len-1] == '0'))
                  break;
                /* Get rid of embedded newline, if any.  Test for "main" */
                {
                  char * nl = strchr(result_buf, '\n');
                  if (nl != NULL
                      && (word)nl < (word)(result_buf + result_len)) {
                    *nl = ':';
                  }
                  if (strncmp(result_buf, "main",
                              nl != NULL
                                ? (size_t)((word)nl /* a cppcheck workaround */
                                           - COVERT_DATAFLOW(result_buf))
                                : result_len) == 0) {
                    stop = TRUE;
                  }
                }
                if (result_len < RESULT_SZ - 25) {
                  /* Add in hex address */
                  (void)snprintf(&result_buf[result_len],
                                 sizeof(result_buf) - result_len,
                                 " [0x%lx]", (unsigned long)info[i].ci_pc);
                  result_buf[sizeof(result_buf) - 1] = '\0';
                }
#               if defined(CPPCHECK)
                  GC_noop1((unsigned char)name[0]);
                                /* name computed previously is discarded */
#               endif
                name = result_buf;
            } while (0);
#         endif
          GC_err_printf("\t\t%s\n", name);
#         if defined(GC_HAVE_BUILTIN_BACKTRACE) \
             && !defined(GC_BACKTRACE_SYMBOLS_BROKEN)
            if (sym_name != NULL)
              free(sym_name);   /* May call GC_[debug_]free; that's OK  */
#         endif
        }
#       ifdef CALLINFO_USE_ADDR2LINE
          if (stop)
            break;
#       endif
    }
    LOCK();
      --reentry_count;
    UNLOCK();
}

#endif /* NEED_CALLINFO */

#if defined(LINUX) && defined(__ELF__) && !defined(SMALL_CONFIG)
  /* Dump /proc/self/maps to GC_stderr, to enable looking up names for  */
  /* addresses in FIND_LEAK output.                                     */
  void GC_print_address_map(void)
  {
    const char *maps = GC_get_maps();

    GC_err_printf("---------- Begin address map ----------\n");
    GC_err_puts(maps);
    GC_err_printf("---------- End address map ----------\n");
  }
#endif /* LINUX && ELF */
