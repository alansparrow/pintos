#ifndef FRAMETABLE_H
#define	FRAMETABLE_H

#include "debug.h"
#include "../filesys/off_t.h"

struct frame
{
    struct frame* next;
    int referenced;
    void* page_vaddr;
    void* frame_paddr;
    struct thread* owner;
};

void frametable_init (void);

void* frametable_get_page (void);
void* frametable_get_pages (size_t pg_count);
void frametable_free_page (void* page_vaddr, bool evict);
void frametable_free_pages (void* page_vaddr, size_t count, bool evict);

#endif	/* FRAMETABLE_H */

