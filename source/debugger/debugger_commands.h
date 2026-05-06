#ifndef __ARMLET_DEBUGGER_COMMANDS__
#define __ARMLET_DEBUGGER_COMMANDS__

typedef enum {
  CMD_NONE,
  CMD_BREAK,
  CMD_CONTINUE,
  CMD_STEP,
  CMD_NEXT,
  CMD_WATCH,
  CMD_UNWATCH,
  CMD_PRINT,
  CMD_BACKTRACE,
  CMD_LIST,
  CMD_RESTART,
  CMD_QUIT,
  CMD_FINISH,
  CMD_UP,
  CMD_DOWN,
  CMD_ENABLE,
  CMD_DISABLE,
  CMD_SEARCH,
  CMD_INFO,
  CMD_HELP,
  CMD_UNKNOWN,
} armlet_dbg_cmd_type;

typedef struct {
  armlet_dbg_cmd_type type;
  char *arg;
} armlet_dbg_command;

armlet_dbg_command armlet_dbg_parse_command(const char *input);
void armlet_dbg_command_free(armlet_dbg_command *cmd);

#endif
