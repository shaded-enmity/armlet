#define _GNU_SOURCE
#include "debugger.h"
#include "debugger_highlight.h"
#include "debugger_tui.h"

#include "../parser.h"
#include "../utils/string.h"

#include <stdlib.h>
#include <string.h>

bool armlet_debugger_is_statement(enum armlet_ast_node_type type) {
  switch (type) {
  case AST_ASSIGNMENT:
  case AST_VAR_DEF:
  case AST_ARRAY:
  case AST_CALL:
  case AST_IF:
  case AST_WHEN:
  case AST_LOOP:
  case AST_ASSERT:
  case AST_RETURN:
  case AST_IMPORT:
  case AST_USE:
  case AST_TRAP:
  case AST_FUNDEF:
  case AST_ENUM:
  case AST_TYPE:
  case AST_TYPE_ALIAS:
  case AST_BITLAYOUT:
    return true;
  default:
    return false;
  }
}

static bool node_line_range(const armlet_ast_node *n, const char **file,
                            size_t *start, size_t *end) {
  if (!n || !n->source || !n->source->source.data)
    return false;
  armlet_line_info li =
      armlet_source_line_info_one(&n->source->source, n->source->span);
  if (!li.line)
    return false;
  *file = n->source->source.file;
  *start = li.lineno;
  *end = li.lineno;
  for (uint32_t i = n->source->span.start; i < n->source->span.end; i++) {
    if (n->source->source.data[i] == '\n')
      (*end)++;
  }
  free(li.line);
  return true;
}

static size_t find_statement_start(const armlet_ast_node *n,
                                   const char *target_file,
                                   size_t target_line) {
  if (!n || !n->source)
    return 0;

  switch (n->type) {
  case AST_SUITE:
    for (size_t i = 0; i < n->num_suite; i++) {
      size_t r = find_statement_start(n->suite[i], target_file, target_line);
      if (r)
        return r;
    }
    return 0;
  case AST_BLOCK:
    for (size_t i = 0; i < n->block->num_nodes; i++) {
      size_t r =
          find_statement_start(n->block->nodes[i], target_file, target_line);
      if (r)
        return r;
    }
    return 0;
  case AST_IF:
    for (size_t i = 0; i < n->if_->num_conditions; i++) {
      size_t r = find_statement_start(n->if_->conditions[i]->consequence,
                                      target_file, target_line);
      if (r)
        return r;
    }
    if (n->if_->alternative) {
      size_t r = find_statement_start(n->if_->alternative, target_file,
                                      target_line);
      if (r)
        return r;
    }
    return 0;
  case AST_WHEN:
    for (size_t i = 0; i < n->case_->num_cases; i++) {
      size_t r = find_statement_start(n->case_->cases[i]->consequence,
                                      target_file, target_line);
      if (r)
        return r;
    }
    if (n->case_->otherwise) {
      size_t r = find_statement_start(n->case_->otherwise, target_file,
                                      target_line);
      if (r)
        return r;
    }
    return 0;
  case AST_LOOP:
    if (n->loop->block) {
      size_t r =
          find_statement_start(n->loop->block, target_file, target_line);
      if (r)
        return r;
    }
    return 0;
  case AST_FUNDEF:
    if (n->callable_def->body) {
      for (size_t i = 0; i < n->callable_def->body->num_nodes; i++) {
        size_t r = find_statement_start(n->callable_def->body->nodes[i],
                                        target_file, target_line);
        if (r)
          return r;
      }
    }
    return 0;
  case AST_BITLAYOUT:
    if (n->bitlayout->handler) {
      size_t r = find_statement_start(n->bitlayout->handler, target_file,
                                      target_line);
      if (r)
        return r;
    }
    return 0;
  default:
    break;
  }

  if (!armlet_debugger_is_statement(n->type))
    return 0;

  const char *file;
  size_t start, end;
  if (!node_line_range(n, &file, &start, &end))
    return 0;

  if (!file || !target_file || strcmp(file, target_file) != 0)
    return 0;

  if (target_line >= start && target_line <= end)
    return start;

  return 0;
}

static size_t span_extra_lines(const armlet_source *src, armlet_span span) {
  if (!src || !src->data)
    return 0;
  size_t count = 0;
  for (uint32_t i = span.start; i < span.end; i++) {
    if (src->data[i] == '\n')
      count++;
  }
  return count;
}

static bool has_breakpoint_in_range(armlet_debugger *dbg, const char *file,
                                    size_t start_line, size_t end_line) {
  for (size_t i = 0; i < dbg->num_breakpoints; i++) {
    armlet_breakpoint *bp = dbg->breakpoints[i];
    if (!bp->enabled)
      continue;
    if (bp->line < start_line || bp->line > end_line)
      continue;
    if (bp->file == file ||
        (bp->file && file && strcmp(bp->file, file) == 0))
      return true;
  }
  return false;
}

static bool is_container_statement(enum armlet_ast_node_type type) {
  switch (type) {
  case AST_IF:
  case AST_WHEN:
  case AST_LOOP:
  case AST_FUNDEF:
  case AST_ENUM:
  case AST_TYPE:
  case AST_BITLAYOUT:
    return true;
  default:
    return false;
  }
}

static bool should_pause(armlet_debugger *dbg, armlet_ast_node *n) {
  bool is_stmt = armlet_debugger_is_statement(n->type);

  switch (dbg->mode) {
  case DBG_PAUSED:
    return true;

  case DBG_STEP:
    return is_stmt;

  case DBG_NEXT: {
    if (!is_stmt)
      return false;
    if (dbg->vm->num_frames > dbg->step_frame_depth)
      return false;
    if (dbg->step_file && n->source && n->source->source.file &&
        strcmp(dbg->step_file, n->source->source.file) != 0)
      return false;
    if (dbg->step_line != 0 && n->source) {
      armlet_line_info li =
          armlet_source_line_info_one(&n->source->source, n->source->span);
      bool same = (li.lineno == dbg->step_line);
      free(li.line);
      if (same)
        return false;
    }
    return true;
  }

  case DBG_FINISH: {
    if (!is_stmt)
      return false;
    return dbg->vm->num_frames < dbg->step_frame_depth;
  }

  case DBG_RUN: {
    if (!is_stmt || !n->source)
      return false;
    armlet_line_info li =
        armlet_source_line_info_one(&n->source->source, n->source->span);

    // Track position only at a shallower frame depth than where `c`
    // was pressed, so the source view returns to the caller's file
    // rather than staying inside a library function.
    if (dbg->vm->num_frames < dbg->skip_bp_frame) {
      dbg->current_file = n->source->source.file;
      dbg->current_line = li.lineno;
      dbg->current_node = n;
    }

    size_t end_line = li.lineno;
    if (!is_container_statement(n->type))
      end_line += span_extra_lines(&n->source->source, n->source->span);
    bool hit = has_breakpoint_in_range(dbg, n->source->source.file,
                                       li.lineno, end_line);
    if (hit && dbg->skip_bp_line == li.lineno && dbg->skip_bp_file &&
        n->source->source.file &&
        strcmp(dbg->skip_bp_file, n->source->source.file) == 0) {
      free(li.line);
      return false;
    }
    free(li.line);
    if (dbg->skip_bp_line != 0 && li.lineno != dbg->skip_bp_line &&
        dbg->vm->num_frames <= dbg->skip_bp_frame)
      dbg->skip_bp_line = 0;
    return hit;
  }

  default:
    return false;
  }
}

void armlet_debugger_hook(armlet_vm_context *vm, armlet_ast_node *n,
                          void *userdata) {
  armlet_debugger *dbg = (armlet_debugger *)userdata;
  if (!dbg || !n || !n->source)
    return;

  if (vm->break_requested) {
    vm->break_requested = false;
    dbg->mode = DBG_STEP;
  }

  armlet_debugger_resolve_pending(dbg);

  if (!should_pause(dbg, n))
    return;

  armlet_line_info li =
      armlet_source_line_info_one(&n->source->source, n->source->span);
  dbg->current_file = n->source->source.file;
  dbg->current_line = li.lineno;
  dbg->current_node = n;
  free(li.line);

  dbg->mode = DBG_PAUSED;
  dbg->selected_frame = dbg->vm->num_frames - 1;
  dbg->view_file = NULL;
  dbg->skip_bp_file = NULL;
  dbg->skip_bp_line = 0;

  fflush(stdout);
  armlet_debugger_tui_drain_stdout(dbg->tui);

  armlet_debugger_tui_center_on_line(dbg->tui, dbg->current_line);
  armlet_debugger_tui_draw(dbg->tui);
  armlet_debugger_tui_pause(dbg->tui);
}

armlet_debugger *armlet_debugger_init(armlet_vm_context *vm) {
  armlet_debugger *dbg = calloc(1, sizeof(armlet_debugger));
  dbg->vm = vm;
  dbg->mode = DBG_STEP;
  Hashtable *sc = NULL;
  hashtable_new(64, &sc);
  dbg->source_cache = sc;

  dbg->tui = armlet_debugger_tui_init(dbg);

#ifdef HAVE_TREE_SITTER
  dbg->highlighter = armlet_highlighter_init();
  if (!dbg->highlighter)
    armlet_debugger_tui_output(dbg->tui,
        "Warning: syntax highlighting unavailable (tree-sitter init failed)");
#endif

  vm->debugger_hook = armlet_debugger_hook;
  vm->debugger_userdata = dbg;

  return dbg;
}

static void collect_func_names(armlet_ast_node *n, Hashtable *imported_files,
                               char **names, size_t *count, size_t cap) {
  if (!n)
    return;

  switch (n->type) {
  case AST_SUITE:
    for (size_t i = 0; i < n->num_suite; i++)
      collect_func_names(n->suite[i], imported_files, names, count, cap);
    break;
  case AST_BLOCK:
    for (size_t i = 0; i < n->block->num_nodes; i++)
      collect_func_names(n->block->nodes[i], imported_files, names, count, cap);
    break;
  case AST_IMPORT: {
    armlet_ast_node *imported = armlet_parse_import(n, imported_files, false);
    if (imported)
      collect_func_names(imported, imported_files, names, count, cap);
    break;
  }
  case AST_FUNDEF: {
    const armlet_ast_node *nm = n->callable_def->name;
    if (nm && nm->type == AST_VALUE && nm->value->tag == VAL_NAME) {
      const char *fname = nm->value->name;
      bool dup = false;
      for (size_t i = 0; i < *count; i++) {
        if (strcmp(names[i], fname) == 0) {
          dup = true;
          break;
        }
      }
      if (!dup && *count < cap)
        names[(*count)++] = (char *)fname;
    }
    break;
  }
  default:
    break;
  }
}

static int cmp_str(const void *a, const void *b) {
  return strcmp(*(const char **)a, *(const char **)b);
}

void armlet_debugger_collect_completions(armlet_debugger *dbg) {
  if (dbg->completion_names)
    return;

  if (!dbg->ast_root)
    return;

  Hashtable *scratch_imports = NULL;
  hashtable_new(64, &scratch_imports);

  size_t cap = 1024;
  char **tmp = CHECKED_MALLOC(cap * sizeof(char *));
  size_t count = 0;

  collect_func_names(dbg->ast_root, scratch_imports, tmp, &count, cap);

  hashtable_unref(scratch_imports);

  qsort(tmp, count, sizeof(char *), cmp_str);

  dbg->completion_names = tmp;
  dbg->num_completion_names = count;
}

void armlet_debugger_cleanup(armlet_debugger *dbg) {
  if (!dbg)
    return;

  if (dbg->vm) {
    dbg->vm->debugger_hook = NULL;
    dbg->vm->debugger_userdata = NULL;
  }

  armlet_debugger_tui_cleanup(dbg->tui);

#ifdef HAVE_TREE_SITTER
  armlet_highlighter_cleanup(dbg->highlighter);
#endif

  for (size_t i = 0; i < dbg->num_breakpoints; i++) {
    free(dbg->breakpoints[i]->file);
    free(dbg->breakpoints[i]);
  }
  free(dbg->breakpoints);

  for (size_t i = 0; i < dbg->num_watches; i++) {
    free(dbg->watches[i]->name);
    free(dbg->watches[i]);
  }
  free(dbg->watches);

  for (size_t i = 0; i < dbg->num_pending_bps; i++) {
    free(dbg->pending_bps[i]->name);
    free(dbg->pending_bps[i]);
  }
  free(dbg->pending_bps);

  free(dbg->completion_names);

  free(dbg->last_search);

  if (dbg->source_cache) {
    HtIterator *it = NULL;
    hashtable_iterate(dbg->source_cache, &it);
    void *value;
    while (hashtable_iterator_next(it, NULL, &value)) {
      armlet_debugger_source *src = (armlet_debugger_source *)value;
      for (size_t i = 0; i < src->num_lines; i++)
        free(src->lines[i]);
      free(src->lines);
      free(src->raw);
      free(src->line_offsets);
      free(src);
    }
    hashtable_unref(dbg->source_cache);
  }

  free(dbg);
}

bool armlet_debugger_has_breakpoint(armlet_debugger *dbg, const char *file,
                                    size_t line) {
  for (size_t i = 0; i < dbg->num_breakpoints; i++) {
    armlet_breakpoint *bp = dbg->breakpoints[i];
    if (bp->enabled && bp->line == line) {
      if (bp->file == file || (bp->file && file && strcmp(bp->file, file) == 0))
        return true;
    }
  }
  return false;
}

int armlet_debugger_breakpoint_state(armlet_debugger *dbg, const char *file,
                                     size_t line) {
  for (size_t i = 0; i < dbg->num_breakpoints; i++) {
    armlet_breakpoint *bp = dbg->breakpoints[i];
    if (bp->line == line) {
      if (bp->file == file || (bp->file && file && strcmp(bp->file, file) == 0))
        return bp->enabled ? 1 : -1;
    }
  }
  return 0;
}

void armlet_debugger_toggle_breakpoint(armlet_debugger *dbg, const char *file,
                                       size_t line) {
  if (dbg->ast_root) {
    size_t snapped = find_statement_start(dbg->ast_root, file, line);
    if (snapped)
      line = snapped;
  }

  for (size_t i = 0; i < dbg->num_breakpoints; i++) {
    armlet_breakpoint *bp = dbg->breakpoints[i];
    if (bp->line == line &&
        (bp->file == file ||
         (bp->file && file && strcmp(bp->file, file) == 0))) {
      free(bp->file);
      free(bp);
      dbg->breakpoints[i] = dbg->breakpoints[dbg->num_breakpoints - 1];
      dbg->num_breakpoints--;
      return;
    }
  }

  armlet_breakpoint *bp = calloc(1, sizeof(armlet_breakpoint));
  bp->file = file ? strdup(file) : NULL;
  bp->line = line;
  bp->enabled = true;
  ARR_APPEND(dbg, breakpoints, bp);
}

char *armlet_debugger_resolve_var(armlet_debugger *dbg, const char *name) {
  if (!dbg->vm || dbg->vm->num_frames == 0)
    return strdup("<no frame>");

  armlet_string_list sl = str_split(".", name);
  armlet_ast_value value;
  if (sl.num_items == 1) {
    value = (armlet_ast_value){.tag = VAL_NAME, .name = sl.items[0]};
    free(sl.items);
  } else {
    value = (armlet_ast_value){
        .tag = VAL_DEREF,
        .deref = {.names = sl.items, .num_names = sl.num_items}};
  }

  armlet_vm_named_array *na =
      armlet_vm_resolve_var(dbg->vm, &value, SEM_GET, dbg->current_node, true);

  if (value.tag == VAL_NAME) {
    free(value.name);
  } else {
    for (size_t i = 0; i < value.deref.num_names; i++)
      free(value.deref.names[i]);
    free(value.deref.names);
  }

  if (!na || na->num_items == 0)
    return strdup("<undefined>");

  armlet_vm_var *v = na->items[0]->var;
  if (v && v->type)
    return armlet_vm_var_to_string(v, false);
  else
    return strdup("<unset>");
}

char *armlet_debugger_resolve_var_in_frame(armlet_debugger *dbg, const char *name,
                                           size_t frame_idx) {
  if (!dbg->vm || dbg->vm->num_frames == 0)
    return strdup("<no frame>");
  if (frame_idx >= dbg->vm->num_frames)
    return strdup("<invalid frame>");

  size_t saved = dbg->vm->num_frames;
  dbg->vm->num_frames = frame_idx + 1;
  char *result = armlet_debugger_resolve_var(dbg, name);
  dbg->vm->num_frames = saved;
  return result;
}

armlet_debugger_source *armlet_debugger_get_source(armlet_debugger *dbg,
                                                   const char *file) {
  if (!file)
    return NULL;

  armlet_debugger_source *cached = NULL;
  if (hashtable_find_str(dbg->source_cache, file, (void **)&cached) == 0)
    return cached;

  const char *data = NULL;
  if (dbg->current_node && dbg->current_node->source &&
      dbg->current_node->source->source.file &&
      strcmp(dbg->current_node->source->source.file, file) == 0) {
    data = dbg->current_node->source->source.data;
  }

  if (!data) {
    size_t len = 0;
    data = slurp_file(file, &len);
    if (!data)
      return NULL;
  }

  armlet_debugger_source *src = calloc(1, sizeof(armlet_debugger_source));
  src->raw_len = strlen(data);
  src->raw = CHECKED_MALLOC(src->raw_len + 1);
  memcpy(src->raw, data, src->raw_len + 1);

  size_t cap = 64;
  src->lines = CHECKED_MALLOC(cap * sizeof(char *));
  src->line_offsets = CHECKED_MALLOC(cap * sizeof(size_t));
  src->num_lines = 0;

  const char *p = src->raw;
  while (*p) {
    const char *eol = strchr(p, '\n');
    size_t len = eol ? (size_t)(eol - p) : strlen(p);

    if (src->num_lines == cap) {
      cap *= 2;
      src->lines = CHECKED_REALLOC(src->lines, cap, sizeof(char *));
      src->line_offsets = CHECKED_REALLOC(src->line_offsets, cap, sizeof(size_t));
    }

    src->line_offsets[src->num_lines] = (size_t)(p - src->raw);
    src->lines[src->num_lines] = CHECKED_MALLOC(len + 1);
    memcpy(src->lines[src->num_lines], p, len);
    src->lines[src->num_lines][len] = '\0';
    src->num_lines++;

    if (eol)
      p = eol + 1;
    else
      break;
  }

  hashtable_add_str(dbg->source_cache, file, src);
  return src;
}

static bool path_suffix_match(const char *full_path, const char *fragment) {
  size_t frag_len = strlen(fragment);
  size_t path_len = strlen(full_path);
  if (frag_len > path_len)
    return false;
  if (strcmp(full_path, fragment) == 0)
    return true;
  const char *suffix = full_path + path_len - frag_len;
  return strcmp(suffix, fragment) == 0 &&
         (suffix == full_path || suffix[-1] == '/');
}

const char *armlet_debugger_match_file(armlet_debugger *dbg,
                                       const char *fragment) {
  if (!fragment || !fragment[0])
    return NULL;

  if (dbg->current_file && path_suffix_match(dbg->current_file, fragment))
    return dbg->current_file;

  if (dbg->vm && dbg->vm->imported_files) {
    HASHTABLE_ITERATE_KEYS(dbg->vm->imported_files, char *, {
      if (path_suffix_match(key, fragment))
        return key;
    });
  }

  return NULL;
}

static const char *func_param_sig(armlet_vm_function *f) {
  if (!f || f->num_parameter_type_names == 0)
    return "";
  static char buf[256];
  buf[0] = '\0';
  size_t off = 0;
  for (size_t i = 0; i < f->num_parameter_type_names && off < sizeof(buf) - 2;
       i++) {
    if (i > 0)
      off += snprintf(buf + off, sizeof(buf) - off, ", ");
    off += snprintf(buf + off, sizeof(buf) - off, "%s",
                    f->parameter_type_names[i]);
  }
  return buf;
}

size_t armlet_debugger_find_functions(armlet_debugger *dbg, const char *name,
                                      armlet_debugger_func_loc *locs,
                                      size_t max_locs) {
  if (!dbg->vm || dbg->vm->num_frames == 0 || !name)
    return 0;

  size_t count = 0;

  for (size_t fi = 0; fi < dbg->vm->num_frames && count < max_locs; fi++) {
    armlet_vm_frame *frame = dbg->vm->frames[fi];
    if (!frame->symbols || !frame->symbols->symbols)
      continue;

    armlet_vm_named_array *na =
        armlet_vm_symbol_resolve(frame->symbols, name);
    if (!na)
      continue;

    for (size_t j = 0; j < na->num_items && count < max_locs; j++) {
      armlet_vm_var *v = na->items[j]->var;
      if (!v || !v->type)
        continue;
      if (v->type->tag != T_FUNCTION && v->type->tag != T_GETTER &&
          v->type->tag != T_SETTER)
        continue;

      armlet_vm_function *fn = v->function;
      if (!fn || !fn->def || !fn->def->body || fn->def->body->num_nodes == 0)
        continue;

      armlet_ast_node *first = fn->def->body->nodes[0];
      if (!first || !first->source)
        continue;

      armlet_line_info li = armlet_source_line_info_one(
          &first->source->source, first->source->span);
      locs[count].file = first->source->source.file;
      locs[count].line = li.lineno;
      locs[count].params = func_param_sig(fn);
      free(li.line);

      bool dup = false;
      for (size_t k = 0; k < count; k++) {
        if (locs[k].line == locs[count].line && locs[k].file &&
            locs[count].file &&
            strcmp(locs[k].file, locs[count].file) == 0) {
          dup = true;
          break;
        }
      }
      if (!dup)
        count++;
    }

    if (fi == 0 && dbg->vm->num_frames > 1)
      fi = dbg->vm->num_frames - 2; 
  }

  return count;
}

void armlet_debugger_resolve_pending(armlet_debugger *dbg) {
  if (dbg->num_pending_bps == 0)
    return;

  size_t i = 0;
  while (i < dbg->num_pending_bps) {
    armlet_pending_bp *pb = dbg->pending_bps[i];
    armlet_debugger_func_loc locs[16];
    size_t count =
        armlet_debugger_find_functions(dbg, pb->name, locs, 16);

    if (count > 0) {
      for (size_t j = 0; j < count; j++) {
        if (!armlet_debugger_has_breakpoint(dbg, locs[j].file, locs[j].line)) {
          armlet_debugger_toggle_breakpoint(dbg, locs[j].file, locs[j].line);
          if (dbg->tui) {
            const char *short_name = strrchr(locs[j].file, '/');
            short_name = short_name ? short_name + 1 : locs[j].file;
            armlet_debugger_tui_output(
                dbg->tui, "Pending breakpoint resolved: %s(%s) %s:%zu",
                pb->name, locs[j].params, short_name, locs[j].line);
          }
        }
      }
      free(pb->name);
      free(pb);
      dbg->pending_bps[i] = dbg->pending_bps[dbg->num_pending_bps - 1];
      dbg->num_pending_bps--;
    } else {
      i++;
    }
  }
}

bool armlet_debugger_post_run(armlet_debugger *dbg) {
  if (!dbg || !dbg->tui)
    return false;

  dbg->finished = true;
  dbg->mode = DBG_PAUSED;

  fflush(stdout);
  armlet_debugger_tui_drain_stdout(dbg->tui);

  armlet_debugger_tui_output(dbg->tui, "Program finished. Use 'r' to restart or 'q' to quit.");
  armlet_debugger_tui_center_on_line(dbg->tui, dbg->current_line);
  armlet_debugger_tui_draw(dbg->tui);
  armlet_debugger_tui_pause(dbg->tui);

  return dbg->restart_requested;
}
