#include "vm/swaptable.h"
#include <stdbool.h>
#include "lib/kernel/hash.h"
#include "lib/kernel/bitmap.h"
#include "threads/vaddr.h"
#include "devices/block.h"
#include "vm/suppl_page_table.h"
#include "threads/thread.h"
#include "vm/frametable.h"
#include "userprog/pagedir.h"
#include "threads/malloc.h"

#define SECTORS_PER_SLOT (PGSIZE / BLOCK_SECTOR_SIZE)

static struct hash swap_map;
static struct bitmap* swap_slots;
static struct block* swap;

struct swap_mapping
{
  struct hash_elem elem;
  void* page_vaddr;
  struct thread* thread;

  /* swap slot = page index (0...1000+) */
  int slot;
};

unsigned swap_hash (const struct hash_elem* p_, void* aux UNUSED);
bool swap_equals (const struct hash_elem* a_, const struct hash_elem* b_,
                  void* aux UNUSED);

void
swap_init ()
{
  swap = block_get_role (BLOCK_SWAP);

  // Bitmap to map each page in swap as free (0) / occupied (1)
  int swap_size_bytes = BLOCK_SECTOR_SIZE * block_size (swap);
  swap_slots = bitmap_create (swap_size_bytes / PGSIZE);

  hash_init (&swap_map, swap_hash, swap_equals, NULL);
}

bool
swap_write (void* page_vaddr) 
{
  return false;
}

bool
swap_read (void* page_vaddr)
{
  struct swap_mapping p;
  struct hash_elem* e;
  struct thread* thread = thread_current ();

  // Find entry in swap table
  p.page_vaddr = page_vaddr;
  p.thread = thread;  
  e = hash_find (&swap_map, &p.elem);
  if (e == NULL) return false;
  struct swap_mapping* mapping = hash_entry (e, struct swap_mapping, elem);

  // Get new page
  void* kpage = frametable_get_page ();
  ASSERT (kpage != NULL);

  // Read content from swap into page
  int baseSector = mapping->slot * SECTORS_PER_SLOT;
  int i;
  
  for (i = 0; i < SECTORS_PER_SLOT; i++)
    {      
      block_read (swap, baseSector + i, kpage + (i * BLOCK_SECTOR_SIZE));
    }
  
  // Add page to the process's address space
  struct page_suppl* spte = suppl_get (page_vaddr);
  ASSERT (spte != NULL);
  
  if (!(pagedir_get_page (thread->pagedir, page_vaddr) == NULL
          && pagedir_set_page (thread->pagedir, page_vaddr, kpage, spte->writable)))
    {      
      frametable_free_page (kpage);
      return false;
    }
  
  // Mark swap slot as free now
  bitmap_set (swap_slots, mapping->slot, false);
  hash_delete (&swap_map, &mapping->elem);
  free (mapping);

  return true;
}

unsigned
swap_hash (const struct hash_elem* p_, void* aux UNUSED)
{
  const struct swap_mapping* p = hash_entry (p_, struct swap_mapping, elem);
  int hash = hash_int ((int) p->page_vaddr);
  return hash;
}

bool
swap_equals (const struct hash_elem* a_, const struct hash_elem* b_,
             void* aux UNUSED)
{
  struct swap_mapping* a = hash_entry (a_, struct swap_mapping, elem);
  struct swap_mapping* b = hash_entry (b_, struct swap_mapping, elem);

  if (a->page_vaddr == b->page_vaddr && a->thread == b->thread)
    return 0;
  else
    return 1;
}