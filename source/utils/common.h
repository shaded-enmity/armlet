#ifndef __ARMLET_COMMON__
#define __ARMLET_COMMON__

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

extern char BAIL_BUFFER[4096];

#define DEFINE_ARRAY(_T, _N)                                                   \
  _T *_N;                                                                      \
  size_t num_##_N;                                                             \
  size_t cap_##_N;

#define ARR_APPEND(obj, arr, item)                                             \
  do {                                                                         \
    if ((obj)->num_##arr == (obj)->cap_##arr) {                                \
      size_t __newcap = (obj)->cap_##arr ? (obj)->cap_##arr * 2 : 8;           \
      if (__newcap < (obj)->cap_##arr)                                         \
        BAIL("Overflow in ARR_APPEND: %s:%d\n", __FILE__, __LINE__);           \
      void *__tmp = realloc((obj)->arr, __newcap * sizeof(*(obj)->arr));       \
      if (!__tmp)                                                              \
        BAIL("Out of memory: %s:%d\n", __FILE__, __LINE__);                    \
      (obj)->arr = __tmp;                                                      \
      (obj)->cap_##arr = __newcap;                                             \
    }                                                                          \
    (obj)->arr[(obj)->num_##arr] = item;                                       \
    (obj)->num_##arr += 1;                                                     \
  } while (0)

#define ARR_FOREACH(obj, arr, blk)                                             \
  do {                                                                         \
    for (size_t i = 0; i < (obj)->num_##arr; ++i) {                            \
      __typeof(*(obj)->arr) it = (obj)->arr[i];                                \
      (void)it;                                                                \
      blk                                                                      \
    }                                                                          \
  } while (0)

#define ARR_FOREACH_REVERSE(obj, arr, blk)                                     \
  do {                                                                         \
    for (size_t i = ((obj)->num_##arr); i > 0; --i) {                          \
      __typeof(*(obj)->arr) it = (obj)->arr[i - 1];                            \
      (void)it;                                                                \
      blk                                                                      \
    }                                                                          \
  } while (0)

#define PASTE(x) [x] = #x
#define PASTE_BM(x) [x] = (1 << (x))

#define MUL_NO_OVERFLOW ((size_t)1 << (sizeof(size_t) * 4))

#define ARMLET_VER_MAJOR 0
#define ARMLET_VER_MINOR 1
#define ARMLET_VER_REVISION 0

#define BAIL(...)                                                              \
  {                                                                            \
    snprintf(BAIL_BUFFER, sizeof(BAIL_BUFFER), __VA_ARGS__);                   \
    fputs(BAIL_BUFFER, stderr);                                                \
    abort();                                                                   \
  }

#define CHECKED_MALLOC(S)                                                      \
  ({                                                                           \
    void *__X = malloc((S));                                                   \
    if (__X == NULL)                                                           \
      BAIL("Out of memory: %s:%d", __FILE__, __LINE__);                        \
    __X;                                                                       \
  })

typedef void *(*realloc_fn_t)(void *, size_t);

static inline void *safe_realloc_array(void *ptr, size_t nmemb, size_t size,
                                       realloc_fn_t realloc_fn) {
  if ((nmemb >= MUL_NO_OVERFLOW || size >= MUL_NO_OVERFLOW) && nmemb > 0 &&
      SIZE_MAX / nmemb < size) {
    return NULL;
  }
  return realloc_fn(ptr, size * nmemb);
}

#define CHECKED_REALLOC(ptr, count, elem_size)                                 \
  ({                                                                           \
    void *__p = safe_realloc_array((ptr), (count), (elem_size), realloc);      \
    if (!__p && (count))                                                       \
      BAIL("Out of memory: %s:%d\n", __FILE__, __LINE__);                      \
    __p;                                                                       \
  })

#define NEW0(t)                                                                \
  ({                                                                           \
    t *__p = (t *)calloc(1, sizeof(t));                                        \
    if (!__p)                                                                  \
      BAIL("Out of memory: %s:%d\n", __FILE__, __LINE__);                      \
    __p;                                                                       \
  })

#define LIKELY(x) __builtin_expect(!!(x), 1)
#define UNLIKELY(x) __builtin_expect(!!(x), 0)

#define MAPPING(v, c) op_map(v, sizeof(v) / sizeof(armlet_op_mapping), c)

#define ARRAY_SIZE(a) (sizeof(a) / sizeof(*a))

typedef struct {
  int s;
  char *m;
} armlet_op_mapping;

int op_map(const armlet_op_mapping map[], size_t size, char *c);
#endif
