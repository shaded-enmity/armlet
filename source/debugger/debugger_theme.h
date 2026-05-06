#ifndef __ARMLET_DEBUGGER_THEME__
#define __ARMLET_DEBUGGER_THEME__

#include <ncurses.h>

enum {
  CLR_SOURCE_TEXT = 1, // normal source code text
  CLR_CURRENT_LINE,    // current execution line highlight
  CLR_BREAKPOINT,      // breakpoint marker in gutter
  CLR_GUTTER,          // line numbers
  CLR_PROMPT,          // (dbg) prompt
  CLR_TITLE,           // window title bars
  CLR_WATCH_NAME,      // variable names in watch panel
  CLR_WATCH_VALUE,     // variable values in watch panel
  CLR_OUTPUT,          // output panel text
  CLR_STATUS,          // status line
  CLR_SEPARATOR,       // separator lines
  CLR_CURRENT_ARROW,   // > arrow on current line
  CLR_STACK_CURRENT,   // current frame in stack pane
  CLR_STACK_FRAME,     // non-current frames in stack pane

  CLR_SYN_KEYWORD,     // keywords (if, while, return, ...)
  CLR_SYN_STRING,      // strings, bitstrings
  CLR_SYN_COMMENT,     // comments
  CLR_SYN_NUMBER,      // integer/float literals
  CLR_SYN_FUNCTION,    // function names
  CLR_SYN_TYPE,        // type names, enums
  CLR_SYN_OPERATOR,    // operators
  CLR_SYN_BUILTIN,     // built-in functions/booleans

  CLR_SLOT_COUNT       // must be last
};

typedef struct {
  const char *name;

  struct {
    short fg;
    short bg;
  } colors[CLR_SLOT_COUNT];

  bool bold[CLR_SLOT_COUNT];
} armlet_debugger_theme;

static const armlet_debugger_theme armlet_theme_norton_commander = {
    .name = "norton",
    .colors =
        {
            [0] = {0, 0},
            [CLR_SOURCE_TEXT] = {COLOR_WHITE, COLOR_BLUE},
            [CLR_CURRENT_LINE] = {COLOR_BLACK, COLOR_CYAN},
            [CLR_BREAKPOINT] = {COLOR_RED, COLOR_BLUE},
            [CLR_GUTTER] = {COLOR_CYAN, COLOR_BLUE},
            [CLR_PROMPT] = {COLOR_WHITE, COLOR_BLUE},
            [CLR_TITLE] = {COLOR_BLACK, COLOR_CYAN},
            [CLR_WATCH_NAME] = {COLOR_WHITE, COLOR_BLUE},
            [CLR_WATCH_VALUE] = {COLOR_YELLOW, COLOR_BLUE},
            [CLR_OUTPUT] = {COLOR_WHITE, COLOR_BLUE},
            [CLR_STATUS] = {COLOR_CYAN, COLOR_BLUE},
            [CLR_SEPARATOR] = {COLOR_CYAN, COLOR_BLUE},
            [CLR_CURRENT_ARROW] = {COLOR_YELLOW, COLOR_BLUE},
            [CLR_STACK_CURRENT] = {COLOR_YELLOW, COLOR_BLUE},
            [CLR_STACK_FRAME] = {COLOR_CYAN, COLOR_BLUE},
            [CLR_SYN_KEYWORD] = {COLOR_YELLOW, COLOR_BLUE},
            [CLR_SYN_STRING] = {COLOR_GREEN, COLOR_BLUE},
            [CLR_SYN_COMMENT] = {COLOR_CYAN, COLOR_BLUE},
            [CLR_SYN_NUMBER] = {COLOR_MAGENTA, COLOR_BLUE},
            [CLR_SYN_FUNCTION] = {COLOR_WHITE, COLOR_BLUE},
            [CLR_SYN_TYPE] = {COLOR_GREEN, COLOR_BLUE},
            [CLR_SYN_OPERATOR] = {COLOR_WHITE, COLOR_BLUE},
            [CLR_SYN_BUILTIN] = {COLOR_CYAN, COLOR_BLUE},
        },
    .bold =
        {
            [CLR_SOURCE_TEXT] = false,
            [CLR_CURRENT_LINE] = true,
            [CLR_BREAKPOINT] = true,
            [CLR_GUTTER] = false,
            [CLR_PROMPT] = true,
            [CLR_TITLE] = true,
            [CLR_WATCH_NAME] = true,
            [CLR_WATCH_VALUE] = false,
            [CLR_OUTPUT] = false,
            [CLR_STATUS] = false,
            [CLR_SEPARATOR] = false,
            [CLR_CURRENT_ARROW] = true,
            [CLR_STACK_CURRENT] = true,
            [CLR_STACK_FRAME] = false,
            [CLR_SYN_KEYWORD] = true,
            [CLR_SYN_STRING] = false,
            [CLR_SYN_COMMENT] = false,
            [CLR_SYN_NUMBER] = false,
            [CLR_SYN_FUNCTION] = true,
            [CLR_SYN_TYPE] = true,
            [CLR_SYN_OPERATOR] = false,
            [CLR_SYN_BUILTIN] = true,
        },
};

static const armlet_debugger_theme armlet_theme_default = {
    .name = "default",
    .colors =
        {
            [0] = {0, 0},
            [CLR_SOURCE_TEXT] = {-1, -1},
            [CLR_CURRENT_LINE] = {COLOR_BLACK, COLOR_YELLOW},
            [CLR_BREAKPOINT] = {COLOR_RED, -1},
            [CLR_GUTTER] = {COLOR_CYAN, -1},
            [CLR_PROMPT] = {COLOR_GREEN, -1},
            [CLR_TITLE] = {COLOR_WHITE, COLOR_BLUE},
            [CLR_WATCH_NAME] = {-1, -1},
            [CLR_WATCH_VALUE] = {COLOR_YELLOW, -1},
            [CLR_OUTPUT] = {COLOR_WHITE, -1},
            [CLR_STATUS] = {-1, -1},
            [CLR_SEPARATOR] = {-1, -1},
            [CLR_CURRENT_ARROW] = {-1, -1},
            [CLR_STACK_CURRENT] = {COLOR_YELLOW, -1},
            [CLR_STACK_FRAME] = {-1, -1},
            [CLR_SYN_KEYWORD] = {COLOR_BLUE, -1},
            [CLR_SYN_STRING] = {COLOR_GREEN, -1},
            [CLR_SYN_COMMENT] = {COLOR_WHITE, -1},
            [CLR_SYN_NUMBER] = {COLOR_MAGENTA, -1},
            [CLR_SYN_FUNCTION] = {-1, -1},
            [CLR_SYN_TYPE] = {COLOR_CYAN, -1},
            [CLR_SYN_OPERATOR] = {-1, -1},
            [CLR_SYN_BUILTIN] = {COLOR_CYAN, -1},
        },
    .bold =
        {
            [CLR_SOURCE_TEXT] = false,
            [CLR_CURRENT_LINE] = false,
            [CLR_BREAKPOINT] = true,
            [CLR_GUTTER] = false,
            [CLR_PROMPT] = true,
            [CLR_TITLE] = true,
            [CLR_WATCH_NAME] = true,
            [CLR_WATCH_VALUE] = false,
            [CLR_OUTPUT] = false,
            [CLR_STATUS] = false,
            [CLR_SEPARATOR] = false,
            [CLR_CURRENT_ARROW] = true,
            [CLR_STACK_CURRENT] = true,
            [CLR_STACK_FRAME] = false,
            [CLR_SYN_KEYWORD] = true,
            [CLR_SYN_STRING] = false,
            [CLR_SYN_COMMENT] = false,
            [CLR_SYN_NUMBER] = false,
            [CLR_SYN_FUNCTION] = true,
            [CLR_SYN_TYPE] = false,
            [CLR_SYN_OPERATOR] = false,
            [CLR_SYN_BUILTIN] = true,
        },
};

static inline void armlet_theme_init(const armlet_debugger_theme *t) {
  for (int i = 1; i < CLR_SLOT_COUNT; i++)
    init_pair(i, t->colors[i].fg, t->colors[i].bg);
}

static inline attr_t armlet_theme_attr(const armlet_debugger_theme *t, int slot) {
  attr_t a = COLOR_PAIR(slot);
  if (t->bold[slot])
    a |= A_BOLD;
  return a;
}

#endif
