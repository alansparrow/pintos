#ifndef FRAMETABLE_H
#define	FRAMETABLE_H

#include "debug.h"
#include "../filesys/off_t.h"
#include "lib/kernel/hash.h"

/* Stores frames that have pages loaded and is used for eviction. */
struct frame
{
    struct frame* next;         /* Next list element in ring buffer */
    int referenced;             /* Referenced Bit for Clock algorithm */
    void* page_vaddr;           /* Kernel virtual address */    
    struct thread* owner;       /* Owning thread */
};

/* Mapping of a kernel virtual frame to a user page */
struct frame_mapping
{
    struct hash_elem elem;      /* Hash element */
    void* kpage;                /* kernel virtual address of the frame */
    void* upage;                /* user virtual address of the page */
    struct thread* owner;       /* Owning thread */
};

void frametable_init (void);
void* frametable_get_page (void);
void* frametable_get_pages (size_t pg_count);

void frametable_free_page (void* page_vaddr, bool evict);
void frametable_free_pages (void* page_vaddr, size_t count, bool evict);

bool frame_map (void* upage, void* kpage, struct thread* owner, bool writable);
bool frame_unmap (void* kpage);

unsigned frame_hash (const struct hash_elem* p_, void* aux UNUSED);
bool frame_equals (const struct hash_elem* a_, const struct hash_elem* b_,
            void* aux UNUSED);

#endif	/* FRAMETABLE_H */

