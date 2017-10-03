#define GC_DISABLED
#include "gc.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/// Macro Definitions

//#define DEBUG 1  // uncomment to enable debug messages

#define HASHMAP_SIZE 1021  // prime sizes are best for hashmaps

#define ALLOCATE(PTR_NAME, ALLOCATOR_CALL)                                \
  alloc_invocations_since_last_collect++;                                 \
  if (alloc_invocations_since_last_collect > gc_alloc_collect_interval && \
      bytes_alloced_since_last_collect > gc_byte_collect_interval)        \
    gc_collect();                                                         \
  void* PTR_NAME = ALLOCATOR_CALL;                                        \
  /* if we could not allocate, try again after collecting garbage */      \
  if (!PTR_NAME) {                                                        \
    gc_collect();                                                         \
    PTR_NAME = ALLOCATOR_CALL;                                            \
  }

/// Structure and Type Definitions

typedef struct _Gc_alloc_chunk _Gc_alloc_chunk;
struct _Gc_alloc_chunk {
  void* ptr;
  size_t size;
  bool marked;
};

typedef struct _Gc_alloc_header _Gc_alloc_header;
struct _Gc_alloc_header {
  size_t size;
  size_t capacity;
  _Gc_alloc_chunk* chunks;
};

/// Global Variables

void* _gc_stack_base;  // highest stack address

static size_t bytes_alloced_since_last_collect = 0;
size_t gc_byte_collect_interval = 1 << 30;

static size_t alloc_invocations_since_last_collect = 0;
size_t gc_alloc_collect_interval = 1 << 10;

static _Gc_alloc_header allocs[HASHMAP_SIZE];

/// Local Function Declarations

static void add_entry(void* ptr, size_t size);
static _Gc_alloc_chunk* get_entry(void* ptr);
static size_t address_hash(void* ptr);
static void clear_marks();
static void mark(void** start, size_t len);
static void sweep();

/// Function Definitions

void _gc_init_chunkmap() {
  for (size_t i = 0; i < HASHMAP_SIZE; i++) {
    allocs[i].size = 0;
    allocs[i].capacity = 1;
    allocs[i].chunks = malloc(sizeof(_Gc_alloc_chunk) * allocs[i].capacity);
  }
}

void _gc_free_chunkmap() {
  for (size_t i = 0; i < HASHMAP_SIZE; i++) {
    free(allocs[i].chunks);
  }
}

void* gc_malloc(size_t size) {
  bytes_alloced_since_last_collect += size;
  ALLOCATE(ptr, malloc(size));

  if (ptr)
    add_entry(ptr, size);
  return ptr;
}

void add_entry(void* ptr, size_t size) {
  size_t i = address_hash(ptr);

  if (allocs[i].size == allocs[i].capacity) {
    _Gc_alloc_chunk* chunks = realloc(
        allocs[i].chunks, allocs[i].capacity * 2 * sizeof(_Gc_alloc_chunk));
    if (chunks) {
      allocs[i].chunks = chunks;
      allocs[i].capacity *= 2;
    } else {
      fprintf(stderr,
              "[gc] Could not allocate memory tracking entry."
              "Falling back to unmanaged allocation.\n");
      return;
    }
  }

  allocs[i].chunks[allocs[i].size].ptr = ptr;
  allocs[i].chunks[allocs[i].size].size = size;
  allocs[i].size++;
}

size_t address_hash(void* ptr) {
  return ((intptr_t)ptr >> 4) % HASHMAP_SIZE;
}

void* gc_calloc(size_t nmeb, size_t size) {
  bytes_alloced_since_last_collect += nmeb * size;
  ALLOCATE(ptr, calloc(nmeb, size));

  if (ptr)
    add_entry(ptr, nmeb * size);
  return ptr;
}

void* gc_realloc(void* ptr, size_t size) {
  // if ptr is NULL, realloc works the same way malloc does
  if (!ptr)
    return gc_malloc(size);

  bytes_alloced_since_last_collect += size;
  ALLOCATE(new_ptr, realloc(ptr, size));

  if (new_ptr) {
    // remove old management chunk
    size_t i = address_hash(ptr);
    for (size_t j = 0; j < allocs[i].size; j++) {
      if (allocs[i].chunks[j].ptr == ptr) {
        memmove(&allocs[i].chunks[j], &allocs[i].chunks[j + 1],
                sizeof(_Gc_alloc_chunk) * (allocs[i].size - j - 1));
        allocs[i].size--;
        break;
      }
    }

    add_entry(new_ptr, size);
  }

  return new_ptr;
}

void gc_free(void* ptr) {
  size_t i = address_hash(ptr);

  for (size_t j = 0; j < allocs[i].size; j++) {
    if (allocs[i].chunks[j].ptr == ptr) {
      free(ptr);
      memmove(&allocs[i].chunks[j], &allocs[i].chunks[j + 1],
              sizeof(_Gc_alloc_chunk) * (allocs[i].size - j - 1));
      allocs[i].size--;
      break;
    }
  }
}

extern char __data_start, _end;

void gc_collect() {
  void* top_of_stack;
  bytes_alloced_since_last_collect = 0;
#ifdef DEBUG
  size_t managed_objects = gc_count_managed_objects();
#endif

  clear_marks();

  // stack
  mark(&top_of_stack + 1, (intptr_t)_gc_stack_base - (intptr_t)&top_of_stack);

  // bss & data, but without our allocs table
  mark((void*)&__data_start, (intptr_t)&_end - (intptr_t)allocs);
  mark((void*)((intptr_t)allocs + sizeof(allocs)),
       (intptr_t)&_end - (intptr_t)allocs - sizeof(allocs));

  sweep();

#ifdef DEBUG
  fprintf(stderr, "[gc][DEBUG] freed %ld object(s).\n",
          managed_objects - gc_count_managed_objects());
#endif
}

void clear_marks() {
  for (size_t i = 0; i < HASHMAP_SIZE; i++) {
    for (size_t j = 0; j < allocs[i].size; j++) {
      allocs[i].chunks[j].marked = false;
    }
  }
}

void mark(void** start, size_t len) {
  void** curr = start;
  len /= sizeof(void*);

  for (; len > 0; len--, curr++) {
    _Gc_alloc_chunk* chunk = get_entry(*curr);
    if (chunk && !chunk->marked) {
      chunk->marked = true;
      mark(chunk->ptr, chunk->size);
    }
  }
}

_Gc_alloc_chunk* get_entry(void* ptr) {
  size_t i = address_hash(ptr) % HASHMAP_SIZE;

  for (size_t j = 0; j < allocs[i].size; j++) {
    if (allocs[i].chunks[j].ptr == ptr) {
      return &allocs[i].chunks[j];
    }
  }

  return NULL;
}

void sweep() {
  for (size_t i = 0; i < HASHMAP_SIZE; i++) {
    for (size_t j = 0; j < allocs[i].size; j++) {
      _Gc_alloc_chunk* chunk = &allocs[i].chunks[j];
      if (!chunk->marked) {
        free(chunk->ptr);
        memmove(chunk, chunk + 1,
                sizeof(_Gc_alloc_chunk) * (allocs[i].size - j - 1));
        allocs[i].size--;
      }
    }
  }
}

void* raw_malloc(size_t size) {
  return malloc(size);
}

void* raw_calloc(size_t nmeb, size_t size) {
  return calloc(nmeb, size);
}

void* raw_realloc(void* ptr, size_t size) {
  return realloc(ptr, size);
}

void raw_free(void* ptr) {
  free(ptr);
}

size_t gc_count_managed_objects() {
  size_t count = 0;
  for (size_t i = 0; i < HASHMAP_SIZE; i++)
    count += allocs[i].size;

  return count;
}

bool gc_is_managed(void* ptr) {
  return get_entry(ptr);
}
