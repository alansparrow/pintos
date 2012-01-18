#include "vm/suppl_page_table.h"
#include "debug.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "userprog/pagedir.h"

struct hash* get_suppl_page_table (void);
void free_entry (struct hash_elem* e, void* aux);

bool
suppl_equals (const struct hash_elem* a_, const struct hash_elem* b_,
            void* aux UNUSED)
{            
  struct page_suppl* a = hash_entry (a_, struct page_suppl, elem);
  struct page_suppl* b = hash_entry (b_, struct page_suppl, elem);

  if (a->page_vaddr == b->page_vaddr)
    return 0;
  else
    return 1;
}


unsigned
suppl_hash (const struct hash_elem* p_, void* aux UNUSED)
{  
  const struct page_suppl* p = hash_entry (p_, struct page_suppl, elem);
  int hash = hash_int ((int)p->page_vaddr);
  return hash;
}

/* Sets the supplemental page table entry for page_vaddr */
void 
suppl_set (void* page_vaddr, struct file* file, off_t ofs, 
           uint32_t read_bytes, uint32_t zero_bytes, bool writable,
           enum page_origin from)
{
  struct page_suppl p;
  struct hash_elem* e;
  struct hash* table = get_suppl_page_table ();
  
  p.page_vaddr = page_vaddr;
  e = hash_find (table, &p.elem);
  
  struct page_suppl* entry = NULL;
  
  if (e == NULL)
    {
      // Create supplemental page table entry
      entry = malloc (sizeof(struct page_suppl));      
    }
  else
    {
      // Use existing entry for overwrite
      entry = hash_entry (e, struct page_suppl, elem);
    }

  entry->page_vaddr = page_vaddr;
  entry->file = file;
  entry->ofs = ofs;
  entry->read_bytes = read_bytes;
  entry->zero_bytes = zero_bytes;
  entry->writable = writable;
  entry->origin = from;

  if (e == NULL)
    hash_insert (table, &entry->elem);
}

/* Removes the supplemental page table entry for page_vaddr */
void 
suppl_free (void* page_vaddr)
{    
  suppl_free_other (page_vaddr, thread_current());
}

void suppl_free_other (void* page_vaddr, struct thread* thread)
{
  struct page_suppl p;
  struct page_suppl* entry;
  struct hash_elem* e; 
  struct hash* table = &thread->suppl_page_table;
  
  p.page_vaddr = page_vaddr;
  e = hash_delete (table, &p.elem);  
  
  if (e != NULL)
    {
      entry = hash_entry (e, struct page_suppl, elem);
      pagedir_clear_page (thread->pagedir, entry->page_vaddr);
      free (entry);
    }  
}

struct page_suppl* 
suppl_get (void* page_vaddr)
{
  return suppl_get_other (page_vaddr, thread_current());
}

struct page_suppl* 
suppl_get_other (void* page_vaddr, struct thread* thread)
{
  struct page_suppl p;
  struct hash_elem* e;
  struct hash* table = &thread->suppl_page_table;
  
  p.page_vaddr = page_vaddr;
  e = hash_find (table, &p.elem);
  
  if (e != NULL)
    {  
      return hash_entry (e, struct page_suppl, elem);
    }
  else
    {
      return NULL;
    }
}

struct hash* 
get_suppl_page_table ()
{
  struct thread* cur = thread_current ();
  return &cur->suppl_page_table;
}

void free_entry (struct hash_elem* e, void* aux UNUSED)
{
  struct page_suppl* entry = hash_entry (e, struct page_suppl, elem);
  free (entry);
}

void 
suppl_destroy ()
{
  hash_destroy (get_suppl_page_table (), free_entry);
}