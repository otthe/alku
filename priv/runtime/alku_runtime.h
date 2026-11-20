/*
 * The Alku runtime: how Alku values are represented in C, and the
 * operations that generated code calls.
 *
 * Every Alku value is a small struct passed by value. Phase 1 has only
 * integers and atoms, so nothing lives on the heap yet.
 */
#ifndef ALKU_RUNTIME_H
#define ALKU_RUNTIME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Tags are declared in Erlang term order: numbers < atoms. */
typedef enum {
    ALKU_INT,
    ALKU_ATOM
} AlkuTag;

typedef struct {
    AlkuTag tag;
    union {
        int64_t integer;
        const char *atom; /* UTF-8 name; atoms are compared by name */
    } as;
} Value;

/* Constructors */
Value alku_int(int64_t n);
Value alku_atom(const char *name);

/* Tests used by pattern matching, guards and andalso/orelse */
bool alku_exact_eq(Value a, Value b);
bool alku_is_true(Value v);
void alku_check_boolean(Value v);

/* Operators */
Value alku_op_add(Value a, Value b);
Value alku_op_sub(Value a, Value b);
Value alku_op_mul(Value a, Value b);
Value alku_op_div(Value a, Value b);
Value alku_op_rem(Value a, Value b);
Value alku_op_neg(Value a);
Value alku_op_plus(Value a);

Value alku_op_eq(Value a, Value b);
Value alku_op_ne(Value a, Value b);
Value alku_op_exact_eq(Value a, Value b);
Value alku_op_exact_ne(Value a, Value b);
Value alku_op_lt(Value a, Value b);
Value alku_op_gt(Value a, Value b);
Value alku_op_le(Value a, Value b);
Value alku_op_ge(Value a, Value b);

Value alku_op_and(Value a, Value b);
Value alku_op_or(Value a, Value b);
Value alku_op_xor(Value a, Value b);
Value alku_op_not(Value a);

/* Built-in functions */
Value alku_bif_is_integer(Value v);
Value alku_bif_is_atom(Value v);
Value alku_bif_is_boolean(Value v);
Value alku_bif_abs(Value v);
Value alku_io_write(Value v);
Value alku_io_nl(void);

/* Runtime errors: print an Erlang-style message and exit with status 1. */
_Noreturn void alku_error_function_clause(const char *name, int arity, const Value *args);
_Noreturn void alku_error_badmatch(Value v);
_Noreturn void alku_error_case_clause(Value v);
_Noreturn void alku_error_if_clause(void);

#endif
