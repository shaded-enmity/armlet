#include "source/ast_print.h"
#include "source/debugger/debugger.h"
#include "source/debugger/debugger_tui.h"
#include "source/interpreter.h"
#include "source/parser.h"
#include "source/serialization.h"
#include "source/utils/common.h"

#include <bits/getopt_core.h>
#include <getopt.h>
#include <stdio.h>
#include <sys/cdefs.h>
#include <unistd.h>

struct {
  bool usage;
  bool debug;
  bool strict;
  bool print_ast;
  bool print_ast_json;
  bool print_ast_spans;
  bool debugger;
  char *file_name;
  char *implementation_defined;
  char *unknown_source;
  char *import_path;
} armlet_opts = {
    .usage = false,
    .debug = false,
    .strict = false,
    .print_ast = false,
    .print_ast_json = false,
    .print_ast_spans = false,
    .debugger = false,
    .file_name = NULL,
    .implementation_defined = NULL,
    .unknown_source = "/dev/urandom",
    .import_path = NULL,
};

static void usage(const char *prog) {
  fprintf(stderr,
          "armlet v%u.%u.%u\n"
          "Usage: %s [OPTIONS] <file>\n\n"
          "Options:\n"
          "  -h, --help                           Show this help and exit\n"
          "  -d, --debug                          Enable debug mode\n"
          "  -D, --debugger                       Launch TUI debugger\n"
          "  -s, --strict                         Enable strict mode\n"
          "  -u, --unknown <FILE>                 Unknown semantics [default: "
          "/dev/urandom]\n"
          "  -i, --implementation-defined <FILE>  File to provide "
          "implementation defined values\n"
          "  -p, --print-ast                      Print AST as indented tree "
          "and exit\n"
          "  -P, --print-ast-json                 Print AST as JSON and exit\n"
          "  -L, --print-ast-spans                Include source spans in "
          "AST output\n"
          "  -I, --import-path <DIR>              Extra directory to search "
          "for imports\n",
          ARMLET_VER_MAJOR, ARMLET_VER_MINOR, ARMLET_VER_REVISION, prog);
}

void fopen_error(const char *path, const char *label) {
  char msg[1024];
  snprintf(msg, sizeof(msg), "%s (%s)", label, path);
  perror(msg);
}

int handle_implementation_defined(armlet_vm_context *vm) {
  if (armlet_opts.implementation_defined != NULL) {
    FILE *f = fopen(armlet_opts.implementation_defined, "rb");

    if (f == NULL) {
      fopen_error(armlet_opts.implementation_defined,
                  "Implementation Defined File");
      return 1;
    }

    armlet_serialize_value value = {};
    int rc = armlet_vm_deserialize(vm, f, &value);
    fclose(f);

    if (rc != 0) {
      fprintf(stderr,
              "Implementation Defined File (%s): deserialization failed\n",
              armlet_opts.implementation_defined);
      return 1;
    }

    if (value.tag != SERIALIZE_HASHTABLE) {
      fprintf(stderr,
              "Implementation Defined File (%s) doesn't contain proper data\n",
              armlet_opts.implementation_defined);
      return 1;
    }

    vm->config.implementation_defined.values = value.hashtable;

    // Reopen in write mode for end_implementation_defined() to serialize back
    f = fopen(armlet_opts.implementation_defined, "wb");
    if (f == NULL) {
      fopen_error(armlet_opts.implementation_defined,
                  "Implementation Defined File for writing");
      return 1;
    }
    vm->config.implementation_defined.file = f;
  }

  return 0;
}

int command_line_options(int argc, char **argv) {
  static struct option long_options[] = {
      {"help", no_argument, NULL, 'h'},
      {"debug", no_argument, NULL, 'd'},
      {"debugger", no_argument, NULL, 'D'},
      {"strict", no_argument, NULL, 's'},
      {"unknown", required_argument, NULL, 'u'},
      {"implementation-defined", required_argument, NULL, 'i'},
      {"print-ast", no_argument, NULL, 'p'},
      {"print-ast-json", no_argument, NULL, 'P'},
      {"print-ast-spans", no_argument, NULL, 'L'},
      {"import-path", required_argument, NULL, 'I'},
      {NULL, 0, NULL, 0}};

  int opt;
  int option_index = 0;

  while ((opt = getopt_long(argc, argv, "hdDsu:i:pPLI:", long_options,
                            &option_index)) != -1) {
    switch (opt) {
    case 'h':
      armlet_opts.usage = true;
      usage(argv[0]);
      return 0;
    case 's':
      armlet_opts.strict = true;
      break;
    case 'd':
      armlet_opts.debug = true;
      break;
    case 'D':
      armlet_opts.debugger = true;
      break;
    case 'u':
      armlet_opts.unknown_source = optarg;
      break;
    case 'i':
      armlet_opts.implementation_defined = optarg;
      break;
    case 'p':
      armlet_opts.print_ast = true;
      break;
    case 'P':
      armlet_opts.print_ast_json = true;
      break;
    case 'L':
      armlet_opts.print_ast_spans = true;
      break;
    case 'I':
      armlet_opts.import_path = optarg;
      break;
    case '?':
    default:
      usage(argv[0]);
      return 1;
    }
  }

  if (optind < argc) {
    armlet_opts.file_name = argv[optind++];
    if (optind < argc) {
      fprintf(stderr,
              "ERROR: extra arguments provided (only one <file> expected)\n");
      usage(argv[0]);
      return 1;
    }
  } else {
    fprintf(stderr, "ERROR: missing required <file> argument\n");
    usage(argv[0]);
    return 1;
  }

  return 0;
}

static void lazy_debugger_init(armlet_vm_context *vm, void *userdata) {
  if (!isatty(STDIN_FILENO)) {
    return;
  }
  armlet_ast_node *ast_root = (armlet_ast_node *)userdata;
  armlet_debugger *dbg = armlet_debugger_init(vm);
  dbg->ast_root = ast_root;
}

int main(int argc, char *argv[]) {
  if (command_line_options(argc, argv) != 0) {
    return 1;
  }

  if (armlet_opts.usage)
    return 0;

  if (armlet_opts.import_path)
    armlet_parser_set_import_path(armlet_opts.import_path);

  armlet_ast_node *n = armlet_parse_file_pure(armlet_opts.file_name, armlet_opts.debug);
  if (n == NULL) {
    fprintf(stderr, "ERROR: AST processing failed\n");
    return 1;
  }

  if (n->type != AST_SUITE) {
    armlet_source_error_n(
        n, "ERROR: Parsing failed, root node should be AST_SUITE but got: '%s'",
        armlet_ast_node_type_names[n->type]);
  }

  if (armlet_opts.print_ast || armlet_opts.print_ast_json) {
    int flags = 0;
    if (armlet_opts.print_ast_json)
      flags |= AST_PRINT_JSON;
    if (armlet_opts.print_ast_spans)
      flags |= AST_PRINT_SPANS;
    armlet_ast_print(n, stdout, flags);
    return 0;
  }

  armlet_vm_context *vm = armlet_vm_init(&n->source->source);
  vm->config.debug = armlet_opts.debug;
  vm->config.strict = armlet_opts.strict;
  vm->config.initializer = fopen(armlet_opts.unknown_source, "rb");

  if (vm->config.initializer == NULL) {
    fopen_error(armlet_opts.unknown_source,
                "File couldn't be opened");
    return 1;
  }

  if (handle_implementation_defined(vm) != 0) {
    return 1;
  }

  armlet_vm_init_builtins(vm);

  vm->debugger_lazy_init = lazy_debugger_init;
  vm->debugger_lazy_init_userdata = n;

  armlet_debugger *dbg = NULL;
  if (armlet_opts.debugger) {
    dbg = armlet_debugger_init(vm);
    dbg->ast_root = n;
  }

  armlet_vm_run(vm, n);

  // To support the 'break()' built-in
  if (dbg == NULL) {
    dbg = (armlet_debugger *)vm->debugger_userdata;
  }

  if (dbg) {
    while (armlet_debugger_post_run(dbg)) {
      dbg->finished = false;
      dbg->restart_requested = false;
      dbg->mode = DBG_STEP;
      dbg->current_file = NULL;
      dbg->current_line = 0;
      dbg->current_node = NULL;
      dbg->skip_bp_file = NULL;
      dbg->skip_bp_line = 0;

      armlet_vm_context *new_vm = armlet_vm_init(&n->source->source);
      new_vm->config.debug = armlet_opts.debug;
      new_vm->config.strict = armlet_opts.strict;
      new_vm->config.initializer = fopen(armlet_opts.unknown_source, "rb");
      if (new_vm->config.initializer == NULL) {
        armlet_debugger_tui_output(dbg->tui, "Failed to reopen unknown source");
        break;
      }
      handle_implementation_defined(new_vm);
      armlet_vm_init_builtins(new_vm);

      dbg->vm = new_vm;
      new_vm->debugger_hook = armlet_debugger_hook;
      new_vm->debugger_userdata = dbg;
      new_vm->debugger_lazy_init = lazy_debugger_init;
      new_vm->debugger_lazy_init_userdata = n;

      armlet_debugger_tui_output(dbg->tui, "Restarting program...");
      armlet_debugger_tui_draw(dbg->tui);

      armlet_vm_run(new_vm, n);
    }

    armlet_debugger_cleanup(dbg);
  }

  if (vm->config.implementation_defined.file != NULL) {
    fclose(vm->config.implementation_defined.file);
  }
  if (vm->config.initializer != NULL) {
    fclose(vm->config.initializer);
  }

  return 0;
}
