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
  
  block_sector_t sector_index;      /* Sector index on filesys block device */
  void* content;                    /* Cached content of the block */
  bool dirty;                       /* True if block was written in cache */
  int referenced;                   /* Reference counter for clock algorithm */  
};

unsigned cache_hash (const struct hash_elem* p_, void* aux UNUSED);
bool cache_equals (const struct hash_elem* a_, const struct hash_elem* b_,
                   void* aux UNUSED);
struct cache_block* cache_lookup (block_sector_t sector);
static void cache_init (void);
struct cache_block* get_successor (struct cache_block* block);
void cache_evict ();
struct cache_block* cache_create (block_sector_t sector);

static struct list block_list;
static struct hash block_map;  
static struct lock create_lock;
static int blocks_cached;  /* Number of blocks in the cache right now */
static struct cache_block* hand;

static void 
cache_init ()
{
  list_init (&block_list);
  hash_init (&block_map, cache_hash, cache_equals, NULL);
  lock_init (&create_lock);
  
  blocks_cached = 0;
  hand = NULL;
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
  struct cache_block* block = cache_lookup (sector);
  if (block == NULL)
    return false;
  
  // copy cached content into buffer
  memcpy (block->content, buffer, BLOCK_SECTOR_SIZE);
  
  return true;
}
       
void
cache_write (block_sector_t sector, void* buffer)
{
  struct cache_block* block = cache_lookup (sector);
  if (block != NULL)
    {
      lock_acquire (&block->access_lock);
      
      // block is already cached -> overwrite
      memcpy (buffer, block->content, BLOCK_SECTOR_SIZE);
      block->dirty = true;
      
      lock_release (&block->access_lock);
      return;
    }
  
  // Need to create new cache block
  block = cache_create (sector);
  
  // Write buffer content into cache
  lock_acquire (&block->access_lock);  
  memcpy (buffer, block->content, BLOCK_SECTOR_SIZE);  
  lock_release (&block->access_lock);
}

/* Creates a new cache block for given sector. If the maximum number of 
 * cached blocks is reached, another block is evicted. Returns NULL if
 * no memory can be allocated. */
struct cache_block* cache_create (block_sector_t sector)
{
  lock_acquire (&create_lock);
  
  if (blocks_cached == BUFFER_CACHE_SIZE)
    {
      cache_evict ();
    }
  
  ASSERT (blocks_cached < BUFFER_CACHE_SIZE);
  
  struct cache_block* block = calloc (sizeof(struct cache_block), 1);
  if (block == NULL) return NULL;
  
  block->dirty = false;
  block->referenced = 1;
  block->sector_index = sector;
  lock_init (&block->access_lock);
  
  if (hand == NULL)
    {
      // First entry is inserted in to the last
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
void cache_evict ()
{
  // Search for a block to evict (Clock algorithm)
  struct cache_block* prev = hand;
  
  while (hand->referenced != 0)
    {
      if (hand->referenced == 1)
        hand->referenced = 0;
      
      hand = get_successor (hand);
    }    
  
  ASSERT (hand->referenced == 0);
  
  struct cache_block* evictee = hand;
  hand = get_successor (hand);
  
  // Remove evictee from block lists
  list_remove (&hand->list_elem);
  hash_delete (&block_map, &hand->hash_elem);
  blocks_cached--;
  
  // TODO: Was ist, wenn gerad einer auf den Block zugreifen will?
  
  // Write back to file system if required
  if (evictee->dirty)
    {
      block_write_nocache (fs_device, evictee->sector_index, evictee->content);      
    }
  
  // and free the resources
  free (hand);
}

/* Gets the successor of given block for the clock algorithm, which will always
   be the next element in the list, or the beginning if the end was reached. */
struct cache_block* get_successor (struct cache_block* block)
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