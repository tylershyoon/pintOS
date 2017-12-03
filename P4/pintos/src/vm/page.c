#include "vm/page.h"
#include "vm/frame.h"
#include "vm/swap.h"
#include "userprog/pagedir.h"
#include "threads/vaddr.h"
#include "threads/malloc.h"

/* declaration header for helpers */
static unsigned page_hash_func (const struct hash_elem*, void * UNUSED);
static bool page_less_func (const struct hash_elem*, const struct hash_elem*, void * UNUSED);

/* Major functions */

bool
spt_init (struct hash* table)
{
  bool success = hash_init (table, page_hash_func, page_less_func, NULL);
  return success;
}

struct spte*
page_lookup (struct hash * table, const void * addr)
{
  struct hash_elem *he;
  struct spte entry;
  entry.address = pg_round_down(addr);
  he = hash_find(table, &entry.hash_elem);
  if (he == NULL){ return NULL; }
  else{ return hash_entry(he, struct spte, hash_elem); }
}

struct hash_elem*
spt_insert (struct hash * table, struct hash_elem* he)
{
  struct hash_elem* result = hash_insert(table, he);
  return result;
}

struct spte*
get_spte (void *addr, enum type type, bool writable)
{
  struct spte* spte = (struct spte*)malloc(sizeof(struct spte));
  if (!spte) { return NULL; }

  spte->type = type;
  spte->address = addr;
  spte->thread = thread_current();
  spte->writable = writable;
  spt_insert(&spte->thread->spt, &spte->hash_elem);
  return spte;
}

bool
get_lazy_file (struct spte* spte)
{
  uint8_t *kpage;
  if (spte->size == PGSIZE)
  {
    kpage = f_allocate(PAL_USER, spte);
    if (file_read_at(spte->file, kpage, PGSIZE, spte->offset) != PGSIZE)
    {
      f_free(kpage);
      return false;
    }
  }
  else if (spte->size == 0){ kpage = f_allocate(PAL_ZERO | PAL_USER, spte); }
  else
  {
    kpage = f_allocate(PAL_ZERO | PAL_USER, spte);
    file_read_at (spte->file, kpage, spte->size, spte->offset);
  }
  spte->type = MEMORY;
  if (!pagedir_set_page (spte->thread->pagedir, spte->address, kpage, spte->writable))
  {
    f_free(kpage);
    return false;
  }
  return true;
}

bool
get_swap (struct spte* spte)
{
  void *frame = f_allocate(PAL_USER, spte);
  struct thread* curr = thread_current();

  swap_in(spte->swap_index, frame);
  spte->type = MEMORY;
  if(!pagedir_set_page(curr->pagedir, spte->address, frame, spte->writable))
  {
    f_free(frame);
    return false;
  }
  else{ return true; }
}



/* Helper functions */

static unsigned
page_hash_func (const struct hash_elem *e, void *aux UNUSED)
{
  const struct spte *p = hash_entry (e, struct spte, hash_elem);
  return hash_bytes (&p->address, sizeof(void*));
}

static bool
page_less_func (const struct hash_elem *e1, const struct hash_elem *e2, void *aux UNUSED)
{
  const struct spte *s1 = hash_entry (e1, struct spte, hash_elem);
  const struct spte *s2 = hash_entry (e2, struct spte, hash_elem);

  return s1->address < s2->address;
}
