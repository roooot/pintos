#include "vm/frame.h"
#include <stdbool.h>
#include <stddef.h>
#include <list.h>
#include <string.h>
#include <user/syscall.h>
#include "userprog/pagedir.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "vm/page.h"

static struct list frame_table;       /* Frame table. */
static struct lock frame_lock;        /* Frame lock. */
static struct list_elem *clock_hand;  /* The hand of the clock. */

static struct frame *create_frame (void *kaddr);
static struct list_elem *clock_next (void);
static struct frame *clock_algorithm (void);
static struct frame *frame_evict (void);

/* Initializes the frame table and the frame lock. */
void
frame_init (void)
{
  list_init (&frame_table);
  lock_init (&frame_lock);

  clock_hand = list_head (&frame_table);
}

/* Allocates a frame and marks it for the given user address.
   This frame may come from an unallocated frame or the eviction
   of a previously-allocated frame. */
struct frame*
frame_alloc (struct page *p, enum palloc_flags flags)
{
  struct frame *f = NULL;

  /* Attempt to allocate a frame from the user pool*/
  void *kaddr = palloc_get_page (PAL_USER | flags);

  if (kaddr != NULL)
    {
      /* Successfully allocate physical frame. 
         So, create a frame struct. */
      f = create_frame (kaddr);
    }
    else
    {
      /* Failed to allocate a frame. Evict an existing frame */
      f = frame_evict ();
      if (f == NULL)
        return NULL;

      /* Zero out the page if requested */
      if (flags & PAL_ZERO)
        memset (f->kaddr, 0, PGSIZE); 
    }

  f->thread = thread_current ();
  f->page = p;

  return f;
}

/* install the frame into the frame list. 
   After installed, the frame can be evicted. */
void
frame_install (struct frame *f)
{
  lock_acquire (&frame_lock);
  sema_up (&f->sema);
  lock_release (&frame_lock);
}

/* Deallocates a frame. */
void
frame_free (struct frame *f)
{
  /* Wait for the frame is installed. */
  sema_down (&f->sema);

  /* Prevent from the thread which does not own the frame */
  if (thread_current () != f->thread)
    return;

  lock_acquire (&frame_lock);

  /* If the clock hand aim to this frame, move to next frame */
  if (&f->elem == clock_hand)
    clock_next ();
  list_remove (&f->elem);

  lock_release (&frame_lock);
  
  palloc_free_page(f->kaddr);
  free (f);
}

/* Creates a frame with the kernel address. */
static struct frame *
create_frame (void *kaddr)
{
  struct frame *f = malloc (sizeof (struct frame));

  if (f == NULL)
    {
      palloc_free_page (kaddr);
      return NULL;
    }

  f->kaddr = kaddr;
  sema_init (&f->sema, 0);

  lock_acquire (&frame_lock);
  list_push_back (&frame_table, &f->elem);
  lock_release (&frame_lock);

  return f;
}

/* Helper function for the clock algorithm to treat the frame 
   list as a circularly linked list. */
static struct list_elem *
clock_next (void)
{
  clock_hand = list_next (clock_hand);
  if (clock_hand == list_end (&frame_table))
    clock_hand = list_begin (&frame_table);

  return clock_hand;
}

/* Uses the clock algorithm to find the next frame for eviction. */
static struct frame *
clock_algorithm (void)
{
  struct frame *f = NULL;

  lock_acquire (&frame_lock);

  /* the frame table is empty case. */
  if (list_empty (&frame_table))
    return NULL;

  f = list_entry (clock_next (), struct frame, elem);

  /* Run clock algorithm.
     I'm afraid of that it takes too much cycles. */
  while (true) 
    {
      if (sema_try_down (&f->sema)) 
        {
          if (pagedir_is_accessed (f->thread->pagedir, f->page->uaddr))
            {
              sema_up (&f->sema);
              pagedir_set_accessed (f->thread->pagedir, f->page->uaddr, false);
            }
          else
            {
              break;
            }
        }
      f = list_entry (clock_next (), struct frame, elem);
    }

  lock_release (&frame_lock);

  return f;
}

/* Evicts a frame from the frame table and returns it. */
static struct frame *
frame_evict (void)
{
  struct frame *f = NULL;
  bool success = false;

  while (!success)
    {
      /* Choose a frame to evict using clock algorithm. */
      f = clock_algorithm ();

      /* Could not find a frame to evict. */
      if (f == NULL)
        return NULL;

      /* Need to try to evict the page. */
      success = page_evict (f->page);
    }
  
  return f;
}
