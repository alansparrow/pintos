#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include <string.h>
#include "devices/input.h"
#include "devices/shutdown.h"
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/palloc.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "devices/shutdown.h"

struct file_descriptor {
  int fd_id;            /* FD unique identifier */
  tid_t owner;	        /* thread/process holding this FD */
  struct file *file;    /* reference to filesystem */
  struct list_elem elem;/* element for open_files list */
  char *exec_name;      /* name of the file (used to prevent ivalid writes)
                           in case it is executed currently */
};

struct list open_files;
struct lock file_lock;
int get_unique_fd_id (void);
struct file_descriptor* get_open_file (int fd);
struct file_descriptor* get_owned_file (int fd);

static void syscall_handler (struct intr_frame *);

bool is_valid_uaddr(const void *);
bool are_valid_uaddrs(const void *, int);

void halt (void);
void exit (int) NO_RETURN;
tid_t exec_call (const char *);
bool create (const char *, unsigned);
bool remove (const char *);
int open (const char *);
int filesize (int);
int read (int, void *, unsigned);
int write (int, const void *, unsigned);
void seek (int, unsigned);
int mmap (int fd, void* vaddr);
void munmap (int mmap_id);
unsigned tell (int);
void close (int);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  list_init (&open_files);
  lock_init (&file_lock);
}

void release_files (struct thread* cur) 
{
    /* close all owned files */
    struct list_elem *e;
    struct file_descriptor *fds;

    lock_acquire(&file_lock);
    for (e = list_begin(&open_files); e != list_tail(&open_files); e = list_next(e)) 
    {
        fds = list_entry(e, struct file_descriptor, elem);
        if (fds->owner == cur->tid) 
        {
            e = list_prev(e);
            list_remove(&fds->elem);
            file_close(fds->file);
            free(fds->exec_name);
            free(fds);
        }
    }
    lock_release(&file_lock);    
}

static void
syscall_handler (struct intr_frame *f) 
{
  uint32_t* esp = f->esp;
  
  /* is zero-th parameter in userspace? */
  if (!is_valid_uaddr (esp))
    {
      exit (-1);
    }  
  int syscall_nr = *esp;
  
  /* check all other parameters */

  /* check first parameter */
  switch (syscall_nr)
    {
      case SYS_EXIT:
      case SYS_EXEC:
      case SYS_WAIT:
      case SYS_CREATE:
      case SYS_REMOVE:
      case SYS_OPEN:
      case SYS_FILESIZE:
      case SYS_READ:
      case SYS_WRITE:
      case SYS_SEEK:
      case SYS_TELL:
      case SYS_CLOSE:
      case SYS_MMAP:
      case SYS_MUNMAP:
        if (!is_valid_uaddr (esp + 1))
          {
            exit (-1);
          } 
    }
  
  /* check second parameter */
  switch (syscall_nr)
    {
      case SYS_CREATE:
      case SYS_READ:
      case SYS_WRITE:
      case SYS_SEEK:
      case SYS_MMAP:
        if (!is_valid_uaddr (esp + 2))
          {
            exit (-1);
          } 
    }
	
  /* check third parameter */
  switch (syscall_nr)
    {
      case SYS_READ:
      case SYS_WRITE:
      if (!is_valid_uaddr (esp + 3))
        {
          exit (-1);
        } 
    }
	
  /* execute system call */
  switch (syscall_nr)
    {
      case SYS_HALT:
        shutdown_power_off ();
        break;
      case SYS_EXIT:
        exit (* (esp + 1));
        break;
      case SYS_EXEC:
        f->eax = exec_call ( (char *) * (esp + 1));
        break;
      case SYS_WAIT:
        f->eax = process_wait (* (esp + 1));
        break;
      case SYS_CREATE:
        f->eax = create ( (char *) * (esp + 1), * (esp + 2));
        break;
      case SYS_REMOVE:
        f->eax = remove ( (char *) * (esp + 1));
        break;
      case SYS_OPEN:
        f->eax = open ( (char *) * (esp + 1));
        break;
      case SYS_FILESIZE:
        f->eax = filesize (* (esp + 1));
        break;
      case SYS_READ:
        f->eax = read (* (esp + 1), (void *) * (esp + 2), * (esp + 3));
        break;
      case SYS_WRITE:
        f->eax = write (* (esp + 1), (void *) * (esp + 2), * (esp + 3));
        break;
      case SYS_SEEK:
        seek (* (esp + 1), * (esp + 2));
        break;
      case SYS_TELL:
        f->eax = tell (*(esp + 1));
        break;
      case SYS_CLOSE:
        close (* (esp + 1));
        break;
      case SYS_MMAP:
        f->eax = mmap (*(esp + 1), *(esp + 2));
        break;
      case SYS_MUNMAP:
        munmap (*(esp + 1));
        break;
      default:
        exit (-1);
    }
}

/* Halt the operating system. */
void 
halt (void) 
{
  shutdown_power_off ();
}

/* Prints the processes state and Name before
   terminating the process 
   (should only be called by userprocesses) */
void 
exit (int status) 
{
  struct thread *cur = thread_current ();
  char *p_name, *aux = cur->name;

  p_name = strtok_r (cur->name, " ", &aux);
  printf ("%s: exit(%d)\n", p_name, status);
  
  release_files (cur);
  	  
  if (cur->parent != NULL && cur->own_exit_status != NULL)
    {
      cur->own_exit_status->status = status;
      /* thread_exit wakes waiting process if existing. */
    }
  
  thread_exit ();
}

/* Execute syscall:
   Creates a new thread and loads given file to execute (if exists).
   Returns tid of new thread or -1 if execution failed.  */
tid_t
exec_call (const char *file)
{
  tid_t new_pid = -1;
  /* get filename */
  char *p_name, *aux, *buf = palloc_get_page ( (enum palloc_flags)0);
  if (buf == NULL)
    {
      return -1;
    }
  ASSERT (strlcpy (buf, file, PGSIZE) > 0);
  aux = buf;
  p_name = strtok_r (NULL, " ", &aux);

  /* look if file exists */
  lock_acquire (&file_lock);
  struct file *f = filesys_open (p_name);
  lock_release (&file_lock);
  palloc_free_page (buf);

  /* if file exists create new process */
  if (f != NULL)
    {
      new_pid = process_execute (file);
    }
  return new_pid;
}

/* Creates a new file called file initially initial_size 
bytes in size. Returns true if successful, false otherwise.
Creating a new file does not open it: opening the new file 
is a separate operation which would require a open system call. */
bool 
create (const char *file, unsigned initial_size)
{
  if (!is_valid_uaddr (file))
    {
      exit (-1);
    }
  bool ret = false;
  lock_acquire (&file_lock);
  ret = filesys_create (file, initial_size);
  lock_release (&file_lock);
  return ret;
}

/* Deletes the file called file. Returns true if successful, 
false otherwise. A file may be removed regardless of whether 
it is open or closed, and removing an open file does not 
close it */
bool 
remove (const char *file) 
{
  if (!is_valid_uaddr (file))
    {
      exit (-1);
    }
  bool ret = false;
  lock_acquire (&file_lock);
  ret = filesys_remove (file);
  lock_release (&file_lock);
  return ret;
}

/* Opens the file called file. Returns a nonnegative integer 
handle called a "file descriptor" (fd), or -1 if the file 
could not be opened.  */
int 
open (const char *file) 
{	
  struct file_descriptor *fd;
  struct file *f;
  int status = -1;

  /* check if pointer is in userspace */
  if (!is_valid_uaddr (file))
    {
      exit (-1);
    }
	
  /* get file */
  lock_acquire (&file_lock);
  f = filesys_open (file);
  if (f != NULL)
    {
      fd = (struct file_descriptor *) malloc (sizeof (struct file_descriptor));
      /* abort if no memory */
      if (fd == NULL)
        {
          file_close (f);
          status = -1;
        }
      else
        {
          fd->fd_id = get_unique_fd_id ();
          fd->owner = thread_current ()->tid;
          fd->file = f;

          fd->exec_name = malloc (strlen (file) + 1); 
          ASSERT (strlcpy (fd->exec_name, file, strlen (file) + 1) > 0);
       
         list_push_back (&open_files, &fd->elem);
         status = fd->fd_id;
       }
   }
  lock_release (&file_lock);
  return status;
}

/* Returns the size, in bytes, of the file open as fd.  */
int 
filesize (int fd) 
{
  int ret = -1;
  lock_acquire (&file_lock);
  struct file_descriptor* fds = get_owned_file (fd);
  if (fds != NULL)
    {
      ret = file_length (fds->file);
    }
  lock_release (&file_lock);
  return ret;
}

void munmap (int mmap_id)
{
  struct thread* thread = thread_current ();
  int i;
  
  if (mmap_id < 0 || mmap_id > list_size (&thread->mmappings) - 1) 
    exit (-1);
    
  /*
  struct mmapping* mapping = list_get (&thread->mmappings, mmap_id);
  ASSERT (mapping != NULL && mapping->fd > 1);
  
  int bytes_left = mapping->length;
  int pos_before = tell (mapping->fd);
  int num_pages = (mapping->length % PGSIZE) + 1;
  
  // Write back modified pages
  while (bytes_left > 0)
    {
      int read_bytes = bytes_left >= PGSIZE ? PGSIZE : (bytes_left % PGSIZE);
      void* kpage = mapping->kpage + i * PGSIZE;
      void* vaddr = mapping->vaddr + i * PGSIZE;
      
      if (pagedir_is_dirty (&thread->pagedir, vaddr))
        {
          // write back to file, if page was written into
          seek (mapping->fd, mapping->length - bytes_left);
          write (mapping->fd, vaddr, read_bytes);                    
        }
            
      bytes_left -= read_bytes;                  
    }
  */
  // Free used pages
  //frametable_free_pages (mapping->kpage, num_pages, false);
  
  // Don't use this mapping any more (can be replaced by a new one)
  //mapping->fd = -1;
  //mapping->kpage = NULL;
  //mapping->length = 0;
  //mapping->mmap_id = -1;
  //mapping->vaddr = NULL;
  
  // Reset file pointer
  //seek (mapping->fd, pos_before);
}

bool is_mapping_possible (void* vaddr, int length)
{
  struct thread* thread = thread_current ();
  int stack_bottom = PHYS_BASE - thread->num_stack_pages * PGSIZE;
  
  // Don't overlap stack
  if (vaddr + length >= stack_bottom)
    return false;
  
  // Don't overlap code
  struct page_suppl* spte = suppl_get (vaddr);
  if (spte != NULL && spte->origin == from_executable)
    {      
      return false;
    }
  
  return true;
}

int mmap (int fd, void* vaddr) 
{  
  struct thread* thread = thread_current ();
  int stack_bottom = PHYS_BASE - thread->num_stack_pages * PGSIZE;
  
  // Check for valid file and valid page-aligned address
  if (fd < 2 || vaddr == NULL || pg_ofs (vaddr) > 0 || vaddr >= stack_bottom)
    return -1;
    
  int length = filesize (fd);
  int num_pages = length / PGSIZE + 1;
  int i;  
  
  // Check for overlaps etc.
  if (!is_mapping_possible (vaddr, length))
    return -1;
  
  void* pages = frametable_get_pages (num_pages);
  int bytes_left = length;  
  int pos_before = tell (fd);
  
  for (i = 0; i < num_pages; i++)
    {
      void* kpage = pages + PGSIZE * i;
      void* upage = vaddr + PGSIZE * i;
      int read_bytes = bytes_left >= PGSIZE ? PGSIZE : (length % PGSIZE);           
      
      bool success = /*pagedir_get_page (thread->pagedir, upage) == NULL &&*/
                     pagedir_set_page (thread->pagedir, upage, kpage, true);            
      
      if (!success)
        PANIC ("Could not install Page");
      
      read (fd, upage, read_bytes);      
      bytes_left -= read_bytes;
      
      if (bytes_left == 0 && length % PGSIZE > 0)
        {
          // fill the rest of the last page with zeroes
          memset (upage + read_bytes, 0, length % PGSIZE);
        }
    }       
  
  seek (fd, pos_before);
  
  // Create Memory Mapping entry
  struct mmapping* mapping = malloc (sizeof(struct mmapping));
  mapping->fd = fd;
  mapping->length = length;
  mapping->mmap_id = list_size (&thread->mmappings);
  mapping->vaddr = vaddr;
  mapping->kpage = pages;
  
  list_push_back (&thread->mmappings, &mapping->elem);
  
  return mapping->mmap_id;
}

/* Reads size bytes from the file open as fd into buffer. 
Returns the number of bytes actually read (0 at end of file), 
or -1 if the file could not be read (due to a condition other 
than end of file). */
int 
read (int fd, void *buffer, unsigned length) 
{
  if (!are_valid_uaddrs (buffer, length))
    {      
      exit (-1);
    }
  int status = 0;
  lock_acquire (&file_lock);
  if (fd == STDOUT_FILENO)
    {
      status = -1;
    }
  else if (fd == STDIN_FILENO) 
    {
      /* read from stdin */
      int i = 0;
      char c;
      char* buf = (char *) buffer;
      while (length > 1 && (c = input_getc ()))
        {
          buf[i] = c;
          i++;
          length--;
        }
      buf[i] = 0;
      status = i;
    }
  else 
    {
      /* write to file */
      struct file_descriptor* fds = get_owned_file(fd);
      if (fds != NULL)
        {
          status = file_read(fds->file, buffer, length);
        }
      else
        {
          lock_release (&file_lock);
          exit (-1);
        }
    }
  lock_release (&file_lock);
  return status;
}

/* Writes size bytes from buffer to the open file fd. 
Returns the number of bytes actually written, which may 
be less than size if some bytes could not be written.  */
int 
write (int fd, const void *buffer, unsigned length) 
{
  if (!are_valid_uaddrs (buffer, length))
    {
      exit (-1);
    }
  int status = 0;
  lock_acquire (&file_lock);
  if (fd == STDIN_FILENO)
    {
      status = -1;
    }
  else if (fd == STDOUT_FILENO) 
    {
      /* write to stdout */
      putbuf (buffer, length);
      status = (int) length;
    }
  else 
    {
      /* write to file if not executed */
      struct file_descriptor* fds = get_owned_file (fd);
      if (fds != NULL)
        {
          if (file_executed (fds->exec_name))
            {
              file_deny_write (fds->file);
            }
          else
            {
              file_allow_write (fds->file);
            }
          status = file_write (fds->file, buffer, length);
        }
      else
        {
          lock_release (&file_lock);
	  exit (-1);
        }
    }
  lock_release(&file_lock);
  return status;
}

/* Changes the next byte to be read or written in
open file fd to position, expressed in bytes from 
the beginning of the file. */
void 
seek (int fd, unsigned position)
{
  lock_acquire (&file_lock);
  struct file_descriptor* fds = get_owned_file (fd);
  if (fds != NULL)
    {
      file_seek (fds->file, position);
    }
  lock_release (&file_lock);
}

/* Returns the position of the next byte to be read 
or written in open file fd, expressed in bytes from 
the beginning of the file. */
unsigned 
tell (int fd) 
{
  unsigned ret = 0;
  lock_acquire (&file_lock);
  struct file_descriptor* fds = get_owned_file (fd);
  if (fds != NULL)
    {
      ret = file_tell (fds->file);
    }
  lock_release (&file_lock);
  return ret;
}

/* Closes file descriptor fd. Exiting or terminating 
a process implicitly closes all its open file 
descriptors, as if by calling this function for 
each one.  */
void 
close (int fd) 
{
  struct file_descriptor *fds;
  lock_acquire (&file_lock);
  fds = get_owned_file (fd);
  if (fds != NULL)
    {
      list_remove (&fds->elem);
      file_close (fds->file);
      free (fds->exec_name);	
      free (fds);
    }
  else
    {
      lock_release (&file_lock);
      exit (-1);
    }
  lock_release (&file_lock);
}

/* Checks if given address is a vaild userprocess address. */
bool
is_valid_uaddr (const void *upointer) 
{
  struct thread *cur = thread_current ();
  if (upointer != NULL && is_user_vaddr (upointer))
    {
      return (pagedir_get_page (cur->pagedir, upointer)) != NULL;
    }
  return false; 
}

/* Check if the whole memory area from given pointer to pointer + offset
is valid in userporcess space */
bool
are_valid_uaddrs (const void *upointer, int size) 
{
  while(size > 0) 
    {
      /* check current "page" */
      if (!is_valid_uaddr (upointer))
        {
	  return false;
        }
        /* get next "page" */
        if(size >= PGSIZE)
          {
            size -= PGSIZE;
            upointer += PGSIZE;
          }
        else
          {
            /* check last address */
            upointer += size - 1;
            size = 0;
            if (!is_valid_uaddr (upointer)) 
              {
                return false;	
              }
       }
    }
  return true;
}

/* Returns a new unique file descriptor id */
int
get_unique_fd_id () 
{
  static int id = STDOUT_FILENO + 1;
  return id++;
}

/* Get a opened file by its fd */
struct file_descriptor *
get_open_file (int fd) 
{
  struct list_elem *e;
  struct file_descriptor *fds; 
  for(e = list_begin (&open_files); e!=list_tail (&open_files); 
      e = list_next (e))
    {
      fds = list_entry (e, struct file_descriptor, elem);
      if (fds->fd_id == fd)
        {
          return fds;
        }
    }
  return NULL;
}

/* Get the file if current_thread is the owner. */
struct file_descriptor *
get_owned_file (int fd) 
{
  struct file_descriptor* fds = get_open_file (fd);
  if (fds == NULL)
    {
      /* always call with file_lock hold */
      lock_release (&file_lock);
      exit (-1);
    }
  if (fds->owner == thread_current ()->tid)
    {
      return fds;
    }
  else
    {
      return NULL;
    }
}
