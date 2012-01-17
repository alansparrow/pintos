#ifndef SUPPL_PAGE_TABLE_H
#define	SUPPL_PAGE_TABLE_H

#include <inttypes.h>
#include "../lib/kernel/hash.h"
#include "../filesys/off_t.h"
#include "debug.h"

/* Entry of the supplemental page table that stores information on what 
   data should be in a page. */
struct page_suppl
{
    struct hash_elem elem;
    int* page_vaddr;
    struct file *file;
    off_t ofs;
    uint32_t read_bytes;
    uint32_t zero_bytes;
    bool writable;
};

/* Get the supplemental page table entry for page_vaddr, or NULL if it doesn't exist. */
struct page_suppl* suppl_get (void* page_vaddr);

/* Sets the supplemental page table entry for page_vaddr */
void suppl_set (void* page_vaddr, struct file* file, off_t ofs, 
                uint32_t read_bytes, uint32_t zero_bytes, bool writable);

/* Removes the supplemental page table entry for page_vaddr */
void suppl_free (void* page_vaddr);

/* Destroys the suppl. page table of the current thread */
void suppl_destroy (void);

unsigned suppl_hash (const struct hash_elem* p_, void* aux UNUSED);
bool suppl_equals (const struct hash_elem* a_, const struct hash_elem* b_,
            void* aux UNUSED);

#endif	/* SUPPL_PAGE_TABLE_H */

