#ifndef FRAMETABLE_H
#define	FRAMETABLE_H

#include "debug.h"
#include "../filesys/off_t.h"
#include "lib/kernel/hash.h"

struct frame
{
    struct frame* next;
    int referenced;
    void* page_vaddr;
    void* frame_paddr;
    struct thread* owner;
};

struct frame_mapping
{
    struct hash_elem elem;
    void* kpage;
    void* upage;
    struct thread* owner;
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

