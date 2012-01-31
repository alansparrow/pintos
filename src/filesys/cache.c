#include "cache.h"
#include "devices/block.h"
#include "lib/kernel/hash.h"
#include "lib/kernel/list.h"
#include "lib/debug.h"

struct cache_block 
{
  struct list_elem list_elem;
  struct hash_elem hash_elem;
  
  block_sector_t sector_index;      /* Sector index on filesys block device */
  void* content;                    /* Cached content of the block */
  bool dirty;                       /* True if block was written in cache */
};

unsigned cache_hash (const struct hash_elem* p_, void* aux UNUSED);
bool cache_equals (const struct hash_elem* a_, const struct hash_elem* b_,
                   void* aux UNUSED);

static struct list block_list;
static struct hash block_map;  
static int blocks_cached;  /* Number of blocks in the cache right now */

static void 
cache_init ()
{
  list_init (&block_list);
  hash_init (&block_map, cache_hash, cache_equals, NULL);
  
  blocks_cached = 0;
}

unsigned 
cache_hash (const struct hash_elem* p_, void* aux UNUSED)
{
  const struct cache_block* p = hash_entry (p_, struct cache_block, hash_elem);
  int hash = hash_int ((int)p->sector_index);
  return hash;
}

bool 
cache_equals (const struct hash_elem* a_, const struct hash_elem* b_,
                   void* aux UNUSED)
{
  struct cache_block* a = hash_entry (a_, struct cache_block, hash_elem);
  struct cache_block* b = hash_entry (b_, struct cache_block, hash_elem);
  
  if (a->sector_index == b->sector_index)
    return 0;
  else
    return 1;
}

bool 
cache_read (block_sector_t sector, void* buffer)
{
  
}
       
void
cache_write (block_sector_t sector, void* buffer)
{
  
}