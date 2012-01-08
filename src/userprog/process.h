#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"
#include "../lib/kernel/hash.h"
#include "debug.h"

tid_t process_execute (const char *);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);

struct frame
{
    struct hash_elem elem;
    int* page_vaddr;
    int* frame_paddr;
};

unsigned frame_hash (const struct hash_elem* p_, void* aux UNUSED);
void frametable_init (void);

void* frametable_get_page (void);
void frametable_free_page (void* page_vaddr);
    
bool frame_equals (const struct hash_elem* a_, const struct hash_elem* b_, 
                 void* aux UNUSED);

#endif /* userprog/process.h */
