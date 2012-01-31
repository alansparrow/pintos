#ifndef CACHE_H
#define	CACHE_H

#include "devices/block.h"
#include <stdbool.h>

// Size of the buffer cache in blocks (sectors)
#define BUFFER_CACHE_SIZE 64

bool cache_read (block_sector_t sector, void* buffer);
void cache_write (block_sector_t sector, void* buffer);

#endif	/* CACHE_H */

