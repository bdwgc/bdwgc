/*
 * A build-time utility used by `Makefile.direct` file.  Just exit with
 * a nonzero code if the file specified by `argv[1]` does not exist.
 */

#define NOT_GCBUILD
#include "private/gc_priv.h"

#ifdef __DJGPP__
#  include <dirent.h>
#endif

int
main(int argc, char **argv)
{
  FILE *f;
#ifdef __DJGPP__
  DIR *d;
#endif
  const char *fname;

  if (argc != 2) {
    fprintf(stderr, "Usage: %s <file_name>\n", argv[0]);
    return 1;
  }

  fname = TRUSTED_STRING(argv[1]);
  f = fopen(fname, "rb");
  if (f != NULL || (f = fopen(fname, "r")) != NULL) {
    fclose(f);
    return 0;
  }
#ifdef __DJGPP__
  d = opendir(fname);
  if (d != NULL) {
    closedir(d);
    return 0;
  }
#endif
  /* The file is missing. */
  return 2;
}
