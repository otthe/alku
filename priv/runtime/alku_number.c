/*
 * Numbers: integers of any size, floats, and the math module.
 *
 * Integers that fit in 64 bits are stored inside the Value (ALKU_INT).
 * When a result does not fit, it becomes a big integer (ALKU_BIG): sign
 * and magnitude, with the magnitude in 32-bit limbs, least significant
 * first. Results are always normalized, so a value has exactly one
 * representation and comparing representations compares values.
 */
#include "alku_internal.h"
#include "alku_gc.h"

#include <inttypes.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ---------- Magnitudes (arrays of 32-bit limbs) ---------- */

static uint32_t *new_limbs(size_t n) {
    uint32_t *limbs = calloc(n + 1, sizeof *limbs);
    if (limbs == NULL) out_of_memory();
    return limbs;
}

static size_t trimmed(const uint32_t *a, size_t n) {
    while (n > 0 && a[n - 1] == 0) n--;
    return n;
}

static int mag_compare(const uint32_t *a, size_t na, const uint32_t *b, size_t nb) {
    if (na != nb) return na < nb ? -1 : 1;
    for (size_t i = na; i-- > 0;) {
        if (a[i] != b[i]) return a[i] < b[i] ? -1 : 1;
    }
    return 0;
}

/* out needs max(na, nb) + 1 limbs. */
static size_t mag_add(const uint32_t *a, size_t na, const uint32_t *b, size_t nb, uint32_t *out) {
    if (na < nb) {
        const uint32_t *t = a; a = b; b = t;
        size_t tn = na; na = nb; nb = tn;
    }
    uint64_t carry = 0;
    for (size_t i = 0; i < na; i++) {
        uint64_t s = (uint64_t)a[i] + (i < nb ? b[i] : 0) + carry;
        out[i] = (uint32_t)s;
        carry = s >> 32;
    }
    out[na] = (uint32_t)carry;
    return na + 1;
}

/* a - b where a >= b; out needs na limbs. */
static size_t mag_sub(const uint32_t *a, size_t na, const uint32_t *b, size_t nb, uint32_t *out) {
    int64_t borrow = 0;
    for (size_t i = 0; i < na; i++) {
        int64_t d = (int64_t)a[i] - (i < nb ? b[i] : 0) - borrow;
        out[i] = (uint32_t)d;
        borrow = d < 0;
    }
    return na;
}

/* out needs na + nb limbs, zeroed. */
static size_t mag_mul(const uint32_t *a, size_t na, const uint32_t *b, size_t nb, uint32_t *out) {
    for (size_t i = 0; i < na; i++) {
        uint64_t carry = 0;
        for (size_t j = 0; j < nb; j++) {
            uint64_t t = (uint64_t)a[i] * b[j] + out[i + j] + carry;
            out[i + j] = (uint32_t)t;
            carry = t >> 32;
        }
        out[i + nb] = (uint32_t)carry;
    }
    return na + nb;
}

/* Divides a magnitude in place by a small divisor; returns the remainder. */
static uint32_t mag_div_small(uint32_t *a, size_t n, uint32_t divisor) {
    uint64_t rem = 0;
    for (size_t i = n; i-- > 0;) {
        uint64_t t = (rem << 32) | a[i];
        a[i] = (uint32_t)(t / divisor);
        rem = t % divisor;
    }
    return (uint32_t)rem;
}

/* a = a * factor + addend, in place; a has room for one more limb. */
static size_t mag_mul_add_small(uint32_t *a, size_t n, uint32_t factor, uint32_t addend) {
    uint64_t carry = addend;
    for (size_t i = 0; i < n; i++) {
        uint64_t t = (uint64_t)a[i] * factor + carry;
        a[i] = (uint32_t)t;
        carry = t >> 32;
    }
    if (carry) a[n++] = (uint32_t)carry;
    return n;
}

/* Long division (Knuth, The Art of Computer Programming, vol. 2,
 * algorithm D). u has m limbs, v has n >= 2 limbs, m >= n. q gets
 * m - n + 1 limbs, r gets n limbs. */
static void mag_divmod(const uint32_t *u, size_t m, const uint32_t *v, size_t n,
                       uint32_t *q, uint32_t *r) {
    const uint64_t base = (uint64_t)1 << 32;
    int s = __builtin_clz(v[n - 1]); /* shift so v's top limb has its top bit set */
    uint32_t *vn = new_limbs(n), *un = new_limbs(m + 1);
    for (size_t i = n - 1; i > 0; i--) {
        vn[i] = (v[i] << s) | (s ? (uint32_t)((uint64_t)v[i - 1] >> (32 - s)) : 0);
    }
    vn[0] = v[0] << s;
    un[m] = s ? (uint32_t)((uint64_t)u[m - 1] >> (32 - s)) : 0;
    for (size_t i = m - 1; i > 0; i--) {
        un[i] = (u[i] << s) | (s ? (uint32_t)((uint64_t)u[i - 1] >> (32 - s)) : 0);
    }
    un[0] = u[0] << s;

    for (size_t j = m - n + 1; j-- > 0;) {
        /* estimate this quotient digit from the top two limbs */
        uint64_t top = ((uint64_t)un[j + n] << 32) | un[j + n - 1];
        uint64_t qhat = top / vn[n - 1];
        uint64_t rhat = top % vn[n - 1];
        while (qhat >= base || qhat * vn[n - 2] > ((rhat << 32) | un[j + n - 2])) {
            qhat--;
            rhat += vn[n - 1];
            if (rhat >= base) break;
        }
        /* multiply and subtract */
        int64_t borrow = 0;
        for (size_t i = 0; i < n; i++) {
            uint64_t p = qhat * vn[i];
            int64_t t = (int64_t)un[i + j] - borrow - (int64_t)(p & 0xFFFFFFFF);
            un[i + j] = (uint32_t)t;
            borrow = (int64_t)(p >> 32) - (t >> 32);
        }
        int64_t t = (int64_t)un[j + n] - borrow;
        un[j + n] = (uint32_t)t;
        q[j] = (uint32_t)qhat;
        if (t < 0) { /* the estimate was one too big: add back */
            q[j]--;
            uint64_t carry = 0;
            for (size_t i = 0; i < n; i++) {
                uint64_t sum = (uint64_t)un[i + j] + vn[i] + carry;
                un[i + j] = (uint32_t)sum;
                carry = sum >> 32;
            }
            un[j + n] += (uint32_t)carry;
        }
    }
    for (size_t i = 0; i + 1 < n; i++) {
        r[i] = (un[i] >> s) | (s ? (uint32_t)((uint64_t)un[i + 1] << (32 - s)) : 0);
    }
    r[n - 1] = un[n - 1] >> s;
    free(vn);
    free(un);
}

static uint32_t *twos_complement(Value v, size_t n);
static Value from_twos_complement(uint32_t *limbs, size_t n);

/* ---------- Integers ---------- */

/* A read-only view of an integer's sign and magnitude. Fixnums are
 * unpacked into `small`, so the view must not be copied. */
typedef struct {
    const uint32_t *limbs;
    size_t length;
    bool negative;
    uint32_t small[2];
} IntView;

static void view(Value v, IntView *out) {
    if (v.tag == ALKU_INT) {
        int64_t n = v.as.integer;
        uint64_t m = n < 0 ? (uint64_t)0 - (uint64_t)n : (uint64_t)n;
        out->small[0] = (uint32_t)m;
        out->small[1] = (uint32_t)(m >> 32);
        out->length = trimmed(out->small, 2);
        out->limbs = out->small;
        out->negative = n < 0;
    } else {
        out->limbs = v.as.big->limbs;
        out->length = v.as.big->length;
        out->negative = v.as.big->negative;
    }
}

/* Builds an integer from a magnitude (which the caller still owns). */
static Value make_integer(bool negative, const uint32_t *limbs, size_t n) {
    n = trimmed(limbs, n);
    if (n <= 2) {
        uint64_t m = n == 0 ? 0 : n == 1 ? limbs[0] : ((uint64_t)limbs[1] << 32) | limbs[0];
        if (!negative && m <= (uint64_t)INT64_MAX) return alku_int((int64_t)m);
        if (negative && m <= (uint64_t)INT64_MAX) return alku_int(-(int64_t)m);
        if (negative && m == (uint64_t)INT64_MAX + 1) return alku_int(INT64_MIN);
    }
    AlkuBig *big = gc_alloc(GC_BIG, sizeof *big + n * sizeof(uint32_t));
    big->length = n;
    big->negative = negative;
    memcpy(big->limbs, limbs, n * sizeof(uint32_t));
    Value v;
    v.tag = ALKU_BIG;
    v.as.big = big;
    return v;
}

bool is_integer_value(Value v) {
    return v.tag == ALKU_INT || v.tag == ALKU_BIG;
}

bool is_number(Value v) {
    return v.tag == ALKU_INT || v.tag == ALKU_BIG || v.tag == ALKU_FLOAT;
}

static Value integer_arg(Value v) {
    if (!is_integer_value(v)) badarith();
    return v;
}

/* a + b, or a - b when subtract is set */
static Value int_add(Value a, Value b, bool subtract) {
    IntView x, y;
    view(a, &x);
    view(b, &y);
    bool y_negative = y.negative != subtract;
    size_t room = (x.length > y.length ? x.length : y.length) + 1;
    uint32_t *out = new_limbs(room);
    size_t n;
    bool negative;
    if (x.negative == y_negative) {
        n = mag_add(x.limbs, x.length, y.limbs, y.length, out);
        negative = x.negative;
    } else if (mag_compare(x.limbs, x.length, y.limbs, y.length) >= 0) {
        n = mag_sub(x.limbs, x.length, y.limbs, y.length, out);
        negative = x.negative;
    } else {
        n = mag_sub(y.limbs, y.length, x.limbs, x.length, out);
        negative = y_negative;
    }
    Value result = make_integer(negative, out, n);
    free(out);
    return result;
}

static Value int_mul(Value a, Value b) {
    IntView x, y;
    view(a, &x);
    view(b, &y);
    uint32_t *out = new_limbs(x.length + y.length);
    size_t n = mag_mul(x.limbs, x.length, y.limbs, y.length, out);
    Value result = make_integer(x.negative != y.negative, out, n);
    free(out);
    return result;
}

/* Truncating division, like Erlang's div and rem. */
static void int_divrem(Value a, Value b, Value *quotient, Value *remainder) {
    IntView x, y;
    view(a, &x);
    view(b, &y);
    if (y.length == 0) badarith();
    if (mag_compare(x.limbs, x.length, y.limbs, y.length) < 0) {
        if (quotient) *quotient = alku_int(0);
        if (remainder) *remainder = a;
        return;
    }
    size_t m = x.length, n = y.length;
    uint32_t *q = new_limbs(m), *r = new_limbs(n);
    if (n == 1) {
        memcpy(q, x.limbs, m * sizeof(uint32_t));
        r[0] = mag_div_small(q, m, y.limbs[0]);
    } else {
        mag_divmod(x.limbs, m, y.limbs, n, q, r); /* q gets m - n + 1 limbs */
    }
    if (quotient) *quotient = make_integer(x.negative != y.negative, q, n == 1 ? m : m - n + 1);
    if (remainder) *remainder = make_integer(x.negative, r, n);
    free(q);
    free(r);
}

static int compare_integers(Value a, Value b) {
    if (a.tag == ALKU_INT && b.tag == ALKU_INT) {
        return (a.as.integer > b.as.integer) - (a.as.integer < b.as.integer);
    }
    IntView x, y;
    view(a, &x);
    view(b, &y);
    if (x.negative != y.negative) return x.negative ? -1 : 1;
    int c = mag_compare(x.limbs, x.length, y.limbs, y.length);
    return x.negative ? -c : c;
}

static double integer_to_double(Value v) {
    if (v.tag == ALKU_INT) return (double)v.as.integer;
    double d = 0;
    for (size_t i = v.as.big->length; i-- > 0;) d = d * 4294967296.0 + v.as.big->limbs[i];
    return v.as.big->negative ? -d : d;
}

/* The integer with the value of an integral double. */
static Value integer_from_double(double d) {
    if (d >= -9223372036854775808.0 && d < 9223372036854775808.0) return alku_int((int64_t)d);
    int exponent;
    double fraction = frexp(fabs(d), &exponent); /* |d| = fraction * 2^exponent */
    uint64_t mantissa = (uint64_t)ldexp(fraction, 53);
    int shift = exponent - 53; /* at least 10 here */
    size_t limbs = (size_t)(shift / 32) + 3;
    uint32_t *out = new_limbs(limbs);
    size_t word = (size_t)(shift / 32);
    int bit = shift % 32;
    uint64_t low = mantissa << bit;
    uint64_t high = bit ? mantissa >> (64 - bit) : 0;
    out[word] = (uint32_t)low;
    out[word + 1] = (uint32_t)(low >> 32);
    out[word + 2] = (uint32_t)high;
    Value result = make_integer(d < 0, out, limbs);
    free(out);
    return result;
}

/* Parses digits in the given base (with an optional sign). */
Value parse_integer(const char *text, size_t length, unsigned base, bool *ok) {
    size_t i = 0;
    bool negative = false;
    *ok = false;
    if (i < length && (text[i] == '-' || text[i] == '+')) negative = text[i++] == '-';
    if (i == length) return alku_int(0);
    uint32_t *mag = new_limbs(length / 4 + 2); /* a base-36 digit is under 6 bits */
    size_t n = 0;
    for (; i < length; i++) {
        char c = text[i];
        unsigned digit = c >= '0' && c <= '9' ? (unsigned)(c - '0')
                       : c >= 'a' && c <= 'z' ? (unsigned)(c - 'a' + 10)
                       : c >= 'A' && c <= 'Z' ? (unsigned)(c - 'A' + 10) : 99;
        if (digit >= base) {
            free(mag);
            return alku_int(0);
        }
        n = mag_mul_add_small(mag, n, base, digit);
    }
    *ok = true;
    Value result = make_integer(negative, mag, n);
    free(mag);
    return result;
}

Value alku_integer_literal(const char *decimal) {
    bool ok;
    return parse_integer(decimal, strlen(decimal), 10, &ok);
}

void print_integer(Buffer *b, Value v, unsigned base, bool lowercase) {
    if (v.tag == ALKU_INT && base == 10) {
        buf_int(b, v.as.integer);
        return;
    }
    const char *alphabet = lowercase ? "0123456789abcdefghijklmnopqrstuvwxyz"
                                     : "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    IntView x;
    view(v, &x);
    if (x.length == 0) {
        buf_putc(b, '0');
        return;
    }
    /* divide by the largest power of the base that fits in a limb */
    uint32_t chunk = base;
    int chunk_digits = 1;
    while ((uint64_t)chunk * base <= UINT32_MAX) {
        chunk *= base;
        chunk_digits++;
    }
    uint32_t *mag = new_limbs(x.length);
    memcpy(mag, x.limbs, x.length * sizeof(uint32_t));
    size_t n = x.length;
    char *digits = malloc(x.length * 32 + 1);
    if (digits == NULL) out_of_memory();
    size_t count = 0;
    while (n > 0) {
        uint32_t rem = mag_div_small(mag, n, chunk);
        n = trimmed(mag, n);
        for (int i = 0; i < chunk_digits && (n > 0 || rem > 0); i++) {
            digits[count++] = alphabet[rem % base];
            rem /= base;
        }
    }
    if (x.negative) buf_putc(b, '-');
    while (count > 0) buf_putc(b, digits[--count]);
    free(digits);
    free(mag);
}

/* The low n bytes of v in two's complement, most significant first. */
void integer_to_be_bytes(Value v, unsigned char *out, size_t n) {
    size_t limbs = n / 4 + 2;
    if (v.tag == ALKU_INT) {
        uint64_t u = (uint64_t)v.as.integer;
        for (size_t i = 0; i < n; i++) {
            out[n - 1 - i] = i < 8 ? (unsigned char)(u >> (8 * i)) : (v.as.integer < 0 ? 0xFF : 0);
        }
        return;
    }
    if (limbs < v.as.big->length + 1) limbs = v.as.big->length + 1;
    uint32_t *t = twos_complement(v, limbs);
    for (size_t i = 0; i < n; i++) {
        size_t limb = i / 4;
        uint32_t word = limb < limbs ? t[limb] : (v.as.big->negative ? 0xFFFFFFFF : 0);
        out[n - 1 - i] = (unsigned char)(word >> (8 * (i % 4)));
    }
    free(t);
}

/* The integer in n bytes, most significant first. */
Value integer_from_be_bytes(const unsigned char *in, size_t n, bool is_signed) {
    if (n <= 7 || (n == 8 && is_signed)) {
        uint64_t u = 0;
        for (size_t i = 0; i < n; i++) u = (u << 8) | in[i];
        if (is_signed && n > 0 && (in[0] & 0x80)) {
            u |= n == 8 ? 0 : ~(uint64_t)0 << (8 * n); /* sign-extend */
        }
        return alku_int((int64_t)u);
    }
    size_t limbs = n / 4 + 2;
    uint32_t *t = new_limbs(limbs);
    bool negative = is_signed && n > 0 && (in[0] & 0x80);
    for (size_t i = 0; i < limbs * 4; i++) {
        unsigned char byte = i < n ? in[n - 1 - i] : (negative ? 0xFF : 0);
        t[i / 4] |= (uint32_t)byte << (8 * (i % 4));
    }
    Value result = from_twos_complement(t, limbs);
    free(t);
    return result;
}

/* ---------- Floats ---------- */

Value alku_float(double d) {
    Value v;
    v.tag = ALKU_FLOAT;
    v.as.flt = d;
    return v;
}

/* Erlang has no infinity or NaN: such results are arithmetic errors. */
static Value make_float(double d) {
    if (!isfinite(d)) badarith();
    return alku_float(d);
}

static double to_double(Value v) {
    if (v.tag == ALKU_FLOAT) return v.as.flt;
    if (is_integer_value(v)) return integer_to_double(v);
    badarith();
}

/* The fewest significant digits that read back as d (d > 0).
 * Fills digits (no dot, no trailing zeros) and the decimal exponent of
 * the first digit: 1234.5 gives "12345" and 3. */
static void shortest_digits(double d, char *digits, int *exponent) {
    char text[40];
    for (int precision = 1; precision <= 17; precision++) {
        snprintf(text, sizeof text, "%.*e", precision - 1, d);
        if (strtod(text, NULL) == d) break;
    }
    size_t n = 0;
    const char *p = text;
    for (; *p != 'e'; p++) {
        if (*p != '.') digits[n++] = *p;
    }
    while (n > 1 && digits[n - 1] == '0') n--;
    digits[n] = '\0';
    *exponent = atoi(p + 1);
}

/* How Erlang prints floats with ~p, ~w and io:write: the shortest digits,
 * in plain or scientific notation, whichever is shorter (plain on a tie):
 * 0.1, 1.5, 100.0, 1.0e3, 1.0e-5, 0.30000000000000004. */
static void print_float_short(Buffer *b, double d) {
    if (d == 0) {
        buf_puts(b, signbit(d) ? "-0.0" : "0.0");
        return;
    }
    if (d < 0) {
        buf_putc(b, '-');
        d = -d;
    }
    char digits[24];
    int x;
    shortest_digits(d, digits, &x);
    int n = (int)strlen(digits);

    char exp_text[8];
    snprintf(exp_text, sizeof exp_text, "%d", x);
    int scientific_length = n + (n == 1 ? 2 : 1) + 1 + (int)strlen(exp_text);
    int plain_length = x >= 0 ? (n <= x + 1 ? x + 3 : n + 1) : n + 1 - x;

    if (plain_length <= scientific_length) {
        if (x < 0) {
            buf_puts(b, "0.");
            for (int i = 0; i < -x - 1; i++) buf_putc(b, '0');
            buf_puts(b, digits);
        } else if (n <= x + 1) {
            buf_puts(b, digits);
            for (int i = 0; i < x + 1 - n; i++) buf_putc(b, '0');
            buf_puts(b, ".0");
        } else {
            for (int i = 0; i < n; i++) {
                buf_putc(b, digits[i]);
                if (i == x) buf_putc(b, '.');
            }
        }
    } else {
        buf_putc(b, digits[0]);
        buf_putc(b, '.');
        buf_puts(b, n == 1 ? "0" : digits + 1);
        buf_putc(b, 'e');
        buf_puts(b, exp_text);
    }
}

void print_float_fixed(Buffer *b, double d, int decimals) {
    char text[400];
    snprintf(text, sizeof text, "%.*f", decimals > 300 ? 300 : decimals, d);
    buf_puts(b, text);
}

/* ~e: digits significant digits, exponent as Erlang writes it (e+2). */
void print_float_scientific(Buffer *b, double d, int digits) {
    char text[400];
    snprintf(text, sizeof text, "%.*e", digits < 2 ? 0 : digits > 300 ? 299 : digits - 1, d);
    char *e = strchr(text, 'e');
    *e = '\0';
    buf_puts(b, text);
    buf_putc(b, 'e');
    buf_putc(b, e[1]);
    buf_int(b, atoi(e + 2));
}

void print_number(Buffer *b, Value v) {
    if (v.tag == ALKU_FLOAT) {
        print_float_short(b, v.as.flt);
    } else {
        print_integer(b, v, 10, false);
    }
}

/* Numeric order; an integer and a float with the same value are equal. */
int compare_numbers(Value a, Value b) {
    if (a.tag == ALKU_FLOAT && b.tag == ALKU_FLOAT) {
        return (a.as.flt > b.as.flt) - (a.as.flt < b.as.flt);
    }
    if (a.tag != ALKU_FLOAT && b.tag != ALKU_FLOAT) return compare_integers(a, b);
    if (b.tag == ALKU_FLOAT) return -compare_numbers(b, a);
    /* a is a float, b an integer: compare floor(a) exactly, then any fraction */
    double whole = floor(a.as.flt);
    int c = compare_integers(integer_from_double(whole), b);
    if (c != 0) return c;
    return a.as.flt > whole ? 1 : 0;
}

/* ---------- Arithmetic ---------- */

static bool either_float(Value a, Value b) {
    return a.tag == ALKU_FLOAT || b.tag == ALKU_FLOAT;
}

Value alku_op_add(Value a, Value b) {
    int64_t r;
    if (a.tag == ALKU_INT && b.tag == ALKU_INT && !__builtin_add_overflow(a.as.integer, b.as.integer, &r)) {
        return alku_int(r);
    }
    if (either_float(a, b)) return make_float(to_double(a) + to_double(b));
    return int_add(integer_arg(a), integer_arg(b), false);
}

Value alku_op_sub(Value a, Value b) {
    int64_t r;
    if (a.tag == ALKU_INT && b.tag == ALKU_INT && !__builtin_sub_overflow(a.as.integer, b.as.integer, &r)) {
        return alku_int(r);
    }
    if (either_float(a, b)) return make_float(to_double(a) - to_double(b));
    return int_add(integer_arg(a), integer_arg(b), true);
}

Value alku_op_mul(Value a, Value b) {
    int64_t r;
    if (a.tag == ALKU_INT && b.tag == ALKU_INT && !__builtin_mul_overflow(a.as.integer, b.as.integer, &r)) {
        return alku_int(r);
    }
    if (either_float(a, b)) return make_float(to_double(a) * to_double(b));
    return int_mul(integer_arg(a), integer_arg(b));
}

Value alku_op_fdiv(Value a, Value b) {
    double y = to_double(b);
    if (y == 0) badarith();
    return make_float(to_double(a) / y);
}

/* div and rem truncate toward zero, exactly like C's / and %. */
Value alku_op_div(Value a, Value b) {
    if (a.tag == ALKU_INT && b.tag == ALKU_INT && b.as.integer != 0 &&
        !(a.as.integer == INT64_MIN && b.as.integer == -1)) {
        return alku_int(a.as.integer / b.as.integer);
    }
    Value q;
    int_divrem(integer_arg(a), integer_arg(b), &q, NULL);
    return q;
}

Value alku_op_rem(Value a, Value b) {
    if (a.tag == ALKU_INT && b.tag == ALKU_INT && b.as.integer != 0) {
        if (b.as.integer == -1) return alku_int(0); /* INT64_MIN % -1 is undefined in C */
        return alku_int(a.as.integer % b.as.integer);
    }
    Value r;
    int_divrem(integer_arg(a), integer_arg(b), NULL, &r);
    return r;
}

Value alku_op_neg(Value a) {
    if (a.tag == ALKU_INT && a.as.integer != INT64_MIN) return alku_int(-a.as.integer);
    if (a.tag == ALKU_FLOAT) return alku_float(-a.as.flt);
    return int_add(alku_int(0), integer_arg(a), true);
}

Value alku_op_plus(Value a) {
    if (!is_number(a)) badarith();
    return a;
}

Value alku_bif_abs(Value v) {
    if (v.tag == ALKU_FLOAT) return alku_float(fabs(v.as.flt));
    if (!is_integer_value(v)) badarg(v);
    return compare_integers(v, alku_int(0)) < 0 ? alku_op_neg(v) : v;
}

/* ---------- Bitwise operators ---------- */

/* Two's complement form of an integer in n limbs (enough for its sign). */
static uint32_t *twos_complement(Value v, size_t n) {
    IntView x;
    view(v, &x);
    uint32_t *out = new_limbs(n);
    memcpy(out, x.limbs, x.length * sizeof(uint32_t));
    if (x.negative) { /* -m is ~(m - 1) */
        for (size_t i = 0; i < n; i++) {
            if (out[i]-- != 0) break;
        }
        for (size_t i = 0; i < n; i++) out[i] = ~out[i];
    }
    return out;
}

static Value from_twos_complement(uint32_t *limbs, size_t n) {
    bool negative = limbs[n - 1] >> 31;
    if (negative) { /* back to magnitude: ~t + 1 */
        for (size_t i = 0; i < n; i++) limbs[i] = ~limbs[i];
        for (size_t i = 0; i < n; i++) {
            if (++limbs[i] != 0) break;
        }
    }
    return make_integer(negative, limbs, n);
}

static size_t limb_count(Value v) {
    return v.tag == ALKU_INT ? 2 : v.as.big->length;
}

static Value bitwise(Value a, Value b, char op) {
    a = integer_arg(a);
    b = integer_arg(b);
    if (a.tag == ALKU_INT && b.tag == ALKU_INT) {
        int64_t x = a.as.integer, y = b.as.integer;
        return alku_int(op == '&' ? x & y : op == '|' ? x | y : x ^ y);
    }
    size_t n = (limb_count(a) > limb_count(b) ? limb_count(a) : limb_count(b)) + 1;
    uint32_t *x = twos_complement(a, n), *y = twos_complement(b, n);
    for (size_t i = 0; i < n; i++) {
        x[i] = op == '&' ? x[i] & y[i] : op == '|' ? x[i] | y[i] : x[i] ^ y[i];
    }
    Value result = from_twos_complement(x, n);
    free(x);
    free(y);
    return result;
}

Value alku_op_band(Value a, Value b) { return bitwise(a, b, '&'); }
Value alku_op_bor(Value a, Value b) { return bitwise(a, b, '|'); }
Value alku_op_bxor(Value a, Value b) { return bitwise(a, b, '^'); }

Value alku_op_bnot(Value a) {
    return alku_op_sub(alku_op_neg(integer_arg(a)), alku_int(1)); /* bnot X = -X - 1 */
}

/* a * 2^shift for shift >= 0; floor(a / 2^-shift) for shift < 0. */
static Value shift(Value a, Value amount) {
    a = integer_arg(a);
    if (amount.tag != ALKU_INT) {
        integer_arg(amount); /* not an integer: badarith */
        system_limit();      /* an integer beyond 64 bits */
    }
    int64_t s = amount.as.integer;
    if (s > (int64_t)1 << 32) system_limit();
    if (s < -((int64_t)1 << 40)) {
        return alku_int(compare_integers(a, alku_int(0)) < 0 ? -1 : 0);
    }
    if (a.tag == ALKU_INT) {
        int64_t x = a.as.integer;
        if (s <= 0) return alku_int(s <= -63 ? (x < 0 ? -1 : 0) : x >> -s);
        if (s < 62 && (x >= 0 ? x <= INT64_MAX >> s : x >= INT64_MIN >> s)) {
            return alku_int((int64_t)((uint64_t)x << s));
        }
    }
    IntView v;
    view(a, &v);
    if (s >= 0) {
        size_t words = (size_t)(s / 32), bits = (size_t)(s % 32);
        size_t n = v.length + words + 1;
        uint32_t *out = new_limbs(n);
        for (size_t i = 0; i < v.length; i++) {
            uint64_t t = (uint64_t)v.limbs[i] << bits;
            out[i + words] |= (uint32_t)t;
            out[i + words + 1] |= (uint32_t)(t >> 32);
        }
        Value result = make_integer(v.negative, out, n);
        free(out);
        return result;
    }
    /* right shift rounds toward minus infinity: -m >> s = -((m - 1) >> s) - 1 */
    if (v.negative) {
        Value m1 = alku_op_sub(alku_op_neg(a), alku_int(1));
        return alku_op_sub(alku_op_neg(shift(m1, amount)), alku_int(1));
    }
    uint64_t right = (uint64_t)-s;
    size_t words = (size_t)(right / 32), bits = (size_t)(right % 32);
    if (words >= v.length) return alku_int(0);
    size_t n = v.length - words;
    uint32_t *out = new_limbs(n);
    for (size_t i = 0; i < n; i++) {
        uint64_t t = v.limbs[i + words] | (i + words + 1 < v.length ? (uint64_t)v.limbs[i + words + 1] << 32 : 0);
        out[i] = (uint32_t)(t >> bits);
    }
    Value result = make_integer(false, out, n);
    free(out);
    return result;
}

Value alku_op_bsl(Value a, Value b) { return shift(a, b); }

Value alku_op_bsr(Value a, Value b) {
    if (b.tag == ALKU_INT && b.as.integer != INT64_MIN) return shift(a, alku_int(-b.as.integer));
    integer_arg(b);
    system_limit();
}

/* ---------- Conversion built-ins ---------- */

static Value boolean(bool b) {
    return alku_atom(b ? "true" : "false");
}

Value alku_bif_is_float(Value v) { return boolean(v.tag == ALKU_FLOAT); }
Value alku_bif_is_number(Value v) { return boolean(is_number(v)); }

Value alku_bif_float(Value v) {
    if (!is_number(v)) badarg(v);
    return make_float(to_double(v));
}

static Value float_to_integer(Value v, double (*round_fn)(double)) {
    if (is_integer_value(v)) return v;
    if (v.tag != ALKU_FLOAT) badarg(v);
    return integer_from_double(round_fn(v.as.flt));
}

Value alku_bif_round(Value v) { return float_to_integer(v, round); }
Value alku_bif_trunc(Value v) { return float_to_integer(v, trunc); }
Value alku_bif_floor(Value v) { return float_to_integer(v, floor); }
Value alku_bif_ceil(Value v) { return float_to_integer(v, ceil); }

static Value buffer_to_list(Buffer *b) {
    Value list = utf8_to_list((const unsigned char *)b->data, b->length);
    free(b->data);
    return list;
}

/* Erlang's default: "1.50000000000000000000e+00" */
Value alku_bif_float_to_list_1(Value f) {
    if (f.tag != ALKU_FLOAT) badarg(f);
    char text[64];
    snprintf(text, sizeof text, "%.20e", f.as.flt);
    return alku_string(text, strlen(text));
}

/* Options: [short], [{decimals, N}] or [{scientific, N}]. */
Value alku_bif_float_to_list_2(Value f, Value options) {
    if (f.tag != ALKU_FLOAT) badarg(f);
    Buffer b = {0};
    if (options.tag == ALKU_NIL) return alku_bif_float_to_list_1(f);
    if (options.tag != ALKU_CONS || options.as.cons->tail.tag != ALKU_NIL) badarg(options);
    Value option = options.as.cons->head;
    if (is_atom_named(option, "short")) {
        print_float_short(&b, f.as.flt);
    } else if (option.tag == ALKU_TUPLE && option.as.tuple->arity == 2 &&
               option.as.tuple->elements[1].tag == ALKU_INT &&
               option.as.tuple->elements[1].as.integer >= 0 &&
               option.as.tuple->elements[1].as.integer <= 253) {
        int n = (int)option.as.tuple->elements[1].as.integer;
        if (is_atom_named(option.as.tuple->elements[0], "decimals")) {
            print_float_fixed(&b, f.as.flt, n);
        } else if (is_atom_named(option.as.tuple->elements[0], "scientific")) {
            char text[400];
            snprintf(text, sizeof text, "%.*e", n, f.as.flt);
            buf_puts(&b, text);
        } else {
            badarg(options);
        }
    } else {
        badarg(options);
    }
    return buffer_to_list(&b);
}

/* Collects a string of ASCII characters; false if it has anything else. */
static bool ascii_text(Value chars, char *out, size_t size) {
    size_t n = 0;
    Value v = chars;
    for (; v.tag == ALKU_CONS; v = v.as.cons->tail) {
        Value c = v.as.cons->head;
        if (c.tag != ALKU_INT || c.as.integer <= 0 || c.as.integer > 127 || n + 1 >= size) return false;
        out[n++] = (char)c.as.integer;
    }
    out[n] = '\0';
    return v.tag == ALKU_NIL;
}

/* Erlang syntax only: digits, a dot, digits, optional exponent. */
Value alku_bif_list_to_float(Value chars) {
    char text[512];
    if (!ascii_text(chars, text, sizeof text)) badarg(chars);
    const char *p = text;
    if (*p == '-' || *p == '+') p++;
    if (!(*p >= '0' && *p <= '9')) badarg(chars);
    while (*p >= '0' && *p <= '9') p++;
    if (*p++ != '.' || !(*p >= '0' && *p <= '9')) badarg(chars);
    while (*p >= '0' && *p <= '9') p++;
    if (*p == 'e' || *p == 'E') {
        p++;
        if (*p == '-' || *p == '+') p++;
        if (!(*p >= '0' && *p <= '9')) badarg(chars);
        while (*p >= '0' && *p <= '9') p++;
    }
    if (*p != '\0') badarg(chars);
    double d = strtod(text, NULL);
    if (!isfinite(d)) badarg(chars);
    return alku_float(d);
}

static unsigned base_arg(Value base) {
    if (base.tag != ALKU_INT || base.as.integer < 2 || base.as.integer > 36) badarg(base);
    return (unsigned)base.as.integer;
}

Value alku_bif_integer_to_list_2(Value n, Value base) {
    if (!is_integer_value(n)) badarg(n);
    Buffer b = {0};
    print_integer(&b, n, base_arg(base), false);
    return buffer_to_list(&b);
}

Value alku_bif_integer_to_list(Value n) {
    return alku_bif_integer_to_list_2(n, alku_int(10));
}

Value alku_bif_list_to_integer_2(Value chars, Value base) {
    unsigned b = base_arg(base);
    Buffer text = {0};
    for (Value v = chars; ; v = v.as.cons->tail) {
        if (v.tag == ALKU_NIL) break;
        if (v.tag != ALKU_CONS || v.as.cons->head.tag != ALKU_INT ||
            v.as.cons->head.as.integer <= 0 || v.as.cons->head.as.integer > 127) {
            free(text.data);
            badarg(chars);
        }
        buf_putc(&text, (char)v.as.cons->head.as.integer);
    }
    bool ok;
    Value result = parse_integer(text.data, text.length, b, &ok);
    free(text.data);
    if (!ok) badarg(chars);
    return result;
}

Value alku_bif_list_to_integer(Value chars) {
    return alku_bif_list_to_integer_2(chars, alku_int(10));
}

/* ---------- math ---------- */

static double math_arg(Value v) {
    if (!is_number(v)) badarg(v);
    return to_double(v);
}

/* Domain errors (like sqrt(-1)) are arithmetic errors in Erlang. */
static Value math_result(double d) {
    return make_float(d);
}

Value alku_math_pi(void) { return alku_float(3.14159265358979323846); }
Value alku_math_sqrt(Value x) { return math_result(sqrt(math_arg(x))); }
Value alku_math_pow(Value x, Value y) { return math_result(pow(math_arg(x), math_arg(y))); }
Value alku_math_exp(Value x) { return math_result(exp(math_arg(x))); }
Value alku_math_log(Value x) { return math_result(log(math_arg(x))); }
Value alku_math_log2(Value x) { return math_result(log2(math_arg(x))); }
Value alku_math_log10(Value x) { return math_result(log10(math_arg(x))); }
Value alku_math_sin(Value x) { return math_result(sin(math_arg(x))); }
Value alku_math_cos(Value x) { return math_result(cos(math_arg(x))); }
Value alku_math_tan(Value x) { return math_result(tan(math_arg(x))); }
Value alku_math_asin(Value x) { return math_result(asin(math_arg(x))); }
Value alku_math_acos(Value x) { return math_result(acos(math_arg(x))); }
Value alku_math_atan(Value x) { return math_result(atan(math_arg(x))); }
Value alku_math_atan2(Value y, Value x) { return math_result(atan2(math_arg(y), math_arg(x))); }
Value alku_math_floor(Value x) { return math_result(floor(math_arg(x))); }
Value alku_math_ceil(Value x) { return math_result(ceil(math_arg(x))); }
