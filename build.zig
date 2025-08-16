// THIS MATERIAL IS PROVIDED AS IS, WITH ABSOLUTELY NO WARRANTY EXPRESSED
// OR IMPLIED.  ANY USE IS AT YOUR OWN RISK.
//
// Permission is hereby granted to use or copy this program
// for any purpose, provided the above notices are retained on all copies.
// Permission to modify the code and to distribute modified code is granted,
// provided the above notices are retained, and a notice that the code was
// modified is included with the above copyright notice.

// A script to build and test the collector using Zig build system.
// This script matches `CMakeLists.txt` file as much as possible.

const builtin = @import("builtin");
const std = @import("std");

const zig_min_required_version = "0.14.0";

// TODO: specify `PACKAGE_VERSION` and `LIB*_VER_INFO`.

// Compared to the `cmake` script, some definitions and compiler options
// are hard-coded here, which is natural because build.zig is only built with
// the Zig build system and Zig ships with an embedded clang (as of zig 0.14).
// As a consequence, we do not have to support lots of different compilers
// (a notable exception is msvc target which implies use of the corresponding
// native compiler).
// And, on the contrary, we know exactly what we get and thus we can align on
// clang's capabilities rather than having to discover compiler capabilities.
// Similarly, since Zig ships `libc` headers for many platforms, we can, with
// the knowledge of the platform, determine what capabilities should be
// enabled or not.

comptime {
    const required_ver = std.SemanticVersion.parse(zig_min_required_version)
                            catch unreachable;
    if (builtin.zig_version.order(required_ver) == .lt) {
        @compileError(std.fmt.comptimePrint(
            "Zig version {} does not meet the build requirement of {}",
            .{ builtin.zig_version, required_ver },
        ));
    }
}

pub fn build(b: *std.Build) void {
    const optimize = b.standardOptimizeOption(.{});
    const target = b.standardTargetOptions(.{});
    const t = target.result;

    const default_enable_threads = !t.cpu.arch.isWasm(); // emscripten/wasi

    // Customize build by passing "-D<option_name>[=false]" in command line.
    const enable_cplusplus = b.option(bool, "enable_cplusplus",
                                      "C++ support") orelse false;
    const build_shared_libs = b.option(bool, "BUILD_SHARED_LIBS",
                "Build shared libraries (otherwise static ones)") orelse true;
    const build_cord = b.option(bool, "build_cord",
                                "Build cord library") orelse true;
    const cflags_extra = b.option([]const u8, "CFLAGS_EXTRA",
                                  "Extra user-defined cflags") orelse "";
    // TODO: support `enable_docs`
    const enable_threads = b.option(bool, "enable_threads",
                "Support threads") orelse default_enable_threads;
    const enable_parallel_mark = b.option(bool, "enable_parallel_mark",
        "Parallelize marking and free list construction") orelse true;
    const enable_thread_local_alloc = b.option(bool,
        "enable_thread_local_alloc",
        "Turn on thread-local allocation optimization") orelse true;
    const enable_threads_discovery = b.option(bool,
        "enable_threads_discovery",
        "Enable threads discovery in GC") orelse true;
    const enable_rwlock = b.option(bool, "enable_rwlock",
        "Enable reader mode of the allocator lock") orelse false;
    const enable_throw_bad_alloc_library = b.option(bool,
        "enable_throw_bad_alloc_library",
        "Turn on C++ gctba library build") orelse true;
    const enable_gcj_support = b.option(bool, "enable_gcj_support",
                                        "Support for gcj") orelse true;
    const enable_sigrt_signals = b.option(bool, "enable_sigrt_signals",
        "Use SIGRTMIN-based signals for thread suspend/resume") orelse false;
    const enable_valgrind_tracking = b.option(bool,
        "enable_valgrind_tracking",
        "Support tracking GC_malloc and friends for heap profiling tools")
        orelse false;
    const enable_gc_debug = b.option(bool, "enable_gc_debug",
        "Support for pointer back-tracing") orelse false;
    const disable_gc_debug = b.option(bool, "disable_gc_debug",
        "Disable debugging like GC_dump and its callees") orelse false;
    const enable_java_finalization = b.option(bool,
        "enable_java_finalization",
        "Support for java finalization") orelse true;
    const enable_atomic_uncollectable = b.option(bool,
        "enable_atomic_uncollectable",
        "Support for atomic uncollectible allocation") orelse true;
    const enable_redirect_malloc = b.option(bool, "enable_redirect_malloc",
        "Redirect malloc and friend to GC routines") orelse false;
    const enable_disclaim = b.option(bool, "enable_disclaim",
        "Support alternative finalization interface") orelse true;
    const enable_dynamic_pointer_mask = b.option(bool,
        "enable_dynamic_pointer_mask",
        "Support pointer mask/shift set at runtime") orelse false;
    const enable_large_config = b.option(bool, "enable_large_config",
        "Optimize for large heap or root set") orelse false;
    const enable_gc_assertions = b.option(bool, "enable_gc_assertions",
        "Enable collector-internal assertion checking") orelse false;
    const enable_mmap = b.option(bool, "enable_mmap",
        "Use mmap instead of sbrk to expand the heap") orelse false;
    const enable_munmap = b.option(bool, "enable_munmap",
        "Return page to the OS if empty for N collections") orelse true;
    const enable_dynamic_loading = b.option(bool, "enable_dynamic_loading",
        "Enable tracing of dynamic library data roots") orelse true;
    const enable_register_main_static_data = b.option(bool,
        "enable_register_main_static_data",
        "Perform the initial guess of data root sets") orelse true;
    const enable_checksums = b.option(bool, "enable_checksums",
        "Report erroneously cleared dirty bits") orelse false;
    const enable_werror = b.option(bool, "enable_werror",
        "Pass -Werror to the C compiler (treat warnings as errors)")
        orelse false;
    const enable_single_obj_compilation = b.option(bool,
        "enable_single_obj_compilation",
        "Compile all libgc source files into single .o") orelse false;
    const disable_single_obj_compilation = b.option(bool,
        "disable_single_obj_compilation",
        "Compile each libgc source file independently") orelse false;
    const enable_handle_fork = b.option(bool, "enable_handle_fork",
        "Attempt to ensure a usable collector after fork()") orelse true;
    const disable_handle_fork = b.option(bool, "disable_handle_fork",
        "Prohibit installation of pthread_atfork() handlers") orelse false;
    // TODO: support `enable_emscripten_asyncify`
    const install_headers = b.option(bool, "install_headers",
        "Install header and pkg-config metadata files") orelse true;
    // TODO: support `with_libatomic_ops`, `without_libatomic_ops`

    var source_files: std.ArrayListUnmanaged([]const u8) = .empty;
    defer source_files.deinit(b.allocator);
    var flags: std.ArrayListUnmanaged([]const u8) = .empty;
    defer flags.deinit(b.allocator);

    // Always enabled.
    flags.append(b.allocator, "-D ALL_INTERIOR_POINTERS") catch unreachable;
    flags.append(b.allocator, "-D NO_EXECUTE_PERMISSION") catch unreachable;

    // Output all warnings.
    flags.appendSlice(b.allocator, &.{
        "-Wall",
        "-Wextra",
        "-Wpedantic",
    }) catch unreachable;

    // Disable MS `crt` security warnings reported e.g. for `getenv`, `strcpy`.
    if (t.abi == .msvc) {
        flags.append(b.allocator,
                     "-D _CRT_SECURE_NO_DEPRECATE") catch unreachable;
    }

    source_files.appendSlice(b.allocator, &.{
        "allchblk.c",
        "alloc.c",
        "blacklst.c",
        "dbg_mlc.c",
        "dyn_load.c",
        "finalize.c",
        "headers.c",
        "mach_dep.c",
        "malloc.c",
        "mallocx.c",
        "mark.c",
        "mark_rts.c",
        "misc.c",
        "new_hblk.c",
        "os_dep.c",
        "ptr_chck.c",
        "reclaim.c",
        "typd_mlc.c",
    }) catch unreachable;

    if (enable_threads) {
        flags.append(b.allocator, "-D GC_THREADS") catch unreachable;
        if (enable_parallel_mark) {
            flags.append(b.allocator, "-D PARALLEL_MARK") catch unreachable;
        }
        if (t.os.tag != .windows) { // assume `pthreads`
            // TODO: support cygwin when supported by zig
            // Zig comes with clang which supports GCC atomic intrinsics.
            flags.append(b.allocator,
                         "-D GC_BUILTIN_ATOMIC") catch unreachable;
            // TODO: define and use `THREADDLLIBS_LIST`
            source_files.appendSlice(b.allocator, &.{
                "gc_dlopen.c",
                "pthread_start.c",
                "pthread_support.c",
            }) catch unreachable;
            if (t.os.tag.isDarwin()) {
                source_files.append(b.allocator,
                                    "darwin_stop_world.c") catch unreachable;
            } else {
                source_files.append(b.allocator,
                                    "pthread_stop_world.c") catch unreachable;
            }
            // Common defines for POSIX platforms.
            flags.append(b.allocator, "-D _REENTRANT") catch unreachable;
            // TODO: some targets might need `_PTHREADS` defined too.
            if (enable_thread_local_alloc) {
                flags.append(b.allocator,
                             "-D THREAD_LOCAL_ALLOC") catch unreachable;
                source_files.appendSlice(b.allocator, &.{
                    "specific.c",
                    "thread_local_alloc.c",
                }) catch unreachable;
            }
            // Message for clients: Explicit `GC_INIT` call may be required.
            if (enable_handle_fork and !disable_handle_fork) {
                flags.append(b.allocator, "-D HANDLE_FORK") catch unreachable;
            }
            if (enable_sigrt_signals) {
                flags.append(b.allocator,
                             "-D GC_USESIGRT_SIGNALS") catch unreachable;
            }
        } else {
            // Assume the GCC atomic intrinsics are supported.
            flags.append(b.allocator,
                         "-D GC_BUILTIN_ATOMIC") catch unreachable;
            if (enable_thread_local_alloc
                    and (enable_parallel_mark or !build_shared_libs)) {
                // Imply `THREAD_LOCAL_ALLOC` unless `GC_DLL`.
                flags.append(b.allocator,
                             "-D THREAD_LOCAL_ALLOC") catch unreachable;
                source_files.append(b.allocator,
                                    "thread_local_alloc.c") catch unreachable;
            }
            flags.append(b.allocator,
                         "-D EMPTY_GETENV_RESULTS") catch unreachable;
            source_files.appendSlice(b.allocator, &.{
                // Add `pthread_start.c` file just in case client defines
                // `GC_WIN32_PTHREADS` macro.
                "pthread_start.c",
                "pthread_support.c",
                "win32_threads.c",
            }) catch unreachable;
        }
    }

    // TODO: define/use `NEED_LIB_RT`

    if (disable_handle_fork) {
        flags.append(b.allocator, "-D NO_HANDLE_FORK") catch unreachable;
    }

    if (enable_gcj_support) {
        flags.append(b.allocator, "-D GC_GCJ_SUPPORT") catch unreachable;
        // TODO: do not define `GC_ENABLE_SUSPEND_THREAD` on kFreeBSD
        // if `enable_thread_local_alloc` (a workaround for some bug).
        flags.append(b.allocator,
                     "-D GC_ENABLE_SUSPEND_THREAD") catch unreachable;
        source_files.append(b.allocator, "gcj_mlc.c") catch unreachable;
    }

    if (enable_disclaim) {
        flags.append(b.allocator, "-D ENABLE_DISCLAIM") catch unreachable;
        source_files.append(b.allocator, "fnlz_mlc.c") catch unreachable;
    }

    if (enable_dynamic_pointer_mask) {
        flags.append(b.allocator, "-D DYNAMIC_POINTER_MASK") catch unreachable;
    }

    if (enable_java_finalization) {
        flags.append(b.allocator, "-D JAVA_FINALIZATION") catch unreachable;
    }

    if (enable_atomic_uncollectable) {
        flags.append(b.allocator,
                     "-D GC_ATOMIC_UNCOLLECTABLE") catch unreachable;
    }

    if (enable_valgrind_tracking) {
        flags.append(b.allocator, "-D VALGRIND_TRACKING") catch unreachable;
    }

    if (enable_gc_debug) {
        flags.append(b.allocator, "-D DBG_HDRS_ALL") catch unreachable;
        flags.append(b.allocator, "-D KEEP_BACK_PTRS") catch unreachable;
        if (t.os.tag == .linux) {
            flags.append(b.allocator, "-D MAKE_BACK_GRAPH") catch unreachable;
            // TODO: do not define `SAVE_CALL_COUNT` for e2k
            flags.append(b.allocator,
                         "-D SAVE_CALL_COUNT=8") catch unreachable;
            source_files.append(b.allocator, "backgraph.c") catch unreachable;
        }
    }

    if (disable_gc_debug) {
        flags.append(b.allocator, "-D NO_DEBUGGING") catch unreachable;
    }
    if (optimize != .Debug) {
        flags.append(b.allocator, "-D NDEBUG") catch unreachable;
    }

    if (enable_redirect_malloc) {
        if (enable_gc_debug) {
            flags.append(b.allocator,
                         "-D REDIRECT_MALLOC=GC_debug_malloc_replacement")
                catch unreachable;
            flags.append(b.allocator,
                         "-D REDIRECT_REALLOC=GC_debug_realloc_replacement")
                catch unreachable;
            flags.append(b.allocator,
                         "-D REDIRECT_FREE=GC_debug_free") catch unreachable;
        } else {
            flags.append(b.allocator,
                         "-D REDIRECT_MALLOC=GC_malloc") catch unreachable;
        }
        if (t.os.tag == .windows) {
            flags.append(b.allocator,
                         "-D REDIRECT_MALLOC_IN_HEADER") catch unreachable;
        } else {
            flags.append(b.allocator,
                         "-D GC_USE_DLOPEN_WRAP") catch unreachable;
        }
    }

    if (enable_mmap or enable_munmap) {
        flags.append(b.allocator, "-D USE_MMAP") catch unreachable;
    }

    if (enable_munmap) {
        flags.append(b.allocator, "-D USE_MUNMAP") catch unreachable;
    }

    if (!enable_dynamic_loading) {
        flags.append(b.allocator,
                     "-D IGNORE_DDYNAMIC_LOADING") catch unreachable;
    }

    if (!enable_register_main_static_data) {
        flags.append(b.allocator,
                     "-D GC_DONT_REGISTER_MAIN_STATIC_DATA") catch unreachable;
    }

    if (enable_large_config) {
        flags.append(b.allocator, "-D LARGE_CONFIG") catch unreachable;
    }

    if (enable_gc_assertions) {
        flags.append(b.allocator, "-D GC_ASSERTIONS") catch unreachable;
    }

    if (!enable_threads_discovery) {
        flags.append(b.allocator,
                     "-D GC_NO_THREADS_DISCOVERY") catch unreachable;
    }

    if (enable_rwlock) {
        flags.append(b.allocator, "-D USE_RWLOCK") catch unreachable;
    }

    if (enable_checksums) {
        if (enable_munmap or enable_threads) {
            @panic("CHECKSUMS not compatible with USE_MUNMAP or threads");
        }
        flags.append(b.allocator, "-D CHECKSUMS") catch unreachable;
        source_files.append(b.allocator, "checksums.c") catch unreachable;
    }

    if (enable_werror) {
        flags.append(b.allocator, "-Werror") catch unreachable;
    }

    if (enable_single_obj_compilation
            or (build_shared_libs and !disable_single_obj_compilation)) {
        source_files.clearAndFree(b.allocator);
        source_files.append(b.allocator, "extra/gc.c") catch unreachable;
        if (enable_threads and !t.os.tag.isDarwin() and t.os.tag != .windows) {
            flags.append(b.allocator,
                         "-D GC_PTHREAD_START_STANDALONE") catch unreachable;
            source_files.append(b.allocator,
                                "pthread_start.c") catch unreachable;
        }
    }

    // Add implementation of `backtrace` and `backtrace_symbols`.
    if (t.abi == .msvc) {
        source_files.append(b.allocator, "extra/msvc_dbg.c") catch unreachable;
    }

    // TODO: declare that the libraries do not refer to external symbols
    // of `build_shared_libs`.

    // `zig cc` supports this flag.
    flags.appendSlice(b.allocator, &.{
        // TODO: `-Wno-unused-command-line-argument`
        // Prevent "__builtin_return_address with nonzero argument is unsafe".
        "-Wno-frame-address",
    }) catch unreachable;

    if (build_shared_libs) {
        flags.append(b.allocator, "-D GC_DLL") catch unreachable;
        if (t.abi == .msvc) {
            // TODO: depend on `user32.lib` file instead
            flags.append(b.allocator,
                         "-D DONT_USE_USER32_DLL") catch unreachable;
        } else {
            // `zig cc` supports these flags.
            flags.append(b.allocator,
                         "-D GC_VISIBILITY_HIDDEN_SET") catch unreachable;
            flags.append(b.allocator,
                         "-fvisibility=hidden") catch unreachable;
        }
    } else {
        flags.append(b.allocator, "-D GC_NOT_DLL") catch unreachable;
        if (t.os.tag == .windows) {
            // Do not require the clients to link with `user32` system library.
            flags.append(b.allocator,
                         "-D DONT_USE_USER32_DLL") catch unreachable;
        }
    }

    // Note: Zig uses clang which ships with these so, unless another
    // sysroot/libc, etc. headers location is pointed out, it is fine to
    // hard-code enable this.
    // `-U GC_MISSING_EXECINFO_H`
    // `-U GC_NO_SIGSETJMP`
    flags.append(b.allocator, "-D HAVE_SYS_TYPES_H") catch unreachable;

    if (t.abi == .msvc) {
        // To workaround "extension used" error reported
        // for `__try`/`__finally`.
        flags.append(b.allocator, "-D NO_SEH_AVAILABLE") catch unreachable;
    } else {
        flags.append(b.allocator, "-D HAVE_UNISTD_H") catch unreachable;
    }

    const have_getcontext = !t.abi.isMusl() and t.os.tag != .windows;
    if (!have_getcontext) {
        flags.append(b.allocator, "-D NO_GETCONTEXT") catch unreachable;
    }

    if (!t.os.tag.isDarwin() and t.os.tag != .windows) {
        // `dl_iterate_phdr` exists (as a strong symbol).
        flags.append(b.allocator, "-D HAVE_DL_ITERATE_PHDR") catch unreachable;
        if (enable_threads) {
            // `pthread_sigmask` and `sigset_t` are available and needed.
            flags.append(b.allocator,
                         "-D HAVE_PTHREAD_SIGMASK") catch unreachable;
        }
    }

    // Build with `GC_wcsdup` support (`wcslen` is available).
    flags.append(b.allocator, "-D GC_REQUIRE_WCSDUP") catch unreachable;

    // `pthread_setname_np`, if available, may have 1, 2 or 3 arguments.
    if (t.os.tag.isDarwin()) {
        flags.append(b.allocator, "-D HAVE_PTHREAD_SETNAME_NP_WITHOUT_TID")
                catch unreachable;
    } else if (t.os.tag == .linux) {
        flags.append(b.allocator,
                     "-D HAVE_PTHREAD_SETNAME_NP_WITH_TID") catch unreachable;
    } else {
        // TODO: support `HAVE_PTHREAD_SETNAME_NP_WITH_TID_AND_ARG`
        // and `HAVE_PTHREAD_SET_NAME_NP` targets.
    }

    if (t.os.tag != .windows) {
        // Define to use `dladdr` function (used for debugging).
        flags.append(b.allocator, "-D HAVE_DLADDR") catch unreachable;
    }

    // TODO: as of zig 0.14, exception.h and getsect.h are not provided
    // by zig itself for Darwin target.
    if (t.os.tag.isDarwin() and !target.query.isNative()) {
        flags.append(b.allocator,
                     "-D MISSING_MACH_O_GETSECT_H") catch unreachable;
        flags.append(b.allocator,
                     "-D NO_MPROTECT_VDB") catch unreachable;
    }

    if (enable_cplusplus and enable_werror) {
        if (build_shared_libs and t.os.tag == .windows or t.abi == .msvc) {
            // Avoid "replacement operator new[] cannot be declared inline"
            // warnings.
            flags.append(b.allocator,
                         "-Wno-inline-new-delete") catch unreachable;
        }
        if (t.abi == .msvc) {
            // TODO: as of zig 0.14,
            // "argument unused during compilation: -nostdinc++" warning is
            // reported if using MS compiler.
            flags.append(b.allocator, "-Wno-unused-command-line-argument")
                catch unreachable;
        }
    }

    // Extra user-defined flags (if any) to pass to the compiler.
    if (cflags_extra.len > 0) {
        // Split it up on a space and append each part to flags separately.
        var tokenizer = std.mem.tokenizeScalar(u8, cflags_extra, ' ');
        while (tokenizer.next()) |token| {
            flags.append(b.allocator, token) catch unreachable;
        }
    }

    // TODO: convert `VER_INFO` values to `VERSION`, `SOVERSION` ones
    const gc = b.addLibrary(.{
        .linkage = if (build_shared_libs) .dynamic else .static,
        .name = "gc",
        .root_module = b.createModule(.{
            .target = target,
            .optimize = optimize,
        }),
    });
    gc.addCSourceFiles(.{
        .files = source_files.items,
        .flags = flags.items,
    });
    gc.addIncludePath(b.path("include"));
    gc.linkLibC();

    var gccpp: *std.Build.Step.Compile = undefined;
    var gctba: *std.Build.Step.Compile = undefined;
    if (enable_cplusplus) {
        gccpp = b.addLibrary(.{
            .linkage = if (build_shared_libs) .dynamic else .static,
            .name = "gccpp",
            .root_module = b.createModule(.{
                .target = target,
                .optimize = optimize,
            }),
        });
        gccpp.addCSourceFiles(.{
            .files = &.{
                "gc_badalc.cc",
                "gc_cpp.cc",
            },
            .flags = flags.items,
        });
        gccpp.addIncludePath(b.path("include"));
        gccpp.linkLibrary(gc);
        linkLibCpp(gccpp);
        if (enable_throw_bad_alloc_library) {
            // The same as `gccpp` but contains only `gc_badalc`.
            gctba = b.addLibrary(.{
                .linkage = if (build_shared_libs) .dynamic else .static,
                .name = "gctba",
                .root_module = b.createModule(.{
                    .target = target,
                    .optimize = optimize,
                }),
            });
            gctba.addCSourceFiles(.{
                .files = &.{
                    "gc_badalc.cc",
                },
                .flags = flags.items,
            });
            gctba.addIncludePath(b.path("include"));
            gctba.linkLibrary(gc);
            linkLibCpp(gctba);
        }
    }

    var cord: *std.Build.Step.Compile = undefined;
    if (build_cord) {
        cord = b.addLibrary(.{
            .linkage = if (build_shared_libs) .dynamic else .static,
            .name = "cord",
            .root_module = b.createModule(.{
                .target = target,
                .optimize = optimize,
            })
        });
        cord.addCSourceFiles(.{
            .files = &.{
                "cord/cordbscs.c",
                "cord/cordprnt.c",
                "cord/cordxtra.c",
            },
            .flags = flags.items,
        });
        cord.addIncludePath(b.path("include"));
        cord.linkLibrary(gc);
        cord.linkLibC();
    }

    if (install_headers) {
        installHeader(b, gc, "gc.h");
        installHeader(b, gc, "gc/gc.h");
        installHeader(b, gc, "gc/gc_backptr.h");
        installHeader(b, gc, "gc/gc_config_macros.h");
        installHeader(b, gc, "gc/gc_inline.h");
        installHeader(b, gc, "gc/gc_mark.h");
        installHeader(b, gc, "gc/gc_tiny_fl.h");
        installHeader(b, gc, "gc/gc_typed.h");
        installHeader(b, gc, "gc/gc_version.h");
        installHeader(b, gc, "gc/javaxfc.h");
        installHeader(b, gc, "gc/leak_detector.h");
        if (enable_cplusplus) {
            installHeader(b, gccpp, "gc_cpp.h");
            installHeader(b, gccpp, "gc/gc_allocator.h");
            installHeader(b, gccpp, "gc/gc_cpp.h");
            if (enable_throw_bad_alloc_library) {
                // The same headers as `gccpp` library has.
                installHeader(b, gctba, "gc_cpp.h");
                installHeader(b, gctba, "gc/gc_allocator.h");
                installHeader(b, gctba, "gc/gc_cpp.h");
            }
        }
        if (enable_disclaim) {
            installHeader(b, gc, "gc/gc_disclaim.h");
        }
        if (enable_gcj_support) {
            installHeader(b, gc, "gc/gc_gcj.h");
        }
        if (enable_threads) {
            installHeader(b, gc, "gc/gc_pthread_redirects.h");
        }
        if (build_cord) {
            installHeader(b, cord, "gc/cord.h");
            installHeader(b, cord, "gc/cord_pos.h");
            installHeader(b, cord, "gc/ec.h");
        }
        // TODO: compose and install `bdw-gc.pc` and `pkgconfig`.
    }

    b.installArtifact(gc);
    if (enable_cplusplus) {
        b.installArtifact(gccpp);
        if (enable_throw_bad_alloc_library) {
            b.installArtifact(gctba);
        }
    }
    if (build_cord) {
        b.installArtifact(cord);
    }

    // Note: there is no `build_tests` option, as the tests are built
    // only if `test` step is requested.
    const test_step = b.step("test", "Run tests");
    addTest(b, gc, test_step, flags, "gctest", "tests/gctest.c");
    if (build_cord) {
        addTestExt(b, gc, cord, test_step, flags,
                   "cordtest", "cord/tests/cordtest.c");
        // TODO: add `de` test (Windows only)
    }
    addTest(b, gc, test_step, flags, "hugetest", "tests/huge.c");
    addTest(b, gc, test_step, flags, "leaktest", "tests/leak.c");
    addTest(b, gc, test_step, flags, "middletest", "tests/middle.c");
    addTest(b, gc, test_step, flags, "realloctest", "tests/realloc.c");
    addTest(b, gc, test_step, flags, "smashtest", "tests/smash.c");
    // TODO: add `staticroots` test
    if (enable_gc_debug) {
        addTest(b, gc, test_step, flags, "tracetest", "tests/trace.c");
    }
    if (enable_threads) {
        addTest(b, gc, test_step, flags, "atomicopstest", "tests/atomicops.c");
        addTest(b, gc, test_step, flags,
                "initfromthreadtest", "tests/initfromthread.c");
        addTest(b, gc, test_step, flags,
                "subthreadcreatetest", "tests/subthreadcreate.c");
        addTest(b, gc, test_step, flags,
                "threadleaktest", "tests/threadleak.c");
        if (t.os.tag != .windows) {
            addTest(b, gc, test_step, flags,
                    "threadkeytest", "tests/threadkey.c");
        }
    }
    if (enable_cplusplus) {
        addTestExt(b, gc, gccpp, test_step, flags, "cpptest", "tests/cpp.cc");
        if (enable_throw_bad_alloc_library) {
            addTestExt(b, gc, gctba, test_step, flags,
                       "treetest", "tests/tree.cc");
        }
    }
    if (enable_disclaim) {
        addTest(b, gc, test_step, flags,
                "disclaim_bench", "tests/disclaim_bench.c");
        addTest(b, gc, test_step, flags, "disclaimtest", "tests/disclaim.c");
        addTest(b, gc, test_step, flags, "weakmaptest", "tests/weakmap.c");
    }
}

fn linkLibCpp(lib: *std.Build.Step.Compile) void {
    const t = lib.rootModuleTarget();
    if (t.abi == .msvc) {
        // TODO: as of zig 0.14, "unable to build libcxxabi" warning is
        // reported if linking C++ code using MS compiler.
        lib.linkLibC();
    } else {
        lib.linkLibCpp();
    }
}

fn addTest(b: *std.Build, gc: *std.Build.Step.Compile,
           test_step: *std.Build.Step,
           flags: std.ArrayListUnmanaged([]const u8), testname: []const u8,
           filename: []const u8) void {
    addTestExt(b, gc, null, test_step, flags, testname, filename);
}

fn addTestExt(b: *std.Build, gc: *std.Build.Step.Compile,
              lib2: ?*std.Build.Step.Compile, test_step: *std.Build.Step,
              flags: std.ArrayListUnmanaged([]const u8), testname: []const u8,
              filename: []const u8) void {
    const test_exe = b.addExecutable(.{
        .name = testname,
        .root_module = b.createModule(.{
            .optimize = gc.root_module.optimize.?,
            .target = gc.root_module.resolved_target.?
        })
    });
    test_exe.addCSourceFile(.{
        .file = b.path(filename),
        .flags = flags.items
    });
    test_exe.addIncludePath(b.path("include"));
    test_exe.linkLibrary(gc);
    if (lib2 != null) {
        test_exe.linkLibrary(lib2.?);
    }
    test_exe.linkLibC();
    const run_test_exe = b.addRunArtifact(test_exe);
    test_step.dependOn(&run_test_exe.step);
}

fn installHeader(b: *std.Build, lib: *std.Build.Step.Compile,
                 hfile: []const u8) void {
   const src_path = b.pathJoin(&.{ "include", hfile });
   lib.installHeader(b.path(src_path), hfile);
}
