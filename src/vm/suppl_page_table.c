#include "vm/suppl_page_table.h"
#include "debug.h"

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
