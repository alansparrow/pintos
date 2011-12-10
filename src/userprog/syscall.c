#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"

static void syscall_handler (struct intr_frame *);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

void syscall_exit (struct intr_frame *f)
{
  void* esp = f->esp + 4;        
  int status = *((int*)esp);
      
  struct thread* t = thread_current ();
  
  int i = 0;
  while (t->name[i] != ' ' && t->name[i] != '\0') i++;
  char temp = t->name[i];
  t->name[i] = '\0';
  
  printf("%s: exit(%d)\n", t->name, status);
  
  t->name[i] = temp;
    
  thread_exit ();
}

void syscall_write (struct intr_frame *f)
{
  void* esp = f->esp + 4;
  
  int size = -1;
  int dest_addr = -1;
  void* buffer;
  int fd = -1;  
  
  fd = *((int*)esp);      
  esp += 4;

  dest_addr = *((int*)esp);
  esp += 4;

  buffer = dest_addr;    
  size = *((int*)esp);
  esp += 4;

  if (fd == 1)
    {
      putbuf(buffer, size);
    }    
  else
    ASSERT("NOT YET IMPLEMENTED");
}

bool valid_pointer (const void* uaddr)
{
  struct thread* t = thread_current ();
  return pagedir_valid_uaddr (uaddr, t->pagedir);
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{  
  void* esp = f->esp;         
  
  int nr = *((int*)esp);
  esp += 4;
  
  switch (nr)
    {
    case SYS_EXIT:
      syscall_exit (f);
      break;
      
    case SYS_WRITE:
      syscall_write (f);
      break;
      
    default:
      printf("Not Implemented: System Call %d\n", nr); 
      thread_exit ();
    }     
}

