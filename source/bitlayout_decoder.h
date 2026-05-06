#ifndef __ARMLET_BITLAYOUT_DECODER__
#define __ARMLET_BITLAYOUT_DECODER__

#include "ast.h"
#include "utils/tristate.h"
#include <stddef.h>
#include <stdint.h>

typedef struct {
  armlet_ast_bitlayout *layout;
  armlet_ast_node *node;
  armlet_bitvector *pattern;
  size_t specificity;
} armlet_decoder_entry;

typedef struct {
  armlet_decoder_entry *a;
  armlet_decoder_entry *b;
} armlet_decoder_ambiguity;

enum armlet_decode_node_type {
  DECODE_NODE_BRANCH,
  DECODE_NODE_LEAF,
  DECODE_NODE_FAIL,
};

typedef struct armlet_decode_node {
  enum armlet_decode_node_type type;
  union {
    struct {
      size_t bit_index;
      struct armlet_decode_node *on_zero;
      struct armlet_decode_node *on_one;
    } branch;

    struct {
      armlet_decoder_entry *entry;
    } leaf;
  };
} armlet_decode_node;

typedef struct {
  size_t bit_width;
  armlet_decode_node *root;
  size_t n_entries;
  armlet_decoder_entry **entries;
} armlet_decoder;

typedef struct {
  armlet_decoder_entry **entries;
  size_t n;
  size_t cap;
  size_t bit_width;
  bool error;
} armlet_decoder_builder;

armlet_decoder_entry *
armlet_decoder_entry_from_layout(armlet_ast_bitlayout *layout,
                                 armlet_ast_node *node);

void armlet_decoder_entry_free(armlet_decoder_entry *e);

int armlet_decoder_check_uniqueness(armlet_decoder_entry **entries, size_t n,
                                    armlet_decoder_ambiguity *out_conflict);

void armlet_decoder_report_ambiguity(const armlet_decoder_ambiguity *conflict);

armlet_decoder_builder *armlet_decoder_builder_new(void);

void armlet_decoder_builder_add(armlet_decoder_builder *b,
                                armlet_ast_bitlayout *layout,
                                armlet_ast_node *node);

armlet_decoder *armlet_decoder_builder_finish(armlet_decoder_builder *b);

void armlet_decoder_free(armlet_decoder *decoder);

armlet_ast_bitlayout *armlet_decoder_dispatch(const armlet_decoder *decoder,
                                              const uint8_t *bits);

#endif
