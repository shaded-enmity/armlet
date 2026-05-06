#define _GNU_SOURCE
#include "debugger_highlight.h"
#include "../utils/common.h"

#ifdef HAVE_TREE_SITTER

#include "debugger_theme.h"
#include "../utils/hashtable.h"

#include <stdlib.h>
#include <string.h>
#include <tree_sitter/api.h>

extern const TSLanguage *tree_sitter_armlet(void);

// Embedded highlights.scm query

static const char HIGHLIGHTS_SCM[] =
    "(qualifier) @keyword.storage.modifier\n"
    "(bits (expression) @variable)\n"
    "(bit_select bit_select_inner: (expression) @attribute)\n"
    "[(integer) (float)] @constant.numeric\n"
    "(return_tuple [(type_spec) (integer) (float)] @type.builtin)\n"
    "(type_spec) @type.builtin\n"
    "(argument_list (expression) @variable.parameter)\n"
    "(argument_list_def (name) @variable.parameter)\n"
    "(boolean_value) @constant.builtin.boolean\n"
    "(func_def [(name) (dereference)] @function)\n"
    "(func [(name) (dereference)] @function)\n"
    "(left_hand_side) @variable\n"
    "(enum_def enum_variant: (name) @type.enum.variant)\n"
    "(enum_def (name) @type.enum)\n"
    "[(string) (bitstring)] @string\n"
    "(interpolation) @string.special\n"
    "(interpolation (name) @variable)\n"
    "(getter (name) @variable.parameter)\n"
    "(getter name: [(name) (dereference)] @function)\n"
    "(setter (name) @variable.parameter)\n"
    "(setter name: [(name) (dereference)] @function)\n"
    "(comment) @comment\n"
    "(array_access name: [(name) (dereference)] @function)\n"
    "(array_access arguments: (expression (_)) @variable.parameter)\n"
    "[\";\" \",\" \":\" \".\"] @punctuation.delimiter\n"
    "[\"(\" \")\" \"[\" \"]\" \"{\" \"}\"] @punctuation.bracket\n"
    "[\"-\" \"!=\" \"*\" \"&&\" \"||\" \"MOD\" \"DIV\" \"^\" \"+\" \"<\" \"<<\" "
    "\"<=\" \"=\" \"==\" \">\" \">=\" \">>\" \"||\" \"IN\" \"EOR\" \"AND\" "
    "\"OR\" \":\" \"+:\" \"/\"] @operator\n"
    "(bit_select [\"<\" \">\"] @punctuation.bracket)\n"
    "[\"type\" \"assert\" \"for\" \"to\" \"downto\" \"repeat\" \"until\" "
    "\"while\" \"if\" \"then\" \"elsif\" \"else\" \"case\" \"of\" \"when\" "
    "\"otherwise\" \"return\" \"do\" \"is\" \"enumeration\" \"array\" "
    "\"import\" \"use\" \"UNKNOWN\" \"IMPLEMENTATION_DEFINED\" \"SEE\" "
    "\"UNDEFINED\" \"UNPREDICTABLE\" \"bitlayout\"] @keyword\n"
    "((func (name) @function.builtin) (#match? @function.builtin "
    "\"^(print|break|backtrace|inspect|set_bits_range_name|dispatch|"
    "serialize|deserialize|begin_implementation_defined|"
    "implementation_defined|end_implementation_defined|Log2|Real|"
    "RoundUp|RoundDown)$\"))\n";

static int capture_to_slot(const char *name, uint32_t len) {
  if (len >= 7 && memcmp(name, "keyword", 7) == 0)
    return CLR_SYN_KEYWORD;
  if (len >= 6 && memcmp(name, "string", 6) == 0)
    return CLR_SYN_STRING;
  if (len == 7 && memcmp(name, "comment", 7) == 0)
    return CLR_SYN_COMMENT;
  if (len >= 16 && memcmp(name, "constant.numeric", 16) == 0)
    return CLR_SYN_NUMBER;
  if (len >= 16 && memcmp(name, "function.builtin", 16) == 0)
    return CLR_SYN_BUILTIN;
  if (len >= 8 && memcmp(name, "function", 8) == 0)
    return CLR_SYN_FUNCTION;
  if (len >= 24 && memcmp(name, "constant.builtin.boolean", 24) == 0)
    return CLR_SYN_BUILTIN;
  if (len >= 4 && memcmp(name, "type", 4) == 0)
    return CLR_SYN_TYPE;
  if (len == 9 && memcmp(name, "attribute", 9) == 0)
    return CLR_SYN_TYPE;
  if (len >= 8 && memcmp(name, "operator", 8) == 0)
    return CLR_SYN_OPERATOR;
  return 0;
}

struct armlet_highlighter {
  TSParser *parser;
  TSQuery *query;
  Hashtable *cache;
};

armlet_highlighter *armlet_highlighter_init(void) {
  armlet_highlighter *hl = calloc(1, sizeof(armlet_highlighter));

  hl->parser = ts_parser_new();
  ts_parser_set_language(hl->parser, tree_sitter_armlet());

  uint32_t error_offset = 0;
  TSQueryError error_type = TSQueryErrorNone;
  hl->query = ts_query_new(tree_sitter_armlet(), HIGHLIGHTS_SCM,
                            (uint32_t)strlen(HIGHLIGHTS_SCM), &error_offset,
                            &error_type);
  if (!hl->query) {
    ts_parser_delete(hl->parser);
    free(hl);
    return NULL;
  }

  hashtable_new(32, &hl->cache);
  return hl;
}

static void free_result(armlet_highlight_result *hr) {
  if (!hr)
    return;
  free(hr->spans);
  free(hr);
}

void armlet_highlighter_cleanup(armlet_highlighter *hl) {
  if (!hl)
    return;

  if (hl->cache) {
    HtIterator *it = NULL;
    hashtable_iterate(hl->cache, &it);
    void *value;
    while (hashtable_iterator_next(it, NULL, &value))
      free_result((armlet_highlight_result *)value);
    hashtable_unref(hl->cache);
  }

  if (hl->query)
    ts_query_delete(hl->query);
  if (hl->parser)
    ts_parser_delete(hl->parser);
  free(hl);
}

static int cmp_spans(const void *a, const void *b) {
  const armlet_highlight_span *sa = (const armlet_highlight_span *)a;
  const armlet_highlight_span *sb = (const armlet_highlight_span *)b;
  if (sa->start_byte != sb->start_byte)
    return sa->start_byte < sb->start_byte ? -1 : 1;
  if (sa->end_byte != sb->end_byte)
    return sa->end_byte > sb->end_byte ? -1 : 1;
  return 0;
}

const armlet_highlight_result *
armlet_highlighter_get(armlet_highlighter *hl, const char *file_key,
                       const char *source, size_t source_len) {
  if (!hl || !source)
    return NULL;

  armlet_highlight_result *cached = NULL;
  if (hashtable_find_str(hl->cache, file_key, (void **)&cached) == 0)
    return cached;

  TSTree *tree =
      ts_parser_parse_string(hl->parser, NULL, source, (uint32_t)source_len);
  if (!tree)
    return NULL;

  TSQueryCursor *cursor = ts_query_cursor_new();
  ts_query_cursor_exec(cursor, hl->query, ts_tree_root_node(tree));

  size_t cap = 256;
  armlet_highlight_result *hr = calloc(1, sizeof(armlet_highlight_result));
  hr->spans = CHECKED_MALLOC(cap * sizeof(armlet_highlight_span));
  hr->num_spans = 0;

  TSQueryMatch match;
  while (ts_query_cursor_next_match(cursor, &match)) {
    for (uint16_t ci = 0; ci < match.capture_count; ci++) {
      TSQueryCapture cap_item = match.captures[ci];
      uint32_t name_len = 0;
      const char *name =
          ts_query_capture_name_for_id(hl->query, cap_item.index, &name_len);

      int slot = capture_to_slot(name, name_len);
      if (slot == 0)
        continue;

      uint32_t start = ts_node_start_byte(cap_item.node);
      uint32_t end = ts_node_end_byte(cap_item.node);

      if (hr->num_spans == cap) {
        cap *= 2;
        hr->spans = CHECKED_REALLOC(hr->spans, cap, sizeof(armlet_highlight_span));
      }
      hr->spans[hr->num_spans++] =
          (armlet_highlight_span){.start_byte = start,
                                  .end_byte = end,
                                  .color_slot = slot};
    }
  }

  ts_query_cursor_delete(cursor);
  ts_tree_delete(tree);

  if (hr->num_spans > 1)
    qsort(hr->spans, hr->num_spans, sizeof(armlet_highlight_span), cmp_spans);

  hashtable_add_str(hl->cache, file_key, hr);
  return hr;
}

int armlet_highlight_lookup(const armlet_highlight_result *hr,
                            uint32_t byte_offset) {
  if (!hr || hr->num_spans == 0)
    return 0;

  size_t lo = 0, hi = hr->num_spans;
  while (lo < hi) {
    size_t mid = lo + (hi - lo) / 2;
    if (hr->spans[mid].start_byte <= byte_offset)
      lo = mid + 1;
    else
      hi = mid;
  }

  for (size_t i = lo; i > 0; i--) {
    const armlet_highlight_span *s = &hr->spans[i - 1];
    if (s->start_byte <= byte_offset && s->end_byte > byte_offset)
      return s->color_slot;
  }
  return 0;
}

#endif
