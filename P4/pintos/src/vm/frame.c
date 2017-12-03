#include "vm/frame.h"
#include "vm/swap.h"
#include "userprog/pagedir.h"
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include <stdio.h>
#include "lib/kernel/list.h"

struct lock ft_lock;
struct list ft_lst;

void 
ft_init(void)
{
  list_init(&ft_lst);
  lock_init(&ft_lock);
}

/* flags either assert zero user */
void* 
f_allocate(enum palloc_flags flags, struct spte *spte)
{
  struct fte* fte;
  void* frame;

  lock_acquire (&ft_lock);
  frame = palloc_get_page(flags);
  while(!frame){ frame = f_evict(flags); }

  fte = malloc(sizeof(struct spte));
  fte->frame = frame;
  fte->spte = spte;
  spte->type = MEMORY;
  list_push_back (&ft_lst, &fte->elem);
  
  lock_release(&ft_lock);
  return frame;
}

void
f_free(void* frame)
{
  struct thread* curr = thread_current();
  struct spte* spte;
  struct fte* fte;
  struct list_elem *itr;

  lock_acquire(&ft_lock);

  for(itr=list_begin(&ft_lst); itr != list_end(&ft_lst); itr = list_next(itr))
  {
    fte = list_entry(itr, struct fte, elem);
    if (frame == fte->frame)
    {
      list_remove(itr);
      spte = fte->spte;
      pagedir_clear_page(spte->thread->pagedir, spte->address);
      palloc_free_page(frame);
      hash_delete(&curr->spt, &spte->hash_elem);
      free(fte);
      free(spte);
      lock_release(&ft_lock);
      return;
    }
  }
  lock_release(&ft_lock);
  return;
}

void f_free_spte(struct spte* spte)
{
  struct fte* fte;
  struct list_elem* itr;

  lock_acquire(&ft_lock);

  for (itr = list_begin(&ft_lst); itr != list_end(&ft_lst); itr = list_next(itr))
  {
    fte = list_entry(itr, struct fte, elem);
    if (spte == fte->spte)
    {
      list_remove(itr);
      pagedir_clear_page(spte->thread->pagedir, spte->address);
      palloc_free_page(fte->frame);
      free(fte);
      lock_release(&ft_lock);
      return;
    }
  }
  lock_release(&ft_lock);
  return;
}

void* f_evict (enum palloc_flags flags)
{
  struct fte* fte = list_entry(list_pop_front(&ft_lst), struct fte, elem);
  struct spte* spte = fte->spte;

  spte->type = SWAP;
  spte->swap_index = swap_out(fte->frame);
  pagedir_clear_page(spte->thread->pagedir, spte->address);
  palloc_free_page(fte->frame);
  free(fte);

  return palloc_get_page(flags);
}
