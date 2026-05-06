#ifndef __ARMLET_HASHTABLE__
#define __ARMLET_HASHTABLE__

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <assert.h>
#include <errno.h>
#include <string.h>

enum { BV_MAX_SHIFT = sizeof(uint64_t) * 8, BV_INT_MAX = (uint64_t)-1 };

typedef struct _HtMemOps {
  void *(*calloc)(size_t, size_t);
  void *(*realloc)(void *, size_t);
  void  (*free)(void *);
} HtMemOps;

typedef struct _BitVector {
  uint64_t *items;
  size_t num_bits;
  size_t num_items;
  HtMemOps *mops;
} BitVector;

#define MURMUR64_SEED (uint64_t)0xbac0f00dbac0f00d
#define MURMUR64L(s) murmur3_64(s, strlen(s), MURMUR64_SEED)
#define HASH(s) MURMUR64L(s)

typedef uint64_t Id;

typedef Id (*HtHashKey)(void *);
typedef int (*HtCmpKey)(const void *, const void *);

typedef struct _Element {
  void *key;
  void *value;
} Element;

typedef struct _Bucket {
  size_t num_items;
  Element *items;
} Bucket;

typedef struct _Hashtable {
  Bucket *buckets;
  BitVector *vector;
  size_t num_buckets;
  size_t num_items;
  float rehash_factor;
  HtHashKey hash_func;
  HtCmpKey key_cmp;
  HtMemOps *mops;
} Hashtable;

typedef struct _HtIterator {
  Hashtable *hashtable;
  size_t offset;
  size_t probe;
  size_t seen;
} HtIterator;

int bitvector_new(size_t num_bits, BitVector **out_vector);
int bitvector_new_impl(size_t num_bits, HtMemOps *mops, BitVector **out_vector);
int bitvector_set_bit(BitVector *vector, size_t bit, bool value);
int bitvector_get_bit(BitVector *vector, size_t bit, bool *value);
int bitvector_set_bits(BitVector *vector, size_t min, size_t max, bool value);
int bitvector_mask_bits(BitVector *vector, size_t start, uint64_t *bits,
                        size_t size);
int bitvector_count_bits(BitVector *vector, size_t max, size_t *out_count);
int bitvector_next_set_bit(BitVector *vector, size_t index, size_t *out_index);
int bitvector_unref(BitVector *vector);

int hashtable_new(size_t buckets, Hashtable **out_hash);
int hashtable_new_impl(size_t buckets, HtMemOps *mops, Hashtable **out_hash);
int hashtable_find(const Hashtable *hash, Id id, void *key, void **out_value);
int hashtable_find_str(const Hashtable *hash, const char *key, void **out_value);
int hashtable_add(Hashtable *hash, Id id, void *key, void *value);
int hashtable_add_str(Hashtable *hash, const char *key, void *value);
int hashtable_remove(Hashtable *hash, Id id, void *key, void **out_value);
int hashtable_unref(Hashtable *hash);
int hashtable_iterate(Hashtable *hash, HtIterator **out_iterator);
bool hashtable_iterator_next(HtIterator *iterator, void **out_key,
                             void **out_value);
bool hashtable_iterator_end(HtIterator *iterator);
int hashtable_iterator_unref(HtIterator *iterator);

extern HtMemOps DefaultMem;

#define HASHTABLE_ITERATE(_HT, _KT, _VT, _BLK) ({\
    HtIterator *_IT = NULL;\
    hashtable_iterate((_HT), &_IT);\
    _KT key;\
    _VT value;\
    while (hashtable_iterator_next(_IT, (void **)&key, (void **)&value)) _BLK\
})

#define HASHTABLE_ITERATE_KEYS(_HT, _KT, _BLK) ({\
    HtIterator *_IT = NULL;\
    hashtable_iterate((_HT), &_IT);\
    _KT key;\
    while (hashtable_iterator_next(_IT, (void **)&key, (void **)NULL)) _BLK\
})

#define HASHTABLE_ITERATE_VALUES(_HT, _VT, _BLK) ({\
    HtIterator *_IT = NULL;\
    hashtable_iterate((_HT), &_IT);\
    _VT value;\
    while (hashtable_iterator_next(_IT, (void **)NULL, (void **)&value)) _BLK\
})

#endif
