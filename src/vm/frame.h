#ifndef VM_FRAME_H
#define VM_FRAME_H

#include <list.h>
#include <stdbool.h>
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/synch.h"
#include "vm/page.h"

/* Frame. */
struct frame
  {
    struct thread *thread;      /* Thread. */
    void *kaddr;                /* User virtual address. */
    struct page *page;          /* Kernel virtual address. */
    struct list_elem elem;      /* List element. */
    struct semaphore sema;      /* Semaphore. */
  };

void frame_init (void);
struct frame* frame_alloc (struct page *p, enum palloc_flags flags);
void frame_install (struct frame *f);
void frame_free (struct frame *f);

#endif /* vm/frame.h */
