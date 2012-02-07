#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

#define INDEX_SIZE 120

/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE (512) bytes long. */
struct inode_disk
  {
    block_sector_t inode_sector;         /* Sector of this inode */
    off_t length;                       /* File size in bytes. */
    unsigned magic;                     /* Magic number. */
    
    //===== ^^ 12 Bytes ^^ =====
    
    int num_sectors;                    /* Number of referenced sectors */    
    block_sector_t sectors[INDEX_SIZE]; /* References to used sectors for data */
    
    int num_meta_nodes;                 /* Number of linked inode_disk blocks */
    block_sector_t prev;                /* Previous inode belong to this file */
    block_sector_t next;                /* Next inode belonging to this file */
    block_sector_t tail;        /* Pointer to last meta node, only for head */
    
    //===== ^^ 500 Bytes ^^ ====
    
    //uint32_t unused[125];               /* Not used. */
  };
  
/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, BLOCK_SECTOR_SIZE);  
}

bool inode_disk_valid (struct inode_disk* inode)
{
  int* p = (int*)inode;
  return p[2] == INODE_MAGIC;
}

/* In-memory inode. */
struct inode 
  {
    struct list_elem elem;              /* Element in inode list. */
    block_sector_t sector;              /* Sector number of disk location. */
    int open_cnt;                       /* Number of openers. */
    bool removed;                       /* True if deleted, false otherwise. */
    int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
    struct inode_disk** data;           /* Inode contents. */
    int num_disk_inodes;                /* number of linked disk inodes */
  };

/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static block_sector_t
byte_to_sector (const struct inode *inode, off_t pos) 
{
  ASSERT (inode != NULL);
  
  if (pos > inode->data[0]->length)
    return -1;
  
  // Each inode can point to INDEX_SIZE sectors. If this number is reached,
  // a new inode is referenced as "next" that contains the following sector 
  // indices, etc.
  int bytes_per_inode = BLOCK_SECTOR_SIZE * INDEX_SIZE;
  int inode_offset = pos / bytes_per_inode;
  pos -= inode_offset * bytes_per_inode;
  int index = pos / BLOCK_SECTOR_SIZE;
  
  ASSERT (inode_offset < inode->num_disk_inodes);
  ASSERT (index < inode->data[inode_offset]->num_sectors);
    
  return inode->data[inode_offset]->sectors[index];
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

/* Initializes the inode module. */
void
inode_init (void) 
{
  list_init (&open_inodes);
}

/* Appends a new sector to the given disk INODE with BUFFER as content.
   If the index table is full, a new inode is created and pointed to by the next 
   pointer, with the new sector being referenced as the first index table entry 
   of the new inode. */
bool
inode_disk_append_sector (struct inode_disk* inode, const void* buffer)
{ 
  struct inode_disk* tail = NULL;
  
  // The new sector is going to be appended at the tail of the linked list
  if (inode->tail != inode->inode_sector && inode->tail > 0)
    {
      // the tail must be loaded from disk
      tail = malloc (sizeof (struct inode_disk));
      if (tail == NULL) 
        return false;
      block_read (fs_device, inode->tail, tail);
    }
  else
    {
      // There's only one node so far or given inode is the tail itself
      tail = inode;
    }
  
  // Check if the new sector fits into tail's sector table
  if (tail->num_sectors < INDEX_SIZE)
    {
      // Allocate sector for the data
      block_sector_t sector = 0;  
      if (!free_map_allocate (1, &sector))
        return false;
      
      // Write new data block to disk
      block_write (fs_device, sector, buffer);
      tail->sectors[tail->num_sectors++] = sector;
      tail->length += BLOCK_SECTOR_SIZE;
      
      // Update the tail itself on disk (it's cheap 'cause it's cached anyway)
      block_write (fs_device, tail->inode_sector, tail);
      
      // Update the head of the list too
      if (tail != inode) 
        {
          inode->length += BLOCK_SECTOR_SIZE;
          block_write (fs_device, inode->inode_sector, inode);
          free (tail);
        }
      
      return true;
    }    
  
  // If no space is left in the tail's index table, a new node has to be created
  struct inode_disk* old_tail = tail;
  struct inode_disk* next = calloc (1, sizeof (struct inode_disk));
  if (next == NULL)
    return false;

  next->magic = INODE_MAGIC;
  next->next = 0;
  next->prev = tail->inode_sector;         
  next->length = inode->length;  
  next->num_meta_nodes = inode->num_meta_nodes + 1;
  next->num_sectors = 0;

  // Allocate a sector for it on the file system
  block_sector_t next_inode_sector = 0;
  if (!free_map_allocate (1, &next_inode_sector))
    {
      free (next);
      if (tail != inode)
        free (tail);
      return false;
    }

  next->inode_sector = next_inode_sector;
  next->tail = next_inode_sector;
  
  // Update old tail and head
  old_tail->next = next_inode_sector;
  old_tail->num_meta_nodes = tail->num_meta_nodes;  
  inode->tail = next_inode_sector;
  inode->num_meta_nodes = tail->num_meta_nodes;    
  
  // Append new sector to new tail; also writes new tail to disk
  bool success = inode_disk_append_sector (next, buffer);
  
  // Write changes in old tail and head to disk
  if (success)
    {
      block_write (fs_device, old_tail->inode_sector, old_tail);
      if (old_tail != inode)
        block_write (fs_device, inode->inode_sector, inode);
    }
  
  // If tail was read from disk it can be free'd now
  if (old_tail != inode)
    free (old_tail);
  
  return success;
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create (block_sector_t sector, off_t length)
{
  struct inode_disk *disk_inode = NULL;
  bool success = false;

  //ASSERT (length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof *disk_inode == BLOCK_SECTOR_SIZE);

  disk_inode = calloc (1, sizeof *disk_inode);
  if (disk_inode != NULL)
    {      
      size_t sectors = bytes_to_sectors (length);
      disk_inode->length = 0;
      disk_inode->magic = INODE_MAGIC;
      disk_inode->inode_sector = sector;
      disk_inode->prev = 0;
      disk_inode->next = 0;
      disk_inode->tail = sector; 
      disk_inode->num_meta_nodes = 1;
      disk_inode->num_sectors = 0;      
            
      // Allocate sectors       
      static char zeros[BLOCK_SECTOR_SIZE];
      
      while (sectors > 0)
        {
          if (!inode_disk_append_sector(disk_inode, zeros))
            {
              PANIC("TODO: free_map_allocate == false behandeln");
              return false;
            }
          sectors--;
        }      

      disk_inode->length = length;
      block_write (fs_device, sector, disk_inode);
      
      success = true;      
      free (disk_inode);
    }
  return success;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (block_sector_t sector)
{
  struct list_elem *e;
  struct inode *inode;

  /* Check whether this inode is already open. */
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e)) 
    {
      inode = list_entry (e, struct inode, elem);
      if (inode->sector == sector) 
        {
          inode_reopen (inode);
          return inode; 
        }
    }

  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  if (inode == NULL)
    return NULL;

  /* Initialize. */
  list_push_front (&open_inodes, &inode->elem);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  inode->num_disk_inodes = 0;
  
  // Store data nodes in a fixed size array for easier access later on
  inode->data = NULL;    
  struct inode_disk* data = NULL;
  
  do
    {
      // Load one inode_disk node; the first lies in SECTOR, the rest is linked
      int next_sector = data == NULL ? inode->sector : data->next;
      data = malloc (sizeof (struct inode_disk));
      
      if (data == NULL) PANIC("Out of Memory");    
      block_read (fs_device, next_sector, data);      
      ASSERT (inode_disk_valid (data));
      
      // The first block contains the number of linked blocks, use it to create
      // the array to store all meta data nodes
      if (inode->data == NULL)
        {          
          inode->data = malloc (sizeof(struct inode_disk) * data->num_meta_nodes);
          if (inode->data == NULL) 
            {
              hex_dump (0, data, 512, true);
              printf("Failed to allocate %d nodes for inode at sector %d\n", 
                     data->num_meta_nodes, sector);
              PANIC("Out of Memory");
            }
        }
      
      // Save element in data array
      inode->data[inode->num_disk_inodes++] = data;      
    }
  while (data->next > 0);   
  
  return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
  if (inode != NULL)
    inode->open_cnt++;
  return inode;
}

/* Returns INODE's inode number. */
block_sector_t
inode_get_inumber (const struct inode *inode)
{
  return inode->sector;
}

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode) 
{
  /* Ignore null pointer. */
  if (inode == NULL)
    return;

  /* Release resources if this was the last opener. */
  if (--inode->open_cnt == 0)
    {
      /* Remove from inode list and release lock. */
      list_remove (&inode->elem);
 
      /* Deallocate blocks if removed. */
      if (inode->removed) 
        {
          int i, j;
          
          // Free all linked sectors
          for (i = 0; i < inode->num_disk_inodes; i++)
            {
              // Release the referenced data blocks on the disk
              for (j = 0; j < inode->data[i]->num_sectors; j++)
                {
                  free_map_release (inode->data[i]->sectors[j], 1);
                }
              
              // Release the block containing this inode
              free_map_release (inode->data[i]->inode_sector, 1);

              // The inode structure itself was dynamically allocated, free it
              free (inode->data[i]);
            }
        }      
      
      free (inode); 
    }
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode) 
{
  ASSERT (inode != NULL);
  inode->removed = true;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at_old (struct inode *inode, void *buffer_, off_t size, off_t offset) 
{
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;
  uint8_t *bounce = NULL;

  while (size > 0) 
    {
      /* Disk sector to read, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          /* Read full sector directly into caller's buffer. */
          block_read (fs_device, sector_idx, buffer + bytes_read);
        }
      else 
        {
          /* Read sector into bounce buffer, then partially copy
             into caller's buffer. */
          if (bounce == NULL) 
            {
              bounce = malloc (BLOCK_SECTOR_SIZE);
              if (bounce == NULL)
                break;
            }
          block_read (fs_device, sector_idx, bounce);
          memcpy (buffer + bytes_read, bounce + sector_ofs, chunk_size);
        }
      
      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }
  free (bounce);

  return bytes_read;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset) 
{    
  off_t bytes_read = 0;
  
  while (size > 0)
    {
      // Disk sector to read, starting byte offset within sector. 
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      // Bytes left in inode, bytes left in sector, lesser of the two. 
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      // Number of bytes to actually copy out of this sector.
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;
      
      // Reads the data range from cache directly
      cache_read_in (sector_idx, buffer_ + bytes_read, sector_ofs, chunk_size);
      
      // Advance
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }
  
  return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
off_t
inode_write_at_old (struct inode *inode, const void *buffer_, off_t size,
                off_t offset) 
{
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;
  uint8_t *bounce = NULL;

  if (inode->deny_write_cnt)
    return 0;

  while (size > 0) 
    {
      /* Sector to write, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          /* Write full sector directly to disk. */
          block_write (fs_device, sector_idx, buffer + bytes_written);
        }
      else 
        {
          /* We need a bounce buffer. */
          if (bounce == NULL) 
            {
              bounce = malloc (BLOCK_SECTOR_SIZE);
              if (bounce == NULL)
                break;
            }

          /* If the sector contains data before or after the chunk
             we're writing, then we need to read in the sector
             first.  Otherwise we start with a sector of all zeros. */
          if (sector_ofs > 0 || chunk_size < sector_left) 
            block_read (fs_device, sector_idx, bounce);
          else
            memset (bounce, 0, BLOCK_SECTOR_SIZE);
          memcpy (bounce + sector_ofs, buffer + bytes_written, chunk_size);
          block_write (fs_device, sector_idx, bounce);
        }

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }
  free (bounce);

  return bytes_written;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset) 
{
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;
  uint8_t *bounce = NULL;

  if (inode->deny_write_cnt)
    return 0;

  while (size > 0) 
    {
      /* Sector to write, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          /* Write full sector directly to disk. */
          block_write (fs_device, sector_idx, buffer + bytes_written);
        }
      else 
        {
          /* We need a bounce buffer. */
          if (bounce == NULL) 
            {
              bounce = malloc (BLOCK_SECTOR_SIZE);
              if (bounce == NULL)
                break;
            }

          /* If the sector contains data before or after the chunk
             we're writing, then we need to read in the sector
             first.  Otherwise we start with a sector of all zeros. */
          if (sector_ofs > 0 || chunk_size < sector_left) 
            block_read (fs_device, sector_idx, bounce);
          else
            memset (bounce, 0, BLOCK_SECTOR_SIZE);
          memcpy (bounce + sector_ofs, buffer + bytes_written, chunk_size);
          block_write (fs_device, sector_idx, bounce);
        }

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }
  free (bounce);

  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode) 
{
  inode->deny_write_cnt++;
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode) 
{
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode)
{
  return inode->data[0]->length;  
}
