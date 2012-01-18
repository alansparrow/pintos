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

// Number of swap sectors per page / swap slot
#define SECTORS_PER_SLOT (PGSIZE / BLOCK_SECTOR_SIZE)

static struct hash swap_map;              /* Swap mappings (s.b.) */
static struct bitmap* swap_slots = NULL;  /* Bitmap for finding free slots */
static struct block* swap = NULL;         /* The swap block device */

/* Mapping of a user page to a swap slot */
struct swap_mapping
{
  struct hash_elem elem;        /* hash element */
  void* page_vaddr;             /* User virtual page address */
  struct thread* thread;        /* Owning thread */  
  int slot;                     /* swap slot = page index (0...1000+) */
};

unsigned swap_hash (const struct hash_elem* p_, void* aux UNUSED);
bool swap_equals (const struct hash_elem* a_, const struct hash_elem* b_,
                  void* aux UNUSED);

/* Determines if the swap block device is available */
bool 
swap_available ()
{
  return swap != NULL;
}

/* Acquires the swap block device and performs necessary initializations for
   swap management structures. */
void
swap_init ()
{
  swap = block_get_role (BLOCK_SWAP);
  if (swap == NULL)
    // No Swap Partition available
    return;

  // Bitmap to map each page in swap as free (0) / occupied (1)
  int swap_size_bytes = BLOCK_SECTOR_SIZE * block_size (swap);
  swap_slots = bitmap_create (swap_size_bytes / PGSIZE);

  hash_init (&swap_map, swap_hash, swap_equals, NULL);
}

/* Writes the contents of given user page into an available swap slot. The 
   content can be retrieved later by using swap_read only by the same thread.
   Returns always true, or else panics the kernel currently, if the swap is
   full. */
bool
swap_write (void* page_vaddr) 
{
  ASSERT (swap != NULL);
  ASSERT (swap_slots != NULL);
  
  // Find free swap slot  
  int slot = bitmap_scan_and_flip (swap_slots, 0, 1, false);  
  if (slot == BITMAP_ERROR)
    PANIC("#############\nOMAGAWD SWAP IS FULL. NEED MOAR! \n###########");
  
  // Write content from page into swap
  int baseSector = slot * SECTORS_PER_SLOT;
  int i;
  
  for (i = 0; i < SECTORS_PER_SLOT; i++)
    {      
      block_write (swap, baseSector + i, page_vaddr + (i * BLOCK_SECTOR_SIZE));
    }  
  
  // Create entry in swap map
  struct swap_mapping* entry = malloc(sizeof(struct swap_mapping));
  ASSERT (entry != NULL);
  
  entry->page_vaddr = page_vaddr;
  entry->slot = slot;
  entry->thread = thread_current ();
  
  hash_insert (&swap_map, &entry->elem);
  
  return true;
}

/* Reads the contents of a swapped out user page back into the same page.
   Additionally the page is added to the current process's page dir. 
   This may only be called by the same thread that wrote the page to swap.
   Returns true on success.
 */
bool
swap_read (void* page_vaddr)
{    
  if (!swap_available ()) return false;
  
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
  
  if (!frame_map (page_vaddr, kpage, thread, spte->writable))
    {            
      frametable_free_page (kpage, false);
      return false;
    }
  
  // Mark swap slot as free now
  bitmap_set (swap_slots, mapping->slot, false);
  hash_delete (&swap_map, &mapping->elem);
  free (mapping);

  return true;
}

/* Hashes a swap_mapping using the user page address */
unsigned
swap_hash (const struct hash_elem* p_, void* aux UNUSED)
{
  const struct swap_mapping* p = hash_entry (p_, struct swap_mapping, elem);
  int hash = hash_int ((int) p->page_vaddr);
  return hash;
}

/* Compares two swap_mappings using the user page address */
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