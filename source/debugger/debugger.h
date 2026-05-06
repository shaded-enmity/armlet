#ifndef __ARMLET_DEBUGGER__
#define __ARMLET_DEBUGGER__

#include "../interpreter.h"
#include "../diagnostics.h"
#include "../utils/hashtable.h"

typedef enum {
  DBG_RUN,
  DBG_STEP,
  DBG_NEXT,
  DBG_FINISH,
  DBG_PAUSED,
} armlet_dbg_mode;

typedef struct {
  char *file;
  size_t line;
  bool enabled;
} armlet_breakpoint;

typedef struct {
  char *name;
} armlet_watch_entry;

typedef struct {
  char *name;
} armlet_pending_bp;

typedef struct armlet_debugger_tui armlet_debugger_tui;

typedef struct armlet_debugger {
  armlet_dbg_mode mode;

  DEFINE_ARRAY(armlet_breakpoint *, breakpoints);
  DEFINE_ARRAY(armlet_watch_entry *, watches);
  DEFINE_ARRAY(armlet_pending_bp *, pending_bps);

  size_t step_frame_depth;
  const char *step_file;
  size_t step_line;

  const char *current_file;
  size_t current_line;
  armlet_ast_node *current_node;
  armlet_vm_context *vm;

  // After continuing from a breakpoint, skip re-triggering the same line
  // until we move to a different statement at the same frame depth.
  const char *skip_bp_file;
  size_t skip_bp_line;
  size_t skip_bp_frame;

  bool finished;
  bool restart_requested;

  size_t selected_frame;

  const char *view_file;
  size_t view_line;

  char *last_search;

  Hashtable *source_cache;
  armlet_debugger_tui *tui;

#ifdef HAVE_TREE_SITTER
  struct armlet_highlighter *highlighter;
#endif

  armlet_ast_node *ast_root;
  char **completion_names;
  size_t num_completion_names;
} armlet_debugger;

armlet_debugger *armlet_debugger_init(armlet_vm_context *vm);
void armlet_debugger_cleanup(armlet_debugger *dbg);

void armlet_debugger_hook(armlet_vm_context *vm, armlet_ast_node *n,
                          void *userdata);

bool armlet_debugger_post_run(armlet_debugger *dbg);

bool armlet_debugger_has_breakpoint(armlet_debugger *dbg, const char *file,
                                    size_t line);

int armlet_debugger_breakpoint_state(armlet_debugger *dbg, const char *file,
                                     size_t line);

void armlet_debugger_toggle_breakpoint(armlet_debugger *dbg, const char *file,
                                       size_t line);

char *armlet_debugger_resolve_var(armlet_debugger *dbg, const char *name);

char *armlet_debugger_resolve_var_in_frame(armlet_debugger *dbg, const char *name,
                                           size_t frame_idx);

typedef struct {
  char **lines;
  size_t num_lines;
  char *raw;
  size_t raw_len;
  size_t *line_offsets;
} armlet_debugger_source;

armlet_debugger_source *armlet_debugger_get_source(armlet_debugger *dbg,
                                                   const char *file);

bool armlet_debugger_is_statement(enum armlet_ast_node_type type);

const char *armlet_debugger_match_file(armlet_debugger *dbg, const char *fragment);

typedef struct {
  const char *file;
  size_t line;
  const char *params;
} armlet_debugger_func_loc;

size_t armlet_debugger_find_functions(armlet_debugger *dbg, const char *name,
                                      armlet_debugger_func_loc *locs,
                                      size_t max_locs);

void armlet_debugger_resolve_pending(armlet_debugger *dbg);

void armlet_debugger_collect_completions(armlet_debugger *dbg);

#endif
