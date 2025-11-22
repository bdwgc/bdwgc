/*
 * Copyright (c) 2004-2005 Andrei Polushin
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#if !defined(_M_ARM) && !defined(_M_ARM64) && !defined(_M_X64) \
    && defined(_MSC_VER)

/* TODO: arm[64], x86_64 currently miss some machine-dependent code below. */

/* See also `GC_HAVE_BUILTIN_BACKTRACE` in `gc_config_macros.h` file. */

#  include <stdio.h>
#  include <stdlib.h>

#  define GC_BUILD
#  include "gc/gc.h"

#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN 1
#  endif
#  define NOSERVICE
#  include <windows.h>

#  pragma pack(push, 8)
#  include <imagehlp.h>
#  pragma pack(pop)

#  ifdef __cplusplus
extern "C" {
#  endif

/* Compatibility with the platform `execinfo.h` file. */
int backtrace(void *addresses[], int count);
char **backtrace_symbols(void *const addresses[], int count);

#  ifdef __cplusplus
} /* extern "C" */
#  endif

#  pragma comment(lib, "dbghelp.lib")
#  pragma optimize("gy", off)

/*
 * Disable a warning that `/GS` cannot protect parameters and local
 * variables from local buffer overrun because optimizations are off.
 */
#  pragma warning(disable : 4748)

typedef GC_word word;
#  define GC_ULONG_PTR word

#  ifdef _WIN64
typedef GC_ULONG_PTR ULONG_ADDR;
#  else
typedef ULONG ULONG_ADDR;
#  endif

#  ifndef MAX_SYM_NAME
#    define MAX_SYM_NAME 2000
#  endif

static HANDLE
GetSymHandle(void)
{
  static HANDLE symHandle = NULL;
  if (!symHandle) {
    BOOL bRet = SymInitialize(symHandle = GetCurrentProcess(), NULL, FALSE);
    if (bRet) {
      DWORD dwOptions = SymGetOptions();
      SymSetOptions((dwOptions & ~(DWORD)SYMOPT_UNDNAME) | SYMOPT_LOAD_LINES);
    }
  }
  return symHandle;
}

static void *CALLBACK
FunctionTableAccess(HANDLE hProcess, ULONG_ADDR dwAddrBase)
{
  return SymFunctionTableAccess(hProcess, dwAddrBase);
}

static ULONG_ADDR CALLBACK
GetModuleBase(HANDLE hProcess, ULONG_ADDR dwAddress)
{
  MEMORY_BASIC_INFORMATION memoryInfo;
  ULONG_ADDR dwAddrBase = SymGetModuleBase(hProcess, dwAddress);
  if (dwAddrBase != 0) {
    return dwAddrBase;
  }
  if (VirtualQueryEx(hProcess, (void *)(GC_ULONG_PTR)dwAddress, &memoryInfo,
                     sizeof(memoryInfo))) {
    char filePath[_MAX_PATH];
    char curDir[_MAX_PATH];
    char exePath[_MAX_PATH];
    DWORD size = GetModuleFileNameA((HINSTANCE)memoryInfo.AllocationBase,
                                    filePath, sizeof(filePath));

    /*
     * Save and restore current directory around `SymLoadModule` (see
     * the KB article Q189780).
     */
    GetCurrentDirectoryA(sizeof(curDir), curDir);
    GetModuleFileNameA(NULL, exePath, sizeof(exePath));
#  if _MSC_VER > 1200
    strcat_s(exePath, sizeof(exePath), "\\..");
#  else /* VC 6 or earlier */
    strcat(exePath, "\\..");
#  endif
    SetCurrentDirectoryA(exePath);
#  ifdef _DEBUG
    GetCurrentDirectoryA(sizeof(exePath), exePath);
#  endif
    SymLoadModule(hProcess, NULL, size ? filePath : NULL, NULL,
                  (ULONG_ADDR)(GC_ULONG_PTR)memoryInfo.AllocationBase, 0);
    SetCurrentDirectoryA(curDir);
  }
  return (ULONG_ADDR)(GC_ULONG_PTR)memoryInfo.AllocationBase;
}

static ULONG_ADDR
CheckAddress(void *address)
{
  ULONG_ADDR dwAddress = (ULONG_ADDR)(GC_ULONG_PTR)address;
  GetModuleBase(GetSymHandle(), dwAddress);
  return dwAddress;
}

static size_t GetStackFramesFromContext(HANDLE hProcess, HANDLE hThread,
                                        CONTEXT *context, size_t skip,
                                        void *frames[], size_t maxFrames);

static size_t
GetStackFrames(size_t skip, void *frames[], size_t maxFrames)
{
  HANDLE hProcess = GetSymHandle();
  HANDLE hThread = GetCurrentThread();
  CONTEXT context;
  context.ContextFlags = CONTEXT_FULL;
  if (!GetThreadContext(hThread, &context)) {
    return 0;
  }
  /* `GetThreadContext` might return invalid context for the current thread. */
#  if defined(_M_IX86)
  __asm mov context.Ebp, ebp
#  endif
      return GetStackFramesFromContext(hProcess, hThread, &context, skip + 1,
                                       frames, maxFrames);
}

static size_t
GetStackFramesFromContext(HANDLE hProcess, HANDLE hThread, CONTEXT *context,
                          size_t skip, void *frames[], size_t maxFrames)
{
  size_t frameIndex;
  DWORD machineType;
  STACKFRAME stackFrame = { 0 };
  stackFrame.AddrPC.Mode = AddrModeFlat;
#  if defined(_M_IX86)
  machineType = IMAGE_FILE_MACHINE_I386;
  stackFrame.AddrPC.Offset = context->Eip;
  stackFrame.AddrStack.Mode = AddrModeFlat;
  stackFrame.AddrStack.Offset = context->Esp;
  stackFrame.AddrFrame.Mode = AddrModeFlat;
  stackFrame.AddrFrame.Offset = context->Ebp;
#  elif defined(_M_MRX000)
  machineType = IMAGE_FILE_MACHINE_R4000;
  stackFrame.AddrPC.Offset = context->Fir;
#  elif defined(_M_ALPHA)
  machineType = IMAGE_FILE_MACHINE_ALPHA;
  stackFrame.AddrPC.Offset = (unsigned long)context->Fir;
#  elif defined(_M_PPC)
  machineType = IMAGE_FILE_MACHINE_POWERPC;
  stackFrame.AddrPC.Offset = context->Iar;
#  elif defined(_M_IA64)
  machineType = IMAGE_FILE_MACHINE_IA64;
  stackFrame.AddrPC.Offset = context->StIIP;
#  elif defined(_M_ALPHA64)
  machineType = IMAGE_FILE_MACHINE_ALPHA64;
  stackFrame.AddrPC.Offset = context->Fir;
#  elif !defined(CPPCHECK)
#    error Unknown CPU
#  endif
  for (frameIndex = 0; frameIndex < maxFrames;) {
    BOOL bRet
        = StackWalk(machineType, hProcess, hThread, &stackFrame, &context,
                    NULL, FunctionTableAccess, GetModuleBase, NULL);
    if (!bRet) {
      break;
    }
    if (skip) {
      skip--;
    } else {
      frames[frameIndex++] = (void *)(GC_ULONG_PTR)stackFrame.AddrPC.Offset;
    }
  }
  return frameIndex;
}

static size_t
GetModuleNameFromAddress(void *address, char *moduleName, size_t size)
{
  const char *sourceName;
  IMAGEHLP_MODULE moduleInfo = { sizeof(moduleInfo) };

  if (size)
    *moduleName = 0;
  if (!SymGetModuleInfo(GetSymHandle(), CheckAddress(address), &moduleInfo)) {
    return 0;
  }
  sourceName = strrchr(moduleInfo.ImageName, '\\');
  if (sourceName) {
    sourceName++;
  } else {
    sourceName = moduleInfo.ImageName;
  }
  if (size) {
    strncpy(moduleName, sourceName, size)[size - 1] = 0;
  }
  return strlen(sourceName);
}

union sym_namebuf_u {
  IMAGEHLP_SYMBOL sym;
  char symNameBuffer[sizeof(IMAGEHLP_SYMBOL) + MAX_SYM_NAME];
};

static size_t
GetSymbolNameFromAddress(void *address, char *symbolName, size_t size,
                         size_t *offsetBytes)
{
  if (size)
    *symbolName = 0;
  if (offsetBytes)
    *offsetBytes = 0;
  __try {
    ULONG_ADDR dwOffset = 0;
    union sym_namebuf_u u;

    u.sym.SizeOfStruct = sizeof(u.sym);
    u.sym.MaxNameLength = sizeof(u.symNameBuffer) - sizeof(u.sym);

    if (!SymGetSymFromAddr(GetSymHandle(), CheckAddress(address), &dwOffset,
                           &u.sym)) {
      return 0;
    } else {
      const char *sourceName = u.sym.Name;
      char undName[1024];
      if (UnDecorateSymbolName(u.sym.Name, undName, sizeof(undName),
                               UNDNAME_NO_MS_KEYWORDS
                                   | UNDNAME_NO_ACCESS_SPECIFIERS)) {
        sourceName = undName;
      } else if (SymUnDName(&u.sym, undName, sizeof(undName))) {
        sourceName = undName;
      }
      if (offsetBytes) {
        *offsetBytes = dwOffset;
      }
      if (size) {
        strncpy(symbolName, sourceName, size)[size - 1] = 0;
      }
      return strlen(sourceName);
    }
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    SetLastError(GetExceptionCode());
  }
  return 0;
}

static size_t
GetFileLineFromAddress(void *address, char *fileName, size_t size,
                       size_t *lineNumber, size_t *offsetBytes)
{
  const char *sourceName;
  IMAGEHLP_LINE line = { sizeof(line) };
  GC_ULONG_PTR dwOffset = 0;

  if (size)
    *fileName = 0;
  if (lineNumber)
    *lineNumber = 0;
  if (offsetBytes)
    *offsetBytes = 0;
  if (!SymGetLineFromAddr(GetSymHandle(), CheckAddress(address), &dwOffset,
                          &line)) {
    return 0;
  }
  if (lineNumber) {
    *lineNumber = line.LineNumber;
  }
  if (offsetBytes) {
    *offsetBytes = dwOffset;
  }
  sourceName = line.FileName;
  /*
   * TODO: Resolve relative filenames, found in "source directories"
   * registered with MSVC IDE.
   */
  if (size) {
    strncpy(fileName, sourceName, size)[size - 1] = 0;
  }
  return strlen(sourceName);
}

#  define GC_SNPRINTF _snprintf

static size_t
GetDescriptionFromAddress(void *address, const char *format, char *buffer,
                          size_t size)
{
  char *const begin = buffer;
  char *const end = buffer + size;
  size_t line_number = 0;

  (void)format;
  if (size) {
    *buffer = 0;
  }
  buffer += GetFileLineFromAddress(address, buffer, size, &line_number, NULL);
  size = (GC_ULONG_PTR)end < (GC_ULONG_PTR)buffer ? 0 : end - buffer;

  if (line_number) {
    char str[20];

    (void)GC_SNPRINTF(str, sizeof(str), "(%d) : ", (int)line_number);
    str[sizeof(str) - 1] = '\0';
    if (size) {
      strncpy(buffer, str, size)[size - 1] = 0;
    }
    buffer += strlen(str);
    size = (GC_ULONG_PTR)end < (GC_ULONG_PTR)buffer ? 0 : end - buffer;
  }

  if (size) {
    strncpy(buffer, "at ", size)[size - 1] = 0;
  }
  buffer += sizeof("at ") - 1;
  size = (GC_ULONG_PTR)end < (GC_ULONG_PTR)buffer ? 0 : end - buffer;

  buffer += GetSymbolNameFromAddress(address, buffer, size, NULL);
  size = (GC_ULONG_PTR)end < (GC_ULONG_PTR)buffer ? 0 : end - buffer;

  if (size) {
    strncpy(buffer, " in ", size)[size - 1] = 0;
  }
  buffer += sizeof(" in ") - 1;
  size = (GC_ULONG_PTR)end < (GC_ULONG_PTR)buffer ? 0 : end - buffer;

  buffer += GetModuleNameFromAddress(address, buffer, size);
  return buffer - begin;
}

static size_t
GetDescriptionFromStack(void *const frames[], size_t count, const char *format,
                        char *description[], size_t size)
{
  const GC_ULONG_PTR begin = (GC_ULONG_PTR)description;
  const GC_ULONG_PTR end = begin + size;
  GC_ULONG_PTR buffer = begin + (count + 1) * sizeof(char *);
  size_t i;

  for (i = 0; i < count; ++i) {
    if (description)
      description[i] = (char *)buffer;
    buffer += 1
              + GetDescriptionFromAddress(frames[i], format, (char *)buffer,
                                          end < buffer ? 0 : end - buffer);
  }
  if (description)
    description[count] = NULL;
  return buffer - begin;
}

/* The below is for compatibility with the platform `execinfo.h` file. */

int
backtrace(void *addresses[], int count)
{
  return GetStackFrames(1, addresses, count);
}

char **
backtrace_symbols(void *const addresses[], int count)
{
  size_t size = GetDescriptionFromStack(addresses, count, NULL, NULL, 0);
  char **symbols = (char **)malloc(size);
  if (symbols != NULL)
    GetDescriptionFromStack(addresses, count, NULL, symbols, size);
  return symbols;
}

#else

/* ANSI C does not allow translation units to be empty. */
extern int GC_quiet_msvc_dbg;

#endif
