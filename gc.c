#include "gc.h"

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

  if (cp == NULL || space[GCP_to_PAGE(cp)] == next_space)
  {
    return cp;
  }

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
    while (type[page] == CONTINUED)
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

  next_space = (current_space + 1) & 077777;
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

  cnt = globals;

  while (cnt--) {
    *globalp[cnt] = (uintptr_t)move(*globalp[cnt]);
  }

  while (queue_head != 0) {
    cp = PAGE_to_GCP(queue_head);
    while (GCP_to_PAGE(cp) == queue_head && cp != freep) {
      cnt = HEADER_PTRS(*cp);
      pp = cp + 1;
      while (cnt--) {
        *pp = (uintptr_t)move(*pp);
        pp = pp + 1;
      }
      cp = cp + HEADER_WORDS(*cp);
    }
    queue_head = link[queue_head];
  }

  current_space = next_space;
}

void allocatepage(uintptr_t pages) {
  uintptr_t free;
  uintptr_t firstpage;
  uintptr_t allpages;

  if (allocatedpages + pages >= heappages / 2) {
    collect();
    return;
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

  fprintf(stderr,
          "gcalloc - Unable to allocate %d pages in a %d page heap\n",
          pages, heappages);

  exit(1);
}

struct gc_state gcinit(uintptr_t heap_size, uintptr_t *stack_base, GCP global_ptr) 
{
  char *heap_start;
  char *heap;
  uintptr_t i;
  GCP *gp;

  heappages = heap_size / PAGEBYTES;
  heap_start = malloc(heap_size + PAGEBYTES - 1);
  heap = heap_start;

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
      **gp = NULL;
      gp = gp + 1;
    }
  }

  stackbase = stack_base;
  current_space = 1;
  next_space = 1;
  freepage = firstheappage;
  allocatedpages = 0;
  queue_head = 0;

  struct gc_state state = {
    .heap = heap_start,
    .space = space_ptr,
    .link = link_ptr,
    .type = type_ptr
  };

  return state;
}

// make valgrind happy
void gcfree(struct gc_state state) {
  free(state.heap);
  free(state.space);
  free(state.link);
  free(state.type);
}

GCP gcalloc(int bytes, int pointers) {
  int words;
  int i;
  GCP object;

  words = (bytes + WORDBYTES - 1) / WORDBYTES + 1;

  while (words > freewords) {
    if (freewords != 0) {
      *freep = MAKE_HEADER(freewords, 0);
    }
    freewords = 0;
    allocatepage((words + PAGEWORDS - 1) / PAGEWORDS);
  }

  *freep = MAKE_HEADER(words, pointers);

  for (i = 1; i <= pointers; i++) {
    freep[i] = NULL;
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