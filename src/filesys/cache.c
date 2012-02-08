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
#include "threads/thread.h"
#include "devices/timer.h"

struct cache_block
{
  struct list_elem list_elem;
  struct hash_elem hash_elem;
  struct lock access_lock;

  block_sector_t sector_index; /* Sector index on filesys block device */
  void* content; /* Cached content of the block */
  bool dirty; /* True if block was written while in cache */
  int referenced; /* Reference counter for clock algorithm */
  bool accessing; /* True if someone is accessing this block right now */
};

static bool enable_cache = true;

/* Interval in which the write behind thread is executed to flush the cache to disk */
static int write_behind_interval_ms = 2000;

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
void cache_write_behind (void* aux UNUSED); 

static struct list block_list;
static struct hash block_map;
static struct lock create_lock;
static struct lock search_lock;
static struct cache_block* hand = NULL;

static int blocks_cached = -1; /* Number of blocks in the cache right now */
static int num_dirty = 0;
static bool write_behind = true;

void
cache_init ()
{
  if (!enable_cache) return;  
  list_init (&block_list);
  hash_init (&block_map, cache_hash, cache_equals, NULL);
  lock_init (&create_lock);
  lock_init (&search_lock);

  blocks_cached = 0;
  hand = NULL;    
  
  thread_create ("WriteBehind", PRI_DEFAULT, cache_write_behind, NULL);
  
  printf("Cache was initialized.\n");
}

void cache_write_behind (void* aux UNUSED)
{      
  while (blocks_cached >= 0 && write_behind)
    {      
      cache_flush ();
      
      timer_msleep (write_behind_interval_ms);
    }     
  
  cache_flush();
}

void cache_exit ()
{  
  write_behind = false;
}

bool cache_enabled ()
{
  return blocks_cached >= 0 && enable_cache;
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

/* Attempts to read the contents of SECTOR into BUFFER from the cache. If 
   SECTOR is not cached, false is returned. */
bool
cache_read (block_sector_t sector, void* buffer)
{
  ASSERT (enable_cache);
  
  // Look up a cache block and lock it for eviction
  struct cache_block* block = cache_lookup (sector);
  if (block == NULL)
    return false;
  
  ASSERT (block->sector_index == sector);

  // copy cached content into buffer
  memcpy (buffer, block->content, BLOCK_SECTOR_SIZE);
  
  // reset access flag so that cache block can be evicted again
  block->accessing = false;

  return true;
}

/* Reads the contents of SECTOR into BUFFER. Starting from OFS only SIZE bytes
   will be copied. If SECTOR is not cached, it will be loaded into the cache. */
void 
cache_read_in (block_sector_t sector, void* buffer, off_t ofs, off_t size)
{
  ASSERT (enable_cache);
  ASSERT (ofs < BLOCK_SECTOR_SIZE);
  ASSERT (ofs + size <= BLOCK_SECTOR_SIZE);
  
  struct cache_block* block = cache_lookup (sector);
  
  // If the sector is not yet cashed, read in into the cash now
  if (block == NULL)
    {
      void* temp = malloc (BLOCK_SECTOR_SIZE);
      if (temp == NULL) 
        PANIC("Out of Memory");
      
      block_read_nocache (fs_device, sector, temp);
      cache_write (sector, temp);
      free (temp);
      
      block = cache_lookup (sector);
      ASSERT (block != NULL);
    }
  
  // Now read the target range into buffer
  memcpy (buffer, block->content + ofs, size);
  
  // Reset accessing flag, so block can be evicted again
  block->accessing = false;
}

void 
cache_write_in (block_sector_t sector, void* buffer, off_t ofs, off_t size)
{
  ASSERT (enable_cache);  
  
  struct cache_block* block = cache_lookup (sector);
  if (block != NULL)
    {
      lock_acquire (&block->access_lock);

      // block is already cached -> overwrite
      memcpy (block->content + ofs, buffer, size);            
      
      if (!block->dirty)
        {
           block->dirty = true;
           num_dirty++;
        }

      lock_release (&block->access_lock);            
      
      block->accessing = false;
      return;
    }

  // Otherwise need to create new cache block
  block = cache_create (sector);
  if (block == NULL) PANIC ("Out Of Memory");

  // Write buffer content into cache
  lock_acquire (&block->access_lock);
  memcpy (block->content + ofs, buffer, size);
  lock_release (&block->access_lock);

  if (!block->dirty)
    {
      block->dirty = true;
      num_dirty++;
    } 
  
  // Reset access flag, so block can be evicted
  block->accessing = false;  
}

/* Writes BUFFER as content for SECTOR into the cache. */
void
cache_write (block_sector_t sector, const void* buffer)
{
  cache_write_in (sector, buffer, 0, BLOCK_SECTOR_SIZE);    
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
  if (block == NULL)
    {
      // Out of Memory
      lock_release (&create_lock);
      return NULL;
    }

  block->content = calloc (1, BLOCK_SECTOR_SIZE);
  if (block->content == NULL)
    {
      // Out of Memory
      free (block);
      lock_release (&create_lock);
      return NULL;
    }
  
  block->accessing = true;
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
  // Search for a block to evict (Clock algorithm), skip blocks that are being
  // accessed right now
  while (hand->referenced != 0 && !hand->accessing)
    {
      if (hand->referenced == 1)
        hand->referenced = 0;

      hand = get_successor (hand);
    }
  
  // The search could have had no success and therefore not moved the hand to 
  // a proper element
  if (hand->accessing || hand->referenced != 0)
    {
      PANIC("Could not find a cache block, that can be evicted");
    }

  // Found a block to evict; move hand to next element
  struct cache_block* evictee = hand;
  hand = get_successor (hand);
  
  int temp = blocks_cached;

  // Remove the block from the cache
  cache_remove (evictee, true);
  
  ASSERT (blocks_cached == temp - 1);
}

void
cache_remove (struct cache_block* block, bool flush)
{
  // Prevent other threads from accessing the cache while this element is removed
  lock_acquire (&search_lock);
  //lock_acquire (&block->access_lock);
  
  // Remove evictee from block lists
  list_remove (&block->list_elem);
  hash_delete (&block_map, &block->hash_elem);
  blocks_cached--;
  
  lock_release (&search_lock);  

  // Write back to file system if required
  if (flush)
    cache_flush_block (block);

  // and free the resources
  free (block->content);
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
            
      ASSERT (block->content != NULL);      
      block_write_nocache (fs_device, block->sector_index, block->content);
      block->dirty = false;
      num_dirty--;

      lock_release (&block->access_lock);
    }
}

/* Flushes the whole cache by writing out all dirty cache blocks. However,
   the cache itself is not cleared or free'd. */
void
cache_flush ()
{    
  if (!cache_enabled () || blocks_cached == 0) return;  
  struct cache_block* p = NULL;    
  struct list_elem* e = list_head (&block_list);
  
  while ((e = list_next (e)) != list_end (&block_list))
    {
      p = list_entry (e, struct cache_block, list_elem); 
      cache_flush_block (p);            
    }      
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
  if (!cache_enabled()) return;  
  
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

/* Looks up a SECTOR in the cache. If the cache contains the sector, the
   corresponding block will be returned, NULL otherwise. 
   Note that the "accessing" flag will be set to true atomically, so that
   the returned cache block cannot be evicted after obtaining it. Therefore,
   when operations on the block are finished, the flag must be reset to false. 
   Otherwise at some point no blocks can be evicted wich will result in a 
   kernel panic. */
struct cache_block*
cache_lookup (block_sector_t sector)
{
  struct cache_block p;
  struct hash_elem* e;
  struct cache_block* block;

  p.sector_index = sector;
  
  lock_acquire (&search_lock);
  
  e = hash_find (&block_map, &p.hash_elem);
  block = e != NULL ? hash_entry (e, struct cache_block, hash_elem) : NULL;
  
  if (block == NULL) 
    {
      lock_release (&search_lock);
      return false;
    }
  else
    {
      block->accessing = true;
      lock_release (&search_lock);
      return block;
    }
}