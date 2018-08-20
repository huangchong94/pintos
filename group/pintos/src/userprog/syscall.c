#include "userprog/syscall.h"
#include "userprog/process.h"
#include "userprog/pagedir.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/synch.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "devices/serial.h"
#include "devices/vga.h"
#include "devices/shutdown.h"
#include "devices/input.h"
#include "lib/string.h"
#include "threads/synch.h"

/* stupid global file lock 做p3时去掉它*/
static struct lock file_lock;
static void syscall_handler (struct intr_frame *);
tid_t sys_exec (const char *file);
int sys_wait (tid_t tid);
void sys_halt (void);
bool sys_create (const char *file, unsigned initial_size);
bool sys_remove (const char *file);
int sys_open (const char *file);
int sys_write (int fd, void *buffer, unsigned size);
int sys_read (int fd, void *buffer, unsigned size);
int sys_filesize (int fd);
void sys_seek (int fd, unsigned position);
unsigned sys_tell (int fd);
void sys_close (int fd);
int check_bytes (void *start_, size_t size);
int check_args(uint32_t *args);
int check_string(const char *s);

static int argcs[16];

void
syscall_init (void)
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  lock_init (&file_lock);
  argcs[SYS_PRACTICE] = 1;
  argcs[SYS_HALT] = 0;
  argcs[SYS_EXIT] = 1;
  argcs[SYS_EXEC] = 1;
  argcs[SYS_WAIT] = 1;

  argcs[SYS_CREATE] = 2;
  argcs[SYS_REMOVE] = 1;
  argcs[SYS_OPEN] = 1;
  argcs[SYS_WRITE] = 3;
  argcs[SYS_READ] = 3;
  argcs[SYS_CLOSE] = 1;
  argcs[SYS_FILESIZE] = 1;
  argcs[SYS_SEEK] = 2;
  argcs[SYS_TELL] = 1;
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

  if (args[0] == SYS_CREATE) {
    f->eax = sys_create ((char*)args[1], (unsigned)args[2]);
  }

  if (args[0] == SYS_REMOVE) {
    f->eax = sys_remove ((char*)args[1]);
  }

  if (args[0] == SYS_OPEN) {
    f->eax = sys_open ((char*)args[1]);
  }

  if (args[0] == SYS_READ) {
    int fd = args[1]; 
    void *buffer = (void*)args[2];
    unsigned size = args[3];
    f->eax =  sys_read (fd, buffer, size);
  }
  
  if (args[0] == SYS_FILESIZE) {
    f->eax = sys_filesize ((int)args[1]);
  }

  if (args[0] == SYS_SEEK) {
    sys_seek ((int)args[1], (unsigned)args[2]);
  }

  if (args[0] == SYS_TELL) {
    f->eax = sys_tell ((int)args[1]);
  }

  if (args[0] == SYS_CLOSE) {
    sys_close ((int)args[1]);
  }
}

bool
sys_create (const char *file, unsigned initial_size)
{
  if (!check_string (file))
    sys_exit (-1);
  
  lock_acquire (&file_lock);
  bool result = filesys_create (file, initial_size);
  lock_release (&file_lock);
  return result;
}

bool 
sys_remove (const char *file)
{
  if (!check_string (file))
    sys_exit (-1);

  lock_acquire (&file_lock);
  bool result = filesys_remove (file);
  lock_release (&file_lock);
  return result;
}


int
sys_open (const char *file)
{
  if (!check_string (file))
    sys_exit (-1);

  struct thread *t = thread_current ();
  if (t->open_cnt >= OPEN_CNT_MAX)
    return -1;
  if (!check_string (file))
    sys_exit (-1);

  lock_acquire (&file_lock);
  struct file *f = filesys_open (file);
  lock_release (&file_lock);
  if (!f)
    return -1;

  int i = 3;
  struct file **fd_table = t->fd_table;
  while (fd_table[i])
    i++;
  fd_table[i] = f;
  t->open_cnt += 1;
  return i; 
}

int
sys_write(int fd, void *buffer, unsigned size)
{
  if (fd <= 0 || fd == 2 || fd >= OPEN_CNT_MAX)
    return -1;

  if (!check_bytes (buffer, size))
    sys_exit(-1);

  if (fd == 1) {
    putbuf ((char*)buffer, size);
    return size;
  }

  struct file *f = thread_current ()->fd_table[fd];
  if (!f)
    return -1;

  lock_acquire (&file_lock);
  int result = file_write (f, buffer, size);
  lock_release (&file_lock);
  return result;
}

int
sys_read(int fd, void *buffer,  unsigned size)
{
  if (fd < 0 || fd == 1 || fd == 2 || fd >= OPEN_CNT_MAX)
    return -1;

  if (!check_bytes (buffer, size))
    sys_exit(-1);
  
  unsigned cnt = 0;
  char *buf = (char*)buffer;
  if (fd == 0) {
    while (cnt < size) {
      uint8_t c = input_getc ();
      buf[cnt] = c;
      cnt++;
    }
    return (int)cnt;
  }

  struct file *f = thread_current ()->fd_table[fd];
  if (!f)
    return -1;
  lock_acquire (&file_lock);
  int result = file_read (f, buffer, size);
  lock_release (&file_lock);
  return result;
}

int
sys_filesize (int fd) {
  if (fd < 3 || fd >= OPEN_CNT_MAX)
    return -1;
  struct file *f = thread_current ()->fd_table[fd];
  if (!f)
    return -1;
  lock_acquire (&file_lock);
  int result = file_length (f); 
  lock_release (&file_lock);
  return result;
}

void 
sys_seek (int fd, unsigned position)
{
  if (fd < 3 || fd >= OPEN_CNT_MAX)
    return;
  struct file *f = thread_current ()->fd_table[fd];
  if (!f)
    return;
  lock_acquire (&file_lock);
  file_seek (f, position);
  lock_release (&file_lock);
}

unsigned
sys_tell (int fd)
{
  if (fd < 3 || fd >= OPEN_CNT_MAX)
    return -1;
  struct file *f = thread_current ()->fd_table[fd];
  if (!f)
    return -1;
  lock_acquire (&file_lock);
  unsigned result = file_tell (f);
  lock_release (&file_lock);
  return result;
}

void
sys_close (int fd)
{
  if (fd < 3 || fd >= OPEN_CNT_MAX)
    return;

  struct thread *t = thread_current ();
  struct file **fd_table = t->fd_table;
  struct file *f = fd_table[fd];
  if (!f)
    return;
  fd_table[fd] = NULL;
  lock_acquire (&file_lock);
  file_close (f);
  lock_release (&file_lock);
  t->open_cnt -= 1;
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

