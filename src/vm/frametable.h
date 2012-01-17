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
};

void frametable_init (void);

void* frametable_get_page (void);
void frametable_free_page (void* page_vaddr);

#endif	/* FRAMETABLE_H */

