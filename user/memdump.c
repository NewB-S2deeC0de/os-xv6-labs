#include "kernel/types.h"
#include "user/user.h"
#include "kernel/fcntl.h"

void memdump(char *fmt, char *data, int len);

int
main(int argc, char *argv[])
{
  if (argc == 1) {
    printf("Example 1:\n");
    int a[2] = {61810, 2026};
    memdump("ii", (char *)a, sizeof(a));

    printf("Example 2:\n");
    memdump("S", "a string", sizeof("a string"));

    printf("Example 3:\n");
    char *s = "another";
    memdump("s", (char *)&s, sizeof(s));

    struct sss {
      char *ptr;
      int num1;
      short num2;
      char byte;
      char bytes[8];
    } example;

    example.ptr = "hello";
    example.num1 = 1819438967;
    example.num2 = 100;
    example.byte = 'z';
    strcpy(example.bytes, "xyzzy");

    printf("Example 4:\n");
    memdump("pihcS", (char *)&example, sizeof(example));

    printf("Example 5:\n");
    memdump("sccccc", (char *)&example, sizeof(example));
  } else if (argc == 2) {
    // format in argv[1], up to 512 bytes of data from standard input.
    char data[512];
    int n = 0;
    memset(data, '\0', sizeof(data));
    while (n < sizeof(data)) {
      int nn = read(0, data + n, sizeof(data) - n);
      if (nn <= 0)
        break;
      n += nn;
    }
    memdump(argv[1], data, n);
  } else {
    printf("Usage: memdump [format]\n");
    exit(1);
  }
  exit(0);
}

void
memdump(char *fmt, char *data, int len)
{
  int off = 0;

  for (; *fmt; fmt++) {
    int size;

    switch (*fmt) {
    case 'i':
      size = sizeof(int);
      break;
    case 'p':
    case 's':
      size = sizeof(char *);
      break;
    case 'h':
      size = sizeof(short);
      break;
    case 'c':
      size = sizeof(char);
      break;
    case 'S': {
      int end;

      for (end = off; end < len && data[end] != '\0'; end++)
        ;
      for (int i = off; i < end; i++)
        printf("%c", data[i]);
      printf("\n");
      off = end < len ? end + 1 : len;
      continue;
    }
    default:
      continue;
    }

    if (size > len - off) {
      printf("memdump: not enough data\n");
      return;
    }

    switch (*fmt) {
    case 'i': {
      int value;
      memcpy(&value, data + off, sizeof(value));
      printf("%d\n", value);
      break;
    }
    case 'p': {
      uint64 value = 0;
      memcpy(&value, data + off, sizeof(char *));
      printf("%lx\n", value);
      break;
    }
    case 'h': {
      short value;
      memcpy(&value, data + off, sizeof(value));
      printf("%d\n", value);
      break;
    }
    case 'c':
      printf("%c\n", data[off]);
      break;
    case 's': {
      char *value;
      memcpy(&value, data + off, sizeof(value));
      printf("%s\n", value);
      break;
    }
    }
    off += size;
  }
}
