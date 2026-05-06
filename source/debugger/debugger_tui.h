#ifndef __ARMLET_DEBUGGER_TUI__
#define __ARMLET_DEBUGGER_TUI__

#include "debugger.h"
#include "debugger_theme.h"
#include <ncurses.h>
#include <stdio.h>

#define DBG_HISTORY_MAX 64
#define DBG_OUTPUT_LINES 128
#define OUTPUT_HEIGHT 4

typedef struct armlet_debugger_tui {
  WINDOW *source_win;
  WINDOW *stack_win;
  WINDOW *watch_win;
  WINDOW *output_win;
  WINDOW *cmd_win;
  SCREEN *screen;

  int term_rows;
  int term_cols;

  int source_scroll;
  int source_width;
  int watch_width;
  int stack_height;
  int cmd_height;
  int output_height;

  int capture_pipe[2];
  int saved_stdout_fd;
  FILE *term_out;

  char *output_buf[DBG_OUTPUT_LINES];
  int output_count;

  int output_scroll;

  char *history[DBG_HISTORY_MAX];
  int history_count;
  int history_cursor;

  struct armlet_debugger *dbg;
  const armlet_debugger_theme *theme;
} armlet_debugger_tui;

armlet_debugger_tui *armlet_debugger_tui_init(struct armlet_debugger *dbg);
void armlet_debugger_tui_cleanup(armlet_debugger_tui *tui);

void armlet_debugger_tui_draw(armlet_debugger_tui *tui);
void armlet_debugger_tui_center_on_line(armlet_debugger_tui *tui, size_t line);

void armlet_debugger_tui_output(armlet_debugger_tui *tui, const char *fmt, ...);

void armlet_debugger_tui_drain_stdout(armlet_debugger_tui *tui);

void armlet_debugger_tui_pause(armlet_debugger_tui *tui);

#endif
