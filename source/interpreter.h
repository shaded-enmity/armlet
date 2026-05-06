#ifndef __ARMLET_INTERPRETER__
#define __ARMLET_INTERPRETER__
#define _GNU_SOURCE

#include "ast.h"
#include "utils/common.h"
#include "bitlayout_decoder.h"

#include <stdio.h>

enum armlet_vm_var_tag {
  T_INVALID,
  T_INTEGER,
  T_REAL,
  T_BOOLEAN,
  T_BITS,
  T_TYPE,
  T_INSTANCE,
  T_ENUMERATION_TYPE,
  T_ENUMERATION,
  T_ARRAY,
  T_SET,
  T_TUPLE,
  T_RANGE,
  T_FUNCTION,
  T_SCOPE,
  T_GETTER,
  T_SETTER,
  T_BUILTIN,
  T_STRING,
  T_TYPE_ALIAS,
  T_BITLAYOUT,
};

typedef uint64_t armlet_vm_var_tag_mask;

extern const char *armlet_vm_var_tag_names[];
typedef struct armlet_vm_type armlet_vm_type;

typedef size_t armlet_refcount;

// Instead of having explicit static flag we just set the refcount to to a
// maximum size so that we don't have to do if (is_static) ... and can just
// always decrement the value because it's unrealistic for Armlet programs to
// reach this point, and if you do, congratulations, PR welcome.
#define ARMLET_REFCOUNT_STATIC (SIZE_MAX >> 1)

void cleanup_free(void *o);
void cleanup_named_weak(void *o);
void cleanup_mpz(mpz_t *v);

#define ARMLET_MPZ_VAR(name)                                                   \
  mpz_t name __attribute__((cleanup(cleanup_mpz)));                            \
  mpz_init(name);

#define ARMLET_NAMED_WEAK(name)                                                \
  armlet_vm_named_array *name __attribute__((cleanup(cleanup_named_weak)))

#define ARMLET_STRING(name) char *name __attribute__((cleanup(cleanup_free)))

#define ARMLET_VAR(name)                                                       \
  armlet_vm_var *name __attribute__((cleanup(armlet_cleanup_var)))

#define ARMLET_TYPE(name)                                                      \
  armlet_vm_type *name __attribute__((cleanup(armlet_cleanup_type)))

#define ARMLET_INCREF(name) ((name)->ref_count += 1, (name))

#define ARMLET_DECREF(name) ((name)->ref_count -= 1, (name))

#define ARMLET_INCREF_VAR(name)                                                \
  (((name)->ref_count += 1,                                                    \
    ((name)->type != NULL ? ((name)->type->ref_count += 1, NULL) : NULL)),     \
   (name))

#define ARMLET_REFCOUNTED armlet_refcount ref_count

typedef struct armlet_vm_type {
  char *name;
  enum armlet_vm_var_tag tag;

  struct {
    size_t size;

    struct {
      size_t start;
      size_t end;
    };
  };

  armlet_vm_type *inner;

  ARMLET_REFCOUNTED;
} armlet_vm_type;

typedef struct {
  armlet_vm_type *type;
  char *name;
  uint64_t value;
} armlet_vm_enum_element;

typedef struct armlet_vm_instance armlet_vm_instance;
typedef struct armlet_vm_namespace armlet_vm_namespace;
typedef struct armlet_vm_custom_type armlet_vm_custom_type;
typedef struct armlet_vm_var armlet_vm_var;
typedef struct armlet_vm_function armlet_vm_function;
typedef struct armlet_vm_context armlet_vm_context;

typedef void (*armlet_builtin)(armlet_vm_context *, armlet_ast_node *);

typedef struct {
  armlet_ast_node *from;
  armlet_ast_node *to;
} armlet_vm_type_alias;

typedef struct {
  Hashtable *ranges;
} armlet_vm_named_bits;

typedef struct armlet_vm_var {
  armlet_vm_type *type;

  union {
    mpz_t integer;
    float real;

    struct {
      uint8_t *bits;
      armlet_vm_named_bits *named_bits;
    };

    bool boolean;
    void *context;
    armlet_builtin builtin;
    char *string;
    struct armlet_vm_instance *instance;
    struct armlet_vm_namespace *namespace;
    struct armlet_vm_custom_type *custom_type;
    struct armlet_vm_function *function;
    armlet_vm_enum_element *enum_value;
    armlet_vm_type_alias *type_alias;
    armlet_ast_bitlayout *bitlayout;

    struct {
      DEFINE_ARRAY(armlet_vm_var *, contents);
      armlet_vm_type *contents_type;
      size_t contents_base;
    };

    struct {
      size_t range_start;
      size_t range_end;
    };
  };

  // Can't be mutated
  bool is_const : 1;

  // Unset varable
  bool is_unset : 1;

  // Reference to a chunk of bits via named_bits
  bool is_bits_ref : 1;
  uint8_t *bits_ref_target;

  ARMLET_REFCOUNTED;
} armlet_vm_var;

typedef struct armlet_vm_function {
  char *name;
  char *trace_info;
  armlet_ast_callable_definition *def;
  DEFINE_ARRAY(char *, parameter_type_names);
} armlet_vm_function;

typedef struct {
  DEFINE_ARRAY(armlet_vm_var *, evaluated);
} armlet_vm_callable_search;

typedef struct {
  Hashtable *symbols;
} armlet_vm_symbols;

typedef struct {
  char *name;
  armlet_vm_type *type;
} armlet_vm_parameter;

typedef struct {
  char *name;
  armlet_vm_var *var;
} armlet_vm_var_named;

typedef struct armlet_vm_custom_type {
  armlet_vm_type *type;
  char *name;

  DEFINE_ARRAY(armlet_vm_parameter *, fields);
} armlet_vm_custom_type;

typedef struct armlet_vm_instance {
  armlet_vm_custom_type *class;

  Hashtable *fields;
} armlet_vm_instance;

typedef struct {
  char *context;
  armlet_vm_symbols *symbols;
} armlet_vm_frame;

typedef struct armlet_vm_namespace {
  char *name;
  armlet_vm_symbols *symbols;
} armlet_vm_namespace;

typedef struct {
  DEFINE_ARRAY(armlet_vm_var_named *, items);
} armlet_vm_named_array;

typedef struct {
  armlet_vm_var **stack;
  size_t capacity;
  size_t tos;
} armlet_vm_stack;

enum armlet_var_init_semantics {
  INIT_ZERO,
  INIT_SOURCE,
};

typedef struct {
  Hashtable *values;
  FILE *file;
} armlet_implementation_defined;

typedef struct {
  bool debug;
  bool strict;
  FILE *initializer;
  armlet_implementation_defined implementation_defined;
} armlet_vm_runtime_config;

typedef void (*armlet_debugger_hook_fn)(struct armlet_vm_context *vm,
                                       armlet_ast_node *n, void *userdata);

typedef void (*armlet_debugger_lazy_init_fn)(struct armlet_vm_context *vm,
                                             void *userdata);

typedef struct armlet_vm_context {
  // armlet_ast_node *current_node;
  armlet_ast_node *returned;
  armlet_vm_stack *stack;
  armlet_vm_runtime_config config;
  armlet_decoder_builder *decoder_builder;
  armlet_decoder *decoder;
  armlet_debugger_hook_fn debugger_hook;
  armlet_debugger_lazy_init_fn debugger_lazy_init;
  void *debugger_lazy_init_userdata;
  void *debugger_userdata;
  Hashtable *imported_files;
  bool break_requested;
  size_t eval_depth;

  DEFINE_ARRAY(armlet_vm_frame *, frames);
} armlet_vm_context;

typedef struct {
  DEFINE_ARRAY(size_t, indices);
} armlet_index_list;

enum armlet_vm_symbol_add_semantics {
  SEM_SYMBOL_VALUE,
  SEM_SYMBOL_POLYMORPHIC,
};

enum armlet_dereference_semantics {
  SEM_INVALID = 0,
  SEM_GET = 1,
  SEM_CREATE = 2,
  SEM_GET_OR_CREATE = 3,
  SEM_POLYMORPHIC = 4,
};

typedef enum {
  ORDER_LT = -1,
  ORDER_EQ = 0,
  ORDER_GT = 1,
  ORDER_UNORDERED = 2,
  ORDER_UNSUPPORTED = 3,
} armlet_vm_ordering;

enum armlet_callable_preference {
  CALLPREF_SETTER,
  CALLPREF_GETTER,
  CALLPREF_ANY
};

#define STACK_DEFAULT_CAPACITY 4096

armlet_vm_var *armlet_vm_var_new(armlet_vm_type *);
void armlet_vm_var_release(armlet_vm_var *);
void armlet_vm_type_release(armlet_vm_type *);

armlet_vm_var_named *armlet_vm_named_from_var(armlet_vm_var *, const char *);
armlet_vm_var *armlet_var_from_named(armlet_vm_var_named *);

armlet_vm_type *armlet_vm_make_custom_type(char *, size_t);
armlet_vm_type *armlet_vm_make_enum_type(char *, size_t);
armlet_vm_type *armlet_vm_make_enum_instance_type(armlet_vm_type *, size_t);

armlet_vm_context *armlet_vm_init(armlet_source *source);
armlet_vm_frame *armlet_vm_run(armlet_vm_context *vm, armlet_ast_node *n);

armlet_vm_named_array *armlet_vm_symbol_resolve(armlet_vm_symbols *s,
                                                const char *n);

armlet_vm_named_array *
armlet_vm_resolve_var(armlet_vm_context *vm, armlet_ast_value *v,
                      enum armlet_dereference_semantics sem,
                      armlet_ast_node *n, bool null_on_error);

void armlet_vm_polymorphic_error(armlet_vm_context *vm,
                                 armlet_ast_value *callable_name,
                                 armlet_vm_named_array *na,
                                 armlet_vm_callable_search *search,
                                 armlet_ast_node *n);

void armlet_vm_init_builtins(armlet_vm_context *vm);

char *armlet_vm_var_to_string(armlet_vm_var *, bool);

bool armlet_apply_op_from_ordering(armlet_vm_ordering ord,
                                   enum armlet_vm_comparison op);
#endif
