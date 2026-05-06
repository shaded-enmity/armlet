#include "hashtable.h"
#include "common.h"

HtMemOps DefaultMem = {
    .calloc = calloc,
    .realloc = realloc,
    .free = free,
};

int bitvector_new(size_t num_bits, BitVector **out_vector) {
  return bitvector_new_impl(num_bits, &DefaultMem, out_vector);
}

int bitvector_new_impl(size_t num_bits, HtMemOps *mops,
                       BitVector **out_vector) {
  BitVector *bv;
  uint64_t *data;
  size_t items;
  assert(out_vector);

  items = 1;
  if (num_bits > 64)
    items = num_bits >> 6;

  bv = mops->calloc(1, sizeof(BitVector));
  if (!bv)
    return -ENOMEM;

  data = (uint64_t *)mops->calloc(items, sizeof(*data));
  if (!data) {
    mops->free((void *)bv);
    return -ENOMEM;
  }

  bv->items = data;
  bv->num_items = items;
  bv->mops = mops;

  *out_vector = bv;

  return 0;
}


static int _bitvector_realloc(BitVector *vector, size_t offset) {
  assert(vector);

  if (vector->num_items >= offset)
    return 0;

  vector->items =
      safe_realloc_array(vector->items, offset + 1, sizeof(uint64_t),
                         vector->mops->realloc);

  if (!vector->items)
    return -ENOMEM;

  vector->items[offset] = 0;
  vector->num_items = offset;

  return 0;
}

int bitvector_set_bit(BitVector *vector, size_t bit, bool value) {
  size_t offset, shift;
  int r;
  assert(vector);

  offset = bit >> 6;
  shift = bit & 63;

  r = _bitvector_realloc(vector, offset);
  if (r < 0)
    return r;

  if (value)
    vector->items[offset] |= (uint64_t)1 << (uint64_t)shift;
  else
    vector->items[offset] &= ~((uint64_t)1 << (uint64_t)shift);

  return 0;
}

int bitvector_get_bit(BitVector *vector, size_t bit, bool *value) {
  size_t offset, shift;
  assert(vector);
  assert(value);

  offset = bit >> 6;
  shift = bit & 63;

  if (offset >= vector->num_items)
    return -EINVAL;

  *value = vector->items[offset] & ((uint64_t)1 << (uint64_t)shift);

  return 0;
}

inline static void _set_range(uint64_t *data, size_t min, size_t max,
                              bool value) {
  if (value)
    *data |= ((1ull << (max - min)) - 1) << min;
  else
    *data &= ~(((1ull << (max - min)) - 1) << min);
}

int bitvector_set_bits(BitVector *vector, size_t min, size_t max, bool value) {
  size_t offset_min, offset_max, shift_min, shift_max;
  int r;
  assert(vector);
  assert(max > min);

  offset_min = min >> 6;
  shift_min = min & 63;
  offset_max = max >> 6;
  shift_max = max & 63;

  r = _bitvector_realloc(vector, offset_max);
  if (r < 0)
    return r;

  if (offset_min == offset_max)
    _set_range(&vector->items[offset_min], shift_min, shift_max, value);
  else {
    _set_range(&vector->items[offset_min], shift_min, BV_MAX_SHIFT, value);
    _set_range(&vector->items[offset_max], 0, shift_max, value);
    for (offset_min++; offset_min < (offset_max - 1); offset_min++)
      vector->items[offset_min] = value ? BV_INT_MAX : 0;
  }

  return 0;
}

int bitvector_mask_bits(BitVector *vector, size_t start, uint64_t *bits,
                        size_t size) {
  size_t offset_min, offset_max, shift_min, shift_max;
  uint64_t *pb;
  assert(vector);
  assert(bits);

  pb = bits + 1;
  offset_min = start >> 6;
  shift_min = start & 63;
  offset_max = (start + size) >> 6;
  shift_max = (start + size) & 63;

  if (offset_min == offset_max)
    vector->items[offset_min] &= BV_INT_MAX ^ (*bits << shift_min);
  else {
    vector->items[offset_min] &= BV_INT_MAX ^ (*bits << shift_min);
    vector->items[offset_max] &= BV_INT_MAX ^ (*bits << shift_max);
    for (offset_min++; offset_min < (offset_max - 1); offset_min++)
      vector->items[offset_min] &= *pb++;
  }

  return 0;
}

// TODO: This is ARM64 specific
uint64_t popcnt64_fast(uint64_t *p, size_t len) {
  unsigned long long *d = (unsigned long long *)p;
  unsigned int masked = 0, i = 0;
  uint64_t c = 0;

  masked = len & ~3;
  for (; i < masked; i += 4)
    __asm__("LD1 {v0.2D, v1.2D}, [%1], #32    \n\t"
            "CNT v0.16b, v0.16b               \n\t"
            "CNT v1.16b, v1.16b               \n\t"
            "UADDLV h2, v0.16b                \n\t"
            "UADDLV h3, v1.16b                \n\t"
            "ADD d2, d3, d2                   \n\t"
            "UMOV x0, v2.d[0]                 \n\t"
            "ADD %0, x0, %0                   \n\t"
            : "+r"(c), "+r"(d)::"x0", "v0", "v1", "v2", "v3");

  for (; i < len; ++i)
    __asm__("LD1  {v0.D}[0], [%1], #8 \n\t"
            "CNT  v0.8b, v0.8b        \n\t"
            "UADDLV h1, v0.8b         \n\t"
            "UMOV x0, v1.d[0]         \n\t"
            "ADD %0, x0, %0           \n\t"
            : "+r"(c), "+r"(d)::"x0", "v0", "v1");

  return c;
}

int bitvector_count_bits(BitVector *vector, size_t max, size_t *out_count) {
  size_t offset, shift, count;
  assert(vector);
  assert(out_count);

  count = 0;
  offset = max >> 6;
  shift = max & 63;

  if (offset)
    count += popcnt64_fast(vector->items, offset);

  count += __builtin_popcountll(vector->items[offset] & ((1ull << shift) - 1));

  *out_count = count;

  return 0;
}

int bitvector_next_set_bit(BitVector *vector, size_t index, size_t *out_index) {
  size_t offset, shift, mask;
  int p;
  assert(vector);
  assert(out_index);

  offset = index >> 6;
  shift = index & 63;
  mask = ((1ull << (shift + 1)) - 1);

  if (vector->items[offset] >= mask) {
    p = __builtin_ffsll(vector->items[offset] & (BV_INT_MAX ^ mask));
    *out_index = (offset << 6) + (p - 1);
    return 0;
  }

  for (++offset; offset < vector->num_items; offset++) {
    p = __builtin_ffsll(vector->items[offset]);
    if (p) {
      *out_index = (offset << 6) + (p - 1);
      return 0;
    }
  }

  *out_index = BV_INT_MAX;
  return -ENOENT;
}

int bitvector_unref(BitVector *vector) {
  assert(vector);

  HtMemOps *mops = vector->mops;

  mops->free((void *)vector->items);
  mops->free((void *)vector);

  return 0;
}

uint64_t murmur3_64(const char *key, size_t len, uint64_t seed) {
  const uint64_t m = 0xc6a4a7935bd1e995;
  const int r = 47;

  uint64_t h = seed ^ (len * m);

  const uint64_t *data = (const uint64_t *)key;
  const uint64_t *end = data + (len / 8);

  while (data != end) {
    uint64_t k = *data++;

    k *= m;
    k ^= k >> r;
    k *= m;

    h ^= k;
    h *= m;
  }

  const unsigned char *data2 = (const unsigned char *)data;

  switch (len & 7) {
  case 7:
    h ^= (uint64_t)(data2[6]) << 48;
    /* fallthrough */
  case 6:
    h ^= (uint64_t)(data2[5]) << 40;
    /* fallthrough */
  case 5:
    h ^= (uint64_t)(data2[4]) << 32;
    /* fallthrough */
  case 4:
    h ^= (uint64_t)(data2[3]) << 24;
    /* fallthrough */
  case 3:
    h ^= (uint64_t)(data2[2]) << 16;
    /* fallthrough */
  case 2:
    h ^= (uint64_t)(data2[1]) << 8;
    /* fallthrough */
  case 1:
    h ^= (uint64_t)(data2[0]);
    h *= m;
  };

  h ^= h >> r;
  h *= m;
  h ^= h >> r;

  return h;
}

static Id _hashtable_hash_string(void *p) {
  const char *str = p;
  return HASH(str);
}

static int _hashtable_cmp_string(const void *a, const void *b) {
  if (a == b)
    return 0;

  return strcmp((const char *)a, (const char *)b);
}

static size_t _hashtable_calc_size(size_t old_size) { return old_size << 1; }

static float _hashtable_load_factor(size_t buckets, size_t items) {
  return (float)items / ((float)buckets * 4.0f);
}

static int _hashtable_insert_bucket(Hashtable *hash, Bucket *bucket, void *key,
                                    void *value) {
  assert(bucket);

  bucket->items = safe_realloc_array(bucket->items, bucket->num_items + 1,
                                     sizeof(Element), hash->mops->realloc);

  if (!bucket->items)
    return -ENOMEM;

  bucket->num_items++;
  bucket->items[bucket->num_items - 1].key = key;
  bucket->items[bucket->num_items - 1].value = value;

  return 0;
}

static int _hashtable_resize_and_rehash(Hashtable *hash) {
  BitVector *vector;
  Element *element;
  Bucket *new_buckets;
  size_t new_size, new_loc, old_size, i, j;
  int r;
  assert(hash);

  old_size = hash->num_buckets;
  new_size = _hashtable_calc_size(hash->num_buckets);

  r = bitvector_new_impl(new_size, hash->mops, &vector);
  if (r < 0)
    return r;

  new_buckets = (Bucket *)hash->mops->calloc(sizeof(Bucket), new_size);

  for (i = 0; i < old_size; ++i) {
    for (j = 0; j < hash->buckets[i].num_items; ++j) {
      element = &hash->buckets[i].items[j];

      new_loc = hash->hash_func(element->key) % new_size;

      (void)bitvector_set_bit(vector, new_loc, true);
      r = _hashtable_insert_bucket(hash, &new_buckets[new_loc], element->key,
                                   element->value);
      if (r < 0)
        goto err_insert;
    }
  }

  hash->mops->free((void *)hash->buckets);
  hash->buckets = new_buckets;
  hash->mops->free((void *)hash->vector);
  hash->vector = vector;

  return 0;
err_insert:
  for (--i; i != BV_INT_MAX; --i)
    hash->mops->free((void *)new_buckets[i].items);
  hash->mops->free((void *)new_buckets);
  bitvector_unref(vector);
  return r;
}

int hashtable_new(size_t buckets, Hashtable **out_hash) {
  return hashtable_new_impl(buckets, &DefaultMem, out_hash);
}

int hashtable_new_impl(size_t buckets, HtMemOps *mops, Hashtable **out_hash) {
  Hashtable *sh;
  size_t size;
  int r = -ENOMEM;
  assert(out_hash);

  size = _hashtable_calc_size(buckets);

  sh = mops->calloc(1, sizeof(Hashtable));
  if (!sh)
    return r;

  sh->buckets = (Bucket *)mops->calloc(sizeof(Bucket), size);
  if (!sh->buckets)
    goto err;

  r = bitvector_new_impl(size, mops, &sh->vector);
  if (r < 0)
    goto err;

  sh->key_cmp = _hashtable_cmp_string;
  sh->hash_func = _hashtable_hash_string;
  sh->mops = mops;

  sh->num_buckets = size;
  sh->rehash_factor = 0.75f;
  *out_hash = sh;

  return 0;

err:
  if (sh)
    mops->free((void *)sh);
  if (sh->buckets)
    mops->free((void *)sh->buckets);

  return r;
}

int hashtable_find(const Hashtable *hash, Id id, void *key, void **out_value) {
  size_t index, i;
  bool v;
  int r;
  assert(hash);

  r = -ENOENT;

  index = id % hash->num_buckets;
  (void)bitvector_get_bit(hash->vector, index, &v);
  if (!v)
    goto out;

  for (i = 0; i < hash->buckets[index].num_items; ++i) {
    if (hash->key_cmp(hash->buckets[index].items[i].key, key) == 0) {
      if (out_value)
        *out_value = hash->buckets[index].items[i].value;
      r = 0;
      goto out;
    }
  }

out:
  return r;
}

int hashtable_find_str(const Hashtable *hash, const char *key,
                       void **out_value) {
  return hashtable_find(hash, hash->hash_func((void *)key), (void *)key,
                        out_value);
}

int hashtable_add(Hashtable *hash, Id id, void *key, void *value) {
  size_t index;
  int r;
  assert(hash);

  r = hashtable_find(hash, id, key, NULL);
  if (!r) {
    r = -EEXIST;
    goto out;
  } else
    r = 0;

  if (_hashtable_load_factor(hash->num_buckets, hash->num_items + 1) >
      hash->rehash_factor) {
    r = _hashtable_resize_and_rehash(hash);
    if (r < 0)
      goto out;
  }

  index = id % hash->num_buckets;

  r = bitvector_set_bit(hash->vector, index, true);
  if (r < 0)
    goto out;

  r = _hashtable_insert_bucket(hash, &hash->buckets[index], key, value);
  if (r < 0) {
    if (hash->buckets[index].num_items > 0)
      (void)bitvector_set_bit(hash->vector, index, false);
    goto out;
  }

  hash->num_items++;

out:
  return r;
}

int hashtable_add_str(Hashtable *hash, const char *key, void *value) {
  assert(hash);
  assert(key);
  return hashtable_add(hash, hash->hash_func((void *)key), (void *)key, value);
}

int hashtable_remove(Hashtable *hash, Id id, void *key, void **out_value) {
  size_t index, i, last;
  int r;
  bool v;
  assert(hash);

  r = -ENOENT;

  index = id % hash->num_buckets;
  (void)bitvector_get_bit(hash->vector, index, &v);
  if (!v)
    goto out;

  for (i = 0; i < hash->buckets[index].num_items; ++i) {
    if (hash->key_cmp(hash->buckets[index].items[i].key, key) == 0) {
      if (hash->buckets[index].num_items == 1) {
        (void)bitvector_set_bit(hash->vector, index, false);
        hash->mops->free((void *)hash->buckets[index].items);
      } else {
        last = hash->buckets[index].num_items - 1;

        if (i != last) {
          hash->buckets[index].items[i].key =
              hash->buckets[index].items[last].key;
          hash->buckets[index].items[i].value =
              hash->buckets[index].items[last].value;
        }

        hash->buckets[index].items =
            safe_realloc_array(hash->buckets[index].items, last, sizeof(Element),
                               hash->mops->realloc);
        if (!hash->buckets[index].items) {
          r = -ENOMEM;
          goto out;
        }
      }

      if (out_value)
        *out_value = hash->buckets[index].items[i].value;

      hash->num_items--;
      hash->buckets[index].num_items--;

      r = 0;
      goto out;
    }
  }

out:
  return r;
}

int hashtable_unref(Hashtable *hash) {
  assert(hash);

  HtMemOps *mops = hash->mops;

  bitvector_unref(hash->vector);
  mops->free((void *)hash->buckets);
  mops->free((void *)hash);

  return 0;
}

int hashtable_iterate(Hashtable *hash, HtIterator **out_iterator) {
  HtIterator *it;
  assert(hash);
  assert(out_iterator);

  it = hash->mops->calloc(1, sizeof(HtIterator));
  if (!it)
    return -ENOMEM;

  it->hashtable = hash;

  *out_iterator = it;

  return 0;
}

bool hashtable_iterator_next(HtIterator *iterator, void **out_key,
                             void **out_value) {
  Element *e;
  Hashtable *h;
  size_t next;
  int r;
  assert(iterator);

  h = iterator->hashtable;

  if (h->num_items == iterator->seen)
    return false;

  if (iterator->probe >= h->buckets[iterator->offset].num_items) {
    iterator->probe = 0;

    r = bitvector_next_set_bit(iterator->hashtable->vector, iterator->offset,
                               &next);
    if (r < 0)
      return false;

    iterator->offset = next;
    if (h->buckets[iterator->offset].num_items)
      goto success;

    while (!h->buckets[iterator->offset].num_items) {
      r = bitvector_next_set_bit(iterator->hashtable->vector, iterator->offset,
                                 &next);
      if (r < 0)
        return false;

      iterator->offset = next;
    }
  }

success:
  e = &h->buckets[iterator->offset].items[iterator->probe];
  if (out_key)
    *out_key = e->key;
  if (out_value)
    *out_value = e->value;
  iterator->probe++;
  iterator->seen++;
  return true;
}

bool hashtable_iterator_end(HtIterator *iterator) {
  assert(iterator);

  return iterator->hashtable->num_items == iterator->seen;
}

int hashtable_iterator_unref(HtIterator *iterator) {
  assert(iterator);
  iterator->hashtable->mops->free((void *)iterator);
  return 0;
}
