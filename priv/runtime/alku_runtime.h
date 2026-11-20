/*
 * The Alku runtime: how Alku values are represented in C, and the
 * operations that generated code calls.
 *
 * Every Alku value is a small struct passed by value. Integers and atoms
 * live inside the struct; tuples and list cells live on the heap and the
 * struct points to them. Strings are lists of character codes, as in Erlang.
 */
#ifndef ALKU_RUNTIME_H
#define ALKU_RUNTIME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Tags are declared in Erlang term order:
 * number < atom < tuple < nil < list. */
typedef enum {
    ALKU_INT,
    ALKU_ATOM,
    ALKU_TUPLE,
    ALKU_NIL,
    ALKU_CONS
} AlkuTag;

typedef struct AlkuTuple AlkuTuple;
typedef struct AlkuCons AlkuCons;

typedef struct {
    AlkuTag tag;
    union {
        int64_t integer;
        const char *atom; /* UTF-8 name; atoms are compared by name */
        AlkuTuple *tuple;
        AlkuCons *cons;
    } as;
} Value;

struct AlkuTuple {
    size_t arity;
    Value elements[];
};

/* One list cell: [head | tail] */
struct AlkuCons {
    Value head;
    Value tail;
};

/* Constructors */
Value alku_int(int64_t n);
Value alku_atom(const char *name);
Value alku_nil(void);
Value alku_cons(Value head, Value tail);
Value alku_make_tuple(size_t arity, const Value *elements);
Value alku_string(const char *utf8, size_t length);

/* Tests used by pattern matching, guards and andalso/orelse */
bool alku_exact_eq(Value a, Value b);
bool alku_is_true(Value v);
bool alku_is_cons(Value v);
bool alku_is_tuple_of(Value v, size_t arity);
void alku_check_boolean(Value v);

/* Unchecked accessors: only used after alku_is_cons / alku_is_tuple_of */
Value alku_head(Value list);
Value alku_tail(Value list);
Value alku_tuple_element(Value tuple, size_t index); /* 0-based */

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

Value alku_op_append(Value a, Value b);   /* ++ */
Value alku_op_subtract(Value a, Value b); /* -- */

/* Built-in functions */
Value alku_bif_is_integer(Value v);
Value alku_bif_is_atom(Value v);
Value alku_bif_is_boolean(Value v);
Value alku_bif_is_list(Value v);
Value alku_bif_is_tuple(Value v);
Value alku_bif_abs(Value v);
Value alku_bif_hd(Value list);
Value alku_bif_tl(Value list);
Value alku_bif_length(Value list);
Value alku_bif_element(Value n, Value tuple);
Value alku_bif_setelement(Value n, Value tuple, Value v);
Value alku_bif_tuple_size(Value tuple);
Value alku_bif_integer_to_list(Value n);
Value alku_bif_atom_to_list(Value atom);
Value alku_lists_reverse_1(Value list);
Value alku_lists_reverse_2(Value list, Value tail);
Value alku_io_write(Value v);
Value alku_io_nl(void);
Value alku_io_format_1(Value format);
Value alku_io_format_2(Value format, Value args);

/* Runtime errors: print an Erlang-style message and exit with status 1. */
_Noreturn void alku_error_function_clause(const char *name, int arity, const Value *args);
_Noreturn void alku_error_badmatch(Value v);
_Noreturn void alku_error_case_clause(Value v);
_Noreturn void alku_error_if_clause(void);

#endif
