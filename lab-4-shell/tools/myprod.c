/*
 * myprod.c - Another handy routine for testing your tiny shell
 *
 * usage: myprod <n>
 * Prints a number every second to stdout in 1-second intervals.
 * Terminates after n seconds.
 *
 * If invoked with no argument, n defaults to 10.
 *
 * Useful to test (slow) pipes.
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define NITER 10

int main(int argc, char *argv[])
{
  int n = argc == 2 ? atoi(argv[1]) : NITER;

  for (int i=0; i<n; i++) {
    sleep(1);

    printf("%d\n", i+1);
  }

  return EXIT_SUCCESS;
}
