
#include "../threads/synch.h"
#include "../threads/malloc.h"
#include "../threads/palloc.h"
#include "../threads/vaddr.h"
#include "vm/frametable.h"
#include "../lib/kernel/hash.h"
#include "debug.h"
#include "../threads/thread.h"
#include "userprog/pagedir.h"
#include <string.h>

static struct frame frametable;
static struct frame* hand;
static struct lock frametable_lock;

struct frame* find_frame (void* frame_paddr, struct frame** out_prev);

void
frametable_init (void)
{
  lock_init (&frametable_lock);

  frametable.next = &frametable;
  frametable.page_vaddr = NULL;
  frametable.frame_paddr = NULL;
  frametable.referenced = -1;

  hand = &frametable;
}

void*
frametable_get_page()
{
  return frametable_get_pages(1);
}

/* Returns a pointer to the start page of count consecutive free pages.
  If required, existing pages a free'd. */
void* get_free_pages (size_t count)
{
  // Try to allocate pages
  void* page_vaddr = NULL;
  int infinite_loop = 0;

  while ((page_vaddr = palloc_get_multiple (PAL_USER, count)) == NULL)
    {
      //PANIC("EVICTION DOESN'T WORK PROPERLY YET");
      ASSERT (infinite_loop++ < 2);

      if (!swap_available ())
        {
          PANIC ("NO SWAP AVAILABLE");
          return NULL;
        }

      // Search for a frame to evict (Clock algorithm)
      struct frame* prev = hand;
      hand = hand->next;
      while (hand->referenced != 0)
        {
          if (hand->referenced == 1)
            hand->referenced = 0;

          prev = hand;
          hand = hand->next;
        }

      ASSERT (hand->referenced == 0);

      // Evict pages at hand
      frametable_free_pages (hand->page_vaddr, count, true);
    }  
  
  return page_vaddr;
}

void*
frametable_get_pages (size_t pg_count)
{
  int i;  
  void* page_vaddr = get_free_pages (pg_count);   
  
  // Create new entries in frame table    
  for (i = 0; i < pg_count; i++)
    {            
      struct frame* frame = find_frame (page_vaddr + PGSIZE * i, NULL);
      ASSERT (frame == NULL);
        
      // Store frame in frame table    
      frame = malloc (sizeof (struct frame));
      frame->page_vaddr = page_vaddr + PGSIZE * i;
      frame->next = hand->next;
      frame->referenced = 1;
      frame->owner = thread_current ();

      hand->next = frame;
      hand = frame;              
    }
   
  return page_vaddr;
}

void frametable_free_pages (void* page_vaddr, size_t count, bool evict)
{
  void* p = NULL;
  for (p = page_vaddr; p < page_vaddr + PGSIZE * count; p += PGSIZE)
    {            
      frametable_free_page (p, evict);
    }
}

struct frame* find_frame (void* page_vaddr, struct frame** out_prev)
{
  struct frame* p = &frametable;
  int infinite_loop = 0;  
  
  // TODO: Lock list or use hash table instead or w/e  
  while (p->next->page_vaddr != page_vaddr)
    {
      p = p->next;
      if (p->next->referenced == -1) infinite_loop++;
      if (infinite_loop >= 2) break; // nothing found
    }
  
  // Nothing found
  if (p->next->page_vaddr != page_vaddr)
    return NULL;
  
  // Write out previous element if requested
  if (out_prev != NULL)
    *out_prev = p;
    
  return p->next;
}

void
frametable_free_page (void* page_vaddr, bool evict)
{    
  // Find frame table entry  
  struct frame* p = NULL;
  struct frame* frame_to_free = find_frame (page_vaddr, &p);    

  struct page_suppl* spte = suppl_get_other (frame_to_free->page_vaddr, frame_to_free->owner);
  
  // Remove frame from frame table
  if (frame_to_free != NULL && frame_to_free->frame_paddr == page_vaddr)
    {                  
      // Remove from linked list
      p->next = frame_to_free->next;
      if (hand == frame_to_free) 
        hand = frame_to_free->next;
      frame_to_free->referenced = 0;      
      
      if (evict)
        {                    
          // Evict page at hand
          struct thread* thread = frame_to_free->owner;
          if (pagedir_is_dirty (thread->pagedir, frame_to_free->page_vaddr))
            {              
              // Write to swap
              swap_write (frame_to_free->page_vaddr);              
            }          
        }
      
      // Remove from suppl. page table of thread
      suppl_free_other (frame_to_free->page_vaddr, frame_to_free->owner);
      
      free (frame_to_free);
    }      
   
  palloc_free_page (page_vaddr);  
}
