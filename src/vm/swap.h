#ifndef VM_SWAP_H
#define VM_SWAP_H

#include <stddef.h>
#include <stdbool.h>

void swap_init (void);
void swap_destroy (void);

bool swap_out (void *address, size_t *swap_out_idx);
void swap_in (size_t swap_idx, void *address);
void swap_free (size_t swap_idx);

#endif /* vm/swap.h */
