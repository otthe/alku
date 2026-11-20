/* POSIX: clock_gettime, nanosleep, getpid, pause */
#define _POSIX_C_SOURCE 200809L

#include "alku_internal.h"
#include "alku_gc.h"

#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

_Noreturn void out_of_memory(void) {
    fputs("** out of memory\n", stderr);
    exit(1);
}

/* ---------- Output buffer ---------- */

/* Text is formatted into a buffer first and written in one go, so a
 * failing io:format prints nothing, as in Erlang. */
void buf_putc(Buffer *b, char c) {
    if (b->length == b->capacity) {
        b->capacity = b->capacity ? 2 * b->capacity : 64;
        b->data = realloc(b->data, b->capacity);
        if (b->data == NULL) out_of_memory();
    }
    b->data[b->length++] = c;
}

void buf_puts(Buffer *b, const char *s) {
    for (; *s != '\0'; s++) buf_putc(b, *s);
}

void buf_int(Buffer *b, int64_t n) {
    char digits[24];
    snprintf(digits, sizeof digits, "%" PRId64, n);
    buf_puts(b, digits);
}

bool is_char_code(int64_t c) {
    return c >= 0 && c <= 0x10FFFF;
}

void buf_utf8(Buffer *b, int64_t c) {
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

void buf_flush(Buffer *b, FILE *out) {
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
void print_value(Buffer *b, Value v, bool pretty) {
    switch (v.tag) {
    case ALKU_INT:
    case ALKU_BIG:
    case ALKU_FLOAT:
        print_number(b, v);
        break;
    case ALKU_ATOM:
        print_atom(b, v.as.atom);
        break;
    case ALKU_MAP:
        print_map(b, v, pretty);
        break;
    case ALKU_BINARY:
        print_binary(b, v, pretty);
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
    case ALKU_PID: /* an open file */
        buf_puts(b, "<0.");
        buf_int(b, 80 + v.as.integer);
        buf_puts(b, ".0>");
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

static char *c_string(Value chars);

/* ---------- Exceptions ---------- */

/* try/catch pushes a handler (a jmp_buf on the C stack); raising an
 * exception jumps to the innermost one. The caught class and reason are
 * kept in globals, which stay valid across longjmp. */
static AlkuHandler *handlers;
static Value caught_class, caught_reason, caught_stack;

AlkuFrame *alku_frames;

/* Erlang's stack trace format, innermost call first, at most 8 calls:
 * [{Module, Function, Arity, [{file, File}, {line, Line}]}, ...] */
static Value stack_trace(void) {
    AlkuFrame *frames[8];
    size_t n = 0;
    for (AlkuFrame *f = alku_frames; f != NULL && n < 8; f = f->prev) frames[n++] = f;
    Value list = alku_nil();
    while (n > 0) {
        AlkuFrame *f = frames[--n];
        Value location = alku_cons(pair("file", alku_string(f->info->file, strlen(f->info->file))),
                                   alku_cons(pair("line", alku_int(f->line)), alku_nil()));
        Value entry = alku_make_tuple(4, (Value[]){alku_atom(f->info->module),
                                                   alku_atom(f->info->function),
                                                   alku_int(f->info->arity), location});
        list = alku_cons(entry, list);
    }
    return list;
}

/* Extra details for the message printed when an error is not caught. */
static struct {
    const char *function; /* function_clause: the function's name */
    Value args;           /* ... and its arguments, as a list */
    bool has_detail;      /* badarg: the offending value */
    Value detail;
} error_info;

void alku_try(AlkuHandler *h) {
    h->prev = handlers;
    h->frames = alku_frames;
    handlers = h;
}

void alku_end_try(AlkuHandler *h) {
    handlers = h->prev;
}

Value alku_caught_class(void) { return caught_class; }
Value alku_caught_reason(void) { return caught_reason; }
Value alku_caught_stack(void) { return caught_stack; }

static _Noreturn void uncaught(Value class, Value reason);

static _Noreturn void raise_keeping_info(Value class, Value reason) {
    caught_class = class;
    caught_reason = reason;
    caught_stack = stack_trace();
    AlkuHandler *h = handlers;
    if (h == NULL) uncaught(class, reason);
    handlers = h->prev;
    alku_frames = h->frames; /* back to the frames of the function with the try */
    longjmp(h->env, 1);
}

_Noreturn void alku_raise(Value class, Value reason) {
    error_info.function = NULL;
    error_info.has_detail = false;
    raise_keeping_info(class, reason);
}

_Noreturn void raise_error(Value reason) {
    alku_raise(alku_atom("error"), reason);
}

Value pair(const char *tag, Value v) {
    return alku_make_tuple(2, (Value[]){alku_atom(tag), v});
}

/* catch Expr: the value an exception turns into. */
Value alku_catch_value(Value class, Value reason) {
    if (is_atom_named(class, "throw")) return reason;
    if (is_atom_named(class, "exit")) return pair("EXIT", reason);
    return pair("EXIT", alku_make_tuple(2, (Value[]){reason, caught_stack}));
}

_Noreturn void badarith(void) {
    raise_error(alku_atom("badarith"));
}

_Noreturn void system_limit(void) {
    raise_error(alku_atom("system_limit"));
}

_Noreturn void badarg(Value v) {
    error_info.function = NULL;
    error_info.has_detail = true;
    error_info.detail = v;
    raise_keeping_info(alku_atom("error"), alku_atom("badarg"));
}

Value array_to_list(size_t n, const Value *items) {
    Value list = alku_nil();
    while (n > 0) {
        n--;
        list = alku_cons(items[n], list);
    }
    return list;
}

_Noreturn void alku_error_function_clause(const char *name, int arity, const Value *args) {
    error_info.args = array_to_list((size_t)arity, args);
    error_info.function = name;
    error_info.has_detail = false;
    raise_keeping_info(alku_atom("error"), alku_atom("function_clause"));
}

static _Noreturn void badfun(Value v) {
    raise_error(pair("badfun", v));
}

static _Noreturn void badarity(Value fun, size_t arity, const Value *args) {
    Value inner = alku_make_tuple(2, (Value[]){fun, array_to_list(arity, args)});
    raise_error(pair("badarity", inner));
}

_Noreturn void alku_error_badmatch(Value v) { raise_error(pair("badmatch", v)); }
_Noreturn void alku_error_case_clause(Value v) { raise_error(pair("case_clause", v)); }
_Noreturn void alku_error_try_clause(Value v) { raise_error(pair("try_clause", v)); }
_Noreturn void alku_error_if_clause(void) { raise_error(alku_atom("if_clause")); }

/* {Tag, X} with the given tag? Then *x is X. */
static bool tagged(Value v, const char *tag, Value *x) {
    if (v.tag != ALKU_TUPLE || v.as.tuple->arity != 2) return false;
    if (!is_atom_named(v.as.tuple->elements[0], tag)) return false;
    *x = v.as.tuple->elements[1];
    return true;
}

/* The same wording as Erlang's shell uses for common errors. */
static void describe_error(Buffer *b, Value reason) {
    Value x;
    if (is_atom_named(reason, "badarith")) {
        buf_puts(b, "an error occurred when evaluating an arithmetic expression");
    } else if (is_atom_named(reason, "system_limit")) {
        buf_puts(b, "a system limit has been reached");
    } else if (is_atom_named(reason, "if_clause")) {
        buf_puts(b, "no true branch found when evaluating an if expression");
    } else if (is_atom_named(reason, "function_clause") && error_info.function != NULL) {
        buf_puts(b, "no function clause matching ");
        print_atom(b, error_info.function);
        buf_putc(b, '(');
        for (Value a = error_info.args; a.tag == ALKU_CONS; a = a.as.cons->tail) {
            if (a.as.cons != error_info.args.as.cons) buf_puts(b, ", ");
            print_value(b, a.as.cons->head, true);
        }
        buf_putc(b, ')');
    } else if (is_atom_named(reason, "badarg")) {
        buf_puts(b, "bad argument");
        if (error_info.has_detail) {
            buf_puts(b, ": ");
            print_value(b, error_info.detail, true);
        }
    } else if (tagged(reason, "badmatch", &x)) {
        buf_puts(b, "no match of right hand side value ");
        print_value(b, x, true);
    } else if (tagged(reason, "case_clause", &x)) {
        buf_puts(b, "no case clause matching ");
        print_value(b, x, true);
    } else if (tagged(reason, "try_clause", &x)) {
        buf_puts(b, "no try clause matching ");
        print_value(b, x, true);
    } else if (tagged(reason, "badkey", &x)) {
        buf_puts(b, "bad key: ");
        print_value(b, x, true);
    } else if (tagged(reason, "badmap", &x)) {
        buf_puts(b, "bad map: ");
        print_value(b, x, true);
    } else if (tagged(reason, "badrecord", &x)) {
        buf_puts(b, "bad record ");
        print_value(b, x, true);
    } else if (tagged(reason, "badfun", &x)) {
        buf_puts(b, "bad function ");
        print_value(b, x, true);
    } else if (tagged(reason, "badarity", &x) && x.tag == ALKU_TUPLE && x.as.tuple->arity == 2) {
        int64_t n = 0;
        for (Value a = x.as.tuple->elements[1]; a.tag == ALKU_CONS; a = a.as.cons->tail) n++;
        print_value(b, x.as.tuple->elements[0], true);
        buf_puts(b, " called with ");
        buf_int(b, n);
        buf_puts(b, n == 1 ? " argument" : " arguments");
    } else {
        print_value(b, reason, true);
    }
}

/* Where it happened, as Erlang's shell shows it:
 *      in function  shapes:area/1 (shapes.erl, line 4)
 *      in call from main:main/0 (main.erl, line 9) */
static void describe_stack(Buffer *b) {
    bool first = true;
    for (Value s = caught_stack; s.tag == ALKU_CONS; s = s.as.cons->tail) {
        const Value *entry = s.as.cons->head.as.tuple->elements;
        const Value *location = entry[3].as.cons->head.as.tuple->elements;
        const Value *line = entry[3].as.cons->tail.as.cons->head.as.tuple->elements;
        buf_puts(b, first ? "\n     in function  " : "\n     in call from ");
        first = false;
        print_atom(b, entry[0].as.atom);
        buf_putc(b, ':');
        print_atom(b, entry[1].as.atom);
        buf_putc(b, '/');
        buf_int(b, entry[2].as.integer);
        buf_puts(b, " (");
        print_chars(b, location[1], location[1]);
        buf_puts(b, ", line ");
        buf_int(b, line[1].as.integer);
        buf_putc(b, ')');
    }
}

static _Noreturn void uncaught(Value class, Value reason) {
    Buffer b = {0};
    buf_puts(&b, "** exception ");
    buf_puts(&b, class.as.atom);
    buf_puts(&b, ": ");
    if (is_atom_named(class, "error")) {
        describe_error(&b, reason);
    } else {
        print_value(&b, reason, true);
    }
    describe_stack(&b);
    buf_putc(&b, '\n');
    fflush(stdout); /* keep the program's earlier output before the error */
    buf_flush(&b, stderr);
    exit(1);
}

Value alku_bif_throw(Value reason) { alku_raise(alku_atom("throw"), reason); }
Value alku_bif_error_1(Value reason) { raise_error(reason); }
Value alku_bif_error_2(Value reason, Value args) { (void)args; raise_error(reason); }
Value alku_bif_exit(Value reason) { alku_raise(alku_atom("exit"), reason); }

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
Value utf8_to_list(const unsigned char *s, size_t length) {
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

bool is_atom_named(Value v, const char *name) {
    return v.tag == ALKU_ATOM && strcmp(v.as.atom, name) == 0;
}

static int sign(int x) {
    return (x > 0) - (x < 0);
}

/* Position in Erlang's term order; all numbers share one class. */
static int term_class(AlkuTag tag) {
    switch (tag) {
    case ALKU_INT:
    case ALKU_BIG:
    case ALKU_FLOAT: return 0;
    case ALKU_ATOM: return 1;
    case ALKU_FUN: return 2;
    case ALKU_PID: return 3;
    case ALKU_TUPLE: return 4;
    case ALKU_MAP: return 5;
    case ALKU_NIL: return 6;
    case ALKU_CONS: return 7;
    case ALKU_BINARY: return 8;
    case ALKU_PENDING: return 9;
    }
    return 9;
}

int compare_terms(Value a, Value b, bool exact) {
    for (;;) {
        int ca = term_class(a.tag), cb = term_class(b.tag);
        if (ca != cb) return ca < cb ? -1 : 1;
        switch (a.tag) {
        case ALKU_INT:
        case ALKU_BIG:
        case ALKU_FLOAT: {
            int c = compare_numbers(a, b);
            /* =:= tells 1 and 1.0 apart; integers then come first */
            if (c == 0 && exact && (a.tag == ALKU_FLOAT) != (b.tag == ALKU_FLOAT)) {
                return a.tag == ALKU_FLOAT ? 1 : -1;
            }
            return c;
        }
        case ALKU_ATOM:
            return sign(strcmp(a.as.atom, b.as.atom));
        case ALKU_PID:
            return (a.as.integer > b.as.integer) - (a.as.integer < b.as.integer);
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
                int c = compare_terms(f->env[i], g->env[i], exact);
                if (c != 0) return c;
            }
            return 0;
        }
        case ALKU_TUPLE: {
            size_t n = a.as.tuple->arity, m = b.as.tuple->arity;
            if (n != m) return n < m ? -1 : 1; /* smaller tuples come first */
            for (size_t i = 0; i < n; i++) {
                int c = compare_terms(a.as.tuple->elements[i], b.as.tuple->elements[i], exact);
                if (c != 0) return c;
            }
            return 0;
        }
        case ALKU_MAP:
            return compare_maps(a, b, exact);
        case ALKU_BINARY:
            return compare_binaries(a, b);
        case ALKU_CONS: {
            int c = compare_terms(a.as.cons->head, b.as.cons->head, exact);
            if (c != 0) return c;
            a = a.as.cons->tail; /* loop instead of recursing on long lists */
            b = b.as.cons->tail;
            break;
        }
        }
    }
}

bool alku_exact_eq(Value a, Value b) {
    return compare_terms(a, b, true) == 0;
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

/* ---------- Comparison ---------- */

/* == and < compare numbers by value (1 == 1.0); =:= does not. */
Value alku_op_eq(Value a, Value b) { return boolean(compare_terms(a, b, false) == 0); }
Value alku_op_ne(Value a, Value b) { return boolean(compare_terms(a, b, false) != 0); }
Value alku_op_exact_eq(Value a, Value b) { return boolean(compare_terms(a, b, true) == 0); }
Value alku_op_exact_ne(Value a, Value b) { return boolean(compare_terms(a, b, true) != 0); }
Value alku_op_lt(Value a, Value b) { return boolean(compare_terms(a, b, false) < 0); }
Value alku_op_gt(Value a, Value b) { return boolean(compare_terms(a, b, false) > 0); }
Value alku_op_le(Value a, Value b) { return boolean(compare_terms(a, b, false) <= 0); }
Value alku_op_ge(Value a, Value b) { return boolean(compare_terms(a, b, false) >= 0); }

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
            if (!removed[i] && compare_terms(items[i], v.as.cons->head, true) == 0) {
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

Value alku_bif_is_integer(Value v) { return boolean(is_integer_value(v)); }
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

Value alku_bif_atom_to_list(Value atom) {
    if (atom.tag != ALKU_ATOM) badarg(atom);
    return utf8_to_list((const unsigned char *)atom.as.atom, strlen(atom.as.atom));
}

/* Atoms are never freed, as in Erlang. */
Value alku_bif_list_to_atom(Value chars) {
    return alku_atom(c_string(chars));
}

Value alku_bif_tuple_to_list(Value tuple) {
    if (tuple.tag != ALKU_TUPLE) badarg(tuple);
    return array_to_list(tuple.as.tuple->arity, tuple.as.tuple->elements);
}

Value alku_bif_list_to_tuple(Value list) {
    size_t n = proper_length(list);
    Value tuple = alku_make_tuple(n, NULL);
    size_t i = 0;
    for (Value v = list; v.tag == ALKU_CONS; v = v.as.cons->tail) {
        tuple.as.tuple->elements[i++] = v.as.cons->head;
    }
    return tuple;
}

/* If the terms are equal, the first one is returned, as in Erlang. */
Value alku_bif_max(Value a, Value b) { return compare_terms(a, b, false) >= 0 ? a : b; }
Value alku_bif_min(Value a, Value b) { return compare_terms(a, b, false) <= 0 ? a : b; }

/* is_record(R, Name, Size): what records compile to (erl_expand_records). */
Value alku_bif_is_record_3(Value v, Value name, Value size) {
    return boolean(v.tag == ALKU_TUPLE && size.tag == ALKU_INT &&
                   v.as.tuple->arity == (uint64_t)size.as.integer && v.as.tuple->arity > 0 &&
                   compare_terms(v.as.tuple->elements[0], name, true) == 0);
}

Value alku_bif_is_record_2(Value v, Value name) {
    if (name.tag != ALKU_ATOM) badarg(name);
    return boolean(v.tag == ALKU_TUPLE && v.as.tuple->arity > 0 &&
                   compare_terms(v.as.tuple->elements[0], name, true) == 0);
}

Value alku_bif_apply_2(Value fun, Value args) {
    size_t n = proper_length(args);
    Value array = alku_make_tuple(n, NULL); /* a tuple keeps the values visible to the GC */
    size_t i = 0;
    for (Value v = args; v.tag == ALKU_CONS; v = v.as.cons->tail) {
        array.as.tuple->elements[i++] = v.as.cons->head;
    }
    return alku_settle(alku_apply(fun, n, array.as.tuple->elements));
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

/* ---------- Devices ---------- */

/* Open files are numbered and shown as pids, like Erlang's file:open/2
 * returns. standard_io / user / standard_error are atoms. */
#define MAX_FILES 256
static FILE *files[MAX_FILES];

static Value device_value(int64_t index) {
    Value v;
    v.tag = ALKU_PID;
    v.as.integer = index;
    return v;
}

static FILE *output_device(Value dev) {
    if (is_atom_named(dev, "standard_io") || is_atom_named(dev, "user")) return stdout;
    if (is_atom_named(dev, "standard_error")) return stderr;
    if (dev.tag == ALKU_PID && dev.as.integer >= 0 && dev.as.integer < MAX_FILES &&
        files[dev.as.integer] != NULL) {
        return files[dev.as.integer];
    }
    badarg(dev);
}

static FILE *input_device(Value dev) {
    if (is_atom_named(dev, "standard_io") || is_atom_named(dev, "user")) return stdin;
    return output_device(dev);
}

/* The Erlang name for an errno value, as in {error, enoent}. */
static Value errno_atom(int e) {
    switch (e) {
    case ENOENT: return alku_atom("enoent");
    case EACCES: return alku_atom("eacces");
    case EISDIR: return alku_atom("eisdir");
    case ENOTDIR: return alku_atom("enotdir");
    case EEXIST: return alku_atom("eexist");
    case ENOSPC: return alku_atom("enospc");
    case EROFS: return alku_atom("erofs");
    case EMFILE: return alku_atom("emfile");
    default: return alku_atom("eio");
    }
}

static Value error_tuple(int e) {
    return pair("error", errno_atom(e));
}

/* ---------- io ---------- */

/* Text for ~s and io:put_chars: an atom, or a (possibly nested) list of
 * character codes and binaries (whose bytes are taken as UTF-8). */
void print_chars(Buffer *b, Value v, Value arg) {
    if (v.tag == ALKU_ATOM) {
        buf_puts(b, v.as.atom);
        return;
    }
    if (v.tag == ALKU_BINARY) {
        append_binary(b, v);
        return;
    }
    for (; v.tag == ALKU_CONS; v = v.as.cons->tail) {
        Value c = v.as.cons->head;
        if (c.tag == ALKU_INT && is_char_code(c.as.integer)) {
            buf_utf8(b, c.as.integer);
        } else if (c.tag == ALKU_CONS || c.tag == ALKU_NIL || c.tag == ALKU_BINARY) {
            print_chars(b, c, arg);
        } else {
            badarg(arg);
        }
    }
    if (v.tag != ALKU_NIL && v.tag != ALKU_BINARY) badarg(arg);
    if (v.tag == ALKU_BINARY) append_binary(b, v); /* [... | <<"tail">>] */
}

/* iodata (for files and list_to_binary): bytes, binaries, nested lists. */
void print_iodata(Buffer *b, Value v, Value arg) {
    if (v.tag == ALKU_BINARY) {
        append_binary(b, v);
        return;
    }
    for (; v.tag == ALKU_CONS; v = v.as.cons->tail) {
        Value c = v.as.cons->head;
        if (c.tag == ALKU_INT && c.as.integer >= 0 && c.as.integer <= 255) {
            buf_putc(b, (char)c.as.integer);
        } else if (c.tag == ALKU_CONS || c.tag == ALKU_NIL || c.tag == ALKU_BINARY) {
            print_iodata(b, c, arg);
        } else {
            badarg(arg);
        }
    }
    if (v.tag == ALKU_BINARY) append_binary(b, v);
    else if (v.tag != ALKU_NIL) badarg(arg);
}

/* A NUL-terminated UTF-8 copy of a string (for file names). Free it. */
static char *c_string(Value chars) {
    Buffer b = {0};
    print_chars(&b, chars, chars);
    buf_putc(&b, '\0');
    return b.data;
}

static Value next_arg(Value *args, Value all) {
    if (args->tag != ALKU_CONS) badarg(all);
    Value arg = args->as.cons->head;
    *args = args->as.cons->tail;
    return arg;
}

static size_t utf8_length(const char *s, size_t n) {
    size_t count = 0;
    for (size_t i = 0; i < n; i++) {
        if (((unsigned char)s[i] & 0xC0) != 0x80) count++;
    }
    return count;
}

/* Keeps the first max characters of a UTF-8 buffer. */
static void utf8_truncate(Buffer *b, size_t max) {
    size_t count = 0;
    for (size_t i = 0; i < b->length; i++) {
        if (((unsigned char)b->data[i] & 0xC0) != 0x80 && count++ == max) {
            b->length = i;
            return;
        }
    }
}

/* The next character of a format string. */
static int64_t next_format_char(Value *f, Value format) {
    if (f->tag != ALKU_CONS || f->as.cons->head.tag != ALKU_INT) badarg(format);
    int64_t c = f->as.cons->head.as.integer;
    *f = f->as.cons->tail;
    return c;
}

static int64_t small_int_arg(Value v) {
    if (v.tag != ALKU_INT) badarg(v);
    return v.as.integer;
}

static double float_arg(Value v) {
    if (v.tag != ALKU_FLOAT) badarg(v); /* ~f and ~e need floats, as in Erlang */
    return v.as.flt;
}

/* A field width or precision: digits, or * to take it from the arguments. */
static int64_t format_number(int64_t *c, Value *f, Value format, Value *rest, Value args) {
    int64_t n = -1;
    if (*c == '*') {
        n = small_int_arg(next_arg(rest, args));
        *c = next_format_char(f, format);
    } else if (*c >= '0' && *c <= '9') {
        n = 0;
        while (*c >= '0' && *c <= '9') {
            if (n < 100000) n = n * 10 + (*c - '0');
            *c = next_format_char(f, format);
        }
    }
    return n;
}

/* Erlang control sequences: ~F.P.PadC with C one of
 * w p s c b B f e i n ~. F is the field width (right-justified; -F or a
 * negative * value left-justifies), P the precision, Pad the fill
 * character. */
static void format(Buffer *b, Value format, Value args) {
    Value f = format, rest = args;
    while (f.tag == ALKU_CONS) {
        int64_t c = next_format_char(&f, format);
        if (!is_char_code(c)) badarg(format);
        if (c != '~') {
            buf_utf8(b, c);
            continue;
        }
        c = next_format_char(&f, format);
        bool left = false;
        if (c == '-') {
            left = true;
            c = next_format_char(&f, format);
        }
        int64_t width = format_number(&c, &f, format, &rest, args);
        if (width < -1) {
            left = true;
            width = -width;
        }
        int64_t precision = -1, pad = ' ';
        if (c == '.') {
            c = next_format_char(&f, format);
            precision = format_number(&c, &f, format, &rest, args);
            if (c == '.') {
                pad = next_format_char(&f, format);
                c = next_format_char(&f, format);
            }
        }
        while (c == 't' || c == 'l' || c == 'k') c = next_format_char(&f, format); /* modifiers */
        if (c == 'n') {
            buf_putc(b, '\n');
            continue;
        }
        if (c == '~') {
            buf_putc(b, '~');
            continue;
        }
        Value arg = next_arg(&rest, args);
        Buffer item = {0};
        bool truncates = false;
        switch (c) {
        case 'w': print_value(&item, arg, false); break;
        case 'p': print_value(&item, arg, true); break;
        case 'i': continue; /* ignores its argument */
        case 's':
            print_chars(&item, arg, arg);
            truncates = true;
            break;
        case 'c': {
            if (arg.tag != ALKU_INT || !is_char_code(arg.as.integer)) badarg(arg);
            int64_t times = precision >= 0 ? precision : width >= 0 ? width : 1;
            for (int64_t i = 0; i < times; i++) buf_utf8(&item, arg.as.integer);
            break;
        }
        case 'b':
        case 'B': {
            int64_t base = precision >= 0 ? precision : 10;
            if (!is_integer_value(arg)) badarg(arg);
            if (base < 2 || base > 36) badarg(format);
            print_integer(&item, arg, (unsigned)base, c == 'b');
            break;
        }
        case 'f': print_float_fixed(&item, float_arg(arg), precision >= 0 ? (int)precision : 6); break;
        case 'e': print_float_scientific(&item, float_arg(arg), precision >= 0 ? (int)precision : 6); break;
        default:
            badarg(format);
        }
        size_t length = utf8_length(item.data, item.length);
        if (truncates && (precision >= 0 || width >= 0)) {
            size_t max = (size_t)(precision >= 0 ? precision : width);
            if (length > max) {
                utf8_truncate(&item, max);
                length = max;
            }
        }
        if (width >= 0 && length > (size_t)width) { /* too long: Erlang fills with * */
            item.length = 0;
            for (int64_t i = 0; i < width; i++) buf_putc(&item, '*');
            length = (size_t)width;
        }
        size_t padding = width >= 0 ? (size_t)width - length : 0;
        if (!left) for (size_t i = 0; i < padding; i++) buf_utf8(b, pad);
        for (size_t i = 0; i < item.length; i++) buf_putc(b, item.data[i]);
        if (left) for (size_t i = 0; i < padding; i++) buf_utf8(b, pad);
        free(item.data);
    }
    if (f.tag != ALKU_NIL || rest.tag != ALKU_NIL) badarg(args);
}

/* Output is formatted completely before anything is written, so a failing
 * call prints nothing, as in Erlang. */
static Value write_buffer(Buffer *b, Value dev) {
    FILE *out = output_device(dev);
    buf_flush(b, out);
    return alku_atom("ok");
}

Value alku_io_format_3(Value dev, Value fmt, Value args) {
    Buffer b = {0};
    format(&b, fmt, args);
    return write_buffer(&b, dev);
}

Value alku_io_format_2(Value fmt, Value args) {
    return alku_io_format_3(alku_atom("standard_io"), fmt, args);
}

Value alku_io_format_1(Value fmt) {
    return alku_io_format_3(alku_atom("standard_io"), fmt, alku_nil());
}

/* io_lib:format: the formatted text as a string instead of printing it. */
Value alku_io_lib_format(Value fmt, Value args) {
    Buffer b = {0};
    format(&b, fmt, args);
    Value result = utf8_to_list((const unsigned char *)b.data, b.length);
    free(b.data);
    return result;
}

Value alku_io_write_2(Value dev, Value v) {
    Buffer b = {0};
    print_value(&b, v, false);
    return write_buffer(&b, dev);
}

Value alku_io_write(Value v) {
    return alku_io_write_2(alku_atom("standard_io"), v);
}

Value alku_io_put_chars_2(Value dev, Value chars) {
    Buffer b = {0};
    print_chars(&b, chars, chars);
    return write_buffer(&b, dev);
}

Value alku_io_put_chars_1(Value chars) {
    return alku_io_put_chars_2(alku_atom("standard_io"), chars);
}

Value alku_io_nl_1(Value dev) {
    return alku_io_put_chars_2(dev, alku_string("\n", 1));
}

Value alku_io_nl(void) {
    return alku_io_nl_1(alku_atom("standard_io"));
}

/* io:get_line: the next line including its "\n", or eof. */
Value alku_io_get_line_2(Value dev, Value prompt) {
    FILE *in = input_device(dev);
    if (in == stdin) {
        Buffer p = {0};
        print_chars(&p, prompt, prompt);
        buf_flush(&p, stdout);
        fflush(stdout);
    }
    Buffer line = {0};
    int c;
    while ((c = getc(in)) != EOF) {
        buf_putc(&line, (char)c);
        if (c == '\n') break;
    }
    if (line.length == 0) {
        free(line.data);
        return ferror(in) ? error_tuple(errno) : alku_atom("eof");
    }
    Value result = utf8_to_list((const unsigned char *)line.data, line.length);
    free(line.data);
    return result;
}

Value alku_io_get_line_1(Value prompt) {
    return alku_io_get_line_2(alku_atom("standard_io"), prompt);
}

/* ---------- file ---------- */

/* file:open(Name, Modes) with modes read, write and append. */
Value alku_file_open(Value name, Value modes) {
    bool read = false, write = false, append = false;
    Value m = modes;
    for (; m.tag == ALKU_CONS; m = m.as.cons->tail) {
        Value mode = m.as.cons->head;
        if (is_atom_named(mode, "read")) read = true;
        else if (is_atom_named(mode, "write")) write = true;
        else if (is_atom_named(mode, "append")) append = true;
        else badarg(mode); /* binary mode and others are not supported yet */
    }
    if (m.tag != ALKU_NIL) badarg(modes);
    const char *how = append ? (read ? "a+" : "a") : write ? (read ? "w+" : "w") : "r";
    int64_t slot = 0;
    while (slot < MAX_FILES && files[slot] != NULL) slot++;
    if (slot == MAX_FILES) return error_tuple(EMFILE);
    char *path = c_string(name);
    FILE *f = fopen(path, how);
    int e = errno;
    free(path);
    if (f == NULL) return error_tuple(e);
    files[slot] = f;
    return pair("ok", device_value(slot));
}

Value alku_file_close(Value dev) {
    if (dev.tag != ALKU_PID || dev.as.integer < 0 || dev.as.integer >= MAX_FILES ||
        files[dev.as.integer] == NULL) {
        badarg(dev);
    }
    fclose(files[dev.as.integer]);
    files[dev.as.integer] = NULL;
    return alku_atom("ok");
}

Value alku_file_write_file(Value name, Value data) {
    Buffer b = {0};
    print_iodata(&b, data, data);
    char *path = c_string(name);
    FILE *f = fopen(path, "w");
    int e = errno;
    free(path);
    if (f == NULL) {
        free(b.data);
        return error_tuple(e);
    }
    if (b.length > 0) fwrite(b.data, 1, b.length, f);
    free(b.data);
    bool failed = fclose(f) != 0;
    return failed ? error_tuple(errno) : alku_atom("ok");
}

/* file:read_file: the whole file as a binary. */
Value alku_file_read_file(Value name) {
    char *path = c_string(name);
    FILE *f = fopen(path, "rb");
    int e = errno;
    free(path);
    if (f == NULL) return error_tuple(e);
    Buffer b = {0};
    char chunk[65536];
    size_t n;
    while ((n = fread(chunk, 1, sizeof chunk, f)) > 0) {
        for (size_t i = 0; i < n; i++) buf_putc(&b, chunk[i]);
    }
    bool failed = ferror(f);
    e = errno;
    fclose(f);
    if (failed) {
        free(b.data);
        return error_tuple(e);
    }
    Value bin = make_binary(b.data, b.length);
    free(b.data);
    return pair("ok", bin);
}

/* ---------- Tuples, time, environment, random numbers ---------- */

Value alku_bif_make_tuple(Value n, Value init) {
    if (n.tag != ALKU_INT || n.as.integer < 0 || n.as.integer > (1 << 24)) badarg(n);
    Value t = alku_make_tuple((size_t)n.as.integer, NULL);
    for (int64_t i = 0; i < n.as.integer; i++) t.as.tuple->elements[i] = init;
    return t;
}

/* size/1: of a tuple or a binary */
Value alku_bif_size(Value v) {
    if (v.tag == ALKU_TUPLE) return alku_int((int64_t)v.as.tuple->arity);
    return alku_bif_byte_size(v);
}

static int64_t nanoseconds(clockid_t clock) {
    struct timespec ts;
    clock_gettime(clock, &ts);
    return (int64_t)ts.tv_sec * 1000000000 + ts.tv_nsec;
}

/* Erlang time units; native is nanoseconds here. */
static Value in_unit(int64_t ns, Value unit) {
    if (is_atom_named(unit, "second") || is_atom_named(unit, "seconds")) return alku_int(ns / 1000000000);
    if (is_atom_named(unit, "millisecond")) return alku_int(ns / 1000000);
    if (is_atom_named(unit, "microsecond")) return alku_int(ns / 1000);
    if (is_atom_named(unit, "nanosecond") || is_atom_named(unit, "native")) return alku_int(ns);
    badarg(unit);
}

Value alku_bif_system_time_1(Value unit) { return in_unit(nanoseconds(CLOCK_REALTIME), unit); }
Value alku_bif_system_time_0(void) { return alku_int(nanoseconds(CLOCK_REALTIME)); }
Value alku_bif_monotonic_time_1(Value unit) { return in_unit(nanoseconds(CLOCK_MONOTONIC), unit); }
Value alku_bif_monotonic_time_0(void) { return alku_int(nanoseconds(CLOCK_MONOTONIC)); }

/* os:timestamp(): {MegaSecs, Secs, MicroSecs} */
Value alku_os_timestamp(void) {
    int64_t us = nanoseconds(CLOCK_REALTIME) / 1000;
    return alku_make_tuple(3, (Value[]){alku_int(us / 1000000000000), alku_int(us / 1000000 % 1000000),
                                        alku_int(us % 1000000)});
}

Value alku_os_getenv_2(Value name, Value fallback) {
    char *key = c_string(name);
    const char *value = getenv(key);
    free(key);
    return value ? alku_string(value, strlen(value)) : fallback;
}

Value alku_os_getenv_1(Value name) {
    return alku_os_getenv_2(name, alku_atom("false"));
}

Value alku_timer_sleep(Value ms) {
    if (is_atom_named(ms, "infinity")) {
        for (;;) pause();
    }
    if (ms.tag != ALKU_INT || ms.as.integer < 0) badarg(ms);
    struct timespec ts = {(time_t)(ms.as.integer / 1000), (long)(ms.as.integer % 1000) * 1000000};
    while (nanosleep(&ts, &ts) != 0 && errno == EINTR) {
    }
    return alku_atom("ok");
}

/* rand: xoshiro256** seeded from the clock and the process id. */
static uint64_t rand_state[4];
static bool rand_seeded;

static uint64_t rotl(uint64_t x, int k) {
    return (x << k) | (x >> (64 - k));
}

static uint64_t splitmix(uint64_t *x) {
    uint64_t z = (*x += 0x9E3779B97F4A7C15u);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9u;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBu;
    return z ^ (z >> 31);
}

static uint64_t next_random(void) {
    if (!rand_seeded) {
        uint64_t seed = (uint64_t)nanoseconds(CLOCK_REALTIME) ^ ((uint64_t)getpid() << 32);
        for (int i = 0; i < 4; i++) rand_state[i] = splitmix(&seed);
        rand_seeded = true;
    }
    uint64_t *s = rand_state;
    uint64_t result = rotl(s[1] * 5, 7) * 9;
    uint64_t t = s[1] << 17;
    s[2] ^= s[0];
    s[3] ^= s[1];
    s[1] ^= s[2];
    s[0] ^= s[3];
    s[2] ^= t;
    s[3] = rotl(s[3], 45);
    return result;
}

/* A float in [0.0, 1.0) */
Value alku_rand_uniform_0(void) {
    return alku_float((double)(next_random() >> 11) * 0x1.0p-53);
}

/* An integer in 1..N */
Value alku_rand_uniform_1(Value n) {
    if (n.tag != ALKU_INT || n.as.integer < 1) badarg(n);
    uint64_t range = (uint64_t)n.as.integer;
    uint64_t limit = UINT64_MAX - UINT64_MAX % range; /* no modulo bias */
    uint64_t r;
    do r = next_random(); while (r >= limit);
    return alku_int((int64_t)(r % range) + 1);
}

/* ---------- halt ---------- */

Value alku_bif_halt_1(Value status) {
    if (status.tag != ALKU_INT || status.as.integer < 0 || status.as.integer > 255) badarg(status);
    fflush(stdout);
    exit((int)status.as.integer);
}

Value alku_bif_halt_0(void) {
    return alku_bif_halt_1(alku_int(0));
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

static AlkuFun *checked_fun(Value fun, size_t arity, const Value *args) {
    if (fun.tag != ALKU_FUN) badfun(fun);
    if (fun.as.fun->arity != arity) badarity(fun, arity, args);
    return fun.as.fun;
}

Value alku_apply(Value fun, size_t arity, const Value *args) {
    AlkuFun *f = checked_fun(fun, arity, args);
    return f->code(args, f);
}

Value alku_tail_apply(Value fun, size_t arity, const Value *args) {
    AlkuFun *f = checked_fun(fun, arity, args);
    return alku_tail_call(f->code, f, arity, args);
}

/* Programs run on a thread with a big stack, so deep (non-tail) recursion
 * over long lists works as it does in Erlang. Only the pages actually used
 * take memory. */
#define STACK_SIZE ((size_t)512 * 1024 * 1024)

static struct {
    AlkuCode main;
    int arity; /* main/0 or main/1 */
    int argc;
    char **argv;
} program;

static void *run_main(void *unused) {
    (void)unused;
    gc_init(__builtin_frame_address(0));
    gc_add_root(&pending, sizeof pending);
    gc_add_root(&caught_class, sizeof caught_class);
    gc_add_root(&caught_reason, sizeof caught_reason);
    gc_add_root(&caught_stack, sizeof caught_stack);
    gc_add_root(&error_info, sizeof error_info);
    if (program.arity == 1) { /* main(Args): the command-line arguments as strings */
        ListBuilder lb = list_builder();
        for (int i = 1; i < program.argc; i++) {
            list_push(&lb, alku_string(program.argv[i], strlen(program.argv[i])));
        }
        Value args = list_finish(&lb, alku_nil());
        (void)alku_settle(program.main(&args, NULL));
    } else {
        (void)alku_settle(program.main(NULL, NULL));
    }
    fflush(stdout);
    return NULL;
}

int alku_start(AlkuCode main, int arity, int argc, char **argv) {
    program.main = main;
    program.arity = arity;
    program.argc = argc;
    program.argv = argv;
    pthread_attr_t attr;
    pthread_t thread;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, STACK_SIZE);
    if (pthread_create(&thread, &attr, run_main, NULL) != 0) {
        fputs("** could not start the program thread\n", stderr);
        return 1;
    }
    pthread_join(thread, NULL);
    return 0;
}
