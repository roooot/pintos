#include "vm/swap.h"
#include <bitmap.h>
#include "devices/disk.h"
#include "threads/synch.h"
#include "threads/vaddr.h"

#define SECTORS_PER_PAGE (PGSIZE / DISK_SECTOR_SIZE)

/* Swap table. */
static struct bitmap *swap_table = NULL;
static struct lock swap_lock;

static inline struct disk *
get_swap (void)
{
  return disk_get (1, 1);
}

/* Initializes the swap table. */
void
swap_init (void)
{
  struct disk *swap = get_swap ();
  size_t size;

  if (swap == NULL)
    size = 0;
  else
    size = disk_size (swap);

  swap_table = bitmap_create (size / SECTORS_PER_PAGE);

  if (swap_table == NULL)
    PANIC ("Could not initialize swap.");

  lock_init (&swap_lock);
}

/* Destroys the swap table (never actually called??) */
void
swap_destroy (void)
{
  bitmap_destroy (swap_table);
}

/* Swap out a page from the address into the swap partition. */
bool
swap_out (void *address, size_t *swap_out_idx)
{
  size_t swap_idx;
  disk_sector_t sec_no;

  lock_acquire (&swap_lock);
  swap_idx = bitmap_scan_and_flip (swap_table, 0, 1, false);
  lock_release (&swap_lock);

  if (swap_idx == BITMAP_ERROR)
    return false;

  for (sec_no = 0; sec_no < SECTORS_PER_PAGE; sec_no ++)
    {
      disk_write (get_swap (), swap_idx * SECTORS_PER_PAGE + sec_no,
                  address + sec_no * DISK_SECTOR_SIZE);
    }

  *swap_out_idx = swap_idx;

  return true;
}

/* Swap the frame KPAGE in for the given PAGE. */
void
swap_in (size_t swap_idx, void *address)
{
  disk_sector_t sec_no;

  ASSERT (bitmap_test (swap_table, swap_idx));

  for (sec_no = 0; sec_no < SECTORS_PER_PAGE; sec_no ++)
    {
      disk_read (get_swap (), swap_idx * SECTORS_PER_PAGE + sec_no,
                 address + sec_no * DISK_SECTOR_SIZE);
    }

  lock_acquire (&swap_lock);
  bitmap_set (swap_table, swap_idx, false);
  lock_release (&swap_lock);
}

/* Free the swap. */
void
swap_free (size_t swap_idx)
{
  lock_acquire (&swap_lock);
  bitmap_set (swap_table, swap_idx, false);
  lock_release (&swap_lock);
}
