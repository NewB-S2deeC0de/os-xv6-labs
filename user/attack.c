#include "kernel/types.h"
#include "kernel/fcntl.h"
#include "user/user.h"
#include "kernel/riscv.h"

int
is_valid(char c)
{
  return (c >= '0' && c <= '9') ||
         (c >= 'a' && c <= 'z') ||
         (c >= 'A' && c <= 'Z');
}

int
main(int argc, char *argv[])
{
  int npages = 20;          // number of additional pages to request, in order to increase the likelihood of correctly hitting the original page
  char *mem = sbrk(npages * 4096);
  int p, i, k, start, len;

  for (p = 0; p < npages; p++) {
    char *page = mem + p * 4096;
    i = 0;
    while (i < 4096) {
      if (is_valid(page[i])) {
        start = i;
        while (i < 4096 && is_valid(page[i]))
          i++;
        len = i - start;
        if (len >= 4) {               // Remove overly short strings to reduce noise
          for (k = start; k < i; k++)
            printf("%c", page[k]);
          printf("\n");
        }
      } else {
        i++;
      }
    }
  }

  exit(0);
}
