
#include "vm/frametable.h"
#include "../lib/kernel/hash.h"
#include "debug.h"

void
frametable_init (void)
{
  hash_init (&frametable, hash_int, frame_equals, NULL);
}

void*
frametable_get_page ()
{
  void* page_vaddr = palloc_get_page (PAL_USER);
  if (page_vaddr == NULL)
    {   
      // TODO: Swapping
      return NULL;
    }    
  
  int* frame_paddr = vtop (page_vaddr);
  
  struct frame* frame = malloc (sizeof(struct frame));

  return page_vaddr;
}

bool
frame_equals (const struct hash_elem* a_, const struct hash_elem* b_,
            void* aux UNUSED)
{
  struct frame* a = hash_entry (a_, struct frame, elem);
  struct frame* b = hash_entry (b_, struct frame, elem);

  if (*a->page_vaddr < *b->page_vaddr) return -1;
  else if (*a->page_vaddr > *b->page_vaddr) return 1;
  else return 0;
}