#include "ast.h"
#include "diagnostics.h"
#include "utils/common.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *armlet_ast_node_type_names[] = {
    PASTE(AST_CMP),        PASTE(AST_BINOP),
    PASTE(AST_UNARY),      PASTE(AST_ASSIGNMENT),
    PASTE(AST_CALL),       PASTE(AST_FUNDEF),
    PASTE(AST_BITSEL),     PASTE(AST_BITSEL_RANGE),
    PASTE(AST_FIELDSEL),   PASTE(AST_BITSLURP),
    PASTE(AST_IF),         PASTE(AST_WHEN),
    PASTE(AST_TUPLE),      PASTE(AST_VALUE),
    PASTE(AST_TYPE),       PASTE(AST_ENUM),
    PASTE(AST_RETURN),     PASTE(AST_LOOP),
    PASTE(AST_ASSERT),     PASTE(AST_SUITE),
    PASTE(AST_VAR_DEF),    PASTE(AST_TYPE_SPEC),
    PASTE(AST_PARAMETER),  PASTE(AST_SET),
    PASTE(AST_BLOCK),      PASTE(AST_INLINE_IF),
    PASTE(AST_ARRAY),      PASTE(AST_ARRAY_ACCESS),
    PASTE(AST_TYPE_ALIAS), PASTE(AST_IMPORT),
    PASTE(AST_USE),        PASTE(AST_ARRAY_LITERAL),
    PASTE(AST_UNKNOWN),    PASTE(AST_IMPLEMENTATION_DEFINED),
    PASTE(AST_TRAP),       PASTE(AST_QUALIFIED_LHS),
    PASTE(AST_BITLAYOUT)};

char *armlet_source_string(armlet_node_source *source) {
  size_t len = source->span.end - source->span.start;

  if (len == 0)
    len = 1;

  char *s = CHECKED_MALLOC(len + 1);
  s[len] = '\0';

  memcpy(s, source->source.data + source->span.start, len);

  return s;
}

void armlet_emit_source_diagnostic(const armlet_source *source,
                                   armlet_span span, char *message) {
  armlet_source_diagnostic(stderr, source, span, true, NULL, message);
}

void armlet_source_error_v(const armlet_source *source, armlet_span span,
                           const char *fmt, va_list args) {
  char buffer[8192];
  vsnprintf(buffer, sizeof(buffer), fmt, args);
  armlet_emit_source_diagnostic(source, span, buffer);
  exit(1);
}

void armlet_source_error_n(const armlet_ast_node *n, const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  armlet_source_error_v(&n->source->source, n->source->span, fmt, args);
  va_end(args);
}

void armlet_source_error(const armlet_source *source, armlet_span span,
                         const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  armlet_source_error_v(source, span, fmt, args);
  va_end(args);
}

char *slurp_file(const char *path, size_t *out_len) {
  FILE *f = fopen(path, "rb");
  if (!f)
    return NULL;
  if (fseek(f, 0, SEEK_END) != 0) {
    fclose(f);
    return NULL;
  }
  long n = ftell(f);
  if (n < 0) {
    fclose(f);
    return NULL;
  }
  rewind(f);
  char *buf = (char *)CHECKED_MALLOC((size_t)n + 1);
  size_t rd = fread(buf, 1, (size_t)n, f);
  fclose(f);
  buf[rd] = '\0';
  if (out_len)
    *out_len = rd;
  return buf;
}

armlet_ast_node *armlet_ast_node_new(enum armlet_ast_node_type type,
                                     const armlet_ast_node *parent,
                                     armlet_span span,
                                     const armlet_source *src) {
  armlet_ast_node *n = NEW0(armlet_ast_node);

  n->type = type;
  n->parent = parent;
  n->source = NEW0(armlet_node_source);
  n->source->source = *src;
  n->source->span = span;

  return n;
}

