#ifndef SWAPTABLE_H
#define	SWAPTABLE_H

#include <stdbool.h>

bool swap_write (void* page_vaddr);
void* swap_read (void* page_vaddr);

#endif	/* SWAPTABLE_H */

