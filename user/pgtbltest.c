#include "kernel/param.h"
#include "kernel/fcntl.h"
#include "kernel/types.h"
#include "kernel/riscv.h"
#include "user/user.h"
#include "kernel/vm.h"

#define SZ (8 * SUPERPGSIZE)

void print_pgtbl();
void vmprint_test();
void ugetpid_test();
void pgaccess_test();
void ksuper_test();

int
main(int argc, char *argv[])
{
  print_pgtbl();
  vmprint_test();
  ksuper_test();
  pgaccess_test();
  // ugetpid_test();
  printf("pgtbltest: all tests succeeded\n");
  exit(0);
}

char *testname = "???";

void
err(char *why)
{
  printf("pgtbltest: %s failed: %s, pid=%d\n", testname, why, getpid());
  exit(1);
}

void
print_pte(uint64 va)
{
  pte_t pte = (pte_t)pgpte((void *)va);
  printf("va 0x%lx pte 0x%lx pa 0x%lx perm 0x%lx\n", va, pte, PTE2PA(pte),
         PTE_FLAGS(pte));
}

void
print_pgtbl()
{
  printf("print_pgtbl starting\n");
  for (uint64 i = 0; i < 10; i++) {
    print_pte(i * PGSIZE);
  }
  uint64 top = MAXVA / PGSIZE;
  for (uint64 i = top - 10; i < top; i++) {
    print_pte(i * PGSIZE);
  }
  printf("print_pgtbl: OK\n");
}

void
ugetpid_test()
{
  int i;

  printf("ugetpid_test starting\n");
  testname = "ugetpid_test";

  if (getpid() != ugetpid())
    err("mismatched PID #1");

  for (i = 0; i < 64; i++) {
    int ret = fork();
    if (ret != 0) {
      wait(&ret);
      if (ret != 0)
        exit(1);
      continue;
    }
    if (getpid() != ugetpid())
      err("mismatched PID #2");
    exit(0);
  }
  printf("ugetpid_test: OK\n");
}

void
vmprint_test()
{
  printf("vmprint_test starting\n");
  vmprint();
  printf("vmprint_test: done\n");
}

void
pgaccess_test()
{
  char *buf;
  char *bits;
  unsigned int abits;
  int npage = 256;
  printf("pgaccess_test starting\n");
  testname = "pgaccess_test";
  
  buf = malloc(32 * PGSIZE);
  if (pgaccess(buf, 32, &abits) < 0)
    err("pgaccess failed");
  
  buf[PGSIZE * 1] += 1;
  buf[PGSIZE * 2] += 1;
  buf[PGSIZE * 30] += 1;
  if (pgaccess(buf, 32, &abits) < 0)
    err("pgaccess failed");
  if (abits != ((1 << 1) | (1 << 2) | (1 << 30)))
    err("incorrect access bits set");

  free(buf);

  buf = malloc(npage * PGSIZE);
  bits = malloc(PGSIZE/8);
  if (pgaccess(buf, npage, bits) < 0)
    err("pgaccess failed");

  for (int i = 0; i < npage; i++) {
    buf[i * PGSIZE] += 1;
  }
  
  if (pgaccess(buf, npage, bits) < 0)
    err("pgaccess failed");

  for(int i = 0; i < npage/8; i++) {
    if(bits[i] != 0xFF) {
      err("incorrect access bits set");
    }
  }

  // test accessing pages that aren't mapped
  char *end = sbrk(0);
  if (pgaccess(end, 4096, bits) >= 0)
    err("pgaccess succeeded");

  // test a bits address that isn't mapped
  end = end + PGSIZE;
  if (pgaccess(buf, 4096, end) >= 0)
    err("pgaccess succeeded");

  // a call that fails midway must not clear access bits of earlier pages
  char *last = (char *)(((uint64) sbrk(0) - 1) & ~(uint64)(PGSIZE - 1));
  bits[0] = 0;
  if (pgaccess(last, 1, bits) < 0)
    err("pgaccess failed");
  last[0] += 1;
  if (pgaccess(last, 2, bits) >= 0)
    err("pgaccess succeeded");
  bits[0] = 0;
  if (pgaccess(last, 1, bits) < 0)
    err("pgaccess failed");
  if ((bits[0] & 1) == 0)
    err("failed pgaccess cleared access bit");

  // test accessing first text page
  if (pgaccess((char *) 4096, 1, bits) < 0)
    err("pgaccess failed");

  // test accessing a unreasonable number of pages
  if (pgaccess((char *) 4096, 10000000, bits) >= 0)
    err("pgaccess failed");

  free(buf);
  free(bits);
  
  printf("pgaccess_test: OK\n");
}


void ksuper_test() {
  testname = "ksuper_test";
  uint64 s = 0;
  uint64 n = 0;
  if (ksupernpte(&s, &n) != 0)
    err("ksuperptes failed");
  if (s < 93) {
    err("not enough of super ptes");
  }
  if (n > 1610) {
    err("too many regular ptes");
  }
  printf("ksuper_test: OK\n");
}
