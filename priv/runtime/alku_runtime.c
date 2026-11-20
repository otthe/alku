#include "alku_runtime.h"
#include "alku_gc.h"

#include <inttypes.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static _Noreturn void out_of_memory(void) {
    fputs("** out of memory\n", stderr);
    exit(1);
}

/* ---------- Output buffer ---------- */

/* Text is formatted into a buffer first and written in one go, so a
 * failing io:format prints nothing, as in Erlang. */
typedef struct {
    char *data;
    size_t length, capacity;
} Buffer;

static void buf_putc(Buffer *b, char c) {
    if (b->length == b->capacity) {
        b->capacity = b->capacity ? 2 * b->capacity : 64;
        b->data = realloc(b->data, b->capacity);
        if (b->data == NULL) out_of_memory();
    }
    b->data[b->length++] = c;
}

static void buf_puts(Buffer *b, const char *s) {
    for (; *s != '\0'; s++) buf_putc(b, *s);
}

static void buf_int(Buffer *b, int64_t n) {
    char digits[24];
    snprintf(digits, sizeof digits, "%" PRId64, n);
    buf_puts(b, digits);
}

static bool is_char_code(int64_t c) {
    return c >= 0 && c <= 0x10FFFF;
}

static void buf_utf8(Buffer *b, int64_t c) {
    if (c < 0x80) {
        buf_putc(b, (char)c);
    } else if (c < 0x800) {
        buf_putc(b, (char)(0xC0 | (c >> 6)));
        buf_putc(b, (char)(0x80 | (c & 0x3F)));
    } else if (c < 0x10000) {
        buf_putc(b, (char)(0xE0 | (c >> 12)));
        buf_putc(b, (char)(0x80 | ((c >> 6) & 0x3F)));
        buf_putc(b, (char)(0x80 | (c & 0x3F)));
    } else {
        buf_putc(b, (char)(0xF0 | (c >> 18)));
        buf_putc(b, (char)(0x80 | ((c >> 12) & 0x3F)));
        buf_putc(b, (char)(0x80 | ((c >> 6) & 0x3F)));
        buf_putc(b, (char)(0x80 | (c & 0x3F)));
    }
}

static void buf_flush(Buffer *b, FILE *out) {
    if (b->length > 0) fwrite(b->data, 1, b->length, out);
    free(b->data);
    b->data = NULL;
    b->length = b->capacity = 0;
}

/* ---------- Printing terms (~w and ~p) ---------- */

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

static void print_atom(Buffer *b, const char *name) {
    if (!atom_needs_quotes(name)) {
        buf_puts(b, name);
        return;
    }
    buf_putc(b, '\'');
    for (const char *c = name; *c != '\0'; c++) {
        if (*c == '\'' || *c == '\\') buf_putc(b, '\\');
        buf_putc(b, *c);
    }
    buf_putc(b, '\'');
}

/* ~p shows a list as a string when every element is a printable
 * Latin-1 character (Erlang's default printable range). */
static bool is_printable_char(Value v) {
    if (v.tag != ALKU_INT) return false;
    int64_t c = v.as.integer;
    return (c >= 32 && c <= 126) || (c >= 8 && c <= 13) || c == 27 ||
           (c >= 160 && c <= 255);
}

static bool is_printable_string(Value v) {
    if (v.tag != ALKU_CONS) return false;
    for (; v.tag == ALKU_CONS; v = v.as.cons->tail) {
        if (!is_printable_char(v.as.cons->head)) return false;
    }
    return v.tag == ALKU_NIL;
}

static void print_string(Buffer *b, Value v) {
    buf_putc(b, '"');
    for (; v.tag == ALKU_CONS; v = v.as.cons->tail) {
        int64_t c = v.as.cons->head.as.integer;
        switch (c) {
        case '\n': buf_puts(b, "\\n"); break;
        case '\r': buf_puts(b, "\\r"); break;
        case '\t': buf_puts(b, "\\t"); break;
        case '\v': buf_puts(b, "\\v"); break;
        case '\b': buf_puts(b, "\\b"); break;
        case '\f': buf_puts(b, "\\f"); break;
        case 27: buf_puts(b, "\\e"); break;
        case '"': buf_puts(b, "\\\""); break;
        case '\\': buf_puts(b, "\\\\"); break;
        default: buf_utf8(b, c); break;
        }
    }
    buf_putc(b, '"');
}

/* pretty=false is ~w (and io:write); pretty=true is ~p, minus the line
 * breaking Erlang does for long terms. */
static void print_value(Buffer *b, Value v, bool pretty) {
    switch (v.tag) {
    case ALKU_INT:
        buf_int(b, v.as.integer);
        break;
    case ALKU_ATOM:
        print_atom(b, v.as.atom);
        break;
    case ALKU_FUN:
        /* "fun lists:map/2" for external funs, #Fun<...> for the rest */
        if (strncmp(v.as.fun->name, "fun ", 4) == 0) {
            buf_puts(b, v.as.fun->name);
        } else {
            buf_puts(b, "#Fun<");
            buf_puts(b, v.as.fun->name);
            buf_putc(b, '>');
        }
        break;
    case ALKU_PENDING:
        buf_puts(b, "<pending call>"); /* never seen by Alku code */
        break;
    case ALKU_TUPLE:
        buf_putc(b, '{');
        for (size_t i = 0; i < v.as.tuple->arity; i++) {
            if (i > 0) buf_putc(b, ',');
            print_value(b, v.as.tuple->elements[i], pretty);
        }
        buf_putc(b, '}');
        break;
    case ALKU_NIL:
        buf_puts(b, "[]");
        break;
    case ALKU_CONS:
        if (pretty && is_printable_string(v)) {
            print_string(b, v);
            break;
        }
        buf_putc(b, '[');
        print_value(b, v.as.cons->head, pretty);
        for (v = v.as.cons->tail; v.tag == ALKU_CONS; v = v.as.cons->tail) {
            buf_putc(b, ',');
            print_value(b, v.as.cons->head, pretty);
        }
        if (v.tag != ALKU_NIL) { /* improper list like [1|2] */
            buf_putc(b, '|');
            print_value(b, v, pretty);
        }
        buf_putc(b, ']');
        break;
    }
}

/* ---------- Errors ---------- */

/* Prints "** exception error: <message>" and exits. Values in the message
 * are passed as Value pointers and written with "%v". */
static _Noreturn void fail(const char *format, ...) {
    Buffer b = {0};
    buf_puts(&b, "** exception error: ");
    va_list args;
    va_start(args, format);
    for (const char *f = format; *f != '\0'; f++) {
        if (f[0] == '%' && f[1] == 'v') {
            print_value(&b, *va_arg(args, const Value *), true);
            f++;
        } else {
            buf_putc(&b, *f);
        }
    }
    va_end(args);
    buf_putc(&b, '\n');
    fflush(stdout); /* keep the program's earlier output before the error */
    buf_flush(&b, stderr);
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
    Buffer b = {0};
    buf_puts(&b, "** exception error: no function clause matching ");
    print_atom(&b, name);
    buf_putc(&b, '(');
    for (int i = 0; i < arity; i++) {
        if (i > 0) buf_puts(&b, ", ");
        print_value(&b, args[i], true);
    }
    buf_puts(&b, ")\n");
    fflush(stdout);
    buf_flush(&b, stderr);
    exit(1);
}

static _Noreturn void badfun(Value v) {
    fail("bad function %v", &v);
}

static _Noreturn void badarity(Value fun, size_t arity) {
    Buffer b = {0};
    buf_puts(&b, "** exception error: ");
    print_value(&b, fun, true);
    buf_puts(&b, " called with ");
    buf_int(&b, (int64_t)arity);
    buf_puts(&b, arity == 1 ? " argument\n" : " arguments\n");
    fflush(stdout);
    buf_flush(&b, stderr);
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

/* ---------- Constructors ---------- */

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

Value alku_nil(void) {
    Value v;
    v.tag = ALKU_NIL;
    v.as.integer = 0;
    return v;
}

Value alku_cons(Value head, Value tail) {
    AlkuCons *cell = gc_alloc(GC_CONS, sizeof *cell);
    cell->head = head;
    cell->tail = tail;
    Value v;
    v.tag = ALKU_CONS;
    v.as.cons = cell;
    return v;
}

/* elements may be NULL: the tuple is then filled with [] */
Value alku_make_tuple(size_t arity, const Value *elements) {
    AlkuTuple *t = gc_alloc(GC_TUPLE, sizeof *t + arity * sizeof(Value));
    t->arity = arity;
    for (size_t i = 0; i < arity; i++) t->elements[i] = elements ? elements[i] : alku_nil();
    Value v;
    v.tag = ALKU_TUPLE;
    v.as.tuple = t;
    return v;
}

Value alku_make_fun(AlkuCode code, size_t arity, const char *name,
                    size_t free_count, const Value *env) {
    AlkuFun *f = gc_alloc(GC_FUN, sizeof *f + free_count * sizeof(Value));
    f->code = code;
    f->arity = arity;
    f->name = name;
    f->free_count = free_count;
    for (size_t i = 0; i < free_count; i++) f->env[i] = env[i];
    return alku_fun_value(f);
}

Value alku_fun_value(AlkuFun *fun) {
    Value v;
    v.tag = ALKU_FUN;
    v.as.fun = fun;
    return v;
}

static Value boolean(bool b) {
    return alku_atom(b ? "true" : "false");
}

/* Builds a list front to back. */
typedef struct {
    Value first;
    AlkuCons *last;
} ListBuilder;

static ListBuilder list_builder(void) {
    ListBuilder lb = {alku_nil(), NULL};
    return lb;
}

static void list_push(ListBuilder *lb, Value v) {
    Value cell = alku_cons(v, alku_nil());
    if (lb->last != NULL) {
        lb->last->tail = cell;
    } else {
        lb->first = cell;
    }
    lb->last = cell.as.cons;
}

static Value list_finish(ListBuilder *lb, Value tail) {
    if (lb->last == NULL) return tail;
    lb->last->tail = tail;
    return lb->first;
}

/* UTF-8 bytes -> list of character codes. Input comes from the compiler
 * or from atom names, so it is valid UTF-8. */
static Value utf8_to_list(const unsigned char *s, size_t length) {
    ListBuilder lb = list_builder();
    size_t i = 0;
    while (i < length) {
        unsigned char c = s[i];
        int64_t code;
        size_t n;
        if (c < 0x80) { code = c; n = 1; }
        else if ((c & 0xE0) == 0xC0) { code = c & 0x1F; n = 2; }
        else if ((c & 0xF0) == 0xE0) { code = c & 0x0F; n = 3; }
        else { code = c & 0x07; n = 4; }
        if (i + n > length) n = length - i;
        for (size_t k = 1; k < n; k++) code = (code << 6) | (s[i + k] & 0x3F);
        list_push(&lb, alku_int(code));
        i += n;
    }
    return list_finish(&lb, alku_nil());
}

Value alku_string(const char *utf8, size_t length) {
    return utf8_to_list((const unsigned char *)utf8, length);
}

/* ---------- Tests and accessors ---------- */

static bool is_atom_named(Value v, const char *name) {
    return v.tag == ALKU_ATOM && strcmp(v.as.atom, name) == 0;
}

static int sign(int x) {
    return (x > 0) - (x < 0);
}

/* Erlang term order. Returns -1, 0 or 1. */
static int compare(Value a, Value b) {
    for (;;) {
        if (a.tag != b.tag) return a.tag < b.tag ? -1 : 1;
        switch (a.tag) {
        case ALKU_INT:
            return (a.as.integer > b.as.integer) - (a.as.integer < b.as.integer);
        case ALKU_ATOM:
            return sign(strcmp(a.as.atom, b.as.atom));
        case ALKU_NIL:
        case ALKU_PENDING:
            return 0;
        case ALKU_FUN: {
            /* same code and same captured values means equal */
            AlkuFun *f = a.as.fun, *g = b.as.fun;
            if (f->code != g->code) {
                return (uintptr_t)f->code < (uintptr_t)g->code ? -1 : 1;
            }
            for (size_t i = 0; i < f->free_count; i++) {
                int c = compare(f->env[i], g->env[i]);
                if (c != 0) return c;
            }
            return 0;
        }
        case ALKU_TUPLE: {
            size_t n = a.as.tuple->arity, m = b.as.tuple->arity;
            if (n != m) return n < m ? -1 : 1; /* smaller tuples come first */
            for (size_t i = 0; i < n; i++) {
                int c = compare(a.as.tuple->elements[i], b.as.tuple->elements[i]);
                if (c != 0) return c;
            }
            return 0;
        }
        case ALKU_CONS: {
            int c = compare(a.as.cons->head, b.as.cons->head);
            if (c != 0) return c;
            a = a.as.cons->tail; /* loop instead of recursing on long lists */
            b = b.as.cons->tail;
            break;
        }
        }
    }
}

bool alku_exact_eq(Value a, Value b) {
    return compare(a, b) == 0;
}

bool alku_is_true(Value v) {
    return is_atom_named(v, "true");
}

bool alku_is_cons(Value v) {
    return v.tag == ALKU_CONS;
}

bool alku_is_tuple_of(Value v, size_t arity) {
    return v.tag == ALKU_TUPLE && v.as.tuple->arity == arity;
}

static bool boolean_arg(Value v) {
    if (is_atom_named(v, "true")) return true;
    if (is_atom_named(v, "false")) return false;
    badarg(v);
}

void alku_check_boolean(Value v) {
    (void)boolean_arg(v);
}

Value alku_head(Value list) { return list.as.cons->head; }
Value alku_tail(Value list) { return list.as.cons->tail; }
Value alku_tuple_element(Value tuple, size_t index) { return tuple.as.tuple->elements[index]; }

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

/* ---------- Lists ---------- */

/* Length of a proper list; anything else is a bad argument. */
static size_t proper_length(Value list) {
    size_t n = 0;
    Value v = list;
    for (; v.tag == ALKU_CONS; v = v.as.cons->tail) n++;
    if (v.tag != ALKU_NIL) badarg(list);
    return n;
}

Value alku_op_append(Value a, Value b) {
    ListBuilder lb = list_builder();
    Value v = a;
    for (; v.tag == ALKU_CONS; v = v.as.cons->tail) list_push(&lb, v.as.cons->head);
    if (v.tag != ALKU_NIL) badarg(a);
    return list_finish(&lb, b);
}

/* A -- B removes the first occurrence in A of each element of B. */
Value alku_op_subtract(Value a, Value b) {
    size_t n = proper_length(a);
    (void)proper_length(b);
    /* a tuple, so the collector can see the copied values */
    Value *items = alku_make_tuple(n, NULL).as.tuple->elements;
    bool *removed = malloc(n + 1);
    if (removed == NULL) out_of_memory();
    size_t i = 0;
    for (Value v = a; v.tag == ALKU_CONS; v = v.as.cons->tail, i++) {
        items[i] = v.as.cons->head;
        removed[i] = false;
    }
    for (Value v = b; v.tag == ALKU_CONS; v = v.as.cons->tail) {
        for (i = 0; i < n; i++) {
            if (!removed[i] && compare(items[i], v.as.cons->head) == 0) {
                removed[i] = true;
                break;
            }
        }
    }
    ListBuilder lb = list_builder();
    for (i = 0; i < n; i++) {
        if (!removed[i]) list_push(&lb, items[i]);
    }
    free(removed);
    return list_finish(&lb, alku_nil());
}

/* ---------- Built-in functions ---------- */

Value alku_bif_is_integer(Value v) { return boolean(v.tag == ALKU_INT); }
Value alku_bif_is_atom(Value v) { return boolean(v.tag == ALKU_ATOM); }
Value alku_bif_is_list(Value v) { return boolean(v.tag == ALKU_NIL || v.tag == ALKU_CONS); }
Value alku_bif_is_tuple(Value v) { return boolean(v.tag == ALKU_TUPLE); }
Value alku_bif_is_function_1(Value v) { return boolean(v.tag == ALKU_FUN); }

Value alku_bif_is_function_2(Value v, Value arity) {
    if (arity.tag != ALKU_INT || arity.as.integer < 0) badarg(arity);
    return boolean(v.tag == ALKU_FUN && v.as.fun->arity == (uint64_t)arity.as.integer);
}

Value alku_bif_is_boolean(Value v) {
    return boolean(is_atom_named(v, "true") || is_atom_named(v, "false"));
}

Value alku_bif_abs(Value v) {
    int64_t x = integer_arg(v);
    if (x == INT64_MIN) overflow();
    return alku_int(x < 0 ? -x : x);
}

Value alku_bif_hd(Value list) {
    if (list.tag != ALKU_CONS) badarg(list);
    return list.as.cons->head;
}

Value alku_bif_tl(Value list) {
    if (list.tag != ALKU_CONS) badarg(list);
    return list.as.cons->tail;
}

Value alku_bif_length(Value list) {
    return alku_int((int64_t)proper_length(list));
}

/* Checks a 1-based tuple index and returns it 0-based. */
static size_t tuple_index(Value n, Value tuple) {
    if (tuple.tag != ALKU_TUPLE) badarg(tuple);
    if (n.tag != ALKU_INT || n.as.integer < 1 ||
        (uint64_t)n.as.integer > tuple.as.tuple->arity) {
        badarg(n);
    }
    return (size_t)n.as.integer - 1;
}

Value alku_bif_element(Value n, Value tuple) {
    return tuple.as.tuple->elements[tuple_index(n, tuple)];
}

Value alku_bif_setelement(Value n, Value tuple, Value v) {
    size_t i = tuple_index(n, tuple);
    Value copy = alku_make_tuple(tuple.as.tuple->arity, tuple.as.tuple->elements);
    copy.as.tuple->elements[i] = v;
    return copy;
}

Value alku_bif_tuple_size(Value tuple) {
    if (tuple.tag != ALKU_TUPLE) badarg(tuple);
    return alku_int((int64_t)tuple.as.tuple->arity);
}

Value alku_bif_integer_to_list(Value n) {
    if (n.tag != ALKU_INT) badarg(n);
    char digits[24];
    int length = snprintf(digits, sizeof digits, "%" PRId64, n.as.integer);
    return utf8_to_list((const unsigned char *)digits, (size_t)length);
}

Value alku_bif_atom_to_list(Value atom) {
    if (atom.tag != ALKU_ATOM) badarg(atom);
    return utf8_to_list((const unsigned char *)atom.as.atom, strlen(atom.as.atom));
}

Value alku_lists_reverse_2(Value list, Value tail) {
    Value result = tail;
    Value v = list;
    for (; v.tag == ALKU_CONS; v = v.as.cons->tail) result = alku_cons(v.as.cons->head, result);
    if (v.tag != ALKU_NIL) badarg(list);
    return result;
}

Value alku_lists_reverse_1(Value list) {
    return alku_lists_reverse_2(list, alku_nil());
}

/* ---------- io ---------- */

Value alku_io_write(Value v) {
    Buffer b = {0};
    print_value(&b, v, false);
    buf_flush(&b, stdout);
    return alku_atom("ok");
}

Value alku_io_nl(void) {
    putchar('\n');
    return alku_atom("ok");
}

/* ~s accepts an atom or a (possibly nested) list of character codes. */
static void print_chars(Buffer *b, Value v, Value arg) {
    if (v.tag == ALKU_ATOM) {
        buf_puts(b, v.as.atom);
        return;
    }
    for (; v.tag == ALKU_CONS; v = v.as.cons->tail) {
        Value c = v.as.cons->head;
        if (c.tag == ALKU_INT && is_char_code(c.as.integer)) {
            buf_utf8(b, c.as.integer);
        } else if (c.tag == ALKU_CONS || c.tag == ALKU_NIL) {
            print_chars(b, c, arg);
        } else {
            badarg(arg);
        }
    }
    if (v.tag != ALKU_NIL) badarg(arg);
}

static Value next_arg(Value *args, Value all) {
    if (args->tag != ALKU_CONS) badarg(all);
    Value arg = args->as.cons->head;
    *args = args->as.cons->tail;
    return arg;
}

/* Supports ~w ~p ~s ~c ~b ~B ~n ~~ (no field widths yet). */
Value alku_io_format_2(Value format, Value args) {
    Buffer b = {0};
    Value f = format, rest = args;
    while (f.tag == ALKU_CONS) {
        Value c = f.as.cons->head;
        f = f.as.cons->tail;
        if (c.tag != ALKU_INT || !is_char_code(c.as.integer)) badarg(format);
        if (c.as.integer != '~') {
            buf_utf8(&b, c.as.integer);
            continue;
        }
        if (f.tag != ALKU_CONS || f.as.cons->head.tag != ALKU_INT) badarg(format);
        int64_t control = f.as.cons->head.as.integer;
        f = f.as.cons->tail;
        switch (control) {
        case 'n': buf_putc(&b, '\n'); break;
        case '~': buf_putc(&b, '~'); break;
        case 'w': print_value(&b, next_arg(&rest, args), false); break;
        case 'p': print_value(&b, next_arg(&rest, args), true); break;
        case 's': {
            Value arg = next_arg(&rest, args);
            print_chars(&b, arg, arg);
            break;
        }
        case 'c': {
            Value arg = next_arg(&rest, args);
            if (arg.tag != ALKU_INT || !is_char_code(arg.as.integer)) badarg(arg);
            buf_utf8(&b, arg.as.integer);
            break;
        }
        case 'b':
        case 'B': {
            Value arg = next_arg(&rest, args);
            if (arg.tag != ALKU_INT) badarg(arg);
            buf_int(&b, arg.as.integer);
            break;
        }
        default:
            badarg(format);
        }
    }
    if (f.tag != ALKU_NIL || rest.tag != ALKU_NIL) badarg(args);
    buf_flush(&b, stdout);
    return alku_atom("ok");
}

Value alku_io_format_1(Value format) {
    return alku_io_format_2(format, alku_nil());
}

/* ---------- Calls, tail calls and program start ---------- */

/* The one pending tail call. Erlang functions have at most 255 arguments. */
static struct {
    AlkuCode code;
    AlkuFun *self;
    Value args[256];
} pending;

static Value pending_marker(void) {
    Value v;
    v.tag = ALKU_PENDING;
    v.as.integer = 0;
    return v;
}

Value alku_tail_call(AlkuCode code, AlkuFun *self, size_t arity, const Value *args) {
    pending.code = code;
    pending.self = self;
    for (size_t i = 0; i < arity; i++) pending.args[i] = args[i];
    return pending_marker();
}

/* Compiled functions copy their arguments before doing anything else, so
 * the next tail call may safely reuse pending.args. */
Value alku_run_pending(void) {
    return pending.code(pending.args, pending.self);
}

static AlkuFun *checked_fun(Value fun, size_t arity) {
    if (fun.tag != ALKU_FUN) badfun(fun);
    if (fun.as.fun->arity != arity) badarity(fun, arity);
    return fun.as.fun;
}

Value alku_apply(Value fun, size_t arity, const Value *args) {
    AlkuFun *f = checked_fun(fun, arity);
    return f->code(args, f);
}

Value alku_tail_apply(Value fun, size_t arity, const Value *args) {
    AlkuFun *f = checked_fun(fun, arity);
    return alku_tail_call(f->code, f, arity, args);
}

/* Programs run on a thread with a big stack, so deep (non-tail) recursion
 * over long lists works as it does in Erlang. Only the pages actually used
 * take memory. */
#define STACK_SIZE ((size_t)512 * 1024 * 1024)

static void *run_main(void *main) {
    gc_init(__builtin_frame_address(0));
    gc_add_root(&pending, sizeof pending);
    (void)alku_settle(((AlkuCode)(uintptr_t)main)(NULL, NULL));
    fflush(stdout);
    return NULL;
}

int alku_start(AlkuCode main) {
    pthread_attr_t attr;
    pthread_t thread;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, STACK_SIZE);
    if (pthread_create(&thread, &attr, run_main, (void *)(uintptr_t)main) != 0) {
        fputs("** could not start the program thread\n", stderr);
        return 1;
    }
    pthread_join(thread, NULL);
    return 0;
}
