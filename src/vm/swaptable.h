#ifndef SWAPTABLE_H
#define	SWAPTABLE_H

#include <stdbool.h>

void swap_init (void);

bool swap_write (void* page_vaddr);
bool swap_read (void* page_vaddr);

#endif	/* SWAPTABLE_H */

