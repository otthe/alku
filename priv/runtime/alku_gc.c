#include "alku_gc.h"

#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PAGE_SIZE ((size_t)64 * 1024)
#define LARGE_OBJECT ((size_t)16 * 1024) /* bigger objects get their own pages */
#define MIN_THRESHOLD ((size_t)8 * 1024 * 1024)
#define STRESS_THRESHOLD ((size_t)512)
#define NUM_KINDS 6

static const size_t size_classes[] = {
    16, 32, 48, 64, 96, 128, 192, 256, 384, 512, 768, 1024,
    1536, 2048, 3072, 4096, 6144, 8192, 12288, 16384};
#define NUM_CLASSES (sizeof size_classes / sizeof size_classes[0])

enum { SLOT_USED = 1, SLOT_MARKED = 2 };

typedef struct {
    GcKind kind;
    size_t size_class; /* index into size_classes; NUM_CLASSES for large pages */
    size_t slot_size;
    size_t slot_count;
    size_t bytes;          /* size of the whole page allocation */
    unsigned char *slots;  /* first slot */
    unsigned char flags[]; /* SLOT_USED / SLOT_MARKED, one byte per slot */
} Page;

typedef struct FreeSlot {
    struct FreeSlot *next;
} FreeSlot;

typedef struct {
    const void *start;
    size_t bytes;
} Root;

static FreeSlot *free_lists[NUM_KINDS][NUM_CLASSES];

static Page **pages; /* sorted by address, for looking up stack words */
static size_t page_count, page_capacity;
static uintptr_t heap_low = UINTPTR_MAX, heap_high;

static void **mark_stack;
static size_t mark_count, mark_capacity;

static Root roots[8];
static size_t root_count;

static void *stack_bottom;
static size_t allocated, threshold = MIN_THRESHOLD;
static bool stress;

static _Noreturn void out_of_memory(void) {
    fputs("** out of memory\n", stderr);
    exit(1);
}

static void *checked_realloc(void *p, size_t bytes) {
    p = realloc(p, bytes);
    if (p == NULL) out_of_memory();
    return p;
}

void gc_init(void *bottom) {
    stack_bottom = bottom;
    const char *s = getenv("ALKU_GC_STRESS");
    stress = s != NULL && s[0] != '\0' && s[0] != '0';
    if (stress) threshold = STRESS_THRESHOLD;
}

void gc_add_root(const void *start, size_t bytes) {
    if (root_count == sizeof roots / sizeof roots[0]) {
        fputs("** too many GC roots\n", stderr);
        exit(1);
    }
    roots[root_count].start = start;
    roots[root_count].bytes = bytes;
    root_count++;
}

/* ---------- Pages ---------- */

static size_t align16(size_t n) {
    return (n + 15) & ~(size_t)15;
}

static void register_page(Page *page) {
    if (page_count == page_capacity) {
        page_capacity = page_capacity ? 2 * page_capacity : 64;
        pages = checked_realloc(pages, page_capacity * sizeof *pages);
    }
    size_t i = page_count;
    while (i > 0 && pages[i - 1] > page) {
        pages[i] = pages[i - 1];
        i--;
    }
    pages[i] = page;
    page_count++;
    uintptr_t start = (uintptr_t)page;
    if (start < heap_low) heap_low = start;
    if (start + page->bytes > heap_high) heap_high = start + page->bytes;
}

static Page *new_page(GcKind kind, size_t size_class, size_t slot_size,
                      size_t slot_count, size_t bytes) {
    Page *page = aligned_alloc(PAGE_SIZE, bytes);
    if (page == NULL) out_of_memory();
    page->kind = kind;
    page->size_class = size_class;
    page->slot_size = slot_size;
    page->slot_count = slot_count;
    page->bytes = bytes;
    page->slots = (unsigned char *)page + align16(sizeof(Page) + slot_count);
    memset(page->flags, 0, slot_count);
    register_page(page);
    return page;
}

/* A fresh page for small objects; its slots go onto the free list. */
static void add_small_page(GcKind kind, size_t size_class) {
    size_t slot_size = size_classes[size_class];
    size_t count = (PAGE_SIZE - sizeof(Page) - 15) / (slot_size + 1);
    Page *page = new_page(kind, size_class, slot_size, count, PAGE_SIZE);
    for (size_t i = count; i > 0; i--) {
        FreeSlot *slot = (FreeSlot *)(page->slots + (i - 1) * slot_size);
        slot->next = free_lists[kind][size_class];
        free_lists[kind][size_class] = slot;
    }
}

/* Finds the page containing address p, if it is in the heap. */
static Page *find_page(uintptr_t p) {
    if (p < heap_low || p >= heap_high) return NULL;
    size_t lo = 0, hi = page_count;
    while (lo < hi) { /* first page that starts after p */
        size_t mid = (lo + hi) / 2;
        if ((uintptr_t)pages[mid] <= p) lo = mid + 1;
        else hi = mid;
    }
    if (lo == 0) return NULL;
    Page *page = pages[lo - 1];
    return p < (uintptr_t)page + page->bytes ? page : NULL;
}

/* ---------- Marking ---------- */

static void push_mark(void *object) {
    if (mark_count == mark_capacity) {
        mark_capacity = mark_capacity ? 2 * mark_capacity : 1024;
        mark_stack = checked_realloc(mark_stack, mark_capacity * sizeof *mark_stack);
    }
    mark_stack[mark_count++] = object;
}

/* Marks the object that p points into (start or interior), if any. */
static void mark_address(uintptr_t p) {
    Page *page = find_page(p);
    if (page == NULL) return;
    uintptr_t first = (uintptr_t)page->slots;
    if (p < first) return;
    size_t index = (p - first) / page->slot_size;
    if (index >= page->slot_count) return;
    unsigned char *flags = &page->flags[index];
    if (*flags != SLOT_USED) return; /* free, or already marked */
    *flags |= SLOT_MARKED;
    push_mark(page->slots + index * page->slot_size);
}

static void mark_value(Value v) {
    switch (v.tag) {
    case ALKU_TUPLE: mark_address((uintptr_t)v.as.tuple); break;
    case ALKU_CONS: mark_address((uintptr_t)v.as.cons); break;
    case ALKU_FUN: mark_address((uintptr_t)v.as.fun); break;
    case ALKU_BIG: mark_address((uintptr_t)v.as.big); break;
    case ALKU_MAP: mark_address((uintptr_t)v.as.map); break;
    case ALKU_BINARY: mark_address((uintptr_t)v.as.binary); break;
    default: break;
    }
}

static void mark_values(const Value *values, size_t count) {
    for (size_t i = 0; i < count; i++) mark_value(values[i]);
}

static void trace(void *object) {
    Page *page = find_page((uintptr_t)object);
    switch (page->kind) {
    case GC_CONS: {
        AlkuCons *cell = object;
        mark_value(cell->head);
        mark_value(cell->tail);
        break;
    }
    case GC_TUPLE: {
        AlkuTuple *tuple = object;
        mark_values(tuple->elements, tuple->arity);
        break;
    }
    case GC_FUN: {
        AlkuFun *fun = object;
        mark_values(fun->env, fun->free_count);
        break;
    }
    case GC_BIG:
        break;
    case GC_BINARY: /* a sub-binary keeps its parent's bytes alive */
        mark_address((uintptr_t)((AlkuBinary *)object)->parent);
        break;
    case GC_MAP: {
        AlkuMapNode *node = object;
        mark_value(node->key);
        mark_value(node->value);
        mark_address((uintptr_t)node->left);
        mark_address((uintptr_t)node->right);
        break;
    }
    }
}

/* Reading every word of the stack is intended here, so AddressSanitizer
 * (when testing the runtime with it) is told not to check these reads. */
__attribute__((no_sanitize_address))
static void scan_range(const void *start, const void *end) {
    uintptr_t p = ((uintptr_t)start + sizeof(uintptr_t) - 1) & ~(sizeof(uintptr_t) - 1);
    for (; p + sizeof(uintptr_t) <= (uintptr_t)end; p += sizeof(uintptr_t)) {
        mark_address(*(const volatile uintptr_t *)p);
    }
}

/* Scans from this function's frame up to the bottom of the stack, which
 * covers every caller, including collect's saved registers. */
static __attribute__((noinline)) void scan_stack(void) {
    volatile uintptr_t top = 0;
    scan_range((const void *)&top, stack_bottom);
}

/* ---------- Sweeping ---------- */

static void free_page_at(size_t i) {
    free(pages[i]);
    memmove(&pages[i], &pages[i + 1], (page_count - i - 1) * sizeof *pages);
    page_count--;
}

/* Frees unmarked objects, rebuilds the free lists and returns live bytes. */
static size_t sweep(void) {
    memset(free_lists, 0, sizeof free_lists);
    size_t live = 0;
    size_t i = page_count;
    while (i > 0) {
        i--;
        Page *page = pages[i];
        if (page->size_class == NUM_CLASSES) { /* large object page */
            if (page->flags[0] & SLOT_MARKED) {
                page->flags[0] = SLOT_USED;
                live += page->bytes;
            } else {
                free_page_at(i);
            }
            continue;
        }
        FreeSlot **list = &free_lists[page->kind][page->size_class];
        for (size_t j = page->slot_count; j > 0; j--) {
            unsigned char *flags = &page->flags[j - 1];
            if (*flags & SLOT_MARKED) {
                *flags = SLOT_USED;
                live += page->slot_size;
            } else {
                *flags = 0;
                FreeSlot *slot = (FreeSlot *)(page->slots + (j - 1) * page->slot_size);
                slot->next = *list;
                *list = slot;
            }
        }
    }
    return live;
}

static void collect(void) {
    jmp_buf registers;
    setjmp(registers); /* spills callee-saved registers onto the stack */
    scan_stack();
    for (size_t i = 0; i < root_count; i++) {
        scan_range(roots[i].start, (const char *)roots[i].start + roots[i].bytes);
    }
    while (mark_count > 0) trace(mark_stack[--mark_count]);
    size_t live = sweep();
    if (stress) {
        threshold = STRESS_THRESHOLD + live / 8;
    } else {
        threshold = 2 * live > MIN_THRESHOLD ? 2 * live : MIN_THRESHOLD;
    }
    allocated = 0;
}

/* ---------- Allocation ---------- */

void *gc_alloc(GcKind kind, size_t bytes) {
    if (allocated >= threshold) collect();
    if (bytes > LARGE_OBJECT) {
        size_t total = align16(sizeof(Page) + 1) + bytes;
        total = (total + PAGE_SIZE - 1) / PAGE_SIZE * PAGE_SIZE;
        Page *page = new_page(kind, NUM_CLASSES, bytes, 1, total);
        page->flags[0] = SLOT_USED;
        allocated += total;
        return page->slots;
    }
    size_t c = 0;
    while (size_classes[c] < bytes) c++;
    if (free_lists[kind][c] == NULL) add_small_page(kind, c);
    FreeSlot *slot = free_lists[kind][c];
    free_lists[kind][c] = slot->next;
    /* small pages are exactly one aligned PAGE_SIZE block */
    Page *page = (Page *)((uintptr_t)slot & ~(uintptr_t)(PAGE_SIZE - 1));
    page->flags[((unsigned char *)slot - page->slots) / page->slot_size] = SLOT_USED;
    allocated += page->slot_size;
    return slot;
}
