/*
 * term_to_binary/1,2, binary_to_term/1,2 and phash2/1,2.
 *
 * term_to_binary writes Erlang's external term format, the same bytes
 * OTP 26 writes (atoms as UTF-8), so files written by Alku can be read by
 * Erlang and the other way round. Maps are written in key order, which is
 * what Erlang does for maps of up to 32 keys.
 *
 * Not supported: funs other than fun M:F/A (their code exists only in
 * this program) and open files raise badarg; compressed input (it needs
 * zlib) raises badarg. The `compressed` option is accepted and ignored,
 * which still gives valid, uncompressed output.
 */
#include "alku_internal.h"

#include <stdlib.h>
#include <string.h>

enum {
    VERSION = 131,
    NEW_FLOAT_EXT = 70,
    BIT_BINARY_EXT = 77,
    COMPRESSED = 80,
    NEWER_REFERENCE_EXT = 90,
    SMALL_INTEGER_EXT = 97,
    INTEGER_EXT = 98,
    FLOAT_EXT = 99,
    ATOM_EXT = 100,
    SMALL_TUPLE_EXT = 104,
    LARGE_TUPLE_EXT = 105,
    NIL_EXT = 106,
    STRING_EXT = 107,
    LIST_EXT = 108,
    BINARY_EXT = 109,
    SMALL_BIG_EXT = 110,
    LARGE_BIG_EXT = 111,
    EXPORT_EXT = 113,
    NEW_REFERENCE_EXT = 114,
    SMALL_ATOM_EXT = 115,
    MAP_EXT = 116,
    ATOM_UTF8_EXT = 118,
    SMALL_ATOM_UTF8_EXT = 119
};

static Value intern_atom(const char *utf8, size_t n);

/* ---------- Encoding ---------- */

static void put8(Buffer *b, unsigned x) { buf_putc(b, (char)(x & 0xFF)); }
static void put16(Buffer *b, unsigned x) { put8(b, x >> 8); put8(b, x); }
static void put32(Buffer *b, uint32_t x) { put16(b, x >> 16); put16(b, x & 0xFFFF); }

static void put_bytes(Buffer *b, const void *p, size_t n) {
    for (size_t i = 0; i < n; i++) buf_putc(b, ((const char *)p)[i]);
}

static void put_atom(Buffer *b, const char *name) {
    size_t n = strlen(name);
    if (n <= 255) {
        put8(b, SMALL_ATOM_UTF8_EXT);
        put8(b, (unsigned)n);
    } else {
        put8(b, ATOM_UTF8_EXT);
        put16(b, (unsigned)n);
    }
    put_bytes(b, name, n);
}

/* A big integer from its magnitude as little-endian bytes. */
static void put_big(Buffer *b, bool negative, const unsigned char *le, size_t n) {
    while (n > 0 && le[n - 1] == 0) n--;
    if (n <= 255) {
        put8(b, SMALL_BIG_EXT);
        put8(b, (unsigned)n);
    } else {
        put8(b, LARGE_BIG_EXT);
        put32(b, (uint32_t)n);
    }
    put8(b, negative ? 1 : 0);
    put_bytes(b, le, n);
}

static void put_integer(Buffer *b, Value v) {
    if (v.tag == ALKU_INT) {
        int64_t n = v.as.integer;
        if (n >= 0 && n <= 255) {
            put8(b, SMALL_INTEGER_EXT);
            put8(b, (unsigned)n);
        } else if (n >= INT32_MIN && n <= INT32_MAX) {
            put8(b, INTEGER_EXT);
            put32(b, (uint32_t)(int32_t)n);
        } else {
            uint64_t magnitude = n < 0 ? (uint64_t)(-(n + 1)) + 1 : (uint64_t)n;
            unsigned char le[8];
            for (int i = 0; i < 8; i++) le[i] = (unsigned char)(magnitude >> (8 * i));
            put_big(b, n < 0, le, 8);
        }
        return;
    }
    const AlkuBig *big = v.as.big;
    unsigned char *le = malloc(big->length * 4);
    if (le == NULL) out_of_memory();
    for (size_t i = 0; i < big->length; i++) {
        for (int k = 0; k < 4; k++) le[4 * i + k] = (unsigned char)(big->limbs[i] >> (8 * k));
    }
    put_big(b, big->negative, le, big->length * 4);
    free(le);
}

/* "fun lists:map/2" names a static fun M:F/A; split it into M and F. */
static bool external_fun_name(const AlkuFun *f, Value *module, Value *function) {
    if (is_mfa_fun(f)) {
        *module = f->env[0];
        *function = f->env[1];
        return true;
    }
    if (strncmp(f->name, "fun ", 4) != 0) return false;
    const char *m = f->name + 4;
    const char *colon = strchr(m, ':');
    const char *slash = strrchr(m, '/');
    if (colon == NULL || slash == NULL || slash < colon) return false;
    *module = intern_atom(m, (size_t)(colon - m));
    *function = intern_atom(colon + 1, (size_t)(slash - colon - 1));
    return true;
}

static void put_map(Buffer *b, const AlkuMapNode *node, bool hashing);

/* With hashing (for phash2), terms that cannot be written are still
 * turned into bytes, as only equality of the bytes matters. */
static void encode(Buffer *b, Value v, bool hashing) {
    for (;;) {
        switch (v.tag) {
        case ALKU_INT:
        case ALKU_BIG:
            put_integer(b, v);
            return;
        case ALKU_FLOAT: {
            uint64_t u;
            memcpy(&u, &v.as.flt, 8);
            put8(b, NEW_FLOAT_EXT);
            put32(b, (uint32_t)(u >> 32));
            put32(b, (uint32_t)u);
            return;
        }
        case ALKU_ATOM:
            put_atom(b, v.as.atom);
            return;
        case ALKU_REF:
            put8(b, NEWER_REFERENCE_EXT);
            put16(b, 3);
            put_atom(b, "nonode@nohost");
            put32(b, 0);
            put32(b, (uint32_t)v.as.integer);
            put32(b, (uint32_t)((uint64_t)v.as.integer >> 32));
            put32(b, 0);
            return;
        case ALKU_FUN: {
            Value module, function;
            if (external_fun_name(v.as.fun, &module, &function)) {
                put8(b, EXPORT_EXT);
                put_atom(b, module.as.atom);
                put_atom(b, function.as.atom);
                put8(b, SMALL_INTEGER_EXT);
                put8(b, (unsigned)v.as.fun->arity);
                return;
            }
            if (!hashing) badarg(v);
            put8(b, 'F');
            put_bytes(b, v.as.fun->name, strlen(v.as.fun->name) + 1);
            put32(b, (uint32_t)v.as.fun->free_count);
            for (size_t i = 0; i < v.as.fun->free_count; i++) encode(b, v.as.fun->env[i], hashing);
            return;
        }
        case ALKU_PID:
        case ALKU_PENDING:
            if (!hashing) badarg(v);
            put8(b, 'P');
            put32(b, (uint32_t)v.as.integer);
            return;
        case ALKU_TUPLE: {
            size_t n = v.as.tuple->arity;
            if (n <= 255) {
                put8(b, SMALL_TUPLE_EXT);
                put8(b, (unsigned)n);
            } else {
                put8(b, LARGE_TUPLE_EXT);
                put32(b, (uint32_t)n);
            }
            if (n == 0) return;
            for (size_t i = 0; i + 1 < n; i++) encode(b, v.as.tuple->elements[i], hashing);
            v = v.as.tuple->elements[n - 1]; /* loop on the last element */
            break;
        }
        case ALKU_MAP:
            put8(b, MAP_EXT);
            put32(b, (uint32_t)(v.as.map ? v.as.map->size : 0));
            put_map(b, v.as.map, hashing);
            return;
        case ALKU_NIL:
            put8(b, NIL_EXT);
            return;
        case ALKU_CONS: {
            size_t n = 0;
            bool bytes = true;
            Value tail = v;
            for (; tail.tag == ALKU_CONS; tail = tail.as.cons->tail, n++) {
                Value h = tail.as.cons->head;
                bytes = bytes && h.tag == ALKU_INT && h.as.integer >= 0 && h.as.integer <= 255;
            }
            if (bytes && tail.tag == ALKU_NIL && n <= 65535) {
                put8(b, STRING_EXT);
                put16(b, (unsigned)n);
                for (Value c = v; c.tag == ALKU_CONS; c = c.as.cons->tail) put8(b, (unsigned)c.as.cons->head.as.integer);
                return;
            }
            put8(b, LIST_EXT);
            put32(b, (uint32_t)n);
            for (; v.tag == ALKU_CONS; v = v.as.cons->tail) encode(b, v.as.cons->head, hashing);
            break; /* then the tail: [] or what ends an improper list */
        }
        case ALKU_BINARY: {
            const AlkuBinary *bin = v.as.binary;
            if (bin->extra == 0) {
                put8(b, BINARY_EXT);
                put32(b, (uint32_t)bin->size);
                put_bytes(b, bin->bytes, bin->size);
            } else {
                put8(b, BIT_BINARY_EXT);
                put32(b, (uint32_t)(bin->size + 1));
                put8(b, bin->extra);
                put_bytes(b, bin->bytes, bin->size + 1);
            }
            return;
        }
        }
    }
}

static void put_map(Buffer *b, const AlkuMapNode *node, bool hashing) {
    for (; node != NULL; node = node->right) {
        put_map(b, node->left, hashing);
        encode(b, node->key, hashing);
        encode(b, node->value, hashing);
    }
}

static void check_term_options(Value options) {
    Value o = options;
    for (; o.tag == ALKU_CONS; o = o.as.cons->tail) {
        Value opt = o.as.cons->head;
        if (is_atom_named(opt, "compressed") || is_atom_named(opt, "deterministic") ||
            is_atom_named(opt, "local")) {
            continue;
        }
        if (opt.tag == ALKU_TUPLE && opt.as.tuple->arity == 2 &&
            opt.as.tuple->elements[1].tag == ALKU_INT &&
            (is_atom_named(opt.as.tuple->elements[0], "compressed") ||
             is_atom_named(opt.as.tuple->elements[0], "minor_version"))) {
            continue;
        }
        badarg(options);
    }
    if (o.tag != ALKU_NIL) badarg(options);
}

Value alku_bif_term_to_binary_2(Value term, Value options) {
    check_term_options(options);
    Buffer b = {0};
    put8(&b, VERSION);
    encode(&b, term, false);
    Value result = make_binary(b.data, b.length);
    free(b.data);
    return result;
}

Value alku_bif_term_to_binary_1(Value term) {
    return alku_bif_term_to_binary_2(term, alku_nil());
}

/* ---------- Decoding ---------- */

typedef struct {
    const unsigned char *p;
    size_t length, pos;
    Value arg; /* the binary, for badarg */
} Decoder;

static const unsigned char *take(Decoder *d, size_t n) {
    if (n > d->length - d->pos) badarg(d->arg);
    const unsigned char *at = d->p + d->pos;
    d->pos += n;
    return at;
}

static unsigned get8(Decoder *d) { return *take(d, 1); }
static unsigned get16(Decoder *d) { const unsigned char *s = take(d, 2); return (unsigned)s[0] << 8 | s[1]; }
static uint32_t get32(Decoder *d) {
    const unsigned char *s = take(d, 4);
    return (uint32_t)s[0] << 24 | (uint32_t)s[1] << 16 | (uint32_t)s[2] << 8 | s[3];
}

/* Decoded atoms are kept in a table, so reading the same atom again does
 * not use more memory. Atoms are never freed, as in Erlang. */
static char **atom_table;
static size_t atom_slots, atom_count;

static uint64_t text_hash(const char *s, size_t n) {
    uint64_t h = 1469598103934665603u;
    for (size_t i = 0; i < n; i++) h = (h ^ (unsigned char)s[i]) * 1099511628211u;
    return h;
}

static void atom_insert(char *name) {
    size_t slot = text_hash(name, strlen(name)) & (atom_slots - 1);
    while (atom_table[slot] != NULL) slot = (slot + 1) & (atom_slots - 1);
    atom_table[slot] = name;
}

static Value intern_atom(const char *utf8, size_t n) {
    if (2 * (atom_count + 1) > atom_slots) {
        char **old = atom_table;
        size_t old_slots = atom_slots;
        atom_slots = atom_slots ? 2 * atom_slots : 256;
        atom_table = calloc(atom_slots, sizeof *atom_table);
        if (atom_table == NULL) out_of_memory();
        for (size_t i = 0; i < old_slots; i++) {
            if (old[i] != NULL) atom_insert(old[i]);
        }
        free(old);
    }
    size_t slot = text_hash(utf8, n) & (atom_slots - 1);
    for (; atom_table[slot] != NULL; slot = (slot + 1) & (atom_slots - 1)) {
        if (strlen(atom_table[slot]) == n && memcmp(atom_table[slot], utf8, n) == 0) {
            return alku_atom(atom_table[slot]);
        }
    }
    char *name = malloc(n + 1);
    if (name == NULL) out_of_memory();
    memcpy(name, utf8, n);
    name[n] = '\0';
    atom_table[slot] = name;
    atom_count++;
    return alku_atom(name);
}

static Value get_atom_body(Decoder *d, unsigned tag) {
    size_t n = (tag == SMALL_ATOM_EXT || tag == SMALL_ATOM_UTF8_EXT) ? get8(d) : get16(d);
    const unsigned char *s = take(d, n);
    if (tag == ATOM_UTF8_EXT || tag == SMALL_ATOM_UTF8_EXT) return intern_atom((const char *)s, n);
    Buffer b = {0}; /* Latin-1 to UTF-8 */
    for (size_t i = 0; i < n; i++) buf_utf8(&b, s[i]);
    Value atom = intern_atom(b.data ? b.data : "", b.length);
    free(b.data);
    return atom;
}

static Value get_atom(Decoder *d) {
    unsigned tag = get8(d);
    if (tag != ATOM_EXT && tag != SMALL_ATOM_EXT && tag != ATOM_UTF8_EXT && tag != SMALL_ATOM_UTF8_EXT) {
        badarg(d->arg);
    }
    return get_atom_body(d, tag);
}

static Value get_big(Decoder *d, size_t n) {
    bool negative = get8(d) != 0;
    const unsigned char *le = take(d, n);
    if (n == 0) return alku_int(0);
    unsigned char *be = malloc(n);
    if (be == NULL) out_of_memory();
    for (size_t i = 0; i < n; i++) be[i] = le[n - 1 - i];
    Value magnitude = integer_from_be_bytes(be, n, false);
    free(be);
    return negative ? alku_op_neg(magnitude) : magnitude;
}

static Value decode(Decoder *d);

static Value get_ref(Decoder *d, bool newer) {
    size_t n = get16(d);
    (void)get_atom(d); /* node */
    (void)take(d, newer ? 4 : 1); /* creation */
    uint64_t id = 0;
    for (size_t i = 0; i < n; i++) {
        uint32_t part = get32(d);
        if (i < 2) id |= (uint64_t)part << (32 * i);
    }
    Value v;
    v.tag = ALKU_REF;
    v.as.integer = (int64_t)id;
    return v;
}

static Value decode(Decoder *d) {
    unsigned tag = get8(d);
    switch (tag) {
    case SMALL_INTEGER_EXT:
        return alku_int(get8(d));
    case INTEGER_EXT:
        return alku_int((int32_t)get32(d));
    case SMALL_BIG_EXT:
        return get_big(d, get8(d));
    case LARGE_BIG_EXT:
        return get_big(d, get32(d));
    case NEW_FLOAT_EXT: {
        uint64_t u = (uint64_t)get32(d) << 32;
        u |= get32(d);
        double f;
        memcpy(&f, &u, 8);
        if (f != f || f - f != 0) badarg(d->arg);
        return alku_float(f);
    }
    case FLOAT_EXT: { /* the old format: "%.20e" text in 31 bytes */
        char text[32];
        memcpy(text, take(d, 31), 31);
        text[31] = '\0';
        return alku_float(strtod(text, NULL));
    }
    case ATOM_EXT:
    case SMALL_ATOM_EXT:
    case ATOM_UTF8_EXT:
    case SMALL_ATOM_UTF8_EXT:
        return get_atom_body(d, tag);
    case SMALL_TUPLE_EXT:
    case LARGE_TUPLE_EXT: {
        size_t n = tag == SMALL_TUPLE_EXT ? get8(d) : get32(d);
        if (n > d->length - d->pos) badarg(d->arg); /* each element takes a byte at least */
        Value tuple = alku_make_tuple(n, NULL);
        for (size_t i = 0; i < n; i++) tuple.as.tuple->elements[i] = decode(d);
        return tuple;
    }
    case NIL_EXT:
        return alku_nil();
    case STRING_EXT: {
        size_t n = get16(d);
        const unsigned char *s = take(d, n);
        Value list = alku_nil();
        for (size_t i = n; i-- > 0;) list = alku_cons(alku_int(s[i]), list);
        return list;
    }
    case LIST_EXT: {
        size_t n = get32(d);
        if (n > d->length - d->pos) badarg(d->arg);
        Value first = alku_nil();
        AlkuCons *last = NULL;
        for (size_t i = 0; i < n; i++) {
            Value cell = alku_cons(alku_nil(), alku_nil());
            if (last) last->tail = cell; else first = cell;
            last = cell.as.cons;
            last->head = decode(d);
        }
        Value tail = decode(d);
        if (last == NULL) return tail;
        last->tail = tail;
        return first;
    }
    case BINARY_EXT: {
        size_t n = get32(d);
        return make_binary(take(d, n), n);
    }
    case BIT_BINARY_EXT: {
        size_t n = get32(d);
        unsigned bits = get8(d);
        if (n == 0 ? bits != 0 : (bits < 1 || bits > 8)) badarg(d->arg);
        const unsigned char *s = take(d, n);
        return make_bitstring(s, n == 0 ? 0 : (n - 1) * 8 + bits);
    }
    case MAP_EXT: {
        size_t n = get32(d);
        Value map = alku_map_new();
        for (size_t i = 0; i < n; i++) {
            Value key = decode(d);
            Value value = decode(d);
            map = alku_map_put(map, key, value);
        }
        return map;
    }
    case EXPORT_EXT: {
        Value module = get_atom(d);
        Value function = get_atom(d);
        if (get8(d) != SMALL_INTEGER_EXT) badarg(d->arg);
        return alku_make_mfa_fun(module, function, alku_int(get8(d)));
    }
    case NEWER_REFERENCE_EXT:
        return get_ref(d, true);
    case NEW_REFERENCE_EXT:
        return get_ref(d, false);
    default: /* compressed terms, local funs, pids, ports */
        badarg(d->arg);
    }
}

static AlkuBinary *binary_input(Value bin) {
    if (bin.tag != ALKU_BINARY || bin.as.binary->extra != 0) badarg(bin);
    return bin.as.binary;
}

Value alku_bif_binary_to_term_2(Value bin, Value options) {
    AlkuBinary *b = binary_input(bin);
    bool used = false;
    Value o = options;
    for (; o.tag == ALKU_CONS; o = o.as.cons->tail) {
        Value opt = o.as.cons->head;
        if (is_atom_named(opt, "used")) used = true;
        else if (!is_atom_named(opt, "safe")) badarg(options);
    }
    if (o.tag != ALKU_NIL) badarg(options);
    Decoder d = {b->bytes, b->size, 0, bin};
    if (get8(&d) != VERSION) badarg(bin);
    Value term = decode(&d);
    if (!used) return term;
    return alku_make_tuple(2, (Value[]){term, alku_int((int64_t)d.pos)});
}

Value alku_bif_binary_to_term_1(Value bin) {
    return alku_bif_binary_to_term_2(bin, alku_nil());
}

/* ---------- phash2 ---------- */

/* A hash of the term in 0..Range-1 that is the same for equal (=:=)
 * terms, in every run of every Alku program. The numbers are not the same
 * as Erlang's phash2 gives. */
Value alku_bif_phash2_2(Value term, Value range) {
    if (range.tag != ALKU_INT || range.as.integer < 1 || range.as.integer > ((int64_t)1 << 32)) {
        badarg(range);
    }
    Buffer b = {0};
    encode(&b, term, true);
    uint64_t h = text_hash(b.data ? b.data : "", b.length);
    free(b.data);
    h ^= h >> 33; /* spread the bits (MurmurHash3's finaliser) */
    h *= 0xFF51AFD7ED558CCDu;
    h ^= h >> 33;
    h *= 0xC4CEB9FE1A85EC53u;
    h ^= h >> 33;
    return alku_int((int64_t)(h % (uint64_t)range.as.integer));
}

Value alku_bif_phash2_1(Value term) {
    return alku_bif_phash2_2(term, alku_int((int64_t)1 << 27));
}
