#ifndef FRAMETABLE_H
#define	FRAMETABLE_H

#include "../lib/kernel/hash.h"
#include "debug.h"
#include "../filesys/off_t.h"

struct frame
{
    struct hash_elem elem;
    int* page_vaddr;
    int* frame_paddr;
};

unsigned frame_hash (const struct hash_elem* p_, void* aux UNUSED);
void frametable_init (void);

void* frametable_get_page (void);
void frametable_free_page (void* page_vaddr);
    
bool frame_equals (const struct hash_elem* a_, const struct hash_elem* b_, 
                 void* aux UNUSED);

#endif	/* FRAMETABLE_H */

