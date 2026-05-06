#define _GNU_SOURCE
#include "debugger_commands.h"
#include "../utils/common.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static char *skip_whitespace(const char *s) {
  while (*s && isspace((unsigned char)*s))
    s++;
  return (char *)s;
}

static char *extract_arg(const char *s) {
  s = skip_whitespace(s);
  if (*s == '\0')
    return NULL;
  // trim trailing whitespace
  const char *end = s + strlen(s) - 1;
  while (end > s && isspace((unsigned char)*end))
    end--;
  size_t len = (size_t)(end - s) + 1;
  char *arg = CHECKED_MALLOC(len + 1);
  memcpy(arg, s, len);
  arg[len] = '\0';
  return arg;
}

armlet_dbg_command armlet_dbg_parse_command(const char *input) {
  armlet_dbg_command cmd = {.type = CMD_NONE, .arg = NULL};

  if (!input)
    return cmd;

  const char *s = skip_whitespace(input);
  if (*s == '\0')
    return cmd;

  const char *cmd_end = s;
  while (*cmd_end && !isspace((unsigned char)*cmd_end))
    cmd_end++;

  size_t cmd_len = (size_t)(cmd_end - s);

#define MATCH(short, long)                                                     \
  ((cmd_len == strlen(short) && strncmp(s, short, cmd_len) == 0) ||            \
   (cmd_len == strlen(long) && strncmp(s, long, cmd_len) == 0))

  if (s[0] == '/') {
    cmd.type = CMD_SEARCH;
    if (s[1] != '\0')
      cmd.arg = extract_arg(s + 1);
    return cmd;
  }

  if (MATCH("b", "break")) {
    cmd.type = CMD_BREAK;
    cmd.arg = extract_arg(cmd_end);
  } else if (MATCH("c", "continue")) {
    cmd.type = CMD_CONTINUE;
  } else if (MATCH("s", "step")) {
    cmd.type = CMD_STEP;
  } else if (MATCH("n", "next")) {
    cmd.type = CMD_NEXT;
  } else if (MATCH("f", "finish")) {
    cmd.type = CMD_FINISH;
  } else if (MATCH("w", "watch")) {
    cmd.type = CMD_WATCH;
    cmd.arg = extract_arg(cmd_end);
  } else if (cmd_len == 7 && strncmp(s, "unwatch", 7) == 0) {
    cmd.type = CMD_UNWATCH;
    cmd.arg = extract_arg(cmd_end);
  } else if (MATCH("p", "print")) {
    cmd.type = CMD_PRINT;
    cmd.arg = extract_arg(cmd_end);
  } else if (MATCH("bt", "backtrace")) {
    cmd.type = CMD_BACKTRACE;
  } else if (MATCH("l", "list")) {
    cmd.type = CMD_LIST;
    cmd.arg = extract_arg(cmd_end);
  } else if (MATCH("r", "restart")) {
    cmd.type = CMD_RESTART;
  } else if (MATCH("q", "quit")) {
    cmd.type = CMD_QUIT;
  } else if (cmd_len == 2 && strncmp(s, "up", 2) == 0) {
    cmd.type = CMD_UP;
  } else if (cmd_len == 4 && strncmp(s, "down", 4) == 0) {
    cmd.type = CMD_DOWN;
  } else if (cmd_len == 6 && strncmp(s, "enable", 6) == 0) {
    cmd.type = CMD_ENABLE;
    cmd.arg = extract_arg(cmd_end);
  } else if (cmd_len == 7 && strncmp(s, "disable", 7) == 0) {
    cmd.type = CMD_DISABLE;
    cmd.arg = extract_arg(cmd_end);
  } else if (MATCH("i", "info")) {
    cmd.type = CMD_INFO;
    cmd.arg = extract_arg(cmd_end);
  } else if (MATCH("h", "help")) {
    cmd.type = CMD_HELP;
  } else {
    cmd.type = CMD_UNKNOWN;
    cmd.arg = extract_arg(s);
  }

#undef MATCH
  return cmd;
}

void armlet_dbg_command_free(armlet_dbg_command *cmd) {
  if (cmd && cmd->arg) {
    free(cmd->arg);
    cmd->arg = NULL;
  }
}
