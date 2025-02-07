/* Conditionally execute a command based if the file argv[1] doesn't exist */
/* Except for execvp, we stick to ANSI C.                                  */

# define NOT_GCBUILD
# include "private/gc_priv.h"
# include <stdio.h>
# include <stdlib.h>
# include <unistd.h>
#ifdef __DJGPP__
#include <dirent.h>
#endif /* __DJGPP__ */

int main(int argc, char **argv)
{
    FILE * f;
#ifdef __DJGPP__
    DIR * d;
#endif /* __DJGPP__ */
    char *fname;

    if (argc < 3) goto Usage;

    fname = TRUSTED_STRING(argv[1]);
    f = fopen(fname, "rb");
    if (f != NULL) {
        fclose(f);
        return(0);
    }
    f = fopen(fname, "r");
    if (f != NULL) {
        fclose(f);
        return(0);
    }
#ifdef __DJGPP__
    if ((d = opendir(fname)) != 0) {
            closedir(d);
            return(0);
    }
#endif
    printf("^^^^Starting command^^^^\n");
    fflush(stdout);
    execvp(TRUSTED_STRING(argv[2]), (void *)(argv + 2));
    exit(1);

Usage:
    fprintf(stderr, "Usage: %s file_name command\n", argv[0]);
    return(1);
}
