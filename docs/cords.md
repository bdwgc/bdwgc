# Cords package

Cords package (also known as "cord library") is a string package that uses
a tree-based representation.

See `cord.h` file for a description of the basic functions provided.  And,
`ec.h` file describes "extensible cords", those are essentially output streams
that write to a cord; these allow for efficient construction of cords without
requiring a bound on the size of a cord.

The `cord` library is built along with `gc` library by default unless manually
disabled (e.g., in case of cmake-based build, unless `-D build_cord=OFF` option
is passed to `cmake`).

More details on the data structure can be found in: Boehm, Atkinson, and Plass,
["Ropes: An Alternative to Strings"](https://ondoc.logand.com/d/686/pdf),
Software Practice and Experience 25, 12, December 1995, pp. 1315-1330.

A fundamentally similar "rope" data structure is also part of SGI's standard
template library implementation, and its descendants, which include the
GNU C++ library.  That uses reference counting by default.  There is a short
description of that data structure in
[Rope Implementation Overview](https://www.boostcpp.org/sgi/stl/ropeimpl.html).

All of these are descendants of the "ropes" in Xerox Cedar.

`cord/tests/de.c` file is a very dumb text editor that illustrates the use of
cords.  It maintains a list of file versions.  Each version is simply a cord
representing the file contents.  Nonetheless, standard editing operations are
efficient, even on very large files.  (Its 3-line "user manual" can be
obtained by invoking it without arguments.  Note that `^R^N` and `^R^P` move
the cursor by almost a screen.  It does not understand tabs, which will show
up as highlighted "I"s.  Use the UNIX `expand` program first.)  To build the
editor, type `make de` in the bdwgc root directory.

Note that `CORD_printf` and friends use C functions with variable numbers
of arguments in non-standard-conforming ways.  This code is known to break on
some platforms, notably PowerPC.  It should be possible to build the remainder
of the library (everything but `cordprnt.c` file) on any platform that
supports the collector.
