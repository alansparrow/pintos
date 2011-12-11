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

struct file_elem 
{
  struct list_elem* elem;
  struct file* file;
  int fd;
};

static struct lock fd_lock;
static struct lock file_lock;
static struct list opened_files;

static void syscall_handler (struct intr_frame *);
bool valid_pointer (const void* uaddr);

int allocate_fd ()
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
  lock_init (&fd_lock);
}

void
syscall_exit (struct intr_frame *f)
{
  void* esp = f->esp + 4;
  int status = *((int*) esp);
  exit (status);
}

void exit (int status)
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
  // TODO: Validate pointer
  void* esp = f->esp + 4;
  char* file_name = (char*) (*(int*) esp);

  // Starts a new process and waits until it has successfully loaded or failed
  tid_t tid = process_execute (file_name, true);

  // PID == TID as return value
  f->eax = tid;
}

void
syscall_create (struct intr_frame *f)
{
  void* esp = f->esp + 4;

  // TODO: Validate pointer
  char* filename = *((char**) esp);
  int initial_size = *(int*) esp;

  if (!valid_pointer (filename) || initial_size < 0)
    exit (-1);
  
  f->eax = filesys_create (filename, initial_size);
}

void
syscall_open (struct intr_frame *f)
{
  void* esp = f->esp + 4;

  // TODO: Validate pointer
  char* filename = *((char**) esp);  
  
  struct file* file = filesys_open (filename);
  if (file == NULL)
    {
      f->eax = -1;
      return;
    }
  
  // Add entry to list of opened files
  struct file_elem* file_entry = malloc(sizeof(struct file_elem));
  file_entry->fd = allocate_fd ();
  file_entry->file = file;
  
  list_push_back (&opened_files, file_entry->elem);    
  
  f->eax = file_entry->fd;
}

void
syscall_write (struct intr_frame *f)
{
  void* esp = f->esp + 4;

  int size = -1;
  int dest_addr = -1;
  void* buffer;
  int fd = -1;

  fd = *((int*) esp);
  esp += 4;

  dest_addr = *((int*) esp);
  esp += 4;

  buffer = dest_addr;
  size = *((int*) esp);
  esp += 4;

  if (fd == 1)
    {
      putbuf (buffer, size);
    }
  else
    ASSERT ("NOT YET IMPLEMENTED");
}

bool
valid_pointer (const void* uaddr)
{
  struct thread* t = thread_current ();
  return pagedir_valid_uaddr (uaddr, t->pagedir);
}

void syscall_halt ()
{
  shutdown ();
  thread_exit ();
}

static void
syscall_handler (struct intr_frame *f UNUSED)
{
  void* esp = f->esp;

  int nr = *((int*) esp);
  esp += 4;

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

    case SYS_WRITE:
      syscall_write (f);
      break;

    default:
      printf ("Not Implemented: System Call %d\n", nr);
      thread_exit ();
    }
}

