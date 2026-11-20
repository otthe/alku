/*
 * The Alku runtime: how Alku values are represented in C, and the
 * operations that generated code calls.
 *
 * Every Alku value is a small struct passed by value. Integers and atoms
 * live inside the struct; tuples, list cells and funs live on the
 * garbage-collected heap and the struct points to them. Strings are lists
 * of character codes, as in Erlang.
 *
 * Calling convention: every compiled Alku function (and every fun body)
 * has the type AlkuCode: it receives its arguments as an array, plus the
 * fun object it belongs to (NULL for named functions). A function may
 * return a "pending tail call" instead of a value; see alku_settle.
 */
#ifndef ALKU_RUNTIME_H
#define ALKU_RUNTIME_H

#include <setjmp.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Tags are declared in Erlang term order:
 * number < atom < fun < pid < tuple < nil < list. */
typedef enum {
    ALKU_INT,
    ALKU_ATOM,
    ALKU_FUN,
    ALKU_PID, /* an open file, like the pids Erlang uses for files */
    ALKU_TUPLE,
    ALKU_NIL,
    ALKU_CONS,
    ALKU_PENDING /* not a term: marks a pending tail call */
} AlkuTag;

typedef struct AlkuTuple AlkuTuple;
typedef struct AlkuCons AlkuCons;
typedef struct AlkuFun AlkuFun;

typedef struct {
    AlkuTag tag;
    union {
        int64_t integer;
        const char *atom; /* UTF-8 name; atoms are compared by name */
        AlkuTuple *tuple;
        AlkuCons *cons;
        AlkuFun *fun;
    } as;
} Value;

typedef Value (*AlkuCode)(const Value *args, AlkuFun *self);

struct AlkuTuple {
    size_t arity;
    Value elements[];
};

/* One list cell: [head | tail] */
struct AlkuCons {
    Value head;
    Value tail;
};

/* A fun: code plus the variables it captured. */
struct AlkuFun {
    AlkuCode code;
    size_t arity;
    const char *name; /* for printing */
    size_t free_count;
    Value env[];
};

/* Program entry: runs main/0 or main/1 (which gets the command-line
 * arguments) and returns the process exit status. */
int alku_start(AlkuCode main, int arity, int argc, char **argv);

/* Exceptions. try pushes a handler, then calls setjmp(handler.env):
 * raising an exception jumps there with the class (error, throw or exit)
 * and reason available from alku_caught_class/alku_caught_reason. */
typedef struct AlkuHandler {
    jmp_buf env;
    struct AlkuHandler *prev;
} AlkuHandler;

void alku_try(AlkuHandler *h);
void alku_end_try(AlkuHandler *h);
_Noreturn void alku_raise(Value class, Value reason);
Value alku_caught_class(void);
Value alku_caught_reason(void);
Value alku_catch_value(Value class, Value reason);

/* Calls and tail calls */
Value alku_apply(Value fun, size_t arity, const Value *args);
Value alku_tail_apply(Value fun, size_t arity, const Value *args);
Value alku_tail_call(AlkuCode code, AlkuFun *self, size_t arity, const Value *args);
Value alku_run_pending(void);

/* A tail call returns a pending marker instead of calling the target, so
 * the C stack does not grow. Every non-tail call site runs pending calls
 * until a real value comes back. */
static inline Value alku_settle(Value v) {
    while (v.tag == ALKU_PENDING) v = alku_run_pending();
    return v;
}

/* Constructors */
Value alku_int(int64_t n);
Value alku_atom(const char *name);
Value alku_nil(void);
Value alku_cons(Value head, Value tail);
Value alku_make_tuple(size_t arity, const Value *elements);
Value alku_string(const char *utf8, size_t length);
Value alku_make_fun(AlkuCode code, size_t arity, const char *name,
                    size_t free_count, const Value *env);
Value alku_fun_value(AlkuFun *fun);

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
Value alku_bif_is_function_1(Value v);
Value alku_bif_is_function_2(Value v, Value arity);
Value alku_bif_abs(Value v);
Value alku_bif_hd(Value list);
Value alku_bif_tl(Value list);
Value alku_bif_length(Value list);
Value alku_bif_element(Value n, Value tuple);
Value alku_bif_setelement(Value n, Value tuple, Value v);
Value alku_bif_tuple_size(Value tuple);
Value alku_bif_integer_to_list(Value n);
Value alku_bif_atom_to_list(Value atom);
Value alku_bif_list_to_integer(Value chars);
Value alku_bif_list_to_atom(Value chars);
Value alku_bif_tuple_to_list(Value tuple);
Value alku_bif_list_to_tuple(Value list);
Value alku_bif_max(Value a, Value b);
Value alku_bif_min(Value a, Value b);
Value alku_bif_apply_2(Value fun, Value args);
Value alku_bif_throw(Value reason);
Value alku_bif_error_1(Value reason);
Value alku_bif_error_2(Value reason, Value args);
Value alku_bif_exit(Value reason);
Value alku_bif_halt_0(void);
Value alku_bif_halt_1(Value status);
Value alku_lists_reverse_1(Value list);
Value alku_lists_reverse_2(Value list, Value tail);

/* io and file. Devices are standard_io, standard_error or an open file. */
Value alku_io_write(Value v);
Value alku_io_write_2(Value dev, Value v);
Value alku_io_nl(void);
Value alku_io_nl_1(Value dev);
Value alku_io_format_1(Value format);
Value alku_io_format_2(Value format, Value args);
Value alku_io_format_3(Value dev, Value format, Value args);
Value alku_io_put_chars_1(Value chars);
Value alku_io_put_chars_2(Value dev, Value chars);
Value alku_io_get_line_1(Value prompt);
Value alku_io_get_line_2(Value dev, Value prompt);
Value alku_file_open(Value name, Value modes);
Value alku_file_close(Value dev);
Value alku_file_write_file(Value name, Value data);

/* Runtime errors: raise the same error Erlang would. */
_Noreturn void alku_error_function_clause(const char *name, int arity, const Value *args);
_Noreturn void alku_error_badmatch(Value v);
_Noreturn void alku_error_case_clause(Value v);
_Noreturn void alku_error_if_clause(void);
_Noreturn void alku_error_try_clause(Value v);

#endif
