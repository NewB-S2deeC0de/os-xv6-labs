#include "kernel/types.h"
#include "kernel/fcntl.h"
#include "user/user.h"
#include "kernel/riscv.h"

#define DATASIZE (9 * 4096)

int
main(int argc, char *argv[])
{
  if (argc != 2) {
    printf("Usage: secret the-secret\n");
    exit(1);
  }

  char *data = sbrk(DATASIZE);

  // avoid putting the secret near the very start of a page.
  unsigned long align = ((unsigned long) data) % 4096;
  if(align != 0)
    data += (4096 - align);

  // but put it at a random-ish offset.
  if(strlen(argv[1]) > 0)
    data += 32 + (unsigned int)(argv[1][0] & 0xff);

#define MSG "Here it is: "
  strcpy(data, MSG);
  strcpy(data + strlen(MSG), argv[1]);

  exit(0);
}
