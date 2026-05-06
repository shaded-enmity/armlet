#define _GNU_SOURCE

#include "parser.h"
#include "ast.h"
#include "diagnostics.h"
#include "utils/common.h"
#include "utils/hashtable.h"
#include "utils/string.h"

#include <assert.h>
#include <ctype.h>
#include <libgen.h>
#include <unistd.h>
#include <gmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

armlet_ast_node *armlet_ast_node_new(enum armlet_ast_node_type type,
                                     const armlet_ast_node *parent,
                                     armlet_span span,
                                     const armlet_source *src);

typedef enum {
  TK_EOF = 0,
  /* Indentation / structure */
  TK_NEWLINE,
  TK_INDENT,
  TK_DEDENT,
  /* Literals */
  TK_NAME,
  TK_INTEGER,
  TK_FLOAT,
  TK_BITSTRING,
  TK_STRING,
  /* Keywords */
  TK_IF,
  TK_ELSE,
  TK_ELSIF,
  TK_THEN,
  TK_CASE,
  TK_OF,
  TK_WHEN,
  TK_OTHERWISE,
  TK_IN,
  TK_ASSERT,
  TK_RETURN,
  TK_CONSTANT,
  TK_WHILE,
  TK_DO,
  TK_FOR,
  TK_TO,
  TK_DOWNTO,
  TK_REPEAT,
  TK_UNTIL,
  TK_TYPE,
  TK_IS,
  TK_ENUMERATION,
  TK_ARRAY,
  TK_TRUE,
  TK_FALSE,
  TK_IMPORT,
  TK_USE,
  TK_UNKNOWN,
  TK_IMPLEMENTATION_DEFINED,
  TK_SEE,
  TK_UNDEFINED,
  TK_UNPREDICTABLE,
  TK_BITLAYOUT,
  TK_BITS,
  TK_BIT,
  /* Operator keywords */
  TK_DIV,
  TK_MOD,
  TK_EOR,
  TK_AND,
  TK_OR,
  /* Punctuation */
  TK_LPAREN,     /* (  */
  TK_RPAREN,     /* )  */
  TK_LBRACKET,   /* [  */
  TK_RBRACKET,   /* ]  */
  TK_LBRACE,     /* {  */
  TK_RBRACE,     /* }  */
  TK_LT,         /* <  */
  TK_GT,         /* >  */
  TK_LTEQ,       /* <= */
  TK_GTEQ,       /* >= */
  TK_EQEQ,       /* == */
  TK_NEQ,        /* != */
  TK_EQ,         /* =  */
  TK_SEMI,       /* ;  */
  TK_COLON,      /* :  */
  TK_COMMA,      /* ,  */
  TK_DOT,        /* .  */
  TK_DOTDOT,     /* .. */
  TK_PLUS,       /* +  */
  TK_PLUS_COLON, /* +: */
  TK_MINUS,      /* -  */
  TK_STAR,       /* *  */
  TK_SLASH,      /* /  */
  TK_CARET,      /* ^  */
  TK_BANG,       /* !  */
  TK_AMPAMP,     /* && */
  TK_PIPEPIPE,   /* || */
  TK_SHL,        /* << */
  TK_SHR,        /* >> */
} TK;

static const char *tk_to_string[] = {
    PASTE(TK_EOF),
    PASTE(TK_NEWLINE),
    PASTE(TK_INDENT),
    PASTE(TK_DEDENT),
    PASTE(TK_NAME),
    PASTE(TK_INTEGER),
    PASTE(TK_FLOAT),
    PASTE(TK_BITSTRING),
    PASTE(TK_STRING),
    PASTE(TK_IF),
    PASTE(TK_ELSE),
    PASTE(TK_ELSIF),
    PASTE(TK_THEN),
    PASTE(TK_CASE),
    PASTE(TK_OF),
    PASTE(TK_WHEN),
    PASTE(TK_OTHERWISE),
    PASTE(TK_IN),
    PASTE(TK_ASSERT),
    PASTE(TK_RETURN),
    PASTE(TK_CONSTANT),
    PASTE(TK_WHILE),
    PASTE(TK_DO),
    PASTE(TK_FOR),
    PASTE(TK_TO),
    PASTE(TK_DOWNTO),
    PASTE(TK_REPEAT),
    PASTE(TK_UNTIL),
    PASTE(TK_TYPE),
    PASTE(TK_IS),
    PASTE(TK_ENUMERATION),
    PASTE(TK_ARRAY),
    PASTE(TK_TRUE),
    PASTE(TK_FALSE),
    PASTE(TK_IMPORT),
    PASTE(TK_USE),
    PASTE(TK_UNKNOWN),
    PASTE(TK_IMPLEMENTATION_DEFINED),
    PASTE(TK_SEE),
    PASTE(TK_UNDEFINED),
    PASTE(TK_UNPREDICTABLE),
    PASTE(TK_BITLAYOUT),
    PASTE(TK_BITS),
    PASTE(TK_BIT),
    PASTE(TK_DIV),
    PASTE(TK_MOD),
    PASTE(TK_EOR),
    PASTE(TK_AND),
    PASTE(TK_OR),
    PASTE(TK_LPAREN),
    PASTE(TK_RPAREN),
    PASTE(TK_LBRACKET),
    PASTE(TK_RBRACKET),
    PASTE(TK_LBRACE),
    PASTE(TK_RBRACE),
    PASTE(TK_LT),
    PASTE(TK_GT),
    PASTE(TK_LTEQ),
    PASTE(TK_GTEQ),
    PASTE(TK_EQEQ),
    PASTE(TK_NEQ),
    PASTE(TK_EQ),
    PASTE(TK_SEMI),
    PASTE(TK_COLON),
    PASTE(TK_COMMA),
    PASTE(TK_DOT),
    PASTE(TK_DOTDOT),
    PASTE(TK_PLUS),
    PASTE(TK_PLUS_COLON),
    PASTE(TK_MINUS),
    PASTE(TK_STAR),
    PASTE(TK_SLASH),
    PASTE(TK_CARET),
    PASTE(TK_BANG),
    PASTE(TK_AMPAMP),
    PASTE(TK_PIPEPIPE),
    PASTE(TK_SHL),
    PASTE(TK_SHR),
};

typedef struct {
  const char *kw;
  uint8_t len;
  TK kind;
} KwEntry;

#define KW(s, k) {s, sizeof(s) - 1, k}

static const KwEntry kw_table[] = {
    KW("if", TK_IF),
    KW("else", TK_ELSE),
    KW("elsif", TK_ELSIF),
    KW("then", TK_THEN),
    KW("case", TK_CASE),
    KW("of", TK_OF),
    KW("when", TK_WHEN),
    KW("otherwise", TK_OTHERWISE),
    KW("IN", TK_IN),
    KW("assert", TK_ASSERT),
    KW("return", TK_RETURN),
    KW("constant", TK_CONSTANT),
    KW("while", TK_WHILE),
    KW("do", TK_DO),
    KW("for", TK_FOR),
    KW("to", TK_TO),
    KW("downto", TK_DOWNTO),
    KW("repeat", TK_REPEAT),
    KW("until", TK_UNTIL),
    KW("type", TK_TYPE),
    KW("is", TK_IS),
    KW("enumeration", TK_ENUMERATION),
    KW("array", TK_ARRAY),
    KW("TRUE", TK_TRUE),
    KW("FALSE", TK_FALSE),
    KW("import", TK_IMPORT),
    KW("use", TK_USE),
    KW("UNKNOWN", TK_UNKNOWN),
    KW("IMPLEMENTATION_DEFINED", TK_IMPLEMENTATION_DEFINED),
    KW("SEE", TK_SEE),
    KW("UNDEFINED", TK_UNDEFINED),
    KW("UNPREDICTABLE", TK_UNPREDICTABLE),
    KW("bitlayout", TK_BITLAYOUT),
    KW("bits", TK_BITS),
    KW("bit", TK_BIT),
    KW("DIV", TK_DIV),
    KW("MOD", TK_MOD),
    KW("EOR", TK_EOR),
    KW("AND", TK_AND),
    KW("OR", TK_OR),
};

static TK lookup_keyword(const char *s, size_t len) {
  for (size_t i = 0; i < ARRAY_SIZE(kw_table); i++) {
    if (kw_table[i].len == len && memcmp(kw_table[i].kw, s, len) == 0)
      return kw_table[i].kind;
  }
  return TK_NAME;
}

typedef struct {
  TK kind;
  uint32_t start;
  uint32_t end;
} Token;

#define MAX_INDENT 256
#define PENDING_MAX (MAX_INDENT + 2)

typedef struct {
  const char *src;
  uint32_t pos;
  uint32_t len;

  uint16_t indent_stack[MAX_INDENT];
  int indent_top;

  Token pending[PENDING_MAX];
  int pending_head;
  int pending_count;
} Lexer;

static void lexer_init(Lexer *l, const char *src, uint32_t len) {
  memset(l, 0, sizeof(*l));
  l->src = src;
  l->len = len;
  l->indent_stack[0] = 0;
  l->indent_top = 0;
}

static void add_pending(Lexer *l, TK kind, uint32_t pos) {
  assert(l->pending_count < PENDING_MAX);
  int idx = (l->pending_head + l->pending_count) % PENDING_MAX;
  l->pending[idx] = (Token){kind, pos, pos};
  l->pending_count++;
}

/*
 * Called after consuming a '\n'.  Skips blank lines and //-comments, then
 * measures the indentation of the next real line, and queues
 * NEWLINE + optional INDENT/DEDENTs into the pending buffer.
 */
static void process_newline(Lexer *l, uint32_t nl_pos) {
  uint16_t indent = 0;

  for (;;) {
    while (l->pos < l->len) {
      char c = l->src[l->pos];
      if (c == ' ') {
        indent++;
        l->pos++;
      } else if (c == '\t') {
        indent += 8;
        l->pos++;
      } else if (c == '\r' || c == '\f') {
        indent = 0;
        l->pos++;
      } else
        break;
    }

    if (l->pos >= l->len) {
      indent = 0;
      break;
    }

    char c = l->src[l->pos];

    if (c == '\n') {
      indent = 0;
      l->pos++;
      continue;
    }

    if (c == '/' && l->pos + 1 < l->len && l->src[l->pos + 1] == '/') {
      while (l->pos < l->len && l->src[l->pos] != '\n')
        l->pos++;
      if (l->pos < l->len)
        l->pos++;
      indent = 0;
      continue;
    }

    break;
  }

  uint16_t cur = l->indent_stack[l->indent_top];

  add_pending(l, TK_NEWLINE, nl_pos);

  if (indent > cur) {
    if (l->indent_top + 1 < MAX_INDENT)
      l->indent_stack[++l->indent_top] = indent;
    add_pending(l, TK_INDENT, l->pos);
  } else {
    while (l->indent_top > 0 && l->indent_stack[l->indent_top] > indent) {
      l->indent_top--;
      add_pending(l, TK_DEDENT, l->pos);
    }
  }
}

static Token lexer_next(Lexer *l) {
  if (l->pending_count > 0) {
    Token t = l->pending[l->pending_head];
    l->pending_head = (l->pending_head + 1) % PENDING_MAX;
    l->pending_count--;
    return t;
  }

  for (;;) {
    while (l->pos < l->len) {
      char c = l->src[l->pos];
      if (c == ' ' || c == '\t' || c == '\r' || c == '\f')
        l->pos++;
      else
        break;
    }

    if (l->pos + 1 < l->len && l->src[l->pos] == '/' &&
        l->src[l->pos + 1] == '/') {
      while (l->pos < l->len && l->src[l->pos] != '\n')
        l->pos++;
      continue;
    }

    if (l->pos + 1 < l->len && l->src[l->pos] == '/' &&
        l->src[l->pos + 1] == '*') {
      l->pos += 2;
      while (l->pos + 1 < l->len) {
        if (l->src[l->pos] == '*' && l->src[l->pos + 1] == '/') {
          l->pos += 2;
          break;
        }
        l->pos++;
      }
      continue;
    }
    break;
  }

  if (l->pos >= l->len) {
    while (l->indent_top > 0) {
      l->indent_top--;
      add_pending(l, TK_DEDENT, l->pos);
    }
    if (l->pending_count > 0)
      return lexer_next(l);
    return (Token){TK_EOF, l->pos, l->pos};
  }

  uint32_t start = l->pos;
  char c = l->src[l->pos];

  if (c == '\n') {
    l->pos++;
    process_newline(l, start);
    return lexer_next(l);
  }

  if (isalpha((unsigned char)c) || c == '_') {
    while (l->pos < l->len &&
           (isalnum((unsigned char)l->src[l->pos]) || l->src[l->pos] == '_'))
      l->pos++;
    size_t len = l->pos - start;
    TK kind = lookup_keyword(l->src + start, len);
    return (Token){kind, start, l->pos};
  }

  if (isdigit((unsigned char)c)) {
    bool is_float = false;
    if (c == '0' && l->pos + 1 < l->len &&
        (l->src[l->pos + 1] == 'x' || l->src[l->pos + 1] == 'X')) {
      l->pos += 2;
      while (l->pos < l->len &&
             (isxdigit((unsigned char)l->src[l->pos]) || l->src[l->pos] == '_'))
        l->pos++;
    } else {
      while (l->pos < l->len &&
             (isdigit((unsigned char)l->src[l->pos]) || l->src[l->pos] == '_'))
        l->pos++;
      if (l->pos < l->len && l->src[l->pos] == '.' &&
          (l->pos + 1 >= l->len || l->src[l->pos + 1] != '.')) {
        is_float = true;
        l->pos++;
        while (l->pos < l->len && (isdigit((unsigned char)l->src[l->pos]) ||
                                   l->src[l->pos] == '_'))
          l->pos++;
      }
      if (l->pos < l->len && (l->src[l->pos] == 'e' || l->src[l->pos] == 'E')) {
        is_float = true;
        l->pos++;
        if (l->pos < l->len && (l->src[l->pos] == '+' || l->src[l->pos] == '-'))
          l->pos++;
        while (l->pos < l->len && isdigit((unsigned char)l->src[l->pos]))
          l->pos++;
      }
    }
    return (Token){is_float ? TK_FLOAT : TK_INTEGER, start, l->pos};
  }

  if (c == '\'') {
    l->pos++;
    while (l->pos < l->len && l->src[l->pos] != '\'')
      l->pos++;
    if (l->pos < l->len)
      l->pos++;
    return (Token){TK_BITSTRING, start, l->pos};
  }

  if (c == '"') {
    l->pos++;
    while (l->pos < l->len && l->src[l->pos] != '"') {
      if (l->src[l->pos] == '\\')
        l->pos++;
      l->pos++;
    }
    if (l->pos < l->len)
      l->pos++;
    return (Token){TK_STRING, start, l->pos};
  }

  l->pos++;
  TK kind;
  switch (c) {
  case '(':
    kind = TK_LPAREN;
    break;
  case ')':
    kind = TK_RPAREN;
    break;
  case '[':
    kind = TK_LBRACKET;
    break;
  case ']':
    kind = TK_RBRACKET;
    break;
  case '{':
    kind = TK_LBRACE;
    break;
  case '}':
    kind = TK_RBRACE;
    break;
  case ';':
    kind = TK_SEMI;
    break;
  case ',':
    kind = TK_COMMA;
    break;
  case '*':
    kind = TK_STAR;
    break;
  case '^':
    kind = TK_CARET;
    break;
  case '/':
    kind = TK_SLASH;
    break;

  case '!':
    if (l->pos < l->len && l->src[l->pos] == '=') {
      l->pos++;
      kind = TK_NEQ;
    } else
      kind = TK_BANG;
    break;
  case '&':
    if (l->pos < l->len && l->src[l->pos] == '&') {
      l->pos++;
      kind = TK_AMPAMP;
    } else
      kind = TK_AMPAMP;
    break;
  case '|':
    if (l->pos < l->len && l->src[l->pos] == '|') {
      l->pos++;
      kind = TK_PIPEPIPE;
    } else
      kind = TK_PIPEPIPE;
    break;
  case '=':
    if (l->pos < l->len && l->src[l->pos] == '=') {
      l->pos++;
      kind = TK_EQEQ;
    } else
      kind = TK_EQ;
    break;
  case '<':
    if (l->pos < l->len && l->src[l->pos] == '=') {
      l->pos++;
      kind = TK_LTEQ;
    } else if (l->pos < l->len && l->src[l->pos] == '<') {
      l->pos++;
      kind = TK_SHL;
    } else
      kind = TK_LT;
    break;
  case '>':
    if (l->pos < l->len && l->src[l->pos] == '=') {
      l->pos++;
      kind = TK_GTEQ;
    } else if (l->pos < l->len && l->src[l->pos] == '>') {
      l->pos++;
      kind = TK_SHR;
    } else
      kind = TK_GT;
    break;
  case '+':
    if (l->pos < l->len && l->src[l->pos] == ':') {
      l->pos++;
      kind = TK_PLUS_COLON;
    } else
      kind = TK_PLUS;
    break;
  case '-':
    kind = TK_MINUS;
    break;
  case ':':
    kind = TK_COLON;
    break;
  case '.':
    if (l->pos < l->len && l->src[l->pos] == '.') {
      l->pos++;
      kind = TK_DOTDOT;
    } else
      kind = TK_DOT;
    break;
  default:
    kind = TK_EOF;
    break;
  }

  return (Token){kind, start, l->pos};
}

typedef struct {
  Lexer lex;
  const armlet_source *source;

  Token *buf;
  int buf_head;
  int buf_count;
  int buf_cap;

  int parse_depth;

  const armlet_ast_node *parent;
} Parser;

#define MAX_PARSE_DEPTH 512

static Token peek(Parser *p, int n) {
  while (p->buf_count <= n) {
    if (p->buf_count >= p->buf_cap) {
      int newcap = p->buf_cap ? p->buf_cap * 2 : 16;
      Token *nb = CHECKED_MALLOC((size_t)newcap * sizeof(Token));
      for (int i = 0; i < p->buf_count; i++) {
        nb[i] = p->buf[(p->buf_head + i) % p->buf_cap];
      }
      free(p->buf);
      p->buf = nb;
      p->buf_head = 0;
      p->buf_cap = newcap;
    }
    p->buf[(p->buf_head + p->buf_count) % p->buf_cap] = lexer_next(&p->lex);
    p->buf_count++;
  }
  return p->buf[(p->buf_head + n) % p->buf_cap];
}

static Token advance(Parser *p) {
  Token t = peek(p, 0);
  p->buf_head = (p->buf_head + 1) % p->buf_cap;
  p->buf_count--;
  return t;
}

static bool check(Parser *p, TK kind) { return peek(p, 0).kind == kind; }

static bool eat(Parser *p, TK kind) {
  if (!check(p, kind))
    return false;
  advance(p);
  return true;
}

static void skip_nl(Parser *p) {
  TK k;
  while ((k = peek(p, 0).kind) == TK_NEWLINE || k == TK_INDENT ||
         k == TK_DEDENT)
    advance(p);
}

static Token expect(Parser *p, TK kind) {
  Token t = peek(p, 0);
  if (t.kind != kind) {
    armlet_span span = {t.start, t.end};
    armlet_source_error(p->source, span, "Expected token %s, got %s",
                        tk_to_string[kind], tk_to_string[t.kind]);
  }
  return advance(p);
}

static char *src_str(Parser *p, uint32_t start, uint32_t end) {
  if (end < start)
    return strdup("");
  size_t len = end - start;
  char *s = CHECKED_MALLOC(len + 1);
  memcpy(s, p->source->data + start, len);
  s[len] = '\0';
  return s;
}

static char *tok_str(Parser *p, Token t) { return src_str(p, t.start, t.end); }

static armlet_ast_node *new_node(Parser *p, enum armlet_ast_node_type type,
                                 uint32_t start, uint32_t end) {
  armlet_span span = {start, end};
  return armlet_ast_node_new(type, p->parent, span, p->source);
}

static void reparent(armlet_ast_node *child, const armlet_ast_node *parent) {
  if (child)
    child->parent = parent;
}

static armlet_ast_node *name_node(Parser *p, Token t) {
  armlet_ast_node *n = new_node(p, AST_VALUE, t.start, t.end);
  n->value = NEW0(armlet_ast_value);
  n->value->tag = VAL_NAME;
  n->value->name = tok_str(p, t);
  return n;
}

static armlet_ast_node *deref_node(Parser *p, char **names, int count,
                                   uint32_t start, uint32_t end) {
  armlet_ast_node *n = new_node(p, AST_VALUE, start, end);
  n->value = NEW0(armlet_ast_value);
  n->value->tag = VAL_DEREF;
  for (int i = 0; i < count; i++)
    ARR_APPEND(&n->value->deref, names, names[i]);
  return n;
}

static int type_spec_end(Parser *p, int start) {
  TK t = peek(p, start).kind;
  switch (t) {
  case TK_BIT:
    return start + 1;
  case TK_BITS: {
    int i = start + 1;
    if (peek(p, i).kind != TK_LPAREN)
      return start + 1;
    i++;
    int depth = 1;
    while (depth > 0 && peek(p, i).kind != TK_EOF) {
      TK k = peek(p, i++).kind;
      if (k == TK_LPAREN)
        depth++;
      else if (k == TK_RPAREN)
        depth--;
    }
    return i;
  }
  case TK_NAME: {
    int i = start + 1;
    while (peek(p, i).kind == TK_DOT && peek(p, i + 1).kind == TK_NAME)
      i += 2;
    return i;
  }
  default:
    return -1;
  }
}

static int name_or_deref_end(Parser *p, int start) {
  if (peek(p, start).kind != TK_NAME)
    return start;
  int i = start + 1;
  while (peek(p, i).kind == TK_DOT && peek(p, i + 1).kind == TK_NAME)
    i += 2;
  return i;
}

static int skip_balanced(Parser *p, int start, TK open, TK close) {
  assert(peek(p, start).kind == open);
  int i = start + 1;
  int depth = 1;
  while (depth > 0 && peek(p, i).kind != TK_EOF) {
    TK k = peek(p, i++).kind;
    if (k == open)
      depth++;
    else if (k == close)
      depth--;
  }
  return i;
}

static bool is_setter(Parser *p) {
  int i = 0;
  if (peek(p, i).kind != TK_NAME)
    return false;
  i = name_or_deref_end(p, i);
  if (peek(p, i).kind != TK_LBRACKET)
    return false;
  i = skip_balanced(p, i, TK_LBRACKET, TK_RBRACKET);
  if (peek(p, i).kind != TK_EQ)
    return false;
  i++;
  int ts = type_spec_end(p, i);
  if (ts < 0)
    return false;
  if (peek(p, ts).kind != TK_NAME)
    return false;
  ts++;
  return peek(p, ts).kind == TK_NEWLINE;
}

static bool is_func_def_no_return(Parser *p) {
  int i = name_or_deref_end(p, 0);
  if (i == 0)
    return false;
  if (peek(p, i).kind != TK_LPAREN)
    return false;
  int after = skip_balanced(p, i, TK_LPAREN, TK_RPAREN);
  if (peek(p, after).kind != TK_NEWLINE)
    return false;
  if (peek(p, after + 1).kind != TK_INDENT)
    return false;
  int arg0_pos = i + 1;
  TK arg0 = peek(p, arg0_pos).kind;
  if (arg0 == TK_RPAREN)
    return true;
  if (arg0 == TK_STRING || arg0 == TK_INTEGER || arg0 == TK_FLOAT ||
      arg0 == TK_BITSTRING || arg0 == TK_TRUE || arg0 == TK_FALSE ||
      arg0 == TK_MINUS || arg0 == TK_BANG || arg0 == TK_LBRACE ||
      arg0 == TK_LBRACKET || arg0 == TK_LPAREN)
    return false;
  int ts = type_spec_end(p, arg0_pos);
  if (ts <= arg0_pos)
    return false;
  return peek(p, ts).kind == TK_NAME;
}

static bool is_func_def_with_return(Parser *p) {
  int ts = type_spec_end(p, 0);
  if (ts < 0 || ts == 0)
    return false;
  if (peek(p, ts).kind != TK_NAME)
    return false;
  int fn = name_or_deref_end(p, ts);
  if (fn == ts)
    return false;
  if (peek(p, fn).kind != TK_LPAREN)
    return false;
  int after = skip_balanced(p, fn, TK_LPAREN, TK_RPAREN);
  return peek(p, after).kind == TK_NEWLINE;
}

static bool is_func_def_return_tuple(Parser *p) {
  if (peek(p, 0).kind != TK_LPAREN)
    return false;
  int after_rt = skip_balanced(p, 0, TK_LPAREN, TK_RPAREN);
  if (peek(p, after_rt).kind != TK_NAME)
    return false;
  int fn = name_or_deref_end(p, after_rt);
  if (fn == after_rt)
    return false;
  if (peek(p, fn).kind != TK_LPAREN)
    return false;
  int after = skip_balanced(p, fn, TK_LPAREN, TK_RPAREN);
  return peek(p, after).kind == TK_NEWLINE;
}

static bool is_var_def(Parser *p) {
  int ts = type_spec_end(p, 0);
  if (ts < 0 || ts == 0)
    return false;
  if (peek(p, ts).kind != TK_NAME)
    return false;
  int fn = name_or_deref_end(p, ts);
  if (fn == ts)
    return false;
  TK after = peek(p, fn).kind;
  return after == TK_COMMA || after == TK_SEMI;
}

static bool is_typed_assign(Parser *p) {
  int ts = type_spec_end(p, 0);
  if (ts <= 0)
    return false;
  if (peek(p, ts).kind != TK_NAME)
    return false;
  int fn = name_or_deref_end(p, ts);
  if (fn == ts)
    return false;
  return peek(p, fn).kind == TK_EQ;
}

static armlet_ast_node *parse_expr(Parser *p, int min_bp);
static armlet_ast_node *parse_type_spec(Parser *p);
static armlet_ast_node *parse_block(Parser *p);
static armlet_ast_node *parse_statement(Parser *p);
static armlet_ast_node *parse_simple_statement(Parser *p);
static armlet_ast_node *parse_typed_assign(Parser *p);

static armlet_ast_node *parse_name_or_deref(Parser *p) {
  Token first = expect(p, TK_NAME);

  if (peek(p, 0).kind != TK_DOT || peek(p, 1).kind != TK_NAME) {
    return name_node(p, first);
  }

  char *names[256];
  int count = 0;
  names[count++] = tok_str(p, first);

  while (peek(p, 0).kind == TK_DOT && peek(p, 1).kind == TK_NAME) {
    if (count >= 256)
      armlet_source_error(p->source, (armlet_span){first.start, peek(p, 0).end},
                          "Too many dotted name components (limit: 256)");
    advance(p);
    Token nm = advance(p);
    names[count++] = tok_str(p, nm);
  }

  return deref_node(p, names, count, first.start, peek(p, 0).start);
}

static armlet_ast_node *parse_array_variable(Parser *p) {
  Token start_tok = expect(p, TK_ARRAY);
  expect(p, TK_LBRACKET);
  armlet_ast_node *lo = parse_expr(p, 0);
  expect(p, TK_DOTDOT);
  armlet_ast_node *hi = parse_expr(p, 0);
  expect(p, TK_RBRACKET);
  expect(p, TK_OF);
  armlet_ast_node *elem_type = parse_type_spec(p);
  armlet_ast_node *arr_name = parse_name_or_deref(p);

  armlet_ast_node *n =
      new_node(p, AST_ARRAY, start_tok.start, arr_name->source->span.end);
  n->array = NEW0(armlet_ast_array);
  n->array->type = elem_type;
  n->array->name = arr_name;
  n->array->start = lo;
  n->array->end = hi;
  reparent(lo, n);
  reparent(hi, n);
  reparent(elem_type, n);
  reparent(arr_name, n);
  return n;
}

static armlet_ast_node *parse_type_spec(Parser *p) {
  Token start_tok = peek(p, 0);

  if (check(p, TK_ARRAY)) {
    return parse_array_variable(p);
  }

  armlet_ast_node *n = new_node(p, AST_TYPE_SPEC, start_tok.start, 0);
  n->type_spec = NEW0(armlet_ast_type_spec);

  const armlet_ast_node *prev_parent = p->parent;
  p->parent = n;

  if (check(p, TK_BIT)) {
    advance(p);
    armlet_ast_node *name_n =
        new_node(p, AST_VALUE, start_tok.start, start_tok.end);
    name_n->value = NEW0(armlet_ast_value);
    name_n->value->tag = VAL_NAME;
    name_n->value->name = strdup("bits");
    n->type_spec->name = name_n;

    armlet_ast_node *size_n =
        new_node(p, AST_VALUE, start_tok.start, start_tok.end);
    size_n->value = NEW0(armlet_ast_value);
    size_n->value->tag = VAL_IMMEDIATE;
    size_n->value->imm.tag = IMM_INTEGER;
    mpz_init_set_ui(size_n->value->imm.integer, 1);
    n->type_spec->size = size_n;
    n->source->span.end = start_tok.end;
    p->parent = prev_parent;
    return n;
  }

  if (check(p, TK_BITS)) {
    Token bits_tok = advance(p);
    n->type_spec->name = name_node(p, bits_tok);
    expect(p, TK_LPAREN);
    n->type_spec->size = parse_expr(p, 0);
    Token close = expect(p, TK_RPAREN);
    n->source->span.end = close.end;
    p->parent = prev_parent;
    return n;
  }

  Token first = expect(p, TK_NAME);
  if (peek(p, 0).kind == TK_DOT && peek(p, 1).kind == TK_NAME) {
    char *names[256];
    int count = 0;
    names[count++] = tok_str(p, first);
    while (peek(p, 0).kind == TK_DOT && peek(p, 1).kind == TK_NAME) {
      if (count >= 256)
        armlet_source_error(p->source, (armlet_span){first.start, peek(p, 0).end},
                            "Too many dotted name components (limit: 256)");
      advance(p);
      Token nm = advance(p);
      names[count++] = tok_str(p, nm);
    }
    armlet_ast_node *deref =
        deref_node(p, names, count, first.start, peek(p, 0).start);
    n->type_spec->name = deref;
    n->type_spec->size = NULL;
    n->source->span.end = deref->source->span.end;
  } else {
    n->type_spec->name = name_node(p, first);
    n->type_spec->size = NULL;
    n->source->span.end = first.end;
  }
  p->parent = prev_parent;
  return n;
}

static int infix_bp(TK kind) {
  switch (kind) {
  case TK_AMPAMP:
  case TK_PIPEPIPE:
    return 1;
  case TK_LTEQ:
  case TK_EQEQ:
  case TK_NEQ:
  case TK_GTEQ:
  case TK_GT:
  case TK_IN:
    return 2;
  case TK_COLON:
    return 3;
  case TK_SHR:
  case TK_SHL:
  case TK_EOR:
  case TK_AND:
  case TK_OR:
    return 17;
  case TK_PLUS:
  case TK_MINUS:
    return 18;
  case TK_STAR:
  case TK_DIV:
  case TK_SLASH:
  case TK_MOD:
    return 19;
  case TK_CARET:
  case TK_PLUS_COLON:
    return 21;
  case TK_UNKNOWN:
  case TK_IMPLEMENTATION_DEFINED:
    return 25;
  default:
    return 0;
  }
}

static enum armlet_vm_binop binop_from_tk(TK k) {
  switch (k) {
  case TK_PLUS:
    return BINOP_ADD;
  case TK_MINUS:
    return BINOP_SUB;
  case TK_STAR:
    return BINOP_MUL;
  case TK_DIV:
    return BINOP_DIV;
  case TK_SLASH:
    return BINOP_FDIV;
  case TK_MOD:
    return BINOP_MOD;
  case TK_COLON:
    return BINOP_CONCAT;
  case TK_PLUS_COLON:
    return BINOP_OFF_CONCAT;
  case TK_CARET:
    return BINOP_PWR;
  case TK_SHR:
    return BINOP_SHR;
  case TK_SHL:
    return BINOP_SHL;
  case TK_EOR:
    return BINOP_EOR;
  case TK_AND:
    return BINOP_AND;
  case TK_OR:
    return BINOP_OR;
  case TK_AMPAMP:
    return BINOP_AND;
  case TK_PIPEPIPE:
    return BINOP_OR;
  default:
    return BINOP_ADD; /* unreachable */
  }
}

static enum armlet_vm_comparison cmpop_from_tk(TK k) {
  switch (k) {
  case TK_EQEQ:
    return CMP_EQ;
  case TK_NEQ:
    return CMP_NEQ;
  case TK_LT:
    return CMP_LT;
  case TK_LTEQ:
    return CMP_LTEQ;
  case TK_GT:
    return CMP_GT;
  case TK_GTEQ:
    return CMP_GTEQ;
  case TK_IN:
    return CMP_IN;
  default:
    return CMP_EQ;
  }
}

static bool is_cmp_op(TK k) {
  switch (k) {
  case TK_LT:
  case TK_LTEQ:
  case TK_EQEQ:
  case TK_NEQ:
  case TK_GT:
  case TK_GTEQ:
  case TK_IN:
    return true;
  default:
    return false;
  }
}

static armlet_ast_node *parse_paren_or_tuple(Parser *p) {
  Token open = expect(p, TK_LPAREN);
  skip_nl(p);

  if (check(p, TK_RPAREN)) {
    Token close = advance(p);
    armlet_ast_node *n = new_node(p, AST_TUPLE, open.start, close.end);
    n->tuple = NEW0(armlet_ast_tuple);
    return n;
  }

  armlet_ast_node *first = parse_expr(p, 0);
  skip_nl(p);

  if (!check(p, TK_COMMA)) {
    skip_nl(p);
    expect(p, TK_RPAREN);
    return first;
  }

  armlet_ast_node *n = new_node(p, AST_TUPLE, open.start, 0);
  n->tuple = NEW0(armlet_ast_tuple);
  reparent(first, n);
  ARR_APPEND(n->tuple, elements, first);
  const armlet_ast_node *prev_parent = p->parent;
  p->parent = n;
  while (eat(p, TK_COMMA)) {
    skip_nl(p);
    ARR_APPEND(n->tuple, elements, parse_expr(p, 0));
    skip_nl(p);
  }
  p->parent = prev_parent;
  skip_nl(p);
  Token close = expect(p, TK_RPAREN);
  n->source->span.end = close.end;
  return n;
}

static armlet_ast_node *parse_inline_if(Parser *p) {
  Token start = expect(p, TK_IF);
  armlet_ast_node *n = new_node(p, AST_INLINE_IF, start.start, 0);
  n->inline_if = NEW0(armlet_ast_inline_if);

  const armlet_ast_node *prev_parent = p->parent;
  p->parent = n;
  for (;;) {
    armlet_ast_cond_branch *br = NEW0(armlet_ast_cond_branch);
    br->condition = parse_expr(p, 0);
    expect(p, TK_THEN);
    br->consequence = parse_expr(p, 0);
    ARR_APPEND(n->inline_if, conditions, br);
    if (!eat(p, TK_ELSIF))
      break;
  }
  expect(p, TK_ELSE);
  n->inline_if->alternative = parse_expr(p, 0);
  p->parent = prev_parent;
  n->source->span.end = n->inline_if->alternative->source->span.end;
  return n;
}

static armlet_ast_node *parse_set(Parser *p) {
  Token open = expect(p, TK_LBRACE);
  armlet_ast_node *n = new_node(p, AST_SET, open.start, 0);
  n->set = NEW0(armlet_ast_set);
  const armlet_ast_node *prev_parent = p->parent;
  p->parent = n;
  skip_nl(p);
  for (;;) {
    if (check(p, TK_RBRACE))
      break;
    ARR_APPEND(n->set, members, parse_expr(p, 0));
    skip_nl(p);
    if (!eat(p, TK_COMMA))
      break;
    skip_nl(p);
  }
  p->parent = prev_parent;
  skip_nl(p);
  Token close = expect(p, TK_RBRACE);
  n->source->span.end = close.end;
  return n;
}

static armlet_ast_node *parse_array_literal(Parser *p) {
  Token open = expect(p, TK_LBRACKET);
  armlet_ast_node *n = new_node(p, AST_ARRAY_LITERAL, open.start, 0);
  n->array_literal = NEW0(armlet_ast_array_literal);
  const armlet_ast_node *prev_parent = p->parent;
  p->parent = n;
  skip_nl(p);
  for (;;) {
    if (check(p, TK_RBRACKET))
      break;
    ARR_APPEND(n->array_literal, members, parse_expr(p, 0));
    skip_nl(p);
    if (!eat(p, TK_COMMA))
      break;
    skip_nl(p);
  }
  p->parent = prev_parent;
  skip_nl(p);
  Token close = expect(p, TK_RBRACKET);
  n->source->span.end = close.end;
  return n;
}

static armlet_ast_node *parse_atom(Parser *p) {
  {
    TK t0 = peek(p, 0).kind;
    if (t0 == TK_BITS || t0 == TK_BIT) {
      int ts = type_spec_end(p, 0);
      if (ts > 0) {
        TK after = peek(p, ts).kind;
        if (after == TK_UNKNOWN) {
          armlet_ast_node *type = parse_type_spec(p);
          Token uk = advance(p);
          armlet_ast_node *n =
              new_node(p, AST_UNKNOWN, type->source->span.start, uk.end);
          n->unknown = type->type_spec;
          reparent(type, n);
          return n;
        }
        if (after == TK_IMPLEMENTATION_DEFINED) {
          armlet_ast_node *type = parse_type_spec(p);
          Token id = advance(p);
          Token key_tok = expect(p, TK_STRING);
          char *key = src_str(p, key_tok.start + 1, key_tok.end - 1);
          armlet_ast_node *n = new_node(p, AST_IMPLEMENTATION_DEFINED,
                                        type->source->span.start, key_tok.end);
          n->implementation_defined = NEW0(armlet_ast_implementation_defined);
          n->implementation_defined->type = type;
          n->implementation_defined->key = key;
          reparent(type, n);
          (void)id;
          return n;
        }
      }
    }
  }

  Token t = peek(p, 0);

  if (t.kind == TK_UNPREDICTABLE || t.kind == TK_UNDEFINED) {
    advance(p);
    armlet_ast_node *n = new_node(p, AST_TRAP, t.start, t.end);
    n->trap = NEW0(armlet_ast_trap);
    n->trap->trap_type =
        (t.kind == TK_UNPREDICTABLE) ? TRAP_UNPREDICTABLE : TRAP_UNDEFINED;
    return n;
  }

  if (t.kind == TK_SEE) {
    advance(p);
    Token str = expect(p, TK_STRING);
    armlet_ast_node *n = new_node(p, AST_TRAP, t.start, str.end);
    n->trap = NEW0(armlet_ast_trap);
    n->trap->trap_type = TRAP_SEE;
    n->trap->context = src_str(p, str.start + 1, str.end - 1);
    return n;
  }

  if (t.kind == TK_IF) {
    return parse_inline_if(p);
  }

  if (t.kind == TK_LBRACE)
    return parse_set(p);

  if (t.kind == TK_LBRACKET)
    return parse_array_literal(p);

  if (t.kind == TK_LPAREN)
    return parse_paren_or_tuple(p);

  if (t.kind == TK_MINUS) {
    TK next = peek(p, 1).kind;
    if (next == TK_COMMA || next == TK_RPAREN) {
      advance(p);
      armlet_ast_node *n = new_node(p, AST_VALUE, t.start, t.end);
      n->value = NEW0(armlet_ast_value);
      n->value->tag = VAL_NAME;
      n->value->name = strdup("-");
      return n;
    }
  }

  if (t.kind == TK_MINUS || t.kind == TK_BANG) {
    advance(p);
    armlet_ast_node *operand = parse_expr(p, 20);
    armlet_ast_node *n =
        new_node(p, AST_UNARY, t.start, operand->source->span.end);
    n->unary = NEW0(armlet_ast_unary);
    n->unary->op = (t.kind == TK_MINUS) ? UNARY_MINUS : UNARY_NEGATION;
    n->unary->node = operand;
    reparent(operand, n);
    return n;
  }

  if (t.kind == TK_TRUE || t.kind == TK_FALSE) {
    advance(p);
    armlet_ast_node *n = new_node(p, AST_VALUE, t.start, t.end);
    n->value = NEW0(armlet_ast_value);
    n->value->tag = VAL_IMMEDIATE;
    n->value->imm.tag = IMM_BOOLEAN;
    n->value->imm.boolean = (t.kind == TK_TRUE);
    return n;
  }

  if (t.kind == TK_BITSTRING) {
    advance(p);
    char *raw = src_str(p, t.start + 1, t.end - 1);
    size_t rlen = strlen(raw);
    char *out = CHECKED_MALLOC(rlen + 1);
    size_t j = 0;
    for (size_t i = 0; i < rlen; i++) {
      char c = raw[i];
      if (c == '0' || c == '1' || c == 'x')
        out[j++] = c;
      else if (c == 'X')
        out[j++] = 'x';
      else if (c != ' ') {
      }
    }
    out[j] = '\0';
    free(raw);
    armlet_ast_node *n = new_node(p, AST_VALUE, t.start, t.end);
    n->value = NEW0(armlet_ast_value);
    n->value->tag = VAL_IMMEDIATE;
    n->value->imm.tag = IMM_BITSTRING;
    n->value->imm.bits = out;
    return n;
  }

  if (t.kind == TK_INTEGER) {
    advance(p);
    char *raw = tok_str(p, t);
    size_t rlen = strlen(raw);
    char *clean = CHECKED_MALLOC(rlen + 1);
    size_t j = 0;
    for (size_t i = 0; i < rlen; i++)
      if (raw[i] != '_')
        clean[j++] = raw[i];
    clean[j] = '\0';
    free(raw);
    armlet_ast_node *n = new_node(p, AST_VALUE, t.start, t.end);
    n->value = NEW0(armlet_ast_value);
    n->value->tag = VAL_IMMEDIATE;
    n->value->imm.tag = IMM_INTEGER;
    mpz_init_set_str(n->value->imm.integer, clean, 0);
    free(clean);
    return n;
  }

  if (t.kind == TK_FLOAT) {
    advance(p);
    char *raw = tok_str(p, t);
    char *end_ptr = NULL;
    float f = strtof(raw, &end_ptr);
    free(raw);
    armlet_ast_node *n = new_node(p, AST_VALUE, t.start, t.end);
    n->value = NEW0(armlet_ast_value);
    n->value->tag = VAL_IMMEDIATE;
    n->value->imm.tag = IMM_FLOAT;
    n->value->imm.real = f;
    return n;
  }

  if (t.kind == TK_STRING) {
    advance(p);
    armlet_ast_node *n = new_node(p, AST_VALUE, t.start, t.end);
    n->value = NEW0(armlet_ast_value);
    n->value->tag = VAL_IMMEDIATE;
    n->value->imm.tag = IMM_STRING;
    n->value->imm.string = src_str(p, t.start + 1, t.end - 1);
    return n;
  }

  if (t.kind == TK_NAME || t.kind == TK_BITS || t.kind == TK_BIT) {
    Token first;
    if (t.kind == TK_BITS || t.kind == TK_BIT) {
      first = advance(p);
    } else {
      first = advance(p);
    }

    char *names[256];
    int ncount = 0;
    names[ncount++] = tok_str(p, first);

    while (peek(p, 0).kind == TK_DOT) {
      if (peek(p, 1).kind == TK_LT) {
        advance(p);
        advance(p);
        skip_nl(p);
        armlet_ast_node *base;
        if (ncount > 1)
          base = deref_node(p, names, ncount, first.start, peek(p, 0).start);
        else
          base = name_node(p, first);

        armlet_ast_node *n = new_node(p, AST_FIELDSEL, first.start, 0);
        n->fieldselect = NEW0(armlet_ast_fieldselect);
        n->fieldselect->source = base;
        reparent(base, n);
        const armlet_ast_node *prev_parent = p->parent;
        p->parent = n;
        for (;;) {
          Token sel = expect(p, TK_NAME);
          ARR_APPEND(n->fieldselect, selections, name_node(p, sel));
          skip_nl(p);
          if (!eat(p, TK_COMMA))
            break;
          skip_nl(p);
        }
        p->parent = prev_parent;
        skip_nl(p);
        Token gt = expect(p, TK_GT);
        n->source->span.end = gt.end;
        return n;
      }
      if (peek(p, 1).kind == TK_NAME) {
        if (ncount >= 256)
          armlet_source_error(p->source, (armlet_span){first.start, peek(p, 0).end},
                              "Too many dotted name components (limit: 256)");
        advance(p);
        Token nm = advance(p);
        names[ncount++] = tok_str(p, nm);
      } else {
        break;
      }
    }

    armlet_ast_node *base;
    if (ncount > 1)
      base = deref_node(p, names, ncount, first.start, peek(p, 0).start);
    else
      base = name_node(p, first);

    if (peek(p, 0).kind == TK_LBRACKET) {
      Token lb = advance(p);
      skip_nl(p);
      armlet_ast_node *n = new_node(p, AST_ARRAY_ACCESS, first.start, 0);
      n->array_access = NEW0(armlet_ast_array_access);
      n->array_access->name = base;
      reparent(base, n);
      if (!check(p, TK_RBRACKET)) {
        const armlet_ast_node *prev_parent = p->parent;
        p->parent = n;
        for (;;) {
          ARR_APPEND(n->array_access, indices, parse_expr(p, 0));
          skip_nl(p);
          if (!eat(p, TK_COMMA))
            break;
          skip_nl(p);
        }
        p->parent = prev_parent;
      }
      skip_nl(p);
      Token rb = expect(p, TK_RBRACKET);
      n->source->span.end = rb.end;
      (void)lb;

      if (peek(p, 0).kind == TK_DOT && peek(p, 1).kind == TK_LT) {
        advance(p);
        advance(p);
        skip_nl(p);
        armlet_ast_node *fs = new_node(p, AST_FIELDSEL, first.start, 0);
        fs->fieldselect = NEW0(armlet_ast_fieldselect);
        fs->fieldselect->source = n;
        reparent(n, fs);
        const armlet_ast_node *prev_parent = p->parent;
        p->parent = fs;
        for (;;) {
          Token sel = expect(p, TK_NAME);
          ARR_APPEND(fs->fieldselect, selections, name_node(p, sel));
          skip_nl(p);
          if (!eat(p, TK_COMMA))
            break;
          skip_nl(p);
        }
        p->parent = prev_parent;
        skip_nl(p);
        Token gt = expect(p, TK_GT);
        fs->source->span.end = gt.end;
        return fs;
      }
      return n;
    }

    if (peek(p, 0).kind == TK_LPAREN) {
      advance(p);
      skip_nl(p);
      armlet_ast_node *n = new_node(p, AST_CALL, first.start, 0);
      n->call = NEW0(armlet_ast_call);
      n->call->name = base;
      reparent(base, n);
      if (!check(p, TK_RPAREN)) {
        const armlet_ast_node *prev_parent = p->parent;
        p->parent = n;
        for (;;) {
          ARR_APPEND(n->call, parameters, parse_expr(p, 0));
          skip_nl(p);
          if (!eat(p, TK_COMMA))
            break;
          skip_nl(p);
        }
        p->parent = prev_parent;
      }
      skip_nl(p);
      Token rp = expect(p, TK_RPAREN);
      n->source->span.end = rp.end;
      return n;
    }

    return base;
  }

  if (t.kind == TK_LT) {
    Token open = advance(p);
    armlet_ast_node *n = new_node(p, AST_BITSLURP, open.start, 0);
    n->bitslurp = NEW0(armlet_ast_bitslurp);
    const armlet_ast_node *prev_parent = p->parent;
    p->parent = n;
    do {
      Token nm = expect(p, TK_NAME);
      ARR_APPEND(n->bitslurp, elements, name_node(p, nm));
    } while (eat(p, TK_COMMA));
    p->parent = prev_parent;
    Token gt = expect(p, TK_GT);
    n->source->span.end = gt.end;
    return n;
  }

  {
    armlet_span span = {t.start, t.end};
    armlet_source_error(p->source, span,
                        "Unexpected token (kind %s) in expression",
                        tk_to_string[t.kind]);
  }
  return NULL;
}

#define MAX_LOOKAHEAD 4096

static bool lt_is_bit_select(Parser *p) {
  int depth = 0;
  for (int i = 1; i < MAX_LOOKAHEAD; i++) {
    TK k = peek(p, i).kind;
    switch (k) {
    case TK_EOF:
    case TK_NEWLINE:
    case TK_SEMI:
    case TK_DEDENT:
      return false;
    case TK_LT:
    case TK_LPAREN:
    case TK_LBRACKET:
    case TK_LBRACE:
      depth++;
      break;
    case TK_GT:
      if (depth == 0)
        return true;
      depth--;
      break;
    case TK_RPAREN:
    case TK_RBRACKET:
    case TK_RBRACE:
      if (depth == 0)
        return false;
      depth--;
      break;
    default:
      break;
    }
  }
  return false;
}

static armlet_ast_node *parse_expr(Parser *p, int min_bp) {
  if (++p->parse_depth > MAX_PARSE_DEPTH) {
    Token t = peek(p, 0);
    armlet_source_error(p->source, (armlet_span){t.start, t.end},
                        "Expression nesting too deep (limit: %d)",
                        MAX_PARSE_DEPTH);
  }
  armlet_ast_node *left = parse_atom(p);

  for (;;) {
    Token op = peek(p, 0);
    int bp;
    if (op.kind == TK_LT)
      bp = lt_is_bit_select(p) ? 23 : 2;
    else
      bp = infix_bp(op.kind);
    if (bp == 0 || bp <= min_bp)
      break;
    advance(p);

    if (op.kind == TK_LT && bp == 23) {
      armlet_ast_node *n = new_node(p, AST_BITSEL, left->source->span.start, 0);
      n->bitselect = NEW0(armlet_ast_bitselect);
      n->bitselect->source = left;
      reparent(left, n);
      const armlet_ast_node *prev_parent = p->parent;
      p->parent = n;
      do {
        ARR_APPEND(n->bitselect, selections, parse_expr(p, 2));
      } while (eat(p, TK_COMMA));
      p->parent = prev_parent;
      Token gt = expect(p, TK_GT);
      n->source->span.end = gt.end;
      left = n;
      continue;
    }

    if (op.kind == TK_UNKNOWN) {
      armlet_ast_node *n =
          new_node(p, AST_UNKNOWN, left->source->span.start, op.end);
      if (left->type == AST_TYPE_SPEC) {
        n->unknown = left->type_spec;
      } else {
        armlet_ast_type_spec *ts = NEW0(armlet_ast_type_spec);
        ts->name = left;
        n->unknown = ts;
      }
      reparent(left, n);
      left = n;
      continue;
    }

    if (op.kind == TK_IMPLEMENTATION_DEFINED) {
      Token str = expect(p, TK_STRING);
      armlet_ast_node *n = new_node(p, AST_IMPLEMENTATION_DEFINED,
                                    left->source->span.start, str.end);
      n->implementation_defined = NEW0(armlet_ast_implementation_defined);
      if (left->type == AST_TYPE_SPEC) {
        n->implementation_defined->type = left;
        reparent(left, n);
      } else {
        armlet_ast_node *wrap = new_node(
            p, AST_TYPE_SPEC, left->source->span.start, left->source->span.end);
        wrap->type_spec = NEW0(armlet_ast_type_spec);
        wrap->type_spec->name = left;
        reparent(left, wrap);
        n->implementation_defined->type = wrap;
        reparent(wrap, n);
      }
      n->implementation_defined->key = src_str(p, str.start + 1, str.end - 1);
      left = n;
      continue;
    }

    if (is_cmp_op(op.kind)) {
      armlet_ast_node *right = parse_expr(p, bp);
      armlet_ast_node *n = new_node(p, AST_CMP, left->source->span.start,
                                    right->source->span.end);
      n->cmp = NEW0(armlet_ast_cmp);
      n->cmp->left = left;
      n->cmp->right = right;
      n->cmp->op = cmpop_from_tk(op.kind);
      reparent(left, n);
      reparent(right, n);
      left = n;
      continue;
    }

    {
      armlet_ast_node *right = parse_expr(p, bp);
      armlet_ast_node *n = new_node(p, AST_BINOP, left->source->span.start,
                                    right->source->span.end);
      n->binop = NEW0(armlet_ast_binop);
      n->binop->left = left;
      n->binop->right = right;
      n->binop->op = binop_from_tk(op.kind);
      reparent(left, n);
      reparent(right, n);
      left = n;
    }
  }

  p->parse_depth--;
  return left;
}

static armlet_ast_node *parse_block(Parser *p) {
  Token start_tok = peek(p, 0);
  armlet_ast_node *n = new_node(p, AST_BLOCK, start_tok.start, 0);
  n->block = NEW0(armlet_ast_block);

  const armlet_ast_node *prev_parent = p->parent;
  p->parent = n;

  if (check(p, TK_NEWLINE)) {
    advance(p);

    if (check(p, TK_INDENT)) {
      advance(p);
      while (!check(p, TK_DEDENT) && !check(p, TK_EOF)) {
        armlet_ast_node *stmt = parse_statement(p);
        if (stmt)
          ARR_APPEND(n->block, nodes, stmt);
        eat(p, TK_NEWLINE);
      }
      Token dedent = expect(p, TK_DEDENT);
      n->source->span.end = dedent.end;
    }
  } else {
    while (!check(p, TK_NEWLINE) && !check(p, TK_EOF) && !check(p, TK_DEDENT)) {
      armlet_ast_node *stmt = parse_simple_statement(p);
      if (stmt)
        ARR_APPEND(n->block, nodes, stmt);
    }
    if (check(p, TK_NEWLINE)) {
      Token nl = advance(p);
      n->source->span.end = nl.end;
    }
  }

  p->parent = prev_parent;
  return n;
}

static armlet_ast_node *parse_return(Parser *p) {
  Token kw = expect(p, TK_RETURN);
  armlet_ast_node *n = new_node(p, AST_RETURN, kw.start, 0);
  n->return_ = NEW0(armlet_ast_return);
  const armlet_ast_node *prev_parent = p->parent;
  p->parent = n;
  if (!check(p, TK_SEMI)) {
    n->return_->return_ = parse_expr(p, 0);
  }
  p->parent = prev_parent;
  Token semi = expect(p, TK_SEMI);
  n->source->span.end = semi.end;
  return n;
}

static armlet_ast_node *parse_assert(Parser *p) {
  Token kw = expect(p, TK_ASSERT);
  armlet_ast_node *n = new_node(p, AST_ASSERT, kw.start, 0);
  n->assert = NEW0(armlet_ast_assert);
  const armlet_ast_node *prev_parent = p->parent;
  p->parent = n;
  n->assert->condition = parse_expr(p, 0);
  p->parent = prev_parent;
  Token semi = expect(p, TK_SEMI);
  n->source->span.end = semi.end;
  return n;
}

static armlet_ast_node *parse_use(Parser *p) {
  Token kw = expect(p, TK_USE);
  armlet_ast_node *n = new_node(p, AST_USE, kw.start, 0);
  n->use = NEW0(armlet_ast_use);
  const armlet_ast_node *prev_parent = p->parent;
  p->parent = n;
  n->use->target = parse_name_or_deref(p);
  p->parent = prev_parent;
  Token semi = expect(p, TK_SEMI);
  n->source->span.end = semi.end;
  return n;
}

static armlet_ast_node *parse_import(Parser *p) {
  Token kw = expect(p, TK_IMPORT);
  Token path = expect(p, TK_STRING);
  armlet_ast_node *n = new_node(p, AST_IMPORT, kw.start, path.end);
  n->import = NEW0(armlet_ast_import);
  n->import->path = src_str(p, path.start + 1, path.end - 1);
  expect(p, TK_SEMI);
  return n;
}

static armlet_ast_node *parse_var_def(Parser *p) {
  Token start = peek(p, 0);
  armlet_ast_node *n = new_node(p, AST_VAR_DEF, start.start, 0);
  n->var_def = NEW0(armlet_ast_var_definition);
  const armlet_ast_node *prev_parent = p->parent;
  p->parent = n;
  n->var_def->type = parse_type_spec(p);
  do {
    Token nm = expect(p, TK_NAME);
    ARR_APPEND(n->var_def, names, name_node(p, nm));
  } while (eat(p, TK_COMMA));
  p->parent = prev_parent;
  Token semi = expect(p, TK_SEMI);
  n->source->span.end = semi.end;
  return n;
}

static armlet_ast_node *parse_expr_or_assign(Parser *p) {
  Token start = peek(p, 0);
  armlet_ast_node *lhs = parse_expr(p, 0);

  if (check(p, TK_EQ)) {
    advance(p);
    armlet_ast_node *n = new_node(p, AST_ASSIGNMENT, start.start, 0);
    n->assignment = NEW0(armlet_ast_assignment);
    n->assignment->target = lhs;
    reparent(lhs, n);
    const armlet_ast_node *prev_parent = p->parent;
    p->parent = n;
    n->assignment->source = parse_expr(p, 0);
    p->parent = prev_parent;
    Token semi = expect(p, TK_SEMI);
    n->source->span.end = semi.end;
    return n;
  }

  if (lhs && lhs->type == AST_CALL &&
      (check(p, TK_NEWLINE) || check(p, TK_EOF) || check(p, TK_DEDENT))) {
    armlet_span span = {peek(p, 0).start, peek(p, 0).start};
    armlet_source_diagnostic(stderr, p->source, span, true,
                             "\e[1;33mWARNING\e[0m",
                             "Missing ';' after function call");
    return lhs;
  }

  Token semi = expect(p, TK_SEMI);
  lhs->source->span.end = semi.end;
  return lhs;
}

static armlet_ast_node *parse_simple_statement(Parser *p) {
  TK k = peek(p, 0).kind;
  switch (k) {
  case TK_RETURN:
    return parse_return(p);
  case TK_ASSERT:
    return parse_assert(p);
  case TK_USE:
    return parse_use(p);
  case TK_ARRAY: {
    Token start = advance(p);
    armlet_ast_node *type = parse_type_spec(p);
    armlet_ast_node *arr_name = parse_name_or_deref(p);
    expect(p, TK_LBRACKET);
    armlet_ast_node *lo = parse_expr(p, 0);
    expect(p, TK_DOTDOT);
    armlet_ast_node *hi = parse_expr(p, 0);
    expect(p, TK_RBRACKET);
    Token semi = expect(p, TK_SEMI);
    armlet_ast_node *n = new_node(p, AST_ARRAY, start.start, semi.end);
    n->array = NEW0(armlet_ast_array);
    n->array->type = type;
    n->array->name = arr_name;
    n->array->start = lo;
    n->array->end = hi;
    reparent(type, n);
    reparent(arr_name, n);
    reparent(lo, n);
    reparent(hi, n);
    return n;
  }
  default:
    if (is_var_def(p))
      return parse_var_def(p);
    if (is_typed_assign(p))
      return parse_typed_assign(p);
    return parse_expr_or_assign(p);
  }
}

static armlet_ast_node *parse_if_then(Parser *p) {
  Token kw = expect(p, TK_IF);
  armlet_ast_node *n = new_node(p, AST_IF, kw.start, 0);
  n->if_ = NEW0(armlet_ast_if);

  const armlet_ast_node *prev_parent = p->parent;
  p->parent = n;

  armlet_ast_cond_branch *first_br = NEW0(armlet_ast_cond_branch);
  first_br->condition = parse_expr(p, 0);
  expect(p, TK_THEN);
  first_br->consequence = parse_block(p);
  ARR_APPEND(n->if_, conditions, first_br);

  while (check(p, TK_ELSIF)) {
    advance(p);
    armlet_ast_cond_branch *br = NEW0(armlet_ast_cond_branch);
    br->condition = parse_expr(p, 0);
    expect(p, TK_THEN);
    br->consequence = parse_block(p);
    ARR_APPEND(n->if_, conditions, br);
  }

  if (check(p, TK_ELSE)) {
    advance(p);
    n->if_->alternative = parse_block(p);
    n->source->span.end = n->if_->alternative->source->span.end;
  } else {
    size_t last = n->if_->num_conditions - 1;
    n->source->span.end =
        n->if_->conditions[last]->consequence->source->span.end;
  }

  p->parent = prev_parent;
  return n;
}

static armlet_ast_node *parse_case_when(Parser *p) {
  Token kw = expect(p, TK_CASE);
  armlet_ast_node *n = new_node(p, AST_WHEN, kw.start, 0);
  n->case_ = NEW0(armlet_ast_case);

  const armlet_ast_node *prev_parent = p->parent;
  p->parent = n;

  n->case_->match = parse_expr(p, 0);
  expect(p, TK_OF);
  expect(p, TK_NEWLINE);
  expect(p, TK_INDENT);

  while (check(p, TK_WHEN)) {
    advance(p);
    armlet_ast_cond_branch *br = NEW0(armlet_ast_cond_branch);
    br->condition = parse_expr(p, 0);
    br->consequence = parse_block(p);
    ARR_APPEND(n->case_, cases, br);
    eat(p, TK_NEWLINE);
  }

  if (check(p, TK_OTHERWISE)) {
    advance(p);
    n->case_->otherwise = parse_block(p);
    eat(p, TK_NEWLINE);
  }

  Token dedent = expect(p, TK_DEDENT);
  n->source->span.end = dedent.end;
  p->parent = prev_parent;
  return n;
}

static armlet_ast_node *parse_while_loop(Parser *p) {
  Token kw = expect(p, TK_WHILE);
  armlet_ast_node *n = new_node(p, AST_LOOP, kw.start, 0);
  n->loop = NEW0(armlet_ast_loop);
  n->loop->loop_type = LOOP_WHILE;
  const armlet_ast_node *prev_parent = p->parent;
  p->parent = n;
  n->loop->condition = parse_expr(p, 0);
  expect(p, TK_DO);
  n->loop->block = parse_block(p);
  p->parent = prev_parent;
  n->source->span.end = n->loop->block->source->span.end;
  return n;
}

static armlet_ast_node *parse_repeat_until(Parser *p) {
  Token kw = expect(p, TK_REPEAT);
  armlet_ast_node *n = new_node(p, AST_LOOP, kw.start, 0);
  n->loop = NEW0(armlet_ast_loop);
  n->loop->loop_type = LOOP_REPEAT;

  const armlet_ast_node *prev_parent = p->parent;
  p->parent = n;

  if (check(p, TK_NEWLINE)) {
    n->loop->block = parse_block(p);
  } else {
    armlet_ast_node *blk = new_node(p, AST_BLOCK, kw.start, 0);
    blk->block = NEW0(armlet_ast_block);
    const armlet_ast_node *blk_prev = p->parent;
    p->parent = blk;
    armlet_ast_node *stmt = parse_simple_statement(p);
    p->parent = blk_prev;
    ARR_APPEND(blk->block, nodes, stmt);
    blk->source->span.end = stmt->source->span.end;
    n->loop->block = blk;
  }

  expect(p, TK_UNTIL);
  n->loop->condition = parse_expr(p, 0);
  p->parent = prev_parent;
  Token semi = expect(p, TK_SEMI);
  n->source->span.end = semi.end;
  return n;
}

static armlet_ast_node *parse_for_loop(Parser *p) {
  Token kw = expect(p, TK_FOR);
  Token var = expect(p, TK_NAME);
  expect(p, TK_EQ);
  armlet_ast_node *lo = parse_expr(p, 0);

  enum armlet_loop_tag ltype;
  if (check(p, TK_TO)) {
    advance(p);
    ltype = LOOP_FOR_TO;
  } else {
    expect(p, TK_DOWNTO);
    ltype = LOOP_FOR_DOWNTO;
  }

  armlet_ast_node *hi = parse_expr(p, 0);
  armlet_ast_node *body = parse_block(p);

  armlet_ast_node *n = new_node(p, AST_LOOP, kw.start, body->source->span.end);
  n->loop = NEW0(armlet_ast_loop);
  n->loop->loop_type = ltype;
  n->loop->range = NEW0(armlet_ast_loop_limit);

  reparent(lo, n);
  reparent(hi, n);
  reparent(body, n);
  const armlet_ast_node *prev_parent = p->parent;
  p->parent = n;
  n->loop->range->name = name_node(p, var);
  p->parent = prev_parent;
  n->loop->range->start = lo;
  n->loop->range->end = hi;
  n->loop->block = body;
  return n;
}

static enum armlet_vm_contract_type
infer_contract(armlet_ast_var_definition *vd) {
  if (!vd->type || vd->type->type != AST_TYPE_SPEC)
    return CONTRACT_NONE;
  armlet_ast_type_spec *ts = vd->type->type_spec;
  if (ts->size != NULL) {
    if (ts->size->type == AST_VALUE && ts->size->value->tag == VAL_NAME)
      return CONTRACT_NAMED;
    return CONTRACT_COMPUTED;
  }
  if (ts->name != NULL && ts->name->type == AST_VALUE &&
      ts->name->value->tag == VAL_NAME &&
      streq(ts->name->value->name, "integer"))
    return CONTRACT_IMMEDIATE;
  return CONTRACT_NONE;
}

static void parse_argument_list_def(Parser *p,
                                    armlet_ast_callable_definition *c) {
  expect(p, TK_LPAREN);
  skip_nl(p);
  if (!check(p, TK_RPAREN)) {
    for (;;) {
      armlet_ast_var_definition *vd = NEW0(armlet_ast_var_definition);
      if (check(p, TK_ARRAY)) {
        armlet_ast_node *av = parse_array_variable(p);
        vd->type = av;
      } else {
        vd->type = parse_type_spec(p);
        Token nm = expect(p, TK_NAME);
        ARR_APPEND(vd, names, name_node(p, nm));
        vd->contract = infer_contract(vd);
      }
      ARR_APPEND(c, parameters, vd);
      skip_nl(p);
      if (!eat(p, TK_COMMA))
        break;
      skip_nl(p);
    }
  }
  skip_nl(p);
  expect(p, TK_RPAREN);
}

static armlet_ast_node *parse_return_tuple(Parser *p) {
  Token open = expect(p, TK_LPAREN);
  skip_nl(p);
  armlet_ast_node *n = new_node(p, AST_TUPLE, open.start, 0);
  n->tuple = NEW0(armlet_ast_tuple);
  const armlet_ast_node *prev_parent = p->parent;
  p->parent = n;
  for (;;) {
    if (check(p, TK_RPAREN))
      break;
    if (check(p, TK_LPAREN)) {
      ARR_APPEND(n->tuple, elements, parse_return_tuple(p));
    } else {
      ARR_APPEND(n->tuple, elements, parse_type_spec(p));
    }
    skip_nl(p);
    if (!eat(p, TK_COMMA) || check(p, TK_RPAREN))
      break;
    skip_nl(p);
  }
  p->parent = prev_parent;
  skip_nl(p);
  Token close = expect(p, TK_RPAREN);
  n->source->span.end = close.end;
  return n;
}

static armlet_ast_node *parse_func_def(Parser *p) {
  Token start = peek(p, 0);
  armlet_ast_node *n = new_node(p, AST_FUNDEF, start.start, 0);
  n->callable_def = NEW0(armlet_ast_callable_definition);
  n->callable_def->type = CALLABLE_FUNC;

  const armlet_ast_node *prev_parent = p->parent;
  p->parent = n;

  if (check(p, TK_LPAREN)) {
    if (is_func_def_return_tuple(p)) {
      n->callable_def->return_type = parse_return_tuple(p);
    }
  } else if (peek(p, 0).kind != TK_NAME || is_func_def_with_return(p)) {
    int ts = type_spec_end(p, 0);
    if (ts > 0 && peek(p, ts).kind == TK_NAME) {
      n->callable_def->return_type = parse_type_spec(p);
    }
  }

  n->callable_def->name = parse_name_or_deref(p);
  parse_argument_list_def(p, n->callable_def);
  armlet_ast_node *body = parse_block(p);
  n->callable_def->body = body->block;
  n->source->span.end = body->source->span.end;
  p->parent = prev_parent;
  return n;
}

static armlet_ast_node *parse_getter(Parser *p) {
  Token start = peek(p, 0);
  armlet_ast_node *n = new_node(p, AST_FUNDEF, start.start, 0);
  n->callable_def = NEW0(armlet_ast_callable_definition);
  n->callable_def->type = CALLABLE_GETTER;

  const armlet_ast_node *prev_parent = p->parent;
  p->parent = n;

  n->callable_def->return_type = parse_type_spec(p);
  n->callable_def->name = parse_name_or_deref(p);

  if (check(p, TK_LBRACKET)) {
    advance(p);
    skip_nl(p);
    if (!check(p, TK_RBRACKET)) {
      for (;;) {
        armlet_ast_var_definition *vd = NEW0(armlet_ast_var_definition);
        vd->type = parse_type_spec(p);
        Token pname = expect(p, TK_NAME);
        ARR_APPEND(vd, names, name_node(p, pname));
        vd->contract = infer_contract(vd);
        ARR_APPEND(n->callable_def, parameters, vd);
        skip_nl(p);
        if (!eat(p, TK_COMMA))
          break;
        skip_nl(p);
      }
    }
    skip_nl(p);
    expect(p, TK_RBRACKET);
  }

  armlet_ast_node *body = parse_block(p);
  n->callable_def->body = body->block;
  n->source->span.end = body->source->span.end;
  p->parent = prev_parent;
  return n;
}

static armlet_ast_node *parse_setter(Parser *p) {
  Token start = peek(p, 0);
  armlet_ast_node *n = new_node(p, AST_FUNDEF, start.start, 0);
  n->callable_def = NEW0(armlet_ast_callable_definition);
  n->callable_def->type = CALLABLE_SETTER;

  const armlet_ast_node *prev_parent = p->parent;
  p->parent = n;

  n->callable_def->name = parse_name_or_deref(p);

  expect(p, TK_LBRACKET);
  skip_nl(p);
  if (!check(p, TK_RBRACKET)) {
    for (;;) {
      armlet_ast_var_definition *vd = NEW0(armlet_ast_var_definition);
      vd->type = parse_type_spec(p);
      Token pname = expect(p, TK_NAME);
      ARR_APPEND(vd, names, name_node(p, pname));
      vd->contract = infer_contract(vd);
      ARR_APPEND(n->callable_def, parameters, vd);
      skip_nl(p);
      if (!eat(p, TK_COMMA))
        break;
      skip_nl(p);
    }
  }
  skip_nl(p);
  expect(p, TK_RBRACKET);
  expect(p, TK_EQ);

  armlet_ast_var_definition *input = NEW0(armlet_ast_var_definition);
  input->type = parse_type_spec(p);
  Token iname = expect(p, TK_NAME);
  ARR_APPEND(input, names, name_node(p, iname));
  input->contract = infer_contract(input);
  n->callable_def->input_type = input;

  armlet_ast_node *body = parse_block(p);
  n->callable_def->body = body->block;
  n->source->span.end = body->source->span.end;
  p->parent = prev_parent;
  return n;
}

static armlet_ast_node *parse_type_decl(Parser *p) {
  Token kw = expect(p, TK_TYPE);

  armlet_ast_node *from = parse_name_or_deref(p);

  if (check(p, TK_EQ)) {
    advance(p);
    armlet_ast_node *n = new_node(p, AST_TYPE_ALIAS, kw.start, 0);
    n->type_alias = NEW0(armlet_ast_type_alias);
    n->type_alias->from = from;
    reparent(from, n);
    const armlet_ast_node *prev_parent = p->parent;
    p->parent = n;
    if (check(p, TK_LPAREN)) {
      n->type_alias->to = parse_return_tuple(p);
    } else {
      n->type_alias->to = parse_type_spec(p);
    }
    p->parent = prev_parent;
    Token semi = expect(p, TK_SEMI);
    n->source->span.end = semi.end;
    return n;
  }

  expect(p, TK_IS);
  expect(p, TK_LPAREN);
  skip_nl(p);

  armlet_ast_node *n = new_node(p, AST_TYPE, kw.start, 0);
  n->type_def = NEW0(armlet_ast_type_definition);
  if (from->type == AST_VALUE && from->value->tag == VAL_NAME)
    n->type_def->name = from->value->name;
  else
    n->type_def->name = strdup("?");

  const armlet_ast_node *prev_parent = p->parent;
  p->parent = n;
  for (;;) {
    if (check(p, TK_RPAREN))
      break;
    if (check(p, TK_ARRAY)) {
      armlet_ast_node *av = parse_array_variable(p);
      armlet_ast_node *param = new_node(
          p, AST_PARAMETER, av->source->span.start, av->source->span.end);
      param->parameter = NEW0(armlet_ast_parameter);
      param->parameter->type = av;
      reparent(av, param);
      ARR_APPEND(n->type_def, fields, param);
    } else {
      armlet_ast_node *ftype = parse_type_spec(p);
      Token fname = expect(p, TK_NAME);
      armlet_ast_node *param =
          new_node(p, AST_PARAMETER, ftype->source->span.start, fname.end);
      param->parameter = NEW0(armlet_ast_parameter);
      param->parameter->type = ftype;
      param->parameter->name = name_node(p, fname);
      reparent(ftype, param);
      ARR_APPEND(n->type_def, fields, param);
    }
    skip_nl(p);
    if (!eat(p, TK_COMMA) || check(p, TK_RPAREN))
      break;
    skip_nl(p);
  }
  p->parent = prev_parent;
  skip_nl(p);

  expect(p, TK_RPAREN);
  eat(p, TK_SEMI);
  n->source->span.end = peek(p, 0).start;
  return n;
}

static armlet_ast_node *parse_enum_def(Parser *p) {
  Token kw = expect(p, TK_ENUMERATION);
  Token nm = expect(p, TK_NAME);
  armlet_ast_node *n = new_node(p, AST_ENUM, kw.start, 0);
  n->enum_def = NEW0(armlet_ast_enum_definition);
  n->enum_def->name = tok_str(p, nm);
  expect(p, TK_LBRACE);
  skip_nl(p);
  while (!check(p, TK_RBRACE) && !check(p, TK_EOF)) {
    Token variant = expect(p, TK_NAME);
    ARR_APPEND(n->enum_def, elements, tok_str(p, variant));
    skip_nl(p);
    if (!eat(p, TK_COMMA))
      break;
    skip_nl(p);
  }
  expect(p, TK_RBRACE);
  Token semi = expect(p, TK_SEMI);
  n->source->span.end = semi.end;
  return n;
}

static armlet_ast_node *parse_array_definition(Parser *p) {
  Token kw = expect(p, TK_ARRAY);
  armlet_ast_node *type = parse_type_spec(p);
  armlet_ast_node *arr_name = parse_name_or_deref(p);
  expect(p, TK_LBRACKET);
  armlet_ast_node *lo = parse_expr(p, 0);
  expect(p, TK_DOTDOT);
  armlet_ast_node *hi = parse_expr(p, 0);
  expect(p, TK_RBRACKET);
  Token semi = expect(p, TK_SEMI);

  armlet_ast_node *n = new_node(p, AST_ARRAY, kw.start, semi.end);
  n->array = NEW0(armlet_ast_array);
  n->array->type = type;
  n->array->name = arr_name;
  n->array->start = lo;
  n->array->end = hi;
  reparent(type, n);
  reparent(arr_name, n);
  reparent(lo, n);
  reparent(hi, n);
  return n;
}

static armlet_ast_node *parse_bitlayout(Parser *p) {
  Token kw = expect(p, TK_BITLAYOUT);
  Token nm = expect(p, TK_NAME);

  armlet_ast_node *n = new_node(p, AST_BITLAYOUT, kw.start, 0);
  n->bitlayout = NEW0(armlet_ast_bitlayout);
  n->bitlayout->name = tok_str(p, nm);

  const armlet_ast_node *prev_parent = p->parent;
  p->parent = n;

  eat(p, TK_IS);
  expect(p, TK_LPAREN);
  skip_nl(p);

  armlet_string_builder mask = {0};
  armlet_sb_init(&mask, 32);
  size_t total = 0;

  for (;;) {
    if (check(p, TK_RPAREN))
      break;
    armlet_ast_bitlayout_member *bm = NEW0(armlet_ast_bitlayout_member);
    if (check(p, TK_BITSTRING)) {
      Token bs = advance(p);
      char *raw = src_str(p, bs.start + 1, bs.end - 1);
      size_t rlen = strlen(raw);
      char *bits = CHECKED_MALLOC(rlen + 1);
      size_t j = 0;
      for (size_t i = 0; i < rlen; i++) {
        char c = raw[i];
        if (c == '0' || c == '1' || c == 'x')
          bits[j++] = c;
        else if (c == 'X')
          bits[j++] = 'x';
      }
      bits[j] = '\0';
      free(raw);
      bm->mtype = BITLAYOUT_IMMEDIATE;
      bm->immediate = bits;
      bm->size = j;
      armlet_sb_append_string_sized(&mask, bits, j);
      total += j;
    } else {
      Token spec_name = expect(p, TK_NAME);
      expect(p, TK_COLON);
      Token sz_tok = expect(p, TK_INTEGER);
      char *sz_str = tok_str(p, sz_tok);
      char *endptr;
      long size = strtol(sz_str, &endptr, 10);
      free(sz_str);
      if (size <= 0 || size > 65535)
        armlet_source_error(p->source, (armlet_span){sz_tok.start, sz_tok.end},
                            "Bitlayout field size must be between 1 and 65535");
      bm->mtype = BITLAYOUT_NAMED;
      bm->name = tok_str(p, spec_name);
      bm->size = (size_t)size;
      if (total > 65535 - (size_t)size)
        armlet_source_error(p->source, (armlet_span){sz_tok.start, sz_tok.end},
                            "Bitlayout total size exceeds 65535 bits");
      armlet_sb_append_char_repeat(&mask, 'x', (size_t)size);
      total += (size_t)size;
    }
    ARR_APPEND(n->bitlayout, members, bm);
    skip_nl(p);
    if (!eat(p, TK_COMMA) || check(p, TK_RPAREN))
      break;
    skip_nl(p);
  }
  skip_nl(p);

  expect(p, TK_RPAREN);
  eat(p, TK_SEMI);

  if (check(p, TK_THEN)) {
    advance(p);
    expect(p, TK_DO);
    if (check(p, TK_NAME)) {
      Token arg_nm = expect(p, TK_NAME);
      n->bitlayout->argument_name = tok_str(p, arg_nm);
    }
    n->bitlayout->handler = parse_block(p);
    reparent(n->bitlayout->handler, n);
    n->source->span.end = n->bitlayout->handler->source->span.end;
  } else {
    n->source->span.end = peek(p, 0).start;
  }

  p->parent = prev_parent;
  n->bitlayout->compare_mask = (uint8_t *)mask.buf;
  n->bitlayout->total = total;
  return n;
}

static armlet_ast_node *parse_qualified_stmt(Parser *p) {
  Token kw = expect(p, TK_CONSTANT);

  armlet_ast_node *n = new_node(p, AST_QUALIFIED_LHS, kw.start, 0);
  n->qualified = NEW0(armlet_ast_qualified);
  n->qualified->qualifiers = QUALIFIER_CONSTANT;

  const armlet_ast_node *prev_parent = p->parent;
  p->parent = n;

  armlet_ast_node *inner;
  if (is_var_def(p) || (type_spec_end(p, 0) > 0 &&
                        peek(p, type_spec_end(p, 0)).kind == TK_NAME)) {
    armlet_ast_node *vd_node = new_node(p, AST_VAR_DEF, peek(p, 0).start, 0);
    vd_node->var_def = NEW0(armlet_ast_var_definition);
    const armlet_ast_node *vd_prev = p->parent;
    p->parent = vd_node;
    vd_node->var_def->type = parse_type_spec(p);
    armlet_ast_node *qnm = parse_name_or_deref(p);
    ARR_APPEND(vd_node->var_def, names, qnm);
    vd_node->source->span.end = qnm->source->span.end;
    p->parent = vd_prev;
    inner = vd_node;
  } else {
    Token nm = expect(p, TK_NAME);
    inner = name_node(p, nm);
  }

  n->qualified->inner = inner;
  n->qualified->qtype = (inner->type == AST_VAR_DEF) ? QUALIFIED_TYPED
                        : (inner->type == AST_TUPLE) ? QUALIFIED_TUPLE
                                                     : QUALIFIED_NAMED;

  expect(p, TK_EQ);
  armlet_ast_node *asgn = new_node(p, AST_ASSIGNMENT, kw.start, 0);
  asgn->assignment = NEW0(armlet_ast_assignment);
  asgn->assignment->target = n;
  reparent(n, asgn);
  p->parent = asgn;
  asgn->assignment->source = parse_expr(p, 0);
  p->parent = prev_parent;
  Token semi = expect(p, TK_SEMI);
  asgn->source->span.end = semi.end;
  return asgn;
}

static armlet_ast_node *parse_typed_assign(Parser *p) {
  Token start = peek(p, 0);

  armlet_ast_node *lhs = new_node(p, AST_QUALIFIED_LHS, start.start, 0);
  lhs->qualified = NEW0(armlet_ast_qualified);
  lhs->qualified->qualifiers = QUALIFIER_NONE;

  const armlet_ast_node *prev_parent = p->parent;
  p->parent = lhs;

  armlet_ast_node *vd_node = new_node(p, AST_VAR_DEF, start.start, 0);
  vd_node->var_def = NEW0(armlet_ast_var_definition);
  const armlet_ast_node *vd_prev = p->parent;
  p->parent = vd_node;
  vd_node->var_def->type = parse_type_spec(p);
  armlet_ast_node *nm_node = parse_name_or_deref(p);
  ARR_APPEND(vd_node->var_def, names, nm_node);
  vd_node->source->span.end = nm_node->source->span.end;
  p->parent = vd_prev;

  lhs->qualified->inner = vd_node;
  lhs->qualified->qtype = QUALIFIED_TYPED;
  lhs->source->span.end = vd_node->source->span.end;
  p->parent = prev_parent;

  expect(p, TK_EQ);

  armlet_ast_node *asgn = new_node(p, AST_ASSIGNMENT, start.start, 0);
  asgn->assignment = NEW0(armlet_ast_assignment);
  asgn->assignment->target = lhs;
  reparent(lhs, asgn);
  p->parent = asgn;
  asgn->assignment->source = parse_expr(p, 0);
  p->parent = prev_parent;
  Token semi = expect(p, TK_SEMI);
  asgn->source->span.end = semi.end;
  return asgn;
}

static armlet_ast_node *parse_complex_statement(Parser *p) {
  if (is_setter(p))
    return parse_setter(p);
  if (is_func_def_return_tuple(p))
    return parse_func_def(p);
  if (is_func_def_no_return(p))
    return parse_func_def(p);

  {
    int ts = type_spec_end(p, 0);
    if (ts > 0 && peek(p, ts).kind == TK_NAME) {
      int fn = name_or_deref_end(p, ts);
      if (fn > ts) {
        TK after = peek(p, fn).kind;
        if (after == TK_LPAREN) {
          int end = skip_balanced(p, fn, TK_LPAREN, TK_RPAREN);
          if (peek(p, end).kind == TK_NEWLINE)
            return parse_func_def(p);
        } else if (after == TK_LBRACKET || after == TK_NEWLINE) {
          return parse_getter(p);
        } else if (after == TK_COMMA || after == TK_SEMI) {
          return parse_var_def(p);
        } else if (after == TK_EQ) {
          return parse_typed_assign(p);
        }
      }
    }
  }

  return parse_expr_or_assign(p);
}

static armlet_ast_node *parse_statement(Parser *p) {
  while (check(p, TK_NEWLINE))
    advance(p);

  TK k = peek(p, 0).kind;
  switch (k) {
  case TK_EOF:
    return NULL;
  case TK_DEDENT:
    return NULL;
  case TK_IF:
    return parse_if_then(p);
  case TK_CASE:
    return parse_case_when(p);
  case TK_WHILE:
    return parse_while_loop(p);
  case TK_REPEAT:
    return parse_repeat_until(p);
  case TK_FOR:
    return parse_for_loop(p);
  case TK_USE:
    return parse_use(p);
  case TK_IMPORT:
    return parse_import(p);
  case TK_RETURN:
    return parse_return(p);
  case TK_ASSERT:
    return parse_assert(p);
  case TK_TYPE:
    return parse_type_decl(p);
  case TK_ENUMERATION:
    return parse_enum_def(p);
  case TK_ARRAY:
    return parse_array_definition(p);
  case TK_BITLAYOUT:
    return parse_bitlayout(p);
  case TK_CONSTANT:
    return parse_qualified_stmt(p);
  default:
    return parse_complex_statement(p);
  }
}

static armlet_ast_node *parse_suite(Parser *p) {
  armlet_ast_node *n = new_node(p, AST_SUITE, 0, 0);
  p->parent = n;

  while (!check(p, TK_EOF)) {
    while (check(p, TK_NEWLINE) || check(p, TK_INDENT) || check(p, TK_DEDENT))
      advance(p);
    if (check(p, TK_EOF))
      break;
    armlet_ast_node *stmt = parse_statement(p);
    if (stmt)
      ARR_APPEND(n, suite, stmt);
    eat(p, TK_NEWLINE);
  }
  n->source->span.end = peek(p, 0).start;
  return n;
}

armlet_ast_node *armlet_parse_source_pure(const char *src, size_t len,
                                          const char *filename, bool debug) {
  armlet_source *s = NEW0(armlet_source);
  s->data = (char *)src;
  s->file = (char *)filename;

  Parser p = {0};
  lexer_init(&p.lex, src, (uint32_t)len);
  p.source = s;
  p.buf = NULL;
  p.buf_cap = 0;
  p.buf_count = 0;

  armlet_ast_node *root = parse_suite(&p);
  free(p.buf);

  if (debug && root == NULL)
    fprintf(stderr, "[parser] parse returned NULL\n");

  return root;
}

armlet_ast_node *armlet_parse_file_pure(const char *path, bool debug) {
  size_t len = 0;
  char *data = slurp_file(path, &len);

  if (!data) {
    perror("armlet_parse_file_pure: failed to read file");
    return NULL;
  }

  return armlet_parse_source_pure(data, len, path, debug);
}

static bool try_load_path(char *path, Hashtable *imported_files, bool debug,
                          armlet_ast_node **out) {
  if (access(path, R_OK) != 0)
    return false;

  if (hashtable_find_str(imported_files, path, NULL) == 0) {
    *out = NULL;
    return true;
  }

  char *owned = strdup(path);
  hashtable_add_str(imported_files, owned, NULL);
  *out = armlet_parse_file_pure(owned, debug);
  return true;
}

static const char *armlet_import_extra_path = NULL;

void armlet_parser_set_import_path(const char *path) {
  armlet_import_extra_path = path;
}

armlet_ast_node *armlet_parse_import(const armlet_ast_node *n,
                                     Hashtable *imported_files, bool debug) {
  armlet_ast_import *imp = n->import;

  if (strstr(imp->path, "..") != NULL) {
    armlet_source_error_n(n, "Import path must not contain '..'");
  }

  char *cwd = getcwd(NULL, 0);
  char *rps = realpath(n->source->source.file, NULL);
  if (!rps) {
    free(cwd);
    armlet_source_error_n(n, "Cannot resolve path for import: %s",
                          n->source->source.file);
  }
  char *base = dirname(rps);

  const char *search[3];
  size_t search_n = 0;
  search[search_n++] = base;
  char *extra_abs = NULL;
  if (armlet_import_extra_path) {
    extra_abs = realpath(armlet_import_extra_path, NULL);
    if (!extra_abs || access(extra_abs, F_OK) != 0) {
      fprintf(stderr, "ERROR: import search path '%s' does not exist\n",
              armlet_import_extra_path);
      free(extra_abs);
      free(cwd);
      free(rps);
      exit(1);
    }
    search[search_n++] = extra_abs;
  }
  search[search_n++] = cwd;
  armlet_ast_node *result = NULL;

  for (size_t i = 0; i < search_n; ++i) {
    char *path = str_join("/", search[i], imp->path, NULL);

    if (try_load_path(path, imported_files, debug, &result)) {
      free(path);
      goto out;
    }

    if (!str_ends_with(imp->path, ".aml")) {
      char *alt = str_join(".", path, "aml", NULL);
      free(path);

      if (try_load_path(alt, imported_files, debug, &result)) {
        free(alt);
        goto out;
      }

      free(alt);
    } else {
      free(path);
    }
  }

  armlet_source_error_n(n, "Couldn't import '%s'", imp->path);
out:
  free(cwd);
  free(rps);
  free(extra_abs);
  return result;
}
