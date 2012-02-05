#include "cache.h"
#include "devices/block.h"
#include "lib/kernel/hash.h"
#include "lib/kernel/list.h"
#include "threads/synch.h"
#include "lib/debug.h"
#include <string.h>
#include <stdbool.h>
#include "../threads/malloc.h"
#include "filesys.h"

struct cache_block
{
  struct list_elem list_elem;
  struct hash_elem hash_elem;
  struct lock access_lock;

  block_sector_t sector_index; /* Sector index on filesys block device */
  void* content; /* Cached content of the block */
  bool dirty; /* True if block was written while in cache */
  int referenced; /* Reference counter for clock algorithm */
};

unsigned cache_hash (const struct hash_elem* p_, void* aux UNUSED);
bool cache_equals (const struct hash_elem* a_, const struct hash_elem* b_,
                   void* aux UNUSED);
struct cache_block* cache_lookup (block_sector_t sector);
struct cache_block* get_successor (struct cache_block* block);
void cache_evict (void);
struct cache_block* cache_create (block_sector_t sector);
void cache_flush_block (struct cache_block* block);
void cache_free (void);
void cache_remove (struct cache_block* block, bool flush);
void cache_clear (void);

static struct list block_list;
static struct hash block_map;
static struct lock create_lock;
static int blocks_cached = -1; /* Number of blocks in the cache right now */
static struct cache_block* hand = NULL;

void
cache_init ()
{
  list_init (&block_list);
  hash_init (&block_map, cache_hash, cache_equals, NULL);
  lock_init (&create_lock);

  blocks_cached = 0;
  hand = NULL;
}

bool cache_enabled ()
{
  return blocks_cached >= 0;
}

unsigned
cache_hash (const struct hash_elem* p_, void* aux UNUSED)
{
  const struct cache_block* p = hash_entry (p_, struct cache_block, hash_elem);
  int hash = hash_int ((int) p->sector_index);
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
  struct cache_block* block = cache_lookup (sector);
  if (block == NULL)
    return false;

  // copy cached content into buffer
  memcpy (buffer, block->content, BLOCK_SECTOR_SIZE);

  return true;
}

void
cache_write (block_sector_t sector, const void* buffer)
{
  struct cache_block* block = cache_lookup (sector);
  if (block != NULL)
    {
      lock_acquire (&block->access_lock);

      // block is already cached -> overwrite
      memcpy (block->content, buffer, BLOCK_SECTOR_SIZE);
      block->dirty = true;

      lock_release (&block->access_lock);
      return;
    }

  // Otherwise need to create new cache block
  block = cache_create (sector);
  if (block == NULL) PANIC ("Out Of Memory");

  // Write buffer content into cache
  lock_acquire (&block->access_lock);
  memcpy (block->content, buffer, BLOCK_SECTOR_SIZE);
  lock_release (&block->access_lock);
}

/* Creates a new cache block for given sector. If the maximum number of 
 * cached blocks is reached, another block is evicted. Returns NULL if
 * no memory can be allocated. */
struct cache_block*
cache_create (block_sector_t sector)
{
  lock_acquire (&create_lock);

  // If the cache is full, evict one block
  if (blocks_cached == BUFFER_CACHE_SIZE)
    {
      cache_evict ();
    }

  ASSERT (blocks_cached < BUFFER_CACHE_SIZE);

  // Allocate and initialize the new cache block
  struct cache_block* block = calloc (sizeof (struct cache_block), 1);
  if (block == NULL) return NULL;

  block->dirty = false;
  block->referenced = 1;
  block->sector_index = sector;
  lock_init (&block->access_lock);

  if (hand == NULL)
    {
      // No hand means the list is empty, so insert the first element
      // and point the hand to it
      list_push_back (&block_list, &block->list_elem);
      hand = block;
    }
  else
    {
      // Insert new element before hand
      list_insert (&hand->list_elem, &block->list_elem);
    }

  hash_insert (&block_map, &block->hash_elem);
  blocks_cached++;

  lock_release (&create_lock);

  return block;
}

/* Evicts some cache block by first removing it from the block list and map,
   so that new cache blocks can be created, and afterwards writing it to disk,
   if necessary. */
void
cache_evict ()
{
  // Search for a block to evict (Clock algorithm)
  while (hand->referenced != 0)
    {
      if (hand->referenced == 1)
        hand->referenced = 0;

      hand = get_successor (hand);
    }

  ASSERT (hand->referenced == 0);

  // Found a block to evict; move hand to next element
  struct cache_block* evictee = hand;
  hand = get_successor (hand);

  // Remove the block from the cache
  cache_remove (evictee, true);
}

void
cache_remove (struct cache_block* block, bool flush)
{
  // Remove evictee from block lists
  list_remove (&block->list_elem);
  hash_delete (&block_map, &block->hash_elem);
  blocks_cached--;

  // TODO: Was ist, wenn gerad einer auf den Block zugreifen will?

  // Write back to file system if required
  if (flush)
    cache_flush_block (block);

  // and free the resources
  free (block);
}

/* If the given block is dirty, it is written out to the filesystem, and the 
   dirty flag is reset to false. */
void
cache_flush_block (struct cache_block* block)
{
  if (block->dirty)
    {
      lock_acquire (&block->access_lock);

      block_write_nocache (fs_device, block->sector_index, block->content);
      block->dirty = false;

      lock_release (&block->access_lock);
    }
}

/* Flushes the whole cache by writing out all dirty cache blocks. However,
   the cache itself is not cleared or free'd. */
void
cache_flush ()
{
  struct cache_block* start = hand;
  struct cache_block* p = hand;

  do
    {
      cache_flush_block (p);
      p = get_successor (p);
    }
  while (p != start);
}

/* Clears the whole cache by freeing all cache blocks. The cache is not 
   flushed. */
void
cache_clear ()
{
  struct cache_block* p = NULL;

  while (!list_empty (&block_list))
    {
      p = list_entry (list_begin (&block_list), struct cache_block, list_elem);      
      cache_remove (p, false);
    }
}

/* Releases all dynamically allocated resources used by the cache */
void
cache_free ()
{  
  if (blocks_cached > 0)
    cache_clear ();
  
  ASSERT (blocks_cached == 0);
  
  hash_destroy (&block_map, NULL);  
  blocks_cached = -1;
  hand = NULL;
}

/* Gets the successor of given block for the clock algorithm, which will always
   be the next element in the list, or the beginning if the end was reached. */
struct cache_block*
get_successor (struct cache_block* block)
{
  struct list_elem* next = list_next (&block->list_elem);
  if (list_tail (&block_list) == next)
    {
      next = list_begin (&block_list);
    }

  return list_entry (next, struct cache_block, list_elem);
}

/* Returns the page containing the given virtual address,
   or a null pointer if no such page exists. */
struct cache_block*
cache_lookup (block_sector_t sector)
{
  struct cache_block p;
  struct hash_elem *e;

  p.sector_index = sector;
  e = hash_find (&block_map, &p.hash_elem);
  return e != NULL ? hash_entry (e, struct cache_block, hash_elem) : NULL;
}