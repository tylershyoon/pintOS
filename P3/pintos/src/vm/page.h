#ifndef VM_PAGE_H
#define VM_PAGE_H

#include "devices/disk.h"
#include "threads/thread.h"
#include "lib/kernel/hash.h"
#include "filesys/file.h"
#include "filesys/off_t.h"

enum type { MEMORY, FILE, MMAP, SWAP };

struct spte {
  enum type type;

  void * address;
  struct hash_elem hash_elem;
  
  off_t offset;
  off_t size;

  struct thread* thread;
  bool writable;

  struct file* file;

  uint32_t swap_index;
};



/* function header */

bool spt_init (struct hash* );
struct spte* page_lookup (struct hash*, const void *);
struct hash_elem* spt_insert(struct hash*, struct hash_elem*);
struct spte* get_spte(void *, enum type, bool);

bool get_lazy_file(struct spte*);
bool get_swap(struct spte*);

#endif
