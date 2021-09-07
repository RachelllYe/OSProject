#include <stdio.h>
#include <unistd.h>
int inc = 1;
int total = 0;
char *sbrk(int incr);
char *result;
int i;
int main(int argc, int **argv)
{
    while (((int)(result = sbrk(inc))) > 0)
    {
        for (i = 0; i < inc; i++)
            result[i] = 0x12;
        total += inc;
        printf("incremented by: %d, total: %d , result: %d\n", inc, total,   (int)result);
        inc += inc;
    }
    exit(0);
    return 0;
}
