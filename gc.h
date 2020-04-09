#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

volatile extern uintptr_t register_value(uintptr_t registerId);

typedef uintptr_t *GCP;

extern uintptr_t firstheappage;
extern uintptr_t lastheappage;
extern uintptr_t heappages;
extern uintptr_t freewords;
extern uintptr_t *freep;
extern uintptr_t allocatedpages;
extern uintptr_t freepage;
extern uintptr_t *space;
extern uintptr_t *link;
extern uintptr_t *type;
extern uintptr_t queue_head;
extern uintptr_t queue_tail;
extern uintptr_t current_space;
extern uintptr_t next_space;
extern uintptr_t globals;

extern uintptr_t *stackbase;

extern GCP *globalp;

#define OBJECT 0
#define CONTINUED 1

#define PAGEBYTES 512
#define PAGEWORDS (PAGEBYTES/sizeof(uintptr_t))
#define WORDBYTES (sizeof(uintptr_t))

#define PAGE_to_GCP(p) ((GCP)((p)*PAGEBYTES))
#define GCP_to_PAGE(p) (((uintptr_t)p)/PAGEBYTES)

#define MAKE_HEADER(words, ptrs) ((ptrs)<<17 | (words)<<1 | 1)
#define FORWARDED(header) (((header) & 1) == 0)
#define HEADER_PTRS(header) ((header)>>17 & 0x7FFF)
#define HEADER_WORDS(header) ((header)>>1 & 0xFFFF)
#define HEADER_BYTES(header) (((header)>>1 & 0xFFFF)*WORDBYTES)

#define STACKINC 8
#define FIRST_REGISTER 0
#define LAST_REGISTER 15


struct gc_state {
  char * heap;
  uintptr_t * space;
  uintptr_t * link;
  uintptr_t * type;
};

struct gc_state gcinit(uintptr_t heap_size, uintptr_t *stack_base, GCP global_ptr);
void gcfree(struct gc_state state);
GCP gcalloc(int bytes, int pointers);