#include "bitlayout_decoder.h"
#include "utils/common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

armlet_decoder_entry *
armlet_decoder_entry_from_layout(armlet_ast_bitlayout *layout,
                                 armlet_ast_node *node) {
  armlet_decoder_entry *entry = NEW0(armlet_decoder_entry);
  entry->layout = layout;
  entry->node = node;
  entry->pattern = armlet_bitvector_new(layout->total);

  if (entry->pattern == NULL) {
    free(entry);
    return NULL;
  }

  armlet_bitvector_from_string(entry->pattern,
                               (const char *)layout->compare_mask);
  entry->specificity = armlet_bitvector_count_known(entry->pattern);

  return entry;
}

void armlet_decoder_entry_free(armlet_decoder_entry *e) {
  if (e == NULL)
    return;
  armlet_bitvector_free(e->pattern);
  free(e);
}

int armlet_decoder_check_uniqueness(armlet_decoder_entry **entries, size_t n,
                                    armlet_decoder_ambiguity *out_conflict) {
  for (size_t i = 0; i < n; i++) {
    for (size_t j = i + 1; j < n; j++) {
      if (armlet_bitvector_equal_wildcard(entries[i]->pattern,
                                          entries[j]->pattern)) {
        if (out_conflict) {
          out_conflict->a = entries[i];
          out_conflict->b = entries[j];
        }
        return -1;
      }
    }
  }
  return 0;
}

void armlet_decoder_report_ambiguity(const armlet_decoder_ambiguity *conflict) {
  char *pa = armlet_bitvector_to_string_alloc(conflict->a->pattern);
  char *pb = armlet_bitvector_to_string_alloc(conflict->b->pattern);
  fprintf(stderr, "Ambiguous bitlayout patterns:\n");
  fprintf(stderr, "  '%s': %s\n", conflict->a->layout->name, pa);
  fprintf(stderr, "  '%s': %s\n", conflict->b->layout->name, pb);
  free(pa);
  free(pb);

  if (conflict->b->node)
    armlet_source_error_n(conflict->b->node,
                          "Ambiguous bitlayout: '%s' overlaps with '%s'",
                          conflict->b->layout->name, conflict->a->layout->name);
}

static size_t pick_best_bit(armlet_decoder_entry **candidates, size_t n,
                            const uint8_t *decided, size_t bit_width) {
  size_t best = SIZE_MAX, best_count = 0;
  for (size_t i = 0; i < bit_width; i++) {
    if (decided[i])
      continue;
    size_t count = 0;
    for (size_t j = 0; j < n; j++) {
      int is_x = 0;
      armlet_bitvector_get_bit(candidates[j]->pattern, i, &is_x);
      if (!is_x)
        count++;
    }
    if (count > best_count) {
      best_count = count;
      best = i;
    }
  }
  return (best_count > 0) ? best : SIZE_MAX;
}

static armlet_decode_node *build_node(armlet_decoder_entry **candidates,
                                      size_t n_candidates, uint8_t *decided,
                                      size_t bit_width) {
  armlet_decode_node *node = NEW0(armlet_decode_node);

  if (n_candidates == 0) {
    node->type = DECODE_NODE_FAIL;
    return node;
  }

  if (n_candidates == 1) {
    node->type = DECODE_NODE_LEAF;
    node->leaf.entry = candidates[0];
    return node;
  }

  size_t best = pick_best_bit(candidates, n_candidates, decided, bit_width);
  if (best == SIZE_MAX) {
    node->type = DECODE_NODE_FAIL;
    return node;
  }

  armlet_decoder_entry **zeros = CHECKED_MALLOC(n_candidates * sizeof(*zeros));
  armlet_decoder_entry **ones = CHECKED_MALLOC(n_candidates * sizeof(*ones));
  size_t nz = 0, no = 0;

  for (size_t j = 0; j < n_candidates; j++) {
    int is_x = 0;
    int val = armlet_bitvector_get_bit(candidates[j]->pattern, best, &is_x);
    if (is_x) {
      zeros[nz++] = candidates[j];
      ones[no++] = candidates[j];
    } else if (val == 0) {
      zeros[nz++] = candidates[j];
    } else {
      ones[no++] = candidates[j];
    }
  }

  decided[best] = 1;
  uint8_t *dc0 = CHECKED_MALLOC(bit_width);
  uint8_t *dc1 = CHECKED_MALLOC(bit_width);
  memcpy(dc0, decided, bit_width);
  memcpy(dc1, decided, bit_width);

  node->type = DECODE_NODE_BRANCH;
  node->branch.bit_index = best;
  node->branch.on_zero = build_node(zeros, nz, dc0, bit_width);
  node->branch.on_one = build_node(ones, no, dc1, bit_width);

  free(dc0);
  free(dc1);
  free(zeros);
  free(ones);
  return node;
}

static void decode_node_free(armlet_decode_node *node) {
  if (!node)
    return;
  if (node->type == DECODE_NODE_BRANCH) {
    decode_node_free(node->branch.on_zero);
    decode_node_free(node->branch.on_one);
  }
  free(node);
}

armlet_decoder_builder *armlet_decoder_builder_new(void) {
  return NEW0(armlet_decoder_builder);
}

void armlet_decoder_builder_add(armlet_decoder_builder *b,
                                armlet_ast_bitlayout *layout,
                                armlet_ast_node *node) {
  if (b->error)
    return;

  if (b->n == 0) {
    b->bit_width = layout->total;
  } else if (layout->total != b->bit_width) {
    b->error = true;
    if (node)
      armlet_source_error_n(
          node,
          "Bitlayout '%s' is %zu bits wide but this decoder expects %zu bits",
          layout->name, layout->total, b->bit_width);
    return;
  }

  armlet_decoder_entry *entry = armlet_decoder_entry_from_layout(layout, node);
  if (!entry) {
    b->error = true;
    return;
  }

  if (b->n == b->cap) {
    b->cap = b->cap ? b->cap * 2 : 8;
    b->entries = CHECKED_REALLOC(b->entries, b->cap, sizeof(*b->entries));
  }
  b->entries[b->n++] = entry;
}

armlet_decoder *armlet_decoder_builder_finish(armlet_decoder_builder *b) {
  armlet_decoder *dec = NULL;

  if (b->error || b->n == 0)
    goto done;

  armlet_decoder_ambiguity conflict;
  if (armlet_decoder_check_uniqueness(b->entries, b->n, &conflict) != 0) {
    armlet_decoder_report_ambiguity(&conflict);
    goto done;
  }

  dec = NEW0(armlet_decoder);
  dec->bit_width = b->bit_width;
  dec->n_entries = b->n;
  dec->entries = b->entries;
  b->entries = NULL;
  b->n = 0;

  uint8_t *decided = calloc(b->bit_width, 1);
  dec->root = build_node(dec->entries, dec->n_entries, decided, b->bit_width);
  free(decided);

done:
  for (size_t i = 0; i < b->n; i++)
    armlet_decoder_entry_free(b->entries[i]);
  free(b->entries);
  free(b);
  return dec;
}

void armlet_decoder_free(armlet_decoder *dec) {
  if (!dec)
    return;
  decode_node_free(dec->root);
  for (size_t i = 0; i < dec->n_entries; i++)
    armlet_decoder_entry_free(dec->entries[i]);
  free(dec->entries);
  free(dec);
}

static bool leaf_matches(const armlet_decoder_entry *entry,
                         const uint8_t *bits) {
  const uint8_t *mask = entry->layout->compare_mask;
  size_t total = entry->layout->total;
  for (size_t i = 0; i < total; i++) {
    if (mask[i] != 'x' && mask[i] != bits[i])
      return false;
  }
  return true;
}

static armlet_ast_bitlayout *dispatch_linear(const armlet_decoder *dec,
                                             const uint8_t *bits) {
  for (size_t i = 0; i < dec->n_entries; i++) {
    if (leaf_matches(dec->entries[i], bits))
      return dec->entries[i]->layout;
  }
  return NULL;
}

armlet_ast_bitlayout *armlet_decoder_dispatch(const armlet_decoder *dec,
                                              const uint8_t *bits) {
  assert(dec != NULL);
  assert(bits != NULL);

  armlet_decode_node *node = dec->root;
  while (node->type == DECODE_NODE_BRANCH) {
    uint8_t c = bits[node->branch.bit_index];
    if (c == '0')
      node = node->branch.on_zero;
    else if (c == '1')
      node = node->branch.on_one;
    else
      return dispatch_linear(dec, bits);
  }

  if (node->type == DECODE_NODE_LEAF)
    return leaf_matches(node->leaf.entry, bits) ? node->leaf.entry->layout
                                                : NULL;
  return NULL;
}
