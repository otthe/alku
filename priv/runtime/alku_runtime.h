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
 * number < atom < fun < pid < tuple < map < nil < list < binary.
 * The three number tags compare by numeric value. */
typedef enum {
    ALKU_INT,   /* integer that fits in 64 bits */
    ALKU_BIG,   /* any other integer (on the heap) */
    ALKU_FLOAT,
    ALKU_ATOM,
    ALKU_FUN,
    ALKU_PID, /* an open file, like the pids Erlang uses for files */
    ALKU_TUPLE,
    ALKU_MAP,
    ALKU_NIL,
    ALKU_CONS,
    ALKU_BINARY,
    ALKU_PENDING /* not a term: marks a pending tail call */
} AlkuTag;

typedef struct AlkuTuple AlkuTuple;
typedef struct AlkuCons AlkuCons;
typedef struct AlkuFun AlkuFun;
typedef struct AlkuBig AlkuBig;
typedef struct AlkuMapNode AlkuMapNode;
typedef struct AlkuBinary AlkuBinary;

typedef struct {
    AlkuTag tag;
    union {
        int64_t integer;
        double flt;
        const char *atom; /* UTF-8 name; atoms are compared by name */
        AlkuBig *big;
        AlkuTuple *tuple;
        AlkuCons *cons;
        AlkuFun *fun;
        AlkuMapNode *map; /* root of a balanced tree; NULL for #{} */
        AlkuBinary *binary;
    } as;
} Value;

typedef Value (*AlkuCode)(const Value *args, AlkuFun *self);

struct AlkuTuple {
    size_t arity;
    Value elements[];
};

/* A big integer: sign and magnitude in 32-bit limbs, least significant
 * first. Never holds a value that fits in an int64. */
struct AlkuBig {
    size_t length;
    bool negative;
    uint32_t limbs[];
};

/* A map is a persistent weight-balanced binary tree ordered by key. */
struct AlkuMapNode {
    size_t size; /* nodes in this subtree */
    Value key;
    Value value;
    AlkuMapNode *left, *right;
};

/* A binary: size bytes at bytes. A binary owns its bytes (they follow
 * the struct in data) or, as a sub-binary made by matching, points into
 * the bytes of its parent. */
struct AlkuBinary {
    size_t size;
    const unsigned char *bytes;
    AlkuBinary *parent; /* NULL when the bytes are in data */
    unsigned char data[];
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

/* Call frames, for stack traces. Every compiled function keeps an
 * AlkuFrame on the C stack while it runs; frame.line is the line it is
 * executing. A tail call leaves the frame first, as in Erlang, so loops
 * do not pile up frames. */
typedef struct {
    const char *module;
    const char *function;
    int arity;
    const char *file;
} AlkuFunctionInfo;

typedef struct AlkuFrame {
    struct AlkuFrame *prev;
    const AlkuFunctionInfo *info;
    int line;
} AlkuFrame;

extern AlkuFrame *alku_frames; /* the innermost frame */

static inline void alku_enter(AlkuFrame *frame, const AlkuFunctionInfo *info, int line) {
    frame->prev = alku_frames;
    frame->info = info;
    frame->line = line;
    alku_frames = frame;
}

/* Exceptions. try pushes a handler, then calls setjmp(handler.env):
 * raising an exception jumps there with the class (error, throw or exit),
 * reason and stack trace available from alku_caught_class/reason/stack. */
typedef struct AlkuHandler {
    jmp_buf env;
    struct AlkuHandler *prev;
    AlkuFrame *frames; /* the frames when the try started */
} AlkuHandler;

void alku_try(AlkuHandler *h);
void alku_end_try(AlkuHandler *h);
_Noreturn void alku_raise(Value class, Value reason);
Value alku_caught_class(void);
Value alku_caught_reason(void);
Value alku_caught_stack(void);
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
Value alku_float(double d);
Value alku_integer_literal(const char *decimal); /* for integers beyond 64 bits */
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
Value alku_op_fdiv(Value a, Value b); /* / */
Value alku_op_neg(Value a);
Value alku_op_plus(Value a);
Value alku_op_band(Value a, Value b);
Value alku_op_bor(Value a, Value b);
Value alku_op_bxor(Value a, Value b);
Value alku_op_bsl(Value a, Value b);
Value alku_op_bsr(Value a, Value b);
Value alku_op_bnot(Value a);

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

/* Maps */
Value alku_map_new(void);
Value alku_map_put(Value map, Value key, Value value);    /* M#{K => V} */
Value alku_map_update(Value map, Value key, Value value); /* M#{K := V} */
Value alku_map_check(Value map);                          /* M#{} */
bool alku_is_map(Value v);
bool alku_map_lookup(Value map, Value key, Value *value);  /* for patterns */

/* Binaries: building <<...>> and matching it in patterns.
 * Sizes are given in units (a segment is size * unit bits). */
#define ALKU_BIN_SIGNED 1
#define ALKU_BIN_LITTLE 2

typedef struct {
    unsigned char *data;
    size_t bits, capacity;
} AlkuBinBuilder;

typedef struct {
    const AlkuBinary *binary;
    size_t pos, bits; /* bit position and total bits */
} AlkuBinMatch;

void alku_bin_begin(AlkuBinBuilder *b);
void alku_bin_put_integer(AlkuBinBuilder *b, Value v, Value size, unsigned unit, int flags);
void alku_bin_put_float(AlkuBinBuilder *b, Value v, Value size, unsigned unit, int flags);
void alku_bin_put_binary(AlkuBinBuilder *b, Value v, Value size, unsigned unit); /* size all: whole */
void alku_bin_put_utf8(AlkuBinBuilder *b, Value v);
Value alku_bin_end(AlkuBinBuilder *b);

bool alku_bin_match_start(AlkuBinMatch *m, Value v);
bool alku_bin_get_integer(AlkuBinMatch *m, Value size, unsigned unit, int flags, Value *out);
bool alku_bin_get_float(AlkuBinMatch *m, Value size, unsigned unit, int flags, Value *out);
bool alku_bin_get_binary(AlkuBinMatch *m, Value size, unsigned unit, Value *out);
bool alku_bin_get_rest(AlkuBinMatch *m, Value *out);
bool alku_bin_get_utf8(AlkuBinMatch *m, Value *out);
bool alku_bin_match_end(AlkuBinMatch *m);

/* Built-in functions */
Value alku_bif_is_integer(Value v);
Value alku_bif_is_atom(Value v);
Value alku_bif_is_boolean(Value v);
Value alku_bif_is_list(Value v);
Value alku_bif_is_tuple(Value v);
Value alku_bif_is_function_1(Value v);
Value alku_bif_is_function_2(Value v, Value arity);
Value alku_bif_abs(Value v);
Value alku_bif_is_float(Value v);
Value alku_bif_is_number(Value v);
Value alku_bif_float(Value v);
Value alku_bif_round(Value v);
Value alku_bif_trunc(Value v);
Value alku_bif_floor(Value v);
Value alku_bif_ceil(Value v);
Value alku_bif_float_to_list_1(Value f);
Value alku_bif_float_to_list_2(Value f, Value options);
Value alku_bif_list_to_float(Value chars);
Value alku_bif_integer_to_list_2(Value n, Value base);
Value alku_bif_list_to_integer_2(Value chars, Value base);
Value alku_bif_is_record_2(Value v, Value name);
Value alku_bif_is_record_3(Value v, Value name, Value size);
Value alku_bif_is_map(Value v);
Value alku_bif_map_size(Value map);
Value alku_bif_is_map_key(Value key, Value map);
Value alku_bif_map_get(Value key, Value map);
Value alku_maps_new(void);
Value alku_maps_get_2(Value key, Value map);
Value alku_maps_get_3(Value key, Value map, Value fallback);
Value alku_maps_put(Value key, Value value, Value map);
Value alku_maps_update(Value key, Value value, Value map);
Value alku_maps_remove(Value key, Value map);
Value alku_maps_is_key(Value key, Value map);
Value alku_maps_find(Value key, Value map);
Value alku_maps_keys(Value map);
Value alku_maps_values(Value map);
Value alku_maps_to_list(Value map);
Value alku_maps_from_list(Value list);
Value alku_maps_size(Value map);
Value alku_maps_merge(Value a, Value b);
Value alku_bif_is_binary(Value v);
Value alku_bif_byte_size(Value v);
Value alku_bif_bit_size(Value v);
Value alku_bif_binary_to_list(Value v);
Value alku_bif_list_to_binary(Value v);
Value alku_bif_iolist_size(Value v);
Value alku_bif_binary_part(Value bin, Value start, Value length);
Value alku_bif_split_binary(Value bin, Value pos);
Value alku_bif_atom_to_binary_1(Value atom);
Value alku_bif_atom_to_binary_2(Value atom, Value encoding);
Value alku_bif_binary_to_atom_1(Value bin);
Value alku_bif_binary_to_atom_2(Value bin, Value encoding);
Value alku_bif_integer_to_binary_1(Value n);
Value alku_bif_integer_to_binary_2(Value n, Value base);
Value alku_bif_binary_to_integer_1(Value bin);
Value alku_bif_binary_to_integer_2(Value bin, Value base);
Value alku_unicode_characters_to_binary(Value chars);
Value alku_unicode_characters_to_list(Value data);
Value alku_binary_split_2(Value bin, Value pattern);
Value alku_binary_split_3(Value bin, Value pattern, Value options);
Value alku_binary_part(Value bin, Value start, Value length);
Value alku_binary_at(Value bin, Value pos);
Value alku_binary_first(Value bin);
Value alku_binary_last(Value bin);
Value alku_file_read_file(Value name);
Value alku_bif_float_to_binary_1(Value f);
Value alku_bif_float_to_binary_2(Value f, Value options);
Value alku_bif_binary_to_float(Value bin);
Value alku_bif_make_tuple(Value n, Value init);
Value alku_bif_size(Value v);
Value alku_bif_system_time_0(void);
Value alku_bif_system_time_1(Value unit);
Value alku_bif_monotonic_time_0(void);
Value alku_bif_monotonic_time_1(Value unit);
Value alku_io_lib_format(Value format, Value args);
Value alku_os_getenv_1(Value name);
Value alku_os_getenv_2(Value name, Value fallback);
Value alku_os_timestamp(void);
Value alku_rand_uniform_0(void);
Value alku_rand_uniform_1(Value n);
Value alku_timer_sleep(Value ms);
Value alku_math_pi(void);
Value alku_math_sqrt(Value x);
Value alku_math_pow(Value x, Value y);
Value alku_math_exp(Value x);
Value alku_math_log(Value x);
Value alku_math_log2(Value x);
Value alku_math_log10(Value x);
Value alku_math_sin(Value x);
Value alku_math_cos(Value x);
Value alku_math_tan(Value x);
Value alku_math_asin(Value x);
Value alku_math_acos(Value x);
Value alku_math_atan(Value x);
Value alku_math_atan2(Value y, Value x);
Value alku_math_floor(Value x);
Value alku_math_ceil(Value x);
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
