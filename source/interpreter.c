#include "interpreter.h"
#include "ast.h"
#include "bitlayout_decoder.h"
#include "diagnostics.h"
#include "parser.h"
#include "serialization.h"
#include "utils/common.h"
#include "utils/fort.h"
#include "utils/hashtable.h"
#include "utils/string.h"

#include <gmp.h>
#include <libgen.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define SAFE_MPZ_GET_UI(mpz)                                                   \
  ({                                                                           \
    if (!mpz_fits_ulong_p(mpz))                                                \
      BAIL("Integer too large for conversion at %s:%d\n", __FILE__, __LINE__); \
    mpz_get_ui(mpz);                                                           \
  })

const char *armlet_vm_var_tag_names[] = {
    PASTE(T_INVALID),     PASTE(T_INTEGER),
    PASTE(T_REAL),        PASTE(T_BOOLEAN),
    PASTE(T_BITS),        PASTE(T_TYPE),
    PASTE(T_ENUMERATION), PASTE(T_ARRAY),
    PASTE(T_SET),         PASTE(T_INSTANCE),
    PASTE(T_RANGE),       PASTE(T_FUNCTION),
    PASTE(T_SCOPE),       PASTE(T_ENUMERATION_TYPE),
    PASTE(T_TUPLE),       PASTE(T_GETTER),
    PASTE(T_SETTER),      PASTE(T_BUILTIN),
    PASTE(T_STRING),      PASTE(T_TYPE_ALIAS),
    PASTE(T_BITLAYOUT)};

const char *armlet_vm_binop_names[] = {
    PASTE(BINOP_ADD),        PASTE(BINOP_SUB), PASTE(BINOP_DIV),
    PASTE(BINOP_FDIV),       PASTE(BINOP_MUL), PASTE(BINOP_CONCAT),
    PASTE(BINOP_OFF_CONCAT), PASTE(BINOP_PWR), PASTE(BINOP_SHR),
    PASTE(BINOP_SHL),        PASTE(BINOP_EOR), PASTE(BINOP_AND),
    PASTE(BINOP_OR),         PASTE(BINOP_MOD),
};

const armlet_vm_var_tag_mask armlet_vm_var_tag_bits[] = {
    PASTE_BM(T_INVALID),     PASTE_BM(T_INTEGER),
    PASTE_BM(T_REAL),        PASTE_BM(T_BOOLEAN),
    PASTE_BM(T_BITS),        PASTE_BM(T_TYPE),
    PASTE_BM(T_ENUMERATION), PASTE_BM(T_ARRAY),
    PASTE_BM(T_SET),         PASTE_BM(T_INSTANCE),
    PASTE_BM(T_RANGE),       PASTE_BM(T_FUNCTION),
    PASTE_BM(T_SCOPE),       PASTE_BM(T_ENUMERATION_TYPE),
    PASTE_BM(T_TUPLE),       PASTE_BM(T_GETTER),
    PASTE_BM(T_SETTER),      PASTE_BM(T_BUILTIN),
    PASTE_BM(T_STRING),      PASTE_BM(T_TYPE_ALIAS),
    PASTE_BM(T_BITLAYOUT)};

#define TAG_MASK1(X) ((armlet_vm_var_tag_mask)1 << (X))
#define TAG_MASK2(X, Y) (TAG_MASK1(X) | TAG_MASK1(Y))
#define TAG_MASK3(X, Y, Z) (TAG_MASK2(X, Y) | TAG_MASK1(Z))
#define TAG_MASK4(X, Y, Z, W) (TAG_MASK3(Y, Z, W) | TAG_MASK1(X))

static inline bool armlet_vm_var_tag_mask_check(armlet_vm_var_tag_mask mask,
                                                enum armlet_vm_var_tag tag) {
  return (mask & TAG_MASK1(tag)) != 0;
}

const char *armlet_vm_var_type_name(const armlet_vm_var *v) {
  if (v->type == NULL) {
    return "<untyped>";
  }
  return armlet_vm_var_tag_names[v->type->tag];
}

void armlet_vm_type_release(armlet_vm_type *t) {
  if (t != NULL && --t->ref_count == 0) {
    free(t->name);
    if (t->inner) {
      armlet_vm_type_release(t->inner);
    }
    free(t);
  }
}

void armlet_vm_var_release(armlet_vm_var *v) {
  if (--v->ref_count == 0) {
    if (v->type != NULL) {
      switch (v->type->tag) {
      case T_INVALID:
      case T_REAL:
      case T_ENUMERATION_TYPE:
      case T_RANGE:
      case T_FUNCTION:
      case T_GETTER:
      case T_SETTER:
      case T_BUILTIN:
      case T_TYPE_ALIAS:
      case T_BITLAYOUT:
      case T_STRING:
      case T_BOOLEAN: {
        break;
      }
      case T_INTEGER: {
        mpz_clear(v->integer);
        break;
      }
      case T_BITS: {
        free(v->bits);
        if (v->named_bits) {
          HASHTABLE_ITERATE(v->named_bits->ranges, char *, armlet_span *, {
            free(key);
            free(value);
          });
          hashtable_unref(v->named_bits->ranges);
          free(v->named_bits);
        }
        break;
      }
      case T_TYPE: {
        armlet_vm_custom_type *t = v->context;

        armlet_vm_type_release(t->type);

        ARR_FOREACH(t, fields, {
          free(it->name);
          armlet_vm_type_release(it->type);
        });

        free(t->fields);
        free(t->name);
        free(t);
        break;
      }
      case T_INSTANCE: {
        armlet_vm_instance *i = v->instance;
        if (i != NULL) {
          HASHTABLE_ITERATE(i->fields, char *, armlet_vm_var_named *, {
            free(key);
            armlet_vm_var_release(value->var);
          });
          hashtable_unref(i->fields);
          free(i);
        }
        break;
      }
      case T_ENUMERATION: {
        armlet_vm_enum_element *e = v->enum_value;
        armlet_vm_type_release(e->type);
        break;
      }
      case T_SET:
      case T_TUPLE:
      case T_ARRAY: {
        ARR_FOREACH(v, contents, { armlet_vm_var_release(it); });
        armlet_vm_type_release(v->contents_type);
        break;
      }
      case T_SCOPE: {
        armlet_vm_namespace *ns = v->namespace;
        Hashtable *s = ns->symbols->symbols;

        HASHTABLE_ITERATE(s, char *, armlet_vm_named_array *, {
          free(key);

          ARR_FOREACH(value, items, {
            armlet_vm_var_release(it->var);
            free(it);
          });

          free(value->items);
          free(value);
        });

        free(s);
        free(ns->symbols);
        free(ns);
        break;
      }
      }
      armlet_vm_type_release(v->type);
    }
    free(v);
  }
}

void cleanup_mpz(mpz_t *v) { mpz_clear(*v); }

void cleanup_free(void *o) { free(*(void **)o); }

static inline void armlet_cleanup_var(armlet_vm_var **vp) {
  if (vp != NULL && *vp != NULL) {
    armlet_vm_var_release(*vp);
  }
}

static inline void armlet_cleanup_type(armlet_vm_type **tp) {
  if (tp != NULL && *tp != NULL) {
    armlet_vm_type_release(*tp);
  }
}

void armlet_vm_backtrace(armlet_vm_context *vm) {
  puts("Backtrace:");
  ARR_FOREACH_REVERSE(
      vm, frames, { printf(" frame(%zu:%p): %s\n", i - 1, it, it->context); });
}

#define armlet_runtime_error(vm, src, fmt, ...)                                \
  armlet_runtime_error_impl((vm), (src), __FILE__, __LINE__, (fmt),            \
                            ##__VA_ARGS__)

void armlet_runtime_error_impl(armlet_vm_context *vm, armlet_node_source *src,
                               const char *fn, unsigned line, const char *fmt,
                               ...) {
  armlet_vm_backtrace(vm);
  puts("");
  va_list args;
  va_start(args, fmt);
  const char *format = NULL;

  if (vm->config.debug) {
    format = s_sprintf("%s [%s:%u]", fmt, fn, line);
  } else {
    format = fmt;
  }

  armlet_source_error_v(&src->source, src->span, format, args);

  if (vm->config.debug) {
    free((void *)format);
  }

  va_end(args);
}

armlet_vm_symbols *armlet_vm_init_symbols() {
  armlet_vm_symbols *symbols = NEW0(armlet_vm_symbols);
  hashtable_new(64, &symbols->symbols);
  return symbols;
}

static inline bool armlet_vm_var_is_one_of_type(armlet_vm_var *v,
                                                armlet_vm_var_tag_mask mask) {
  if (v->type == NULL) {
    return false;
  }
  armlet_vm_var_tag_mask tag = armlet_vm_var_tag_bits[v->type->tag];
  return (tag & mask) != 0;
}

static inline bool armlet_vm_var_is_type(armlet_vm_var *v,
                                         enum armlet_vm_var_tag t) {
  return v->type != NULL && v->type->tag == t;
}

armlet_vm_stack *armlet_vm_stack_init(size_t capacity) {
  armlet_vm_stack *s = NEW0(armlet_vm_stack);
  s->capacity = capacity;
  s->tos = 0;
  s->stack = calloc(capacity, sizeof(s->stack));
  if (s->stack == NULL)
    BAIL("Out of memory: %s:%d\n", __FILE__, __LINE__);
  return s;
}

armlet_vm_frame *armlet_vm_frame_init(char *name) {
  armlet_vm_frame *f = NEW0(armlet_vm_frame);
  f->context = name;
  f->symbols = armlet_vm_init_symbols();
  return f;
}

void armlet_vm_frame_free(armlet_vm_frame *frame) {
  hashtable_unref(frame->symbols->symbols);
  free(frame->symbols);
  free(frame->context);
  free(frame);
}

size_t armlet_vm_stack_tos(armlet_vm_stack *stack) { return stack->tos; }

void armlet_vm_stack_set_tos(armlet_vm_stack *stack, size_t tos) {
  assert(tos < stack->capacity);
  stack->tos = tos;
}

armlet_vm_var *armlet_vm_stack_pop(armlet_vm_stack *stack) {
  assert(stack->tos > 0);
  armlet_vm_var *v = stack->stack[--stack->tos];
  return v;
}

void armlet_vm_stack_push(armlet_vm_stack *stack, armlet_vm_var *v) {
  if (stack->capacity <= (stack->tos + 1)) {
    stack->capacity *= 2;
    stack->stack =
        CHECKED_REALLOC(stack->stack, stack->capacity, sizeof(*stack->stack));
  }

  stack->stack[stack->tos++] = v;
}

armlet_vm_var *armlet_vm_peek_stack(armlet_vm_context *vm) {
  assert(vm);
  return vm->stack->stack[armlet_vm_stack_tos(vm->stack) - 1];
}

armlet_vm_context *armlet_vm_init(armlet_source *source) {
  assert(source);

  armlet_vm_context *vm = NEW0(armlet_vm_context);

  vm->stack = armlet_vm_stack_init(STACK_DEFAULT_CAPACITY);
  ARR_APPEND(vm, frames, armlet_vm_frame_init(strdup(source->file)));
  hashtable_new(64, &vm->imported_files);
  vm->decoder_builder = armlet_decoder_builder_new();

  return vm;
}

armlet_vm_frame *armlet_vm_current_frame(armlet_vm_context *vm) {
  assert(vm);
  return vm->frames[vm->num_frames - 1];
}

armlet_vm_frame *armlet_vm_run(armlet_vm_context *vm, armlet_ast_node *n);

void armlet_vm_push_frame(armlet_vm_context *vm, char *name) {
  assert(vm);
  ARR_APPEND(vm, frames, armlet_vm_frame_init(name));
  armlet_vm_current_frame(vm)->context = name;
}

void armlet_vm_pop_frame(armlet_vm_context *vm) {
  assert(vm);
  armlet_vm_frame_free(armlet_vm_current_frame(vm));
  vm->num_frames -= 1;
}

void armlet_vm_eval_frame(armlet_vm_context *vm, armlet_ast_node *n,
                          bool current_frame) {
  assert(vm);
  assert(n);

  if (!current_frame) {
    armlet_vm_push_frame(vm, strdup(armlet_ast_node_type_names[n->type]));
  }

  armlet_vm_run(vm, n);

  if (!current_frame) {
    armlet_vm_pop_frame(vm);
  }
}

void armlet_vm_eval(armlet_vm_context *vm, armlet_ast_node *n) {
  assert(vm);
  assert(n);

  armlet_vm_eval_frame(vm, n, true);
}

armlet_vm_var *armlet_vm_eval_value(armlet_vm_context *vm, armlet_ast_node *n) {
  assert(vm);
  assert(n);

  armlet_vm_eval(vm, n);
  return armlet_vm_stack_pop(vm->stack);
}

armlet_vm_named_array *armlet_vm_symbol_resolve(armlet_vm_symbols *s,
                                                const char *n) {
  assert(s);
  assert(n);

  armlet_vm_named_array *st = NULL;

  (void)hashtable_find_str(s->symbols, n, (void **)&st);

  return st;
}

armlet_vm_named_array *
armlet_vm_add_symbol(armlet_vm_context *vm, armlet_ast_node *n,
                     armlet_vm_symbols *s, armlet_vm_var_named *v,
                     enum armlet_vm_symbol_add_semantics sem) {
  assert(vm);
  assert(n);
  assert(s);
  assert(v);

  int r = 0;

  armlet_vm_named_array *st = NULL;

  r = hashtable_find_str(s->symbols, v->name, (void **)&st);
  if (r == -ENOENT) {
    st = NEW0(armlet_vm_named_array);
    hashtable_add_str(s->symbols, v->name, st);
  } else {
    if (sem == SEM_SYMBOL_VALUE) {
      armlet_runtime_error(vm, n->source, "Symbol '%s' already exists",
                           v->name);
    }
  }

  ARR_APPEND(st, items, v);

  return st;
}

armlet_vm_named_array *armlet_vm_get_local(armlet_vm_context *vm,
                                           const char *name) {
  assert(vm);
  assert(name);

  armlet_vm_frame *f = armlet_vm_current_frame(vm);
  armlet_vm_named_array *symbols = armlet_vm_symbol_resolve(f->symbols, name);

  return symbols;
}

armlet_vm_named_array *armlet_vm_get_named(armlet_vm_context *vm,
                                           const char *name) {
  assert(vm);
  assert(name);

  armlet_vm_frame *globals = vm->frames[0];
  armlet_vm_frame *locals = armlet_vm_current_frame(vm);

  armlet_vm_named_array *na = armlet_vm_symbol_resolve(locals->symbols, name);
  if (na == NULL && globals != locals) {
    return armlet_vm_symbol_resolve(globals->symbols, name);
  }

  return na;
}

armlet_vm_var_named *armlet_vm_get_var_named(armlet_vm_context *vm,
                                             const char *name) {
  assert(vm);
  assert(name);

  armlet_vm_named_array *named = armlet_vm_get_named(vm, name);

  if (named != NULL) {
    // Variables are not polymoprhic, so there can be only one inside the named
    // array
    if (named->num_items == 1) {
      return named->items[0];
    }
  }

  return NULL;
}

armlet_vm_var *armlet_vm_get_var(armlet_vm_context *vm, const char *name) {
  assert(vm);
  assert(name);

  armlet_vm_var_named *named = armlet_vm_get_var_named(vm, name);

  if (named != NULL) {
    return named->var;
  }

  return NULL;
}

armlet_vm_namespace *armlet_vm_get_namespace(armlet_vm_context *vm,
                                             const char *name) {
  assert(vm);
  assert(name);

  armlet_vm_var *v = armlet_vm_get_var(vm, name);

  if (v != NULL && armlet_vm_var_is_type(v, T_SCOPE))
    return v->namespace;

  return NULL;
}

armlet_vm_var_named *armlet_vm_var_from_instance(armlet_vm_instance *instance,
                                                 const char *name) {
  armlet_vm_var_named *var = NULL;

  (void)hashtable_find_str(instance->fields, name, (void **)&var);

  return var;
}

armlet_vm_named_array *armlet_vm_search_namespace(armlet_vm_namespace *ns,
                                                  const char *name) {
  return armlet_vm_symbol_resolve(ns->symbols, name);
}

void armlet_vm_set_scoped(armlet_vm_context *vm, armlet_ast_node *n,
                          armlet_vm_namespace *ns, armlet_vm_var_named *named,
                          enum armlet_vm_symbol_add_semantics sem) {
  armlet_vm_add_symbol(vm, n, ns->symbols, named, sem);
}

armlet_vm_var_named *armlet_vm_named_from_var(armlet_vm_var *var,
                                              const char *name) {
  armlet_vm_var_named *v = NEW0(armlet_vm_var_named);
  v->name = strdup(name);
  v->var = ARMLET_INCREF(var);
  return v;
}

void armlet_bitselect_add_index(armlet_index_list *list, size_t start,
                                size_t end) {
  if (start == end) {
    ARR_APPEND(list, indices, start);
  } else {
    for (size_t j = start + 1; j > end; --j) {
      ARR_APPEND(list, indices, j - 1);
    }
  }
}

armlet_index_list armlet_bitselect_indices(armlet_vm_context *vm,
                                           armlet_ast_bitselect *bs) {
  armlet_index_list list = {};

  ARR_FOREACH(bs, selections, {
    ARMLET_VAR(v) = armlet_vm_eval_value(vm, it);

    if (armlet_vm_var_is_type(v, T_INTEGER)) {
      uint64_t n = SAFE_MPZ_GET_UI(v->integer);
      armlet_bitselect_add_index(&list, n, n);
    } else if (armlet_vm_var_is_type(v, T_RANGE)) {
      armlet_bitselect_add_index(&list, v->range_start, v->range_end);
    } else {
      armlet_runtime_error(vm, it->source, "Invalid bitselect element: %s",
                           v ? v->type->name : "NULL");
    }
  });

  return list;
}

void armlet_vm_type_check(armlet_vm_context *vm, armlet_vm_type *a,
                          armlet_vm_type *b, armlet_ast_node *n) {
  if (a->tag != b->tag) {
    armlet_source_error_n(
        n, "Assigning to a variable of type '%s' a value of type '%s'",
        armlet_vm_var_tag_names[a->tag], armlet_vm_var_tag_names[b->tag]);
  }

  if (a->tag == T_BITS) {
    size_t size_left = a->size;
    size_t size_right = b->size;

    if (size_left != size_right) {
      armlet_source_error_n(
          n, "Assignment to a variable of size %zu a value of size %zu",
          size_left, size_right);
    }
  }

  if (a->tag == T_INSTANCE) {
    if (!streq(a->name, b->name)) {
      armlet_source_error_n(
          n,
          "Assignment to a variable instance of type '%s' an instance "
          "of '%s'",
          a->name, b->name);
    }
  }
}

void armlet_vm_type_check_vars(armlet_vm_context *vm, armlet_vm_var *a,
                               armlet_vm_var *b, armlet_ast_node *n) {
  armlet_vm_type_check(vm, a->type, b->type, n);
}

armlet_vm_named_array *armlet_vm_set_local(armlet_vm_context *vm,
                                           armlet_vm_var_named *var,
                                           armlet_ast_node *n) {
  armlet_vm_named_array *existing = armlet_vm_get_local(vm, var->name);
  armlet_vm_var *v = ARMLET_INCREF(var->var);

  if (existing != NULL) {
    if (existing->num_items != 1) {
      armlet_runtime_error(vm, n->source,
                           "Sanity check variable couldn't be set over "
                           "a polymorphic name '%s'",
                           var->name);
    }

    armlet_vm_var_named *nv = existing->items[0];

    if (v->type) {
      armlet_vm_type_check_vars(vm, v, nv->var, n);
    }

    nv->var = v;

    return existing;
  } else {
    armlet_vm_frame *f = armlet_vm_current_frame(vm);
    return armlet_vm_add_symbol(vm, n, f->symbols, var, SEM_SYMBOL_VALUE);
  }
}

armlet_op_mapping TYPE_MAPPING[] = {{.m = "integer", .s = T_INTEGER},
                                    {.m = "bits", .s = T_BITS},
                                    {.m = "bit", .s = T_BITS},
                                    {.m = "real", .s = T_REAL},
                                    {.m = "boolean", .s = T_BOOLEAN}};

armlet_vm_type TYPE_GETTER = {
    .name = "property getter",
    .size = 0,
    .tag = T_GETTER,
    .ref_count = ARMLET_REFCOUNT_STATIC,
};

armlet_vm_type TYPE_SETTER = {
    .name = "property setter",
    .size = 0,
    .tag = T_SETTER,
    .ref_count = ARMLET_REFCOUNT_STATIC,
};

armlet_vm_type TYPE_INTEGER = {
    .name = "integer",
    .size = 0,
    .tag = T_INTEGER,
    .ref_count = ARMLET_REFCOUNT_STATIC,
};

armlet_vm_type TYPE_REAL = {
    .name = "real",
    .size = 0,
    .tag = T_REAL,
    .ref_count = ARMLET_REFCOUNT_STATIC,
};

armlet_vm_type TYPE_BOOLEAN = {
    .name = "boolean",
    .size = 0,
    .tag = T_BOOLEAN,
    .ref_count = ARMLET_REFCOUNT_STATIC,
};

armlet_vm_type TYPE_BIT = {
    .name = "bit",
    .size = 1,
    .tag = T_BITS,
    .ref_count = ARMLET_REFCOUNT_STATIC,
};

armlet_vm_type *DEFINED_SIMPLE_TYPES[] = {
    &TYPE_BOOLEAN,
    &TYPE_INTEGER,
    &TYPE_REAL,
    &TYPE_BIT,
};

armlet_vm_type *armlet_vm_make_bitstring_type(size_t num_bits) {
  armlet_vm_type *type = NEW0(armlet_vm_type);

  type->name = strdup("bits");
  type->tag = T_BITS;
  type->size = num_bits;
  type->ref_count = 1;

  return type;
}

armlet_vm_type *armlet_vm_make_range_type(size_t start, size_t end) {
  armlet_vm_type *type = NEW0(armlet_vm_type);

  type->name = strdup("range");
  type->tag = T_RANGE;
  type->size = end - start;
  type->ref_count = 1;

  return type;
}

armlet_vm_type *armlet_vm_make_custom_type(char *name, size_t num_fields) {
  armlet_vm_type *type = NEW0(armlet_vm_type);

  type->name = strdup(name);
  type->tag = T_TYPE;
  type->size = num_fields;
  type->ref_count = 1;

  return type;
}

armlet_vm_type *armlet_vm_make_enum_type(char *name, size_t num_fields) {
  armlet_vm_type *type = NEW0(armlet_vm_type);

  type->name = strdup(name);
  type->tag = T_ENUMERATION_TYPE;
  type->size = num_fields;
  type->ref_count = 1;

  return type;
}

armlet_vm_type *armlet_vm_make_instance_type(armlet_vm_custom_type *ct) {
  armlet_vm_type *type = NEW0(armlet_vm_type);

  type->tag = T_INSTANCE;
  type->name = strdup(ct->name);
  type->size = ct->num_fields;
  type->ref_count = 1;

  return type;
}

armlet_vm_type *armlet_vm_make_enum_instance_type(armlet_vm_type *t, size_t f) {
  armlet_vm_type *type = NEW0(armlet_vm_type);

  type->tag = T_ENUMERATION;
  type->name = strdup(t->name);
  type->size = f;
  type->ref_count = 1;

  return type;
}

armlet_vm_var *armlet_vm_var_new(armlet_vm_type *t) {
  armlet_vm_var *v = NEW0(armlet_vm_var);
  v->ref_count = 1;
  v->type = t;
  if (t != NULL) {
    (void)ARMLET_INCREF(t);
  }
  return v;
}

armlet_vm_type *armlet_vm_type_new(enum armlet_vm_var_tag tag, char *name,
                                   size_t size) {
  armlet_vm_type *t = NEW0(armlet_vm_type);
  t->tag = tag;
  t->name = strdup(name);
  t->size = size;
  t->ref_count = 1;
  return t;
}

char *armlet_integer_to_bistring(uint64_t val, bool msb_first) {
  size_t bits = sizeof(val) * 8;
  char *min = CHECKED_MALLOC((size_t)bits + 1);
  for (size_t i = 0; i < bits; ++i) {
    int overall_bit_idx;
    if (msb_first) {
      overall_bit_idx = bits - 1 - i;
    } else {
      overall_bit_idx = i;
    }
    int bit = (int)((val >> overall_bit_idx) & 1ULL);
    min[i] = bit ? '1' : '0';
  }
  min[bits] = '\0';
  return min;
}

bool check_flag(int flags, int flag) { return (flags & flag) == flag; }

// Weak cleanup of named array only removes the wrapping array
// without doing anything about it's *individual* items
inline void cleanup_named_weak(void *o) {
  if (o == NULL) {
    return;
  }

  armlet_vm_named_array *na = *(armlet_vm_named_array **)o;
  if (na != NULL) {
    free(na->items);
    free(na);
  }
}

void armlet_vm_named_array_release(armlet_vm_named_array *va) {
  if (va != NULL) {
    ARR_FOREACH(va, items, {
      armlet_vm_var_release(it->var);
      free(it->name);
      free(it);
    });
    free(va->items);
    free(va);
  }
}

armlet_vm_named_array *armlet_vm_named_array_single(armlet_vm_var_named *vn) {
  armlet_vm_named_array *na = NEW0(armlet_vm_named_array);
  ARR_APPEND(na, items, vn);
  return na;
}

armlet_vm_var_named *armlet_vm_named_single(armlet_vm_named_array *na) {
  if (na != NULL && na->num_items == 1) {
    return na->items[0];
  }
  return NULL;
}

armlet_vm_var *armlet_bits_var_from_span(armlet_vm_context *vm,
                                         armlet_ast_node *n,
                                         armlet_vm_var *source,
                                         armlet_span *span) {
  if (span->end > span->start) {
    armlet_runtime_error(vm, n->source,
                         "Invalid span: end (%u) > start (%u)", span->end,
                         span->start);
  }

  if (span->start >= source->type->size) {
    armlet_runtime_error(vm, n->source,
                         "Span start (%u) exceeds source size (%zu)",
                         span->start, source->type->size);
  }

  size_t r = (span->start - span->end) + 1;
  size_t s_size = source->type->size - 1;

  armlet_vm_var *rv = armlet_vm_var_new(armlet_vm_make_bitstring_type(r));

  rv->bits = calloc(1, r + 1);
  if (rv->bits == NULL) {
    armlet_runtime_error(vm, n->source, "Out of memory");
  }

  size_t c = 0;

  for (size_t j = span->start + 1; j > span->end; --j) {
    rv->bits[c++] = source->bits[s_size - (j - 1)];
  }

  rv->is_bits_ref = true;
  rv->bits_ref_target = source->bits + (s_size - span->start);

  return rv;
}

armlet_vm_var *armlet_vm_make_namespace(char *name) {
  armlet_vm_var *scope =
      armlet_vm_var_new(armlet_vm_type_new(T_SCOPE, name, 0));

  scope->namespace = NEW0(armlet_vm_namespace);
  scope->namespace->name = name;
  scope->namespace->symbols = armlet_vm_init_symbols();

  return scope;
}

armlet_vm_named_array *
armlet_vm_dereference(armlet_vm_context *vm, const armlet_dereference *deref,
                      enum armlet_dereference_semantics ns_deref,
                      armlet_ast_node *n, bool null_on_error) {
  enum dereference {
    DEREF_INVALID = 0,
    DEREF_LOCAL = 1,
    DEREF_INSTANCE = 2,
    DEREF_NAMESPACE = 4,
    DEREF_NAMESPACE_DELVE = 8,
    DEREF_BITS = 16,
  };

  void *context = NULL;
  enum dereference state = DEREF_LOCAL | DEREF_NAMESPACE;
  assert(deref->num_names > 0);
  const char *last = deref->names[deref->num_names - 1];

#define CONTINUE_STATE(C, S)                                                   \
  context = (C);                                                               \
  state = (S);                                                                 \
  continue;

  ARR_FOREACH(deref, names, {
    bool is_last = (it == last);

    if (check_flag(state, DEREF_LOCAL)) {
      armlet_vm_var *v = armlet_vm_get_var(vm, it);

      if (v != NULL) {
        if (!armlet_vm_var_is_type(v, T_SCOPE)) {
          if (armlet_vm_var_is_type(v, T_BITS) && !is_last) {
            CONTINUE_STATE(v, DEREF_BITS);
          } else if (!armlet_vm_var_is_type(v, T_INSTANCE)) {
            if (null_on_error)
              return NULL;
            armlet_runtime_error(vm, n->source,
                                 "Dereference expected 'T_INSTANCE', got '%s'",
                                 armlet_vm_var_type_name(v));
          }

          CONTINUE_STATE(v->instance, DEREF_INSTANCE);
        }
      }
    }

    if (check_flag(state, DEREF_BITS)) {
      if (!is_last) {
        if (null_on_error)
          return NULL;
        armlet_runtime_error(
            vm, n->source, "Dereference into named bits with remaining parts");
      }

      armlet_vm_var *source = (armlet_vm_var *)context;
      armlet_vm_named_bits *nb = source->named_bits;

      if (nb != NULL) {
        armlet_span *span = NULL;
        if (hashtable_find_str(nb->ranges, it, (void **)&span) == 0) {
          armlet_vm_var *rv = armlet_bits_var_from_span(vm, n, source, span);
          return armlet_vm_named_array_single(armlet_vm_named_from_var(rv, it));
        } else {
          if (null_on_error)
            return NULL;
          armlet_runtime_error(vm, n->source, "Named bits field '%s' not found",
                               it);
        }
      } else {
        if (null_on_error)
          return NULL;
        armlet_runtime_error(vm, n->source,
                             "Dereference of T_BITS without named bits");
      }
    }

    if (check_flag(state, DEREF_INSTANCE)) {
      armlet_vm_instance *instance = (armlet_vm_instance *)context;
      armlet_vm_var_named *v = armlet_vm_var_from_instance(instance, it);

      if (v == NULL) {
        if (null_on_error)
          return NULL;
        armlet_runtime_error(vm, n->source, "Unknown field '%s' for type '%s'",
                             it, instance->class->name);
      }

      if (is_last) {
        return armlet_vm_named_array_single(v);
      } else {
        // not last item, and current var is an instance, so we delve deeper
        if (armlet_vm_var_is_type(v->var, T_INSTANCE)) {
          CONTINUE_STATE(v->var->instance, DEREF_INSTANCE);
        } else if (armlet_vm_var_is_type(v->var, T_BITS)) {
          CONTINUE_STATE(v->var, DEREF_BITS);
        } else {
          // if the current var is not an object instance, there's nothing to
          // dereference into
          if (null_on_error)
            return NULL;
          armlet_runtime_error(vm, n->source,
                               "Attempt to dereference a non-object "
                               "field '%s' of type '%s'",
                               it, instance->class->name);
        }
      }
    }

    if (check_flag(state, DEREF_NAMESPACE)) {
      armlet_vm_namespace *ns = armlet_vm_get_namespace(vm, it);
      if (!ns) {
        if (ns_deref == SEM_GET_OR_CREATE || ns_deref == SEM_CREATE) {
          armlet_vm_var *v = armlet_vm_make_namespace(it);
          ns = v->namespace;
          armlet_vm_set_local(vm, armlet_vm_named_from_var(v, it), n);

          if (is_last) {
            return armlet_vm_named_array_single(
                armlet_vm_named_from_var(v, it));
          } else {
            CONTINUE_STATE(ns, DEREF_NAMESPACE_DELVE);
          }
        } else {
          if (null_on_error)
            return NULL;
          armlet_runtime_error(vm, n->source,
                               "Unable to find '%s' in current scope", it);
        }
      } else {
        CONTINUE_STATE(ns, DEREF_NAMESPACE_DELVE);
      }
    }

    if (check_flag(state, DEREF_NAMESPACE_DELVE)) {
      armlet_vm_namespace *ns = (armlet_vm_namespace *)context;
      armlet_vm_var_named *nv = NULL;

      if (is_last && (ns_deref == SEM_CREATE)) {
        armlet_vm_var *v = armlet_vm_var_new(NULL);
        nv = armlet_vm_named_from_var(v, it);
        armlet_vm_set_scoped(vm, n, ns, nv, SEM_SYMBOL_POLYMORPHIC);
        return armlet_vm_named_array_single(nv);
      }

      armlet_vm_named_array *na = armlet_vm_search_namespace(ns, it);

      if (na == NULL) {
        if (!is_last && (ns_deref == SEM_GET_OR_CREATE)) {
          armlet_vm_var *v = armlet_vm_make_namespace(it);
          nv = armlet_vm_named_from_var(v, it);
          armlet_vm_set_scoped(vm, n, ns, nv, SEM_SYMBOL_VALUE);
          CONTINUE_STATE(v->namespace, DEREF_NAMESPACE_DELVE);
        } else if (is_last && (ns_deref == SEM_GET_OR_CREATE)) {
          armlet_vm_var *v = armlet_vm_var_new(NULL);
          nv = armlet_vm_named_from_var(v, it);
          armlet_vm_set_scoped(vm, n, ns, nv, SEM_SYMBOL_VALUE);
          return armlet_vm_named_array_single(nv);
        } else {
          if (null_on_error)
            return NULL;
          armlet_runtime_error(vm, n->source, "Unable to find '%s' in '%s'", it,
                               ns->name);
        }
      } else {
        nv = na->items[0];

        switch (nv->var->type->tag) {
        case T_INSTANCE: {
          context = nv->var->instance;
          state = DEREF_INSTANCE;
          break;
        }
        case T_SCOPE: {
          context = nv->var->namespace;
          break;
        }
        case T_BITS: {
          context = nv->var;
          state = DEREF_BITS;
          if (!is_last) {
            break;
          } else {
            return armlet_vm_named_array_single(nv);
          }
        }
        default: {
          if (!is_last) {
            if (null_on_error)
              return NULL;
            armlet_runtime_error(vm, n->source,
                                 "Attempt to dereference '%s' of '%s'",
                                 armlet_vm_var_type_name(nv->var), ns->name);
          }

          return armlet_vm_named_array_single(nv);
        }
        }
      }
    }
  });

#undef CONTINUE_STATE

  return NULL;
}

void armlet_vm_type_spec_check(armlet_vm_context *vm, armlet_ast_node *n,
                               armlet_vm_var *var, armlet_ast_node *err) {
  assert(vm);
  assert(n);
  assert(var);

  armlet_ast_node *err_node = err != NULL ? err : n;

  armlet_ast_node *name = NULL;
  if (n->type == AST_TYPE_SPEC) {
    name = n->type_spec->name;
  } else if (n->type == AST_VALUE) {
    name = n;
  } else {
    armlet_source_error_n(err_node, "Invalid type check node: '%s'",
                          armlet_ast_node_type_names[n->type]);
  }

  armlet_ast_type_spec *ts = n->type_spec;

  const char *left_t = name->value->name;
  const char *right_t = var->type->name;

  enum armlet_vm_var_tag t = MAPPING(TYPE_MAPPING, (char *)left_t);

  if (t == (enum armlet_vm_var_tag) - 1) {
    t = TAG_MASK2(T_INSTANCE, T_ENUMERATION);
  } else {
    t = TAG_MASK1(t);
  }

  if (!armlet_vm_var_is_one_of_type(var, t)) {
    armlet_source_error_n(
        err_node, "Assignment to a variable of type '%s' value '%s' (%s : %s)",
        armlet_vm_var_type_name(var), armlet_vm_var_tag_names[t], left_t,
        right_t);
  }

  if (armlet_vm_var_tag_mask_check(t, T_BITS)) {
    ARMLET_VAR(left_s) = armlet_vm_eval_value(vm, ts->size);
    size_t size_left = SAFE_MPZ_GET_UI(left_s->integer);
    size_t size_right = var->type->size;

    if (size_left != size_right) {
      armlet_runtime_error(
          vm, err_node->source,
          "Assignment to a variable of size %zu a value of size %zu", size_left,
          size_right);
    }
  }

  if (!streq(left_t, right_t)) {
    armlet_runtime_error(
        vm, err_node->source,
        "Assignment to a variable of size %zu a value of size %zu", left_t,
        right_t);
  }
}

armlet_vm_var_named *armlet_vm_resolve_create(armlet_vm_context *vm,
                                              armlet_ast_node *value) {

  assert(vm);
  assert(value);
  assert(value->type == AST_VALUE);

  armlet_vm_named_array *na =
      armlet_vm_resolve_var(vm, value->value, SEM_CREATE, value, false);

  if (!na || na->num_items == 0) {
    armlet_runtime_error(vm, value->source,
                         "Failed to resolve variable for creation");
  }

  return na->items[na->num_items - 1];
}

armlet_vm_var_named *
armlet_vm_resolve_single(armlet_vm_context *vm,
                         enum armlet_dereference_semantics sem,
                         armlet_ast_node *value) {
  assert(vm);
  assert(value);
  assert(value->type == AST_VALUE);

  armlet_vm_named_array *na =
      armlet_vm_resolve_var(vm, value->value, sem, value, false);

  return armlet_vm_named_single(na);
}

armlet_ast_type_spec *armlet_type_spec_from_vardef(armlet_ast_node *n) {
  assert(n->type == AST_VAR_DEF);

  armlet_ast_var_definition *vd = n->var_def;

  assert(vd->type->type == AST_TYPE_SPEC);

  armlet_ast_type_spec *ts = vd->type->type_spec;

  return ts;
}

armlet_vm_type *armlet_vm_type_from_type_spec(armlet_vm_context *vm,
                                              armlet_ast_type_spec *ts) {
  const char *n = ts->name->value->name;

  if (streq(n, "bits")) {
    armlet_vm_var *v = armlet_vm_eval_value(vm, ts->size);
    return armlet_vm_make_bitstring_type(SAFE_MPZ_GET_UI(v->integer));
  } else if (streq(n, "bit")) {
    return armlet_vm_make_bitstring_type(1);
  }

  for (size_t i = 0;
       i < (sizeof(DEFINED_SIMPLE_TYPES) / sizeof(*DEFINED_SIMPLE_TYPES));
       ++i) {
    if (streq(DEFINED_SIMPLE_TYPES[i]->name, n))
      return DEFINED_SIMPLE_TYPES[i];
  }

  armlet_vm_var_named *named =
      armlet_vm_named_single(armlet_vm_get_named(vm, n));
  if (named != NULL && armlet_vm_var_is_type(named->var, T_TYPE)) {
    return named->var->custom_type->type;
  }

  return NULL;
}

armlet_vm_var *armlet_var_from_named(armlet_vm_var_named *n) { return n->var; }

size_t armlet_vm_var_size(armlet_vm_var *v) { return v->type->size; }

bool armlet_vm_type_compare(armlet_vm_type *a, armlet_vm_type *b) {
  switch (a->tag) {
  case T_BITS:
  case T_BITLAYOUT:
    return (b->tag == T_BITS || b->tag == T_BITLAYOUT) && (a->size == b->size);
  default: {
    if (a->tag != b->tag)
      return false;

    if (!streq(a->name, b->name))
      return false;

    if (a->size != b->size)
      return false;
  }
  }
  return true;
}

void armlet_vm_binop_impl(armlet_vm_context *vm, armlet_vm_var *a,
                          armlet_vm_var *b, enum armlet_vm_binop op,
                          armlet_ast_node *n) {
  armlet_vm_var *result = armlet_vm_var_new(NULL);

  if (op == BINOP_PWR) {
    if (!armlet_vm_var_is_type(b, T_INTEGER)) {
      armlet_source_error_n(
          n, "Arguments to ^ power operator must be T_INTEGER, got '%s'",
          armlet_vm_var_type_name(b));
    }
  } else {
    if (!armlet_vm_var_is_type(a, b->type->tag)) {
      armlet_source_error_n(
          n, "Arguments must be of the same type, got '%s' and '%s'",
          armlet_vm_var_type_name(a), armlet_vm_var_type_name(b));
    }
  }

#define OP(_F, _OP)                                                            \
  result->_F = a->_F _OP b->_F;                                                \
  break

#define MPZ_OP(_OP)                                                            \
  _OP(result->integer, a->integer, b->integer);                                \
  break

#define MPZ_OP_UI(_OP)                                                         \
  _OP(result->integer, a->integer, SAFE_MPZ_GET_UI(b->integer));               \
  break

  switch (a->type->tag) {
  case T_BITS: {
    switch (op) {
    case BINOP_ADD:
    case BINOP_SUB:
    case BINOP_MUL:
    case BINOP_DIV: {
      mpz_t na;
      mpz_t nb;
      mpz_t nr;

      mpz_init_set_str(na, (const char *)a->bits, 2);
      mpz_init_set_str(nb, (const char *)b->bits, 2);
      mpz_init(nr);

      switch (op) {
      case BINOP_ADD: {
        mpz_add(nr, na, nb);
        break;
      }
      case BINOP_SUB: {
        mpz_sub(nr, na, nb);
        break;
      }
      case BINOP_MUL: {
        mpz_mul(nr, na, nb);
        break;
      }
      case BINOP_DIV: {
        mpz_div(nr, na, nb);
        break;
      }
      default:
        break;
      }

      char *bits = mpz_get_str(NULL, 2, nr);
      size_t n_bits = strlen(bits);

      result->type = armlet_vm_make_bitstring_type(n_bits);
      result->bits = (uint8_t *)bits;

      mpz_clears(na, nb, nr, NULL);
      break;
    }
    default: {
      size_t n_bits = a->type->size;

      if (n_bits != b->type->size && n_bits != 0) {
        armlet_runtime_error(vm, n->source,
                             "Arguments to bitstring operators must be of "
                             "same size greater than zero, got %zu and %zu",
                             n_bits, b->type->size);
      }

      result->type = armlet_vm_make_bitstring_type(n_bits);
      result->bits = calloc(1, n_bits + 1);
      if (!result->bits)
        BAIL("Out of memory: %s:%d\n", __FILE__, __LINE__);

      for (size_t i = 0; i < n_bits; ++i) {
        bool A = a->bits[i] == '1';
        bool B = b->bits[i] == '1';

        switch (op) {
        case BINOP_AND:
          result->bits[i] = A && B ? '1' : '0';
          break;
        case BINOP_OR:
          result->bits[i] = A || B ? '1' : '0';
          break;
        case BINOP_EOR:
          result->bits[i] = A ^ B ? '1' : '0';
          break;
        default:
          armlet_source_error_n(n, "Unsupported binary %s operator for T_BITS",
                                armlet_vm_binop_names[op]);
        }
      }
    }
    }

    break;
  }
  case T_INTEGER: {
    result->type = &TYPE_INTEGER;
    mpz_init(result->integer);

    switch (op) {
    case BINOP_MUL:
      MPZ_OP(mpz_mul);
    case BINOP_AND:
      MPZ_OP(mpz_and);
    case BINOP_ADD:
      MPZ_OP(mpz_add);
    case BINOP_DIV:
      MPZ_OP(mpz_cdiv_q);
    case BINOP_EOR:
      MPZ_OP(mpz_xor);
    case BINOP_OR:
      MPZ_OP(mpz_ior);
    case BINOP_SUB:
      MPZ_OP(mpz_sub);
    case BINOP_SHL:
      MPZ_OP_UI(mpz_mul_2exp);
    case BINOP_SHR:
      MPZ_OP_UI(mpz_div_2exp);
    case BINOP_MOD:
      MPZ_OP(mpz_mod);
    case BINOP_PWR:
      MPZ_OP_UI(mpz_pow_ui);
    default:
      armlet_source_error_n(n, "Unsupported binary %s operator for T_INTEGER",
                            armlet_vm_binop_names[op]);
    }
    break;
  }
  case T_REAL:
    switch (op) {
    case BINOP_MUL:
      OP(real, *);
    case BINOP_ADD:
      OP(real, +);
    case BINOP_FDIV:
      OP(real, /);
    case BINOP_SUB:
      OP(real, -);
    case BINOP_MOD:
      result->real = fmodf(a->real, b->real);
      break;
    case BINOP_PWR:
      result->real = powf(a->real, SAFE_MPZ_GET_UI(b->integer));
      break;
    default:
      armlet_source_error_n(n, "Unsupported binary %s operator for T_REAL",
                            armlet_vm_binop_names[op]);
    }
    result->type = &TYPE_REAL;
    break;
  case T_BOOLEAN:
    switch (op) {
    case BINOP_AND:
      OP(boolean, &&);
    case BINOP_OR:
      OP(boolean, ||);
    case BINOP_EOR:
      OP(boolean, ^);
    default:
      armlet_source_error_n(n, "Unsupported binary %s operator for T_BOOLEAN",
                            armlet_vm_binop_names[op]);
    }
    result->type = &TYPE_BOOLEAN;
    break;
  default:
    armlet_source_error_n(n, "Unsupported binary %s operator for %s",
                          armlet_vm_binop_names[op],
                          armlet_vm_var_type_name(a));
  }
#undef OP
  armlet_vm_stack_push(vm->stack, result);
}

bool armlet_vm_bitstring_compare_impl(uint8_t *a, uint8_t *b, size_t n) {
  for (size_t i = 0; i < n; ++i) {
    char A = a[i];
    char B = b[i];

    if (A != B) {
      if (A != 'x' && B != 'x')
        return false;
    }
  }

  return true;
}

armlet_vm_var *armlet_vm_var_integer_to_bits(armlet_vm_var *src,
                                             size_t desired_length);

uint8_t *armlet_vm_can_bitstring_compare(const armlet_vm_var *v,
                                         size_t desired_length,
                                         armlet_vm_var **temp_out) {
  *temp_out = NULL;

  if (armlet_vm_var_is_type((armlet_vm_var *)v, T_BITS))
    return v->bits;

  if (armlet_vm_var_is_type((armlet_vm_var *)v, T_BITLAYOUT))
    return v->bitlayout->compare_mask;

  if (armlet_vm_var_is_type((armlet_vm_var *)v, T_INTEGER)) {
    *temp_out = armlet_vm_var_integer_to_bits((armlet_vm_var *)v, desired_length);
    return (*temp_out)->bits;
  }

  return NULL;
}

bool armlet_vm_bitstring_compare(const armlet_vm_var *a,
                                 const armlet_vm_var *b) {
  size_t target;
  if (a->type->tag == T_BITS || a->type->tag == T_BITLAYOUT)
    target = a->type->size;
  else if (b->type->tag == T_BITS || b->type->tag == T_BITLAYOUT)
    target = b->type->size;
  else
    return false;

  armlet_vm_var *a_temp = NULL;
  armlet_vm_var *b_temp = NULL;
  uint8_t *Ab = armlet_vm_can_bitstring_compare(a, target, &a_temp);
  uint8_t *Bb = armlet_vm_can_bitstring_compare(b, target, &b_temp);

  bool result = false;
  if (Ab != NULL && Bb != NULL) {
    size_t a_size = a_temp ? a_temp->type->size : a->type->size;
    size_t b_size = b_temp ? b_temp->type->size : b->type->size;
    if (a_size == b_size)
      result = armlet_vm_bitstring_compare_impl(Ab, Bb, a_size);
  }

  if (a_temp)
    armlet_vm_var_release(ARMLET_DECREF(a_temp));
  if (b_temp)
    armlet_vm_var_release(ARMLET_DECREF(b_temp));

  return result;
}

inline bool armlet_apply_op_from_ordering(armlet_vm_ordering ord,
                                          enum armlet_vm_comparison op) {
  switch (op) {
  case CMP_EQ:
    return ord == ORDER_EQ;
  case CMP_NEQ:
    return ord != ORDER_EQ;
  case CMP_LT:
    return ord == ORDER_LT;
  case CMP_LTEQ:
    return ord == ORDER_LT || ord == ORDER_EQ;
  case CMP_GT:
    return ord == ORDER_GT;
  case CMP_GTEQ:
    return ord == ORDER_GT || ord == ORDER_EQ;
  default:
    return false;
  }
}

int fcmpf(float a, float b, float eps) {
  if (isinf(a) || isinf(b)) {
    if (a == b)
      return 0;
    return (a < b) ? -1 : 1;
  }

  float diff = a - b;
  float tol = eps * fmaxf(1.0f, fmaxf(fabsf(a), fabsf(b)));

  if (fabsf(diff) <= tol)
    return 0;

  return (diff < 0.0f) ? -1 : 1;
}

armlet_vm_ordering armlet_vm_var_ordering(const armlet_vm_var *a,
                                          const armlet_vm_var *b) {
  bool a_bits = a->type->tag == T_BITS || a->type->tag == T_BITLAYOUT;
  bool b_bits = b->type->tag == T_BITS || b->type->tag == T_BITLAYOUT;
  if (a_bits || b_bits)
    return armlet_vm_bitstring_compare(a, b) ? ORDER_EQ : ORDER_GT;

  switch (a->type->tag) {
  case T_INTEGER: {
    int c = mpz_cmp(a->integer, b->integer);
    if (c > 0)
      return ORDER_GT;
    if (c < 0)
      return ORDER_LT;
    return ORDER_EQ;
  }
  case T_REAL: {
    float x = a->real, y = b->real;
    if (isnanf(x) || isnanf(y))
      return ORDER_UNORDERED;
    int c = fcmpf(a->real, b->real, 0.00001); // make epsilon configurable?
    if (c > 0)
      return ORDER_GT;
    if (c < 0)
      return ORDER_LT;
    return ORDER_EQ;
  }

  case T_BOOLEAN: {
    bool x = a->boolean, y = b->boolean;
    if (x == y)
      return ORDER_EQ;
    return x ? ORDER_GT : ORDER_LT;
  }

  case T_ENUMERATION:
    return streq(a->enum_value->name, b->enum_value->name) ? ORDER_EQ
                                                           : ORDER_GT;

  case T_INSTANCE:
    return a->instance == b->instance ? ORDER_EQ : ORDER_GT;

  default:
    return ORDER_UNSUPPORTED;
  }
}

static bool type_supports_op(const armlet_vm_type *t,
                             enum armlet_vm_comparison op) {
  switch (t->tag) {
  case T_INTEGER:
  case T_REAL:
  case T_BOOLEAN:
    return true;
  case T_ENUMERATION:
  case T_BITLAYOUT:
  case T_BITS:
  case T_INSTANCE:
    return (op == CMP_EQ || op == CMP_NEQ);
  default:
    return false;
  }
}

bool armlet_vm_compare(const armlet_vm_var *a, const armlet_vm_var *b,
                       enum armlet_vm_comparison op, armlet_ast_node *n) {
  bool a_bits = a->type->tag == T_BITS || a->type->tag == T_BITLAYOUT;
  bool b_bits = b->type->tag == T_BITS || b->type->tag == T_BITLAYOUT;
  bool mixed_int_bits = (a_bits && b->type->tag == T_INTEGER) ||
                        (b_bits && a->type->tag == T_INTEGER);

  if (!mixed_int_bits && !armlet_vm_type_compare(a->type, b->type)) {
    return false;
  }

  if (!type_supports_op(a->type, op)) {
    armlet_source_error_n(n, "Operator not supported for type '%s'",
                          armlet_vm_var_type_name(a));
  }

  armlet_vm_ordering ord = armlet_vm_var_ordering(a, b);

  if (ord == ORDER_UNSUPPORTED) {
    armlet_source_error_n(n, "Ordering not supported for type '%s'\n",
                          a->type->name);
  }

  if (ord == ORDER_UNORDERED) {
    if (op == CMP_EQ)
      return false;
    if (op == CMP_NEQ)
      return true;
    armlet_source_error_n(n, "Unordered comparison for type '%s' (e.g., NaN)\n",
                          a->type->name);
  }

  return armlet_apply_op_from_ordering(ord, op);
}

bool armlet_vm_compare_sequence(const armlet_vm_var *a, const armlet_vm_var *b,
                                enum armlet_vm_comparison op,
                                armlet_ast_node *n) {

  if (!armlet_vm_type_compare(a->type, b->type)) {
    armlet_source_error_n(n,
                          "Incompatible comparison types %s (size: %zu) "
                          "and %s (size: %zu)",
                          armlet_vm_var_type_name(a), a->num_contents,
                          armlet_vm_var_type_name(b), b->num_contents);
  }

  for (size_t i = 0; i < a->num_contents; ++i) {
    if (!armlet_vm_compare(a->contents[i], b->contents[i], op, n)) {
      return false;
    }
  }

  return true;
}

void data_init_source(armlet_vm_context *vm, void *target, size_t size,
                      armlet_ast_node *n) {
  size_t read = fread(target, size, 1, vm->config.initializer);
  if (read != 1) {
    armlet_runtime_error(vm, n->source,
                         "Unable to read from initializer data source");
  }
}

void armlet_vm_var_init(armlet_vm_context *vm, armlet_vm_var *v,
                        char *type_name, enum armlet_var_init_semantics sem,
                        armlet_ast_node *n) {
  switch (v->type->tag) {
  case T_BITS: {
    v->bits = CHECKED_MALLOC(v->type->size + 1);
    switch (sem) {
    case INIT_ZERO: {
      memset(v->bits, '0', v->type->size);
      break;
    }
    case INIT_SOURCE: {
      data_init_source(vm, v->bits, v->type->size, n);
      for (size_t i = 0; i < v->type->size; ++i) {
        v->bits[i] = '0' + (v->bits[i] & 1);
      }
      break;
    }
    }
    v->bits[v->type->size] = '\0';
    break;
  }
  case T_REAL: {
    switch (sem) {
    case INIT_ZERO:
      v->real = 0.f;
      break;
    case INIT_SOURCE:
      // This should do uniform distribution in the [0, 1) interval
      // uint32_t x = 0;
      // get_random(vm, &x, sizeof(x));
      // x >>= 8;
      // v->real = (float)x / (float)(1U << 24);
      // This would be incorrect most of the time, but it's ok
      // since this is used by UNKNOWN which in turn happens
      // during RESET / startup so stupid and bonkers values should be
      // expected
      data_init_source(vm, &v->real, sizeof(v->real), n);
      break;
    }
    break;
  }
  case T_INTEGER: {
    mpz_init(v->integer);
    unsigned long int x = 0;
    switch (sem) {
    case INIT_ZERO:
      break;
    case INIT_SOURCE: {
      data_init_source(vm, &x, sizeof(x), n);
      break;
    }
    }
    mpz_set_ui(v->integer, x);
    break;
  }
  case T_TYPE: {
    if (type_name == NULL) {
      armlet_runtime_error(vm, n->source,
                           "Instance initialization without a type name\n");
    }

    armlet_vm_var_named *type_ref =
        armlet_vm_named_single(armlet_vm_get_named(vm, type_name));

    if (type_ref == NULL) {
      armlet_runtime_error(
          vm, n->source,
          "Unable to initialize a type name: '%s', type doesn't exist",
          type_name);
    }

    armlet_vm_custom_type *ct = type_ref->var->custom_type;
    armlet_vm_instance *instance = NEW0(armlet_vm_instance);

    hashtable_new(32, &instance->fields);
    instance->class = ct;

    ARR_FOREACH(ct, fields, {
      armlet_vm_var *iv = armlet_vm_var_new(ARMLET_INCREF(it->type));

      if (it->type->tag == T_ARRAY) {
        armlet_vm_type *inner = ARMLET_INCREF(it->type->inner);

        iv->contents_type = inner;
        iv->contents_base = it->type->start;

        char *field_type_name = inner->tag == T_TYPE ? inner->name : NULL;

        size_t size = (it->type->end - it->type->start);

        for (size_t j = 0; j < size; ++j) {
          armlet_vm_var *arr_var = armlet_vm_var_new(it->type->inner);
          armlet_vm_var_init(vm, arr_var, field_type_name, sem, n);
          ARR_APPEND(iv, contents, arr_var);
        }
      } else {
        char *field_type_name = it->type->tag == T_TYPE ? it->type->name : NULL;
        armlet_vm_var_init(vm, iv, field_type_name, sem, n);
      }

      hashtable_add_str(instance->fields, it->name,
                        armlet_vm_named_from_var(iv, it->name));
    });

    v->type = armlet_vm_make_instance_type(ct);
    v->instance = instance;
    break;
  }
  case T_ARRAY:
  case T_ENUMERATION_TYPE:
  case T_INSTANCE:
  case T_SCOPE:
  case T_FUNCTION:
  case T_SET:
    armlet_runtime_error(vm, n->source, "Unable to zero initialize '%s'",
                         armlet_vm_var_type_name(v));
    break;
  default:
    break;
  }

  v->is_unset = true;
}

void armlet_vm_var_set_enum(armlet_vm_var *v, uint64_t value, char *name,
                            armlet_vm_type *type) {
  v->enum_value = NEW0(armlet_vm_enum_element);
  v->enum_value->value = value;
  v->enum_value->name = name;
  v->enum_value->type = type;
}

armlet_vm_named_array *
armlet_vm_resolve_var(armlet_vm_context *vm, armlet_ast_value *v,
                      enum armlet_dereference_semantics sem, armlet_ast_node *n,
                      bool null_on_error) {
  switch (v->tag) {
  case VAL_NAME: {
    char *name = v->name;

    if (sem == SEM_CREATE) {
      armlet_vm_var_named *named = armlet_vm_named_from_var(
          ARMLET_DECREF(armlet_vm_var_new(NULL)), name);

      armlet_vm_frame *f = armlet_vm_current_frame(vm);

      return armlet_vm_add_symbol(vm, n, f->symbols, named,
                                  SEM_SYMBOL_POLYMORPHIC);
    }

    armlet_vm_named_array *named = armlet_vm_get_named(vm, name);

    if (named) {
      return named;
    } else {
      if (sem == SEM_GET_OR_CREATE) {
        armlet_vm_var_named *vn =
            armlet_vm_named_from_var(armlet_vm_var_new(NULL), name);
        // set_local increfs, the returned value should have 1 ref
        vn->var->ref_count = 0;
        return armlet_vm_set_local(vm, vn, n);
      }
      break;
    }
  }
  case VAL_DEREF: {
    armlet_vm_named_array *df =
        armlet_vm_dereference(vm, &v->deref, sem, n, null_on_error);

    if (df) {
      return df;
    } else {
      if (null_on_error)
        return NULL;
      char *path = str_join_from_array(".", (const char **)v->deref.names,
                                       v->deref.num_names);
      armlet_runtime_error(vm, n->source, "Dereference not found '%s'", path);
    }
    break;
  }
  case VAL_IMMEDIATE: {
    armlet_runtime_error(vm, n->source, "Unable to resolve an immediate");
    break;
  }
  }

  return NULL;
}

char *armlet_vm_value_to_string(armlet_ast_value *v) {
  switch (v->tag) {
  case VAL_NAME: {
    return v->name;
  }
  case VAL_DEREF: {
    return str_join_from_array(".", (const char **)v->deref.names,
                               v->deref.num_names);
  }
  default: {
    return NULL;
  }
  }
}

enum armlet_type_alias_semantics { ALIAS_ANY, ALIAS_RETURN, ALIAS_NAMED };

armlet_ast_node *
armlet_vm_type_alias_replace(armlet_vm_context *vm, armlet_ast_node *n,
                             enum armlet_type_alias_semantics sem) {
  if (n->type == AST_TUPLE) {
    ARR_FOREACH(n->tuple, elements, {
      n->tuple->elements[i] = armlet_vm_type_alias_replace(vm, it, sem);
    });

    return n;
  }

  armlet_ast_node *tn = NULL;

  if (n->type == AST_TYPE_SPEC) {
    tn = n->type_spec->name;
  } else if (n->type == AST_VALUE) {
    tn = n;
  } else {
    armlet_source_error_n(n, "Invalid alias node: '%s'",
                          armlet_ast_node_type_names[n->type]);
  }

  armlet_vm_var_named *vn = armlet_vm_resolve_single(vm, SEM_GET, tn);
  if (vn == NULL)
    return n;

  armlet_vm_var *var = vn->var;
  if (!armlet_vm_var_is_type(var, T_TYPE_ALIAS))
    return n;

  armlet_vm_type_alias *ta = var->type_alias;
  if (ta->to == NULL)
    return n;

  if (sem == ALIAS_NAMED && ta->to->type == AST_TUPLE) {
    armlet_source_error_n(n, "Named value cannot be aliased to a tuple");
  }

  return ta->to;
}

void armlet_vm_define_callable(armlet_vm_context *vm, armlet_ast_node *n) {
  armlet_vm_function *f = NEW0(armlet_vm_function);

  armlet_ast_callable_definition *def = n->callable_def;
  f->def = def;

  ARR_FOREACH(def, parameters, {
    armlet_ast_type_spec *ts = it->type->type_spec;

    ts->name = armlet_vm_type_alias_replace(vm, ts->name, ALIAS_NAMED);

    armlet_ast_value *t = ts->name->value;

    ARR_APPEND(f, parameter_type_names, armlet_vm_value_to_string(t));
  });

  armlet_ast_node *rt = def->return_type;
  if (rt != NULL) {
    switch (rt->type) {
    case AST_TUPLE:
    case AST_TYPE_SPEC: {
      def->return_type =
          armlet_vm_type_alias_replace(vm, def->return_type, ALIAS_RETURN);
      break;
    }
    default: {
    }
    }
  }

  switch (def->name->type) {
  case AST_VALUE: {
    switch (def->name->value->tag) {
    case VAL_NAME:
    case VAL_DEREF: {
      armlet_vm_var_named *v = armlet_vm_resolve_create(vm, def->name);

      f->name = v->name;
      v->var->function = f;

      switch (def->type) {
      case CALLABLE_FUNC: {
        v->var->type = armlet_vm_type_new(T_FUNCTION, f->name, 8);
        break;
      }
      case CALLABLE_GETTER: {
        v->var->type = armlet_vm_type_new(T_GETTER, f->name, 8);
        break;
      }
      case CALLABLE_SETTER: {
        v->var->type = armlet_vm_type_new(T_SETTER, f->name, 8);
        break;
      }
      }

      break;
    }
    case VAL_IMMEDIATE: {
      armlet_source_error_n(n, "Unable to call an immediate");
      break;
    }
    }
    break;
  }
  default: {
    break;
  }
  }
}

void armlet_set_integer(armlet_vm_var *v, uint64_t n) {
  mpz_init_set_ui(v->integer, n);
}

char *armlet_vm_parameter_size_name(armlet_ast_type_spec *spec) {
  return spec->size->value->name;
}

armlet_vm_var_named *
armlet_vm_contract_named(armlet_vm_context *vm,
                         armlet_ast_var_definition *definition, size_t size,
                         armlet_ast_node *n) {
  char *size_name = armlet_vm_parameter_size_name(definition->type->type_spec);
  armlet_vm_var_named *existing =
      armlet_vm_named_single(armlet_vm_get_local(vm, size_name));

  if (existing == NULL) {
    armlet_vm_var *size_var = armlet_vm_var_new(&TYPE_INTEGER);
    mpz_init_set_ui(size_var->integer, size);

    armlet_vm_var_named *size_named =
        armlet_vm_named_from_var(size_var, size_name);

    return size_named;
  } else {
    size_t new_size = size;
    size_t old_size = SAFE_MPZ_GET_UI(existing->var->integer);

    if (old_size != new_size) {
      armlet_runtime_error(vm, n->source,
                           "Pre-Condition Contract: Input parameter `%s` can't "
                           "be set to %zu because it was previously set to %zu",
                           size_name, new_size, old_size);
    }

    return NULL;
  }
}

char *armlet_vm_single_var_def_name(armlet_ast_var_definition *vd) {
  if (vd->num_names == 1) {
    return vd->names[0]->value->name;
  }

  return NULL;
}

void armlet_vm_contract_computed(armlet_vm_context *vm,
                                 armlet_ast_var_definition *definition,
                                 armlet_ast_node *n) {

  char *name = armlet_vm_single_var_def_name(definition);

  ARMLET_VAR(v) = armlet_vm_eval_value(vm, definition->type->type_spec->size);

  armlet_vm_var_named *loc =
      armlet_vm_named_single(armlet_vm_get_local(vm, name));

  size_t expected = SAFE_MPZ_GET_UI(v->integer);
  size_t actual = armlet_vm_var_size(loc->var);

  if (actual != expected) {
    armlet_runtime_error(vm, n->source,
                         "Pre-Condition Contract: Size of parameter `%s`"
                         " is %zu but %zu was expected",
                         name, actual, expected);
  }
}

void armlet_vm_contract_return_recurse_tuple(armlet_vm_context *vm,
                                             armlet_vm_function *func,
                                             armlet_ast_node *n,
                                             armlet_vm_var *value,
                                             armlet_ast_node *ret) {
  if (!armlet_vm_var_is_type(value, T_TUPLE)) {
    armlet_runtime_error(vm, ret->source,
                         "Return value of '%s' is a T_TUPLE but '%s' "
                         "was actually returned",
                         func->name, armlet_vm_var_type_name(value));
  }

  if (value->num_contents != n->tuple->num_elements) {
    armlet_runtime_error(vm, ret->source,
                         "Return value of '%s' is a tuple with %u elements "
                         "but a tuple with %u elements was actually returned",
                         func->name, n->tuple->num_elements,
                         value->num_contents);
  }

  for (size_t i = 0; i < value->type->size; ++i) {
    armlet_ast_node *rt = n->tuple->elements[i];
    armlet_vm_var *arv = value->contents[i];

    if (rt->type == AST_TUPLE) {
      armlet_vm_contract_return_recurse_tuple(vm, func, rt, arv, ret);
    } else {
      if (rt->type != AST_TYPE_SPEC) {
        armlet_runtime_error(vm, ret->source,
                             "Invalid return type inside a tuple: %s",
                             armlet_ast_node_type_names[rt->type]);
      }

      armlet_vm_type_spec_check(vm, rt, arv, ret);
    }
  }
}

void armlet_vm_contract_return(armlet_vm_context *vm,
                               armlet_ast_node *definition,
                               armlet_vm_function *func) {
  if (definition->return_->return_ != NULL) {
    armlet_vm_var *value = armlet_vm_peek_stack(vm);

    armlet_ast_node *ret_node = func->def->return_type;
    if (ret_node != NULL) {
      if (ret_node->type == AST_TYPE_SPEC) {
        armlet_vm_type_spec_check(vm, ret_node, value, definition);
      } else if (ret_node->type == AST_TUPLE) {
        armlet_vm_contract_return_recurse_tuple(vm, func, ret_node, value,
                                                definition);
      } else {
        armlet_runtime_error(vm, ret_node->source,
                             "Invalid return type inside a tuple: %s",
                             armlet_ast_node_type_names[ret_node->type]);
      }
    } else {
      armlet_source_error_n(
          definition, "Attempted to return a value from a function with no "
                      "expected return value\n");
    }
  }
}

typedef struct {
  DEFINE_ARRAY(armlet_vm_var *, items);
} armlet_vm_var_array;

void armlet_callable_call(armlet_vm_context *vm, armlet_vm_var *var,
                          armlet_ast_node **in, armlet_vm_var *input_value,
                          armlet_ast_node *n) {
  armlet_vm_function *f = var->function;
  armlet_ast_callable_definition *def = f->def;

  armlet_line_info line_info =
      armlet_source_line_info_one(&n->source->source, n->source->span);

  char *frame_context =
      s_sprintf("<%s>: %s:%d:%d:", armlet_vm_var_to_string(var, false),
                n->source->source.file, line_info.lineno, line_info.col_start);

  armlet_vm_var_array evaluated = {};

  ARR_FOREACH(def, parameters, {
    armlet_vm_var *value = armlet_vm_eval_value(vm, in[i]);
    ARR_APPEND(&evaluated, items, value);
  });

  armlet_vm_push_frame(vm, frame_context);

  ARR_FOREACH(def, parameters, {
    armlet_vm_var *value = evaluated.items[i];

    if (it->contract == CONTRACT_NAMED) {
      armlet_vm_var_named *vn =
          armlet_vm_contract_named(vm, it, armlet_vm_var_size(value), in[i]);
      if (vn != NULL) {
        armlet_vm_set_local(vm, vn, n);
      }
    }

    armlet_vm_var_named *var =
        armlet_vm_named_from_var(value, armlet_vm_single_var_def_name(it));

    armlet_vm_set_local(vm, var, n);
  });

  if (input_value) {
    if (def->input_type->contract == CONTRACT_NAMED) {
      armlet_vm_var_named *nv = armlet_vm_contract_named(
          vm, def->input_type, armlet_vm_var_size(input_value),
          n->assignment->source);
      if (nv != NULL) {
        armlet_vm_set_local(vm, nv, n);
      }
    }

    armlet_vm_var_named *var = armlet_vm_named_from_var(
        input_value, armlet_vm_single_var_def_name(def->input_type));

    armlet_vm_set_local(vm, var, n);
  }

  ARR_FOREACH(def, parameters, {
    if (it->contract == CONTRACT_COMPUTED) {
      armlet_vm_contract_computed(vm, it, in[i]);
    }
  });

  ARR_FOREACH(def->body, nodes, {
    armlet_vm_eval(vm, it);

    if (vm->returned != NULL || it->type == AST_RETURN) {
      armlet_ast_node *r = vm->returned != NULL ? vm->returned : it;
      vm->returned = NULL;
      armlet_vm_contract_return(vm, r, f);
      break;
    }
  });

  armlet_vm_pop_frame(vm);
}

void armlet_vm_bitslurp(armlet_vm_context *vm, armlet_ast_node *n,
                        armlet_vm_var *source) {

  if (!armlet_vm_var_is_type(source, T_BITS)) {
    armlet_runtime_error(vm, n->source,
                         "Source for bit slurp must be bits, got: '%s'",
                         armlet_vm_var_type_name(source));
  }

  armlet_ast_bitslurp *as = n->bitslurp;

  size_t off = 0;
  ARR_FOREACH(as, elements, {
    armlet_vm_var_named *nv = armlet_vm_resolve_single(vm, SEM_GET, it);

    if (!nv) {
      armlet_runtime_error(vm, n->source, "Not found in current scope");
    } else if (nv->var->is_const) {
      armlet_source_error_n(n, "Variable '%s' is constant", nv->name);
    }

    armlet_vm_type *nvt = nv->var->type;

    if (!armlet_vm_var_is_type(nv->var, T_BITS)) {
      armlet_runtime_error(vm, n->source,
                           "Target for bit slurp must be bits, got: '%s'",
                           armlet_vm_var_type_name(nv->var));
    }

    if ((off + nvt->size) > source->type->size) {
      armlet_runtime_error(vm, n->source,
                           "Attempted to slurp more bits than available");
    }

    memcpy(nv->var->bits, source->bits + off, nvt->size);
    off += nvt->size;
  });
}

armlet_vm_var *armlet_vm_valid_condition(armlet_vm_context *vm,
                                         armlet_ast_node *condition) {
  armlet_vm_var *cond = armlet_vm_eval_value(vm, condition);

  if (!armlet_vm_var_is_type(cond, T_BOOLEAN)) {
    armlet_runtime_error(vm, condition->source,
                         "Condition must be of type boolean, got: %s",
                         cond->type->name);
  }

  return cond;
}

void armlet_vm_callable_search_release(armlet_vm_callable_search *cs) {
  ARR_FOREACH(cs, evaluated, { armlet_vm_var_release(it); });
  free(cs->evaluated);
  free(cs);
}

armlet_vm_callable_search *
armlet_vm_callable_search_from(armlet_vm_context *vm, armlet_ast_node **params,
                               size_t num_params) {
  armlet_vm_callable_search *s = NEW0(armlet_vm_callable_search);

  for (size_t i = 0; i < num_params; ++i) {
    armlet_ast_node *param = params[i];
    ARR_APPEND(s, evaluated, armlet_vm_eval_value(vm, param));
  }

  return s;
}

armlet_vm_var_named *
armlet_vm_callable_polymorphic(armlet_vm_callable_search *cs,
                               armlet_vm_named_array *na,
                               enum armlet_callable_preference pref) {
  size_t num_evaluated = cs->num_evaluated;

  ARR_FOREACH(na, items, {
    if (armlet_vm_var_is_one_of_type(it->var, TAG_MASK2(T_ARRAY, T_BITS)) &&
        num_evaluated == 1) {
      if (armlet_vm_var_is_type(cs->evaluated[0], T_INTEGER)) {
        return it;
      } else {
        return NULL;
      }
    }

    if (armlet_vm_var_is_one_of_type(
            it->var, TAG_MASK3(T_TYPE, T_BUILTIN, T_BITLAYOUT))) {
      return it;
    }

    armlet_vm_function *f = it->var->function;
    if (f->num_parameter_type_names == num_evaluated) {
      size_t j;

      for (j = 0; j < num_evaluated; ++j) {
        if (!streq(cs->evaluated[j]->type->name, f->parameter_type_names[j])) {
          break;
        }
      }

      if (j == num_evaluated) {
        switch (pref) {
        case CALLPREF_ANY:
          return it;
        case CALLPREF_SETTER: {
          if (armlet_vm_var_is_type(it->var, T_SETTER))
            return it;
          break;
        }
        case CALLPREF_GETTER: {
          if (armlet_vm_var_is_type(it->var, T_GETTER))
            return it;
          break;
        }
        }
      }
    }
  });

  return NULL;
}

char *armlet_ast_type_to_string(armlet_ast_node *n) {
  if (n == NULL)
    return "";

  switch (n->type) {
  case AST_TUPLE: {
    armlet_string_list sl = {};
    ARR_FOREACH(n->tuple, elements,
                { ARR_APPEND(&sl, items, armlet_ast_type_to_string(it)); });
    return s_sprintf("(%s)", str_join_from_array(", ", (const char **)sl.items,
                                                 sl.num_items));
  }
  case AST_TYPE_SPEC: {
    char *size = "";
    if (n->type_spec->size != NULL) {
      size =
          s_sprintf("(%s)", armlet_source_string(n->type_spec->size->source));
    }
    return s_sprintf(
        "%s%s", armlet_vm_value_to_string(n->type_spec->name->value), size);
  }
  case AST_VALUE: {
    return armlet_vm_value_to_string(n->value);
  }
  default:
    return "INVALID_TYPE";
  }
}

char *armlet_vm_type_size(armlet_vm_var *v) {
  switch (v->type->tag) {
  case T_ARRAY:
    return s_sprintf("%zux%zu", v->type->size, v->contents_type->size);
  default:
    return s_sprintf("%zu", v->type->size);
  }
}

char *armlet_vm_type_to_string(armlet_vm_var *v) {
  switch (v->type->tag) {
  case T_ARRAY: {
    return s_sprintf("T_ARRAY[%zu..%zu; %s]", v->contents_base,
                     v->num_contents - 1,
                     armlet_vm_type_to_string(v->contents[0]));
  }
  case T_INSTANCE: {
    return s_sprintf("T_INSTANCE[%s]", v->instance->class->name);
  }
  case T_ENUMERATION: {
    return s_sprintf("T_ENUMERATION[%s]", v->enum_value->type->name);
  }
  default:
    return (char *)armlet_vm_var_tag_names[v->type->tag];
  }
}

char *armlet_vm_var_to_string(armlet_vm_var *v, bool struct_table) {
  switch (v->type->tag) {
  case T_BITS:
    return strdup((char *)v->bits);
  case T_INTEGER:
    return mpz_get_str(NULL, 10, v->integer);
  case T_REAL:
    return s_sprintf("%f", v->real);
  case T_ENUMERATION:
    return strdup(v->enum_value->name);
  case T_BOOLEAN:
    return strdup(v->boolean ? "TRUE" : "FALSE");
  case T_ARRAY: {
    armlet_string_list sl = {};

    ARR_FOREACH(v, contents, {
      ARR_APPEND(&sl, items, armlet_vm_var_to_string(it, struct_table));
    });

    return s_sprintf("[%s]", str_join_from_array(", ", (const char **)sl.items,
                                                 sl.num_items));
  }
  case T_INSTANCE: {
    armlet_vm_instance *in = v->instance;

    if (struct_table) {
      ft_table_t *table = ft_create_table();

      ft_set_border_style(table, FT_BASIC_STYLE);
      ft_set_cell_prop(table, 0, FT_ANY_COLUMN, FT_CPROP_ROW_TYPE,
                       FT_ROW_HEADER);
      ft_write_ln(table, "NAME", "TYPE", "VALUE");

      HASHTABLE_ITERATE_VALUES(in->fields, armlet_vm_var_named *, {
        ft_write_ln(table, value->name, armlet_vm_type_to_string(value->var),
                    armlet_vm_var_to_string(value->var, struct_table));

        ft_add_separator(table);
      });

      char *table_str = strdup((char *)ft_to_u8string(table));

      ft_destroy_table(table);

      return table_str;
    } else {
      armlet_string_builder sb = {};
      armlet_sb_init(&sb, 64);
      armlet_sb_append_string(&sb, in->class->name);
      armlet_sb_append_char(&sb, '{');

      bool done_first = false;
      HASHTABLE_ITERATE_VALUES(in->fields, armlet_vm_var_named *, {
        if (done_first) {
          armlet_sb_append_string_sized(&sb, ", ", 2);
        }

        armlet_sb_append_string(
            &sb, s_sprintf("%s=%s", value->name,
                           armlet_vm_var_to_string(value->var, struct_table)));

        done_first = true;
      });

      armlet_sb_append_char(&sb, '}');
      return sb.buf;
    }
  }
  case T_GETTER:
  case T_SETTER:
  case T_FUNCTION: {
    armlet_vm_function *f = v->function;
    armlet_string_list sl = {};

    ARR_FOREACH(f->def, parameters, {
      ARR_APPEND(&sl, items,
                 s_sprintf("%s %s", armlet_ast_type_to_string(it->type),
                           armlet_vm_value_to_string(it->names[0]->value)));
    });

    return s_sprintf(
        "%s %s(%s)", armlet_ast_type_to_string(f->def->return_type), f->name,
        str_join_from_array(", ", (const char **)sl.items, sl.num_items));
  }
  case T_TYPE_ALIAS: {
    armlet_vm_type_alias *ta = v->type_alias;
    return s_sprintf("%s -> %s", armlet_ast_type_to_string(ta->from),
                     armlet_ast_type_to_string(ta->to));
  }
  default:
    return "<NOT_IMPLEMENTED>";
  }
}

char *armlet_vm_var_to_bits(armlet_vm_var *v) {
  switch (v->type->tag) {
  case T_INTEGER:
    return mpz_get_str(NULL, 2, v->integer);
  case T_REAL:
    return armlet_integer_to_bistring(*((uint64_t *)&v->real), true);
  default:
    return "";
  }
}

armlet_ast_value armlet_vm_value_for_interpolate_name(const char *s) {
  assert(s);

  armlet_string_list sl = str_split(".", s);
  if (sl.num_items == 1) {
    char *name = sl.items[0];
    free(sl.items);
    return (armlet_ast_value){.tag = VAL_NAME, .name = name};
  } else {
    return (armlet_ast_value){
        .tag = VAL_DEREF,
        .deref = {.names = sl.items, .num_names = sl.num_items}};
  }
}

void armlet_vm_value_release(armlet_ast_value *v) {
  assert(v);

  switch (v->tag) {
  case VAL_NAME: {
    free(v->name);
    break;
  }
  case VAL_DEREF: {
    ARR_FOREACH(&v->deref, names, { free(it); });
    free(v->deref.names);
    break;
  }
  case VAL_IMMEDIATE: {
    BAIL("Unreachable");
  }
  }
}

static void armlet_vm_apply_bitlayout_members(armlet_vm_context *vm,
                                              armlet_vm_var *target,
                                              armlet_ast_bitlayout *layout,
                                              uint8_t *src_bits,
                                              armlet_node_source *err_src) {
  size_t offset = 0;
  ARR_FOREACH(layout, members, {
    switch (it->mtype) {
    case BITLAYOUT_NAMED: {
      armlet_span *span = NEW0(armlet_span);
      span->start = layout->total - offset - 1;
      span->end = (span->start + 1) - it->size;

      hashtable_add_str(target->named_bits->ranges, it->name, span);
      break;
    }
    case BITLAYOUT_IMMEDIATE: {
      if (!armlet_vm_bitstring_compare_impl(
              src_bits + offset, (uint8_t *)it->immediate, it->size)) {
        armlet_runtime_error(
            vm, err_src,
            "Bitlayout bits mismatch at position %zu, length %zu\n  "
            "Got:      %.*s\n  Expected: %.*s",
            offset, it->size, it->size, src_bits + offset, it->size,
            it->immediate);
      }
      break;
    }
    }

    offset += it->size;
  });
}

armlet_vm_var *armlet_vm_bits_to_bits_with_named(armlet_vm_context *vm,
                                                 armlet_vm_var *var,
                                                 armlet_ast_bitlayout *v,
                                                 armlet_ast_node *n) {
  if (!armlet_vm_var_is_type(var, T_BITS)) {
    armlet_runtime_error(vm, n->source,
                         "Argument to bitlayout constructor must be "
                         "T_INTEGER or T_BITS, got %s",
                         armlet_vm_var_type_name(var));
  }

  if (v->total != var->type->size) {
    armlet_runtime_error(
        vm, n->source,
        "Bitlayout constructor '%s' expects %zu bits, got %zu bits", v->name,
        v->total, var->type->size);
  }

  armlet_vm_var *new = armlet_vm_var_new(ARMLET_INCREF(var->type));

  new->bits = (uint8_t *)strdup((const char *)var->bits);
  new->named_bits = NEW0(armlet_vm_named_bits);
  hashtable_new(8, &new->named_bits->ranges);

  armlet_vm_apply_bitlayout_members(vm, new, v, var->bits, n->source);

  return new;
}

armlet_vm_var *armlet_vm_var_integer_to_bits(armlet_vm_var *src,
                                             size_t desired_length) {
  ARMLET_MPZ_VAR(abs);
  mpz_abs(abs, src->integer);

  ARMLET_STRING(abs_str) = mpz_get_str(NULL, 2, abs);
  size_t abs_len = strlen(abs_str);

  desired_length = abs_len > desired_length ? abs_len : desired_length;

  char *binary = (char *)CHECKED_MALLOC(desired_length + 1);

  if (mpz_sgn(src->integer) < 0) {
    ARMLET_MPZ_VAR(mask);
    mpz_ui_pow_ui(mask, 2, desired_length);

    ARMLET_MPZ_VAR(second_complement);
    mpz_add(second_complement, src->integer, mask);

    ARMLET_STRING(second_complement_str) =
        mpz_get_str(NULL, 2, second_complement);
    strncpy(binary, second_complement_str, desired_length);
  } else {
    size_t offset = desired_length - abs_len;
    if (offset > 0) {
      memset(binary, '0', offset);
    }
    strncpy(binary + offset, abs_str, abs_len);
  }
  binary[desired_length] = '\0';

  armlet_vm_var *v =
      armlet_vm_var_new(armlet_vm_make_bitstring_type(desired_length));
  v->bits = (uint8_t *)binary;

  return v;
}

char *armlet_vm_process_string_literal(armlet_vm_context *vm,
                                       armlet_ast_node *n) {
  assert(vm);
  assert(n);

  char *literal = n->value->imm.string;

  armlet_string_builder sb;
  armlet_sb_init(&sb, 128);

  const char *p = literal;

#define ESCAPE_PAIR(_A, _B, _C)                                                \
  if (p[0] == (_A) && p[1] == (_B)) {                                          \
    armlet_sb_append_char(&sb, (_C));                                          \
    p += 2;                                                                    \
    continue;                                                                  \
  }

#define ESCAPE(_X) ESCAPE_PAIR(_X, _X, _X)

  while (*p) {
    ESCAPE('{');
    ESCAPE('}');
    ESCAPE('\\');
    ESCAPE_PAIR('\\', '"', '"');

    if (*p == '{') {
      const char *start = ++p;
      while (*p && *p != '}')
        p++;

      if (*p != '}') {
        armlet_runtime_error(vm, n->source, "Unclosed { in string literal");
        break;
      }

      size_t span = (size_t)(p - start);
      ARMLET_STRING(ident) = CHECKED_MALLOC(span + 1);
      memcpy(ident, start, span);
      ident[span] = '\0';

      armlet_ast_value value = armlet_vm_value_for_interpolate_name(ident);
      armlet_vm_named_array *na =
          armlet_vm_resolve_var(vm, &value, SEM_GET, n, false);
      armlet_vm_value_release(&value);

      if (na == NULL) {
        // Create adjusted copy of the node's source node to point
        // to the location where the name that couldn't be found is
        // mentioned
        armlet_node_source adjusted = *n->source;
        adjusted.span.start += 1 + (start - literal);
        adjusted.span.end = adjusted.span.start + span;

        armlet_runtime_error(vm, &adjusted,
                             "Name '%s' not found in current scope", ident);
      }

      if (na->num_items > 1) {
        armlet_sb_append_char(&sb, '[');
      }

      ARR_FOREACH(na, items, {
        ARMLET_STRING(value) = armlet_vm_var_to_string(it->var, false);

        if (i > 0) {
          armlet_sb_append_string_sized(&sb, ", ", 2);
        }

        armlet_sb_append_string(&sb, value);
      });

      if (na->num_items > 1) {
        armlet_sb_append_char(&sb, ']');
      }

      p++;
      continue;
    }

    armlet_sb_append_char(&sb, *p++);
  }

#undef ESCAPE_PAIR
#undef ESCAPE

  return sb.buf;
}

static FILE *armlet_builtin_fopen(armlet_vm_context *vm, armlet_ast_node *n,
                                  const char *path, const char *mode) {
  FILE *f = fopen(path, mode);
  if (f == NULL) {
    armlet_runtime_error(vm, n->source, "Failed to open file '%s' (mode '%s')",
                         path, mode);
  }
  return f;
}

static void armlet_builtin_check_argc(armlet_ast_node *n, const char *fn,
                                      size_t got, size_t expected) {
  if (got != expected) {
    armlet_source_error_n(
        n, "Built-in function '%s' expects %zu parameters, got: %zu", fn,
        expected, got);
  }
}

static void armlet_builtin_check_arg_type(armlet_ast_node *n, const char *fn,
                                          size_t arg_index, armlet_vm_var *v,
                                          enum armlet_vm_var_tag expected) {
  if (!armlet_vm_var_is_type(v, expected)) {
    armlet_source_error_n(
        n, "Argument %zu to built-in function '%s' must be %s, got: %s",
        arg_index, fn, armlet_vm_var_tag_names[expected],
        armlet_vm_var_type_name(v));
  }
}

#define ARMLET_BUILTIN_DEFINE(name, blk)                                       \
  void armlet_vm_builtin__##name(armlet_vm_context *vm, armlet_ast_node *n) {  \
    const char *function_name = #name;                                         \
                                                                               \
    armlet_ast_call *call = n->call;                                           \
    armlet_ast_node **args = call->parameters;                                 \
    size_t num_args = call->num_parameters;                                    \
    (void)function_name;                                                       \
    (void)call;                                                                \
    (void)args;                                                                \
    (void)num_args;                                                            \
                                                                               \
    blk                                                                        \
  }

ARMLET_BUILTIN_DEFINE(dispatch, {
  if (num_args == 1) {
    if (vm->decoder == NULL) {
      vm->decoder = armlet_decoder_builder_finish(vm->decoder_builder);
    }

    ARMLET_VAR(value) = armlet_vm_eval_value(vm, args[0]);

    if (armlet_vm_var_is_type(value, T_INTEGER)) {
      armlet_vm_var *old = value;
      value = armlet_vm_var_integer_to_bits(value, vm->decoder->bit_width);
      armlet_vm_var_release(ARMLET_DECREF(old));
    } else if (!armlet_vm_var_is_type(value, T_BITS)) {
      armlet_runtime_error(vm, n->source,
                           "Argument to '%s' must be T_INTEGER or T_BITS",
                           function_name);
    }

    armlet_ast_bitlayout *layout =
        armlet_decoder_dispatch(vm->decoder, value->bits);

    if (layout != NULL) {
      if (layout->handler == NULL) {
        armlet_runtime_error(
            vm, n->source, "Unable to dispatch bitlayout without a handler: %s",
            layout->name);
      }

      armlet_vm_push_frame(vm, strdup(layout->name));

      armlet_vm_var *v =
          armlet_vm_bits_to_bits_with_named(vm, value, layout, n);

      if (layout->argument_name != NULL) {
        v->is_const = true;
        armlet_vm_var_named *input =
            armlet_vm_named_from_var(v, layout->argument_name);
        armlet_vm_set_local(vm, input, n);
      }

      // TODO: There are some instructions without any named bits
      // and we should be able to process them, maybe just make
      // the following HASHTABLE_ITERATE conditional?
      if (v->named_bits == NULL) {
        armlet_source_error_n(n, "Can't use a bits value without named bits");
      }

      HASHTABLE_ITERATE(v->named_bits->ranges, char *, armlet_span *, {
        armlet_vm_var *var = armlet_bits_var_from_span(vm, n, v, value);
        var->is_const = true;
        armlet_vm_var_named *named = armlet_vm_named_from_var(var, key);
        armlet_vm_set_local(vm, named, n);
      });

      armlet_vm_run(vm, layout->handler);

      armlet_vm_pop_frame(vm);
    } else {
      armlet_runtime_error(vm, n->source, "No bitlayout matching bitstring: %s",
                           value->bits);
    }
  } else {
    armlet_source_error_n(n,
                          "Built-in function '%s' "
                          "expects 1 parameters (value), got: %zu",
                          function_name, num_args);
  }
});

ARMLET_BUILTIN_DEFINE(implementation_defined, {
  armlet_builtin_check_argc(n, function_name, num_args, 2);

  ARMLET_VAR(key) = armlet_vm_eval_value(vm, args[0]);
  armlet_builtin_check_arg_type(n, function_name, 0, key, T_STRING);

  ARMLET_VAR(value) = armlet_vm_eval_value(vm, args[1]);

  armlet_implementation_defined *id = &vm->config.implementation_defined;
  hashtable_add_str(id->values, key->string, (void *)value);
});

ARMLET_BUILTIN_DEFINE(end_implementation_defined, {
  armlet_builtin_check_argc(n, function_name, num_args, 0);

  armlet_implementation_defined *id = &vm->config.implementation_defined;

  armlet_serialize_value value = {};
  value.tag = SERIALIZE_HASHTABLE;
  value.hashtable = id->values;

  int rc = armlet_vm_serialize(vm, id->file, &value);
  fclose(id->file);
  id->file = NULL;
  if (rc != 0) {
    armlet_runtime_error(vm, n->source,
                         "Failed to serialize implementation-defined values");
  }
});

ARMLET_BUILTIN_DEFINE(begin_implementation_defined, {
  armlet_builtin_check_argc(n, function_name, num_args, 1);

  ARMLET_VAR(file_name) = armlet_vm_eval_value(vm, args[0]);
  armlet_builtin_check_arg_type(n, function_name, 0, file_name, T_STRING);

  armlet_implementation_defined *id = &vm->config.implementation_defined;

  if (id->file != NULL) {
    armlet_source_error_n(n, "Implementation defined file already "
                             "provided via -i / --implementation-defined");
  }

  id->file = armlet_builtin_fopen(vm, n, file_name->string, "wb");
  hashtable_new(128, &id->values);
});

ARMLET_BUILTIN_DEFINE(serialize, {
  armlet_builtin_check_argc(n, function_name, num_args, 2);

  ARMLET_VAR(file_name) = armlet_vm_eval_value(vm, args[0]);
  armlet_builtin_check_arg_type(n, function_name, 0, file_name, T_STRING);

  ARMLET_VAR(variable) = armlet_vm_eval_value(vm, args[1]);

  FILE *f = armlet_builtin_fopen(vm, n, file_name->string, "wb");
  armlet_serialize_value value = {};
  value.tag = SERIALIZE_VAR;
  value.var = variable;
  int rc = armlet_vm_serialize(vm, f, &value);
  fclose(f);
  if (rc != 0) {
    armlet_runtime_error(vm, n->source,
                         "Failed to serialize value to file: %s",
                         file_name->string);
  }
});

ARMLET_BUILTIN_DEFINE(deserialize, {
  armlet_builtin_check_argc(n, function_name, num_args, 1);

  ARMLET_VAR(file_name) = armlet_vm_eval_value(vm, args[0]);
  armlet_builtin_check_arg_type(n, function_name, 0, file_name, T_STRING);

  FILE *f = armlet_builtin_fopen(vm, n, file_name->string, "rb");
  armlet_serialize_value value = {};
  if (armlet_vm_deserialize(vm, f, &value) == 0) {
    armlet_vm_stack_push(vm->stack, value.var);
  } else {
    armlet_runtime_error(vm, n->source, "Error deserializing value");
  }
  fclose(f);
});

static void bitlayout_json_str(FILE *f, const char *s) {
  if (s == NULL) {
    fputs("null", f);
    return;
  }
  fputc('"', f);
  for (const char *p = s; *p; p++) {
    switch (*p) {
    case '"':  fputs("\\\"", f); break;
    case '\\': fputs("\\\\", f); break;
    case '\n': fputs("\\n", f);  break;
    case '\r': fputs("\\r", f);  break;
    case '\t': fputs("\\t", f);  break;
    default:   fputc(*p, f);     break;
    }
  }
  fputc('"', f);
}

static void bitlayout_emit_json(FILE *f, armlet_ast_bitlayout *bl, int indent) {
  fputs("{\n", f);
  fprintf(f, "%*s\"name\": ", indent + 2, "");
  bitlayout_json_str(f, bl->name);
  fputs(",\n", f);
  fprintf(f, "%*s\"total\": %zu,\n", indent + 2, "", bl->total);
  fprintf(f, "%*s\"members\": [", indent + 2, "");

  size_t offset = bl->total;
  bool first = true;
  ARR_FOREACH(bl, members, {
    size_t size = it->size;
    size_t msb = offset - 1;
    size_t lsb = offset - size;
    offset = lsb;

    if (first) {
      fputs("\n", f);
      first = false;
    } else {
      fputs(",\n", f);
    }
    fprintf(f, "%*s{", indent + 4, "");

    switch (it->mtype) {
    case BITLAYOUT_NAMED:
      fputs("\"kind\": \"named\", \"name\": ", f);
      bitlayout_json_str(f, it->name);
      break;
    case BITLAYOUT_IMMEDIATE:
      fputs("\"kind\": \"immediate\", \"value\": ", f);
      bitlayout_json_str(f, it->immediate);
      break;
    }

    fprintf(f, ", \"size\": %zu, \"msb\": %zu, \"lsb\": %zu}", size, msb, lsb);
  });

  if (!first) {
    fputc('\n', f);
    fprintf(f, "%*s", indent + 2, "");
  }
  fputs("]\n", f);
  fprintf(f, "%*s}", indent, "");
}

ARMLET_BUILTIN_DEFINE(bitlayout_to_json, {
  if (num_args != 1 && num_args != 2) {
    armlet_source_error_n(
        n, "Built-in function '%s' expects 1 or 2 parameters, got: %zu",
        function_name, num_args);
  }

  ARMLET_VAR(layout) = armlet_vm_eval_value(vm, args[0]);
  armlet_builtin_check_arg_type(n, function_name, 0, layout, T_BITLAYOUT);

  FILE *f = stdout;
  bool owns_file = false;
  ARMLET_VAR(file_name) = NULL;
  if (num_args == 2) {
    file_name = armlet_vm_eval_value(vm, args[1]);
    armlet_builtin_check_arg_type(n, function_name, 1, file_name, T_STRING);
    f = armlet_builtin_fopen(vm, n, file_name->string, "wb");
    owns_file = true;
  }

  bitlayout_emit_json(f, layout->bitlayout, 0);
  fputc('\n', f);

  if (owns_file) {
    fclose(f);
  }
});

ARMLET_BUILTIN_DEFINE(export_bitlayouts_json, {
  if (num_args > 1) {
    armlet_source_error_n(
        n, "Built-in function '%s' expects 0 or 1 parameters, got: %zu",
        function_name, num_args);
  }

  FILE *f = stdout;
  bool owns_file = false;
  ARMLET_VAR(file_name) = NULL;
  if (num_args == 1) {
    file_name = armlet_vm_eval_value(vm, args[0]);
    armlet_builtin_check_arg_type(n, function_name, 0, file_name, T_STRING);
    f = armlet_builtin_fopen(vm, n, file_name->string, "wb");
    owns_file = true;
  }

  armlet_decoder_entry **entries = vm->decoder_builder->entries;
  size_t count = vm->decoder_builder->n;

  fputc('[', f);
  for (size_t i = 0; i < count; i++) {
    fputs(i == 0 ? "\n  " : ",\n  ", f);
    bitlayout_emit_json(f, entries[i]->layout, 2);
  }
  if (count > 0) {
    fputc('\n', f);
  }
  fputs("]\n", f);

  if (owns_file) {
    fclose(f);
  }
});

bool armlet_ast_is_string_literal(armlet_ast_node *n) {
  assert(n);
  return n->type == AST_VALUE && n->value->tag == VAL_IMMEDIATE &&
         n->value->imm.tag == IMM_STRING;
}

ARMLET_BUILTIN_DEFINE(print, {
  for (size_t i = 0; i < num_args; ++i) {
    if (args[i]->type == AST_VALUE) {
      if (armlet_ast_is_string_literal(args[i])) {
        puts(armlet_vm_process_string_literal(vm, args[i]));
        continue;
      }

      if (args[i]->value->tag == VAL_IMMEDIATE) {
        armlet_immediate imm = args[i]->value->imm;
        switch (imm.tag) {
        case IMM_BITSTRING: {
          puts(imm.bits);
          continue;
        }
        case IMM_BOOLEAN: {
          puts(imm.boolean ? "TRUE" : "FALSE");
          continue;
        }
        case IMM_FLOAT: {
          printf("%f\n", imm.real);
          continue;
        }
        case IMM_INTEGER: {
          ARMLET_STRING(s) = mpz_get_str(NULL, 10, imm.integer);
          puts(s);
          continue;
        }
        case IMM_STRING: {
          printf("%s\n", imm.string);
          continue;
        }
        }
      }

      armlet_vm_named_array *na =
          armlet_vm_resolve_var(vm, args[i]->value, SEM_GET, args[i], false);

      for (size_t j = 0; j < na->num_items; ++j) {
        armlet_vm_var_named *vn = na->items[j];
        armlet_vm_var *v = vn->var;

        puts(armlet_vm_var_to_string(v, false));
      }
    } else {
      armlet_vm_var *v = armlet_vm_eval_value(vm, args[i]);
      puts(armlet_vm_var_to_string(v, false));
    }
  }
});

ARMLET_BUILTIN_DEFINE(inspect, {
  ft_table_t *table = ft_create_table();

  ft_set_border_style(table, FT_DOUBLE2_STYLE);
  ft_set_cell_prop(table, 0, FT_ANY_COLUMN, FT_CPROP_ROW_TYPE, FT_ROW_HEADER);
  ft_set_cell_prop(table, FT_ANY_ROW, 0, FT_CPROP_CELL_TEXT_STYLE,
                   FT_TSTYLE_INVERTED | FT_TSTYLE_BOLD);
  ft_set_cell_prop(table, 0, 0, FT_CPROP_CELL_TEXT_STYLE, FT_TSTYLE_DEFAULT);
  ft_write_ln(table, "NAME", "TYPE", "SIZE", "VALUE", "REPR");

  for (size_t i = 0; i < num_args; ++i) {
    if (args[i]->type == AST_VALUE) {
      armlet_vm_named_array *na =
          armlet_vm_resolve_var(vm, args[i]->value, SEM_GET, args[i], false);

      for (size_t j = 0; j < na->num_items; ++j) {
        armlet_vm_var_named *vn = na->items[j];
        armlet_vm_var *v = vn->var;

        const char *value_name = armlet_vm_value_to_string(args[i]->value);

        ft_write_ln(table, value_name, armlet_vm_type_to_string(v),
                    armlet_vm_type_size(v), armlet_vm_var_to_string(v, true),
                    armlet_vm_var_to_bits(v));
      }
    } else {
      armlet_runtime_error(vm, args[i]->source,
                           "Argument to print must be a value, got: %s",
                           armlet_ast_node_type_names[args[i]->type]);
    }
  }

  printf("%s", ft_to_string(table));
  ft_destroy_table(table);
});

ARMLET_BUILTIN_DEFINE(set_bits_range_name, {
  if (num_args != 3 && num_args != 4) {
    armlet_source_error_n(n,
                          "Built-in function '%s' expects 3 or 4 parameters, "
                          "got: %zu",
                          function_name, num_args);
  }

  armlet_vm_var *target = armlet_vm_eval_value(vm, args[0]);
  if (target == NULL) {
    armlet_source_error_n(args[0], "Variable not found in current scope");
  }
  armlet_builtin_check_arg_type(args[0], function_name, 0, target, T_BITS);

  armlet_vm_var *named = armlet_vm_eval_value(vm, args[1]);
  armlet_builtin_check_arg_type(args[1], function_name, 1, named, T_STRING);

  char *name = named->string;

  ARMLET_VAR(end) = armlet_vm_eval_value(vm, args[2]);
  ARMLET_VAR(start) = num_args == 4 ? armlet_vm_eval_value(vm, args[3]) : end;

  armlet_span *span = NEW0(armlet_span);
  span->start = SAFE_MPZ_GET_UI(start->integer);
  span->end = SAFE_MPZ_GET_UI(end->integer);

  if (target->named_bits == NULL) {
    target->named_bits = NEW0(armlet_vm_named_bits);
    hashtable_new(16, &target->named_bits->ranges);
  }

  if (hashtable_add_str(target->named_bits->ranges, name, span) != 0) {
    char *value_name = armlet_vm_value_to_string(args[0]->value);

    armlet_source_error_n(
        args[1], "Bit span with name '%s' already exist on value '%s'", name,
        value_name);
  }
});

enum armlet_real_rounding {
  ROUND_UP,
  ROUND_DOWN,
};

void armlet_vm_builtin__round(armlet_vm_context *vm, armlet_ast_node *n,
                              const char *fn,
                              enum armlet_real_rounding rounding) {
  armlet_ast_call *call = n->call;
  armlet_ast_node **args = call->parameters;
  size_t num_args = call->num_parameters;

  armlet_builtin_check_argc(n, fn, num_args, 1);

  armlet_vm_var *value = armlet_vm_eval_value(vm, args[0]);
  armlet_builtin_check_arg_type(args[0], fn, 0, value, T_REAL);

  armlet_vm_var *new = armlet_vm_var_new(&TYPE_INTEGER);
  switch (rounding) {
  case ROUND_UP:
    mpz_init_set_d(new->integer, ceilf(value->real));
    break;
  case ROUND_DOWN:
    mpz_init_set_d(new->integer, floorf(value->real));
    break;
  }
  armlet_vm_stack_push(vm->stack, new);
}

ARMLET_BUILTIN_DEFINE(round_up,
                      { armlet_vm_builtin__round(vm, n, function_name, ROUND_UP); });

ARMLET_BUILTIN_DEFINE(round_down,
                      { armlet_vm_builtin__round(vm, n, function_name, ROUND_DOWN); });

ARMLET_BUILTIN_DEFINE(real, {
  armlet_builtin_check_argc(n, function_name, num_args, 1);

  armlet_vm_var *value = armlet_vm_eval_value(vm, args[0]);
  armlet_vm_var *new = armlet_vm_var_new(&TYPE_REAL);

  if (value->type == NULL) {
    armlet_source_error_n(args[0],
                          "Argument 0 to built-in function '%s' has no type",
                          function_name);
  }

  switch (value->type->tag) {
  case T_INTEGER:
    new->real = mpz_get_d(value->integer);
    break;
  case T_REAL:
    new->real = value->real;
    break;
  default:
    armlet_source_error_n(
        n,
        "Built-in function '%s' supports arguments of type T_INTEGER and "
        "T_REAL, got %s",
        function_name, armlet_vm_var_type_name(value));
    break;
  }

  armlet_vm_stack_push(vm->stack, new);
});

static void mpz_log2(mpz_t rop, const mpz_t op) {
  assert(mpz_sgn(op) > 0);

  size_t bits = mpz_sizeinbase(op, 2);

  if (mpz_popcount(op) == 1) {
    mpz_set_ui(rop, bits - 1);
  } else {
    mpz_set_ui(rop, bits);
  }
}

ARMLET_BUILTIN_DEFINE(log2, {
  armlet_builtin_check_argc(n, function_name, num_args, 1);

  armlet_vm_var *value = armlet_vm_eval_value(vm, args[0]);
  armlet_builtin_check_arg_type(args[0], function_name, 0, value, T_INTEGER);

  armlet_vm_var *new = armlet_vm_var_new(&TYPE_INTEGER);
  mpz_init(new->integer);
  mpz_log2(new->integer, value->integer);
  armlet_vm_stack_push(vm->stack, new);
});

ARMLET_BUILTIN_DEFINE(backtrace, { armlet_vm_backtrace(vm); });

ARMLET_BUILTIN_DEFINE(brk, {
  (void)function_name;
  (void)args;
  (void)num_args;
  (void)call;
  if (!vm->debugger_hook && vm->debugger_lazy_init) {
    vm->debugger_lazy_init(vm, vm->debugger_lazy_init_userdata);
  }
  if (vm->debugger_hook) {
    vm->break_requested = true;
  } else {
    fprintf(stderr, "break() called but no debugger is active. "
                    "Run with --debugger flag or in an actual terminal.\n");
  }
});

static armlet_node_source builtin_node_source = {
    .source = {.data = ".", .file = "<builtin>"}, .span = {0, 0}};
static armlet_ast_node builtin_node = {.source = &builtin_node_source,
                                       .type = AST_FUNDEF};

void armlet_vm_add_builtin(armlet_vm_context *vm, const char *name,
                           armlet_builtin builtin) {
  armlet_vm_frame *f = armlet_vm_current_frame(vm);
  armlet_vm_var *var = armlet_vm_var_new(
      armlet_vm_type_new(T_BUILTIN, s_sprintf("builtin <%s>", name), 8));
  var->builtin = builtin;
  armlet_vm_var_named *vn = armlet_vm_named_from_var(var, name);
  armlet_vm_add_symbol(vm, &builtin_node, f->symbols, vn, SEM_SYMBOL_VALUE);
}

void armlet_vm_init_builtins(armlet_vm_context *vm) {
  // Meta functions for IO, debugging and supplementary funcitonality
  armlet_vm_add_builtin(vm, "print", armlet_vm_builtin__print);
  armlet_vm_add_builtin(vm, "inspect", armlet_vm_builtin__inspect);
  armlet_vm_add_builtin(vm, "set_bits_range_name",
                        armlet_vm_builtin__set_bits_range_name);
  armlet_vm_add_builtin(vm, "backtrace", armlet_vm_builtin__backtrace);
  armlet_vm_add_builtin(vm, "break", armlet_vm_builtin__brk);
  armlet_vm_add_builtin(vm, "dispatch", armlet_vm_builtin__dispatch);
  armlet_vm_add_builtin(vm, "bitlayout_to_json",
                        armlet_vm_builtin__bitlayout_to_json);
  armlet_vm_add_builtin(vm, "export_bitlayouts_json",
                        armlet_vm_builtin__export_bitlayouts_json);

  // Ser/De
  armlet_vm_add_builtin(vm, "serialize", armlet_vm_builtin__serialize);
  armlet_vm_add_builtin(vm, "deserialize", armlet_vm_builtin__deserialize);

  // Support for IMPLEMENTATION_DEFINED Ser/De
  armlet_vm_add_builtin(vm, "begin_implementation_defined",
                        armlet_vm_builtin__begin_implementation_defined);
  armlet_vm_add_builtin(vm, "implementation_defined",
                        armlet_vm_builtin__implementation_defined);
  armlet_vm_add_builtin(vm, "end_implementation_defined",
                        armlet_vm_builtin__end_implementation_defined);

  // Math functions
  armlet_vm_add_builtin(vm, "Log2", armlet_vm_builtin__log2);
  armlet_vm_add_builtin(vm, "Real", armlet_vm_builtin__real);
  armlet_vm_add_builtin(vm, "RoundUp", armlet_vm_builtin__round_up);
  armlet_vm_add_builtin(vm, "RoundDown", armlet_vm_builtin__round_down);
}

armlet_vm_var_named *
armlet_vm_resolve_callable_or_fail(armlet_vm_context *vm, armlet_ast_node *name,
                                   enum armlet_callable_preference pref,
                                   armlet_ast_node **params,
                                   size_t num_params) {
  armlet_vm_named_array *na =
      armlet_vm_resolve_var(vm, name->value, SEM_GET, name, false);

  if (na == NULL) {
    armlet_runtime_error(vm, name->source,
                         "Callable '%s' doesn't exist in current scope",
                         armlet_vm_value_to_string(name->value));
  }

  armlet_vm_callable_search *search =
      armlet_vm_callable_search_from(vm, params, num_params);

  armlet_vm_var_named *v = armlet_vm_callable_polymorphic(search, na, pref);

  armlet_vm_callable_search_release(search);
  if (v == NULL) {
    armlet_vm_polymorphic_error(vm, name->value, na, search, name);
  }

  return v;
}

void armlet_vm_polymorphic_error(armlet_vm_context *vm,
                                 armlet_ast_value *callable_name,
                                 armlet_vm_named_array *na,
                                 armlet_vm_callable_search *search,
                                 armlet_ast_node *n) {
  char *name = armlet_vm_value_to_string(callable_name);

  armlet_string_list params = {}, candidates = {};

  ARR_FOREACH(search, evaluated,
              { ARR_APPEND(&params, items, it->type->name); });

  char *call =
      str_join_from_array(", ", (const char **)params.items, params.num_items);

  ARR_FOREACH(na, items, {
    armlet_vm_function *f = it->var->function;
    char *candidate =
        str_join_from_array(", ", (const char **)f->parameter_type_names,
                            f->num_parameter_type_names);
    ARR_APPEND(&candidates, items, s_sprintf("%s(%s)", name, candidate));
  });

  char *candidate_string = str_join_from_array(
      "\n - ", (const char **)candidates.items, candidates.num_items);

  char *msg = s_sprintf("\nCallable signature %s(%s) was not found in "
                        "current scope, candidate signatures were:\n - %s",
                        name, call, candidate_string);
  armlet_runtime_error(vm, n->source, "Callable not found in current scope\n%s",
                       msg);
}

void armlet_vm_assign_array(armlet_vm_context *vm, armlet_vm_var *source,
                            armlet_ast_node *target) {
  armlet_ast_array_access *acc = target->array_access;

  armlet_vm_var_named *var = armlet_vm_resolve_callable_or_fail(
      vm, target->array_access->name, CALLPREF_SETTER, acc->indices,
      acc->num_indices);

  if (armlet_vm_var_is_type(var->var, T_SETTER)) {
    armlet_vm_function *setter = var->var->function;

    if (setter == NULL) {
      armlet_source_error_n(target, "Setter for '%s' is undefined", var->name);
    }

    size_t n_defined_params = setter->def->num_parameters;
    size_t n_params = acc->num_indices;

    if (n_defined_params != n_params) {
      armlet_source_error_n(target,
                            "Getter expected %zu parameters but got %zu",
                            n_defined_params, n_params);
    }

    armlet_callable_call(vm, var->var, acc->indices, source, target);
  } else if (armlet_vm_var_is_type(var->var, T_ARRAY)) {
    armlet_vm_var *dest = var->var;

    if (acc->num_indices != 1) {
      armlet_source_error_n(target,
                            "Attempt to index an array with %zu "
                            "indices, expected 1 index",
                            acc->num_indices);
    }

    ARMLET_VAR(index) = armlet_vm_eval_value(vm, acc->indices[0]);

    size_t offset = SAFE_MPZ_GET_UI(index->integer) - dest->contents_base;

    if (offset >= dest->num_contents) {
      armlet_source_error_n(target, "Index %zu outside of the bounds of array",
                            offset);
    }

    armlet_vm_var *destination = dest->contents[offset];
    armlet_vm_type_check_vars(vm, destination, source, target);
    dest->contents[offset] = ARMLET_INCREF(source);
  } else {
    armlet_source_error_n(target, "Unknown array destination: '%s'",
                          armlet_vm_var_type_name(var->var));
  }
}

void armlet_vm_assign_fieldsel(armlet_vm_context *vm, armlet_ast_node *target,
                               armlet_vm_var *source, armlet_ast_node *n) {
  armlet_ast_fieldselect *fs = target->fieldselect;
  armlet_vm_var_named *inst = armlet_vm_resolve_single(vm, SEM_GET, fs->source);
  if (inst == NULL) {
    armlet_runtime_error(vm, n->source, "Not found in current scope");
  }

  size_t source_size = source->type->size;
  size_t offset = 0;

  ARR_FOREACH(fs, selections, {
    armlet_vm_var_named *field =
        armlet_vm_var_from_instance(inst->var->instance, it->value->name);

    size_t to_get = field->var->type->size;

    if (offset + to_get > source_size) {
      armlet_runtime_error(
          vm, n->source,
          "Not enough bits on the right hand side for field bitslurp");
    }

    memcpy(field->var->bits, source->bits + offset, to_get);

    offset += to_get;
  });

  if (offset != source_size) {
    armlet_emit_source_diagnostic(&n->source->source, n->source->span,
                                  "WARN: Bitslurp had leftover bits");
  }
}

void armlet_vm_assign_bitsel(armlet_vm_context *vm, armlet_vm_var *source,
                             armlet_vm_var *target, armlet_ast_bitselect *bs,
                             armlet_ast_node *target_node) {
  armlet_ast_node *source_node = bs->source;

  if (!armlet_vm_var_is_type(target, T_BITS)) {
    armlet_runtime_error(vm, target_node->source,
                         "Bitselect target has to be 'T_BITS', got: '%s'",
                         armlet_vm_var_type_name(target));
  }

  if (!armlet_vm_var_is_type(source, T_BITS)) {
    armlet_runtime_error(vm, source_node->source,
                         "Bitselect source has to be 'T_BITS', got: '%s'",
                         armlet_vm_var_type_name(source));
  }

  armlet_index_list list = armlet_bitselect_indices(vm, bs);

  size_t r = armlet_vm_var_size(target);
  size_t c = 0;

  ARR_FOREACH(&list, indices, {
    size_t t = r - (it + 1);

    if (t >= target->type->size) {
      armlet_runtime_error(vm, target_node->source,
                           "Bitselect target not long enough");
    }

    if (c >= source->type->size) {
      armlet_runtime_error(vm, source_node->source,
                           "Bitselect not enough bits in source");
    }

    target->bits[t] = source->bits[c++];
  });
}

void armlet_vm_set_tuple_recurse(armlet_vm_context *vm, armlet_ast_node *target,
                                 armlet_vm_var *v, bool expected_const,
                                 armlet_ast_node *n) {
  armlet_ast_tuple *t = target->tuple;

  if (!armlet_vm_var_is_type(v, T_TUPLE)) {
    armlet_source_error_n(n,
                          "Right hand side to a tuple assignment must "
                          "be a tuple, got: '%s'",
                          v->type->name);
  }

  if (t->num_elements != v->num_contents) {
    armlet_source_error_n(
        n,
        "Right hand side (%u) to a tuple assignment must "
        "be of the same cardinality as the left hand side(%u)",
        v->num_contents, t->num_elements);
  }

  ARR_FOREACH(t, elements, {
    if (it->type == AST_VALUE) {
      if (it->value->tag == VAL_NAME) {
        char *name = it->value->name;

        if (streq(name, "-")) {
          continue;
        }
      } else if (it->value->tag == VAL_IMMEDIATE) {
        armlet_source_error_n(n, "Immediate is an invalid lvalue");
      }

      armlet_vm_var_named *vn =
          armlet_vm_resolve_single(vm, SEM_GET_OR_CREATE, it);
      if (vn->var->is_const) {
        armlet_source_error_n(n, "Variable '%s' is constant", vn->name);
      }

      vn->var->is_const = expected_const;
      armlet_vm_var_release(vn->var);
      vn->var = ARMLET_INCREF_VAR(v->contents[i]);
    } else if (it->type == AST_BITSLURP) {
      armlet_vm_var *src = v->contents[i];
      armlet_vm_bitslurp(vm, it, src);
    } else if (it->type == AST_TUPLE) {
      armlet_vm_set_tuple_recurse(vm, it, v->contents[i], expected_const, n);
    } else if (it->type == AST_BITSEL) {
      armlet_ast_bitselect *bs = it->bitselect;

      armlet_vm_var_named *nv =
          armlet_vm_resolve_single(vm, SEM_GET, bs->source);

      if (nv == NULL) {
        armlet_runtime_error(vm, n->source, "Not found in current scope");
      } else if (nv->var->is_const) {
        armlet_source_error_n(n, "Variable '%s' is constant", nv->name);
      }

      armlet_vm_var *target = nv->var;

      armlet_vm_assign_bitsel(vm, v->contents[i], target, bs, it);
    } else if (it->type == AST_FIELDSEL) {
      armlet_vm_assign_fieldsel(vm, it, v->contents[i], n);
    } else if (it->type == AST_ARRAY_ACCESS) {
      armlet_vm_assign_array(vm, v->contents[i], it);
    } else {
      armlet_source_error_n(n, "Unsupported LHS target inside a tuple: '%s'",
                            armlet_ast_node_type_names[it->type]);
    }
  });
}

armlet_vm_type *armlet_vm_make_array(armlet_vm_context *vm,
                                     armlet_ast_node *n) {
  assert(n && n->type == AST_ARRAY);
  assert(n->array && n->array->type && n->array->type->type == AST_TYPE_SPEC);

  armlet_ast_array *array = n->array;

  armlet_vm_type *t = armlet_vm_type_from_type_spec(vm, array->type->type_spec);

  armlet_vm_var *start = armlet_vm_eval_value(vm, array->start);
  armlet_vm_var *end = armlet_vm_eval_value(vm, array->end);

  size_t n_start = 0;
  size_t n_end = 0;

  if (!armlet_vm_type_compare(start->type, end->type)) {
    armlet_runtime_error(vm, n->source,
                         "Array size specification start and end "
                         "types must match, got %s and %s",
                         armlet_vm_var_type_name(start),
                         armlet_vm_var_type_name(end));
  }

  if (!armlet_vm_var_is_type(start, T_INTEGER)) {
    if (!armlet_vm_var_is_type(start, T_ENUMERATION)) {
      armlet_runtime_error(vm, n->source,
                           "Array size specification must be "
                           "T_INTEGER or T_ENUMERATION, got %s",
                           armlet_vm_var_type_name(start));
    } else {
      n_start = start->enum_value->value - 1;
      n_end = end->enum_value->value - 1;
    }
  } else {
    n_start = SAFE_MPZ_GET_UI(start->integer);
    n_end = SAFE_MPZ_GET_UI(end->integer);
  }

  size_t size = (n_end - n_start) + 1;

  armlet_vm_type *vt = armlet_vm_type_new(T_ARRAY, "<array>", size);
  vt->start = n_start;
  vt->end = n_end + 1;
  vt->inner = t;

  return vt;
}

#define ARMLET_MAX_EVAL_DEPTH 1024

armlet_vm_frame *armlet_vm_run(armlet_vm_context *vm, armlet_ast_node *n) {
  if (++vm->eval_depth > ARMLET_MAX_EVAL_DEPTH) {
    armlet_runtime_error(vm, n->source, "Maximum evaluation depth exceeded");
  }

  armlet_vm_frame *frame = armlet_vm_current_frame(vm);

  if (vm->returned != NULL) {
    vm->eval_depth--;
    return frame;
  }

  if (vm->debugger_hook)
    vm->debugger_hook(vm, n, vm->debugger_userdata);

  switch (n->type) {
  case AST_ASSIGNMENT: {
    armlet_ast_node *target = n->assignment->target;

    bool expected_const = false;

    if (target->type == AST_QUALIFIED_LHS) {
      armlet_ast_qualified *q = target->qualified;

      expected_const = (q->qualifiers & QUALIFIER_CONSTANT) != 0;
      target = q->inner;
    }

    armlet_ast_node *expected_type = NULL;
    if (target->type == AST_VAR_DEF) {
      armlet_ast_var_definition *vd = target->var_def;

      expected_type = armlet_vm_type_alias_replace(vm, vd->type, ALIAS_NAMED);
      target = target->var_def->names[0];
    }

    switch (target->type) {
    case AST_TUPLE: {
      armlet_ast_node *s = n->assignment->source;

      ARMLET_VAR(v) = armlet_vm_eval_value(vm, s);

      armlet_vm_set_tuple_recurse(vm, target, v, expected_const, n);
      break;
    }
    case AST_BITSLURP: {
      ARMLET_VAR(v) = armlet_vm_eval_value(vm, n->assignment->source);

      armlet_vm_bitslurp(vm, target, v);
      break;
    }
    case AST_VALUE: {
      ARMLET_VAR(v) = armlet_vm_eval_value(vm, n->assignment->source);

      v->is_const = expected_const;

      if (expected_type) {
        armlet_vm_type_spec_check(vm, expected_type, v, n->assignment->source);
      }

      switch (target->value->tag) {
      case VAL_NAME: {
        const char *name = target->value->name;
        // printf("Defining: %s (%p)\n", name, armlet_vm_current_frame(vm));

        armlet_vm_named_array *va = armlet_vm_get_named(vm, name);
        armlet_vm_var_named *existing = armlet_vm_named_single(va);

        if (existing) {
          if (existing->var->is_const) {
            armlet_source_error_n(
                n, "Existing constant variable '%s' cannot be overwritten",
                name);
          }

          armlet_vm_type_check_vars(vm, existing->var, v, n);
        }

        armlet_vm_var_named *var = armlet_vm_named_from_var(v, name);
        armlet_vm_set_local(vm, var, target);
        break;
      }
      case VAL_DEREF: {
        const armlet_dereference *deref = &target->value->deref;

        armlet_vm_named_array *va =
            armlet_vm_dereference(vm, deref, SEM_GET_OR_CREATE, target, false);
        armlet_vm_var_named *named = armlet_vm_named_single(va);

        if (named->var->type != NULL) {
          if (named->var->is_const) {
            armlet_source_error_n(
                n, "Constant variable '%s' cannot be overwritten", named->name);
          }
          armlet_vm_type_check_vars(vm, named->var, v, n);
        }

        if (named->var != NULL && named->var->is_bits_ref) {
          memcpy(named->var->bits_ref_target, v->bits, v->type->size);
          armlet_vm_named_array_release(va);
        } else {
          named->var = ARMLET_INCREF(v);
          named->var->is_const = expected_const;
        }

        break;
      }
      default:
        armlet_runtime_error(vm, target->source,
                             "Immediate is not a valid assignment target");
        break;
      }
      break;
    }
    case AST_BITSEL: {
      armlet_ast_bitselect *bs = target->bitselect;

      ARMLET_VAR(dest) = armlet_vm_eval_value(vm, bs->source);
      ARMLET_VAR(source) = armlet_vm_eval_value(vm, n->assignment->source);

      armlet_vm_assign_bitsel(vm, source, dest, bs, target);
      break;
    }
    case AST_FIELDSEL: {
      ARMLET_VAR(source) = armlet_vm_eval_value(vm, n->assignment->source);

      armlet_vm_assign_fieldsel(vm, target, source, n);
      break;
    }
    case AST_ARRAY_ACCESS: {
      ARMLET_VAR(source) = armlet_vm_eval_value(vm, n->assignment->source);

      armlet_vm_assign_array(vm, source, target);
      break;
    }
    default:
      armlet_source_error(&n->source->source, n->source->span,
                          "Unknown lvalue: '%s",
                          armlet_ast_node_type_names[target->type]);
      break;
    }
    break;
  }
  case AST_BITSEL: {
    armlet_ast_bitselect *bs = n->bitselect;

    ARMLET_VAR(src) = armlet_vm_eval_value(vm, bs->source);
    enum armlet_vm_var_tag t = src->type->tag;
    armlet_index_list list = armlet_bitselect_indices(vm, bs);

    if (t != T_BITS) {
      if (t == T_INTEGER) {
        size_t max_index = 0;
        ARR_FOREACH(&list, indices,
                    { max_index = it > max_index ? it : max_index; });
        src = armlet_vm_var_integer_to_bits(src, max_index + 1);
      } else {
        armlet_runtime_error(vm, n->source,
                             "Invalid source for bitselect: '%s'",
                             armlet_vm_var_tag_names[t]);
      }
    }

    size_t r = armlet_vm_var_size(src);
    uint8_t *selected = CHECKED_MALLOC(r + 1);

    size_t c = 0;

    ARR_FOREACH_REVERSE(&list, indices, {
      size_t t = r - (it + 1);

      selected[c++] = src->bits[t];
    });

    selected[c] = '\0';
    str_reverse(selected);

    armlet_vm_var *result = armlet_vm_var_new(armlet_vm_make_bitstring_type(c));
    result->bits = selected;

    armlet_vm_stack_push(vm->stack, result);
    break;
  }
  case AST_TUPLE: {
    armlet_ast_tuple *t = n->tuple;

    armlet_vm_var *tuple = armlet_vm_var_new(
        armlet_vm_type_new(T_TUPLE, "tuple", t->num_elements));

    ARR_FOREACH(t, elements,
                { ARR_APPEND(tuple, contents, armlet_vm_eval_value(vm, it)); });

    armlet_vm_stack_push(vm->stack, tuple);
    break;
  }
  case AST_VALUE: {
    const armlet_ast_value *v = n->value;
    armlet_vm_var *result;

    switch (v->tag) {
    case VAL_DEREF: {
      const armlet_dereference *deref = &v->deref;

      armlet_vm_var_named *named = armlet_vm_named_single(
          armlet_vm_dereference(vm, deref, SEM_GET, n, false));

      if (named == NULL) {
        armlet_runtime_error(vm, n->source, "Invalid dereference");
      }

      result = named->var;

      if (result != NULL && result->type->tag == T_GETTER) {
        armlet_callable_call(vm, result, NULL, NULL, n);
        result = armlet_vm_stack_pop(vm->stack);
      }

      result = ARMLET_INCREF(result);
      break;
    }

    case VAL_IMMEDIATE: {
      result = ARMLET_DECREF(armlet_vm_var_new(NULL));

      switch (v->imm.tag) {
      case IMM_BITSTRING: {
        result->type = armlet_vm_make_bitstring_type(strlen(v->imm.bits));
        result->bits = (uint8_t *)strdup(v->imm.bits);
        break;
      }
      case IMM_FLOAT: {
        result->type = &TYPE_REAL;
        result->real = v->imm.real;
        break;
      }
      case IMM_INTEGER: {
        result->type = &TYPE_INTEGER;
        mpz_set(result->integer, v->imm.integer);
        break;
      }
      case IMM_BOOLEAN: {
        result->type = &TYPE_BOOLEAN;
        result->boolean = v->imm.boolean;
        break;
      }
      case IMM_STRING: {
        result->type =
            armlet_vm_type_new(T_STRING, "string", strlen(v->imm.string));
        result->string = v->imm.string;
        break;
      }
      }
      break;
    }

    case VAL_NAME: {
      result = armlet_vm_get_var(vm, v->name);

      if (result != NULL && result->type->tag == T_GETTER) {
        armlet_callable_call(vm, result, NULL, NULL, n);
        result = armlet_vm_stack_pop(vm->stack);
      }

      if (result == NULL) {
        armlet_source_error_n(n, "Variable '%s' doesn't exist in current scope",
                              v->name);
      } else {
        result = ARMLET_INCREF(result);
      }
      break;
    }
    }

    armlet_vm_stack_push(vm->stack, result);
    break;
  }
  case AST_CMP: {
    armlet_ast_cmp *cmp = n->cmp;

    ARMLET_VAR(a) = armlet_vm_eval_value(vm, cmp->left);
    ARMLET_VAR(b) = armlet_vm_eval_value(vm, cmp->right);

    armlet_vm_var *r = armlet_vm_var_new(&TYPE_BOOLEAN);

    switch (cmp->op) {
    case CMP_IN: {
      if (!armlet_vm_var_is_type(b, T_SET)) {
        armlet_source_error_n(
            n, "Right operand of the IN operator must be a set, got: '%s'",
            b->type->name);
      }
      ARR_FOREACH(b, contents, {
        if (armlet_vm_compare(it, a, CMP_EQ, n)) {
          r->boolean = true;
          break;
        }
      });
      break;
    }
    default: {
      if (armlet_vm_var_is_type(a, T_ARRAY) ||
          armlet_vm_var_is_type(b, T_TUPLE)) {
        r->boolean = armlet_vm_compare_sequence(a, b, cmp->op, n);
      } else {
        r->boolean = armlet_vm_compare(a, b, cmp->op, n);
      }
      break;
    }
    }

    armlet_vm_stack_push(vm->stack, r);
    break;
  }
  case AST_BINOP: {
    ARMLET_VAR(a) = armlet_vm_eval_value(vm, n->binop->left);
    ARMLET_VAR(b) = armlet_vm_eval_value(vm, n->binop->right);

    switch (n->binop->op) {
    case BINOP_CONCAT: {
      armlet_vm_var *result = armlet_vm_var_new(NULL);

      if (armlet_vm_var_is_type(a, T_BITS)) {
        if (!armlet_vm_var_is_type(b, T_BITS)) {
          armlet_runtime_error(vm, n->source,
                               "Argument of the ':' concat operator "
                               "must be T_BITS got: %s",
                               armlet_vm_var_type_name(b));
        }

        result->bits = concat(a->bits, b->bits);
        result->type =
            armlet_vm_make_bitstring_type(strlen((const char *)result->bits));
      } else if (armlet_vm_var_is_type(a, T_INTEGER)) {
        if (!armlet_vm_var_is_type(b, T_INTEGER)) {
          armlet_runtime_error(vm, n->source,
                               "Argument of the ':' range operator must "
                               "be T_INTEGER got: %s",
                               armlet_vm_var_type_name(b));
        }

        result->range_start = SAFE_MPZ_GET_UI(a->integer);
        result->range_end = SAFE_MPZ_GET_UI(b->integer);
        result->type =
            armlet_vm_make_range_type(result->range_start, result->range_end);
      } else {
        armlet_runtime_error(vm, n->source,
                             "Argument of the ':' operator must be %s or %s",
                             armlet_vm_var_tag_names[T_BITS],
                             armlet_vm_var_tag_names[T_INTEGER]);
      }

      armlet_vm_stack_push(vm->stack, result);
      break;
    }
    case BINOP_OFF_CONCAT: {
      armlet_vm_var *result = armlet_vm_var_new(NULL);

      if (armlet_vm_var_is_type(a, T_INTEGER)) {
        if (!armlet_vm_var_is_type(b, T_INTEGER)) {
          armlet_runtime_error(
              vm, n->source,
              "Argument of the '+:' range operator must be %s got: %s",
              armlet_vm_var_tag_names[T_INTEGER], armlet_vm_var_type_name(b));
        }

        result->range_start =
            (SAFE_MPZ_GET_UI(a->integer) + SAFE_MPZ_GET_UI(b->integer)) - 1;
        result->range_end = SAFE_MPZ_GET_UI(a->integer);
        result->type =
            armlet_vm_make_range_type(result->range_start, result->range_end);
      } else {
        armlet_runtime_error(vm, n->source,
                             "Argument of the '+:' operator must be %s",
                             armlet_vm_var_tag_names[T_INTEGER]);
      }

      armlet_vm_stack_push(vm->stack, result);
      break;
    }
    default: {
      armlet_vm_binop_impl(vm, a, b, n->binop->op, n);
      break;
    }
    }
    break;
  }
  case AST_VAR_DEF: {
    armlet_ast_type_spec *ts = armlet_type_spec_from_vardef(n);

    armlet_vm_type *t = armlet_vm_type_from_type_spec(vm, ts);

    ARR_FOREACH(n->var_def, names, {
      if (it->type != AST_VALUE || it->value->tag != VAL_NAME) {
        armlet_source_error_n(n, "Variable definition must be a name");
      }

      ARMLET_VAR(v) = armlet_vm_var_new(t);
      armlet_vm_var_init(vm, v, t->tag == T_TYPE ? t->name : NULL, INIT_ZERO,
                         n);

      armlet_vm_var_named *var = armlet_vm_named_from_var(v, it->value->name);

      armlet_vm_set_local(vm, var, n);
    });
    break;
  }
  case AST_TYPE: {
    armlet_ast_type_definition *t = n->type_def;

    armlet_vm_custom_type *ct = NEW0(armlet_vm_custom_type);
    ct->name = t->name;

    ARR_FOREACH(t, fields, {
      armlet_ast_parameter *param = it->parameter;
      armlet_vm_parameter *vm_param = NEW0(armlet_vm_parameter);

      if (param->type->type == AST_TYPE_SPEC) {
        armlet_vm_type *type =
            armlet_vm_type_from_type_spec(vm, param->type->type_spec);

        vm_param->type = type;
        vm_param->name = param->name->value->name;
      } else if (param->type->type == AST_ARRAY) {
        armlet_vm_type *array_type = armlet_vm_make_array(vm, param->type);
        vm_param->type = array_type;

        if (param->type->array->name->value->tag != VAL_NAME) {
          armlet_source_error_n(param->type->array->name,
                                "Array type member name must be a valid name");
        }

        vm_param->name = param->type->array->name->value->name;
      } else {
        armlet_source_error_n(it, "Unknown field type: '%s'",
                              armlet_ast_node_type_names[param->type->type]);
      }

      ARR_APPEND(ct, fields, vm_param);
    });

    armlet_vm_var *v = NEW0(armlet_vm_var);
    v->type = armlet_vm_make_custom_type(ct->name, t->num_fields);
    v->custom_type = ct;
    ct->type = v->type;
    armlet_vm_var_named *named = armlet_vm_named_from_var(v, ct->name);
    armlet_vm_set_local(vm, named, n);
    break;
  }
  case AST_RETURN: {
    armlet_ast_return *ret = n->return_;
    if (ret->return_ != NULL) {
      armlet_vm_stack_push(vm->stack, armlet_vm_eval_value(vm, ret->return_));
    }
    vm->returned = n;
    break;
  }
  case AST_CALL: {
    armlet_ast_call *c = n->call;

    armlet_vm_var_named *vn = armlet_vm_resolve_callable_or_fail(
        vm, c->name, CALLPREF_ANY, c->parameters, c->num_parameters);

    armlet_vm_var *v = vn->var;

    if (armlet_vm_var_is_type(v, T_TYPE)) {
      armlet_vm_custom_type *t = v->context;
      armlet_vm_instance *instance = NEW0(armlet_vm_instance);
      instance->class = t;

      armlet_vm_var *r = armlet_vm_var_new(armlet_vm_make_instance_type(t));
      r->instance = instance;
      (void)hashtable_new(32, &instance->fields);

      size_t p = 0;
      ARR_FOREACH(t, fields, {
        armlet_vm_var_named *named;
        armlet_vm_var *var;

        if (p < c->num_parameters) {
          var = armlet_vm_eval_value(vm, c->parameters[p]);
          armlet_vm_type_check(vm, var->type, it->type, n);
          p++;
        } else {
          var = armlet_vm_var_new(it->type);
        }

        named = armlet_vm_named_from_var(var, it->name);

        hashtable_add_str(instance->fields, named->name, named);
      });

      armlet_vm_stack_push(vm->stack, r);
    } else if (armlet_vm_var_is_type(v, T_FUNCTION)) {
      size_t n_defined_params = v->function->def->num_parameters;
      size_t n_params = c->num_parameters;

      if (n_defined_params != n_params) {
        armlet_source_error_n(n, "Getter expected %zu parameters but got %zu",
                              n_defined_params, n_params);
      }

      armlet_callable_call(vm, v, c->parameters, NULL, n);
    } else if (armlet_vm_var_is_type(v, T_BUILTIN)) {
      v->builtin(vm, n);
    } else if (armlet_vm_var_is_type(v, T_BITLAYOUT)) {
      if (c->num_parameters != 1) {
        armlet_source_error_n(
            n, "Bitlayout constructor %s expects one parameter, got %zu",
            vn->name, c->num_parameters);
      }

      armlet_vm_var *var = armlet_vm_eval_value(vm, c->parameters[0]);

      if (armlet_vm_var_is_type(var, T_INTEGER)) {
        armlet_vm_var *old = var;
        var = armlet_vm_var_integer_to_bits(var, v->bitlayout->total);
        armlet_vm_var_release(ARMLET_DECREF(old));
      }

      if (!armlet_vm_var_is_type(var, T_BITS)) {
        armlet_runtime_error(vm, n->source,
                             "Argument to bitlayout constructor must be "
                             "T_INTEGER or T_BITS, got %s",
                             armlet_vm_var_type_name(var));
      }

      if (v->bitlayout->total != var->type->size) {
        armlet_runtime_error(
            vm, n->source,
            "Bitlayout constructor '%s' expects %zu bits, got %zu bits",
            vn->name, v->bitlayout->total, var->type->size);
      }

      armlet_vm_var *new = armlet_vm_var_new(ARMLET_INCREF(var->type));

      new->bits = (uint8_t *)strdup((const char *)var->bits);
      new->named_bits = NEW0(armlet_vm_named_bits);
      hashtable_new(8, &new->named_bits->ranges);

      armlet_vm_apply_bitlayout_members(vm, new, v->bitlayout, var->bits,
                                        c->parameters[0]->source);

      armlet_vm_stack_push(vm->stack, new);
    }
    break;
  }
  case AST_FUNDEF: {
    armlet_vm_define_callable(vm, n);
    break;
  }
  case AST_ENUM: {
    armlet_ast_enum_definition *def = n->enum_def;

    armlet_vm_type *enum_type =
        armlet_vm_make_enum_type(def->name, def->num_elements);

    armlet_vm_var *enum_type_var = armlet_vm_var_new(enum_type);

    armlet_vm_set_local(vm, armlet_vm_named_from_var(enum_type_var, def->name),
                        n);

    // value of 0 is reserved for unset/invalid state
    // such that MyEnum val; will equal 0
    size_t v = 1;
    ARR_FOREACH(def, elements, {
      armlet_vm_type *element_type =
          armlet_vm_make_enum_instance_type(enum_type, v);
      armlet_vm_var *element_var = armlet_vm_var_new(element_type);

      armlet_vm_var_set_enum(element_var, v++, it, enum_type);
      armlet_vm_set_local(vm, armlet_vm_named_from_var(element_var, it), n);
    });
    break;
  }
  case AST_SET: {
    armlet_ast_set *s = n->set;
    armlet_vm_type *t = armlet_vm_type_new(T_SET, "set", s->num_members);
    armlet_vm_var *set = armlet_vm_var_new(t);
    armlet_vm_type *last_type = NULL;

    ARR_FOREACH(s, members, {
      armlet_vm_var *v = armlet_vm_eval_value(vm, it);

      if (last_type != NULL) {
        if (!armlet_vm_type_compare(last_type, v->type)) {
          armlet_source_error_n(n, "Types in a set must match");
        }
      }

      last_type = v->type;

      ARR_APPEND(set, contents, v);
    });

    armlet_vm_stack_push(vm->stack, set);
    break;
  }
  case AST_INLINE_IF: {
    armlet_ast_inline_if *iif = n->inline_if;

    bool matched = false;
    ARR_FOREACH(iif, conditions, {
      armlet_vm_var *cond = armlet_vm_valid_condition(vm, it->condition);

      if (cond->boolean) {
        armlet_vm_stack_push(vm->stack,
                             armlet_vm_eval_value(vm, it->consequence));
        matched = true;
        break;
      }
    });

    if (!matched && iif->alternative != NULL) {
      armlet_vm_stack_push(vm->stack,
                           armlet_vm_eval_value(vm, iif->alternative));
    }
    break;
  }
  case AST_IF: {
    armlet_ast_if *iif = n->if_;

    bool matched = false;
    ARR_FOREACH(iif, conditions, {
      armlet_vm_var *cond = armlet_vm_valid_condition(vm, it->condition);

      if (cond->boolean) {
        armlet_vm_eval(vm, it->consequence);
        matched = true;
        break;
      }
    });

    if (!matched && iif->alternative != NULL) {
      armlet_vm_eval(vm, iif->alternative);
    }
    break;
  }
  case AST_WHEN: {
    armlet_ast_case *c = n->case_;
    armlet_vm_var *match = armlet_vm_eval_value(vm, c->match);

    bool matched = false;

    ARR_FOREACH(c, cases, {
      armlet_vm_var *cond = armlet_vm_eval_value(vm, it->condition);

      if (armlet_vm_compare(match, cond, CMP_EQ, n)) {
        armlet_vm_eval(vm, it->consequence);
        matched = true;
        break;
      }
    });

    if (!matched && c->otherwise) {
      armlet_vm_eval(vm, c->otherwise);
    }
    break;
  }
  case AST_LOOP: {
    armlet_ast_loop *loop = n->loop;
    switch (loop->loop_type) {
    case LOOP_REPEAT: {
      for (;;) {
        armlet_vm_eval(vm, loop->block);

        armlet_vm_var *cond = armlet_vm_valid_condition(vm, loop->condition);

        if (!cond->boolean || vm->returned != NULL) {
          break;
        }
      }
      break;
    }
    case LOOP_WHILE: {
      for (;;) {
        armlet_vm_var *cond = armlet_vm_valid_condition(vm, loop->condition);

        if (cond->boolean && vm->returned == NULL) {
          armlet_vm_eval(vm, loop->block);
        } else {
          break;
        }
      }
      break;
    }
    case LOOP_FOR_DOWNTO:
    case LOOP_FOR_TO: {
      char *loop_var = loop->range->name->value->name;

      armlet_vm_var *v = armlet_vm_eval_value(vm, loop->range->start);

      armlet_vm_set_local(vm, armlet_vm_named_from_var(v, loop_var), n);

      armlet_vm_var *end = armlet_vm_eval_value(vm, loop->range->end);

      for (;;) {
        armlet_vm_eval(vm, loop->block);

        if (mpz_cmp(v->integer, end->integer) == 0 || vm->returned != NULL)
          break;

        if (loop->loop_type == LOOP_FOR_TO) {
          mpz_add_ui(v->integer, v->integer, 1);
        } else {
          mpz_sub_ui(v->integer, v->integer, 1);
        }
      }
      break;
    }
    }
    break;
  }
  case AST_BLOCK: {
    armlet_ast_block *block = n->block;
    ARR_FOREACH(block, nodes, { armlet_vm_eval(vm, it); });
    break;
  }
  case AST_SUITE: {
    ARR_FOREACH(n, suite, { armlet_vm_eval(vm, it); });
    break;
  }
  case AST_FIELDSEL: {
    armlet_ast_fieldselect *fs = n->fieldselect;

    armlet_vm_var_named *inst =
        armlet_vm_resolve_single(vm, SEM_GET, fs->source);

    if (inst == NULL) {
      armlet_runtime_error(vm, n->source, "Not found in current scope");
    }

    uint8_t *buf = calloc(1, 1);
    if (buf == NULL) {
      BAIL("Out of memory: %s:%d\n", __FILE__, __LINE__);
    }

    ARR_FOREACH(fs, selections, {
      const char *name = it->value->name;

      armlet_vm_var_named *field =
          armlet_vm_var_from_instance(inst->var->instance, name);

      if (field == NULL || field->var == NULL) {
        armlet_source_error_n(it, "Field '%s' not found", name);
      }

      if (!armlet_vm_var_is_type(field->var, T_BITS)) {
        armlet_source_error_n(it, "Field '%s' is not of type T_BITS", name);
      }

      buf = concat_owned(buf, field->var->bits);
    });

    armlet_vm_var *var = armlet_vm_var_new(
        armlet_vm_make_bitstring_type(strlen((const char *)buf)));

    var->bits = buf;

    armlet_vm_stack_push(vm->stack, var);
    break;
  }
  case AST_ARRAY: {
    armlet_vm_type *array_type = armlet_vm_make_array(vm, n);
    armlet_vm_type *t = array_type->inner;

    armlet_vm_var *arr = armlet_vm_var_new(array_type);
    arr->contents_base = array_type->start;
    arr->contents_type = t;

    char *inner_name = t->tag == T_TYPE ? t->inner->name : NULL;

    for (size_t i = 0; i < array_type->size; ++i) {
      armlet_vm_var *item = armlet_vm_var_new(ARMLET_INCREF(t));
      armlet_vm_var_init(vm, item, inner_name, INIT_ZERO, n);
      ARR_APPEND(arr, contents, ARMLET_INCREF(item));
    }

    armlet_vm_var_named *var =
        armlet_vm_resolve_single(vm, SEM_CREATE, n->array->name);
    var->var = arr;

    // TODO: Use armlet_vm_resolve_single to support dereferences
    // armlet_vm_set_local(vm, var, n);
    break;
  }
  case AST_ARRAY_ACCESS: {
    armlet_ast_array_access *acc = n->array_access;

    armlet_vm_var_named *v = armlet_vm_resolve_callable_or_fail(
        vm, acc->name, CALLPREF_GETTER, acc->indices, acc->num_indices);

    if (armlet_vm_var_is_type(v->var, T_ARRAY)) {
      armlet_vm_var *vindex = armlet_vm_eval_value(vm, acc->indices[0]);

      size_t index = 0;
      if (!armlet_vm_var_is_type(vindex, T_INTEGER)) {
        if (armlet_vm_var_is_type(vindex, T_ENUMERATION)) {
          index = vindex->enum_value->value - 1;
        } else {
          armlet_runtime_error(vm, n->source,
                               "Array index must be of type T_INTEGER "
                               "or T_ENUMERATION, got: %s",
                               armlet_vm_var_type_name(vindex));
        }
      } else {
        index = SAFE_MPZ_GET_UI(vindex->integer) - v->var->contents_base;
      }

      if (index >= v->var->num_contents) {
        armlet_source_error_n(n, "Index outside of the bounds of an array");
      }

      armlet_vm_stack_push(vm->stack, ARMLET_INCREF(v->var->contents[index]));
    } else if (armlet_vm_var_is_type(v->var, T_GETTER)) {
      size_t n_defined_params = v->var->function->def->num_parameters;
      size_t n_params = acc->num_indices;

      if (n_defined_params != n_params) {
        armlet_source_error_n(n, "Getter expected %zu parameters but got %zu",
                              n_defined_params, n_params);
      }

      armlet_callable_call(vm, v->var, acc->indices, NULL, n);
    } else if (armlet_vm_var_is_type(v->var, T_BITS)) {
      armlet_vm_var *vindex = armlet_vm_eval_value(vm, acc->indices[0]);
      size_t index = SAFE_MPZ_GET_UI(vindex->integer);
      size_t src_size = v->var->type->size;

      if (index >= src_size) {
        armlet_runtime_error(vm, n->source,
                             "Index outside of bounds, got: %zu, max: %zu",
                             index, src_size - 1);
      }

      armlet_vm_var *result = armlet_vm_var_new(&TYPE_BIT);
      result->bits = calloc(1, 2);
      if (!result->bits)
        BAIL("Out of memory: %s:%d\n", __FILE__, __LINE__);
      result->bits[0] = v->var->bits[(src_size - 1) - index];

      armlet_vm_stack_push(vm->stack, result);
    } else {
      armlet_source_error_n(n, "Attempt to array-access into: '%s'",
                            armlet_vm_var_type_name(v->var));
    }
    break;
  }
  case AST_UNARY: {
    armlet_ast_unary *unary = n->unary;
    armlet_vm_var *v = armlet_vm_eval_value(vm, unary->node);

    switch (unary->op) {
    case UNARY_MINUS: {
      switch (v->type->tag) {
      case T_INTEGER:
        mpz_neg(v->integer, v->integer);
        break;
      case T_REAL:
        v->real = -v->real;
        break;
      default:
        armlet_runtime_error(vm, n->source,
                             "Unary - is not valid for '%s', expected "
                             "T_INTEGER or T_REAL",
                             armlet_vm_var_type_name(v));
      }
      break;
    }
    case UNARY_NEGATION: {
      if (armlet_vm_var_is_type(v, T_BOOLEAN)) {
        v->boolean = !v->boolean;
      } else {
        armlet_runtime_error(
            vm, n->source, "Unary ! is not valid for '%s', expected T_BOOLEAN",
            armlet_vm_var_type_name(v));
      }
      break;
    }
    }

    armlet_vm_stack_push(vm->stack, v);
    break;
  }
  case AST_ASSERT: {
    armlet_ast_assert *assert = n->assert;
    ARMLET_VAR(result) = armlet_vm_eval_value(vm, assert->condition);

    if (!armlet_vm_var_is_type(result, T_BOOLEAN)) {
      armlet_source_error_n(n, "Assert condition must be T_BOOLEAN, got '%s'",
                            armlet_vm_var_type_name(result));
    }

    if (!result->boolean) {
      armlet_runtime_error(vm, n->source, "Assertion Failed");
    }
    break;
  }
  case AST_TYPE_ALIAS: {
    armlet_ast_type_alias *ta = n->type_alias;

    armlet_vm_var_named *vn =
        armlet_vm_resolve_single(vm, SEM_CREATE, ta->from);

    armlet_vm_var *var = vn->var;

    var->type = armlet_vm_type_new(T_TYPE_ALIAS, "alias", 16);

    var->type_alias = NEW0(armlet_vm_type_alias);

    var->type_alias->from = ta->from;
    var->type_alias->to = armlet_vm_type_alias_replace(vm, ta->to, ALIAS_ANY);
    break;
  }
  case AST_IMPORT: {
    armlet_ast_node *imported =
        armlet_parse_import(n, vm->imported_files, vm->config.debug);
    if (imported != NULL)
      armlet_vm_eval(vm, imported);
    break;
  }
  case AST_USE: {
    armlet_ast_use *use = n->use;

    if (use->target->type != AST_VALUE) {
      armlet_source_error_n(
          n, "Use statement paremeter must be AST_VALUE, got: %s",
          armlet_ast_node_type_names[use->target->type]);
    }

    armlet_vm_named_array *na =
        armlet_vm_resolve_var(vm, use->target->value, SEM_GET, n, false);
    armlet_vm_var_named *vn = armlet_vm_named_single(na);

    if (vn == NULL) {
      armlet_source_error_n(n, "Name '%s' doesn't exist in current scope",
                            armlet_vm_value_to_string(use->target->value));
    }

    armlet_vm_var *v = vn->var;

    if (armlet_vm_var_is_type(v, T_BITS)) {
      if (v->named_bits == NULL) {
        armlet_source_error_n(n, "Can't use a bits value without named bits");
      }

      HASHTABLE_ITERATE(v->named_bits->ranges, char *, armlet_span *, {
        armlet_vm_var *var = armlet_bits_var_from_span(vm, n, v, value);
        armlet_vm_var_named *named = armlet_vm_named_from_var(var, key);
        armlet_vm_set_local(vm, named, n);
      });
    } else if (armlet_vm_var_is_type(v, T_SCOPE)) {
      armlet_vm_namespace *ns = v->namespace;
      armlet_vm_symbols *sym = ns->symbols;

      HASHTABLE_ITERATE_VALUES(sym->symbols, armlet_vm_named_array *, {
        ARR_FOREACH(value, items, { armlet_vm_set_local(vm, it, n); });
      });
    } else {
      // TODO: Add support for T_INSTANCE?
      armlet_source_error_n(
          n, "Name '%s' exists but its type is not T_SCOPE or T_BITS but %s",
          armlet_vm_value_to_string(use->target->value),
          armlet_vm_var_type_name(v));
    }
    break;
  }
  case AST_ARRAY_LITERAL: {
    armlet_ast_array_literal *lit = n->array_literal;

    uint8_t *acc = calloc(1, 1);
    if (!acc)
      BAIL("Out of memory: %s:%d\n", __FILE__, __LINE__);
    size_t s = 0;

    ARR_FOREACH(lit, members, {
      armlet_vm_var *v = armlet_vm_eval_value(vm, it);

      if (!armlet_vm_var_is_type(v, T_BITS)) {
        armlet_runtime_error(vm, n->source,
                             "Array concantenation literal elements "
                             "must be T_BITS, got: %s",
                             armlet_vm_var_type_name(v));
      }

      s += v->type->size;
      acc = concat_owned(acc, v->bits);
    });

    armlet_vm_var *result = armlet_vm_var_new(armlet_vm_make_bitstring_type(s));
    result->bits = acc;

    armlet_vm_stack_push(vm->stack, result);
    break;
  }
  case AST_UNKNOWN: {
    armlet_ast_unknown *unk = n->unknown;
    armlet_vm_type *t = armlet_vm_type_from_type_spec(vm, unk);

    if (t == NULL) {
      armlet_runtime_error(vm, n->source, "Invalid type name '%s'",
                           armlet_vm_value_to_string(unk->name->value));
    }

    armlet_vm_var *v = armlet_vm_var_new(t);
    armlet_vm_var_init(vm, v, t->tag == T_TYPE ? t->name : NULL, INIT_SOURCE,
                       n);

    armlet_vm_stack_push(vm->stack, v);
    break;
  }
  case AST_IMPLEMENTATION_DEFINED: {
    armlet_ast_implementation_defined *id = n->implementation_defined;

    if (vm->config.implementation_defined.values == NULL) {
      // No -i file: treat as unknown value of the declared type
      armlet_ast_type_spec *ts = id->type->type_spec;
      armlet_vm_type *t = armlet_vm_type_from_type_spec(vm, ts);
      if (t == NULL) {
        armlet_runtime_error(vm, n->source, "Invalid type name '%s'",
                             armlet_vm_value_to_string(ts->name->value));
      }
      armlet_vm_var *v = armlet_vm_var_new(t);
      armlet_vm_var_init(vm, v, t->tag == T_TYPE ? t->name : NULL, INIT_SOURCE,
                         n);
      armlet_vm_stack_push(vm->stack, v);
      break;
    }

    armlet_vm_var *var = NULL;
    if (hashtable_find_str(vm->config.implementation_defined.values, id->key,
                           (void **)&var) != 0) {
      armlet_runtime_error(vm, n->source,
                           "Implementation defined value '%s' not found",
                           id->key);
    }

    armlet_vm_type_spec_check(vm, id->type, var, n);

    armlet_vm_stack_push(vm->stack, var);
    break;
  }
  case AST_TRAP: {
    armlet_ast_trap *trap = n->trap;
    const char *msg = NULL;

    switch (trap->trap_type) {
    case TRAP_SEE: {
      msg = s_sprintf(" see: %s", trap->context);
      break;
    }
    case TRAP_UNPREDICTABLE: {
      msg = ": unpredictable";
      break;
    }
    case TRAP_UNDEFINED: {
      msg = ": undefined";
      break;
    }
    }

    armlet_runtime_error(vm, n->source, "Execution trapped%s", msg);
    break;
  }
  case AST_BITLAYOUT: {
    armlet_ast_bitlayout *layout = n->bitlayout;

    armlet_vm_type *t =
        armlet_vm_type_new(T_BITLAYOUT, layout->name, layout->total);
    armlet_vm_var *v = armlet_vm_var_new(t);

    v->bitlayout = layout;

    if (vm->decoder != NULL) {
      armlet_runtime_error(
          vm, n->source,
          "Bitlayout definition after dispatch() had been called");
    }

    armlet_decoder_builder_add(vm->decoder_builder, layout, n);

    armlet_vm_var_named *vn = armlet_vm_named_from_var(v, layout->name);
    armlet_vm_set_local(vm, vn, n);
    break;
  }
  default: {
    armlet_runtime_error(vm, n->source, "Unhandled node type: '%s'",
                         armlet_ast_node_type_names[n->type]);
    break;
  }
  }

  vm->eval_depth--;
  return frame;
}
