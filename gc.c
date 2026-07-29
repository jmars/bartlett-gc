#define _GNU_SOURCE
#include "gc.h"
#include <sys/mman.h>

static uintptr_t firstheappage;
static uintptr_t lastheappage;
static uintptr_t heappages;
static uintptr_t freewords;
static uintptr_t *freep;
static uintptr_t allocatedpages;
static uintptr_t freepage;
static uintptr_t *space;
static uintptr_t *link;
static uintptr_t *type;
static uintptr_t queue_head;
static uintptr_t queue_tail;
static uintptr_t current_space;
static uintptr_t next_space;
static uintptr_t globals;

static uintptr_t *stackbase;

static GCP *globalp;

/* Raw allocation pointers — used for realloc/mremap during grow/shrink.
   The heap buffer is mmap'd so mremap can extend it in-place without
   moving the base (which would break the GCP_to_PAGE page-number scheme). */
static char *raw_heap_start;
static size_t heap_mmap_size;   /* current mmap size of the heap */
static uintptr_t *raw_space_ptr;
static uintptr_t *raw_link_ptr;
static uintptr_t *raw_type_ptr;

#define MAX_EXTRA_ROOTS 8
static struct { void *start; size_t size; } extra_roots[MAX_EXTRA_ROOTS];
static int n_extra_roots = 0;

/* Minimum heap size: 16MB (32768 pages). Never shrink below this. */
#define MIN_HEAP_PAGES 32768

/* ---------- dynamic heap growth / shrinkage ---------- */

static int grow_heap(uintptr_t pages_needed) {
    uintptr_t new_heappages = heappages * 2;
    size_t new_heap_size = new_heappages * PAGEBYTES;

    /* Ensure enough for what we actually need */
    uintptr_t min_needed = (allocatedpages + pages_needed + 512) * 2;
    if (new_heappages < min_needed) {
        new_heappages = min_needed;
        new_heap_size = new_heappages * PAGEBYTES;
    }

    /* If the new size fits within the existing mmap, just expand the
       logical page count — no mremap needed. */
    if (new_heap_size + PAGEBYTES - 1 <= heap_mmap_size) {
        /* Grow metadata arrays */
        uintptr_t *new_space = realloc(raw_space_ptr, new_heappages * sizeof(uintptr_t));
        uintptr_t *new_link  = realloc(raw_link_ptr,  new_heappages * sizeof(uintptr_t));
        uintptr_t *new_type  = realloc(raw_type_ptr,  new_heappages * sizeof(uintptr_t));
        if (!new_space || !new_link || !new_type) return -1;

        raw_space_ptr = new_space;
        raw_link_ptr  = new_link;
        raw_type_ptr  = new_type;
        space = new_space - firstheappage;
        link  = new_link  - firstheappage;
        type  = new_type  - firstheappage;

        uintptr_t old_last = lastheappage;
        lastheappage = firstheappage + new_heappages - 1;
        heappages = new_heappages;
        for (uintptr_t i = old_last + 1; i <= lastheappage; i++) {
            space[i] = 0; link[i] = 0; type[i] = 0;
        }

        fprintf(stderr, "[gc] heap grown to %zu MB (%lu pages, live=%lu) [logical]\n",
                new_heap_size / (1024 * 1024),
                (unsigned long)new_heappages,
                (unsigned long)allocatedpages);
        return 0;
    }

    /* Need to extend the mmap.  mremap without MREMAP_MAYMOVE keeps
       the base fixed. */
    void *new_map = mremap(raw_heap_start, heap_mmap_size,
                           new_heap_size + PAGEBYTES - 1, 0);
    if (new_map == MAP_FAILED) {
        fprintf(stderr, "[gc] grow: mremap failed for %zu MB\n",
                new_heap_size / (1024 * 1024));
        return -1;
    }
    raw_heap_start = new_map;
    heap_mmap_size = new_heap_size + PAGEBYTES - 1;

    /* Grow the auxiliary arrays */
    uintptr_t *new_space = realloc(raw_space_ptr, new_heappages * sizeof(uintptr_t));
    uintptr_t *new_link  = realloc(raw_link_ptr,  new_heappages * sizeof(uintptr_t));
    uintptr_t *new_type  = realloc(raw_type_ptr,  new_heappages * sizeof(uintptr_t));
    if (!new_space || !new_link || !new_type) return -1;

    raw_space_ptr = new_space;
    raw_link_ptr  = new_link;
    raw_type_ptr  = new_type;

    space = new_space - firstheappage;
    link  = new_link  - firstheappage;
    type  = new_type  - firstheappage;

    /* Zero out the newly added pages */
    uintptr_t old_last = lastheappage;
    lastheappage = firstheappage + new_heappages - 1;
    heappages = new_heappages;
    for (uintptr_t i = old_last + 1; i <= lastheappage; i++) {
        space[i] = 0; link[i] = 0; type[i] = 0;
    }

    fprintf(stderr, "[gc] heap grown to %zu MB (%lu pages, live=%lu)\n",
            new_heap_size / (1024 * 1024),
            (unsigned long)new_heappages,
            (unsigned long)allocatedpages);
    return 0;
}

static void shrink_heap(void) {
    if (heappages <= MIN_HEAP_PAGES) return;
    /* Only shrink if live data fits comfortably in half the current size */
    if (allocatedpages * 4 > heappages) return;

    uintptr_t new_heappages = heappages / 2;
    if (new_heappages < MIN_HEAP_PAGES) new_heappages = MIN_HEAP_PAGES;
    size_t new_heap_size = new_heappages * PAGEBYTES;

    /* Don't mremap — keep the mmap at its peak size so we can
       re-grow later without VAS conflicts.  Just shrink the logical
       page count and realloc the metadata arrays smaller. */
    uintptr_t *new_space = realloc(raw_space_ptr, new_heappages * sizeof(uintptr_t));
    uintptr_t *new_link  = realloc(raw_link_ptr,  new_heappages * sizeof(uintptr_t));
    uintptr_t *new_type  = realloc(raw_type_ptr,  new_heappages * sizeof(uintptr_t));

    if (!new_space) new_space = raw_space_ptr;
    if (!new_link)  new_link  = raw_link_ptr;
    if (!new_type)  new_type  = raw_type_ptr;

    raw_space_ptr = new_space;
    raw_link_ptr  = new_link;
    raw_type_ptr  = new_type;

    space = new_space - firstheappage;
    link  = new_link  - firstheappage;
    type  = new_type  - firstheappage;

    lastheappage = firstheappage + new_heappages - 1;
    heappages = new_heappages;

    /* Reset cursors that may point beyond the new heap boundary */
    if (freepage > lastheappage) freepage = firstheappage;
    freewords = 0;

    fprintf(stderr, "[gc] heap shrunk to %zu MB (%lu pages, live=%lu)\n",
            new_heap_size / (1024 * 1024),
            (unsigned long)new_heappages,
            (unsigned long)allocatedpages);
}

uintptr_t next_page(uintptr_t page)
{
  if (page == lastheappage)
    return firstheappage;
  return page + 1;
}

void queue(uintptr_t page)
{
  if (queue_head != 0)
  {
    link[queue_tail] = page;
    link[page] = 0;
    queue_tail = page;
  }
  else
  {
    queue_head = page;
    link[page] = 0;
    queue_tail = page;
  }
}

GCP move(GCP cp)
{
  uintptr_t cnt;
  uintptr_t header;
  GCP np;
  GCP from;
  GCP to;
  uintptr_t page;

  if (cp == NULL) return cp;
  page = GCP_to_PAGE(cp);
  if (page < firstheappage || page > lastheappage) return cp;
  if (space[page] == next_space) return cp;

  header = cp[-1];
  if (FORWARDED(header))
  {
    return (GCP)header;
  }

  np = gcalloc(HEADER_BYTES(header) - 4, 0);
  to = np - 1;
  from = cp - 1;
  cnt = HEADER_WORDS(header);

  while (cnt--)
  {
    *to++ = *from++;
  }

  cp[-1] = (uintptr_t)np;
  return np;
}

void promote_page(uintptr_t page)
{
  if (page >= firstheappage &&
      page <= lastheappage &&
      space[page] == current_space)
  {
    while (page > firstheappage && type[page] == CONTINUED)
    {
      allocatedpages = allocatedpages + 1;
      space[page] = next_space;
      page = page - 1;
    }
    space[page] = next_space;
    allocatedpages = allocatedpages + 1;
    queue(page);
  }
}

void collect() {
  uintptr_t *fp;
  uintptr_t reg;
  uintptr_t cnt;
  uintptr_t i;
  GCP cp;
  GCP pp;

  if (next_space != current_space) {
    fprintf(stderr, "gcalloc - Out of space during collect\n");
    exit(1);
  }

  if (freewords != 0) {
    *freep = MAKE_HEADER(freewords, 0);
    freewords = 0;
  }

  next_space = (current_space == 1) ? 2 : 1;
  allocatedpages = 0;
  queue_head = 0;

  for (fp = (uintptr_t *)(&fp);
       fp <= stackbase;
       fp = (uintptr_t *)(((char *)fp) + STACKINC))
  {
    promote_page(GCP_to_PAGE(*fp));
  }
  #ifdef FIRST_REGISTER
    for (reg = FIRST_REGISTER; reg <= LAST_REGISTER; reg++) {
      promote_page(GCP_to_PAGE(register_value(reg)));
    }
  #endif

  for (i = 0; i < n_extra_roots; i++) {
    uintptr_t *p = (uintptr_t *)extra_roots[i].start;
    uintptr_t *end = (uintptr_t *)((char *)extra_roots[i].start + extra_roots[i].size);
    for (; p < end; p++) {
      promote_page(GCP_to_PAGE(*p));
    }
  }

  cnt = globals;

  while (cnt--) {
    *globalp[cnt] = (uintptr_t)move((GCP)*globalp[cnt]);
  }

  while (queue_head != 0) {
    cp = PAGE_to_GCP(queue_head);
    while (GCP_to_PAGE(cp) == queue_head && cp != freep) {
      uintptr_t hw = HEADER_WORDS(*cp);
      if (hw == 0 || hw > PAGEWORDS * 2) break;  /* false positive from stack */
      cnt = HEADER_PTRS(*cp);
      pp = cp + 1;
      while (cnt--) {
        *pp = (uintptr_t)move((GCP)*pp);
        pp = pp + 1;
      }
      cp = cp + hw;
    }
    queue_head = link[queue_head];
  }

  current_space = next_space;

  /* Shrink heap when live set is comfortably small (< 25%).
     This is critical: before shen.initialise returns, the C stack
     contains many false-positive heap pointers that inflate the live
     set; once those stack frames unwind, the real live set is tiny
     (< 50 pages).  Without shrink, the heap stays bloated and
     subsequent allocations push it past the mmap reservation. */
  shrink_heap();
}

void allocatepage(uintptr_t pages) {
  uintptr_t free;
  uintptr_t firstpage;
  uintptr_t allpages;
  int retried = 0;

retry:
  if (allocatedpages + pages >= heappages / 2) {
    collect();
    /* After collection, check again.  If still too full, try to grow. */
    if (allocatedpages + pages >= heappages / 2) {
      if (!retried && grow_heap(pages) == 0) {
        retried = 1;
        goto retry;
      }
      fprintf(stderr,
        "gcalloc - Out of memory: need %lu pages, live set is %lu pages "
        "(semi-space capacity %lu pages)\n",
        (unsigned long)pages, (unsigned long)allocatedpages,
        (unsigned long)(heappages / 2));
      exit(1);
    }
  }

  free = 0;
  allpages = heappages;

  while (allpages--) {
    if (space[freepage] != current_space &&
        space[freepage] != next_space)
    {
      if (free++ == 0) {
        firstpage = freepage;
      }

      if (free == pages) {
        freep = PAGE_to_GCP(firstpage);

        if (current_space != next_space) {
          queue(firstpage);
        }

        freewords = pages * PAGEWORDS;
        allocatedpages = allocatedpages + pages;
        freepage = next_page(freepage);
        space[firstpage] = next_space;
        type[firstpage] = OBJECT;
        
        while (--pages) {
          space[++firstpage] = next_space;
          type[firstpage] = CONTINUED;
        }

        return;
      }
    } else {
      free = 0;
    }
    freepage = next_page(freepage);
    if (freepage == firstheappage) {
      free = 0;
    }
  }

  /* Scan exhausted — try growing (once) then retry */
  if (!retried && grow_heap(pages) == 0) {
    retried = 1;
    goto retry;
  }

  fprintf(stderr,
          "gcalloc - Unable to allocate %lu pages in a %lu page heap\n",
          (unsigned long)pages, (unsigned long)heappages);

  exit(1);
}

struct gc_state gcinit(uintptr_t heap_size, uintptr_t *stack_base, GCP global_ptr) 
{
  char *heap;
  uintptr_t i;
  GCP *gp;

  heappages = heap_size / PAGEBYTES;
  n_extra_roots = 0;
  /* Reserve a larger mmap than the initial heap so we can grow logically
     without mremap.  The extra VAS costs nothing on Linux (lazy commit).
     Reserve 4 GB to give the heap room to grow through several doublings
     (256MB → 512MB → 1GB → 2GB → 4GB) without needing mremap at all.
     Even with conservative stack scan false positives inflating the live
     set during deep call chains, 4GB is plenty of headroom. */
  heap_mmap_size = (heap_size * 16 > (4096ULL * 1024 * 1024))
                     ? heap_size * 16 + PAGEBYTES - 1
                     : 4096ULL * 1024 * 1024 + PAGEBYTES - 1;
  raw_heap_start = mmap(NULL, heap_mmap_size, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (raw_heap_start == MAP_FAILED) {
    fprintf(stderr, "gcinit: mmap failed for %zu bytes\n", heap_mmap_size);
    exit(1);
  }
  heap = raw_heap_start;

  if ((uintptr_t)heap & (PAGEBYTES - 1)) {
    heap = heap + (PAGEBYTES - ((uintptr_t)heap & (PAGEBYTES - 1)));
  }

  firstheappage = GCP_to_PAGE(heap);
  lastheappage = firstheappage + heappages - 1;
  uintptr_t * space_ptr = (uintptr_t *)malloc(heappages * sizeof(uintptr_t));
  space = (space_ptr) - firstheappage;

  for (i = firstheappage; i <= lastheappage; i++) {
    space[i] = 0;
  }

  uintptr_t * link_ptr = (uintptr_t *)malloc(heappages * sizeof(uintptr_t));
  link = (link_ptr) - firstheappage;
  uintptr_t * type_ptr = (uintptr_t *)malloc(heappages * sizeof(uintptr_t));
  type = (type_ptr) - firstheappage;
  /* Zero link[] to avoid stale queue entries causing cycles */
  memset(link_ptr, 0, heappages * sizeof(uintptr_t));
  globals = 0;
  gp = &global_ptr;

  while (*gp++ != NULL) {
    globals = globals + 1;
  }

  if (globals) {
    globalp = (GCP *)malloc(globals * sizeof(GCP));
    i = globals;
    gp = &global_ptr;

    while (i--) {
      globalp[i] = *gp;
      **gp = 0;
      gp = gp + 1;
    }
  }

  stackbase = stack_base;
  current_space = 1;
  next_space = 1;
  freepage = firstheappage;
  allocatedpages = 0;
  queue_head = 0;

  /* Save raw pointers for grow/shrink */
  raw_space_ptr  = space_ptr;
  raw_link_ptr   = link_ptr;
  raw_type_ptr   = type_ptr;

  struct gc_state state = {
    .heap = raw_heap_start,
    .space = space_ptr,
    .link = link_ptr,
    .type = type_ptr
  };

  return state;
}

// make valgrind happy
void gcfree(struct gc_state state) {
  (void)state;
  munmap(raw_heap_start, heap_mmap_size);
  free(raw_space_ptr);
  free(raw_link_ptr);
  free(raw_type_ptr);
}

void gc_set_extra_roots(void *start, size_t size) {
  if (n_extra_roots >= MAX_EXTRA_ROOTS) {
    fprintf(stderr, "gc_set_extra_roots: too many ranges (max %d)\n", MAX_EXTRA_ROOTS);
    exit(1);
  }
  extra_roots[n_extra_roots].start = start;
  extra_roots[n_extra_roots].size = size;
  n_extra_roots++;
}

GCP gcalloc(int bytes, int pointers) {
  int words;
  int i;
  GCP object;

  words = (bytes + WORDBYTES - 1) / WORDBYTES + 1;

  if (words > 0xFFFFFF) {
    fprintf(stderr,
      "gcalloc: object too large for header (%d words, max %d)\n", words, 0xFFFFFF);
    exit(1);
  }
  if (pointers > 0xFFFFF) {
    fprintf(stderr,
      "gcalloc: too many pointers for header (%d, max %d)\n", pointers, 0xFFFFF);
    exit(1);
  }

  while (words > freewords) {
    if (freewords != 0) {
      *freep = MAKE_HEADER(freewords, 0);
    }
    freewords = 0;
    allocatepage((words + PAGEWORDS - 1) / PAGEWORDS);
  }

  *freep = MAKE_HEADER(words, pointers);

  for (i = 1; i <= pointers; i++) {
    freep[i] = 0;
  }

  object = freep + 1;

  if (words < PAGEWORDS) {
    freewords = freewords - words;
    freep = freep + words;
  } else {
    freewords = 0;
  }

  return object;
}