#include "userprog/syscall.h"
#include "userprog/process.h"
#include "userprog/pagedir.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/synch.h"
#include "devices/serial.h"
#include "devices/vga.h"
#include "devices/shutdown.h"
#include "lib/string.h"

static void syscall_handler (struct intr_frame *);
tid_t sys_exec (const char *file);
int sys_wait (tid_t tid);
void sys_halt (void);
int check_bytes (void *start_, size_t size);
int check_args(uint32_t *args);
int check_string(const char *s);

static int argcs[16];

void
syscall_init (void)
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  argcs[SYS_PRACTICE] = 1;
  argcs[SYS_HALT] = 0;
  argcs[SYS_EXIT] = 1;
  argcs[SYS_EXEC] = 1;
  argcs[SYS_WAIT] = 1;
  /*
  args[SYS_CREATE] = ;
  args[SYS_REMOVE] = 1;
  args[SYS_OPEN] = 1;
  args[SYS_FILESIZE] = 1;
  args
  */
}

static void
syscall_handler (struct intr_frame *f UNUSED)
{
  uint32_t* args = ((uint32_t*) f->esp);
  if (!check_args(args)) {
    sys_exit(-1);
  }

  if (args[0] == SYS_WRITE) {
    int fd = args[1]; 
    void *buffer = (void*)args[2];
    unsigned size = args[3];
    int result =  sys_write(fd, buffer, size);
    f->eax = result;
  }

  if (args[0] == SYS_PRACTICE) {
    f->eax = args[1] + 1;
  }

  if (args[0] == SYS_EXEC) {
    char *file = (char*)args[1];
    f->eax = sys_exec (file);
  }

  if (args[0] == SYS_WAIT) {
    tid_t tid = args[1];
    f->eax = sys_wait (tid); 
  }

  if (args[0] == SYS_HALT) {
    sys_halt ();
  }

  if (args[0] == SYS_EXIT) {
    f->eax = args[1];
    sys_exit(args[1]);
  }
}

int
sys_write(int fd, void *buffer, unsigned size)
{
  if (fd < 0)
    return -1;
  if (!check_bytes ((void*)buffer, size))
    sys_exit(-1);

  unsigned cnt = 0;
  char *buf = (char*)buffer;

  if (fd == 1) {
    char *p = buf;
    while (cnt < size) {
      serial_putc(*p);
      vga_putc(*p);
      p++;
      cnt++;
    }
  }
  return (int)cnt;
}


tid_t
sys_exec(const char *file) {
  if (!check_string (file)) {
    sys_exit (-1);
  }
  return process_execute(file);
}

int
sys_wait (tid_t tid) {
  return process_wait (tid);
}

void
sys_halt () {
  shutdown_power_off ();
}

void
sys_exit(int status) {
  printf("%s: exit(%d)\n", (char*)(&thread_current ()->name), status); 
  thread_current ()->exit_status = status;
  thread_exit();
}


/* 检查系统调用参数数组是否全部位于用户地址空间
 * 并且在页表里有相应的映射 */
int
check_args(uint32_t *args) {
  /* 先检查第一个指针指向的元素是否越界 */
  if (!check_bytes((void*)args, 4))
    return 0;

  /* 第一个元素没有越界，解引用获取系统调用号 */
  int argc = argcs[args[0]];

  args += 1;
  if (!check_bytes((void*)args, sizeof (uint32_t) * argc))
    return 0;

  return 1;
}

int
check_string(const char *s) {
  const char *cp = s;
  void *p = NULL;
  while (check_bytes ((void*)cp, 1)) {
    p = next_page (cp);
    while ((void*)cp < p) {
      /* 找到终止符返回true */
      if (!(*cp))
	return true;
      cp++;
    }
  }
  return false;
}

/* 检查以start为起点，大小为size bytes的连续数据
 * 是否在用户地址空间，并且在页表有相应的映射 */
int
check_bytes (void *start, size_t size) {
  void *end = (void*)((uintptr_t)start + size); 
  if (end == NULL || end >= PHYS_BASE)
    return false;

  void *p = pg_round_down ((void*)start);
  while (p <= end) {
    if (!pagedir_get_page (thread_current ()->pagedir, p))
      return false;
    p = next_page (p);
  }
  return true;
}
