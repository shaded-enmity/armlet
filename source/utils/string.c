#include "string.h"
#include "errno.h"

int armlet_sb_init(armlet_string_builder *sb, size_t capacity) {
  if (capacity < 16)
    capacity = 16;

  sb->buf = CHECKED_MALLOC(capacity);

  sb->len = 0;
  sb->cap = capacity;
  sb->buf[0] = '\0';

  return 0;
}

void armlet_sb_free(armlet_string_builder *sb) {
  free(sb->buf);
  sb->buf = NULL;
  sb->cap = sb->len = 0;
}

int armlet_sb_reserve(armlet_string_builder *sb, size_t extra) {
  size_t need = sb->len + extra + 1;
  if (need <= sb->cap)
    return 0;

  size_t newcap = sb->cap;
  while (newcap < need)
    newcap *= 2;

  sb->buf = CHECKED_REALLOC(sb->buf, newcap, 1);
  sb->cap = newcap;
  return 0;
}

int armlet_sb_append_string(armlet_string_builder *sb, const char *s) {
  return armlet_sb_append_string_sized(sb, s, strlen(s));
}

int armlet_sb_append_string_sized(armlet_string_builder *sb, const char *s,
                                  size_t n) {
  int r = 0;
  if ((r = armlet_sb_reserve(sb, n)) != 0)
    return r;

  memcpy(sb->buf + sb->len, s, n);

  sb->len += n;
  sb->buf[sb->len] = '\0';

  return r;
}

int armlet_sb_append_char(armlet_string_builder *sb, char c) {
  int r = 0;

  if ((r = armlet_sb_reserve(sb, 1)) != 0)
    return r;

  sb->buf[sb->len++] = c;
  sb->buf[sb->len] = '\0';

  return r;
}

int armlet_sb_append_char_repeat(armlet_string_builder *sb, char c, size_t n) {
  int r = 0;
  if ((r = armlet_sb_reserve(sb, n)) != 0)
    return r;

  memset(sb->buf + sb->len, c, n);
  sb->len += n;

  return r;
}

char *str_join_string_list(const char *separator,
                           const armlet_string_list *sl) {
  return str_join_from_array(separator, (const char **)sl->items,
                             sl->num_items);
}

char *str_join_from_array(const char *separator, const char *items[],
                          size_t num_items) {
  const char *sep = separator ? separator : "";
  size_t sep_len = strlen(sep);

  if (items == NULL || num_items == 0) {
    return strdup("");
  }

  armlet_string_builder sb = {};
  armlet_sb_init(&sb, 64);

  armlet_sb_append_string(&sb, items[0]);
  for (size_t i = 1; i < num_items; ++i) {
    armlet_sb_append_string_sized(&sb, separator, sep_len);
    armlet_sb_append_string(&sb, items[i]);
  }

  return sb.buf;
}

char *str_join(const char *separator, ...) {
  va_list ap;
  va_start(ap, separator);

  size_t count = 0;
  const char *s;
  while ((s = va_arg(ap, const char *)) != NULL) {
    ++count;
  }
  va_end(ap);

  if (count == 0) {
    return strdup("");
  }

  const char **arr = CHECKED_MALLOC(count * sizeof(const char *));

  va_start(ap, separator);
  size_t i = 0;
  while ((s = va_arg(ap, const char *)) != NULL) {
    arr[i++] = s;
  }
  va_end(ap);

  char *result = str_join_from_array(separator, arr, count);
  free(arr);
  return result;
}

bool streq(const char *a, const char *b) { return strcmp(a, b) == 0; }

bool strseq(const armlet_string_slice *slice, const char *b) {
  return strncmp(slice->s + slice->start, b, slice->end - slice->start) == 0;
}

char *s_sprintf(char *fmt, ...) {
  va_list ap, ap2;
  va_start(ap, fmt);
  va_copy(ap2, ap);
  size_t needed = vsnprintf(NULL, 0, fmt, ap);
  va_end(ap);
  char *buf = calloc(1, needed + 1);
  if (!buf)
    BAIL("Out of memory: %s:%d\n", __FILE__, __LINE__);
  vsnprintf(buf, needed + 1, fmt, ap2);
  va_end(ap2);
  return buf;
}

uint8_t *concat(const uint8_t *a, const uint8_t *b) {
  char *result = CHECKED_MALLOC(strlen((const char *)a) + strlen((const char *)b) + 1);
  strcpy(result, (const char *)a);
  strcat(result, (const char *)b);
  return (uint8_t *)result;
}

uint8_t *concat_owned(uint8_t *a, const uint8_t *b) {
  uint8_t *tmp = a, *r = a;
  r = concat(a, b);
  free(tmp);
  return r;
}

bool strlist_contains(const char **items, size_t num_items, const char *item) {
  for (size_t i = 0; i < num_items; ++i) {
    if (streq(items[i], item))
      return true;
  }

  return false;
}

void str_reverse(uint8_t *s) {
  if (!s)
    return;
  size_t len = strlen((const char *)s);
  for (size_t i = 0; i < len / 2; i++) {
    char tmp = s[i];
    s[i] = s[len - 1 - i];
    s[len - 1 - i] = tmp;
  }
}

armlet_string_list str_split(const char *sep, const char *s) {
  armlet_string_list sl = {};

  char *c, *p, *f;

  c = f = strdup(s);
  while ((p = strsep(&c, sep)) != NULL) {
    ARR_APPEND(&sl, items, strdup(p));
  }

  free(f);
  return sl;
}

bool str_ends_with(const char *str, const char *suffix) {
  if (!str || !suffix)
    return false;

  size_t len_str = strlen(str);
  size_t len_suffix = strlen(suffix);

  if (len_suffix > len_str)
    return false;

  return memcmp(str + len_str - len_suffix, suffix, len_suffix) == 0;
}
