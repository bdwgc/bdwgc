# Makefile to build the garbage collector library using the Digital Mars
# compiler from www.digitalmars.com
# Written by Walter Bright

CFLAGS_EXTRA=
DEFINES=-DGC_DLL -DGC_THREADS -DGC_DISCOVER_TASK_THREADS \
    -DALL_INTERIOR_POINTERS -DENABLE_DISCLAIM -DGC_ATOMIC_UNCOLLECTABLE \
    -DGC_GCJ_SUPPORT -DJAVA_FINALIZATION -DNO_EXECUTE_PERMISSION \
    -DGC_REQUIRE_WCSDUP -DUSE_MUNMAP
CORD_DEFINES=-DGC_DLL -DCORD_NOT_DLL
CFLAGS=-Iinclude -Ilibatomic_ops\src $(DEFINES) -g $(CFLAGS_EXTRA)
CORD_CFLAGS=-Iinclude $(CORD_DEFINES) -g $(CFLAGS_EXTRA)
LFLAGS=/ma/implib/co
CC=sc

# Must precede other goals.
all: cord.lib gc.lib

gc.obj: extra\gc.c
	$(CC) -c $(CFLAGS) extra\gc.c -ogc.obj

.cpp.obj:
	$(CC) -c $(CFLAGS) -Aa $*

check: gctest.exe cpptest.exe treetest.exe cordtest.exe de.exe
	set GC_PROMPT_DISABLED=1
	gctest.exe
	cpptest.exe
	treetest.exe
	cordtest.exe

gc.lib: gc.dll

gc.dll: gc.obj gc_badalc.obj gc_cpp.obj gc.def digimars.mak
	$(CC) -ogc.dll gc.obj gc_badalc.obj gc_cpp.obj -L$(LFLAGS) gc.def kernel32.lib user32.lib

gc.def: digimars.mak
	echo LIBRARY GC >gc.def
	echo DESCRIPTION "Boehm-Demers-Weiser Garbage Collector" >>gc.def
	echo EXETYPE NT	>>gc.def
	echo EXPORTS >>gc.def
	echo GC_is_visible_print_proc >>gc.def
	echo GC_is_valid_displacement_print_proc >>gc.def

# TODO: cannot build `cord` as DLL because of static linking of C runtime.
cord.lib: cord\cordbscs.obj cord\cordprnt.obj cord\cordxtra.obj
	lib -c cord.lib cord\cordbscs.obj cord\cordprnt.obj cord\cordxtra.obj

cord\cordbscs.obj: cord\cordbscs.c
	$(CC) -c $(CORD_CFLAGS) cord\cordbscs.c -ocord\cordbscs.obj

cord\cordprnt.obj: cord\cordprnt.c
	$(CC) -c $(CORD_CFLAGS) cord\cordprnt.c -ocord\cordprnt.obj

cord\cordxtra.obj: cord\cordxtra.c
	$(CC) -c $(CORD_CFLAGS) cord\cordxtra.c -ocord\cordxtra.obj

clean:
	del *.log *.map *.obj gc.def gc.dll gc.lib
	del tests\*.obj gctest.exe cpptest.exe treetest.exe
	del cord\*.obj cord.lib cord\tests\*.obj cordtest.exe de.exe

gctest.exe: gc.lib tests\gctest.obj
	$(CC) -ogctest.exe tests\gctest.obj gc.lib

tests\gctest.obj: tests\gctest.c
	$(CC) -c $(CFLAGS) tests\gctest.c -otests\gctest.obj

cpptest.exe: gc.lib tests\cpptest.obj
	$(CC) -ocpptest.exe tests\cpptest.obj gc.lib

tests\cpptest.obj: tests\cpp.cc
	$(CC) -c $(CFLAGS) -cpp tests\cpp.cc -otests\cpptest.obj

treetest.exe: gc.lib tests\treetest.obj
	$(CC) -otreetest.exe tests\treetest.obj gc.lib

tests\treetest.obj: tests\tree.cc
	$(CC) -c $(CFLAGS) -cpp tests\tree.cc -otests\treetest.obj

cordtest.exe: cord\tests\cordtest.obj cord.lib gc.lib
	$(CC) -ocordtest.exe cord\tests\cordtest.obj cord.lib gc.lib

cord\tests\cordtest.obj: cord\tests\cordtest.c
	$(CC) -c $(CORD_CFLAGS) cord\tests\cordtest.c -ocord\tests\cordtest.obj

# TODO: compile `de_win.rc`.
de.exe: cord\tests\de.obj cord\tests\de_win.obj cord.lib gc.lib
	$(CC) -ode.exe cord\tests\de.obj cord\tests\de_win.obj cord.lib gc.lib gdi32.lib user32.lib

cord\tests\de.obj: cord\tests\de.c
	$(CC) -c $(CORD_CFLAGS) cord\tests\de.c -ocord\tests\de.obj

cord\tests\de_win.obj: cord\tests\de_win.c
	$(CC) -c $(CORD_CFLAGS) cord\tests\de_win.c -ocord\tests\de_win.obj

gc_badalc.obj: gc_badalc.cc gc_badalc.cpp
gc_cpp.obj: gc_cpp.cc gc_cpp.cpp
