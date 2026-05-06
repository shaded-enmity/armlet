#ifndef __ARMLET_AST__
#define __ARMLET_AST__
#define _POSIX_C_SOURCE 200809L

#include "utils/common.h"
#include "utils/hashtable.h"

#include <gmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

enum armlet_ast_node_type {
  AST_CMP,
  AST_BINOP,
  AST_UNARY,
  AST_ASSIGNMENT,
  AST_CALL,
  AST_FUNDEF,
  AST_BITSEL,
  AST_BITSEL_RANGE,
  AST_FIELDSEL,
  AST_BITSLURP,
  AST_IF,
  AST_WHEN,
  AST_TUPLE,
  AST_VALUE,
  AST_TYPE,
  AST_ENUM,
  AST_RETURN,
  AST_LOOP,
  AST_ASSERT,
  AST_SUITE,
  AST_VAR_DEF,
  AST_TYPE_SPEC,
  AST_PARAMETER,
  AST_SET,
  AST_BLOCK,
  AST_INLINE_IF,
  AST_ARRAY,
  AST_ARRAY_ACCESS,
  AST_TYPE_ALIAS,
  AST_IMPORT,
  AST_USE,
  AST_ARRAY_LITERAL,
  AST_UNKNOWN,
  AST_IMPLEMENTATION_DEFINED,
  AST_TRAP,
  AST_QUALIFIED_LHS,
  AST_BITLAYOUT,
};

typedef struct {
  char *data;
  char *file;
} armlet_source;

typedef struct {
  char *line;
  size_t lineno;
  size_t col_start;
  size_t col_end;
} armlet_line_info;

typedef struct {
  uint32_t start;
  uint32_t end;
} armlet_span;

enum armlet_vm_qualifier {
  QUALIFIER_NONE = 0,
  QUALIFIER_CONSTANT = 1,
};

enum armlet_vm_qualified {
  QUALIFIED_TYPED,
  QUALIFIED_TUPLE,
  QUALIFIED_NAMED,
};

enum armlet_vm_trap {
  TRAP_UNDEFINED,
  TRAP_UNPREDICTABLE,
  TRAP_SEE,
};

enum armlet_vm_comparison {
  CMP_EQ,
  CMP_NEQ,
  CMP_GT,
  CMP_LT,
  CMP_GTEQ,
  CMP_LTEQ,
  CMP_IN,
};

enum armlet_vm_binop {
  BINOP_ADD,
  BINOP_SUB,
  BINOP_DIV,
  BINOP_FDIV,
  BINOP_MUL,
  BINOP_CONCAT,
  BINOP_OFF_CONCAT,
  BINOP_PWR,
  BINOP_SHR,
  BINOP_SHL,
  BINOP_EOR,
  BINOP_AND,
  BINOP_OR,
  BINOP_MOD,
};

enum armlet_vm_unary_op {
  UNARY_MINUS,
  UNARY_NEGATION,
};

enum armlet_immediate_tag {
  IMM_INTEGER,
  IMM_FLOAT,
  IMM_BITSTRING,
  IMM_BOOLEAN,
  IMM_STRING,
};

enum armlet_value_tag {
  VAL_IMMEDIATE,
  VAL_NAME,
  VAL_DEREF,
};

typedef struct {
  DEFINE_ARRAY(char *, names);
} armlet_dereference;

typedef struct {
  enum armlet_immediate_tag tag;

  union {
    mpz_t integer;
    float real;
    bool boolean;
    char *bits;
    char *string;
  };
} armlet_immediate;

typedef struct {
  enum armlet_value_tag tag;
  union {
    armlet_immediate imm;
    armlet_dereference deref;
    char *name;
  };
} armlet_ast_value;

typedef struct {
  struct armlet_ast_node *source;
  struct armlet_ast_node *target;
} armlet_ast_assignment;

typedef struct {
  DEFINE_ARRAY(struct armlet_ast_node *, nodes);
} armlet_ast_block;

typedef struct {
  struct armlet_ast_node *left;
  struct armlet_ast_node *right;
  enum armlet_vm_binop op;
} armlet_ast_binop;

typedef struct {
  struct armlet_ast_node *left;
  struct armlet_ast_node *right;
  enum armlet_vm_comparison op;
} armlet_ast_cmp;

typedef struct {
  struct armlet_ast_node *condition;
  struct armlet_ast_node *consequence;
} armlet_ast_cond_branch;

typedef struct {
  DEFINE_ARRAY(armlet_ast_cond_branch *, conditions);
  struct armlet_ast_node *alternative;
} armlet_ast_if;

typedef struct {
  DEFINE_ARRAY(armlet_ast_cond_branch *, conditions);
  struct armlet_ast_node *condition;
  struct armlet_ast_node *consequence;
  struct armlet_ast_node *alternative;
} armlet_ast_inline_if;

typedef struct {
  struct armlet_ast_node *match;
  DEFINE_ARRAY(armlet_ast_cond_branch *, cases);
  struct armlet_ast_node *otherwise;
} armlet_ast_case;

typedef struct {
  struct armlet_ast_node *name;
  struct armlet_ast_node *start;
  struct armlet_ast_node *end;
} armlet_ast_loop_limit;

typedef struct {
  struct armlet_ast_node *source;
  DEFINE_ARRAY(struct armlet_ast_node *, selections);
} armlet_ast_bitselect;

typedef struct {
  struct armlet_ast_node *source;
  DEFINE_ARRAY(struct armlet_ast_node *, selections);
} armlet_ast_fieldselect;

typedef struct {
  DEFINE_ARRAY(struct armlet_ast_node *, elements);
} armlet_ast_tuple;

typedef struct {
  DEFINE_ARRAY(struct armlet_ast_node *, elements);
} armlet_ast_bitslurp;

typedef struct {
  struct armlet_ast_node *type;
  struct armlet_ast_node *name;
} armlet_ast_parameter;

typedef struct {
  struct armlet_ast_node *name;
  DEFINE_ARRAY(struct armlet_ast_node *, parameters);
} armlet_ast_call;

typedef struct {
  char *name;

  DEFINE_ARRAY(struct armlet_ast_node *, fields);
} armlet_ast_type_definition;

typedef struct {
  char *name;

  DEFINE_ARRAY(char *, elements);
} armlet_ast_enum_definition;

enum armlet_vm_contract_type {
  CONTRACT_NONE,
  CONTRACT_NAMED,
  CONTRACT_COMPUTED,
  CONTRACT_IMMEDIATE,
};

typedef struct {
  struct armlet_ast_node *type;
  DEFINE_ARRAY(struct armlet_ast_node *, names);
  enum armlet_vm_contract_type contract;
} armlet_ast_var_definition;

enum armlet_callable_type {
  CALLABLE_FUNC,
  CALLABLE_GETTER,
  CALLABLE_SETTER,
};

typedef struct {
  enum armlet_callable_type type;

  struct armlet_ast_node *name;

  union {
    struct armlet_ast_node *return_type;
    armlet_ast_var_definition *input_type;
  };

  armlet_ast_block *body;

  DEFINE_ARRAY(armlet_ast_var_definition *, parameters);
} armlet_ast_callable_definition;

typedef struct {
  struct armlet_ast_node *return_;
} armlet_ast_return;

typedef struct {
  char *name;
  size_t index;
} armlet_vm_parameter_named_size;

typedef struct {
  enum armlet_vm_contract_type tag;
  char *name;
  armlet_vm_parameter_named_size *named;
} armlet_vm_parameter_contract;

typedef struct {
  DEFINE_ARRAY(armlet_vm_parameter_contract *, contracts);
} armlet_vm_function_input_contract;

enum armlet_loop_tag {
  LOOP_FOR_TO,
  LOOP_FOR_DOWNTO,
  LOOP_WHILE,
  LOOP_REPEAT,
};

typedef struct {
  enum armlet_loop_tag loop_type;

  union {
    struct armlet_ast_node *condition;
    armlet_ast_loop_limit *range;
  };

  struct armlet_ast_node *block;
} armlet_ast_loop;

typedef struct {
  enum armlet_vm_unary_op op;
  struct armlet_ast_node *node;
} armlet_ast_unary;

typedef struct {
  struct armlet_ast_node *condition;
} armlet_ast_assert;

typedef struct {
  bool constant;
  struct armlet_ast_node *name;
  struct armlet_ast_node *size;
} armlet_ast_type_spec;

typedef struct {
  DEFINE_ARRAY(struct armlet_ast_node *, members);
} armlet_ast_set;

typedef struct {
  DEFINE_ARRAY(struct armlet_ast_node *, members);
} armlet_ast_array_literal;

typedef struct {
  struct armlet_ast_node *type;
  struct armlet_ast_node *name;
  struct armlet_ast_node *start;
  struct armlet_ast_node *end;
} armlet_ast_array;

typedef struct {
  struct armlet_ast_node *name;
  DEFINE_ARRAY(struct armlet_ast_node *, indices);
} armlet_ast_array_access;

typedef struct {
  struct armlet_ast_node *from;
  struct armlet_ast_node *to;
} armlet_ast_type_alias;

typedef struct {
  char *path;
} armlet_ast_import;

typedef struct {
  struct armlet_ast_node *target;
} armlet_ast_use;

typedef struct {
  struct armlet_ast_node *type;
  char *key;
} armlet_ast_implementation_defined;

typedef armlet_ast_type_spec armlet_ast_unknown;

typedef struct {
  enum armlet_vm_trap trap_type;
  char *context;
} armlet_ast_trap;

typedef struct {
  enum armlet_vm_qualified qtype;
  enum armlet_vm_qualifier qualifiers;
  struct armlet_ast_node *inner;
} armlet_ast_qualified;

enum armlet_ast_bitlayout_member_type {
  BITLAYOUT_IMMEDIATE,
  BITLAYOUT_NAMED,
};

typedef struct {
  enum armlet_ast_bitlayout_member_type mtype;

  union {
    char *immediate;

    struct {
      char *name;
      size_t size;
    };
  };
} armlet_ast_bitlayout_member;

typedef struct {
  char *name;
  size_t total;
  uint8_t *compare_mask;

  char *argument_name;
  struct armlet_ast_node *handler;

  DEFINE_ARRAY(armlet_ast_bitlayout_member *, members);
} armlet_ast_bitlayout;

typedef struct {
  armlet_span span;
  armlet_source source;
} armlet_node_source;

typedef struct armlet_ast_node {
  const struct armlet_ast_node *parent;
  enum armlet_ast_node_type type;

  union {
    armlet_ast_assignment *assignment;
    armlet_ast_block *block;
    armlet_ast_binop *binop;
    armlet_ast_cmp *cmp;
    armlet_ast_cond_branch *branch;
    armlet_ast_if *if_;
    armlet_ast_case *case_;
    armlet_ast_call *call;
    armlet_ast_loop_limit *range;
    armlet_ast_bitselect *bitselect;
    armlet_ast_fieldselect *fieldselect;
    armlet_ast_tuple *tuple;
    armlet_ast_value *value;
    armlet_ast_parameter *parameter;
    armlet_ast_type_definition *type_def;
    armlet_ast_enum_definition *enum_def;
    armlet_ast_var_definition *var_def;
    armlet_ast_callable_definition *callable_def;
    armlet_ast_return *return_;
    armlet_ast_loop *loop;
    armlet_ast_unary *unary;
    armlet_ast_assert *assert;
    armlet_ast_type_spec *type_spec;
    armlet_ast_set *set;
    armlet_ast_bitslurp *bitslurp;
    armlet_ast_inline_if *inline_if;
    armlet_ast_array *array;
    armlet_ast_array_access *array_access;
    armlet_ast_type_alias *type_alias;
    armlet_ast_import *import;
    armlet_ast_use *use;
    armlet_ast_array_literal *array_literal;
    armlet_ast_unknown *unknown;
    armlet_ast_implementation_defined *implementation_defined;
    armlet_ast_trap *trap;
    armlet_ast_qualified *qualified;
    armlet_ast_bitlayout *bitlayout;
    struct {
      DEFINE_ARRAY(struct armlet_ast_node *, suite);
    };
  };

  armlet_node_source *source;
} armlet_ast_node;

extern const char *armlet_ast_node_type_names[];

char *armlet_source_string(armlet_node_source *source);
char *slurp_file(const char *path, size_t *out_len);
armlet_line_info armlet_line_info_from_span(armlet_span *, char *);

void armlet_emit_source_diagnostic(const armlet_source *source,
                                   armlet_span span, char *message);

void armlet_source_error_v(const armlet_source *source, armlet_span span,
                           const char *fmt, va_list args);

void armlet_source_error_n(const armlet_ast_node *n, const char *fmt, ...);

void armlet_source_error(const armlet_source *source, armlet_span span,
                         const char *fmt, ...);

#endif
