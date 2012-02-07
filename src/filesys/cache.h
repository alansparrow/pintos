#ifndef BUFFER_CACHE_H
#define	BUFFER_CACHE_H

#include "devices/block.h"
#include <stdbool.h>
#include "filesys/off_t.h"

// Size of the buffer cache in blocks (sectors)
#define BUFFER_CACHE_SIZE 64

void cache_init (void);

bool cache_read (block_sector_t sector, void* buffer);
void cache_read_in (block_sector_t sector, void* buffer, off_t ofs, off_t size);
void cache_write (block_sector_t sector, const void* buffer);
void cache_write_in (block_sector_t sector, void* buffer, off_t ofs, off_t size);

bool cache_enabled (void);

void cache_flush (void);
void cache_free (void);

void cache_exit (void);

#endif	/* CACHE_H */

