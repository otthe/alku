/*
 * Maps: persistent (immutable) weight-balanced binary trees, ordered by
 * key in Erlang term order with exact comparison (so 1 and 1.0 are
 * different keys). An update copies only the path from the root to the
 * changed node: O(log n) time and memory, and the old map is unchanged.
 *
 * The balancing scheme is the one of Haskell's Data.Map (Adams' trees
 * with delta = 3, ratio = 2).
 */
#include "alku_internal.h"
#include "alku_gc.h"

#include <stdlib.h>

#define DELTA 3
#define RATIO 2

static size_t size_of(AlkuMapNode *t) {
    return t ? t->size : 0;
}

static AlkuMapNode *node(Value key, Value value, AlkuMapNode *left, AlkuMapNode *right) {
    AlkuMapNode *n = gc_alloc(GC_MAP, sizeof *n);
    n->size = 1 + size_of(left) + size_of(right);
    n->key = key;
    n->value = value;
    n->left = left;
    n->right = right;
    return n;
}

static Value map_value(AlkuMapNode *root) {
    Value v;
    v.tag = ALKU_MAP;
    v.as.map = root;
    return v;
}

static int compare_keys(Value a, Value b) {
    return compare_terms(a, b, true);
}

/* ---------- Balancing ---------- */

static AlkuMapNode *single_left(Value k, Value v, AlkuMapNode *l, AlkuMapNode *r) {
    return node(r->key, r->value, node(k, v, l, r->left), r->right);
}

static AlkuMapNode *double_left(Value k, Value v, AlkuMapNode *l, AlkuMapNode *r) {
    AlkuMapNode *rl = r->left;
    return node(rl->key, rl->value, node(k, v, l, rl->left), node(r->key, r->value, rl->right, r->right));
}

static AlkuMapNode *single_right(Value k, Value v, AlkuMapNode *l, AlkuMapNode *r) {
    return node(l->key, l->value, l->left, node(k, v, l->right, r));
}

static AlkuMapNode *double_right(Value k, Value v, AlkuMapNode *l, AlkuMapNode *r) {
    AlkuMapNode *lr = l->right;
    return node(lr->key, lr->value, node(l->key, l->value, l->left, lr->left), node(k, v, lr->right, r));
}

/* A node with these children, rotated if one side became too heavy. */
static AlkuMapNode *balance(Value k, Value v, AlkuMapNode *l, AlkuMapNode *r) {
    size_t sl = size_of(l), sr = size_of(r);
    if (sl + sr <= 1) return node(k, v, l, r);
    if (sr > DELTA * sl) {
        return size_of(r->left) < RATIO * size_of(r->right) ? single_left(k, v, l, r)
                                                             : double_left(k, v, l, r);
    }
    if (sl > DELTA * sr) {
        return size_of(l->right) < RATIO * size_of(l->left) ? single_right(k, v, l, r)
                                                             : double_right(k, v, l, r);
    }
    return node(k, v, l, r);
}

/* ---------- Tree operations ---------- */

static AlkuMapNode *insert(AlkuMapNode *t, Value key, Value value) {
    if (t == NULL) return node(key, value, NULL, NULL);
    int c = compare_keys(key, t->key);
    if (c < 0) return balance(t->key, t->value, insert(t->left, key, value), t->right);
    if (c > 0) return balance(t->key, t->value, t->left, insert(t->right, key, value));
    return node(key, value, t->left, t->right);
}

static AlkuMapNode *find(AlkuMapNode *t, Value key) {
    while (t != NULL) {
        int c = compare_keys(key, t->key);
        if (c == 0) return t;
        t = c < 0 ? t->left : t->right;
    }
    return NULL;
}

static AlkuMapNode *remove_min(AlkuMapNode *t, AlkuMapNode **min) {
    if (t->left == NULL) {
        *min = t;
        return t->right;
    }
    return balance(t->key, t->value, remove_min(t->left, min), t->right);
}

static AlkuMapNode *remove_max(AlkuMapNode *t, AlkuMapNode **max) {
    if (t->right == NULL) {
        *max = t;
        return t->left;
    }
    return balance(t->key, t->value, t->left, remove_max(t->right, max));
}

/* Joins two subtrees whose keys are in order (everything in l < r). */
static AlkuMapNode *glue(AlkuMapNode *l, AlkuMapNode *r) {
    if (l == NULL) return r;
    if (r == NULL) return l;
    AlkuMapNode *m;
    if (l->size > r->size) {
        AlkuMapNode *rest = remove_max(l, &m);
        return balance(m->key, m->value, rest, r);
    }
    AlkuMapNode *rest = remove_min(r, &m);
    return balance(m->key, m->value, l, rest);
}

static AlkuMapNode *remove_key(AlkuMapNode *t, Value key) {
    if (t == NULL) return NULL;
    int c = compare_keys(key, t->key);
    if (c < 0) return balance(t->key, t->value, remove_key(t->left, key), t->right);
    if (c > 0) return balance(t->key, t->value, t->left, remove_key(t->right, key));
    return glue(t->left, t->right);
}

/* Copies the entries into arrays in key order; returns the next index. */
static size_t collect(AlkuMapNode *t, Value *keys, Value *values, size_t i) {
    if (t == NULL) return i;
    i = collect(t->left, keys, values, i);
    if (keys) keys[i] = t->key;
    if (values) values[i] = t->value;
    return collect(t->right, keys, values, i + 1);
}

/* ---------- Checks and errors ---------- */

bool alku_is_map(Value v) {
    return v.tag == ALKU_MAP;
}

static AlkuMapNode *map_arg(Value map) {
    if (map.tag != ALKU_MAP) raise_error(pair("badmap", map));
    return map.as.map;
}

static _Noreturn void badkey(Value key) {
    raise_error(pair("badkey", key));
}

/* A tuple used as a temporary array, so the collector sees the values. */
static Value *scratch_values(size_t n) {
    return alku_make_tuple(n, NULL).as.tuple->elements;
}

/* ---------- For generated code ---------- */

Value alku_map_new(void) {
    return map_value(NULL);
}

Value alku_map_put(Value map, Value key, Value value) {
    return map_value(insert(map_arg(map), key, value));
}

Value alku_map_update(Value map, Value key, Value value) {
    AlkuMapNode *root = map_arg(map);
    if (find(root, key) == NULL) badkey(key);
    return map_value(insert(root, key, value));
}

Value alku_map_check(Value map) {
    map_arg(map);
    return map;
}

bool alku_map_lookup(Value map, Value key, Value *value) {
    if (map.tag != ALKU_MAP) return false;
    AlkuMapNode *n = find(map.as.map, key);
    if (n == NULL) return false;
    *value = n->value;
    return true;
}

/* ---------- Order and printing ---------- */

/* Erlang's map order: by size, then keys in order, then values. */
int compare_maps(Value a, Value b, bool exact) {
    size_t n = size_of(a.as.map), m = size_of(b.as.map);
    if (n != m) return n < m ? -1 : 1;
    if (n == 0) return 0;
    Value *keys_a = scratch_values(n), *values_a = scratch_values(n);
    Value *keys_b = scratch_values(n), *values_b = scratch_values(n);
    collect(a.as.map, keys_a, values_a, 0);
    collect(b.as.map, keys_b, values_b, 0);
    for (size_t i = 0; i < n; i++) {
        int c = compare_keys(keys_a[i], keys_b[i]);
        if (c != 0) return c;
    }
    for (size_t i = 0; i < n; i++) {
        int c = compare_terms(values_a[i], values_b[i], exact);
        if (c != 0) return c;
    }
    return 0;
}

static void print_entries(Buffer *b, AlkuMapNode *t, bool pretty, bool *first) {
    if (t == NULL) return;
    print_entries(b, t->left, pretty, first);
    if (!*first) buf_putc(b, ',');
    *first = false;
    print_value(b, t->key, pretty);
    buf_puts(b, " => ");
    print_value(b, t->value, pretty);
    print_entries(b, t->right, pretty, first);
}

void print_map(Buffer *b, Value map, bool pretty) {
    bool first = true;
    buf_puts(b, "#{");
    print_entries(b, map.as.map, pretty, &first);
    buf_putc(b, '}');
}

/* ---------- Built-in functions ---------- */

static Value boolean(bool b) {
    return alku_atom(b ? "true" : "false");
}

Value alku_bif_is_map(Value v) { return boolean(v.tag == ALKU_MAP); }

Value alku_bif_map_size(Value map) {
    return alku_int((int64_t)size_of(map_arg(map)));
}

Value alku_bif_is_map_key(Value key, Value map) {
    return boolean(find(map_arg(map), key) != NULL);
}

Value alku_bif_map_get(Value key, Value map) {
    AlkuMapNode *n = find(map_arg(map), key);
    if (n == NULL) badkey(key);
    return n->value;
}

Value alku_maps_new(void) { return alku_map_new(); }
Value alku_maps_get_2(Value key, Value map) { return alku_bif_map_get(key, map); }
Value alku_maps_put(Value key, Value value, Value map) { return alku_map_put(map, key, value); }
Value alku_maps_update(Value key, Value value, Value map) { return alku_map_update(map, key, value); }
Value alku_maps_is_key(Value key, Value map) { return alku_bif_is_map_key(key, map); }
Value alku_maps_size(Value map) { return alku_bif_map_size(map); }

Value alku_maps_get_3(Value key, Value map, Value fallback) {
    AlkuMapNode *n = find(map_arg(map), key);
    return n ? n->value : fallback;
}

Value alku_maps_remove(Value key, Value map) {
    return map_value(remove_key(map_arg(map), key));
}

Value alku_maps_find(Value key, Value map) {
    AlkuMapNode *n = find(map_arg(map), key);
    return n ? pair("ok", n->value) : alku_atom("error");
}

/* keys, values or {key, value} pairs, in key order */
static Value entries(Value map, int which) {
    AlkuMapNode *root = map_arg(map);
    size_t n = size_of(root);
    Value *keys = scratch_values(n), *values = scratch_values(n);
    collect(root, keys, values, 0);
    Value list = alku_nil();
    for (size_t i = n; i-- > 0;) {
        Value item = which == 0 ? keys[i] : which == 1 ? values[i]
                   : alku_make_tuple(2, (Value[]){keys[i], values[i]});
        list = alku_cons(item, list);
    }
    return list;
}

Value alku_maps_keys(Value map) { return entries(map, 0); }
Value alku_maps_values(Value map) { return entries(map, 1); }
Value alku_maps_to_list(Value map) { return entries(map, 2); }

/* Later pairs win, as in Erlang. */
Value alku_maps_from_list(Value list) {
    AlkuMapNode *root = NULL;
    Value v = list;
    for (; v.tag == ALKU_CONS; v = v.as.cons->tail) {
        Value item = v.as.cons->head;
        if (item.tag != ALKU_TUPLE || item.as.tuple->arity != 2) badarg(list);
        root = insert(root, item.as.tuple->elements[0], item.as.tuple->elements[1]);
    }
    if (v.tag != ALKU_NIL) badarg(list);
    return map_value(root);
}

/* Values from b win. */
Value alku_maps_merge(Value a, Value b) {
    AlkuMapNode *root = map_arg(a), *other = map_arg(b);
    size_t n = size_of(other);
    Value *keys = scratch_values(n), *values = scratch_values(n);
    collect(other, keys, values, 0);
    for (size_t i = 0; i < n; i++) root = insert(root, keys[i], values[i]);
    return map_value(root);
}
