#include "alku_runtime.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------- Printing ---------- */

static bool is_reserved_word(const char *name) {
    static const char *words[] = {
        "after", "and", "andalso", "band", "begin", "bnot", "bor", "bsl",
        "bsr", "bxor", "case", "catch", "cond", "div", "end", "fun", "if",
        "let", "not", "of", "or", "orelse", "receive", "rem", "try", "when",
        "xor", NULL};
    for (int i = 0; words[i] != NULL; i++) {
        if (strcmp(name, words[i]) == 0) return true;
    }
    return false;
}

/* An atom prints without quotes if it looks like `foo_Bar@1`. */
static bool atom_needs_quotes(const char *name) {
    if (name[0] < 'a' || name[0] > 'z') return true;
    for (const char *c = name; *c != '\0'; c++) {
        bool plain = (*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') ||
                     (*c >= '0' && *c <= '9') || *c == '_' || *c == '@';
        if (!plain) return true;
    }
    return is_reserved_word(name);
}

static void print_atom(FILE *out, const char *name) {
    if (!atom_needs_quotes(name)) {
        fputs(name, out);
        return;
    }
    fputc('\'', out);
    for (const char *c = name; *c != '\0'; c++) {
        if (*c == '\'' || *c == '\\') fputc('\\', out);
        fputc(*c, out);
    }
    fputc('\'', out);
}

static void print_value(FILE *out, Value v) {
    switch (v.tag) {
    case ALKU_INT:
        fprintf(out, "%" PRId64, v.as.integer);
        break;
    case ALKU_ATOM:
        print_atom(out, v.as.atom);
        break;
    }
}

/* ---------- Errors ---------- */

/* Prints "** exception error: <message>" and exits. Values in the message
 * are passed as Value pointers and written with "%v". */
static _Noreturn void fail(const char *format, ...) {
    fflush(stdout); /* keep the program's earlier output before the error */
    fputs("** exception error: ", stderr);
    va_list args;
    va_start(args, format);
    for (const char *f = format; *f != '\0'; f++) {
        if (f[0] == '%' && f[1] == 'v') {
            print_value(stderr, *va_arg(args, const Value *));
            f++;
        } else {
            fputc(*f, stderr);
        }
    }
    va_end(args);
    fputc('\n', stderr);
    exit(1);
}

static _Noreturn void badarith(void) {
    fail("an error occurred when evaluating an arithmetic expression");
}

static _Noreturn void overflow(void) {
    fail("integer overflow (Alku integers are 64-bit for now)");
}

static _Noreturn void badarg(Value v) {
    fail("bad argument: %v", &v);
}

_Noreturn void alku_error_function_clause(const char *name, int arity, const Value *args) {
    fflush(stdout);
    fputs("** exception error: no function clause matching ", stderr);
    print_atom(stderr, name);
    fputc('(', stderr);
    for (int i = 0; i < arity; i++) {
        if (i > 0) fputs(", ", stderr);
        print_value(stderr, args[i]);
    }
    fputs(")\n", stderr);
    exit(1);
}

_Noreturn void alku_error_badmatch(Value v) {
    fail("no match of right hand side value %v", &v);
}

_Noreturn void alku_error_case_clause(Value v) {
    fail("no case clause matching %v", &v);
}

_Noreturn void alku_error_if_clause(void) {
    fail("no true branch found when evaluating an if expression");
}

/* ---------- Constructors and tests ---------- */

Value alku_int(int64_t n) {
    Value v;
    v.tag = ALKU_INT;
    v.as.integer = n;
    return v;
}

Value alku_atom(const char *name) {
    Value v;
    v.tag = ALKU_ATOM;
    v.as.atom = name;
    return v;
}

static Value boolean(bool b) {
    return alku_atom(b ? "true" : "false");
}

static bool is_atom_named(Value v, const char *name) {
    return v.tag == ALKU_ATOM && strcmp(v.as.atom, name) == 0;
}

/* Erlang term order. Returns <0, 0 or >0 like strcmp. */
static int compare(Value a, Value b) {
    if (a.tag != b.tag) return a.tag < b.tag ? -1 : 1;
    switch (a.tag) {
    case ALKU_INT:
        return (a.as.integer > b.as.integer) - (a.as.integer < b.as.integer);
    case ALKU_ATOM:
        return strcmp(a.as.atom, b.as.atom);
    }
    return 0;
}

bool alku_exact_eq(Value a, Value b) {
    return compare(a, b) == 0;
}

bool alku_is_true(Value v) {
    return is_atom_named(v, "true");
}

static bool boolean_arg(Value v) {
    if (is_atom_named(v, "true")) return true;
    if (is_atom_named(v, "false")) return false;
    badarg(v);
}

void alku_check_boolean(Value v) {
    (void)boolean_arg(v);
}

/* ---------- Arithmetic ---------- */

static int64_t integer_arg(Value v) {
    if (v.tag != ALKU_INT) badarith();
    return v.as.integer;
}

Value alku_op_add(Value a, Value b) {
    int64_t r;
    if (__builtin_add_overflow(integer_arg(a), integer_arg(b), &r)) overflow();
    return alku_int(r);
}

Value alku_op_sub(Value a, Value b) {
    int64_t r;
    if (__builtin_sub_overflow(integer_arg(a), integer_arg(b), &r)) overflow();
    return alku_int(r);
}

Value alku_op_mul(Value a, Value b) {
    int64_t r;
    if (__builtin_mul_overflow(integer_arg(a), integer_arg(b), &r)) overflow();
    return alku_int(r);
}

/* Erlang's div and rem truncate toward zero, exactly like C's / and %. */
Value alku_op_div(Value a, Value b) {
    int64_t x = integer_arg(a), y = integer_arg(b);
    if (y == 0) badarith();
    if (x == INT64_MIN && y == -1) overflow();
    return alku_int(x / y);
}

Value alku_op_rem(Value a, Value b) {
    int64_t x = integer_arg(a), y = integer_arg(b);
    if (y == 0) badarith();
    if (y == -1) return alku_int(0); /* avoids INT64_MIN % -1, which is undefined in C */
    return alku_int(x % y);
}

Value alku_op_neg(Value a) {
    int64_t x = integer_arg(a);
    if (x == INT64_MIN) overflow();
    return alku_int(-x);
}

Value alku_op_plus(Value a) {
    return alku_int(integer_arg(a));
}

/* ---------- Comparison ---------- */

/* == and =:= only differ for floats, which Alku does not have yet. */
Value alku_op_eq(Value a, Value b) { return boolean(compare(a, b) == 0); }
Value alku_op_ne(Value a, Value b) { return boolean(compare(a, b) != 0); }
Value alku_op_exact_eq(Value a, Value b) { return boolean(compare(a, b) == 0); }
Value alku_op_exact_ne(Value a, Value b) { return boolean(compare(a, b) != 0); }
Value alku_op_lt(Value a, Value b) { return boolean(compare(a, b) < 0); }
Value alku_op_gt(Value a, Value b) { return boolean(compare(a, b) > 0); }
Value alku_op_le(Value a, Value b) { return boolean(compare(a, b) <= 0); }
Value alku_op_ge(Value a, Value b) { return boolean(compare(a, b) >= 0); }

/* ---------- Boolean operators ---------- */

Value alku_op_and(Value a, Value b) {
    bool x = boolean_arg(a), y = boolean_arg(b);
    return boolean(x && y);
}

Value alku_op_or(Value a, Value b) {
    bool x = boolean_arg(a), y = boolean_arg(b);
    return boolean(x || y);
}

Value alku_op_xor(Value a, Value b) {
    bool x = boolean_arg(a), y = boolean_arg(b);
    return boolean(x != y);
}

Value alku_op_not(Value a) {
    return boolean(!boolean_arg(a));
}

/* ---------- Built-in functions ---------- */

Value alku_bif_is_integer(Value v) {
    return boolean(v.tag == ALKU_INT);
}

Value alku_bif_is_atom(Value v) {
    return boolean(v.tag == ALKU_ATOM);
}

Value alku_bif_is_boolean(Value v) {
    return boolean(is_atom_named(v, "true") || is_atom_named(v, "false"));
}

Value alku_bif_abs(Value v) {
    int64_t x = integer_arg(v);
    if (x == INT64_MIN) overflow();
    return alku_int(x < 0 ? -x : x);
}

Value alku_io_write(Value v) {
    print_value(stdout, v);
    return alku_atom("ok");
}

Value alku_io_nl(void) {
    putchar('\n');
    return alku_atom("ok");
}
