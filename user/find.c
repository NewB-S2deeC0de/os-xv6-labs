#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/param.h"
#include "kernel/fs.h"
#include "kernel/fcntl.h"
#include "user/user.h"

struct entry {
  char path[MAXPATH];
  struct entry *next;
};

static int
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

// Prepend children in directory order so the work list retains DFS order.
// Every queued entry owns its path; no directory stays open between jobs.
static struct entry *
read_entries(int fd, char *path, struct entry *pending, int *failed)
{
  struct dirent de;
  struct entry *head = 0, **tail = &head;
  int len = strlen(path);
  int separator = len > 0 && path[len - 1] != '/';
  int n;

  while ((n = read(fd, &de, sizeof(de))) == sizeof(de)) {
    if (de.inum == 0)
      continue;
    char name[DIRSIZ + 1];
    memmove(name, de.name, DIRSIZ);
    name[DIRSIZ] = '\0';
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
      continue;
    if (len + separator + strlen(name) + 1 > MAXPATH) {
      fprintf(2, "find: path too long: %s/%s\n", path, name);
      *failed = 1;
      continue;
    }

    struct entry *item = malloc(sizeof(*item));
    if (item == 0) {
      fprintf(2, "find: out of memory\n");
      *failed = 1;
      break;
    }
    strcpy(item->path, path);
    if (separator)
      item->path[len] = '/';
    strcpy(item->path + len + separator, name);
    item->next = 0;
    *tail = item;
    tail = &item->next;
  }
  if (n != 0 && n != sizeof(de)) {
    fprintf(2, "find: cannot read directory %s\n", path);
    *failed = 1;
  }
  close(fd);
  *tail = pending;
  return head;
}

static int
find(char *path, char *target, char **command, int command_argc)
{
  int failed = 0;
  struct entry *pending = malloc(sizeof(*pending));
  if (pending == 0) {
    fprintf(2, "find: out of memory\n");
    return 1;
  }
  strcpy(pending->path, path);
  pending->next = 0;

  while (pending != 0) {
    struct entry *item = pending;
    pending = item->next;
    path = item->path;
    struct stat st;
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
      fprintf(2, "find: cannot open %s\n", path);
      failed = 1;
      free(item);
      continue;
    }
    if (fstat(fd, &st) < 0) {
      fprintf(2, "find: cannot stat %s\n", path);
      close(fd);
      failed = 1;
      free(item);
      continue;
    }

    if (st.type == T_DIR)
      pending = read_entries(fd, path, pending, &failed);
    else
      close(fd);

    char *name = path;
    for (char *p = path; *p; p++) {
      if (*p == '/')
        name = p + 1;
    }
    // Process a matching directory before its queued children. Keep its path
    // alive until on_match has waited for the command to finish.
    if (strcmp(name, target) == 0 && on_match(path, command, command_argc))
      failed = 1;
    free(item);
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
