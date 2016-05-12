#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <hash.h>
#include <stdbool.h>
#include "filesys/file.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "vm/frame.h"

/* Supplemental page table entry struct . */
struct page
  {
    void *uaddr;                /* User page address(page-aligned). */
    bool writable;              /* Page is writable or not. */
    struct frame *frame;        /* Frame entry. */

    bool swapped;               /* Is this block swapped. */
    size_t swap_idx;            /* The index of swap. */

    struct lock lock;           /* Page lock. */
    struct hash_elem hash_elem; /* Entry in thread's hash table. */
  };

bool page_table_init (struct hash *page_table);
void page_table_destroy (struct hash *page_table);

struct page *page_alloc (const void *uaddr, bool writable);
void page_free (struct page *p);
bool page_evict (struct page *p);
bool page_load (void *fault_addr);

#endif /* vm/page.h */
