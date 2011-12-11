#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "filesys/filesys.h"
#include "devices/shutdown.h"
#include "list.h"
#include "threads/synch.h"
#include "userprog/process.h"
#include "threads/malloc.h"
#include "filesys/file.h"
#include "filesys/inode.h"
#include "devices/input.h"

#define VALIDATE_P(p) if (!valid_pointer(p)) exit (-1)
#define VALIDATE_ESP1(p) if (!valid_pointer(p)) exit (-1)
#define VALIDATE_ESP2(p) if (!valid_pointer(p) || !valid_pointer(p+4)) exit (-1)
#define VALIDATE_ESP3(p) if (!valid_pointer(p) || !valid_pointer(p+4) || !valid_pointer(p+8)) exit (-1)

struct file_elem
{
  struct list_elem elem;
  struct file* file;
  int fd;
};

static struct lock fd_lock;
static struct lock file_lock;
static struct lock file_list_lock;
static struct list opened_files;

struct file_elem* get_file_elem (int fd);
static void syscall_handler (struct intr_frame *);
bool valid_pointer (const void* uaddr);
int allocate_fd (void);
void exit (int status);

void syscall_halt (struct intr_frame *f);
void syscall_exit (struct intr_frame *f);
void syscall_exec (struct intr_frame *f);
void syscall_create (struct intr_frame *f);
void syscall_open (struct intr_frame *f);
void syscall_write (struct intr_frame *f);
void syscall_remove (struct intr_frame *f);
void syscall_filesize (struct intr_frame *f);
void syscall_close (struct intr_frame *f);
void syscall_tell (struct intr_frame *f);
void syscall_seek (struct intr_frame *f);
void syscall_read (struct intr_frame *f);
void syscall_wait (struct intr_frame *f);

int
allocate_fd (void)
{
  static int next_fd = 2;
  int fd;

  lock_acquire (&fd_lock);
  fd = next_fd++;
  lock_release (&fd_lock);

  return fd;
}

void
syscall_init (void)
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");

  list_init (&opened_files);
  lock_init (&file_lock);
  lock_init (&file_list_lock);
  lock_init (&fd_lock);
}

void
syscall_exit (struct intr_frame *f)
{
  void* esp = f->esp + 4;
  VALIDATE_P (esp);
  int status = *((int*) esp);
  
  struct thread* t = thread_current ();
  t->exit_code = status;
  
  // TODO: Close open files of this process
  
  exit (status);
}

void
exit (int status)
{
  struct thread* t = thread_current ();

  int i = 0;
  while (t->name[i] != ' ' && t->name[i] != '\0') i++;
  char temp = t->name[i];
  t->name[i] = '\0';

  printf ("%s: exit(%d)\n", t->name, status);

  t->name[i] = temp;

  thread_exit ();
}

void
syscall_exec (struct intr_frame *f)
{
  void* esp = f->esp + 4;
  VALIDATE_ESP1 (esp);

  char* file_name = *((char**) esp);
  VALIDATE_P (file_name);

  // Starts a new process and waits until it has successfully loaded or failed
  tid_t tid = process_execute (file_name, true);

  // PID == TID as return value
  f->eax = tid;
}

void
syscall_create (struct intr_frame *f)
{
  void* esp = f->esp + 4;
  VALIDATE_ESP2 (esp);

  char* filename = *((char**) esp);
  VALIDATE_P (filename);

  esp += 4;
  int initial_size = *(int*) esp;

  if (initial_size < 0)
    {
      f->eax = -1;
      return;
    }
  
  lock_acquire (&file_lock);
  f->eax = filesys_create (filename, initial_size);
  lock_release (&file_lock);
}

void
syscall_open (struct intr_frame *f)
{
  void* esp = f->esp + 4;
  VALIDATE_ESP1 (esp);

  char* filename = *((char**) esp);
  VALIDATE_P (filename);
  
  lock_acquire (&file_lock);
  struct file* file = filesys_open (filename);
  lock_release (&file_lock);
  
  if (file == NULL)
    {
      f->eax = -1;
      return;
    }

  // Add entry to list of opened files
  struct file_elem* file_entry = malloc (sizeof (struct file_elem));
  if (file_entry == NULL)
    {
      file_close (file);
      f->eax = -1;
      return;
    }
  
  file_entry->fd = allocate_fd ();
  file_entry->file = file;

  ASSERT (file_entry->fd > 1);

  lock_acquire (&file_list_lock);
  list_push_back (&opened_files, &file_entry->elem);
  lock_release (&file_list_lock);
  
  f->eax = file_entry->fd;
}

void
syscall_write (struct intr_frame *f)
{
  void* esp = f->esp + 4;
  VALIDATE_ESP3 (esp);

  // File Handle of File to write into
  int fd = *((int*) esp);
  if (fd != 1 && fd < 2) exit (-1);
  
  // Buffer to write
  esp += 4;
  int dest_addr = *((int*) esp);
  VALIDATE_P ((void*)dest_addr);
  void* buffer = (void*)dest_addr;
  
  // Number of Bytes to write
  esp += 4;  
  int size = *((int*) esp);  

  // Write into stdout
  if (fd == 1)
    {
      putbuf (buffer, size);
      f->eax = size;  
      return;
    }
  
  struct file_elem* file_elem = get_file_elem (fd);
  if (file_elem == NULL)
    {
      exit (-1);
    }  
  
  lock_acquire (&file_lock);
  int written = file_write (file_elem->file, buffer, size);
  lock_release (&file_lock);
  
  f->eax = written;  
}

void syscall_remove (struct intr_frame *f)
{
  void* esp = f->esp + 4;
  VALIDATE_ESP1 (esp);
  
  char* file_name = *((char**)esp);
  VALIDATE_P (file_name);
  
  lock_acquire (&file_lock);
  bool success = filesys_remove (file_name);
  lock_release (&file_lock);
  
  f->eax = success;
}

/**
 * Searches for a file list entry with given fd and returns it, if it exists.
 * Otherwise NULL is returned.
 * @param fd File Descriptor
 * @return File list entry or NULL
 */
struct file_elem* get_file_elem (int fd)
{
  struct file_elem* file = NULL;
  
  lock_acquire (&file_list_lock);

  struct list_elem* e = list_begin (&opened_files);
  while (e != list_end (&opened_files))
    {
      struct file_elem* w = (struct file_elem*) e;          

      if (w->fd == fd) 
        {
          file = w;
          break;
        }
    }
  
  lock_release (&file_list_lock);  
  return file;
}

void syscall_filesize (struct intr_frame *f)
{
  void* esp = f->esp + 4; // skip syscall number
  VALIDATE_ESP1 (esp);
  
  int fd = *((int*)esp);
  
  // Cannot determine size of console in/out
  if (fd < 2)
    {
      f->eax = -1;
      return;
    }
  
  // Search for file with this fd
  struct file_elem* file_elem = get_file_elem (fd);
  
  if (file_elem == NULL)
    {
      f->eax = -1;
    }
  else
    {
      ASSERT (file_elem->file != NULL);
      lock_acquire (&file_lock);
      f->eax = file_length (file_elem->file);
      lock_release (&file_lock);
    }
}

void syscall_close (struct intr_frame *f)
{
  void* esp = f->esp + 4; // skip syscall number on stack
  VALIDATE_ESP1 (esp);
  
  int fd = *((int*)esp);  
  if (fd < 2) exit (-1);    
  
  struct file_elem* file_elem = get_file_elem (fd);
  if (file_elem == NULL)
    {
      // nothing to do...
      return;
    }
  
  lock_acquire (&file_lock);
  file_close (file_elem->file);
  lock_release (&file_lock);
  
  // Remove file list entry
  lock_acquire (&file_list_lock);
  list_remove (&file_elem->elem);
  lock_release (&file_list_lock);
  free (file_elem);
}

void
syscall_tell (struct intr_frame *f)
{
  void* esp = f->esp + 4; // skip syscall number on stack
  VALIDATE_ESP1 (esp);
  
  int fd = *((int*)esp);  
  if (fd < 2) exit (-1);    
  
  struct file_elem* file_elem = get_file_elem (fd);
  if (file_elem == NULL)
    {
      exit (-1);
    }
  
  lock_acquire (&file_lock);
  int32_t pos = file_tell (file_elem->file);
  lock_release (&file_lock);
  
  f->eax = pos;
}

void
syscall_seek (struct intr_frame *f)
{
  void* esp = f->esp + 4; // skip syscall number on stack
  VALIDATE_ESP2 (esp);
  
  int fd = *((int*)esp);  
  if (fd < 2) exit (-1);  
  
  esp += 4;
  uint32_t pos = *((uint32_t*)esp);
  
  struct file_elem* file_elem = get_file_elem (fd);
  if (file_elem == NULL)
    {
      exit (-1);
    }  
  
  lock_acquire (&file_lock);
  file_seek (file_elem->file, pos);
  lock_release (&file_lock);
}

void
syscall_read (struct intr_frame *f)
{
  void* esp = f->esp + 4; // skip syscall number on stack
  VALIDATE_ESP3 (esp);
  
  // File Handle
  int fd = *((int*)esp);  
  if (fd != 0 && fd < 2) exit (-1);    
  
  // Buffer to write into
  esp += 4;
  uint8_t* buffer = *(uint8_t**)esp;
  VALIDATE_P (buffer);
  
  // Number of bytes to read
  esp += 4;
  uint32_t size = *((uint32_t*)esp);
  
  // Read from stdin?
  if (fd == 0)
    {
      uint32_t pos = 0;
      while (pos < size)
        buffer[pos++] = input_getc ();
      
      f->eax = pos;
      return;
    }  
  
  // Read from file
  struct file_elem* file_elem = get_file_elem (fd);
  if (file_elem == NULL)
    {
      f->eax = -1;
      return;
    }  
  
  lock_acquire (&file_lock);
  int read = file_read (file_elem->file, (void*)buffer, size);
  lock_release (&file_lock);
  
  f->eax = read;
}

void
syscall_wait (struct intr_frame *f)
{
  void* esp = f->esp + 4;
  VALIDATE_ESP1 (esp);
  
  int pid = *((int*)esp);
      
  // Look for child process with this PID
  struct thread* t = thread_current ();
  struct thread* child = NULL;
  struct list_elem* e = list_begin (&t->child_threads);
  while (e != list_end (&t->child_threads))
    {
      struct thread* temp = list_entry(e, struct thread, child_elem);
      ASSERT (thread_valid (temp));
      
      if (temp->tid == pid)
        {
          child = temp;
          break;
        }
    }
  
  if (child == NULL)
    {
      // Search for termination notice
      int status = thread_exit_status (pid);
      f->eax = status;      
      return;
    }
  
  // wait for process to exit
  sema_down (&child->exit_code_semaphore);
  sema_down (&child->exit_semaphore);
  
  int status = child->exit_code;
  
  sema_up (&child->exit_code_semaphore);
  f->eax = status;
}

bool
valid_pointer (const void* uaddr)
{
  struct thread* t = thread_current ();
  return pagedir_valid_uaddr (uaddr, t->pagedir);
}

void
syscall_halt (struct intr_frame *f UNUSED)
{
  shutdown ();
  thread_exit ();
}

static void
syscall_handler (struct intr_frame *f)
{
  void* esp = f->esp;
  VALIDATE_ESP1 (esp);

  int nr = *((int*) esp);

  switch (nr)
    {
    case SYS_HALT:
      syscall_halt (f);
      break;

    case SYS_EXIT:
      syscall_exit (f);
      break;

    case SYS_EXEC:
      syscall_exec (f);
      break;

    case SYS_OPEN:
      syscall_open (f);
      break;

    case SYS_CREATE:
      syscall_create (f);
      break;
      
    case SYS_REMOVE:
      syscall_remove (f);
      break;
      
    case SYS_READ:
      syscall_read (f);
      break;

    case SYS_WRITE:
      syscall_write (f);
      break;
      
    case SYS_FILESIZE:
      syscall_filesize (f);
      break;
      
    case SYS_CLOSE:
      syscall_close (f);
      break;
      
    case SYS_TELL:
      syscall_tell (f);
      break;
      
    case SYS_SEEK:
      syscall_seek (f);
      break;
      
    case SYS_WAIT:
      syscall_wait (f);
      break;

    default:
      printf ("Not Implemented: System Call %d\n", nr);
      thread_exit ();
    }
}

