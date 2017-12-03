#ifndef VM_FRAME_H
#define VM_FRAME_H

#include "vm/page.h"
#include "threads/thread.h"
#include "threads/palloc.h"
#include "lib/kernel/list.h"

struct fte {
  void* frame;
  struct list_elem elem;
  struct spte *spte;
};

void ft_init (void);
void* f_allocate (enum palloc_flags, struct spte*);
void f_free (void*);
void f_free_spte (struct spte*);
void* f_evict (enum palloc_flags);

#endif
