#include <inttypes.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

int sum (int *a, int n);

int array[2] = { 0x11, 0x22 };

int
main ()
{
    int val = sum (array, 2);
    printf ("val=%d\n", val);
    return val;
}