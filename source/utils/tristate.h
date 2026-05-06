#ifndef __ARMLET_TRISTATE__
#define __ARMLET_TRISTATE__

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __AVX2__
#include <immintrin.h>
#endif

#ifdef __aarch64__
#include <arm_neon.h>
#endif

/**
 * Tri-state bit vector that supports 0, 1 and X states.
 *
 */
enum armlet_wildcard_mode {
  WILDCARD_RESULT,
  WILDCARD_PRESERVE,
};

typedef struct {
  size_t nbits;
  size_t nwords;
  uint64_t last_mask;
  uint64_t *data;
  uint64_t *known;
} armlet_bitvector;

static inline size_t words_for_bits(size_t bits) { return (bits + 63) >> 6; }

static inline uint64_t last_word_mask(size_t bits) {
  if (bits == 0)
    return 0;
  int r = bits & 63;
  if (r == 0)
    return ~0ULL;
  return ((1ULL << r) - 1);
}

static inline void armlet_bitvector_mask_last(armlet_bitvector *bv,
                                              uint64_t *wdata,
                                              uint64_t *wknown) {
  if (bv->nwords == 0)
    return;
  size_t i = bv->nwords - 1;
  wdata[i] &= bv->last_mask;
  wknown[i] &= bv->last_mask;
}

armlet_bitvector *armlet_bitvector_new(size_t nbits);
void armlet_bitvector_free(armlet_bitvector *bv);

void armlet_bitvector_set_all_x(armlet_bitvector *bv);
void armlet_bitvector_set_all_zero(armlet_bitvector *bv);
void armlet_bitvector_set_all_one(armlet_bitvector *bv);
void armlet_bitvector_set_all_fill(armlet_bitvector *bv, int is_x, int value);

void armlet_bitvector_set_bit(armlet_bitvector *bv, size_t idx, int value,
                              int is_x);
int armlet_bitvector_get_bit(const armlet_bitvector *bv, size_t idx,
                             int *is_x);

int armlet_bitvector_from_string(armlet_bitvector *bv, const char *s);
void armlet_bitvector_to_string(const armlet_bitvector *bv, char *out);
char *armlet_bitvector_to_string_alloc(const armlet_bitvector *bv);

void armlet_bitvector_and_scalar(armlet_bitvector *dst,
                                 const armlet_bitvector *a,
                                 const armlet_bitvector *b);
void armlet_bitvector_or_scalar(armlet_bitvector *dst,
                                const armlet_bitvector *a,
                                const armlet_bitvector *b);
void armlet_bitvector_xor_scalar(armlet_bitvector *dst,
                                 const armlet_bitvector *a,
                                 const armlet_bitvector *b);
void armlet_bitvector_not_scalar(armlet_bitvector *dst,
                                 const armlet_bitvector *a);
void armlet_bitvector_copy_scalar(armlet_bitvector *dst,
                                  const armlet_bitvector *src);

#ifdef __AVX2__
static inline void armlet_bitvector_and_simd_inner_avx2(
    uint64_t *dst_data, uint64_t *dst_known, const uint64_t *Da_ptr,
    const uint64_t *Ka_ptr, const uint64_t *Db_ptr, const uint64_t *Kb_ptr,
    size_t blocks4) {
  for (size_t i = 0; i < blocks4; ++i) {
    __m256i Da = _mm256_loadu_si256((const __m256i *)(Da_ptr + 4 * i));
    __m256i Ka = _mm256_loadu_si256((const __m256i *)(Ka_ptr + 4 * i));
    __m256i Db = _mm256_loadu_si256((const __m256i *)(Db_ptr + 4 * i));
    __m256i Kb = _mm256_loadu_si256((const __m256i *)(Kb_ptr + 4 * i));
    __m256i notDa = _mm256_andnot_si256(Da, Ka); // (~Da) & Ka
    __m256i notDb = _mm256_andnot_si256(Db, Kb); // (~Db) & Kb
    __m256i Ka_and_Kb = _mm256_and_si256(Ka, Kb);
    __m256i known = _mm256_or_si256(Ka_and_Kb, _mm256_or_si256(notDb, notDa));
    __m256i data = _mm256_and_si256(_mm256_and_si256(Da, Db), Ka_and_Kb);
    _mm256_storeu_si256((__m256i *)(dst_known + 4 * i), known);
    _mm256_storeu_si256((__m256i *)(dst_data + 4 * i), data);
  }
}

static inline void armlet_bitvector_or_simd_inner_avx2(
    uint64_t *dst_data, uint64_t *dst_known, const uint64_t *Da_ptr,
    const uint64_t *Ka_ptr, const uint64_t *Db_ptr, const uint64_t *Kb_ptr,
    size_t blocks4) {
  for (size_t i = 0; i < blocks4; ++i) {
    __m256i Da = _mm256_loadu_si256((const __m256i *)(Da_ptr + 4 * i));
    __m256i Ka = _mm256_loadu_si256((const __m256i *)(Ka_ptr + 4 * i));
    __m256i Db = _mm256_loadu_si256((const __m256i *)(Db_ptr + 4 * i));
    __m256i Kb = _mm256_loadu_si256((const __m256i *)(Kb_ptr + 4 * i));
    __m256i Da_or_Db = _mm256_or_si256(Da, Db);
    __m256i Ka_and_Kb = _mm256_and_si256(Ka, Kb);
    __m256i Kb_and_Db = _mm256_and_si256(Kb, Db);
    __m256i Ka_and_Da = _mm256_and_si256(Ka, Da);
    __m256i known =
        _mm256_or_si256(Ka_and_Kb, _mm256_or_si256(Kb_and_Db, Ka_and_Da));
    __m256i data = _mm256_or_si256(_mm256_and_si256(Da_or_Db, Ka_and_Kb),
                                   _mm256_or_si256(Kb_and_Db, Ka_and_Da));
    _mm256_storeu_si256((__m256i *)(dst_known + 4 * i), known);
    _mm256_storeu_si256((__m256i *)(dst_data + 4 * i), data);
  }
}

static inline void armlet_bitvector_xor_simd_inner_avx2(
    uint64_t *dst_data, uint64_t *dst_known, const uint64_t *Da_ptr,
    const uint64_t *Ka_ptr, const uint64_t *Db_ptr, const uint64_t *Kb_ptr,
    size_t blocks4) {
  for (size_t i = 0; i < blocks4; ++i) {
    __m256i Da = _mm256_loadu_si256((const __m256i *)(Da_ptr + 4 * i));
    __m256i Ka = _mm256_loadu_si256((const __m256i *)(Ka_ptr + 4 * i));
    __m256i Db = _mm256_loadu_si256((const __m256i *)(Db_ptr + 4 * i));
    __m256i Kb = _mm256_loadu_si256((const __m256i *)(Kb_ptr + 4 * i));
    __m256i known = _mm256_and_si256(Ka, Kb);
    __m256i data = _mm256_and_si256(_mm256_xor_si256(Da, Db), known);
    _mm256_storeu_si256((__m256i *)(dst_known + 4 * i), known);
    _mm256_storeu_si256((__m256i *)(dst_data + 4 * i), data);
  }
}

static inline void armlet_bitvector_not_simd_inner_avx2(uint64_t *dst_data,
                                                        uint64_t *dst_known,
                                                        const uint64_t *Da_ptr,
                                                        const uint64_t *Ka_ptr,
                                                        size_t blocks4) {
  for (size_t i = 0; i < blocks4; ++i) {
    __m256i Da = _mm256_loadu_si256((const __m256i *)(Da_ptr + 4 * i));
    __m256i Ka = _mm256_loadu_si256((const __m256i *)(Ka_ptr + 4 * i));
    __m256i data = _mm256_andnot_si256(Da, Ka); // (~Da) & Ka
    _mm256_storeu_si256((__m256i *)(dst_known + 4 * i), Ka);
    _mm256_storeu_si256((__m256i *)(dst_data + 4 * i), data);
  }
}

static inline void armlet_bitvector_copy_simd_inner_avx2(
    uint64_t *dst_data, uint64_t *dst_known, const uint64_t *src_data,
    const uint64_t *src_known, size_t blocks4) {
  for (size_t i = 0; i < blocks4; ++i) {
    __m256i d = _mm256_loadu_si256((const __m256i *)(src_data + 4 * i));
    __m256i k = _mm256_loadu_si256((const __m256i *)(src_known + 4 * i));
    _mm256_storeu_si256((__m256i *)(dst_data + 4 * i), d);
    _mm256_storeu_si256((__m256i *)(dst_known + 4 * i), k);
  }
}
#endif // __AVX2__

#ifdef __aarch64__
static inline void armlet_bitvector_and_simd_inner_neon(
    uint64_t *dst_data, uint64_t *dst_known, const uint64_t *Da_ptr,
    const uint64_t *Ka_ptr, const uint64_t *Db_ptr, const uint64_t *Kb_ptr,
    size_t blocks2) {
  for (size_t i = 0; i < blocks2; ++i) {
    uint64x2_t Da = vld1q_u64(Da_ptr + 2 * i);
    uint64x2_t Ka = vld1q_u64(Ka_ptr + 2 * i);
    uint64x2_t Db = vld1q_u64(Db_ptr + 2 * i);
    uint64x2_t Kb = vld1q_u64(Kb_ptr + 2 * i);
    // notDa = Ka & ~Da  => vbicq_u64(Da, Ka)  (vbicq(a,b) == b & ~a)
    uint64x2_t notDa = vbicq_u64(Da, Ka);
    uint64x2_t notDb = vbicq_u64(Db, Kb);
    uint64x2_t Ka_and_Kb = vandq_u64(Ka, Kb);
    uint64x2_t known = vorrq_u64(Ka_and_Kb, vorrq_u64(notDb, notDa));
    uint64x2_t data = vandq_u64(vandq_u64(Da, Db), Ka_and_Kb);
    vst1q_u64(dst_known + 2 * i, known);
    vst1q_u64(dst_data + 2 * i, data);
  }
}

static inline void armlet_bitvector_or_simd_inner_neon(
    uint64_t *dst_data, uint64_t *dst_known, const uint64_t *Da_ptr,
    const uint64_t *Ka_ptr, const uint64_t *Db_ptr, const uint64_t *Kb_ptr,
    size_t blocks2) {
  for (size_t i = 0; i < blocks2; ++i) {
    uint64x2_t Da = vld1q_u64(Da_ptr + 2 * i);
    uint64x2_t Ka = vld1q_u64(Ka_ptr + 2 * i);
    uint64x2_t Db = vld1q_u64(Db_ptr + 2 * i);
    uint64x2_t Kb = vld1q_u64(Kb_ptr + 2 * i);
    uint64x2_t Da_or_Db = vorrq_u64(Da, Db);
    uint64x2_t Ka_and_Kb = vandq_u64(Ka, Kb);
    uint64x2_t Kb_and_Db = vandq_u64(Kb, Db);
    uint64x2_t Ka_and_Da = vandq_u64(Ka, Da);
    uint64x2_t known = vorrq_u64(Ka_and_Kb, vorrq_u64(Kb_and_Db, Ka_and_Da));
    uint64x2_t data = vorrq_u64(vandq_u64(Da_or_Db, Ka_and_Kb),
                                vorrq_u64(Kb_and_Db, Ka_and_Da));
    vst1q_u64(dst_known + 2 * i, known);
    vst1q_u64(dst_data + 2 * i, data);
  }
}

static inline void armlet_bitvector_xor_simd_inner_neon(
    uint64_t *dst_data, uint64_t *dst_known, const uint64_t *Da_ptr,
    const uint64_t *Ka_ptr, const uint64_t *Db_ptr, const uint64_t *Kb_ptr,
    size_t blocks2) {
  for (size_t i = 0; i < blocks2; ++i) {
    uint64x2_t Da = vld1q_u64(Da_ptr + 2 * i);
    uint64x2_t Ka = vld1q_u64(Ka_ptr + 2 * i);
    uint64x2_t Db = vld1q_u64(Db_ptr + 2 * i);
    uint64x2_t Kb = vld1q_u64(Kb_ptr + 2 * i);
    uint64x2_t known = vandq_u64(Ka, Kb);
    uint64x2_t data = vandq_u64(veorq_u64(Da, Db), known);
    vst1q_u64(dst_known + 2 * i, known);
    vst1q_u64(dst_data + 2 * i, data);
  }
}

static inline void armlet_bitvector_not_simd_inner_neon(uint64_t *dst_data,
                                                        uint64_t *dst_known,
                                                        const uint64_t *Da_ptr,
                                                        const uint64_t *Ka_ptr,
                                                        size_t blocks2) {
  for (size_t i = 0; i < blocks2; ++i) {
    uint64x2_t Da = vld1q_u64(Da_ptr + 2 * i);
    uint64x2_t Ka = vld1q_u64(Ka_ptr + 2 * i);
    uint64x2_t data = vbicq_u64(Da, Ka); // Ka & ~Da
    vst1q_u64(dst_known + 2 * i, Ka);
    vst1q_u64(dst_data + 2 * i, data);
  }
}

static inline void armlet_bitvector_copy_simd_inner_neon(
    uint64_t *dst_data, uint64_t *dst_known, const uint64_t *src_data,
    const uint64_t *src_known, size_t blocks2) {
  for (size_t i = 0; i < blocks2; ++i) {
    uint64x2_t d = vld1q_u64(src_data + 2 * i);
    uint64x2_t k = vld1q_u64(src_known + 2 * i);
    vst1q_u64(dst_data + 2 * i, d);
    vst1q_u64(dst_known + 2 * i, k);
  }
}
#endif // __aarch64__

void armlet_bitvector_and(armlet_bitvector *dst, const armlet_bitvector *a,
                          const armlet_bitvector *b);
void armlet_bitvector_or(armlet_bitvector *dst, const armlet_bitvector *a,
                         const armlet_bitvector *b);
void armlet_bitvector_xor(armlet_bitvector *dst, const armlet_bitvector *a,
                          const armlet_bitvector *b);
void armlet_bitvector_not(armlet_bitvector *dst, const armlet_bitvector *a);
void armlet_bitvector_copy(armlet_bitvector *dst, const armlet_bitvector *src);

int armlet_bitvector_equal_wildcard(const armlet_bitvector *a,
                                    const armlet_bitvector *b);

void armlet_bitvector_and_mode(armlet_bitvector *dst, const armlet_bitvector *a,
                               const armlet_bitvector *b,
                               enum armlet_wildcard_mode preserve_x);
void armlet_bitvector_or_mode(armlet_bitvector *dst, const armlet_bitvector *a,
                              const armlet_bitvector *b,
                              enum armlet_wildcard_mode preserve_x);

armlet_bitvector *armlet_bitvector_concat(armlet_bitvector *dst,
                                          const armlet_bitvector *a,
                                          const armlet_bitvector *b);
armlet_bitvector *armlet_bitvector_concat_alloc(const armlet_bitvector *a,
                                                const armlet_bitvector *b);

void armlet_bitvector_shift_left(armlet_bitvector *dst,
                                 const armlet_bitvector *src, size_t s,
                                 int fill_is_x, int fill_value);
void armlet_bitvector_shift_right(armlet_bitvector *dst,
                                  const armlet_bitvector *src, size_t s,
                                  int fill_is_x, int fill_value);
void armlet_bitvector_rotate_left(armlet_bitvector *dst,
                                  const armlet_bitvector *src, size_t k);
void armlet_bitvector_rotate_right(armlet_bitvector *dst,
                                   const armlet_bitvector *src, size_t k);

size_t armlet_bitvector_count_known(const armlet_bitvector *bv);
size_t armlet_bitvector_count_known_ones(const armlet_bitvector *bv);
size_t armlet_bitvector_count_known_zeros(const armlet_bitvector *bv);
size_t armlet_bitvector_count_unknowns(const armlet_bitvector *bv);

int armlet_bitvector_bitslurp(armlet_bitvector *src, armlet_bitvector **dsts, size_t num_dsts);

#endif
