#ifndef SUPPL_PAGE_TABLE_H
#define	SUPPL_PAGE_TABLE_H

#include <inttypes.h>
#include "../lib/kernel/hash.h"
#include "../filesys/off_t.h"
#include "debug.h"
#include "threads/thread.h"

/* enumeration for possible page origins */
enum page_origin 
{ 
    from_executable,    /* Page loaded through load_segment from executable */
    from_swap,          /* Page loaded from swap slot */
    from_file           /* Page loaded from file for memory mapping */
}; 

/* Entry of the supplemental page table that stores information on what 
   data should be in a page. Each process has its own SPT. */
struct page_suppl
{
    struct hash_elem elem;      /* hash element */
    int* page_vaddr;            /* user page address */
    struct file *file;          /* file that the page was loaded from */
    off_t ofs;                  /* offset in file */
    uint32_t read_bytes;        /* number of bytes read from file */
    uint32_t zero_bytes;        /* number of zero bytes */
    bool writable;              /* if page should be writable */
    
    enum page_origin origin;    /* where the page came from */
};

struct page_suppl* suppl_get (void* page_vaddr);
struct page_suppl* suppl_get_other (void* page_vaddr, struct thread* thread);

void suppl_set (void* page_vaddr, struct file* file, off_t ofs, 
                uint32_t read_bytes, uint32_t zero_bytes, bool writable,
                enum page_origin from);

void suppl_free (void* page_vaddr);
void suppl_free_other (void* page_vaddr, struct thread* thread);
void suppl_destroy (void);

unsigned suppl_hash (const struct hash_elem* p_, void* aux UNUSED);
bool suppl_equals (const struct hash_elem* a_, const struct hash_elem* b_,
            void* aux UNUSED);

#endif	/* SUPPL_PAGE_TABLE_H */

