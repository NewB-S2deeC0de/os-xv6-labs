#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/param.h"
#include "kernel/fs.h"
#include "kernel/fcntl.h"
#include "user/user.h"

struct entry {
  char name[DIRSIZ + 1];
  struct entry *next;
};

// Keep the command argument array out of each recursive stack frame.
static int __attribute__((noinline))
on_match(char *path, char **command, int command_argc)
{
  if (command_argc == 0) {
    printf("%s\n", path);
    return 0;
  }

  char *args[MAXARG];
  for (int i = 0; i < command_argc; i++)
    args[i] = command[i];
  args[command_argc] = path;
  args[command_argc + 1] = 0;

  int pid = fork();
  if (pid < 0) {
    fprintf(2, "find: fork failed\n");
    return 1;
  }
  if (pid == 0) {
    // Do not pass the traversal's open directory descriptors to the command.
    for (int fd = 3; fd < NOFILE; fd++)
      close(fd);
    exec(args[0], args);
    fprintf(2, "find: exec %s failed\n", args[0]);
    exit(1);
  }
  int status;
  if (wait(&status) < 0) {
    fprintf(2, "find: wait failed\n");
    return 1;
  }
  return status != 0;
}

// Read and close the directory before traversal. The caller owns the list.
static struct entry * __attribute__((noinline))
read_entries(int fd, char *path, int *failed)
{
  struct dirent de;
  struct entry *head = 0, **tail = &head;
  int n;

  while ((n = read(fd, &de, sizeof(de))) == sizeof(de)) {
    if (de.inum == 0)
      continue;
    char name[DIRSIZ + 1];
    memmove(name, de.name, DIRSIZ);
    name[DIRSIZ] = '\0';
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
      continue;

    struct entry *item = malloc(sizeof(*item));
    if (item == 0) {
      fprintf(2, "find: out of memory\n");
      *failed = 1;
      break;
    }
    strcpy(item->name, name);
    item->next = 0;
    *tail = item;
    tail = &item->next;
  }
  if (n != 0 && n != sizeof(de)) {
    fprintf(2, "find: cannot read directory %s\n", path);
    *failed = 1;
  }
  close(fd);
  return head;
}

static int
find(char *path, char *target, char **command, int command_argc)
{
  int fd, failed = 0;
  struct stat st;
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
  struct entry *head = 0;
  if (st.type == T_DIR)
    head = read_entries(fd, path, &failed);
  else
    close(fd);

  // Report directories too, before visiting their children.
  if (strcmp(name, target) == 0 && on_match(path, command, command_argc))
    failed = 1;

  int len = strlen(path);
  int separator = len > 0 && path[len - 1] != '/';
  while (head != 0) {
    struct entry *item = head;
    head = item->next;
    if (len + separator + strlen(item->name) + 1 > MAXPATH) {
      fprintf(2, "find: path too long: %s/%s\n", path, item->name);
      failed = 1;
      free(item);
      continue;
    }
    // Reuse main's path buffer to keep recursive stack frames small.
    if (separator)
      path[len] = '/';
    strcpy(path + len + separator, item->name);
    free(item);
    if (find(path, target, command, command_argc))
      failed = 1;
    path[len] = '\0';
  }
  return failed;
}

int
main(int argc, char *argv[])
{
  char path[MAXPATH];

  if (argc < 3 ||
      (argc > 3 && (argc < 5 || strcmp(argv[3], "-exec") != 0))) {
    fprintf(2, "usage: find <path> <name> [-exec <command> [args...]]\n");
    exit(1);
  }
  int command_argc = argc > 3 ? argc - 4 : 0;
  // Reserve entries for the matched path and the terminating null pointer.
  if (command_argc > MAXARG - 2) {
    fprintf(2, "find: too many command arguments\n");
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

  exit(find(path, argv[2], argc > 3 ? argv + 4 : 0, command_argc));
}
