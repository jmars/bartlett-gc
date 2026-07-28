#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* register_value() and x64_reg.asm are retained for easy re-enablement of the
   x64 register scan.  To re-enable, define FIRST_REGISTER and LAST_REGISTER
   (e.g., 0 and 15) before including this header.  The scan is disabled by
   default because the C stack conservative scan is sufficient when:
   - Frame pointers are used or the compiler spills live GC pointers to stack
   - No LTO/PGO across GC-triggering call boundaries
   Tested safe at -O2 and -O3 (gcc).  If you hit mysterious use-after-free,
   re-enable the register scan as a first debugging step. */
volatile extern uintptr_t register_value(uintptr_t registerId);

typedef uintptr_t *GCP;

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

struct gc_state {
  char * heap;
  uintptr_t * space;
  uintptr_t * link;
  uintptr_t * type;
};

struct gc_state gcinit(uintptr_t heap_size, uintptr_t *stack_base, GCP global_ptr);
void gcfree(struct gc_state state);
GCP gcalloc(int bytes, int pointers);

/* Register a memory range as GC roots.  The range is conservatively scanned
   (every sizeof(uintptr_t) word is checked as a potential heap pointer) in
   the same way the C stack is scanned.  Requirements:
   - start must be uintptr_t-aligned
   - size should be a multiple of sizeof(uintptr_t) (trailing bytes ignored)
   - The range must not overlap the GC heap
   - Call once after gcinit(); state persists across collections until the
     next gcinit() call, which resets it. */
void gc_set_extra_roots(void *start, size_t size);

#define GCALLOC(type, ptrs) ((struct type *)gcalloc(sizeof(struct type), ptrs))