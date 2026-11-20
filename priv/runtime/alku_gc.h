/*
 * The Alku garbage collector (internal to the runtime).
 *
 * A non-moving mark-and-sweep collector:
 * - Objects live in 64 KB pages; each page holds one kind of object
 *   (cons, tuple, fun, big integer or map node) in fixed-size slots. Big objects get their own pages.
 * - Roots are found by scanning the C stack (and registers, via setjmp)
 *   conservatively: any word that points into a live slot keeps it alive.
 *   This means runtime and generated code never have to register values.
 * - From the roots, objects are traced precisely, because each page knows
 *   what kind of object it holds.
 */
#ifndef ALKU_GC_H
#define ALKU_GC_H

#include "alku_runtime.h"

typedef enum {
    GC_CONS,
    GC_TUPLE,
    GC_FUN,
    GC_BIG,  /* big integer digits: nothing to trace */
    GC_MAP   /* map tree node */
} GcKind;

/* stack_bottom: an address in the outermost frame that runs Alku code. */
void gc_init(void *stack_bottom);

/* Memory outside the stack that may hold values (scanned conservatively). */
void gc_add_root(const void *start, size_t bytes);

/* Allocates an uninitialised object; may collect garbage first. */
void *gc_alloc(GcKind kind, size_t bytes);

#endif
