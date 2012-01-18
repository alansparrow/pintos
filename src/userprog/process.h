#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"
#include "../lib/kernel/hash.h"
#include "debug.h"
#include "../filesys/off_t.h"
#include "vm/suppl_page_table.h"

tid_t process_execute (const char *);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);
bool process_load_segment (struct page_suppl* spte);

#endif /* userprog/process.h */
