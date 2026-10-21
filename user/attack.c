#include "kernel/types.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  int npages = 20;
  char *mem = sbrk(npages * 4096);
  char *marker = "Here it is: ";
  int mlen = strlen(marker);
  int total = npages * 4096;
  int i, k, match;

  for (i = 0; i < total - mlen; i++) {
    match = 1;
    for (k = 0; k < mlen; k++) {
      if (mem[i + k] != marker[k]) { match = 0; break; }
    }
    if (match && mem[i + mlen] != '\0') {
      unsigned int expected_offset = 32 + (unsigned int)(mem[i + mlen] & 0xff);
      if (((unsigned long)(mem + i) % 4096) == expected_offset) {
        printf("%s\n", mem + i + mlen);
        exit(0);
      }
    }
  }

  exit(0);
}