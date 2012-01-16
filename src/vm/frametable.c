
#include "../threads/synch.h"
#include "../threads/malloc.h"
#include "../threads/palloc.h"
#include "../threads/vaddr.h"
#include "vm/frametable.h"
#include "../lib/kernel/hash.h"
#include "debug.h"
#include "../threads/thread.h"

static struct hash frametable;
static struct lock frametable_lock;

void
frametable_init (void)
{
  hash_init (&frametable, frame_hash, frame_equals, NULL);      
  lock_init (&frametable_lock);  
  
  struct thread* main_thread = thread_current ();
  hash_init (&main_thread->suppl_page_table, suppl_hash, suppl_equals, NULL);
}

void*
frametable_get_page ()
{
  // Try to allocate page
  void* page_vaddr = palloc_get_page (PAL_USER);
  if (page_vaddr == NULL)
    {   
      // TODO: Swapping
      //ASSERT (false);
      return NULL;
    }    
  
  // Check frame address
  int* frame_paddr = vtop (page_vaddr);  
  
  // Store frame in frametable    
  struct frame* frame = malloc (sizeof(struct frame));
  frame->page_vaddr = page_vaddr;
  frame->frame_paddr = frame_paddr;
    
  lock_acquire (&frametable_lock);
  struct hash_elem* existing = hash_insert (&frametable, &frame->elem); 
  lock_release (&frametable_lock);
  
  if (existing != NULL)
    {
      struct frame* exFrame = hash_entry (existing, struct frame, elem);
      
      if (exFrame->page_vaddr == frame->page_vaddr &&
          exFrame->frame_paddr == frame->frame_paddr)
        {
          // Exact same entry already exists...
        }
      else
        {      
          // TODO: Properly handle hash collision in frame table
          free (frame);
          return NULL;
        }
    }
  
  return page_vaddr;
}

void 
frametable_free_page (void* page_vaddr)
{
  struct frame f;
  struct frame* pFrame;
  struct hash_elem* e;
  
  f.page_vaddr = page_vaddr;
  f.frame_paddr = vtop (page_vaddr);
  
  // Delete frame pointing to that page address
  lock_acquire (&frametable_lock);
  e = hash_delete (&frametable, &f.elem);
  lock_release (&frametable_lock);
  
  if (e != NULL)
    {
      pFrame = hash_entry (e, struct frame, elem); 
      free (pFrame);
    }  
  
  palloc_free_page (page_vaddr);    
}

unsigned
frame_hash (const struct hash_elem* p_, void* aux UNUSED)
{  
  const struct frame* p = hash_entry (p_, struct frame, elem);
  int hash = hash_int ((int)p->frame_paddr);
  return hash;
}

bool
frame_equals (const struct hash_elem* a_, const struct hash_elem* b_,
            void* aux UNUSED)
{            
  struct frame* a = hash_entry (a_, struct frame, elem);
  struct frame* b = hash_entry (b_, struct frame, elem);

  if (a->frame_paddr == b->frame_paddr && a->page_vaddr == b->page_vaddr)
    return 0;
  else
    return 1;
}