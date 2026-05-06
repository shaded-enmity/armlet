#ifndef __ARMLET_STRING__
#define __ARMLET_STRING__

#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "common.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  DEFINE_ARRAY(char *, items);
} armlet_string_list;

typedef struct {
  char *s;
  size_t start;
  size_t end;
} armlet_string_slice;

typedef struct {
  char *buf;
  size_t len;
  size_t cap;
} armlet_string_builder;

#define STRING_SLICE_FMT_ARG "%.*s"
#define STRING_SLICE_ARG(v) ((v).end - (v).start), (v).s

int armlet_sb_init(armlet_string_builder *, size_t);
void armlet_sb_free(armlet_string_builder *);
int armlet_sb_reserve(armlet_string_builder *, size_t);
int armlet_sb_append_string_sized(armlet_string_builder *, const char *,
                                  size_t);
int armlet_sb_append_string(armlet_string_builder *, const char *);
int armlet_sb_append_char(armlet_string_builder *, char);
int armlet_sb_append_char_repeat(armlet_string_builder *, char, size_t);

char *str_join_string_list(const char *, const armlet_string_list *);

char *str_join_from_array(const char *, const char *[], size_t);

char *str_join(const char *, ...);

bool streq(const char *, const char *);

armlet_string_list str_split(const char *, const char *);

bool strseq(const armlet_string_slice *, const char *);

char *s_sprintf(char *, ...);

uint8_t *concat(const uint8_t *, const uint8_t *);
uint8_t *concat_owned(uint8_t *, const uint8_t *);

bool strlist_contains(const char **, size_t, const char *);

void str_reverse(uint8_t *);

bool str_ends_with(const char *, const char *);
#endif
