#include "vm/suppl_page_table.h"
#include "debug.h"
#include "threads/malloc.h"
#include "threads/thread.h"

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
           uint32_t read_bytes, uint32_t zero_bytes, bool writable)
{
  struct page_suppl p;
  struct hash_elem* e;
  struct hash* table = get_suppl_page_table ();
  
  p.page_vaddr = page_vaddr;
  e = hash_find (table, &p.elem);
  
  if (e == NULL)
    {
      // Create supplemental page table entry
      struct page_suppl* entry = malloc (sizeof(struct page_suppl));
      entry->page_vaddr = page_vaddr;
      entry->file = file;
      entry->ofs = ofs;
      entry->read_bytes = read_bytes;
      entry->zero_bytes = zero_bytes;
      entry->writable = writable;
      
      hash_insert (table, &entry->elem);
    }
  else
    {
      // Entry should not already exist
      ASSERT (false);      
    }
}

/* Removes the supplemental page table entry for page_vaddr */
void 
suppl_free (void* page_vaddr)
{    
  struct page_suppl p;
  struct page_suppl* entry;
  struct hash_elem* e; 
  struct hash* table = get_suppl_page_table ();
  
  p.page_vaddr = page_vaddr;
  e = hash_delete (table, &p.elem);  
  
  if (e != NULL)
    {
      entry = hash_entry (e, struct page_suppl, elem);
      free (entry);
    }
}

struct page_suppl* 
suppl_get (void* page_vaddr)
{
  struct page_suppl p;
  struct hash_elem* e;
  struct hash* table = get_suppl_page_table ();
  
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