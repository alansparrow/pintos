#ifndef FRAMETABLE_H
#define	FRAMETABLE_H

/* ICH HASSE C (MAKE)
#include <stdio.h>
#include "../lib/kernel/hash.h"
#include <stdbool.h>
#include "debug.h"

struct frame
{
    struct hash_elem elem;
    int* page_vaddr;
};

static struct hash frametable;

void frametable_init (void);
void* frametable_get_page (void);
    
bool frame_less (const struct hash_elem* a_, const struct hash_elem* b_, 
                 void* aux UNUSED);
 */

#endif	/* FRAMETABLE_H */

