// escape2: tests that a sandboxed process cannot escalate its own privileges
// via interpose(), even with crafted arguments.
//
// Expected output when run as: sandbox <mask blocking SYS_open> - escape2
//   escape2: all confined OK
// Any "ESCAPED" line indicates a privilege escalation bug.

#include "kernel/types.h"
#include "kernel/fcntl.h"
#include "kernel/syscall.h"
#include "user/user.h"

#define SYS_open_bit  (1 << 15)
#define SYS_interpose_bit (1 << 23)

static void
try_open(char *label, char *file)
{
  int fd = open(file, O_RDONLY);
  if (fd >= 0) {
    printf("escape2: ESCAPED via %s\n", label);
    close(fd);
  } else {
    printf("escape2: confined via %s OK\n", label);
  }
}

int
main(void)
{
  // Attempt 1: call interpose(0, 0) — try to reset mask to 0
  interpose(0, 0);
  try_open("reset mask=0", "README");

  // Attempt 2: call interpose(0, "README") — try to add a path exception
  // that would un-block SYS_open for README
  interpose(0, "README");
  try_open("path exception after reset", "README");

  // Attempt 3: try to set SYS_open path exception with a non-blocking mask
  // (mask does not set the SYS_open bit, so it can't add open to path_mask)
  interpose(SYS_interpose_bit, "README");
  try_open("interpose-bit mask with README path", "README");

  // Attempt 4: try replacing existing path restriction with a broader one
  // (First sandbox set path="-" which means all paths blocked; now try to
  //  set a specific allowed path — should be rejected)
  interpose(SYS_open_bit, "README");
  try_open("replace path restriction", "README");

  printf("escape2: all confined OK\n");
  exit(0);
}
