#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"

struct process_status
  {
    tid_t tid;
    struct thread *t;               /* The thread. */
    struct thread *parent;          /* The parent thread. */
    int exit_status;                /* Exit status. */
    struct semaphore sema_wait;     /* Semaphore for process_wait. */
    struct list_elem elem;          /* List elem for children list. */
  };

tid_t process_execute (const char *file_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);

#endif /* userprog/process.h */
