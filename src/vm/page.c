#include "vm/page.h"
#include "threads/malloc.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "vm/swap.h"

static struct page *create_page (const void *uaddr, bool writable);
static void destroy_page (struct page *p);
static bool swap_out_page (struct page *p);
static bool swap_in_page (struct page *p);
static bool install_page (struct page *p);

static struct page *page_lookup (const void *uaddr);
static unsigned page_hash (const struct hash_elem *p_, 
                           void *aux UNUSED);
static bool page_less (const struct hash_elem *a_, 
                       const struct hash_elem *b_, void *aux UNUSED);
static void page_destructor (struct hash_elem *e, void *aux UNUSED);

/* Initializes supplemental page table. */
bool
page_table_init (struct hash *page_table)
{
  ASSERT (page_table != NULL);

  return hash_init (page_table, page_hash, page_less, NULL);
}

/* Destroys an entry in the page table.
   This function is called by process_exit(). */
void
page_table_destroy (struct hash *page_table)
{
  ASSERT (page_table != NULL);
  hash_destroy (page_table, page_destructor);
}

/* Adds a memory-based supplemental page table entry 
   to the current process. */
struct page * 
page_alloc (const void *uaddr, bool writable)
{
  ASSERT (is_user_vaddr (uaddr));

  struct page *p = create_page (uaddr, writable);

  if (p == NULL)
    return NULL;

  p->frame = frame_alloc (p, PAL_ZERO);

  /* Failed to allocate a frame. */
  if (p->frame == NULL)
    {
      destroy_page (p);
      return NULL;
    }

  /* Try to install the page */
  if (!install_page (p))
    {
      frame_free (p->frame);
      destroy_page (p);
      return NULL;
    }
  
  return p;
}

/* Free the page */
void 
page_free (struct page *p)
{
  destroy_page (p);
}

/* Try to evict the page. */
bool
page_evict (struct page *p)
{
  ASSERT (p != NULL);
  
  /* When the frame of the page is evicted,
     the present of PTE should be clear. */
  pagedir_clear_page (p->frame->thread->pagedir, p->uaddr);

  lock_acquire (&p->lock);
  bool success = swap_out_page (p);
  lock_release (&p->lock);

  /* Failed to swap or file in the page table.
     It needs to install again. */
  if (!success) 
    frame_install (p->frame);
    
  return success;
}

/* Attempts to load the page using the supplemental page table. */
bool
page_load (void *fault_addr)
{
  ASSERT ((void*) fault_addr < PHYS_BASE);

  /* Align the page address. */
  fault_addr = pg_round_down (fault_addr);

  struct page *p = page_lookup (fault_addr);

  if (p == NULL)
    return false;

  lock_acquire (&p->lock);
  bool success = swap_in_page (p);
  lock_release (&p->lock);

  return success;
}

/* Create a page entry. */
static struct page *
create_page (const void *uaddr, bool writable)
{
  struct page *p = malloc (sizeof (struct page));
  if (p == NULL)
    return NULL;

  /* Page align the address. */
  void *aligned_uaddr = pg_round_down (uaddr);

  p->uaddr = aligned_uaddr;
  p->writable = writable;
  p->frame = NULL;
  p->swapped = false;
  lock_init (&p->lock);

  /* Install into hash table. */
  hash_insert (&thread_current ()->page_table, &p->hash_elem);

  return p;
}

/* Destroy the page. */
static void
destroy_page_ (struct page *p)
{
  lock_acquire (&p->lock);
  struct frame *f = p->frame;
  lock_release (&p->lock);

  /* Free the frame object if allocated */
  if (f != NULL)
    {
      pagedir_clear_page (f->thread->pagedir, p->uaddr);
      frame_free (p->frame);
    }

  lock_acquire (&p->lock);
  if (p->swapped)
    swap_free (p->swap_idx);

  lock_release (&p->lock);
}

/* Destroy the page. */
static void
destroy_page (struct page *p)
{
  destroy_page_ (p);
  hash_delete (&thread_current ()->page_table, &p->hash_elem);
  free (p);
}

/* Swap out the page to disk. */
static bool
swap_out_page (struct page *p)
{
  ASSERT (p != NULL);
  ASSERT (lock_held_by_current_thread (&p->lock));
  ASSERT (!p->swapped);
  ASSERT (p->frame != NULL);

  bool success = swap_out (p->frame->kaddr, &p->swap_idx);
  if (!success) 
    return false;

  p->swapped = true;
  p->frame = NULL;

  return true;
}

/* Swap in a page into a frame. */
static bool
swap_in_page (struct page *p)
{
  ASSERT (p != NULL);
  ASSERT (lock_held_by_current_thread (&p->lock));
  ASSERT (p->swapped);

  p->frame = frame_alloc (p, 0);
  if (p->frame == NULL)
    return false;

  swap_in (p->swap_idx, p->frame->kaddr);

  /* Try to install the page */
  if (!install_page (p))
    {
      frame_free (p->frame);
      return false;
    }

  p->swapped = false;

  return true;
}

/* Adds a mapping from user virtual address to kernel virtual 
   address to the page table. Then, installs the frame.
   Returns true on success, false if virtual address is already 
   mapped or if memory allocation fails. */
static bool
install_page (struct page* p)
{
  struct thread *t = thread_current ();

  /* Verify that there's not already a page at that virtual
     address, then map our page there. */
  bool success = (pagedir_get_page (t->pagedir, p->uaddr) == NULL
                  && pagedir_set_page (t->pagedir, p->uaddr, 
                                       p->frame->kaddr, p->writable));

  frame_install (p->frame);

  return success;
}

/* Find a page witch the given uaddr from page table */
static struct page *
page_lookup (const void *uaddr)
{
  struct hash_elem *e;
  struct page p = {.uaddr = (void *) uaddr};
  e = hash_find (&thread_current ()->page_table, &p.hash_elem);
  return e != NULL ? hash_entry (e, struct page, hash_elem) : NULL;
}

/* Returns a hash value for page p. */
static unsigned
page_hash (const struct hash_elem *p_, void *aux UNUSED)
{
  const struct page *p = hash_entry (p_, struct page, hash_elem);
  return hash_bytes (&p->uaddr, sizeof p->uaddr);
}

 /* Returns true if page a precedes page b. */
static bool
page_less (const struct hash_elem *a_, const struct hash_elem *b_,
           void *aux UNUSED)
{
  const struct page *a = hash_entry (a_, struct page, hash_elem);
  const struct page *b = hash_entry (b_, struct page, hash_elem);
  return (a->uaddr < b->uaddr);
}

/* Page destructor for hash destroy */
static void
page_destructor (struct hash_elem *e, void *aux UNUSED)
{
  struct page *p = hash_entry (e, struct page, hash_elem);
  destroy_page_ (p);
}
