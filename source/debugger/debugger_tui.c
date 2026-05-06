#define _GNU_SOURCE
#include "debugger_tui.h"
#include "debugger.h"
#include "debugger_commands.h"
#include "debugger_highlight.h"

#include <ctype.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CMD_HEIGHT 3
#define WATCH_MIN_WIDTH 20
#define STACK_MIN_HEIGHT 4
#define STACK_MAX_HEIGHT 12
#define GUTTER_WIDTH 9 // " *NNN > │ "

static bool g_tui_active = false;
static int g_saved_stdout_fd = -1;

static void atexit_endwin(void) {
  if (!g_tui_active)
    return;
  g_tui_active = false;
  endwin();
  if (g_saved_stdout_fd >= 0) {
    dup2(g_saved_stdout_fd, STDOUT_FILENO);
    g_saved_stdout_fd = -1;
  }
}

static void output_push(armlet_debugger_tui *tui, const char *line) {
  if (tui->output_count == DBG_OUTPUT_LINES) {
    free(tui->output_buf[0]);
    memmove(&tui->output_buf[0], &tui->output_buf[1],
            (DBG_OUTPUT_LINES - 1) * sizeof(char *));
    tui->output_count--;
  }
  tui->output_buf[tui->output_count++] = strdup(line);
  tui->output_scroll = 0;
}

void armlet_debugger_tui_output(armlet_debugger_tui *tui, const char *fmt,
                                ...) {
  char buf[1024];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  output_push(tui, buf);
}

void armlet_debugger_tui_drain_stdout(armlet_debugger_tui *tui) {
  char buf[4096];
  ssize_t n;
  static char partial[4096] = "";
  static int partial_len = 0;

  while ((n = read(tui->capture_pipe[0], buf, sizeof(buf) - 1)) > 0) {
    buf[n] = '\0';
    char *p = buf;
    while (*p) {
      char *nl = strchr(p, '\n');
      if (nl) {
        *nl = '\0';
        if (partial_len > 0) {
          int plen = (int)strlen(p);
          if (partial_len + plen < (int)sizeof(partial) - 1) {
            memcpy(partial + partial_len, p, plen);
            partial[partial_len + plen] = '\0';
          }
          output_push(tui, partial);
          partial_len = 0;
          partial[0] = '\0';
        } else {
          output_push(tui, p);
        }
        p = nl + 1;
      } else {
        int plen = (int)strlen(p);
        if (partial_len + plen < (int)sizeof(partial) - 1) {
          memcpy(partial + partial_len, p, plen);
          partial_len += plen;
          partial[partial_len] = '\0';
        }
        break;
      }
    }
  }
  if (partial_len > 0) {
    output_push(tui, partial);
    partial_len = 0;
    partial[0] = '\0';
  }
}

armlet_debugger_tui *armlet_debugger_tui_init(struct armlet_debugger *dbg) {
  armlet_debugger_tui *tui = calloc(1, sizeof(armlet_debugger_tui));
  tui->dbg = dbg;
  tui->cmd_height = CMD_HEIGHT;
  tui->output_height = OUTPUT_HEIGHT;

  tui->saved_stdout_fd = dup(STDOUT_FILENO);
  pipe(tui->capture_pipe);
  fcntl(tui->capture_pipe[0], F_SETFL, O_NONBLOCK);
  dup2(tui->capture_pipe[1], STDOUT_FILENO);
  close(tui->capture_pipe[1]);
  tui->capture_pipe[1] = -1;

  setvbuf(stdout, NULL, _IOLBF, 0);

  tui->term_out = fdopen(tui->saved_stdout_fd, "w");

  tui->screen = newterm(NULL, tui->term_out, stdin);
  set_term(tui->screen);

  g_saved_stdout_fd = tui->saved_stdout_fd;
  g_tui_active = true;
  atexit(atexit_endwin);

  cbreak();
  noecho();
  keypad(stdscr, TRUE);
  curs_set(0);
  mousemask(BUTTON1_CLICKED | BUTTON1_PRESSED, NULL);

  tui->theme = &armlet_theme_norton_commander;

  if (has_colors()) {
    start_color();
    use_default_colors();
    armlet_theme_init(tui->theme);
  }

  getmaxyx(stdscr, tui->term_rows, tui->term_cols);

  tui->watch_width = tui->term_cols / 4;
  if (tui->watch_width < WATCH_MIN_WIDTH)
    tui->watch_width = WATCH_MIN_WIDTH;
  tui->source_width = tui->term_cols - tui->watch_width;

  int main_height = tui->term_rows - tui->cmd_height - tui->output_height;

  tui->stack_height = main_height / 3;
  if (tui->stack_height < STACK_MIN_HEIGHT)
    tui->stack_height = STACK_MIN_HEIGHT;
  if (tui->stack_height > STACK_MAX_HEIGHT)
    tui->stack_height = STACK_MAX_HEIGHT;
  if (tui->stack_height > main_height - STACK_MIN_HEIGHT)
    tui->stack_height = main_height - STACK_MIN_HEIGHT;
  if (main_height < 2 * STACK_MIN_HEIGHT)
    tui->stack_height = main_height / 2;
  int watch_height = main_height - tui->stack_height;

  tui->source_win = newwin(main_height, tui->source_width, 0, 0);
  tui->stack_win =
      newwin(tui->stack_height, tui->watch_width, 0, tui->source_width);
  tui->watch_win = newwin(watch_height, tui->watch_width, tui->stack_height,
                          tui->source_width);
  tui->output_win = newwin(tui->output_height, tui->term_cols, main_height, 0);
  tui->cmd_win = newwin(tui->cmd_height, tui->term_cols,
                        main_height + tui->output_height, 0);

  chtype bg = COLOR_PAIR(CLR_SOURCE_TEXT);
  wbkgd(tui->source_win, bg);
  wbkgd(tui->stack_win, bg);
  wbkgd(tui->watch_win, bg);
  wbkgd(tui->output_win, bg);
  wbkgd(tui->cmd_win, bg);

  scrollok(tui->cmd_win, TRUE);
  keypad(tui->cmd_win, TRUE);

  return tui;
}

void armlet_debugger_tui_cleanup(armlet_debugger_tui *tui) {
  if (!tui)
    return;

  g_tui_active = false;

  dup2(tui->saved_stdout_fd, STDOUT_FILENO);
  close(tui->capture_pipe[0]);

  for (int i = 0; i < tui->output_count; i++)
    free(tui->output_buf[i]);

  for (int i = 0; i < tui->history_count; i++)
    free(tui->history[i]);

  if (tui->source_win)
    delwin(tui->source_win);
  if (tui->stack_win)
    delwin(tui->stack_win);
  if (tui->watch_win)
    delwin(tui->watch_win);
  if (tui->output_win)
    delwin(tui->output_win);
  if (tui->cmd_win)
    delwin(tui->cmd_win);

  endwin();
  if (tui->screen)
    delscreen(tui->screen);
  if (tui->term_out)
    fclose(tui->term_out);

  free(tui);
}

void armlet_debugger_tui_center_on_line(armlet_debugger_tui *tui, size_t line) {
  int main_height = tui->term_rows - tui->cmd_height - tui->output_height;
  int target = (int)line - main_height / 2;
  tui->source_scroll = target < 0 ? 0 : target;
}

static void draw_source(armlet_debugger_tui *tui) {
  WINDOW *w = tui->source_win;
  werase(w);

  struct armlet_debugger *dbg = tui->dbg;
  int max_y, max_x;
  getmaxyx(w, max_y, max_x);

  const char *display_file =
      dbg->view_file ? dbg->view_file : dbg->current_file;

  const armlet_debugger_theme *th = tui->theme;
  attr_t a_title = armlet_theme_attr(th, CLR_TITLE);
  wattron(w, a_title);
  char title[256];
  if (dbg->view_file &&
      (!dbg->current_file || strcmp(dbg->view_file, dbg->current_file) != 0))
    snprintf(title, sizeof(title), " Source: %s [viewing] ",
             display_file ? display_file : "<unknown>");
  else
    snprintf(title, sizeof(title), " Source: %s ",
             display_file ? display_file : "<unknown>");
  mvwprintw(w, 0, 0, "%s", title);
  for (int i = (int)strlen(title); i < max_x; i++)
    waddch(w, ' ');
  wattroff(w, a_title);

  armlet_debugger_source *src =
      display_file ? armlet_debugger_get_source(dbg, display_file) : NULL;

#ifdef HAVE_TREE_SITTER
  const armlet_highlight_result *hl_result = NULL;
  if (dbg->highlighter && src && src->raw)
    hl_result = armlet_highlighter_get(dbg->highlighter, display_file, src->raw,
                                       src->raw_len);
#endif

  for (int row = 1; row < max_y; row++) {
    int line_idx = tui->source_scroll + row - 1;
    size_t lineno = (size_t)(line_idx + 1);

    if (!src || line_idx < 0 || (size_t)line_idx >= src->num_lines) {
      mvwprintw(w, row, 0, "~");
      continue;
    }

    bool is_current = (!dbg->view_file && dbg->current_line > 0 &&
                       lineno == dbg->current_line);
    int bp_state = armlet_debugger_breakpoint_state(dbg, display_file, lineno);

    if (bp_state == 1) {
      attr_t a_bp = armlet_theme_attr(th, CLR_BREAKPOINT);
      wattron(w, a_bp);
      mvwprintw(w, row, 0, " *");
      wattroff(w, a_bp);
    } else if (bp_state == -1) {
      attr_t a_gutter_dim = armlet_theme_attr(th, CLR_GUTTER);
      wattron(w, a_gutter_dim);
      mvwprintw(w, row, 0, " -");
      wattroff(w, a_gutter_dim);
    } else {
      mvwprintw(w, row, 0, "  ");
    }

    attr_t a_gutter = armlet_theme_attr(th, CLR_GUTTER);
    wattron(w, a_gutter);
    wprintw(w, "%3zu ", lineno);
    wattroff(w, a_gutter);

    if (is_current) {
      attr_t a_arrow = armlet_theme_attr(th, CLR_CURRENT_ARROW);
      wattron(w, a_arrow);
      waddch(w, '>');
      wattroff(w, a_arrow);
    } else {
      waddch(w, ' ');
    }

    waddch(w, ACS_VLINE);
    waddch(w, ' ');

    int code_start = GUTTER_WIDTH;
    int avail = max_x - code_start;
    if (avail < 0)
      avail = 0;

    attr_t a_cur = armlet_theme_attr(th, CLR_CURRENT_LINE);
    if (is_current)
      wattron(w, a_cur);

    char *line_text = src->lines[line_idx];
#ifdef HAVE_TREE_SITTER
    uint32_t line_byte_off = (uint32_t)src->line_offsets[line_idx];
#endif
    int col = 0;
#ifdef HAVE_TREE_SITTER
    int prev_syn_slot = 0;
#endif
    for (int ci = 0; line_text[ci] && col < avail; ci++) {
#ifdef HAVE_TREE_SITTER
      if (!is_current && hl_result) {
        int syn_slot =
            armlet_highlight_lookup(hl_result, line_byte_off + (uint32_t)ci);
        if (syn_slot != prev_syn_slot) {
          if (prev_syn_slot)
            wattroff(w, armlet_theme_attr(th, prev_syn_slot));
          if (syn_slot)
            wattron(w, armlet_theme_attr(th, syn_slot));
          prev_syn_slot = syn_slot;
        }
      }
#endif
      if (line_text[ci] == '\t') {
        int spaces = 4 - (col % 4);
        for (int s = 0; s < spaces && col < avail; s++, col++)
          waddch(w, ' ');
      } else {
        waddch(w, line_text[ci]);
        col++;
      }
    }
#ifdef HAVE_TREE_SITTER
    if (prev_syn_slot)
      wattroff(w, armlet_theme_attr(th, prev_syn_slot));
#endif
    if (is_current) {
      for (; col < avail; col++)
        waddch(w, ' ');
    }

    if (is_current)
      wattroff(w, a_cur);
  }

  for (int row = 0; row < max_y; row++)
    mvwaddch(w, row, max_x - 1, ACS_VLINE);

  wrefresh(w);
}

static void draw_stack(armlet_debugger_tui *tui) {
  WINDOW *w = tui->stack_win;
  werase(w);

  struct armlet_debugger *dbg = tui->dbg;
  int max_y, max_x;
  getmaxyx(w, max_y, max_x);

  const armlet_debugger_theme *th = tui->theme;
  attr_t a_title = armlet_theme_attr(th, CLR_TITLE);
  wattron(w, a_title);
  mvwprintw(w, 0, 0, " Stack ");
  for (int i = 7; i < max_x; i++)
    waddch(w, ' ');
  wattroff(w, a_title);

  if (!dbg->vm || dbg->vm->num_frames == 0) {
    mvwprintw(w, 1, 1, "(no frames)");
    wrefresh(w);
    return;
  }

  int row = 1;
  for (size_t fi = dbg->vm->num_frames; fi > 0 && row < max_y; fi--, row++) {
    armlet_vm_frame *f = dbg->vm->frames[fi - 1];
    size_t frame_num = dbg->vm->num_frames - fi;
    bool is_selected = (fi - 1 == dbg->selected_frame);

    int slot = is_selected ? CLR_STACK_CURRENT : CLR_STACK_FRAME;
    attr_t a = armlet_theme_attr(th, slot);
    wattron(w, a);

    const char *name = f->context ? f->context : "<anon>";
    mvwprintw(w, row, 1, "%s#%zu %.*s", is_selected ? "> " : "  ", frame_num,
              max_x - 7, name);

    wattroff(w, a);
  }

  wrefresh(w);
}

static void draw_watches(armlet_debugger_tui *tui) {
  WINDOW *w = tui->watch_win;
  werase(w);

  struct armlet_debugger *dbg = tui->dbg;
  int max_y, max_x;
  getmaxyx(w, max_y, max_x);

  const armlet_debugger_theme *th = tui->theme;
  attr_t a_title = armlet_theme_attr(th, CLR_TITLE);
  wattron(w, a_title);
  mvwprintw(w, 0, 0, " Watches ");
  for (int i = 9; i < max_x; i++)
    waddch(w, ' ');
  wattroff(w, a_title);

  int row = 1;
  for (size_t i = 0; i < dbg->num_watches && row < max_y; i++, row++) {
    char *val = armlet_debugger_resolve_var_in_frame(dbg, dbg->watches[i]->name,
                                                     dbg->selected_frame);

    attr_t a_name = armlet_theme_attr(th, CLR_WATCH_NAME);
    wattron(w, a_name);
    mvwprintw(w, row, 1, "%s", dbg->watches[i]->name);
    wattroff(w, a_name);
    wprintw(w, " = ");
    attr_t a_val = armlet_theme_attr(th, CLR_WATCH_VALUE);
    wattron(w, a_val);
    wprintw(w, "%.*s", max_x - (int)strlen(dbg->watches[i]->name) - 5, val);
    wattroff(w, a_val);

    free(val);
  }

  if (dbg->num_watches == 0) {
    mvwprintw(w, 1, 1, "(no watches)");
    mvwprintw(w, 2, 1, "Use 'w <var>' to add");
  }

  wrefresh(w);
}

static void draw_output(armlet_debugger_tui *tui) {
  WINDOW *w = tui->output_win;
  werase(w);

  int max_y, max_x;
  getmaxyx(w, max_y, max_x);

  const armlet_debugger_theme *th = tui->theme;
  attr_t a_sep = armlet_theme_attr(th, CLR_SEPARATOR);
  wattron(w, a_sep);
  for (int i = 0; i < max_x; i++)
    mvwaddch(w, 0, i, ACS_HLINE);
  wattroff(w, a_sep);
  attr_t a_title = armlet_theme_attr(th, CLR_TITLE);
  wattron(w, a_title);
  if (tui->output_scroll > 0)
    mvwprintw(w, 0, 1, " Output [+%d] ", tui->output_scroll);
  else
    mvwprintw(w, 0, 1, " Output ");
  wattroff(w, a_title);

  int visible = max_y - 1;
  int end = tui->output_count - tui->output_scroll;
  if (end < 0)
    end = 0;
  int start = end - visible;
  if (start < 0)
    start = 0;

  int row = 1;
  for (int i = start; i < end && row < max_y; i++, row++) {
    mvwprintw(w, row, 1, "%.*s", max_x - 2, tui->output_buf[i]);
  }

  wrefresh(w);
}

static void draw_cmd(armlet_debugger_tui *tui) {
  WINDOW *w = tui->cmd_win;
  werase(w);

  int max_y, max_x;
  getmaxyx(w, max_y, max_x);
  (void)max_y;

  const armlet_debugger_theme *th = tui->theme;
  attr_t a_sep = armlet_theme_attr(th, CLR_SEPARATOR);
  wattron(w, a_sep);
  for (int i = 0; i < max_x; i++)
    mvwaddch(w, 0, i, ACS_HLINE);
  wattroff(w, a_sep);

  struct armlet_debugger *dbg = tui->dbg;
  attr_t a_status = armlet_theme_attr(th, CLR_STATUS);
  wattron(w, a_status);
  mvwprintw(w, 1, 1, "[%s:%zu] %zu breakpoints",
            dbg->current_file ? dbg->current_file : "?", dbg->current_line,
            dbg->num_breakpoints);
  wattroff(w, a_status);

  attr_t a_prompt = armlet_theme_attr(th, CLR_PROMPT);
  wattron(w, a_prompt);
  mvwprintw(w, 2, 0, "(dbg) ");
  wattroff(w, a_prompt);

  wrefresh(w);
}

void armlet_debugger_tui_draw(armlet_debugger_tui *tui) {
  draw_source(tui);
  draw_stack(tui);
  draw_watches(tui);
  draw_output(tui);
  draw_cmd(tui);
  doupdate();
}

static void handle_mouse_click(armlet_debugger_tui *tui, int y, int x) {
  int main_height = tui->term_rows - tui->cmd_height - tui->output_height;
  if (y >= 1 && y < main_height && x < GUTTER_WIDTH) {
    struct armlet_debugger *dbg = tui->dbg;
    int line_idx = tui->source_scroll + y - 1;
    size_t lineno = (size_t)(line_idx + 1);

    const char *display_file =
        dbg->view_file ? dbg->view_file : dbg->current_file;
    armlet_debugger_source *src =
        display_file ? armlet_debugger_get_source(dbg, display_file) : NULL;
    if (src && (size_t)line_idx < src->num_lines) {
      armlet_debugger_toggle_breakpoint(dbg, display_file, lineno);
      armlet_debugger_tui_draw(tui);
    }
  }
}

static void handle_command(armlet_debugger_tui *tui, armlet_dbg_command *cmd) {
  struct armlet_debugger *dbg = tui->dbg;

  switch (cmd->type) {
  case CMD_STEP:
    if (dbg->finished) {
      armlet_debugger_tui_output(tui, "Program finished. Use 'r' to restart.");
      draw_output(tui);
      return;
    }
    dbg->mode = DBG_STEP;
    break;

  case CMD_NEXT:
    if (dbg->finished) {
      armlet_debugger_tui_output(tui, "Program finished. Use 'r' to restart.");
      draw_output(tui);
      return;
    }
    dbg->mode = DBG_NEXT;
    dbg->step_frame_depth = dbg->vm->num_frames;
    dbg->step_file = dbg->current_file;
    dbg->step_line = dbg->current_line;
    break;

  case CMD_FINISH:
    if (dbg->finished) {
      armlet_debugger_tui_output(tui, "Program finished. Use 'r' to restart.");
      draw_output(tui);
      return;
    }
    if (dbg->vm->num_frames <= 1) {
      armlet_debugger_tui_output(tui, "Already at top-level frame.");
      draw_output(tui);
      return;
    }
    dbg->mode = DBG_FINISH;
    dbg->step_frame_depth = dbg->vm->num_frames;
    break;

  case CMD_CONTINUE:
    if (dbg->finished) {
      armlet_debugger_tui_output(tui, "Program finished. Use 'r' to restart.");
      draw_output(tui);
      return;
    }
    dbg->skip_bp_file = dbg->current_file;
    dbg->skip_bp_line = dbg->current_line;
    dbg->skip_bp_frame = dbg->vm->num_frames;
    dbg->mode = DBG_RUN;
    break;

  case CMD_RESTART:
    dbg->restart_requested = true;
    dbg->mode = DBG_RUN;
    break;

  case CMD_BREAK:
    if (cmd->arg) {
      const char *bp_file = dbg->current_file;
      size_t line = 0;
      char *resolved = NULL;

      bool is_numeric = true;
      for (const char *p = cmd->arg; *p; p++) {
        if (*p < '0' || *p > '9') {
          is_numeric = false;
          break;
        }
      }

      char *colon = strrchr(cmd->arg, ':');
      bool is_file_line =
          colon && colon != cmd->arg && colon[1] >= '0' && colon[1] <= '9';

      if (is_numeric) {
        line = (size_t)atol(cmd->arg);
      } else if (is_file_line) {
        *colon = '\0';
        line = (size_t)atol(colon + 1);
        bp_file = armlet_debugger_match_file(dbg, cmd->arg);
        if (!bp_file) {
          resolved = realpath(cmd->arg, NULL);
          if (!resolved) {
            char *with_ext = NULL;
            asprintf(&with_ext, "%s.aml", cmd->arg);
            resolved = realpath(with_ext, NULL);
            free(with_ext);
          }
          if (resolved) {
            bp_file = resolved;
          } else {
            armlet_debugger_tui_output(tui, "File not found: %s", cmd->arg);
            *colon = ':';
            draw_output(tui);
            return;
          }
        }
        *colon = ':';
      } else {
        armlet_debugger_func_loc locs[16];
        size_t count = armlet_debugger_find_functions(dbg, cmd->arg, locs, 16);
        if (count == 0) {
          armlet_pending_bp *pb = calloc(1, sizeof(armlet_pending_bp));
          pb->name = strdup(cmd->arg);
          ARR_APPEND(dbg, pending_bps, pb);
          armlet_debugger_tui_output(
              tui, "Pending breakpoint on '%s' (will resolve when defined)",
              cmd->arg);
          draw_output(tui);
          return;
        }
        for (size_t i = 0; i < count; i++) {
          armlet_debugger_toggle_breakpoint(dbg, locs[i].file, locs[i].line);
          const char *short_name = strrchr(locs[i].file, '/');
          short_name = short_name ? short_name + 1 : locs[i].file;
          if (armlet_debugger_has_breakpoint(dbg, locs[i].file, locs[i].line))
            armlet_debugger_tui_output(tui, "Breakpoint set at %s(%s) %s:%zu",
                                       cmd->arg, locs[i].params, short_name,
                                       locs[i].line);
          else
            armlet_debugger_tui_output(
                tui, "Breakpoint removed at %s(%s) %s:%zu", cmd->arg,
                locs[i].params, short_name, locs[i].line);
        }
        armlet_debugger_tui_draw(tui);
        return;
      }

      if (line > 0) {
        armlet_debugger_toggle_breakpoint(dbg, bp_file, line);
        const char *short_name = strrchr(bp_file, '/');
        short_name = short_name ? short_name + 1 : bp_file;
        if (armlet_debugger_has_breakpoint(dbg, bp_file, line))
          armlet_debugger_tui_output(tui, "Breakpoint set at %s:%zu",
                                     short_name, line);
        else
          armlet_debugger_tui_output(tui, "Breakpoint removed at %s:%zu",
                                     short_name, line);
        armlet_debugger_tui_draw(tui);
      } else {
        armlet_debugger_tui_output(tui,
                                   "Usage: break [file:]<line> | break <func>");
        draw_output(tui);
      }
      free(resolved);
    } else {
      armlet_debugger_tui_output(tui,
                                 "Usage: break [file:]<line> | break <func>");
      draw_output(tui);
    }
    return;

  case CMD_WATCH:
    if (cmd->arg) {
      armlet_watch_entry *we = calloc(1, sizeof(armlet_watch_entry));
      we->name = strdup(cmd->arg);
      ARR_APPEND(dbg, watches, we);
      armlet_debugger_tui_draw(tui);
    } else {
      armlet_debugger_tui_output(tui, "Usage: watch <variable>");
      draw_output(tui);
    }
    return;

  case CMD_UNWATCH:
    if (cmd->arg) {
      for (size_t i = 0; i < dbg->num_watches; i++) {
        if (strcmp(dbg->watches[i]->name, cmd->arg) == 0) {
          free(dbg->watches[i]->name);
          free(dbg->watches[i]);
          dbg->watches[i] = dbg->watches[dbg->num_watches - 1];
          dbg->num_watches--;
          armlet_debugger_tui_draw(tui);
          return;
        }
      }
      armlet_debugger_tui_output(tui, "Watch '%s' not found", cmd->arg);
      draw_output(tui);
    } else {
      armlet_debugger_tui_output(tui, "Usage: unwatch <variable>");
      draw_output(tui);
    }
    return;

  case CMD_PRINT:
    if (cmd->arg) {
      char *val = armlet_debugger_resolve_var_in_frame(dbg, cmd->arg,
                                                       dbg->selected_frame);
      armlet_debugger_tui_output(tui, "%s = %s", cmd->arg, val);
      free(val);
      draw_output(tui);
    } else {
      armlet_debugger_tui_output(tui, "Usage: print <variable>");
      draw_output(tui);
    }
    return;

  case CMD_BACKTRACE: {
    for (size_t fi = dbg->vm->num_frames; fi > 0; fi--) {
      armlet_vm_frame *f = dbg->vm->frames[fi - 1];
      armlet_debugger_tui_output(tui, "#%zu %s", dbg->vm->num_frames - fi,
                                 f->context ? f->context : "<anon>");
    }
    draw_output(tui);
    return;
  }

  case CMD_LIST:
    if (cmd->arg) {
      char *colon = strrchr(cmd->arg, ':');
      bool is_file_line =
          colon && colon != cmd->arg && colon[1] >= '0' && colon[1] <= '9';
      bool is_numeric = true;
      for (const char *p = cmd->arg; *p; p++) {
        if (*p < '0' || *p > '9') {
          is_numeric = false;
          break;
        }
      }

      if (is_numeric) {
        size_t line = (size_t)atol(cmd->arg);
        armlet_debugger_tui_center_on_line(tui, line);
      } else if (is_file_line) {
        *colon = '\0';
        size_t line = (size_t)atol(colon + 1);
        const char *file = armlet_debugger_match_file(dbg, cmd->arg);
        *colon = ':';
        if (!file) {
          armlet_debugger_tui_output(tui, "File not found: %s", cmd->arg);
          draw_output(tui);
          return;
        }
        dbg->view_file = file;
        dbg->view_line = line;
        armlet_debugger_tui_center_on_line(tui, line);
      } else {
        const char *file = armlet_debugger_match_file(dbg, cmd->arg);
        if (!file) {
          armlet_debugger_tui_output(tui, "File not found: %s", cmd->arg);
          draw_output(tui);
          return;
        }
        dbg->view_file = file;
        dbg->view_line = 1;
        armlet_debugger_tui_center_on_line(tui, 1);
      }
    } else {
      dbg->view_file = NULL;
      armlet_debugger_tui_center_on_line(tui, dbg->current_line);
    }
    armlet_debugger_tui_draw(tui);
    return;

  case CMD_QUIT:
    armlet_debugger_tui_cleanup(tui);
    dbg->tui = NULL;
    exit(0);

  case CMD_UP:
    if (!dbg->vm || dbg->vm->num_frames == 0) {
      armlet_debugger_tui_output(tui, "No frames.");
      draw_output(tui);
      return;
    }
    if (dbg->selected_frame == 0) {
      armlet_debugger_tui_output(tui, "Already at bottom frame.");
      draw_output(tui);
      return;
    }
    dbg->selected_frame--;
    {
      armlet_vm_frame *f = dbg->vm->frames[dbg->selected_frame];
      armlet_debugger_tui_output(tui, "Frame #%zu: %s",
                                 dbg->vm->num_frames - 1 - dbg->selected_frame,
                                 f->context ? f->context : "<anon>");
    }
    armlet_debugger_tui_draw(tui);
    return;

  case CMD_DOWN:
    if (!dbg->vm || dbg->vm->num_frames == 0) {
      armlet_debugger_tui_output(tui, "No frames.");
      draw_output(tui);
      return;
    }
    if (dbg->selected_frame >= dbg->vm->num_frames - 1) {
      armlet_debugger_tui_output(tui, "Already at top frame.");
      draw_output(tui);
      return;
    }
    dbg->selected_frame++;
    {
      armlet_vm_frame *f = dbg->vm->frames[dbg->selected_frame];
      armlet_debugger_tui_output(tui, "Frame #%zu: %s",
                                 dbg->vm->num_frames - 1 - dbg->selected_frame,
                                 f->context ? f->context : "<anon>");
    }
    armlet_debugger_tui_draw(tui);
    return;

  case CMD_ENABLE:
    if (cmd->arg) {
      size_t idx = (size_t)atol(cmd->arg);
      if (idx < 1 || idx > dbg->num_breakpoints) {
        armlet_debugger_tui_output(tui, "Invalid breakpoint number: %s",
                                   cmd->arg);
      } else {
        armlet_breakpoint *bp = dbg->breakpoints[idx - 1];
        bp->enabled = true;
        const char *short_name = strrchr(bp->file, '/');
        short_name = short_name ? short_name + 1 : bp->file;
        armlet_debugger_tui_output(tui, "Enabled breakpoint %zu: %s:%zu", idx,
                                   short_name, bp->line);
      }
      armlet_debugger_tui_draw(tui);
    } else {
      armlet_debugger_tui_output(tui, "Usage: enable <N>");
      draw_output(tui);
    }
    return;

  case CMD_DISABLE:
    if (cmd->arg) {
      size_t idx = (size_t)atol(cmd->arg);
      if (idx < 1 || idx > dbg->num_breakpoints) {
        armlet_debugger_tui_output(tui, "Invalid breakpoint number: %s",
                                   cmd->arg);
      } else {
        armlet_breakpoint *bp = dbg->breakpoints[idx - 1];
        bp->enabled = false;
        const char *short_name = strrchr(bp->file, '/');
        short_name = short_name ? short_name + 1 : bp->file;
        armlet_debugger_tui_output(tui, "Disabled breakpoint %zu: %s:%zu", idx,
                                   short_name, bp->line);
      }
      armlet_debugger_tui_draw(tui);
    } else {
      armlet_debugger_tui_output(tui, "Usage: disable <N>");
      draw_output(tui);
    }
    return;

  case CMD_SEARCH: {
    const char *pattern = cmd->arg;
    if (!pattern) {
      pattern = dbg->last_search;
      if (!pattern) {
        armlet_debugger_tui_output(tui, "No previous search pattern.");
        draw_output(tui);
        return;
      }
    } else {
      free(dbg->last_search);
      dbg->last_search = strdup(pattern);
    }

    const char *search_file =
        dbg->view_file ? dbg->view_file : dbg->current_file;
    armlet_debugger_source *src = armlet_debugger_get_source(dbg, search_file);
    if (!src) {
      armlet_debugger_tui_output(tui, "No source loaded.");
      draw_output(tui);
      return;
    }

    size_t start_line = (size_t)tui->source_scroll + 1;
    for (size_t i = 0; i < src->num_lines; i++) {
      size_t idx = (start_line + i) % src->num_lines;
      if (strcasestr(src->lines[idx], pattern)) {
        dbg->view_file = search_file;
        armlet_debugger_tui_center_on_line(tui, idx + 1);
        armlet_debugger_tui_output(tui, "Found '%s' at line %zu", pattern,
                                   idx + 1);
        armlet_debugger_tui_draw(tui);
        return;
      }
    }
    armlet_debugger_tui_output(tui, "Pattern not found: %s", pattern);
    draw_output(tui);
    return;
  }

  case CMD_INFO: {
    bool show_bp = !cmd->arg || strcmp(cmd->arg, "breaks") == 0 ||
                   strcmp(cmd->arg, "b") == 0;
    bool show_watches = !cmd->arg || strcmp(cmd->arg, "watches") == 0 ||
                        strcmp(cmd->arg, "w") == 0;
    bool show_funcs = !cmd->arg || strcmp(cmd->arg, "functions") == 0 ||
                      strcmp(cmd->arg, "f") == 0;

    if (cmd->arg && !show_bp && !show_watches && !show_funcs) {
      armlet_debugger_tui_output(tui, "Usage: info [breaks|watches|functions]");
      draw_output(tui);
      return;
    }

    if (show_bp) {
      armlet_debugger_tui_output(tui, "Breakpoints:");
      if (dbg->num_breakpoints == 0) {
        armlet_debugger_tui_output(tui, "  (none)");
      } else {
        for (size_t i = 0; i < dbg->num_breakpoints; i++) {
          armlet_breakpoint *bp = dbg->breakpoints[i];
          const char *short_name = strrchr(bp->file, '/');
          short_name = short_name ? short_name + 1 : bp->file;
          armlet_debugger_tui_output(tui, "  %zu: %s:%zu%s", i + 1, short_name,
                                     bp->line,
                                     bp->enabled ? "" : " [disabled]");
        }
      }
      if (dbg->num_pending_bps > 0) {
        armlet_debugger_tui_output(tui, "Pending:");
        for (size_t i = 0; i < dbg->num_pending_bps; i++)
          armlet_debugger_tui_output(tui, "  %s (unresolved)",
                                     dbg->pending_bps[i]->name);
      }
    }

    if (show_watches) {
      armlet_debugger_tui_output(tui, "Watches:");
      if (dbg->num_watches == 0) {
        armlet_debugger_tui_output(tui, "  (none)");
      } else {
        for (size_t i = 0; i < dbg->num_watches; i++)
          armlet_debugger_tui_output(tui, "  %zu: %s", i + 1,
                                     dbg->watches[i]->name);
      }
    }

    if (show_funcs) {
      armlet_debugger_collect_completions(dbg);
      armlet_debugger_tui_output(tui, "Functions:");
      if (dbg->num_completion_names == 0) {
        armlet_debugger_tui_output(tui, "  (none found)");
      } else {
        for (size_t i = 0; i < dbg->num_completion_names; i++)
          armlet_debugger_tui_output(tui, "  %s", dbg->completion_names[i]);
      }
    }

    draw_output(tui);
    return;
  }

  case CMD_HELP:
    armlet_debugger_tui_output(tui, "Commands:");
    armlet_debugger_tui_output(tui,
                               "  s, step              Step to next statement");
    armlet_debugger_tui_output(
        tui, "  n, next              Step over function calls");
    armlet_debugger_tui_output(
        tui, "  f, finish            Run until current function returns");
    armlet_debugger_tui_output(
        tui, "  c, continue          Resume until next breakpoint");
    armlet_debugger_tui_output(
        tui, "  b, break <line>      Toggle breakpoint at line");
    armlet_debugger_tui_output(
        tui, "  b, break <f:line>    Toggle breakpoint at file:line");
    armlet_debugger_tui_output(
        tui, "  b, break <func>      Toggle breakpoint on function");
    armlet_debugger_tui_output(
        tui, "  enable/disable <N>   Enable/disable breakpoint by number");
    armlet_debugger_tui_output(tui,
                               "  p, print <var>       Print variable value");
    armlet_debugger_tui_output(
        tui, "  w, watch <var>       Add variable to watch panel");
    armlet_debugger_tui_output(
        tui, "  unwatch <var>        Remove from watch panel");
    armlet_debugger_tui_output(tui, "  bt, backtrace        Show call stack");
    armlet_debugger_tui_output(tui,
                               "  up / down            Navigate stack frames");
    armlet_debugger_tui_output(
        tui, "  i, info [b|w|f]      List breaks, watches, or functions");
    armlet_debugger_tui_output(
        tui, "  l, list [f:line]     View source at file:line");
    armlet_debugger_tui_output(tui,
                               "  /<pattern>           Search source forward");
    armlet_debugger_tui_output(tui, "  r, restart           Restart program");
    armlet_debugger_tui_output(tui, "  q, quit              Exit debugger");
    armlet_debugger_tui_output(tui,
                               "  Ctrl+U / Ctrl+D      Scroll output up/down");
    armlet_debugger_tui_output(tui, "  Ctrl+X               Clear input line");
    draw_output(tui);
    return;

  case CMD_UNKNOWN:
    armlet_debugger_tui_output(tui, "Unknown command: %s  (type 'h' for help)",
                               cmd->arg ? cmd->arg : "");
    draw_output(tui);
    return;

  case CMD_NONE:
    return;
  }
}

static void history_push(armlet_debugger_tui *tui, const char *line) {
  if (!line || !line[0])
    return;
  if (tui->history_count > 0 &&
      strcmp(tui->history[tui->history_count - 1], line) == 0)
    return;

  if (tui->history_count == DBG_HISTORY_MAX) {
    free(tui->history[0]);
    memmove(&tui->history[0], &tui->history[1],
            (DBG_HISTORY_MAX - 1) * sizeof(char *));
    tui->history_count--;
  }
  tui->history[tui->history_count++] = strdup(line);
}

#define PROMPT_COL 6

static void redraw_input_line(WINDOW *w, const char *buf, int len, int cursor) {
  wmove(w, 2, PROMPT_COL);
  wclrtoeol(w);
  for (int i = 0; i < len; i++)
    waddch(w, (unsigned char)buf[i]);
  wmove(w, 2, PROMPT_COL + cursor);
  wrefresh(w);
}

static char *read_input(armlet_debugger_tui *tui) {
  WINDOW *w = tui->cmd_win;
  char buf[256];
  int len = 0;
  int cursor = 0;

  tui->history_cursor = -1;

  wmove(w, 2, PROMPT_COL);
  curs_set(1);
  wrefresh(w);

  char saved_line[256] = "";

  while (1) {
    int ch = wgetch(w);

    if (ch == KEY_MOUSE) {
      MEVENT event;
      if (getmouse(&event) == OK)
        handle_mouse_click(tui, event.y, event.x);
      draw_cmd(tui);
      redraw_input_line(w, buf, len, cursor);
      continue;
    }

    if (ch == KEY_RESIZE) {
      getmaxyx(stdscr, tui->term_rows, tui->term_cols);
      armlet_debugger_tui_draw(tui);
      redraw_input_line(w, buf, len, cursor);
      continue;
    }

    if (ch == '\n' || ch == KEY_ENTER) {
      buf[len] = '\0';
      curs_set(0);
      return strdup(buf);
    }

    // Ctrl-C
    if (ch == 3) {
      curs_set(0);
      return strdup("q");
    }

    // Ctrl-A / Home
    if (ch == 1 || ch == KEY_HOME) {
      cursor = 0;
      wmove(w, 2, PROMPT_COL);
      wrefresh(w);
      continue;
    }

    // Ctrl-E / End
    if (ch == 5 || ch == KEY_END) {
      cursor = len;
      wmove(w, 2, PROMPT_COL + cursor);
      wrefresh(w);
      continue;
    }

    // Ctrl-U (scroll output up)
    if (ch == 21) {
      int half = tui->output_height / 2;
      if (half < 1)
        half = 1;
      int max_scroll = tui->output_count - 1;
      if (max_scroll < 0)
        max_scroll = 0;
      tui->output_scroll += half;
      if (tui->output_scroll > max_scroll)
        tui->output_scroll = max_scroll;
      draw_output(tui);
      redraw_input_line(w, buf, len, cursor);
      continue;
    }

    // Ctrl-D (scroll output down)
    if (ch == 4) {
      int half = tui->output_height / 2;
      if (half < 1)
        half = 1;
      tui->output_scroll -= half;
      if (tui->output_scroll < 0)
        tui->output_scroll = 0;
      draw_output(tui);
      redraw_input_line(w, buf, len, cursor);
      continue;
    }

    // Ctrl-X (kill line)
    if (ch == 24) {
      len = 0;
      cursor = 0;
      redraw_input_line(w, buf, len, cursor);
      continue;
    }

    // Ctrl-K (kill to end)
    if (ch == 11) {
      len = cursor;
      redraw_input_line(w, buf, len, cursor);
      continue;
    }

    // Ctrl-W (kill word back)
    if (ch == 23) {
      if (cursor > 0) {
        int end = cursor;
        while (cursor > 0 && buf[cursor - 1] == ' ')
          cursor--;
        while (cursor > 0 && buf[cursor - 1] != ' ')
          cursor--;
        memmove(&buf[cursor], &buf[end], len - end);
        len -= (end - cursor);
        redraw_input_line(w, buf, len, cursor);
      }
      continue;
    }

    // Tab (complete function names for breakpoint command)
    if (ch == '\t') {
      buf[len] = '\0';
      const char *prefix = NULL;
      int prefix_start = 0;

      if (strncmp(buf, "b ", 2) == 0) {
        prefix_start = 2;
      } else if (strncmp(buf, "break ", 6) == 0) {
        prefix_start = 6;
      }

      if (prefix_start > 0) {
        while (prefix_start < len && buf[prefix_start] == ' ')
          prefix_start++;
        prefix = buf + prefix_start;
        size_t prefix_len = len - prefix_start;

        armlet_debugger_collect_completions(tui->dbg);

        const char *matches[256];
        size_t match_count = 0;

        for (size_t i = 0;
             i < tui->dbg->num_completion_names && match_count < 256; i++) {
          const char *name = tui->dbg->completion_names[i];
          if (prefix_len == 0 || strncmp(name, prefix, prefix_len) == 0)
            matches[match_count++] = name;
        }

        if (match_count > 0) {
          size_t lcp_len = strlen(matches[0]);
          for (size_t i = 1; i < match_count; i++) {
            size_t j = 0;
            while (j < lcp_len && matches[i][j] == matches[0][j])
              j++;
            lcp_len = j;
          }

          if (lcp_len > prefix_len) {
            int new_len = prefix_start + (int)lcp_len;
            if (new_len < (int)sizeof(buf)) {
              memcpy(buf + prefix_start, matches[0], lcp_len);
              len = new_len;
              cursor = len;
              redraw_input_line(w, buf, len, cursor);
            }
          }

          if (match_count > 1) {
            for (size_t i = 0; i < match_count; i++)
              armlet_debugger_tui_output(tui, "  %s", matches[i]);
            draw_output(tui);
            draw_cmd(tui);
            redraw_input_line(w, buf, len, cursor);
          }
        }
      }
      continue;
    }

    if (ch == KEY_LEFT) {
      if (cursor > 0) {
        cursor--;
        wmove(w, 2, PROMPT_COL + cursor);
        wrefresh(w);
      }
      continue;
    }

    if (ch == KEY_RIGHT) {
      if (cursor < len) {
        cursor++;
        wmove(w, 2, PROMPT_COL + cursor);
        wrefresh(w);
      }
      continue;
    }

    if (ch == KEY_UP) {
      if (tui->history_count == 0)
        continue;
      if (tui->history_cursor == -1) {
        memcpy(saved_line, buf, len);
        saved_line[len] = '\0';
        tui->history_cursor = tui->history_count - 1;
      } else if (tui->history_cursor > 0) {
        tui->history_cursor--;
      } else {
        continue;
      }
      const char *h = tui->history[tui->history_cursor];
      len = (int)strlen(h);
      if (len > (int)sizeof(buf) - 1)
        len = (int)sizeof(buf) - 1;
      memcpy(buf, h, len);
      cursor = len;
      redraw_input_line(w, buf, len, cursor);
      continue;
    }

    if (ch == KEY_DOWN) {
      if (tui->history_cursor == -1)
        continue;
      if (tui->history_cursor < tui->history_count - 1) {
        tui->history_cursor++;
        const char *h = tui->history[tui->history_cursor];
        len = (int)strlen(h);
        if (len > (int)sizeof(buf) - 1)
          len = (int)sizeof(buf) - 1;
        memcpy(buf, h, len);
        cursor = len;
      } else {
        tui->history_cursor = -1;
        len = (int)strlen(saved_line);
        memcpy(buf, saved_line, len);
        cursor = len;
      }
      redraw_input_line(w, buf, len, cursor);
      continue;
    }

    if (ch == KEY_BACKSPACE || ch == 127 || ch == '\b') {
      if (cursor > 0) {
        memmove(&buf[cursor - 1], &buf[cursor], len - cursor);
        cursor--;
        len--;
        redraw_input_line(w, buf, len, cursor);
      }
      continue;
    }

    if (ch == KEY_DC) {
      if (cursor < len) {
        memmove(&buf[cursor], &buf[cursor + 1], len - cursor - 1);
        len--;
        redraw_input_line(w, buf, len, cursor);
      }
      continue;
    }

    if (len < (int)sizeof(buf) - 1 && isprint(ch)) {
      memmove(&buf[cursor + 1], &buf[cursor], len - cursor);
      buf[cursor] = (char)ch;
      len++;
      cursor++;
      redraw_input_line(w, buf, len, cursor);
    }
  }
}

void armlet_debugger_tui_pause(armlet_debugger_tui *tui) {
  while (tui->dbg->mode == DBG_PAUSED) {
    draw_cmd(tui);

    char *input = read_input(tui);
    if (!input)
      continue;

    if (input[0] == '\0') {
      free(input);
      if (tui->history_count == 0)
        continue;
      input = strdup(tui->history[tui->history_count - 1]);
    } else {
      history_push(tui, input);
    }

    armlet_dbg_command cmd = armlet_dbg_parse_command(input);
    free(input);

    handle_command(tui, &cmd);
    armlet_dbg_command_free(&cmd);
  }
}
