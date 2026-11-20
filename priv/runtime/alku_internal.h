/*
 * Shared between the runtime's C files; not used by generated code.
 *
 *   alku_runtime.c  values, printing, exceptions, lists, tuples, io, calls
 *   alku_number.c   integers (with bignums), floats, math
 *   alku_map.c      maps
 *   alku_gc.c       memory
 */
#ifndef ALKU_INTERNAL_H
#define ALKU_INTERNAL_H

#include "alku_runtime.h"

#include <stdio.h>

/* A growable text buffer: output is formatted here first. */
typedef struct {
    char *data;
    size_t length, capacity;
} Buffer;

void buf_putc(Buffer *b, char c);
void buf_puts(Buffer *b, const char *s);
void buf_int(Buffer *b, int64_t n);
void buf_flush(Buffer *b, FILE *out);

/* ~w (pretty = false) and ~p (pretty = true) */
void print_value(Buffer *b, Value v, bool pretty);

/* Erlang term order: -1, 0 or 1. With exact, 1 and 1.0 differ (=:=). */
int compare_terms(Value a, Value b, bool exact);

bool is_atom_named(Value v, const char *name);
Value pair(const char *tag, Value v);
Value array_to_list(size_t n, const Value *items);
Value utf8_to_list(const unsigned char *s, size_t length);

_Noreturn void out_of_memory(void);
_Noreturn void raise_error(Value reason);
_Noreturn void badarg(Value v);
_Noreturn void badarith(void);
_Noreturn void system_limit(void);

/* alku_number.c */
bool is_number(Value v);
bool is_integer_value(Value v);
int compare_numbers(Value a, Value b);
void print_number(Buffer *b, Value v);
void print_integer(Buffer *b, Value v, unsigned base, bool lowercase);
void print_float_fixed(Buffer *b, double d, int decimals);      /* ~f */
void print_float_scientific(Buffer *b, double d, int digits);   /* ~e */
Value parse_integer(const char *text, size_t length, unsigned base, bool *ok);

/* alku_map.c */
int compare_maps(Value a, Value b, bool exact);
void print_map(Buffer *b, Value map, bool pretty);

#endif
