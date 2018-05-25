#include "userprog/syscall.h"
#include "userprog/process.h"
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
int check_pointer(void *p);
int check_args(uint32_t *args);
int check_string(const char *s);

void
syscall_init (void)
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f UNUSED)
{
  uint32_t* args = ((uint32_t*) f->esp);
  if (!check_args(args))
    sys_exit(-1);

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
  if (!check_string ((char*)buffer))
    sys_exit(-1);

  unsigned cnt = 0;
  char *buf = (char*)buffer;

  if (fd == 1) {
    char *p = buf;
    while (cnt < size) {
      if (!check_pointer(p))
	return -1;
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
  /* 注意这里为什么在这里检查，而不让page_fault去处理可能非法指针访问
   * 是因为如果指针非法process_execute在page_fault之前会分配资源
   * 而page_fault的实现只是简单的exit，这样会资源泄露 */
  if (!check_string (file))
    sys_exit (-1);
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

int
check_pointer(void *p) {
  uint32_t addr = (uint32_t)p;
  return 0 < addr && addr < (uint32_t)PHYS_BASE;
}

/* 检查系统调用参数数组是否全部位于用户地址空间 */
int
check_args(uint32_t *args) {
  /* 先检查第一个指针指向的元素是否越界 */
  if (!check_pointer(args))
    return 0;
  args += 1;
  if (!check_pointer((char*)args - 1))
    return 0;

  /* 第一个元素没有越界，解引用获取系统调用号 */
  int argc;
  unsigned num = args[0];
  if (num == SYS_WRITE)
    argc = 3;
  if (num == SYS_PRACTICE || num == SYS_EXIT)
    argc = 1;

  args += argc;

  if (!check_pointer((char*)args - 1))
    return 0;

  return 1;
}

int
check_string(const char *s) {
  return check_pointer ((void*)(s + strlen(s) + 1));
}
