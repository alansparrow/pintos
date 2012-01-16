#ifndef SUPPL_PAGE_TABLE_H
#define	SUPPL_PAGE_TABLE_H

#include <inttypes.h>
#include "../lib/kernel/hash.h"
#include "../filesys/off_t.h"
#include "debug.h"


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

unsigned suppl_hash (const struct hash_elem* p_, void* aux UNUSED);
bool suppl_equals (const struct hash_elem* a_, const struct hash_elem* b_,
            void* aux UNUSED);

#endif	/* SUPPL_PAGE_TABLE_H */

