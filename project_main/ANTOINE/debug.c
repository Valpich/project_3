#include <sys/types.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <stdio.h>
#include <sys/stat.h>
#include <a.out.h>
#include <dirent.h>

#include <fsck.mfs/fsck.h>

struct stack {
  dir_struct *st_dir;
  struct stack *st_next;
  char st_presence;
} *ftop;

/* Print all the dirs starting from <path> [maybe recursive]. */
int print_dirs(const char *path, int recursive)
{
    struct dirent *direntp = NULL;
    DIR *dirp = NULL;
    size_t path_len;

    /* Check input parameters. */
    if (!path)
        return -1;
    path_len = strlen(path);

    if (!path || !path_len || (path_len > _POSIX_PATH_MAX))
        return -1;

    /* Open directory */
    dirp = opendir(path);
    if (dirp == NULL)
        return -1;

    while ((direntp = readdir(dirp)) != NULL)
    {
        /* For every directory entry... */
        struct stat fstat;
        char full_name[_POSIX_PATH_MAX + 1];

        /* Calculate full name, check we are in file length limts */
        if ((path_len + strlen(direntp->d_name) + 1) > _POSIX_PATH_MAX)
            continue;

        strcpy(full_name, path);
        if (full_name[path_len - 1] != '/')
            strcat(full_name, "/");
        strcat(full_name, direntp->d_name);

            printf("%s\n", full_name);
            if (recursive)
                print_dirs(full_name, 1);
    }

    /* Finalize resources. */
    (void)closedir(dirp);
    return 0;
}

/* We are taking first argument as initial path name. */
int main(int argc, const char* argv[])
{
    stack dir;
    dir->st_dir = argv[1];
    descendtree(dir);
    //print_dirs(argv[1], 1);
    return 0;
}
