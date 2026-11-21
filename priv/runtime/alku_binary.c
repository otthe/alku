/*
 * Binaries, bitstrings and the bit syntax.
 *
 * Bits are numbered from the most significant bit of the first byte, as
 * in Erlang. A bitstring whose length is not a whole number of bytes
 * keeps its last few bits in the top of one extra byte.
 *
 * Matching <<X, Rest/binary>> does not copy: Rest is a small sub-binary
 * pointing into the original bytes. (Parts that do not start and end on a
 * byte boundary are copied.)
 */
#include "alku_internal.h"
#include "alku_gc.h"

#include <stdlib.h>
#include <string.h>

/* ---------- Binary objects ---------- */

static Value binary_value(AlkuBinary *b) {
    Value v;
    v.tag = ALKU_BINARY;
    v.as.binary = b;
    return v;
}

size_t bitstring_bits(const AlkuBinary *b) {
    return b->size * 8 + b->extra;
}

/* A new bitstring owning a copy of the first `bits` bits of data. */
Value make_bitstring(const void *data, size_t bits) {
    size_t n = (bits + 7) / 8;
    AlkuBinary *b = gc_alloc(GC_BINARY, sizeof *b + n);
    b->size = bits / 8;
    b->extra = (unsigned)(bits % 8);
    b->parent = NULL;
    if (n > 0) memcpy(b->data, data, n);
    if (b->extra) b->data[n - 1] &= (unsigned char)(0xFF << (8 - b->extra));
    b->bytes = b->data;
    return binary_value(b);
}

/* A new binary owning a copy of the bytes. */
Value make_binary(const void *bytes, size_t n) {
    return make_bitstring(bytes, n * 8);
}

/* Bytes [offset, offset + n) of an existing binary, without copying. */
static Value sub_binary(const AlkuBinary *of, size_t offset, size_t n) {
    AlkuBinary *owner = of->parent ? of->parent : (AlkuBinary *)of;
    AlkuBinary *b = gc_alloc(GC_BINARY, sizeof *b);
    b->size = n;
    b->extra = 0;
    b->bytes = of->bytes + offset;
    b->parent = owner;
    return binary_value(b);
}

static void put_bits(AlkuBinBuilder *b, const unsigned char *src, size_t from, size_t nbits);

/* Bits [pos, pos + nbits) of a bitstring: shared when they are whole
 * bytes, copied otherwise. */
static Value sub_bits(const AlkuBinary *of, size_t pos, size_t nbits) {
    if (pos % 8 == 0 && nbits % 8 == 0) return sub_binary(of, pos / 8, nbits / 8);
    AlkuBinBuilder copy;
    alku_bin_begin(&copy);
    put_bits(&copy, of->bytes, pos, nbits);
    return alku_bin_end(&copy);
}

/* A binary (whole bytes); bitstrings are a bad argument here. */
static AlkuBinary *binary_arg(Value v) {
    if (v.tag != ALKU_BINARY || v.as.binary->extra != 0) badarg(v);
    return v.as.binary;
}

static AlkuBinary *bitstring_arg(Value v) {
    if (v.tag != ALKU_BINARY) badarg(v);
    return v.as.binary;
}

/* The bytes of a binary, for iodata and text. */
void append_binary(Buffer *b, Value v) {
    const AlkuBinary *bin = binary_arg(v);
    for (size_t i = 0; i < bin->size; i++) buf_putc(b, (char)bin->bytes[i]);
}

static int get_bit(const unsigned char *data, size_t pos) {
    return (data[pos >> 3] >> (7 - (pos & 7))) & 1;
}

/* Bit by bit, as Erlang orders bitstrings; a prefix comes first. */
int compare_binaries(Value a, Value b) {
    const AlkuBinary *x = a.as.binary, *y = b.as.binary;
    size_t xbits = bitstring_bits(x), ybits = bitstring_bits(y);
    size_t common = xbits < ybits ? xbits : ybits;
    size_t whole = common / 8;
    int c = whole > 0 ? memcmp(x->bytes, y->bytes, whole) : 0;
    if (c != 0) return c < 0 ? -1 : 1;
    for (size_t i = whole * 8; i < common; i++) {
        int p = get_bit(x->bytes, i), q = get_bit(y->bytes, i);
        if (p != q) return p < q ? -1 : 1;
    }
    return (xbits > ybits) - (xbits < ybits);
}

static bool decode_utf8(const unsigned char *s, size_t n, int64_t *code, size_t *length);

static bool printable_code(int64_t c) {
    return (c >= 32 && c <= 126) || (c >= 8 && c <= 13) || c == 27 || (c >= 160 && c <= 255);
}

/* Text inside "...": chars, with the same escapes as strings. */
static void print_quoted(Buffer *b, int64_t c) {
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

/* Is the binary printable UTF-8 text with at least one non-ASCII char? */
static bool printable_utf8(const AlkuBinary *bin) {
    bool multibyte = false;
    for (size_t i = 0; i < bin->size;) {
        int64_t c;
        size_t length;
        if (!decode_utf8(bin->bytes + i, bin->size - i, &c, &length) || !printable_code(c)) return false;
        multibyte |= length > 1;
        i += length;
    }
    return multibyte;
}

/* ~p shows text: <<"abc">> (bytes as Latin-1) or <<"hä"/utf8>>; anything
 * else, and ~w, shows the bytes: <<1,2,3>>. A bitstring ends with its
 * last bits as Value:Size, like <<1,2,5:3>>. */
void print_binary(Buffer *b, Value v, bool pretty) {
    const AlkuBinary *bin = v.as.binary;
    buf_puts(b, "<<");
    bool utf8 = pretty && bin->size > 0 && printable_utf8(bin);
    bool latin1 = !utf8 && pretty && bin->size > 0;
    for (size_t i = 0; latin1 && i < bin->size; i++) latin1 = printable_code(bin->bytes[i]);
    if (utf8) {
        buf_putc(b, '"');
        for (size_t i = 0; i < bin->size;) {
            int64_t c;
            size_t length;
            decode_utf8(bin->bytes + i, bin->size - i, &c, &length);
            print_quoted(b, c);
            i += length;
        }
        buf_puts(b, "\"/utf8");
    } else if (latin1) {
        buf_putc(b, '"');
        for (size_t i = 0; i < bin->size; i++) print_quoted(b, bin->bytes[i]);
        buf_putc(b, '"');
    } else {
        for (size_t i = 0; i < bin->size; i++) {
            if (i > 0) buf_putc(b, ',');
            buf_int(b, bin->bytes[i]);
        }
    }
    if (bin->extra) {
        if (bin->size > 0) buf_putc(b, ',');
        buf_int(b, bin->bytes[bin->size] >> (8 - bin->extra));
        buf_putc(b, ':');
        buf_int(b, bin->extra);
    }
    buf_puts(b, ">>");
}

/* ---------- Bits ---------- */

static void builder_reserve(AlkuBinBuilder *b, size_t bits) {
    size_t bytes = (b->bits + bits + 7) / 8;
    if (bytes <= b->capacity) return;
    size_t capacity = b->capacity ? b->capacity : 64;
    while (capacity < bytes) capacity *= 2;
    unsigned char *data = realloc(b->data, capacity);
    if (data == NULL) out_of_memory();
    memset(data + b->capacity, 0, capacity - b->capacity);
    b->data = data;
    b->capacity = capacity;
}

/* Appends nbits bits of src, starting at bit offset `from`. */
static void put_bits(AlkuBinBuilder *b, const unsigned char *src, size_t from, size_t nbits) {
    builder_reserve(b, nbits);
    if (b->bits % 8 == 0 && from % 8 == 0) {
        if (nbits >= 8) memcpy(b->data + b->bits / 8, src + from / 8, nbits / 8);
        size_t done = nbits / 8 * 8;
        b->bits += done;
        from += done;
        nbits -= done;
    }
    for (size_t i = 0; i < nbits; i++) {
        size_t pos = b->bits + i;
        if (get_bit(src, from + i)) b->data[pos >> 3] |= (unsigned char)(0x80 >> (pos & 7));
    }
    b->bits += nbits;
}

/* Copies nbits bits starting at pos into out, right-aligned in nbytes
 * bytes (most significant first, leading bits zero). */
static void get_bits(const unsigned char *data, size_t pos, size_t nbits,
                     unsigned char *out, size_t nbytes) {
    memset(out, 0, nbytes);
    size_t skip = nbytes * 8 - nbits;
    if (pos % 8 == 0 && nbits % 8 == 0) {
        memcpy(out, data + pos / 8, nbits / 8);
        return;
    }
    for (size_t i = 0; i < nbits; i++) {
        if (get_bit(data, pos + i)) out[(skip + i) >> 3] |= (unsigned char)(0x80 >> ((skip + i) & 7));
    }
}

static void reverse_bytes(unsigned char *p, size_t n) {
    for (size_t i = 0; i < n / 2; i++) {
        unsigned char t = p[i];
        p[i] = p[n - 1 - i];
        p[n - 1 - i] = t;
    }
}

/* size * unit in bits; false if size is not a non-negative integer. */
static bool segment_bits(Value size, unsigned unit, size_t *bits) {
    if (size.tag != ALKU_INT || size.as.integer < 0 || size.as.integer > ((int64_t)1 << 40)) return false;
    *bits = (size_t)size.as.integer * unit;
    return true;
}

/* ---------- Building ---------- */

void alku_bin_begin(AlkuBinBuilder *b) {
    b->data = NULL;
    b->bits = 0;
    b->capacity = 0;
}

void alku_bin_put_integer(AlkuBinBuilder *b, Value v, Value size, unsigned unit, int flags) {
    size_t bits;
    if (!is_integer_value(v)) badarg(v);
    if (!segment_bits(size, unit, &bits)) badarg(size);
    size_t nbytes = (bits + 7) / 8;
    unsigned char small[16];
    unsigned char *bytes = nbytes <= sizeof small ? small : malloc(nbytes);
    if (bytes == NULL) out_of_memory();
    integer_to_be_bytes(v, bytes, nbytes);
    if ((flags & ALKU_BIN_LITTLE) && bits % 8 == 0) reverse_bytes(bytes, nbytes);
    put_bits(b, bytes, nbytes * 8 - bits, bits);
    if (bytes != small) free(bytes);
}

void alku_bin_put_float(AlkuBinBuilder *b, Value v, Value size, unsigned unit, int flags) {
    size_t bits;
    if (!is_number(v)) badarg(v);
    if (!segment_bits(size, unit, &bits) || (bits != 32 && bits != 64)) badarg(size);
    double d = v.tag == ALKU_FLOAT ? v.as.flt : alku_bif_float(v).as.flt;
    unsigned char bytes[8];
    uint64_t u;
    if (bits == 64) {
        memcpy(&u, &d, 8);
    } else {
        float f = (float)d;
        uint32_t u32;
        memcpy(&u32, &f, 4);
        u = u32;
    }
    for (size_t i = 0; i < bits / 8; i++) bytes[bits / 8 - 1 - i] = (unsigned char)(u >> (8 * i));
    if (flags & ALKU_BIN_LITTLE) reverse_bytes(bytes, bits / 8);
    put_bits(b, bytes, 0, bits);
}

/* /binary (unit 8) or /bitstring (unit 1). Without a size the whole value
 * goes in, and its length must be a multiple of the unit. */
void alku_bin_put_binary(AlkuBinBuilder *b, Value v, Value size, unsigned unit) {
    AlkuBinary *bin = bitstring_arg(v);
    size_t bits = bitstring_bits(bin);
    if (is_atom_named(size, "all")) {
        if (unit > 1 && bits % unit != 0) badarg(v);
    } else {
        size_t wanted;
        if (!segment_bits(size, unit, &wanted) || wanted > bits) badarg(size);
        bits = wanted;
    }
    put_bits(b, bin->bytes, 0, bits);
}

static bool valid_code(Value v) {
    return v.tag == ALKU_INT && is_char_code(v.as.integer) &&
           !(v.as.integer >= 0xD800 && v.as.integer <= 0xDFFF);
}

void alku_bin_put_utf8(AlkuBinBuilder *b, Value v) {
    if (!valid_code(v)) badarg(v);
    Buffer text = {0};
    buf_utf8(&text, v.as.integer);
    put_bits(b, (const unsigned char *)text.data, 0, text.length * 8);
    free(text.data);
}

static void put_unit16(AlkuBinBuilder *b, unsigned u, int flags) {
    unsigned char bytes[2] = {(unsigned char)(u >> 8), (unsigned char)u};
    if (flags & ALKU_BIN_LITTLE) reverse_bytes(bytes, 2);
    put_bits(b, bytes, 0, 16);
}

/* UTF-16: one 16-bit unit, or a surrogate pair above U+FFFF. */
void alku_bin_put_utf16(AlkuBinBuilder *b, Value v, int flags) {
    if (!valid_code(v)) badarg(v);
    int64_t c = v.as.integer;
    if (c < 0x10000) {
        put_unit16(b, (unsigned)c, flags);
    } else {
        c -= 0x10000;
        put_unit16(b, (unsigned)(0xD800 | (c >> 10)), flags);
        put_unit16(b, (unsigned)(0xDC00 | (c & 0x3FF)), flags);
    }
}

void alku_bin_put_utf32(AlkuBinBuilder *b, Value v, int flags) {
    if (!valid_code(v)) badarg(v);
    uint32_t c = (uint32_t)v.as.integer;
    unsigned char bytes[4] = {(unsigned char)(c >> 24), (unsigned char)(c >> 16),
                              (unsigned char)(c >> 8), (unsigned char)c};
    if (flags & ALKU_BIN_LITTLE) reverse_bytes(bytes, 4);
    put_bits(b, bytes, 0, 32);
}

Value alku_bin_end(AlkuBinBuilder *b) {
    Value result = make_bitstring(b->data, b->bits);
    free(b->data);
    return result;
}

/* ---------- Matching ---------- */

bool alku_bin_match_start(AlkuBinMatch *m, Value v) {
    if (v.tag != ALKU_BINARY) return false;
    m->binary = v.as.binary;
    m->pos = 0;
    m->bits = bitstring_bits(v.as.binary);
    return true;
}

bool alku_bin_get_integer(AlkuBinMatch *m, Value size, unsigned unit, int flags, Value *out) {
    size_t bits;
    if (!segment_bits(size, unit, &bits) || m->pos + bits > m->bits) return false;
    size_t nbytes = (bits + 7) / 8;
    unsigned char small[16];
    unsigned char *bytes = nbytes <= sizeof small ? small : malloc(nbytes);
    if (bytes == NULL) out_of_memory();
    get_bits(m->binary->bytes, m->pos, bits, bytes, nbytes);
    if ((flags & ALKU_BIN_LITTLE) && bits % 8 == 0) reverse_bytes(bytes, nbytes);
    bool is_signed = flags & ALKU_BIN_SIGNED;
    if (is_signed && bits > 0 && bits % 8 != 0) {
        /* sign-extend from the segment's top bit into the padding */
        size_t skip = nbytes * 8 - bits;
        if (bytes[0] & (0x80 >> skip)) bytes[0] |= (unsigned char)(0xFF << (8 - skip));
    }
    *out = bits == 0 ? alku_int(0) : integer_from_be_bytes(bytes, nbytes, is_signed);
    if (bytes != small) free(bytes);
    m->pos += bits;
    return true;
}

bool alku_bin_get_float(AlkuBinMatch *m, Value size, unsigned unit, int flags, Value *out) {
    size_t bits;
    if (!segment_bits(size, unit, &bits) || (bits != 32 && bits != 64) || m->pos + bits > m->bits) {
        return false;
    }
    unsigned char bytes[8];
    get_bits(m->binary->bytes, m->pos, bits, bytes, bits / 8);
    if (flags & ALKU_BIN_LITTLE) reverse_bytes(bytes, bits / 8);
    uint64_t u = 0;
    for (size_t i = 0; i < bits / 8; i++) u = (u << 8) | bytes[i];
    double d;
    if (bits == 64) {
        memcpy(&d, &u, 8);
    } else {
        uint32_t u32 = (uint32_t)u;
        float f;
        memcpy(&f, &u32, 4);
        d = f;
    }
    if (d != d || d - d != 0) return false; /* NaN or infinity: no match */
    *out = alku_float(d);
    m->pos += bits;
    return true;
}

bool alku_bin_get_binary(AlkuBinMatch *m, Value size, unsigned unit, Value *out) {
    size_t bits;
    if (!segment_bits(size, unit, &bits) || m->pos + bits > m->bits) return false;
    *out = sub_bits(m->binary, m->pos, bits);
    m->pos += bits;
    return true;
}

/* The rest of the bits; for /binary (unit 8) they must be whole bytes. */
bool alku_bin_get_rest(AlkuBinMatch *m, unsigned unit, Value *out) {
    size_t bits = m->bits - m->pos;
    if (unit > 1 && bits % unit != 0) return false;
    *out = sub_bits(m->binary, m->pos, bits);
    m->pos = m->bits;
    return true;
}

/* Decodes one UTF-8 character. */
static bool decode_utf8(const unsigned char *s, size_t n, int64_t *code, size_t *length) {
    if (n == 0) return false;
    unsigned char c = s[0];
    size_t len = c < 0x80 ? 1 : (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : (c & 0xF8) == 0xF0 ? 4 : 0;
    if (len == 0 || len > n) return false;
    int64_t cp = len == 1 ? c : len == 2 ? (c & 0x1F) : len == 3 ? (c & 0x0F) : (c & 0x07);
    for (size_t i = 1; i < len; i++) {
        if ((s[i] & 0xC0) != 0x80) return false;
        cp = (cp << 6) | (s[i] & 0x3F);
    }
    static const int64_t smallest[] = {0, 0, 0x80, 0x800, 0x10000};
    if (cp < smallest[len] || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return false;
    *code = cp;
    *length = len;
    return true;
}

/* Up to n whole bytes from the match position (the bits need not be
 * byte-aligned); returns how many there were. */
static size_t peek_bytes(const AlkuBinMatch *m, unsigned char *out, size_t n) {
    size_t available = (m->bits - m->pos) / 8;
    if (n > available) n = available;
    if (n > 0) get_bits(m->binary->bytes, m->pos, n * 8, out, n);
    return n;
}

bool alku_bin_get_utf8(AlkuBinMatch *m, Value *out) {
    unsigned char bytes[4];
    size_t n = peek_bytes(m, bytes, 4);
    int64_t code;
    size_t length;
    if (!decode_utf8(bytes, n, &code, &length)) return false;
    *out = alku_int(code);
    m->pos += length * 8;
    return true;
}

bool alku_bin_get_utf16(AlkuBinMatch *m, int flags, Value *out) {
    unsigned char bytes[4];
    size_t n = peek_bytes(m, bytes, 4);
    if (n < 2) return false;
    if (flags & ALKU_BIN_LITTLE) {
        reverse_bytes(bytes, 2);
        if (n == 4) reverse_bytes(bytes + 2, 2);
    }
    unsigned first = (unsigned)bytes[0] << 8 | bytes[1];
    if (first < 0xD800 || first > 0xDFFF) {
        *out = alku_int(first);
        m->pos += 16;
        return true;
    }
    if (first > 0xDBFF || n < 4) return false; /* a lone low surrogate, or cut off */
    unsigned second = (unsigned)bytes[2] << 8 | bytes[3];
    if (second < 0xDC00 || second > 0xDFFF) return false;
    *out = alku_int(0x10000 + ((int64_t)(first - 0xD800) << 10) + (second - 0xDC00));
    m->pos += 32;
    return true;
}

bool alku_bin_get_utf32(AlkuBinMatch *m, int flags, Value *out) {
    unsigned char bytes[4];
    if (peek_bytes(m, bytes, 4) < 4) return false;
    if (flags & ALKU_BIN_LITTLE) reverse_bytes(bytes, 4);
    int64_t c = (int64_t)bytes[0] << 24 | (int64_t)bytes[1] << 16 | (int64_t)bytes[2] << 8 | bytes[3];
    Value v = alku_int(c);
    if (!valid_code(v)) return false;
    *out = v;
    m->pos += 32;
    return true;
}

bool alku_bin_match_end(AlkuBinMatch *m) {
    return m->pos == m->bits;
}

/* ---------- Built-in functions ---------- */

static Value boolean(bool b) {
    return alku_atom(b ? "true" : "false");
}

static Value buffer_to_binary(Buffer *b) {
    Value result = make_binary(b->data, b->length);
    free(b->data);
    return result;
}

Value alku_bif_is_binary(Value v) { return boolean(v.tag == ALKU_BINARY && v.as.binary->extra == 0); }
Value alku_bif_is_bitstring(Value v) { return boolean(v.tag == ALKU_BINARY); }

Value alku_bif_byte_size(Value v) {
    AlkuBinary *b = bitstring_arg(v);
    return alku_int((int64_t)(b->size + (b->extra ? 1 : 0)));
}

Value alku_bif_bit_size(Value v) { return alku_int((int64_t)bitstring_bits(bitstring_arg(v))); }

/* Bytes, and the last few bits as a bitstring. */
Value alku_bif_bitstring_to_list(Value v) {
    AlkuBinary *bin = bitstring_arg(v);
    Value list = alku_nil();
    if (bin->extra) list = alku_cons(sub_bits(bin, bin->size * 8, bin->extra), list);
    for (size_t i = bin->size; i-- > 0;) list = alku_cons(alku_int(bin->bytes[i]), list);
    return list;
}

/* Like list_to_binary, but bitstrings may appear anywhere. */
static void put_bitstring_list(AlkuBinBuilder *b, Value v, Value arg) {
    if (v.tag == ALKU_BINARY) {
        put_bits(b, v.as.binary->bytes, 0, bitstring_bits(v.as.binary));
        return;
    }
    for (; v.tag == ALKU_CONS; v = v.as.cons->tail) {
        Value c = v.as.cons->head;
        if (c.tag == ALKU_INT && c.as.integer >= 0 && c.as.integer <= 255) {
            unsigned char byte = (unsigned char)c.as.integer;
            put_bits(b, &byte, 0, 8);
        } else if (c.tag == ALKU_CONS || c.tag == ALKU_NIL || c.tag == ALKU_BINARY) {
            put_bitstring_list(b, c, arg);
        } else {
            free(b->data);
            badarg(arg);
        }
    }
    if (v.tag == ALKU_BINARY) {
        put_bitstring_list(b, v, arg);
    } else if (v.tag != ALKU_NIL) {
        free(b->data);
        badarg(arg);
    }
}

Value alku_bif_list_to_bitstring(Value v) {
    AlkuBinBuilder b;
    alku_bin_begin(&b);
    put_bitstring_list(&b, v, v);
    return alku_bin_end(&b);
}

Value alku_bif_binary_to_list(Value v) {
    AlkuBinary *bin = binary_arg(v);
    Value list = alku_nil();
    for (size_t i = bin->size; i-- > 0;) list = alku_cons(alku_int(bin->bytes[i]), list);
    return list;
}

Value alku_bif_list_to_binary(Value v) {
    Buffer b = {0};
    print_iodata(&b, v, v);
    return buffer_to_binary(&b);
}

Value alku_bif_iolist_size(Value v) {
    Buffer b = {0};
    print_iodata(&b, v, v);
    free(b.data);
    return alku_int((int64_t)b.length);
}

static size_t index_arg(Value v) {
    if (v.tag != ALKU_INT || v.as.integer < 0) badarg(v);
    return (size_t)v.as.integer;
}

/* binary_part(Bin, Start, Length); a negative length counts backwards. */
Value alku_bif_binary_part(Value bin, Value start, Value length) {
    AlkuBinary *b = binary_arg(bin);
    size_t from = index_arg(start);
    if (length.tag != ALKU_INT) badarg(length);
    int64_t len = length.as.integer;
    if (len < 0) {
        if ((size_t)-len > from) badarg(length);
        from -= (size_t)-len;
        len = -len;
    }
    if (from > b->size || (size_t)len > b->size - from) badarg(bin);
    return sub_binary(b, from, (size_t)len);
}

Value alku_bif_split_binary(Value bin, Value pos) {
    AlkuBinary *b = binary_arg(bin);
    size_t at = index_arg(pos);
    if (at > b->size) badarg(pos);
    return alku_make_tuple(2, (Value[]){sub_binary(b, 0, at), sub_binary(b, at, b->size - at)});
}

static void check_encoding(Value encoding) {
    if (!is_atom_named(encoding, "utf8") && !is_atom_named(encoding, "unicode") &&
        !is_atom_named(encoding, "latin1")) {
        badarg(encoding);
    }
}

Value alku_bif_atom_to_binary_1(Value atom) {
    if (atom.tag != ALKU_ATOM) badarg(atom);
    return make_binary(atom.as.atom, strlen(atom.as.atom));
}

Value alku_bif_atom_to_binary_2(Value atom, Value encoding) {
    check_encoding(encoding);
    return alku_bif_atom_to_binary_1(atom);
}

Value alku_bif_binary_to_atom_1(Value bin) {
    return alku_bif_list_to_atom(alku_unicode_characters_to_list(bin));
}

Value alku_bif_binary_to_atom_2(Value bin, Value encoding) {
    check_encoding(encoding);
    return alku_bif_binary_to_atom_1(bin);
}

Value alku_bif_integer_to_binary_2(Value n, Value base) {
    Buffer b = {0};
    print_chars(&b, alku_bif_integer_to_list_2(n, base), n);
    return buffer_to_binary(&b);
}

Value alku_bif_integer_to_binary_1(Value n) {
    return alku_bif_integer_to_binary_2(n, alku_int(10));
}

Value alku_bif_binary_to_integer_2(Value bin, Value base) {
    return alku_bif_list_to_integer_2(alku_bif_binary_to_list(bin), base);
}

Value alku_bif_binary_to_integer_1(Value bin) {
    return alku_bif_binary_to_integer_2(bin, alku_int(10));
}

/* Characters (lists of code points, binaries as UTF-8) to a UTF-8 binary. */
Value alku_unicode_characters_to_binary(Value chars) {
    Buffer b = {0};
    print_chars(&b, chars, chars);
    return buffer_to_binary(&b);
}

/* UTF-8 (binaries and nested lists) to a list of code points. */
Value alku_unicode_characters_to_list(Value data) {
    Buffer b = {0};
    print_chars(&b, data, data);
    const unsigned char *s = (const unsigned char *)b.data;
    for (size_t i = 0; i < b.length;) {
        int64_t code;
        size_t length;
        if (!decode_utf8(s + i, b.length - i, &code, &length)) {
            free(b.data);
            badarg(data);
        }
        i += length;
    }
    Value list = utf8_to_list(s, b.length);
    free(b.data);
    return list;
}

Value alku_bif_float_to_binary_2(Value f, Value options) {
    Buffer b = {0};
    print_chars(&b, alku_bif_float_to_list_2(f, options), f);
    return buffer_to_binary(&b);
}

Value alku_bif_float_to_binary_1(Value f) {
    return alku_bif_float_to_binary_2(f, alku_nil());
}

Value alku_bif_binary_to_float(Value bin) {
    return alku_bif_list_to_float(alku_bif_binary_to_list(bin));
}

/* ---------- The binary module ---------- */

static bool starts_with(const AlkuBinary *b, size_t at, const AlkuBinary *pattern) {
    return pattern->size > 0 && at + pattern->size <= b->size &&
           memcmp(b->bytes + at, pattern->bytes, pattern->size) == 0;
}

/* The longest of the patterns (a binary or a list of binaries) at `at`. */
static size_t match_at(const AlkuBinary *b, size_t at, Value patterns) {
    if (patterns.tag == ALKU_BINARY) return starts_with(b, at, patterns.as.binary) ? patterns.as.binary->size : 0;
    size_t best = 0;
    for (Value p = patterns; p.tag == ALKU_CONS; p = p.as.cons->tail) {
        const AlkuBinary *pattern = binary_arg(p.as.cons->head);
        if (starts_with(b, at, pattern) && pattern->size > best) best = pattern->size;
    }
    return best;
}

/* binary:split(Bin, Pattern, Options) with options global, trim, trim_all. */
Value alku_binary_split_3(Value bin, Value pattern, Value options) {
    AlkuBinary *b = binary_arg(bin);
    bool global = false, trim = false, trim_all = false;
    for (Value o = options; o.tag == ALKU_CONS; o = o.as.cons->tail) {
        Value opt = o.as.cons->head;
        if (is_atom_named(opt, "global")) global = true;
        else if (is_atom_named(opt, "trim")) trim = true;
        else if (is_atom_named(opt, "trim_all")) trim_all = true;
        else badarg(opt);
    }
    if (pattern.tag != ALKU_BINARY) {
        if (pattern.tag != ALKU_CONS) badarg(pattern);
        for (Value p = pattern; p.tag == ALKU_CONS; p = p.as.cons->tail) binary_arg(p.as.cons->head);
    }
    Value reversed = alku_nil(); /* the parts, last one first */
    size_t start = 0, at = 0;
    while (at < b->size) {
        size_t len = match_at(b, at, pattern);
        if (len == 0) {
            at++;
            continue;
        }
        reversed = alku_cons(sub_binary(b, start, at - start), reversed);
        at += len;
        start = at;
        if (!global) break;
    }
    reversed = alku_cons(sub_binary(b, start, b->size - start), reversed);
    Value list = alku_nil();
    bool trailing = true;
    for (Value p = reversed; p.tag == ALKU_CONS; p = p.as.cons->tail) {
        Value part = p.as.cons->head;
        bool empty = part.as.binary->size == 0;
        if (empty && (trim_all || (trim && trailing))) continue;
        trailing = false;
        list = alku_cons(part, list);
    }
    return list;
}

Value alku_binary_split_2(Value bin, Value pattern) {
    return alku_binary_split_3(bin, pattern, alku_nil());
}

Value alku_binary_part(Value bin, Value start, Value length) {
    return alku_bif_binary_part(bin, start, length);
}

Value alku_binary_at(Value bin, Value pos) {
    AlkuBinary *b = binary_arg(bin);
    size_t at = index_arg(pos);
    if (at >= b->size) badarg(pos);
    return alku_int(b->bytes[at]);
}

Value alku_binary_first(Value bin) {
    AlkuBinary *b = binary_arg(bin);
    if (b->size == 0) badarg(bin);
    return alku_int(b->bytes[0]);
}

Value alku_binary_last(Value bin) {
    AlkuBinary *b = binary_arg(bin);
    if (b->size == 0) badarg(bin);
    return alku_int(b->bytes[b->size - 1]);
}
