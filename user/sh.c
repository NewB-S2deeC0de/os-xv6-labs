// Shell.

#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fcntl.h"
#include "kernel/fs.h"

// Parsed command representation
#define EXEC  1
#define REDIR 2
#define PIPE  3
#define LIST  4
#define BACK  5

#define MAXARGS 10

struct cmd {
  int type;
};

struct execcmd {
  int type;
  char *argv[MAXARGS];
  char *eargv[MAXARGS];
};

struct redircmd {
  int type;
  struct cmd *cmd;
  char *file;
  char *efile;
  int mode;
  int fd;
};

struct pipecmd {
  int type;
  struct cmd *left;
  struct cmd *right;
};

struct listcmd {
  int type;
  struct cmd *left;
  struct cmd *right;
};

struct backcmd {
  int type;
  struct cmd *cmd;
};

int fork1(void); // Fork but panics on failure.
void panic(char *);
struct cmd *parsecmd(char *);
void runcmd(struct cmd *) __attribute__((noreturn));

// Execute cmd.  Never returns.
void
runcmd(struct cmd *cmd)
{
  int p[2];
  struct backcmd *bcmd;
  struct execcmd *ecmd;
  struct listcmd *lcmd;
  struct pipecmd *pcmd;
  struct redircmd *rcmd;

  if (cmd == 0)
    exit(1);

  switch (cmd->type) {
  default:
    panic("runcmd");

  case EXEC:
    ecmd = (struct execcmd *)cmd;
    if (ecmd->argv[0] == 0)
      exit(1);
    exec(ecmd->argv[0], ecmd->argv);
    if (ecmd->argv[0][0] != '/' && strlen(ecmd->argv[0]) < 60) {
      char path[64];
      path[0] = '/';
      strcpy(path + 1, ecmd->argv[0]);
      exec(path, ecmd->argv);
    }
    fprintf(2, "exec %s failed\n", ecmd->argv[0]);
    break;

  case REDIR:
    rcmd = (struct redircmd *)cmd;
    close(rcmd->fd);
    if (open(rcmd->file, rcmd->mode) < 0) {
      fprintf(2, "open %s failed\n", rcmd->file);
      exit(1);
    }
    runcmd(rcmd->cmd);
    break;

  case LIST:
    lcmd = (struct listcmd *)cmd;
    if (fork1() == 0)
      runcmd(lcmd->left);
    wait(0);
    runcmd(lcmd->right);
    break;

  case PIPE:
    pcmd = (struct pipecmd *)cmd;
    if (pipe(p) < 0)
      panic("pipe");
    if (fork1() == 0) {
      close(1);
      dup(p[1]);
      close(p[0]);
      close(p[1]);
      runcmd(pcmd->left);
    }
    if (fork1() == 0) {
      close(0);
      dup(p[0]);
      close(p[0]);
      close(p[1]);
      runcmd(pcmd->right);
    }
    close(p[0]);
    close(p[1]);
    wait(0);
    wait(0);
    break;

  case BACK:
    bcmd = (struct backcmd *)cmd;
    if (fork1() == 0)
      runcmd(bcmd->cmd);
    break;
  }
  exit(0);
}

#define MAX_MATCHES 32

static int
starts_with(const char *s, const char *prefix, int prefix_len)
{
  if (strlen(s) < prefix_len)
    return 0;
  return memcmp(s, prefix, prefix_len) == 0;
}

static void
tab_complete(char *buf, int *len, int nbuf)
{
  int i = *len;
  while (i > 0 && buf[i - 1] != ' ' && buf[i - 1] != '\t') {
    i--;
  }

  char prefix[DIRSIZ + 1];
  int prefix_len = *len - i;
  if (prefix_len <= 0)
    return;
  if (prefix_len > DIRSIZ)
    prefix_len = DIRSIZ;
  memmove(prefix, buf + i, prefix_len);
  prefix[prefix_len] = '\0';

  // Check if this is the first word on the command line
  int start = 0;
  while (start < i && (buf[start] == ' ' || buf[start] == '\t')) {
    start++;
  }
  int is_first_word = (start == i);

  char *search_prefix = prefix;
  int search_len = prefix_len;
  if (is_first_word && prefix[0] == '/') {
    search_prefix = prefix + 1;
    search_len = prefix_len - 1;
  }

  char *dir = is_first_word ? "/" : ".";
  int fd = open(dir, O_RDONLY);
  if (fd < 0)
    return;

  char matches[MAX_MATCHES][DIRSIZ + 1];
  int match_count = 0;
  struct dirent de;

  while (read(fd, &de, sizeof(de)) == sizeof(de)) {
    if (de.inum == 0)
      continue;

    char name[DIRSIZ + 1];
    memmove(name, de.name, DIRSIZ);
    name[DIRSIZ] = '\0';

    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
      continue;

    if (starts_with(name, search_prefix, search_len)) {
      if (match_count < MAX_MATCHES) {
        int dup = 0;
        for (int k = 0; k < match_count; k++) {
          if (strcmp(matches[k], name) == 0) {
            dup = 1;
            break;
          }
        }
        if (!dup) {
          strcpy(matches[match_count++], name);
        }
      }
    }
  }
  close(fd);

  if (match_count == 0)
    return;

  // Sort matches alphabetically
  for (int m = 0; m < match_count - 1; m++) {
    for (int k = m + 1; k < match_count; k++) {
      if (strcmp(matches[m], matches[k]) > 0) {
        char tmp[DIRSIZ + 1];
        strcpy(tmp, matches[m]);
        strcpy(matches[m], matches[k]);
        strcpy(matches[k], tmp);
      }
    }
  }

  if (match_count == 1) {
    if (is_first_word) {
      // Replace typed prefix on screen with "/<matches[0]> "
      for (int k = 0; k < prefix_len; k++) {
        write(2, "\b \b", 3);
      }
      char completion[DIRSIZ + 4];
      completion[0] = '/';
      strcpy(completion + 1, matches[0]);
      int clen = strlen(completion);
      completion[clen] = ' ';
      completion[clen + 1] = '\0';
      clen++;

      if (i + clen < nbuf) {
        write(2, completion, clen);
        strcpy(buf + i, completion);
        *len = i + clen;
      }
    } else {
      char completion[DIRSIZ + 2];
      strcpy(completion, matches[0] + prefix_len);
      int compl_len = strlen(completion);
      if (*len + compl_len < nbuf) {
        write(2, completion, compl_len);
        strcpy(buf + *len, completion);
        *len += compl_len;
      }
    }
  } else {
    // Multiple matches
    write(2, "\n", 1);
    for (int m = 0; m < match_count; m++) {
      if (m > 0)
        write(2, " ", 1);
      write(2, matches[m], strlen(matches[m]));
    }
    write(2, "\n$ ", 3);
    write(2, buf, *len);
  }
}

#define KEY_UP    0xE2
#define KEY_DOWN  0xE3
#define MAX_HIST  32
#define HIST_CMD_SIZE 100

static char history[MAX_HIST][HIST_CMD_SIZE];
static int hist_count = 0; // Total count of commands recorded (1-indexed)

int
getcmd(char *buf, int nbuf)
{
  struct stat st;
  int is_console = (fstat(0, &st) == 0 && st.type == T_DEVICE);

  memset(buf, 0, nbuf);

  if (!is_console) {
    gets(buf, nbuf);
    if (buf[0] == 0) // EOF
      return -1;
    return 0;
  }

  rawmode(1);
  write(2, "$ ", 2);

#define C(x) ((x) - '@')

  int len = 0;
  int hist_nav = hist_count; // Navigation starts at the end (current line)
  char temp_buf[HIST_CMD_SIZE];
  temp_buf[0] = '\0';

  while (len + 1 < nbuf) {
    char c;
    int cc = read(0, &c, 1);
    if (cc < 1) {
      rawmode(0);
      if (len == 0)
        return -1; // EOF
      break;
    }

    if ((uchar)c == (uchar)KEY_UP) {
      int min_nav = (hist_count > MAX_HIST) ? (hist_count - MAX_HIST) : 0;
      if (hist_nav > min_nav) {
        if (hist_nav == hist_count) {
          memmove(temp_buf, buf, len);
          temp_buf[len] = '\0';
        }
        hist_nav--;

        // Erase current line on screen
        for (int k = 0; k < len; k++) {
          write(2, "\b \b", 3);
        }

        // Copy history entry into buf
        char *hcmd = history[hist_nav % MAX_HIST];
        len = strlen(hcmd);
        if (len >= nbuf)
          len = nbuf - 1;
        memmove(buf, hcmd, len);
        buf[len] = '\0';

        write(2, buf, len);
      }
      continue;
    }

    if ((uchar)c == (uchar)KEY_DOWN) {
      if (hist_nav < hist_count) {
        hist_nav++;

        // Erase current line on screen
        for (int k = 0; k < len; k++) {
          write(2, "\b \b", 3);
        }

        if (hist_nav < hist_count) {
          char *hcmd = history[hist_nav % MAX_HIST];
          len = strlen(hcmd);
          if (len >= nbuf)
            len = nbuf - 1;
          memmove(buf, hcmd, len);
          buf[len] = '\0';
        } else {
          len = strlen(temp_buf);
          if (len >= nbuf)
            len = nbuf - 1;
          memmove(buf, temp_buf, len);
          buf[len] = '\0';
        }

        write(2, buf, len);
      }
      continue;
    }

    if (c == '\b') {
      if (len > 0) {
        len--;
        buf[len] = '\0';
        write(2, "\b \b", 3);
      }
      continue;
    }

    if (c == C('U') || c == '\x15') {
      while (len > 0) {
        len--;
        write(2, "\b \b", 3);
      }
      buf[0] = '\0';
      hist_nav = hist_count;
      temp_buf[0] = '\0';
      continue;
    }
    if (c == '\t') {
      tab_complete(buf, &len, nbuf);
      continue;
    }

    if (c >= 32 && c <= 126) {
      buf[len++] = c;
      buf[len] = '\0';
    } else if (c == '\n' || c == '\r') {
      buf[len++] = '\n';
      buf[len] = '\0';
      break;
    }
  }

  rawmode(0);

  if (len == 0)
    return -1;
  return 0;
}

int
main(void)
{
  static char buf[100];
  int fd;

  // Ensure that three file descriptors are open.
  while ((fd = open("console", O_RDWR)) >= 0) {
    if (fd >= 3) {
      close(fd);
      break;
    }
  }

  // Read and run input commands.
  while (getcmd(buf, sizeof(buf)) >= 0) {
    char *cmd = buf;
    while (*cmd == ' ' || *cmd == '\t')
      cmd++;
    if (*cmd == '\n' || *cmd == '\0') // is a blank command
      continue;

    // Chop trailing newline and whitespace
    int cmdlen = strlen(cmd);
    while (cmdlen > 0 && (cmd[cmdlen - 1] == '\n' || cmd[cmdlen - 1] == '\r' ||
                          cmd[cmdlen - 1] == ' ' || cmd[cmdlen - 1] == '\t')) {
      cmd[--cmdlen] = '\0';
    }

    // Sanitize cmd: filter only printable ASCII characters (32 to 126) and Tab ('\t')
    int w = 0;
    for (int r = 0; r < cmdlen; r++) {
      if ((cmd[r] >= 32 && cmd[r] <= 126) || cmd[r] == '\t') {
        cmd[w++] = cmd[r];
      }
    }
    cmd[w] = '\0';
    cmdlen = w;
    if (cmdlen == 0)
      continue;

    // Check for !! and !n expansion
    static char expanded_cmd[HIST_CMD_SIZE];
    if (cmd[0] == '!') {
      int found = 0;
      if (cmd[1] == '!' && cmd[2] == '\0') {
        if (hist_count == 0) {
          fprintf(2, "!!: event not found\n");
          continue;
        }
        strcpy(expanded_cmd, history[(hist_count - 1) % MAX_HIST]);
        found = 1;
      } else if (cmd[1] >= '0' && cmd[1] <= '9') {
        int num = atoi(cmd + 1);
        int min_id = (hist_count > MAX_HIST) ? (hist_count - MAX_HIST + 1) : 1;
        if (num >= min_id && num <= hist_count) {
          strcpy(expanded_cmd, history[(num - 1) % MAX_HIST]);
          found = 1;
        } else {
          fprintf(2, "!%d: event not found\n", num);
          continue;
        }
      }

      if (found) {
        printf("%s\n", expanded_cmd); // Shell in lệnh ra trước khi chạy
        cmd = expanded_cmd;
      }
    }

    // Add command to history
    strcpy(history[hist_count % MAX_HIST], cmd);
    hist_count++;

    // Built-in history command
    if (strcmp(cmd, "history") == 0) {
      int start_id = (hist_count > MAX_HIST) ? (hist_count - MAX_HIST + 1) : 1;
      for (int k = start_id; k <= hist_count; k++) {
        printf("%d %s\n", k, history[(k - 1) % MAX_HIST]);
      }
      continue;
    }

    // Built-in cd command
    if (cmd[0] == 'c' && cmd[1] == 'd' && (cmd[2] == ' ' || cmd[2] == '\t' || cmd[2] == '\0')) {
      char *path = cmd + 2;
      while (*path == ' ' || *path == '\t')
        path++;
      if (*path == '\0')
        path = "/";
      if (chdir(path) < 0)
        fprintf(2, "cannot cd %s\n", path);
      continue;
    }

    if (fork1() == 0)
      runcmd(parsecmd(cmd));
    wait(0);
  }
  exit(0);
}

void
panic(char *s)
{
  fprintf(2, "%s\n", s);
  exit(1);
}

int
fork1(void)
{
  int pid;

  pid = fork();
  if (pid == -1)
    panic("fork");
  return pid;
}

//PAGEBREAK!
// Constructors

struct cmd *
execcmd(void)
{
  struct execcmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = EXEC;
  return (struct cmd *)cmd;
}

struct cmd *
redircmd(struct cmd *subcmd, char *file, char *efile, int mode, int fd)
{
  struct redircmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = REDIR;
  cmd->cmd = subcmd;
  cmd->file = file;
  cmd->efile = efile;
  cmd->mode = mode;
  cmd->fd = fd;
  return (struct cmd *)cmd;
}

struct cmd *
pipecmd(struct cmd *left, struct cmd *right)
{
  struct pipecmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = PIPE;
  cmd->left = left;
  cmd->right = right;
  return (struct cmd *)cmd;
}

struct cmd *
listcmd(struct cmd *left, struct cmd *right)
{
  struct listcmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = LIST;
  cmd->left = left;
  cmd->right = right;
  return (struct cmd *)cmd;
}

struct cmd *
backcmd(struct cmd *subcmd)
{
  struct backcmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = BACK;
  cmd->cmd = subcmd;
  return (struct cmd *)cmd;
}
//PAGEBREAK!
// Parsing

char whitespace[] = " \t\r\n\v";
char symbols[] = "<|>&;()";

int
gettoken(char **ps, char *es, char **q, char **eq)
{
  char *s;
  int ret;

  s = *ps;
  while (s < es && strchr(whitespace, *s))
    s++;
  if (q)
    *q = s;
  ret = *s;
  switch (*s) {
  case 0:
    break;
  case '|':
  case '(':
  case ')':
  case ';':
  case '&':
  case '<':
    s++;
    break;
  case '>':
    s++;
    if (*s == '>') {
      ret = '+';
      s++;
    }
    break;
  default:
    ret = 'a';
    while (s < es && !strchr(whitespace, *s) && !strchr(symbols, *s))
      s++;
    break;
  }
  if (eq)
    *eq = s;

  while (s < es && strchr(whitespace, *s))
    s++;
  *ps = s;
  return ret;
}

int
peek(char **ps, char *es, char *toks)
{
  char *s;

  s = *ps;
  while (s < es && strchr(whitespace, *s))
    s++;
  *ps = s;
  return *s && strchr(toks, *s);
}

struct cmd *parseline(char **, char *);
struct cmd *parsepipe(char **, char *);
struct cmd *parseexec(char **, char *);
struct cmd *nulterminate(struct cmd *);

struct cmd *
parsecmd(char *s)
{
  char *es;
  struct cmd *cmd;

  es = s + strlen(s);
  cmd = parseline(&s, es);
  peek(&s, es, "");
  if (s != es) {
    fprintf(2, "leftovers: %s\n", s);
    panic("syntax");
  }
  nulterminate(cmd);
  return cmd;
}

struct cmd *
parseline(char **ps, char *es)
{
  struct cmd *cmd;

  cmd = parsepipe(ps, es);
  while (peek(ps, es, "&")) {
    gettoken(ps, es, 0, 0);
    cmd = backcmd(cmd);
  }
  if (peek(ps, es, ";")) {
    gettoken(ps, es, 0, 0);
    cmd = listcmd(cmd, parseline(ps, es));
  }
  return cmd;
}

struct cmd *
parsepipe(char **ps, char *es)
{
  struct cmd *cmd;

  cmd = parseexec(ps, es);
  if (peek(ps, es, "|")) {
    gettoken(ps, es, 0, 0);
    cmd = pipecmd(cmd, parsepipe(ps, es));
  }
  return cmd;
}

struct cmd *
parseredirs(struct cmd *cmd, char **ps, char *es)
{
  int tok;
  char *q, *eq;

  while (peek(ps, es, "<>")) {
    tok = gettoken(ps, es, 0, 0);
    if (gettoken(ps, es, &q, &eq) != 'a')
      panic("missing file for redirection");
    switch (tok) {
    case '<':
      cmd = redircmd(cmd, q, eq, O_RDONLY, 0);
      break;
    case '>':
      cmd = redircmd(cmd, q, eq, O_WRONLY | O_CREATE | O_TRUNC, 1);
      break;
    case '+': // >>
      cmd = redircmd(cmd, q, eq, O_WRONLY | O_CREATE, 1);
      break;
    }
  }
  return cmd;
}

struct cmd *
parseblock(char **ps, char *es)
{
  struct cmd *cmd;

  if (!peek(ps, es, "("))
    panic("parseblock");
  gettoken(ps, es, 0, 0);
  cmd = parseline(ps, es);
  if (!peek(ps, es, ")"))
    panic("syntax - missing )");
  gettoken(ps, es, 0, 0);
  cmd = parseredirs(cmd, ps, es);
  return cmd;
}

struct cmd *
parseexec(char **ps, char *es)
{
  char *q, *eq;
  int tok, argc;
  struct execcmd *cmd;
  struct cmd *ret;

  if (peek(ps, es, "("))
    return parseblock(ps, es);

  ret = execcmd();
  cmd = (struct execcmd *)ret;

  argc = 0;
  ret = parseredirs(ret, ps, es);
  while (!peek(ps, es, "|)&;")) {
    if ((tok = gettoken(ps, es, &q, &eq)) == 0)
      break;
    if (tok != 'a')
      panic("syntax");
    cmd->argv[argc] = q;
    cmd->eargv[argc] = eq;
    argc++;
    if (argc >= MAXARGS)
      panic("too many args");
    ret = parseredirs(ret, ps, es);
  }
  cmd->argv[argc] = 0;
  cmd->eargv[argc] = 0;
  return ret;
}

// NUL-terminate all the counted strings.
struct cmd *
nulterminate(struct cmd *cmd)
{
  int i;
  struct backcmd *bcmd;
  struct execcmd *ecmd;
  struct listcmd *lcmd;
  struct pipecmd *pcmd;
  struct redircmd *rcmd;

  if (cmd == 0)
    return 0;

  switch (cmd->type) {
  case EXEC:
    ecmd = (struct execcmd *)cmd;
    for (i = 0; ecmd->argv[i]; i++)
      *ecmd->eargv[i] = 0;
    break;

  case REDIR:
    rcmd = (struct redircmd *)cmd;
    nulterminate(rcmd->cmd);
    *rcmd->efile = 0;
    break;

  case PIPE:
    pcmd = (struct pipecmd *)cmd;
    nulterminate(pcmd->left);
    nulterminate(pcmd->right);
    break;

  case LIST:
    lcmd = (struct listcmd *)cmd;
    nulterminate(lcmd->left);
    nulterminate(lcmd->right);
    break;

  case BACK:
    bcmd = (struct backcmd *)cmd;
    nulterminate(bcmd->cmd);
    break;
  }
  return cmd;
}
