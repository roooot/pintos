#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "userprog/process.h"
#include "threads/interrupt.h"
#include "threads/init.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "devices/input.h"

/* Process identifier. */
typedef int pid_t;

/* File identifier. */
typedef int fid_t;

static void syscall_handler (struct intr_frame *);

static void      sys_halt (void);
static void      sys_exit (int status);
static pid_t     sys_exec (const char *file);
static int       sys_wait (pid_t pid);
static bool      sys_create (const char *file, unsigned initial_size);
static bool      sys_remove (const char *file);
static int       sys_open (const char *file);
static int       sys_filesize (int fd);
static int       sys_read (int fd, void *buffer, unsigned size);
static int       sys_write (int fd, const void *buffer, unsigned size);
static void      sys_seek (int fd, unsigned position);
static unsigned  sys_tell (int fd);
static void      sys_close (int fd);

struct user_file
  {
    struct file *file;                 /* Pointer to the actual file. */
    fid_t fid;                         /* File identifier. */
    struct list_elem thread_elem;      /* List elem for a thread's file list. */
  };

static struct lock file_lock;
static struct list file_list;

static fid_t allocate_fid (void);
static struct user_file *file_by_fid (int fid);

/* Initialization of syscall handlers */
void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");

  lock_init (&file_lock);
  list_init (&file_list);
}

/* Syscall handler calls the appropriate function. */
static void
syscall_handler (struct intr_frame *f) 
{
  int ret = 0;
  int *syscall_nr = (int *) f->esp;
  void *arg1 = (int *) f->esp + 1;
  void *arg2 = (int *) f->esp + 2;
  void *arg3 = (int *) f->esp + 3;

  /* Check validate pointer. */
  if (!is_user_vaddr (syscall_nr) || !is_user_vaddr (arg1) ||
      !is_user_vaddr (arg2)       || !is_user_vaddr (arg3))
    sys_exit (-1);
  
  switch (*syscall_nr)
    {
    case SYS_HALT:
      sys_halt ();
      break;
    case SYS_EXIT:
      sys_exit (*(int *) arg1);
      break;
    case SYS_EXEC:
      ret = sys_exec (*(char **) arg1);
      break;
    case SYS_WAIT:
      ret = sys_wait (*(pid_t *) arg1);
      break;
    case SYS_CREATE:
      ret = sys_create (*(char **) arg1, *(unsigned *) arg2);
      break;
    case SYS_REMOVE:
      ret = sys_remove (*(char **) arg1);
      break;
    case SYS_OPEN:
      ret = sys_open (*(char **) arg1);
      break;
    case SYS_FILESIZE:
      ret = sys_filesize (*(int *) arg1);
      break;
    case SYS_READ:
      ret = sys_read (*(int *) arg1, *(void **) arg2, *(unsigned *) arg3);
      break;
    case SYS_WRITE:
      ret = sys_write (*(int *) arg1, *(void **) arg2, *(unsigned *) arg3);
      break;
    case SYS_SEEK:
      sys_seek (*(int *) arg1, *(unsigned *) arg2);
      break;
    case SYS_TELL:
      ret = sys_tell (*(int *) arg1);
      break;
    case SYS_CLOSE:
      sys_close (*(int *) arg1);
      break;
    default:
      printf (" (%s) system call! (%d)\n", thread_name (), *syscall_nr);
      sys_exit (-1);
      break;
    }

  f->eax = ret;
}

/* Halt the operating system. */
static void
sys_halt (void)
{
#if PRINT_DEBUG
  printf ("[SYSCALL] SYS_HALT\n");
#endif

  power_off ();
}

static void
sys_exit (int status)
{
  struct thread *t;
  struct list_elem *e;

#if PRINT_DEBUG
  printf ("[SYSCALL] SYS_EXIT\n");
#endif

  t = thread_current ();
  if (lock_held_by_current_thread (&file_lock))
    lock_release (&file_lock);

  /* Close all opened files of the thread. */
  while (!list_empty (&t->files) )
    {
      e = list_begin (&t->files);
      sys_close (list_entry (e, struct user_file, thread_elem)->fid);
    }

  if (t->ps != NULL)
    t->ps->exit_status = status;
  
  thread_exit ();
}

/* Start another new process. */
static pid_t
sys_exec (const char *file)
{
#if PRINT_DEBUG
  printf ("[SYSCALL] SYS_EXEC file: %s\n", file);
#endif

  if (!is_user_vaddr (file))
    sys_exit (-1);

  lock_acquire (&file_lock);
  int ret = process_execute (file);
  lock_release (&file_lock);
  return ret;
}

/* Wait for a child process to die. */
static int
sys_wait (pid_t pid)
{
#if PRINT_DEBUG
  printf ("[SYSCALL] SYS_WAIT\n");
#endif

  return process_wait (pid);
}

/* Create new file. */
static bool
sys_create (const char *file, unsigned initial_size)
{
  bool success;

#if PRINT_DEBUG
  printf ("[SYSCALL] SYS_CREATE: file: %s, initial_size: %u\n", file, initial_size);
#endif

  if (file == NULL || !is_user_vaddr (file))
    sys_exit (-1);

  lock_acquire (&file_lock);
  success = filesys_create (file, initial_size);
  lock_release (&file_lock);
  return success;
}

/* Delete a file. */
static bool
sys_remove (const char *file)
{
  bool success;

#if PRINT_DEBUG
  printf ("[SYSCALL] SYS_REMOVE: file: %s\n", file);
#endif

  if (file == NULL || !is_user_vaddr (file))
    sys_exit (-1);
  
  lock_acquire (&file_lock);
  success = filesys_remove (file);
  lock_release (&file_lock);
  return success;
}

/* Open a file. */
static int
sys_open (const char *file)
{
  struct file *sys_f;
  struct user_file *f;

#if PRINT_DEBUG
  printf ("[SYSCALL] SYS_OPEN: file: %s\n", file);
#endif

  if (file == NULL || !is_user_vaddr (file))
    sys_exit (-1);

  lock_acquire (&file_lock);
  sys_f = filesys_open (file);
  lock_release (&file_lock);
  if (sys_f == NULL)
    return -1;

  f = (struct user_file *) malloc (sizeof (struct user_file));
  if (f == NULL)
    {
      lock_acquire (&file_lock);
      file_close (sys_f);
      lock_release (&file_lock);
      return -1;
    }

  f->file = sys_f;
  f->fid = allocate_fid ();
  list_push_back (&thread_current ()->files, &f->thread_elem);

  return f->fid;
}

/* Obtain a file's size. */
static int
sys_filesize (int fd)
{
  struct user_file *f;
  int size = -1;

#if PRINT_DEBUG
  printf ("[SYSCALL] SYS_FILESIZE: fd: %d\n", fd);
#endif

  f = file_by_fid (fd);
  if (f == NULL)
    return -1;

  lock_acquire (&file_lock);
  size = file_length (f->file);
  lock_release (&file_lock);

  return size;
}

/* Read from a file. */
static int
sys_read (int fd, void *buffer, unsigned size)
{
  struct user_file *f;
  int ret = -1;

#if PRINT_DEBUG
  printf ("[SYSCALL] SYS_READ: fd: %d, buffer: %p, size: %u\n", fd, buffer, size);
#endif

  if (fd == STDIN_FILENO)
    {
      unsigned i;
      for (i = 0; i < size; ++i)
        *(uint8_t *)(buffer + i) = input_getc ();
      ret = size;
    }
  else if (fd == STDOUT_FILENO)
    ret = -1;
  else if (!is_user_vaddr (buffer) || !is_user_vaddr (buffer + size))
    sys_exit (-1);
  else
    {
      f = file_by_fid (fd);
      if (f == NULL)
        ret = -1;
      else
        {
          lock_acquire (&file_lock);
          ret = file_read (f->file, buffer, size);
          lock_release (&file_lock);
        }
    }

  return ret;
}

/* Write to a file. */
static int
sys_write (int fd, const void *buffer, unsigned size)
{
  struct user_file *f;
  int ret = -1;

#if PRINT_DEBUG
  printf ("[SYSCALL] SYS_WRITE: fd: %d, buffer: %p, size: %u\n", fd, buffer, size);
#endif

  if (fd == STDIN_FILENO)
    ret = -1;
  else if (fd == STDOUT_FILENO)
    {
      putbuf (buffer, size);
      ret = size;
    }
  else if (!is_user_vaddr (buffer) || !is_user_vaddr (buffer + size))
    sys_exit (-1);
  else
    {
      f = file_by_fid (fd);
      if (f == NULL)
        ret = -1;
      else
        {
          lock_acquire (&file_lock);
          ret = file_write (f->file, buffer, size);
          lock_release (&file_lock);
        }
    }
  return ret;
}

/* Change position in a file. */
static void
sys_seek (int fd, unsigned position)
{
  struct user_file *f;

#if PRINT_DEBUG
  printf ("[SYSCALL] SYS_SEEK: fd: %d, position: %u\n", fd, position);
#endif

  f = file_by_fid (fd);
  if (!f)
    sys_exit (-1);

  lock_acquire (&file_lock);
  file_seek (f->file, position);
  lock_release (&file_lock);
}

/* Report current position in a file. */
static unsigned
sys_tell (int fd)
{
  struct user_file *f;
  unsigned status;

#if PRINT_DEBUG
  printf ("[SYSCALL] SYS_TELL: fd: %d\n", fd);
#endif

  f = file_by_fid (fd);
  if (!f)
    sys_exit (-1);

  lock_acquire (&file_lock);
  status = file_tell (f->file);
  lock_release (&file_lock);

  return status;
}

/* Close a file. */
static void
sys_close (int fd)
{
  struct user_file *f;

#if PRINT_DEBUG
  printf ("[SYSCALL] SYS_CLOSE: fd: %d\n", fd);
#endif

  f = file_by_fid (fd);

  if (f == NULL)
    sys_exit (-1);

  lock_acquire (&file_lock);
  list_remove (&f->thread_elem);
  file_close (f->file);
  free (f);
  lock_release (&file_lock);
}

/* Extern function for sys_exit */
void 
sys_t_exit (int status)
{
  sys_exit (status);
}

/* Allocate a new fid for a file */
static fid_t
allocate_fid (void)
{
  static fid_t next_fid = 2;
  fid_t ret_fid;
  
  lock_acquire (&file_lock);
   ret_fid = next_fid++;
  lock_release (&file_lock);

  return ret_fid;
}

/* Returns the file with the given fid from the current thread's files */
static struct user_file *
file_by_fid (int fid)
{
  struct list_elem *e;
  struct thread *t;

  t = thread_current();
  for (e = list_begin (&t->files); e != list_end (&t->files);
       e = list_next (e))
    {
      struct user_file *f = list_entry (e, struct user_file, thread_elem);
      if (f->fid == fid)
        return f;
    }

  return NULL;
}
