#ifndef GC_H
#define GC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/// Macro Definitions

/* Wrapper for main function.
 *
 * This macro wraps the main function with the code necassary to set up garbage
 * collection. The main function has to have the signature `int main(int argc,
 * char* argv[])`.
 */
#define main(ARGC, ARGV)                                                       \
  /* Forward declaration of the wrapped main function */                       \
  _gc_main(ARGC, ARGV);                                                        \
  /* Pointer to the base of the stack. Defined in gc.c. */                     \
  extern void* _gc_stack_base;                                                 \
  extern void _gc_init_chunkmap();                                             \
  extern void _gc_free_chunkmap();                                             \
  int main(int argc, char* argv[]) {                                           \
    /* Determine the highest address of the stack. Since we call the actual    \
     * main function from here, all reachable, dynamically allocated variables \
     * which are on the stack have to be located between the stack base the    \
     * stack pointer. */                                                       \
    void* stack_base;                                                          \
    _gc_stack_base = &stack_base;                                              \
    _gc_init_chunkmap();                                                       \
    int res = _gc_main(argc, argv);                                            \
    /* Clean up. */                                                            \
    gc_collect();                                                              \
    _gc_free_chunkmap();                                                       \
    return res;                                                                \
  }                                                                            \
  /* First line of the actual (renamed) main function. */                      \
  int _gc_main(ARGC, ARGV)

// replace stdlib memory management functions with garbage collected ones
#ifndef GC_DISABLED
#define malloc gc_malloc
#define calloc gc_calloc
#define realloc gc_realloc
#define free gc_free
#endif  // GC_DISABLED

/// Global Variables
extern size_t gc_collect_interval;

/// Function Declarations

void* gc_malloc(size_t size);
void* gc_calloc(size_t nmeb, size_t size);
void* gc_realloc(void* ptr, size_t size);
void gc_free(void* ptr);
void gc_collect();

void* raw_malloc(size_t size);
void* raw_calloc(size_t nmeb, size_t size);
void* raw_realloc(void* ptr, size_t size);
void raw_free(void* ptr);

size_t gc_count_managed_objects();
bool gc_is_managed(void* ptr);

#endif  // GC_H
