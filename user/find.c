#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/param.h"
#include "kernel/fs.h"
#include "kernel/fcntl.h"
#include "user/user.h"

static int
find(char *path, char *target)
{
  int fd, n, failed = 0;
  struct stat st;
  struct dirent de;
  char child[MAXPATH];
  char *name = path;

  if ((fd = open(path, O_RDONLY)) < 0) {
    fprintf(2, "find: cannot open %s\n", path);
    return 1;
  }
  if (fstat(fd, &st) < 0) {
    fprintf(2, "find: cannot stat %s\n", path);
    close(fd);
    return 1;
  }

  for (char *p = path; *p; p++) {
    if (*p == '/')
      name = p + 1;
  }
  // Report directories too, before visiting their children.
  if (strcmp(name, target) == 0)
    printf("%s\n", path);

  if (st.type == T_DIR) {
    int len = strlen(path);
    int separator = len > 0 && path[len - 1] != '/';
    while ((n = read(fd, &de, sizeof(de))) == sizeof(de)) {
      if (de.inum == 0)
        continue;

      // Directory entry names are fixed-width and may lack a terminator.
      char entry[DIRSIZ + 1];
      memmove(entry, de.name, DIRSIZ);
      entry[DIRSIZ] = '\0';
      if (strcmp(entry, ".") == 0 || strcmp(entry, "..") == 0)
        continue;

      if (len + separator + strlen(entry) + 1 > sizeof(child)) {
        fprintf(2, "find: path too long: %s/%s\n", path, entry);
        failed = 1;
        continue;
      }
      strcpy(child, path);
      if (separator)
        child[len] = '/';
      strcpy(child + len + separator, entry);
      if (find(child, target))
        failed = 1;
    }
    if (n != 0) {
      fprintf(2, "find: cannot read directory %s\n", path);
      failed = 1;
    }
  }
  close(fd);
  return failed;
}

int
main(int argc, char *argv[])
{
  char path[MAXPATH];

  if (argc != 3) {
    fprintf(2, "usage: find <path> <name>\n");
    exit(1);
  }
  int len = strlen(argv[1]);
  if (len >= sizeof(path)) {
    fprintf(2, "find: path too long\n");
    exit(1);
  }
  strcpy(path, argv[1]);
  // Keep root intact, but accept starting directories with trailing slashes.
  while (len > 1 && path[len - 1] == '/')
    path[--len] = '\0';

  exit(find(path, argv[2]));
}
