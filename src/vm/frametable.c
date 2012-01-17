
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
frametable_get_page ()
{
  // Try to allocate page
  void* page_vaddr = palloc_get_page (PAL_USER);
  if (page_vaddr == NULL)
    {
      // Search for a frame to evict (Clock algorithm)
      hand = hand->next;
      while (hand->referenced != 0)
        {
          if (hand->referenced == 1)
            hand->referenced = 0;

          hand = hand->next;
        }

      // Evict page at hand
      struct thread* thread = thread_current ();
      if (pagedir_is_dirty (thread->pagedir, hand->page_vaddr))
        {
          // Write to swap
          swap_write (hand->page_vaddr);
        }
      else
        {
          // Set page content to zero
          memset (&hand->page_vaddr, 0, PGSIZE);
        }

      // Use evicted page as new page
      void* new_page = hand->page_vaddr;      
      hand->referenced = 1;
      hand = hand->next;
      
      return new_page;
    }

  // Check frame address
  void* frame_paddr = (void*) vtop (page_vaddr);

  // Store frame in frametable    
  struct frame* frame = malloc (sizeof (struct frame));
  frame->page_vaddr = page_vaddr;
  frame->frame_paddr = frame_paddr;
  frame->next = hand->next;
  frame->referenced = 1;

  hand->next = frame;

  return page_vaddr;
}

void
frametable_free_page (void* page_vaddr)
{
  // Remove from frametable
  struct frame* p = &frametable;
  while (p->next->page_vaddr != page_vaddr)
    p = p->next;
  
  struct frame* frame_to_free = p->next;
  p->next = p->next->next;
  free (frame_to_free);

  palloc_free_page (page_vaddr);
}
