#include "apue.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#define BUFFSIZE 8192
int
main (int argc, char *argv[])
{
    int n, fd;
    char buf[BUFFSIZE], buf1[BUFFSIZE];
    char line[MAXLINE];
    /* read filename to cat from stdin */
    while (fgets (line, MAXLINE, stdin) != NULL) {
        if (line[strlen (line) - 1] == '\n')
            line[strlen (line) - 1] = 0; /* replace newline with null */
        /* open the file */
        if ((fd = csopen (line, O_RDONLY)) < 0)
            continue; /* csopen() prints error from server */
        sprintf (buf1, "return fd=%d\n", fd);
        write (STDOUT_FILENO, buf1, 1024);
        /* and cat to stdout */
        while ((n = read (fd, buf, BUFFSIZE)) > 0)
            if (write (STDOUT_FILENO, buf, n) != n)
                err_sys ("write error");
        if (n < 0)
            err_sys ("read error");
        close (fd);
    }
    exit (0);
}