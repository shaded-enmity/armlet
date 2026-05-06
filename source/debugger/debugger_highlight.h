#ifndef __ARMLET_DEBUGGER_HIGHLIGHT__
#define __ARMLET_DEBUGGER_HIGHLIGHT__

#ifdef HAVE_TREE_SITTER

#include <stddef.h>
#include <stdint.h>

typedef struct {
  uint32_t start_byte;
  uint32_t end_byte;
  int color_slot; // CLR_SYN_* value, or 0 for no highlight
} armlet_highlight_span;

typedef struct {
  armlet_highlight_span *spans;
  size_t num_spans;
} armlet_highlight_result;

typedef struct armlet_highlighter armlet_highlighter;

armlet_highlighter *armlet_highlighter_init(void);
void armlet_highlighter_cleanup(armlet_highlighter *hl);

const armlet_highlight_result *
armlet_highlighter_get(armlet_highlighter *hl, const char *file_key,
                       const char *source, size_t source_len);

int armlet_highlight_lookup(const armlet_highlight_result *hr,
                            uint32_t byte_offset);

#else

// Stubs when tree-sitter is not available
#include <stddef.h>
typedef struct armlet_highlighter armlet_highlighter;
static inline armlet_highlighter *armlet_highlighter_init(void) {
  return NULL;
}
static inline void armlet_highlighter_cleanup(armlet_highlighter *hl) {
  (void)hl;
}

#endif
#endif
