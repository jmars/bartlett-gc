#include "gc.h"

struct symbol {
  struct symbol * next;
  char name[10];
};

int main() {
  uintptr_t stack_root;
  // use a small heap size to force many collections
  struct gc_state gcs = gcinit(4096, &stack_root, NULL);

  struct symbol * sp = (struct symbol*)gcalloc(sizeof(struct symbol), 1);
  strncpy(sp->name, "test\0", 5);
  char test[5];
  strncpy(test, sp->name, 5);
  printf(test);

  // run 10 million allocations to stress the garbage collector
  for (size_t i = 0; i < 10000000; i++) {
    struct symbol * sp2 = (struct symbol*)gcalloc(sizeof(struct symbol), 1);
    sp->next = sp2;
    sp = sp2;
  }

  gcfree(gcs);
  return 0;
}