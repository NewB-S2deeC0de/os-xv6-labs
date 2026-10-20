//
// Console input and output, to the uart.
// Reads are line at a time.
// Implements special input characters:
//   newline -- end of line
//   control-h -- backspace
//   control-u -- kill line
//   control-d -- end of file
//   control-p -- print process list
//

#include <stdarg.h>

#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "file.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"
#include "proc.h"

#define BACKSPACE 0x100       // erase the last output character
#define C(x)      ((x) - '@') // Control-x
#define KEY_UP    0xE2        // Up-arrow sentinel byte forwarded to user-space
#define KEY_DOWN  0xE3        // Down-arrow sentinel byte forwarded to user-space

//
// send one character to the uart, but don't use
// interrupts or sleep(). safe to be called from
// interrupts, e.g. by printk and to echo input
// characters.
//
void
consputc(int c)
{
  if (c == BACKSPACE) {
    // if the user typed backspace, overwrite with a space.
    uartputc_sync('\b');
    uartputc_sync(' ');
    uartputc_sync('\b');
  } else {
    uartputc_sync(c);
  }
}

struct {
  struct spinlock lock;

  // input circular buffer
#define INPUT_BUF_SIZE 128
  char buf[INPUT_BUF_SIZE];
  uint r; // Read index
  uint w; // Write index
  uint e; // Edit index
  int raw_pid; // PID of process that enabled raw/interactive console mode
} cons;

int
consoleraw(int enable)
{
  acquire(&cons.lock);
  if (enable) {
    cons.raw_pid = myproc()->pid;
  } else {
    if (cons.raw_pid == myproc()->pid) {
      cons.raw_pid = 0;
    }
  }
  release(&cons.lock);
  return 0;
}

void
consoleraw_cleanup(int pid)
{
  acquire(&cons.lock);
  if (cons.raw_pid == pid) {
    cons.raw_pid = 0;
  }
  release(&cons.lock);
}

//
// user write() system calls to the console go here.
// uses sleep() and UART interrupts.
//
int
consolewrite(int user_src, uint64 src, int n)
{
  char buf[32]; // move batches from user space to uart.
  int i = 0;

  while (i < n) {
    int nn = sizeof(buf);
    if (nn > n - i)
      nn = n - i;
    if (either_copyin(buf, user_src, src + i, nn) == -1)
      break;
    uartwrite(buf, nn);
    i += nn;
  }

  return i;
}

//
// user read()s from the console go here.
// copy (up to) a whole input line to dst.
// user_dst indicates whether dst is a user
// or kernel address.
//
int
consoleread(int user_dst, uint64 dst, int n)
{
  uint target;
  int c;
  char cbuf;

  target = n;
  acquire(&cons.lock);
  while (n > 0) {
    // wait until interrupt handler has put some
    // input into cons.buffer.
    while (cons.r == cons.w) {
      if (killed(myproc())) {
        release(&cons.lock);
        return -1;
      }
      sleep_prepare(&cons.r);
      release(&cons.lock);
      sleep();
      acquire(&cons.lock);
    }

    c = cons.buf[cons.r++ % INPUT_BUF_SIZE];

    if (c == C('D')) { // end-of-file
      if (n < target) {
        // Save ^D for next time, to make sure
        // caller gets a 0-byte result.
        cons.r--;
      }
      break;
    }

    // copy the input byte to the user-space buffer.
    cbuf = c;
    if (either_copyout(user_dst, dst, &cbuf, 1) == -1)
      break;

    dst++;
    --n;

    if (c == '\n') {
      break;
    }

    // In raw mode for shell: return immediately on special control keys
    if (cons.raw_pid != 0) {
      if (c == '\t' || c == '\b' || c == '\x15' ||
          (uchar)c == (uchar)KEY_UP || (uchar)c == (uchar)KEY_DOWN) {
        break;
      }
    }
  }
  release(&cons.lock);

  return target - n;
}

//
// the console input interrupt handler.
// uartintr() calls this for each input character.
// do erase/kill processing, append to cons.buf,
// wake up consoleread() if a whole line has arrived.
//
void
consoleintr(int c)
{
  acquire(&cons.lock);

  // --- ANSI escape sequence state machine ---
  // Intercept arrow-key sequences (\x1b [ A/B) without echoing them.
  // Forwards KEY_UP / KEY_DOWN sentinel bytes to user-space instead.
  static int esc_state = 0;

  if (esc_state == 1) {
    esc_state = (c == '[') ? 2 : 0;
    if (esc_state == 0) {
      // Not a CSI sequence; discard the stray ESC + this byte.
      release(&cons.lock);
      return;
    }
    release(&cons.lock);
    return;
  } else if (esc_state == 2) {
    // Parameter bytes (digits, semicolons) inside an escape sequence.
    if ((c >= '0' && c <= '9') || c == ';') {
      release(&cons.lock);
      return;
    }
    esc_state = 0;
    // Only handle Up (A) and Down (B) when interactive raw mode is active.
    if ((c == 'A' || c == 'B') && cons.raw_pid != 0) {
      char key = (c == 'A') ? (char)KEY_UP : (char)KEY_DOWN;
      if (cons.e - cons.r < INPUT_BUF_SIZE) {
        cons.buf[cons.e++ % INPUT_BUF_SIZE] = key;
        cons.w = cons.e;
        wakeup(&cons.r);
      }
    }
    release(&cons.lock);
    return;
  }

  if (c == 0x1b) { // ESC — start of an escape sequence
    esc_state = 1;
    release(&cons.lock);
    return;
  }
  // ------------------------------------------

  switch (c) {
  case C('P'): // Print process list.
    procdump();
    break;

  case C('U'): // Kill line.
    // Phase 1: erase what the user typed into the kernel's editable region.
    while (cons.e != cons.w &&
           cons.buf[(cons.e - 1) % INPUT_BUF_SIZE] != '\n') {
      cons.e--;
      consputc(BACKSPACE);
    }
    // Phase 2: forward '\x15' ONLY when raw interactive mode is active for shell
    // (so shell can erase completion text in its own buffer).
    // In cooked mode (cat, keytest, ctrlutest), never emit raw byte 21.
    if (cons.raw_pid != 0) {
      if (cons.e - cons.r < INPUT_BUF_SIZE) {
        cons.buf[cons.e++ % INPUT_BUF_SIZE] = '\x15';
        cons.w = cons.e;
        wakeup(&cons.r);
      }
    }
    break;

  case C('H'): // Backspace (Ctrl-H)
  case '\x7f': // Delete key
    if (cons.e != cons.w) {
      // Normal case: user typed some chars; kernel erases the last one.
      cons.e--;
      consputc(BACKSPACE);
    } else if (cons.raw_pid != 0) {
      // Special case: editable region is empty (cons.e == cons.w) AND raw
      // mode is active for shell. Forward '\b' to shell to erase completion text.
      // For all other programs, do not emit stray '\b' on an empty line.
      if (cons.e - cons.r < INPUT_BUF_SIZE) {
        cons.buf[cons.e++ % INPUT_BUF_SIZE] = '\b';
        cons.w = cons.e;
        wakeup(&cons.r);
      }
    }
    break;

  case '\t': // Tab key
    if (cons.raw_pid != 0) {
      // Raw mode (shell): commit immediately so shell can call tab_complete().
      if (cons.e - cons.r < INPUT_BUF_SIZE) {
        cons.buf[cons.e++ % INPUT_BUF_SIZE] = '\t';
        cons.w = cons.e;
        wakeup(&cons.r);
      }
    } else {
      // Cooked mode: standard xv6 behavior, echo and buffer without waking reader.
      if (cons.e - cons.r < INPUT_BUF_SIZE) {
        consputc('\t');
        cons.buf[cons.e++ % INPUT_BUF_SIZE] = '\t';
      }
    }
    break;

  default:
    if (c != 0 && cons.e - cons.r < INPUT_BUF_SIZE) {
      // Accept only printable ASCII (32–126), carriage return, newline,
      // and Ctrl-D.  This blocks stray control bytes from IME / dead keys.
      if (c == '\r' || c == '\n' || c == C('D') || (c >= 32 && c <= 126)) {
        c = (c == '\r') ? '\n' : c;

        // echo back to the user.
        consputc(c);

        // store for consumption by consoleread().
        cons.buf[cons.e++ % INPUT_BUF_SIZE] = c;

        if (c == '\n' || c == C('D') || cons.e - cons.r == INPUT_BUF_SIZE) {
          // wake up consoleread() if a whole line (or end-of-file) arrived.
          cons.w = cons.e;
          wakeup(&cons.r);
        }
      }
    }
    break;
  }

  release(&cons.lock);
}

void
consoleinit(void)
{
  initlock(&cons.lock, "cons");
  cons.raw_pid = 0;

  uartinit();

  // connect read and write system calls
  // to consoleread and consolewrite.
  devsw[CONSOLE].read = consoleread;
  devsw[CONSOLE].write = consolewrite;
}
