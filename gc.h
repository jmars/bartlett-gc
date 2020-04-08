#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

extern int zzReadRegister(int registerId);

typedef uintptr_t *GCP;

uintptr_t firstheappage;
uintptr_t lastheappage;
uintptr_t heappages;
uintptr_t freewords;
uintptr_t *freep;
uintptr_t allocatedpages;
uintptr_t freepage;
uintptr_t *space;
uintptr_t *link;
uintptr_t *type;
uintptr_t queue_head;
uintptr_t queue_tail;
uintptr_t current_space;
uintptr_t next_space;
uintptr_t globals;

uintptr_t *stackbase;

GCP *globalp;

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

GCP gcalloc(uintptr_t bytes, uintptr_t pointers);