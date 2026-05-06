#include "tristate.h"
#include "common.h"

armlet_bitvector *armlet_bitvector_new(size_t nbits) {
  armlet_bitvector *bv = (armlet_bitvector *)CHECKED_MALLOC(sizeof(armlet_bitvector));
  bv->nbits = nbits;
  bv->nwords = words_for_bits(nbits);
  bv->last_mask = last_word_mask(nbits);
  if (bv->nwords == 0) {
    bv->data = NULL;
    bv->known = NULL;
    return bv;
  }
  uint64_t *block = (uint64_t *)calloc(2 * bv->nwords, sizeof(uint64_t));
  if (!block) {
    free(bv);
    return NULL;
  }
  bv->data = block;
  bv->known = block + bv->nwords;
  return bv;
}

void armlet_bitvector_free(armlet_bitvector *bv) {
  if (!bv)
    return;
  free(bv->data);
  free(bv);
}

void armlet_bitvector_set_all_x(armlet_bitvector *bv) {
  if (bv->nwords == 0)
    return;
  memset(bv->data, 0, bv->nwords * sizeof(uint64_t));
  memset(bv->known, 0, bv->nwords * sizeof(uint64_t));
}

void armlet_bitvector_set_all_zero(armlet_bitvector *bv) {
  if (bv->nwords == 0)
    return;
  memset(bv->data, 0, bv->nwords * sizeof(uint64_t));
  for (size_t i = 0; i < bv->nwords; ++i)
    bv->known[i] = ~0ULL;
  armlet_bitvector_mask_last(bv, bv->data, bv->known);
}

void armlet_bitvector_set_all_one(armlet_bitvector *bv) {
  if (bv->nwords == 0)
    return;
  for (size_t i = 0; i < bv->nwords; ++i)
    bv->data[i] = ~0ULL;
  for (size_t i = 0; i < bv->nwords; ++i)
    bv->known[i] = ~0ULL;
  armlet_bitvector_mask_last(bv, bv->data, bv->known);
}

void armlet_bitvector_set_all_fill(armlet_bitvector *bv, int is_x, int value) {
  if (is_x)
    armlet_bitvector_set_all_x(bv);
  else if (value)
    armlet_bitvector_set_all_one(bv);
  else
    armlet_bitvector_set_all_zero(bv);
}

void armlet_bitvector_set_bit(armlet_bitvector *bv, size_t idx, int value,
                              int is_x) {
  assert(bv && idx < bv->nbits);
  size_t w = idx >> 6;
  unsigned shift = idx & 63;
  uint64_t bit = 1ULL << shift;
  if (is_x) {
    bv->known[w] &= ~bit;
    bv->data[w] &= ~bit;
  } else {
    bv->known[w] |= bit;
    if (value)
      bv->data[w] |= bit;
    else
      bv->data[w] &= ~bit;
  }
}

int armlet_bitvector_get_bit(const armlet_bitvector *bv, size_t idx,
                             int *is_x) {
  assert(bv && idx < bv->nbits);
  size_t w = idx >> 6;
  unsigned shift = idx & 63;
  uint64_t bit = (1ULL << shift);
  if (is_x)
    *is_x = ((bv->known[w] & bit) == 0);
  if ((bv->known[w] & bit) == 0)
    return 0;
  return ((bv->data[w] & bit) != 0);
}

int armlet_bitvector_from_string(armlet_bitvector *bv, const char *s) {
  if (!bv || !s)
    return -1;
  size_t cnt = 0;
  for (const char *p = s; *p; ++p) {
    char c = *p;
    if (c == '0' || c == '1' || c == 'x' || c == 'X' || c == '?')
      ++cnt;
    else if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '_')
      continue;
    else
      return -1;
  }
  if (cnt != bv->nbits)
    return -1;
  armlet_bitvector_set_all_x(bv);
  size_t idx = 0;
  for (const char *p = s; *p; ++p) {
    char c = *p;
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '_')
      continue;
    if (c == 'x' || c == 'X' || c == '?')
      armlet_bitvector_set_bit(bv, idx++, 0, 1);
    else if (c == '0')
      armlet_bitvector_set_bit(bv, idx++, 0, 0);
    else if (c == '1')
      armlet_bitvector_set_bit(bv, idx++, 1, 0);
    else
      return -1;
  }
  return 0;
}

void armlet_bitvector_to_string(const armlet_bitvector *bv, char *out) {
  for (size_t i = 0; i < bv->nbits; ++i) {
    size_t w = i >> 6;
    unsigned shift = i & 63;
    uint64_t bit = 1ULL << shift;
    if ((bv->known[w] & bit) == 0)
      out[i] = 'x';
    else
      out[i] = (bv->data[w] & bit) ? '1' : '0';
  }
  out[bv->nbits] = '\0';
}

char *armlet_bitvector_to_string_alloc(const armlet_bitvector *bv) {
  char *out = (char *)CHECKED_MALLOC(bv->nbits + 1);
  armlet_bitvector_to_string(bv, out);
  return out;
}

void armlet_bitvector_and_scalar(armlet_bitvector *dst,
                                 const armlet_bitvector *a,
                                 const armlet_bitvector *b) {
  assert(dst && a && b && dst->nbits == a->nbits && a->nbits == b->nbits);
  size_t n = a->nwords;
  for (size_t i = 0; i < n; ++i) {
    uint64_t Da = a->data[i], Ka = a->known[i];
    uint64_t Db = b->data[i], Kb = b->known[i];
    uint64_t known = (Ka & Kb) | (Kb & (~Db)) | (Ka & (~Da));
    uint64_t data = (Da & Db & Ka & Kb);
    dst->known[i] = known;
    dst->data[i] = data;
  }
  armlet_bitvector_mask_last(dst, dst->data, dst->known);
}

void armlet_bitvector_or_scalar(armlet_bitvector *dst,
                                const armlet_bitvector *a,
                                const armlet_bitvector *b) {
  assert(dst && a && b && dst->nbits == a->nbits && a->nbits == b->nbits);
  size_t n = a->nwords;
  for (size_t i = 0; i < n; ++i) {
    uint64_t Da = a->data[i], Ka = a->known[i];
    uint64_t Db = b->data[i], Kb = b->known[i];
    uint64_t known = (Ka & Kb) | (Kb & Db) | (Ka & Da);
    uint64_t data = ((Da | Db) & (Ka & Kb)) | (Kb & Db) | (Ka & Da);
    dst->known[i] = known;
    dst->data[i] = data;
  }
  armlet_bitvector_mask_last(dst, dst->data, dst->known);
}

void armlet_bitvector_xor_scalar(armlet_bitvector *dst,
                                 const armlet_bitvector *a,
                                 const armlet_bitvector *b) {
  assert(dst && a && b && dst->nbits == a->nbits && a->nbits == b->nbits);
  size_t n = a->nwords;
  for (size_t i = 0; i < n; ++i) {
    uint64_t Da = a->data[i], Ka = a->known[i];
    uint64_t Db = b->data[i], Kb = b->known[i];
    uint64_t known = (Ka & Kb);
    uint64_t data = ((Da ^ Db) & known);
    dst->known[i] = known;
    dst->data[i] = data;
  }
  armlet_bitvector_mask_last(dst, dst->data, dst->known);
}

void armlet_bitvector_not_scalar(armlet_bitvector *dst,
                                 const armlet_bitvector *a) {
  assert(dst && a && dst->nbits == a->nbits);
  size_t n = a->nwords;
  for (size_t i = 0; i < n; ++i) {
    uint64_t Da = a->data[i], Ka = a->known[i];
    dst->known[i] = Ka;
    dst->data[i] = (~Da) & Ka;
  }
  armlet_bitvector_mask_last(dst, dst->data, dst->known);
}

void armlet_bitvector_copy_scalar(armlet_bitvector *dst,
                                  const armlet_bitvector *src) {
  assert(dst && src && dst->nbits == src->nbits);
  if (src->nwords == 0)
    return;
  memcpy(dst->data, src->data, src->nwords * sizeof(uint64_t));
  memcpy(dst->known, src->known, src->nwords * sizeof(uint64_t));
  armlet_bitvector_mask_last(dst, dst->data, dst->known);
}

void armlet_bitvector_and(armlet_bitvector *dst, const armlet_bitvector *a,
                          const armlet_bitvector *b) {
  assert(dst && a && b && dst->nbits == a->nbits && a->nbits == b->nbits);
  size_t n = a->nwords;
#ifdef __AVX2__
  size_t blocks4 = n / 4;
  size_t tail = n % 4;
  if (blocks4)
    armlet_bitvector_and_simd_inner_avx2(dst->data, dst->known, a->data,
                                         a->known, b->data, b->known, blocks4);
  if (tail) {
    size_t start = blocks4 * 4;
    for (size_t i = start; i < n; ++i) {
      uint64_t Da = a->data[i], Ka = a->known[i];
      uint64_t Db = b->data[i], Kb = b->known[i];
      uint64_t known = (Ka & Kb) | (Kb & (~Db)) | (Ka & (~Da));
      uint64_t data = (Da & Db & Ka & Kb);
      dst->known[i] = known;
      dst->data[i] = data;
    }
  }
#elif defined(__aarch64__)
  size_t blocks2 = n / 2;
  size_t tail = n % 2;
  if (blocks2)
    armlet_bitvector_and_simd_inner_neon(dst->data, dst->known, a->data,
                                         a->known, b->data, b->known, blocks2);
  if (tail) {
    size_t start = blocks2 * 2;
    for (size_t i = start; i < n; ++i) {
      uint64_t Da = a->data[i], Ka = a->known[i];
      uint64_t Db = b->data[i], Kb = b->known[i];
      uint64_t known = (Ka & Kb) | (Kb & (~Db)) | (Ka & (~Da));
      uint64_t data = (Da & Db & Ka & Kb);
      dst->known[i] = known;
      dst->data[i] = data;
    }
  }
#else
  armlet_bitvector_and_scalar(dst, a, b);
#endif
  armlet_bitvector_mask_last(dst, dst->data, dst->known);
}

void armlet_bitvector_or(armlet_bitvector *dst, const armlet_bitvector *a,
                         const armlet_bitvector *b) {
  assert(dst && a && b && dst->nbits == a->nbits && a->nbits == b->nbits);
  size_t n = a->nwords;
#ifdef __AVX2__
  size_t blocks4 = n / 4;
  size_t tail = n % 4;
  if (blocks4)
    armlet_bitvector_or_simd_inner_avx2(dst->data, dst->known, a->data,
                                        a->known, b->data, b->known, blocks4);
  if (tail) {
    size_t start = blocks4 * 4;
    for (size_t i = start; i < n; ++i) {
      uint64_t Da = a->data[i], Ka = a->known[i];
      uint64_t Db = b->data[i], Kb = b->known[i];
      uint64_t known = (Ka & Kb) | (Kb & Db) | (Ka & Da);
      uint64_t data = ((Da | Db) & (Ka & Kb)) | (Kb & Db) | (Ka & Da);
      dst->known[i] = known;
      dst->data[i] = data;
    }
  }
#elif defined(__aarch64__)
  size_t blocks2 = n / 2;
  size_t tail = n % 2;
  if (blocks2)
    armlet_bitvector_or_simd_inner_neon(dst->data, dst->known, a->data,
                                        a->known, b->data, b->known, blocks2);
  if (tail) {
    size_t start = blocks2 * 2;
    for (size_t i = start; i < n; ++i) {
      uint64_t Da = a->data[i], Ka = a->known[i];
      uint64_t Db = b->data[i], Kb = b->known[i];
      uint64_t known = (Ka & Kb) | (Kb & Db) | (Ka & Da);
      uint64_t data = ((Da | Db) & (Ka & Kb)) | (Kb & Db) | (Ka & Da);
      dst->known[i] = known;
      dst->data[i] = data;
    }
  }
#else
  armlet_bitvector_or_scalar(dst, a, b);
#endif
  armlet_bitvector_mask_last(dst, dst->data, dst->known);
}

void armlet_bitvector_xor(armlet_bitvector *dst, const armlet_bitvector *a,
                          const armlet_bitvector *b) {
  assert(dst && a && b && dst->nbits == a->nbits && a->nbits == b->nbits);
  size_t n = a->nwords;
#ifdef __AVX2__
  size_t blocks4 = n / 4;
  size_t tail = n % 4;
  if (blocks4)
    armlet_bitvector_xor_simd_inner_avx2(dst->data, dst->known, a->data,
                                         a->known, b->data, b->known, blocks4);
  if (tail) {
    size_t start = blocks4 * 4;
    for (size_t i = start; i < n; ++i) {
      uint64_t Da = a->data[i], Ka = a->known[i];
      uint64_t Db = b->data[i], Kb = b->known[i];
      uint64_t known = (Ka & Kb);
      uint64_t data = ((Da ^ Db) & known);
      dst->known[i] = known;
      dst->data[i] = data;
    }
  }
#elif defined(__aarch64__)
  size_t blocks2 = n / 2;
  size_t tail = n % 2;
  if (blocks2)
    armlet_bitvector_xor_simd_inner_neon(dst->data, dst->known, a->data,
                                         a->known, b->data, b->known, blocks2);
  if (tail) {
    size_t start = blocks2 * 2;
    for (size_t i = start; i < n; ++i) {
      uint64_t Da = a->data[i], Ka = a->known[i];
      uint64_t Db = b->data[i], Kb = b->known[i];
      uint64_t known = (Ka & Kb);
      uint64_t data = ((Da ^ Db) & known);
      dst->known[i] = known;
      dst->data[i] = data;
    }
  }
#else
  armlet_bitvector_xor_scalar(dst, a, b);
#endif
  armlet_bitvector_mask_last(dst, dst->data, dst->known);
}

void armlet_bitvector_not(armlet_bitvector *dst, const armlet_bitvector *a) {
  assert(dst && a && dst->nbits == a->nbits);
  size_t n = a->nwords;
#ifdef __AVX2__
  size_t blocks4 = n / 4;
  size_t tail = n % 4;
  if (blocks4)
    armlet_bitvector_not_simd_inner_avx2(dst->data, dst->known, a->data,
                                         a->known, blocks4);
  if (tail) {
    size_t start = blocks4 * 4;
    for (size_t i = start; i < n; ++i) {
      uint64_t Da = a->data[i], Ka = a->known[i];
      dst->known[i] = Ka;
      dst->data[i] = (~Da) & Ka;
    }
  }
#elif defined(__aarch64__)
  size_t blocks2 = n / 2;
  size_t tail = n % 2;
  if (blocks2)
    armlet_bitvector_not_simd_inner_neon(dst->data, dst->known, a->data,
                                         a->known, blocks2);
  if (tail) {
    size_t start = blocks2 * 2;
    for (size_t i = start; i < n; ++i) {
      uint64_t Da = a->data[i], Ka = a->known[i];
      dst->known[i] = Ka;
      dst->data[i] = (~Da) & Ka;
    }
  }
#else
  armlet_bitvector_not_scalar(dst, a);
#endif
  armlet_bitvector_mask_last(dst, dst->data, dst->known);
}

void armlet_bitvector_copy(armlet_bitvector *dst, const armlet_bitvector *src) {
  assert(dst && src && dst->nbits == src->nbits);
  size_t n = src->nwords;
#ifdef __AVX2__
  size_t blocks4 = n / 4;
  size_t tail = n % 4;
  if (blocks4)
    armlet_bitvector_copy_simd_inner_avx2(dst->data, dst->known, src->data,
                                          src->known, blocks4);
  if (tail) {
    size_t start = blocks4 * 4;
    for (size_t i = start; i < n; ++i) {
      dst->data[i] = src->data[i];
      dst->known[i] = src->known[i];
    }
  }
#elif defined(__aarch64__)
  size_t blocks2 = n / 2;
  size_t tail = n % 2;
  if (blocks2)
    armlet_bitvector_copy_simd_inner_neon(dst->data, dst->known, src->data,
                                          src->known, blocks2);
  if (tail) {
    size_t start = blocks2 * 2;
    for (size_t i = start; i < n; ++i) {
      dst->data[i] = src->data[i];
      dst->known[i] = src->known[i];
    }
  }
#else
  armlet_bitvector_copy_scalar(dst, src);
#endif
  armlet_bitvector_mask_last(dst, dst->data, dst->known);
}

int armlet_bitvector_equal_wildcard(const armlet_bitvector *a,
                                    const armlet_bitvector *b) {
  assert(a && b && a->nbits == b->nbits);
  size_t n = a->nwords;
#ifdef __AVX2__
  size_t blocks4 = n / 4;
  size_t tail = n % 4;
  for (size_t i = 0; i < blocks4; ++i) {
    __m256i Da = _mm256_loadu_si256((const __m256i *)(a->data + 4 * i));
    __m256i Db = _mm256_loadu_si256((const __m256i *)(b->data + 4 * i));
    __m256i Ka = _mm256_loadu_si256((const __m256i *)(a->known + 4 * i));
    __m256i Kb = _mm256_loadu_si256((const __m256i *)(b->known + 4 * i));
    __m256i diff =
        _mm256_and_si256(_mm256_xor_si256(Da, Db), _mm256_and_si256(Ka, Kb));
    if (!_mm256_testz_si256(diff, diff))
      return 0;
  }
  if (tail) {
    size_t start = blocks4 * 4;
    for (size_t i = start; i < n; ++i) {
      uint64_t diff = (a->data[i] ^ b->data[i]) & (a->known[i] & b->known[i]);
      if (diff)
        return 0;
    }
  }
  return 1;
#elif defined(__aarch64__)
  size_t blocks2 = n / 2;
  size_t tail = n % 2;
  for (size_t i = 0; i < blocks2; ++i) {
    uint64x2_t Da = vld1q_u64(a->data + 2 * i);
    uint64x2_t Db = vld1q_u64(b->data + 2 * i);
    uint64x2_t Ka = vld1q_u64(a->known + 2 * i);
    uint64x2_t Kb = vld1q_u64(b->known + 2 * i);
    uint64x2_t diff = vandq_u64(veorq_u64(Da, Db), vandq_u64(Ka, Kb));
    uint64_t d0 = vgetq_lane_u64(diff, 0);
    uint64_t d1 = vgetq_lane_u64(diff, 1);
    if ((d0 | d1) != 0)
      return 0;
  }
  if (tail) {
    size_t start = blocks2 * 2;
    for (size_t i = start; i < n; ++i) {
      uint64_t diff = (a->data[i] ^ b->data[i]) & (a->known[i] & b->known[i]);
      if (diff)
        return 0;
    }
  }
  return 1;
#else
  for (size_t i = 0; i < n; ++i) {
    uint64_t diff = (a->data[i] ^ b->data[i]) & (a->known[i] & b->known[i]);
    if (diff)
      return 0;
  }
  return 1;
#endif
}


static void armlet_bitvector_and_preserve_scalar(armlet_bitvector *dst,
                                                 const armlet_bitvector *a,
                                                 const armlet_bitvector *b) {
  assert(dst && a && b && dst->nbits == a->nbits && a->nbits == b->nbits);
  size_t n = a->nwords;
  for (size_t i = 0; i < n; ++i) {
    uint64_t Da = a->data[i], Ka = a->known[i];
    uint64_t Db = b->data[i], Kb = b->known[i];
    uint64_t known = Ka & Kb;
    uint64_t data = (Da & Db) & known;
    dst->known[i] = known;
    dst->data[i] = data;
  }
  armlet_bitvector_mask_last(dst, dst->data, dst->known);
}

static void armlet_bitvector_or_preserve_scalar(armlet_bitvector *dst,
                                                const armlet_bitvector *a,
                                                const armlet_bitvector *b) {
  assert(dst && a && b && dst->nbits == a->nbits && a->nbits == b->nbits);
  size_t n = a->nwords;
  for (size_t i = 0; i < n; ++i) {
    uint64_t Da = a->data[i], Ka = a->known[i];
    uint64_t Db = b->data[i], Kb = b->known[i];
    uint64_t known = Ka & Kb;
    uint64_t data = ((Da | Db) & known);
    dst->known[i] = known;
    dst->data[i] = data;
  }
  armlet_bitvector_mask_last(dst, dst->data, dst->known);
}

#ifdef __AVX2__
static void armlet_bitvector_and_preserve_avx2(
    uint64_t *dst_data, uint64_t *dst_known, const uint64_t *Da_ptr,
    const uint64_t *Ka_ptr, const uint64_t *Db_ptr, const uint64_t *Kb_ptr,
    size_t blocks4) {
  for (size_t i = 0; i < blocks4; ++i) {
    __m256i Da = _mm256_loadu_si256((const __m256i *)(Da_ptr + 4 * i));
    __m256i Ka = _mm256_loadu_si256((const __m256i *)(Ka_ptr + 4 * i));
    __m256i Db = _mm256_loadu_si256((const __m256i *)(Db_ptr + 4 * i));
    __m256i Kb = _mm256_loadu_si256((const __m256i *)(Kb_ptr + 4 * i));
    __m256i known = _mm256_and_si256(Ka, Kb);
    __m256i data = _mm256_and_si256(_mm256_and_si256(Da, Db), known);
    _mm256_storeu_si256((__m256i *)(dst_known + 4 * i), known);
    _mm256_storeu_si256((__m256i *)(dst_data + 4 * i), data);
  }
}

static void armlet_bitvector_or_preserve_avx2(
    uint64_t *dst_data, uint64_t *dst_known, const uint64_t *Da_ptr,
    const uint64_t *Ka_ptr, const uint64_t *Db_ptr, const uint64_t *Kb_ptr,
    size_t blocks4) {
  for (size_t i = 0; i < blocks4; ++i) {
    __m256i Da = _mm256_loadu_si256((const __m256i *)(Da_ptr + 4 * i));
    __m256i Ka = _mm256_loadu_si256((const __m256i *)(Ka_ptr + 4 * i));
    __m256i Db = _mm256_loadu_si256((const __m256i *)(Db_ptr + 4 * i));
    __m256i Kb = _mm256_loadu_si256((const __m256i *)(Kb_ptr + 4 * i));
    __m256i known = _mm256_and_si256(Ka, Kb);
    __m256i data = _mm256_and_si256(_mm256_or_si256(Da, Db), known);
    _mm256_storeu_si256((__m256i *)(dst_known + 4 * i), known);
    _mm256_storeu_si256((__m256i *)(dst_data + 4 * i), data);
  }
}
#endif

#ifdef __aarch64__
static void armlet_bitvector_and_preserve_neon(
    uint64_t *dst_data, uint64_t *dst_known, const uint64_t *Da_ptr,
    const uint64_t *Ka_ptr, const uint64_t *Db_ptr, const uint64_t *Kb_ptr,
    size_t blocks2) {
  for (size_t i = 0; i < blocks2; ++i) {
    uint64x2_t Da = vld1q_u64(Da_ptr + 2 * i);
    uint64x2_t Ka = vld1q_u64(Ka_ptr + 2 * i);
    uint64x2_t Db = vld1q_u64(Db_ptr + 2 * i);
    uint64x2_t Kb = vld1q_u64(Kb_ptr + 2 * i);
    uint64x2_t known = vandq_u64(Ka, Kb);
    uint64x2_t data = vandq_u64(vandq_u64(Da, Db), known);
    vst1q_u64(dst_known + 2 * i, known);
    vst1q_u64(dst_data + 2 * i, data);
  }
}

static void armlet_bitvector_or_preserve_neon(
    uint64_t *dst_data, uint64_t *dst_known, const uint64_t *Da_ptr,
    const uint64_t *Ka_ptr, const uint64_t *Db_ptr, const uint64_t *Kb_ptr,
    size_t blocks2) {
  for (size_t i = 0; i < blocks2; ++i) {
    uint64x2_t Da = vld1q_u64(Da_ptr + 2 * i);
    uint64x2_t Ka = vld1q_u64(Ka_ptr + 2 * i);
    uint64x2_t Db = vld1q_u64(Db_ptr + 2 * i);
    uint64x2_t Kb = vld1q_u64(Kb_ptr + 2 * i);
    uint64x2_t known = vandq_u64(Ka, Kb);
    uint64x2_t data = vandq_u64(vorrq_u64(Da, Db), known);
    vst1q_u64(dst_known + 2 * i, known);
    vst1q_u64(dst_data + 2 * i, data);
  }
}
#endif

void armlet_bitvector_and_mode(armlet_bitvector *dst, const armlet_bitvector *a,
                               const armlet_bitvector *b,
                               enum armlet_wildcard_mode preserve_x) {
  assert(dst && a && b && dst->nbits == a->nbits && a->nbits == b->nbits);
  if (preserve_x == WILDCARD_RESULT) {
    armlet_bitvector_and(dst, a, b);
    return;
  }
  size_t n = a->nwords;
#ifdef __AVX2__
  size_t blocks4 = n / 4;
  size_t tail = n % 4;
  if (blocks4)
    armlet_bitvector_and_preserve_avx2(dst->data, dst->known, a->data, a->known,
                                       b->data, b->known, blocks4);
  if (tail) {
    size_t start = blocks4 * 4;
    for (size_t i = start; i < n; ++i) {
      uint64_t known = a->known[i] & b->known[i];
      uint64_t data = (a->data[i] & b->data[i]) & known;
      dst->known[i] = known;
      dst->data[i] = data;
    }
  }
#elif defined(__aarch64__)
  size_t blocks2 = n / 2;
  size_t tail = n % 2;
  if (blocks2)
    armlet_bitvector_and_preserve_neon(dst->data, dst->known, a->data, a->known,
                                       b->data, b->known, blocks2);
  if (tail) {
    size_t start = blocks2 * 2;
    for (size_t i = start; i < n; ++i) {
      uint64_t known = a->known[i] & b->known[i];
      uint64_t data = (a->data[i] & b->data[i]) & known;
      dst->known[i] = known;
      dst->data[i] = data;
    }
  }
#else
  armlet_bitvector_and_preserve_scalar(dst, a, b);
#endif
  armlet_bitvector_mask_last(dst, dst->data, dst->known);
}

void armlet_bitvector_or_mode(armlet_bitvector *dst, const armlet_bitvector *a,
                              const armlet_bitvector *b,
                              enum armlet_wildcard_mode preserve_x) {
  assert(dst && a && b && dst->nbits == a->nbits && a->nbits == b->nbits);
  if (preserve_x == WILDCARD_RESULT) {
    armlet_bitvector_or(dst, a, b);
    return;
  }
  size_t n = a->nwords;
#ifdef __AVX2__
  size_t blocks4 = n / 4;
  size_t tail = n % 4;
  if (blocks4)
    armlet_bitvector_or_preserve_avx2(dst->data, dst->known, a->data, a->known,
                                      b->data, b->known, blocks4);
  if (tail) {
    size_t start = blocks4 * 4;
    for (size_t i = start; i < n; ++i) {
      uint64_t known = a->known[i] & b->known[i];
      uint64_t data = ((a->data[i] | b->data[i]) & known);
      dst->known[i] = known;
      dst->data[i] = data;
    }
  }
#elif defined(__aarch64__)
  size_t blocks2 = n / 2;
  size_t tail = n % 2;
  if (blocks2)
    armlet_bitvector_or_preserve_neon(dst->data, dst->known, a->data, a->known,
                                      b->data, b->known, blocks2);
  if (tail) {
    size_t start = blocks2 * 2;
    for (size_t i = start; i < n; ++i) {
      uint64_t known = a->known[i] & b->known[i];
      uint64_t data = ((a->data[i] | b->data[i]) & known);
      dst->known[i] = known;
      dst->data[i] = data;
    }
  }
#else
  armlet_bitvector_or_preserve_scalar(dst, a, b);
#endif
  armlet_bitvector_mask_last(dst, dst->data, dst->known);
}

armlet_bitvector *armlet_bitvector_concat(armlet_bitvector *dst,
                                          const armlet_bitvector *a,
                                          const armlet_bitvector *b) {
  assert(a && b && dst);

  memset(dst->data, 0, dst->nwords * sizeof(uint64_t));
  memset(dst->known, 0, dst->nwords * sizeof(uint64_t));

  for (size_t i = 0; i < a->nwords; ++i) {
    dst->data[i] |= a->data[i];
    dst->known[i] |= a->known[i];
  }

  size_t s = a->nbits;
  size_t wshift = s >> 6;
  unsigned bshift = s & 63;
  for (size_t i = 0; i < b->nwords; ++i) {
    size_t tgt = i + wshift;
    if (tgt < dst->nwords) {
      if (bshift == 0) {
        dst->data[tgt] |= b->data[i];
        dst->known[tgt] |= b->known[i];
      } else {
        dst->data[tgt] |= (b->data[i] << bshift);
        dst->known[tgt] |= (b->known[i] << bshift);
        if (tgt + 1 < dst->nwords) {
          dst->data[tgt + 1] |= (b->data[i] >> (64 - bshift));
          dst->known[tgt + 1] |= (b->known[i] >> (64 - bshift));
        }
      }
    }
  }

  armlet_bitvector_mask_last(dst, dst->data, dst->known);

  return dst;
}

armlet_bitvector *armlet_bitvector_concat_alloc(const armlet_bitvector *a,
                                                const armlet_bitvector *b) {
  assert(a && b);

  size_t total = a->nbits + b->nbits;

  armlet_bitvector *dst = armlet_bitvector_new(total);
  if (!dst)
    return NULL;

  return armlet_bitvector_concat(dst, a, b);
}

void armlet_bitvector_shift_left(armlet_bitvector *dst,
                                 const armlet_bitvector *src, size_t s,
                                 int fill_is_x, int fill_value) {
  assert(dst && src && dst->nbits == src->nbits);
  if (s == 0) {
    armlet_bitvector_copy(dst, src);
    return;
  }
  if (src->nbits == 0)
    return;
  if (s >= src->nbits) {
    armlet_bitvector_set_all_fill(dst, fill_is_x, fill_value);
    return;
  }

  size_t wshift = s >> 6;
  unsigned bshift = s & 63;
  memset(dst->data, 0, dst->nwords * sizeof(uint64_t));
  memset(dst->known, 0, dst->nwords * sizeof(uint64_t));

  for (size_t i = 0; i < src->nwords; ++i) {
    size_t tgt = i + wshift;
    if (tgt < dst->nwords) {
      if (bshift == 0) {
        dst->data[tgt] |= src->data[i];
        dst->known[tgt] |= src->known[i];
      } else {
        dst->data[tgt] |= (src->data[i] << bshift);
        dst->known[tgt] |= (src->known[i] << bshift);
        if (tgt + 1 < dst->nwords) {
          dst->data[tgt + 1] |= (src->data[i] >> (64 - bshift));
          dst->known[tgt + 1] |= (src->known[i] >> (64 - bshift));
        }
      }
    }
  }

  // fill low bits [0 .. s-1] with fill
  if (fill_is_x) {
    for (size_t bit = 0; bit < s; ++bit) {
      size_t w = bit >> 6;
      unsigned sh = bit & 63;
      dst->known[w] &= ~(1ULL << sh);
      dst->data[w] &= ~(1ULL << sh);
    }
  } else {
    for (size_t bit = 0; bit < s; ++bit) {
      size_t w = bit >> 6;
      unsigned sh = bit & 63;
      dst->known[w] |= (1ULL << sh);
      if (fill_value)
        dst->data[w] |= (1ULL << sh);
      else
        dst->data[w] &= ~(1ULL << sh);
    }
  }

  armlet_bitvector_mask_last(dst, dst->data, dst->known);
}

void armlet_bitvector_shift_right(armlet_bitvector *dst,
                                  const armlet_bitvector *src, size_t s,
                                  int fill_is_x, int fill_value) {
  assert(dst && src && dst->nbits == src->nbits);
  if (s == 0) {
    armlet_bitvector_copy(dst, src);
    return;
  }
  if (src->nbits == 0)
    return;
  if (s >= src->nbits) {
    armlet_bitvector_set_all_fill(dst, fill_is_x, fill_value);
    return;
  }

  size_t wshift = s >> 6;
  unsigned bshift = s & 63;
  memset(dst->data, 0, dst->nwords * sizeof(uint64_t));
  memset(dst->known, 0, dst->nwords * sizeof(uint64_t));

  for (size_t i = 0; i < src->nwords; ++i) {
    if (i >= wshift) {
      size_t tgt = i - wshift;
      if (bshift == 0) {
        dst->data[tgt] |= src->data[i];
        dst->known[tgt] |= src->known[i];
      } else {
        dst->data[tgt] |= (src->data[i] >> bshift);
        dst->known[tgt] |= (src->known[i] >> bshift);
        if (tgt > 0) {
          dst->data[tgt - 1] |= (src->data[i] << (64 - bshift));
          dst->known[tgt - 1] |= (src->known[i] << (64 - bshift));
        }
      }
    }
  }

  size_t start = dst->nbits - s;
  if (fill_is_x) {
    for (size_t bit = start; bit < dst->nbits; ++bit) {
      size_t w = bit >> 6;
      unsigned sh = bit & 63;
      dst->known[w] &= ~(1ULL << sh);
      dst->data[w] &= ~(1ULL << sh);
    }
  } else {
    for (size_t bit = start; bit < dst->nbits; ++bit) {
      size_t w = bit >> 6;
      unsigned sh = bit & 63;
      dst->known[w] |= (1ULL << sh);
      if (fill_value)
        dst->data[w] |= (1ULL << sh);
      else
        dst->data[w] &= ~(1ULL << sh);
    }
  }

  armlet_bitvector_mask_last(dst, dst->data, dst->known);
}

void armlet_bitvector_rotate_left(armlet_bitvector *dst,
                                  const armlet_bitvector *src, size_t k) {
  assert(dst && src && dst->nbits == src->nbits);
  if (src->nbits == 0)
    return;
  k %= src->nbits;
  if (k == 0) {
    armlet_bitvector_copy(dst, src);
    return;
  }
  armlet_bitvector *t1 = armlet_bitvector_new(src->nbits);
  armlet_bitvector *t2 = armlet_bitvector_new(src->nbits);
  if (!t1 || !t2) {
    armlet_bitvector_free(t1);
    armlet_bitvector_free(t2);
    return;
  }
  armlet_bitvector_shift_left(t1, src, k, 0, 0);
  armlet_bitvector_shift_right(t2, src, src->nbits - k, 0, 0);
  armlet_bitvector_or(dst, t1, t2);
  armlet_bitvector_free(t1);
  armlet_bitvector_free(t2);
}

void armlet_bitvector_rotate_right(armlet_bitvector *dst,
                                   const armlet_bitvector *src, size_t k) {
  assert(dst && src && dst->nbits == src->nbits);
  if (src->nbits == 0)
    return;
  k %= src->nbits;
  if (k == 0) {
    armlet_bitvector_copy(dst, src);
    return;
  }
  armlet_bitvector *t1 = armlet_bitvector_new(src->nbits);
  armlet_bitvector *t2 = armlet_bitvector_new(src->nbits);
  if (!t1 || !t2) {
    armlet_bitvector_free(t1);
    armlet_bitvector_free(t2);
    return;
  }
  armlet_bitvector_shift_right(t1, src, k, 0, 0);
  armlet_bitvector_shift_left(t2, src, src->nbits - k, 0, 0);
  armlet_bitvector_or(dst, t1, t2);
  armlet_bitvector_free(t1);
  armlet_bitvector_free(t2);
}

size_t armlet_bitvector_count_known(const armlet_bitvector *bv) {
  assert(bv);
  size_t cnt = 0;
  for (size_t i = 0; i < bv->nwords; ++i)
    cnt += __builtin_popcountll(bv->known[i]);
  return cnt;
}

size_t armlet_bitvector_count_known_ones(const armlet_bitvector *bv) {
  assert(bv);
  size_t cnt = 0;
  for (size_t i = 0; i < bv->nwords; ++i)
    cnt += __builtin_popcountll(bv->data[i] & bv->known[i]);
  return cnt;
}

size_t armlet_bitvector_count_known_zeros(const armlet_bitvector *bv) {
  assert(bv);
  size_t k = armlet_bitvector_count_known(bv);
  size_t ones = armlet_bitvector_count_known_ones(bv);
  return k - ones;
}

size_t armlet_bitvector_count_unknowns(const armlet_bitvector *bv) {
  assert(bv);
  return bv->nbits - armlet_bitvector_count_known(bv);
}

int armlet_bitvector_bitslurp(armlet_bitvector *src, armlet_bitvector **dsts, size_t num_dsts) {
    if (!src || !dsts || num_dsts == 0) return -1;

    size_t total = 0;
    for (size_t i = 0; i < num_dsts; ++i) {
        if (!dsts[i]) return -1;
        total += dsts[i]->nbits;
    }

    if (total != src->nbits) return -2;

    if (src->nbits == 0) return 0;

    size_t src_bit_pos = 0;

    for (size_t di = 0; di < num_dsts; ++di) {
        armlet_bitvector *dst = dsts[di];
        size_t n = dst->nbits;
        if (n == 0) { continue; }

        memset(dst->data, 0, dst->nwords * sizeof(uint64_t));
        memset(dst->known, 0, dst->nwords * sizeof(uint64_t));

        size_t ws = src_bit_pos >> 6;
        unsigned bs = src_bit_pos & 63;

        for (size_t w = 0; w < dst->nwords; ++w) {
            size_t src_idx = ws + w;

            uint64_t low_data = 0, low_known = 0;
            uint64_t high_data = 0, high_known = 0;

            if (src_idx < src->nwords) {
                low_data = src->data[src_idx];
                low_known = src->known[src_idx];
            }
            if ((src_idx + 1) < src->nwords) {
                high_data = src->data[src_idx + 1];
                high_known = src->known[src_idx + 1];
            }

            uint64_t out_data, out_known;
            if (bs == 0) {
                out_data  = low_data;
                out_known = low_known;
            } else {
                out_data  = (low_data >> bs) | (high_data << (64 - bs));
                out_known = (low_known >> bs) | (high_known << (64 - bs));
            }

            dst->data[w]  = out_data;
            dst->known[w] = out_known;
        }

        armlet_bitvector_mask_last(dst, dst->data, dst->known);

        src_bit_pos += n;
    }

    return 0;
}
