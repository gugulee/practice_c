#include "apue.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

struct foo {
    char *name;
    void (*print) (struct foo *, char *);
};

typedef struct foo FOO;

void
print (FOO *f, char *prefix)
{
    printf ("%s: %s\n", prefix, f->name);
}

int
main (void)
{
    FOO f = { .name = "lee", .print = print };
    f.print (&f, "hello");
    exit (0);
}