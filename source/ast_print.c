#define _GNU_SOURCE

#include "ast_print.h"
#include "diagnostics.h"
#include "parser.h"
#include "utils/hashtable.h"
#include "utils/string.h"

#include <stdio.h>

static const char *binop_str(enum armlet_vm_binop op) {
  switch (op) {
  case BINOP_ADD:
    return "+";
  case BINOP_SUB:
    return "-";
  case BINOP_MUL:
    return "*";
  case BINOP_DIV:
    return "DIV";
  case BINOP_FDIV:
    return "/";
  case BINOP_MOD:
    return "MOD";
  case BINOP_CONCAT:
    return ":";
  case BINOP_OFF_CONCAT:
    return "+:";
  case BINOP_PWR:
    return "^";
  case BINOP_SHR:
    return ">>";
  case BINOP_SHL:
    return "<<";
  case BINOP_EOR:
    return "EOR";
  case BINOP_AND:
    return "AND";
  case BINOP_OR:
    return "OR";
  default:
    return "?";
  }
}

static const char *cmpop_str(enum armlet_vm_comparison op) {
  switch (op) {
  case CMP_EQ:
    return "==";
  case CMP_NEQ:
    return "!=";
  case CMP_GT:
    return ">";
  case CMP_LT:
    return "<";
  case CMP_GTEQ:
    return ">=";
  case CMP_LTEQ:
    return "<=";
  case CMP_IN:
    return "IN";
  default:
    return "?";
  }
}

static const char *callable_str(enum armlet_callable_type t) {
  switch (t) {
  case CALLABLE_FUNC:
    return "func";
  case CALLABLE_GETTER:
    return "getter";
  case CALLABLE_SETTER:
    return "setter";
  default:
    return "?";
  }
}

static const char *loop_str(enum armlet_loop_tag t) {
  switch (t) {
  case LOOP_FOR_TO:
    return "for..to";
  case LOOP_FOR_DOWNTO:
    return "for..downto";
  case LOOP_WHILE:
    return "while";
  case LOOP_REPEAT:
    return "repeat..until";
  default:
    return "?";
  }
}

static const char *trap_str(enum armlet_vm_trap t) {
  switch (t) {
  case TRAP_UNDEFINED:
    return "UNDEFINED";
  case TRAP_UNPREDICTABLE:
    return "UNPREDICTABLE";
  case TRAP_SEE:
    return "SEE";
  default:
    return "?";
  }
}


typedef struct {
  const armlet_ast_node **buf;
  size_t n;
  size_t cap;
} ChildBuf;

static void cb_add(ChildBuf *cb, const armlet_ast_node *node) {
  if (node == NULL)
    return;
  if (cb->n == cb->cap) {
    cb->cap = cb->cap ? cb->cap * 2 : 8;
    cb->buf = CHECKED_REALLOC(cb->buf, cb->cap, sizeof(*cb->buf));
  }
  cb->buf[cb->n++] = node;
}

static void cb_free(ChildBuf *cb) {
  free(cb->buf);
  cb->buf = NULL;
  cb->n = cb->cap = 0;
}

static void cb_add_vardef(ChildBuf *cb, const armlet_ast_var_definition *vd) {
  if (vd == NULL)
    return;
  cb_add(cb, vd->type);
  for (size_t i = 0; i < vd->num_names; i++) {
    cb_add(cb, vd->names[i]);
  }
}

static void fill_children(const armlet_ast_node *n, ChildBuf *cb,
                          Hashtable *imported_files) {
  switch (n->type) {
  case AST_ENUM:
  case AST_VALUE:
    /* leaf nodes */
    break;
  case AST_CMP:
    cb_add(cb, n->cmp->left);
    cb_add(cb, n->cmp->right);
    break;
  case AST_BINOP:
    cb_add(cb, n->binop->left);
    cb_add(cb, n->binop->right);
    break;
  case AST_UNARY:
    cb_add(cb, n->unary->node);
    break;
  case AST_ASSIGNMENT:
    cb_add(cb, n->assignment->target);
    cb_add(cb, n->assignment->source);
    break;
  case AST_CALL:
    cb_add(cb, n->call->name);
    for (size_t i = 0; i < n->call->num_parameters; i++)
      cb_add(cb, n->call->parameters[i]);
    break;
  case AST_FUNDEF: {
    armlet_ast_callable_definition *cd = n->callable_def;
    if (cd->type == CALLABLE_SETTER) {
      if (cd->input_type != NULL)
        cb_add_vardef(cb, cd->input_type);
    } else {
      cb_add(cb, cd->return_type);
    }
    for (size_t i = 0; i < cd->num_parameters; i++)
      cb_add_vardef(cb, cd->parameters[i]);
    if (cd->body != NULL)
      for (size_t i = 0; i < cd->body->num_nodes; i++)
        cb_add(cb, cd->body->nodes[i]);
    break;
  }
  case AST_BITSEL:
  case AST_BITSEL_RANGE:
    cb_add(cb, n->bitselect->source);
    for (size_t i = 0; i < n->bitselect->num_selections; i++)
      cb_add(cb, n->bitselect->selections[i]);
    break;
  case AST_FIELDSEL:
    cb_add(cb, n->fieldselect->source);
    for (size_t i = 0; i < n->fieldselect->num_selections; i++)
      cb_add(cb, n->fieldselect->selections[i]);
    break;
  case AST_BITSLURP:
    for (size_t i = 0; i < n->bitslurp->num_elements; i++)
      cb_add(cb, n->bitslurp->elements[i]);
    break;
  case AST_IF:
    for (size_t i = 0; i < n->if_->num_conditions; i++) {
      cb_add(cb, n->if_->conditions[i]->condition);
      cb_add(cb, n->if_->conditions[i]->consequence);
    }
    cb_add(cb, n->if_->alternative);
    break;
  case AST_WHEN:
    cb_add(cb, n->case_->match);
    for (size_t i = 0; i < n->case_->num_cases; i++) {
      cb_add(cb, n->case_->cases[i]->condition);
      cb_add(cb, n->case_->cases[i]->consequence);
    }
    cb_add(cb, n->case_->otherwise);
    break;
  case AST_TUPLE:
    for (size_t i = 0; i < n->tuple->num_elements; i++)
      cb_add(cb, n->tuple->elements[i]);
    break;
  case AST_TYPE:
    for (size_t i = 0; i < n->type_def->num_fields; i++)
      cb_add(cb, n->type_def->fields[i]);
    break;
  case AST_RETURN:
    cb_add(cb, n->return_->return_);
    break;
  case AST_LOOP:
    if (n->loop->loop_type == LOOP_FOR_TO ||
        n->loop->loop_type == LOOP_FOR_DOWNTO) {
      cb_add(cb, n->loop->range->name);
      cb_add(cb, n->loop->range->start);
      cb_add(cb, n->loop->range->end);
    } else {
      cb_add(cb, n->loop->condition);
    }
    cb_add(cb, n->loop->block);
    break;
  case AST_ASSERT:
    cb_add(cb, n->assert->condition);
    break;
  case AST_SUITE:
    for (size_t i = 0; i < n->num_suite; i++)
      cb_add(cb, n->suite[i]);
    break;
  case AST_VAR_DEF:
    cb_add_vardef(cb, n->var_def);
    break;
  case AST_TYPE_SPEC:
    cb_add(cb, n->type_spec->name);
    cb_add(cb, n->type_spec->size);
    break;
  case AST_PARAMETER:
    cb_add(cb, n->parameter->type);
    cb_add(cb, n->parameter->name);
    break;
  case AST_SET:
    for (size_t i = 0; i < n->set->num_members; i++)
      cb_add(cb, n->set->members[i]);
    break;
  case AST_BLOCK:
    for (size_t i = 0; i < n->block->num_nodes; i++)
      cb_add(cb, n->block->nodes[i]);
    break;
  case AST_INLINE_IF:
    for (size_t i = 0; i < n->inline_if->num_conditions; i++) {
      cb_add(cb, n->inline_if->conditions[i]->condition);
      cb_add(cb, n->inline_if->conditions[i]->consequence);
    }
    cb_add(cb, n->inline_if->alternative);
    break;
  case AST_ARRAY:
    cb_add(cb, n->array->type);
    cb_add(cb, n->array->name);
    cb_add(cb, n->array->start);
    cb_add(cb, n->array->end);
    break;
  case AST_ARRAY_ACCESS:
    cb_add(cb, n->array_access->name);
    for (size_t i = 0; i < n->array_access->num_indices; i++)
      cb_add(cb, n->array_access->indices[i]);
    break;
  case AST_TYPE_ALIAS:
    cb_add(cb, n->type_alias->from);
    cb_add(cb, n->type_alias->to);
    break;
  case AST_IMPORT: {
    armlet_ast_node *imported = armlet_parse_import(n, imported_files, false);
    if (imported != NULL)
      cb_add(cb, imported);
    break;
  }
  case AST_USE:
    cb_add(cb, n->use->target);
    break;
  case AST_ARRAY_LITERAL:
    for (size_t i = 0; i < n->array_literal->num_members; i++)
      cb_add(cb, n->array_literal->members[i]);
    break;
  case AST_UNKNOWN:
    cb_add(cb, n->unknown->name);
    cb_add(cb, n->unknown->size);
    break;
  case AST_IMPLEMENTATION_DEFINED:
    cb_add(cb, n->implementation_defined->type);
    break;
  case AST_TRAP:
    break;
  case AST_QUALIFIED_LHS:
    cb_add(cb, n->qualified->inner);
    break;
  case AST_BITLAYOUT:
    cb_add(cb, n->bitlayout->handler);
    break;
  }
}

static void tree_node(const armlet_ast_node *n, FILE *out, int flags,
                      const char *cont, bool is_last,
                      Hashtable *imported_files);

static char *value_to_string(armlet_ast_value *v) {
  switch (v->tag) {
  case VAL_NAME: {
    return v->name;
  }
  case VAL_DEREF: {
    return str_join_from_array(".", (const char **)v->deref.names,
                               v->deref.num_names);
  }
  default: {
    return "?";
  }
  }
}

static void tree_print_label(FILE *out, int flags, const armlet_ast_node *n,
                             const char *cont, bool is_last, bool is_root) {
  if (!is_root)
    fprintf(out, "%s%s", cont,
            is_last ? "\xe2\x94\x94\xe2\x94\x80 "   /* └─ */
                    : "\xe2\x94\x9c\xe2\x94\x80 "); /* ├─ */

  fprintf(out, "%s: ", armlet_ast_node_type_names[n->type]);

  switch (n->type) {
  case AST_CMP:
    fprintf(out, "Cmp %s", cmpop_str(n->cmp->op));
    break;
  case AST_BINOP:
    fprintf(out, "BinOp %s", binop_str(n->binop->op));
    break;
  case AST_UNARY:
    fprintf(out, "Unary %s", n->unary->op == UNARY_MINUS ? "-" : "!");
    break;
  case AST_ASSIGNMENT:
    fprintf(out, "Assignment");
    break;
  case AST_CALL:
    fprintf(out, "Call [%zu args]", n->call->num_parameters);
    break;
  case AST_FUNDEF: {
    const armlet_ast_node *nm = n->callable_def->name;
    const char *name =
        (nm && nm->type == AST_VALUE) ? value_to_string(nm->value) : "?";
    fprintf(out, "FunDef '%s' [%s, %zu params]", name,
            callable_str(n->callable_def->type),
            n->callable_def->num_parameters);
    break;
  }
  case AST_BITSEL:
  case AST_BITSEL_RANGE:
    fprintf(out, "BitSelect [%zu selections]", n->bitselect->num_selections);
    break;
  case AST_FIELDSEL:
    fprintf(out, "FieldSelect [%zu selections]",
            n->fieldselect->num_selections);
    break;
  case AST_BITSLURP:
    fprintf(out, "BitSlurp [%zu]", n->bitslurp->num_elements);
    break;
  case AST_IF:
    fprintf(out, "If [%zu branches%s]", n->if_->num_conditions,
            n->if_->alternative ? "+else" : "");
    break;
  case AST_WHEN:
    fprintf(out, "Case [%zu when%s]", n->case_->num_cases,
            n->case_->otherwise ? "+otherwise" : "");
    break;
  case AST_TUPLE:
    fprintf(out, "Tuple [%zu]", n->tuple->num_elements);
    break;
  case AST_VALUE:
    switch (n->value->tag) {
    case VAL_NAME:
      fprintf(out, "Name '%s'", n->value->name);
      break;
    case VAL_DEREF:
      fprintf(out, "Deref '");
      for (size_t i = 0; i < n->value->deref.num_names; i++) {
        if (i)
          fputc('.', out);
        fputs(n->value->deref.names[i], out);
      }
      fputc('\'', out);
      break;
    case VAL_IMMEDIATE:
      switch (n->value->imm.tag) {
      case IMM_INTEGER: {
        char *s = mpz_get_str(NULL, 10, n->value->imm.integer);
        fprintf(out, "Int %s", s);
        free(s);
        break;
      }
      case IMM_FLOAT:
        fprintf(out, "Float %g", (double)n->value->imm.real);
        break;
      case IMM_BITSTRING:
        fprintf(out, "Bits '%s'", n->value->imm.bits);
        break;
      case IMM_BOOLEAN:
        fprintf(out, "Bool %s", n->value->imm.boolean ? "TRUE" : "FALSE");
        break;
      case IMM_STRING:
        fprintf(out, "String \"%s\"", n->value->imm.string);
        break;
      }
      break;
    }
    break;
  case AST_TYPE:
    fprintf(out, "TypeDef '%s' [%zu fields]", n->type_def->name,
            n->type_def->num_fields);
    break;
  case AST_ENUM:
    fprintf(out, "EnumDef '%s' [%zu variants]", n->enum_def->name,
            n->enum_def->num_elements);
    break;
  case AST_RETURN:
    fprintf(out, "Return");
    break;
  case AST_LOOP:
    fprintf(out, "Loop [%s]", loop_str(n->loop->loop_type));
    break;
  case AST_ASSERT:
    fprintf(out, "Assert");
    break;
  case AST_SUITE:
    fprintf(out, "Suite [%zu]", n->num_suite);
    break;
  case AST_VAR_DEF:
    fprintf(out, "VarDef [%zu names]", n->var_def->num_names);
    break;
  case AST_TYPE_SPEC:
    fprintf(out, "TypeSpec%s", n->type_spec->constant ? " [constant]" : "");
    break;
  case AST_PARAMETER:
    fprintf(out, "Param");
    break;
  case AST_SET:
    fprintf(out, "Set [%zu]", n->set->num_members);
    break;
  case AST_BLOCK:
    fprintf(out, "Block [%zu]", n->block->num_nodes);
    break;
  case AST_INLINE_IF:
    fprintf(out, "InlineIf [%zu branches]", n->inline_if->num_conditions);
    break;
  case AST_ARRAY:
    fprintf(out, "Array");
    break;
  case AST_ARRAY_ACCESS:
    fprintf(out, "ArrayAccess [%zu indices]", n->array_access->num_indices);
    break;
  case AST_TYPE_ALIAS:
    fprintf(out, "TypeAlias");
    break;
  case AST_IMPORT:
    fprintf(out, "Import '%s'", n->import->path);
    break;
  case AST_USE:
    fprintf(out, "Use");
    break;
  case AST_ARRAY_LITERAL:
    fprintf(out, "ArrayLiteral [%zu]", n->array_literal->num_members);
    break;
  case AST_UNKNOWN:
    fprintf(out, "Unknown");
    break;
  case AST_IMPLEMENTATION_DEFINED:
    fprintf(out, "ImplDefined '%s'", n->implementation_defined->key);
    break;
  case AST_TRAP:
    if (n->trap->trap_type == TRAP_SEE && n->trap->context)
      fprintf(out, "Trap %s '%s'", trap_str(n->trap->trap_type),
              n->trap->context);
    else
      fprintf(out, "Trap %s", trap_str(n->trap->trap_type));
    break;
  case AST_QUALIFIED_LHS:
    fprintf(out, "Qualified");
    break;
  case AST_BITLAYOUT:
    fprintf(out, "Bitlayout '%s' [%zu bits, %zu members]", n->bitlayout->name,
            n->bitlayout->total, n->bitlayout->num_members);
    break;
  }

  if ((flags & AST_PRINT_SPANS) && n->source) {
    armlet_line_info li =
        armlet_source_line_info_one(&n->source->source, n->source->span);
    fprintf(out, "  [%s:%zu:%zu-%zu]",
            n->source->source.file ? n->source->source.file : "?", li.lineno,
            li.col_start, li.col_end);
  }

  fputc('\n', out);
}

static void tree_node(const armlet_ast_node *n, FILE *out, int flags,
                      const char *cont, bool is_last,
                      Hashtable *imported_files) {
  bool is_root = (cont == NULL);
  if (is_root)
    cont = "";

  tree_print_label(out, flags, n, cont, is_last, is_root);

  char child_cont[4096];
  if (is_root)
    snprintf(child_cont, sizeof(child_cont), "%s", cont);
  else
    snprintf(child_cont, sizeof(child_cont), "%s%s", cont,
             is_last ? "   " : "\xe2\x94\x82  ");

  ChildBuf cb = {NULL, 0, 0};
  fill_children(n, &cb, imported_files);
  for (size_t i = 0; i < cb.n; i++) {
    tree_node(cb.buf[i], out, flags, child_cont, i == cb.n - 1, imported_files);
  }
  cb_free(&cb);
}

static void json_node(const armlet_ast_node *n, FILE *out, int flags, int depth,
                      Hashtable *imported_files);

static void json_indent(FILE *out, int depth) {
  for (int i = 0; i < depth * 2; i++)
    fputc(' ', out);
}

static void json_str(FILE *out, const char *s) {
  if (!s) {
    fputs("null", out);
    return;
  }
  fputc('"', out);
  for (const char *p = s; *p; p++) {
    switch (*p) {
    case '"':
      fputs("\\\"", out);
      break;
    case '\\':
      fputs("\\\\", out);
      break;
    case '\n':
      fputs("\\n", out);
      break;
    case '\r':
      fputs("\\r", out);
      break;
    case '\t':
      fputs("\\t", out);
      break;
    default:
      fputc(*p, out);
      break;
    }
  }
  fputc('"', out);
}

static void jf(FILE *out, bool *first, int depth, const char *key) {
  if (*first)
    *first = false;
  else
    fputs(",\n", out);
  json_indent(out, depth);
  fputc('"', out);
  fputs(key, out);
  fputs("\": ", out);
}

static void jf_node(FILE *out, bool *first, int depth, const char *key,
                    const armlet_ast_node *child, int flags,
                    Hashtable *imported_files) {
  if (child == NULL)
    return;
  jf(out, first, depth, key);
  json_node(child, out, flags, depth, imported_files);
}

static void jf_arr_open(FILE *out, bool *first, int depth, const char *key) {
  jf(out, first, depth, key);
  fputs("[\n", out);
}

static void jf_arr_close(FILE *out, int depth, bool arr_empty) {
  if (!arr_empty)
    fputc('\n', out);
  json_indent(out, depth);
  fputc(']', out);
}

static void jarr_node(FILE *out, bool *first_elem, int depth,
                      const armlet_ast_node *n, int flags,
                      Hashtable *imported_files) {
  if (n == NULL)
    return;
  if (!*first_elem)
    fputs(",\n", out);
  else
    *first_elem = false;
  json_indent(out, depth);
  json_node(n, out, flags, depth, imported_files);
}

static void jarr_str(FILE *out, bool *first_elem, int depth, const char *s) {
  if (!*first_elem)
    fputs(",\n", out);
  else
    *first_elem = false;
  json_indent(out, depth);
  json_str(out, s);
}

static void json_vardef(FILE *out, const armlet_ast_var_definition *vd,
                        int flags, int depth) {
  fputs("{\n", out);
  bool first = true;
  jf_node(out, &first, depth + 1, "type", vd->type, flags, NULL);
  if (vd->num_names == 1) {
    jf_node(out, &first, depth + 1, "name", vd->names[0], flags, NULL);
  } else if (vd->num_names > 1) {
    jf_arr_open(out, &first, depth + 1, "names");
    bool fe = true;
    for (size_t i = 0; i < vd->num_names; i++)
      jarr_node(out, &fe, depth + 2, vd->names[i], flags, NULL);
    jf_arr_close(out, depth + 1, fe);
  }
  fputc('\n', out);
  json_indent(out, depth);
  fputc('}', out);
}

static void json_node(const armlet_ast_node *n, FILE *out, int flags, int depth,
                      Hashtable *imported_files) {
  if (n == NULL) {
    fputs("null", out);
    return;
  }

  fputs("{\n", out);
  bool first = true;

  jf(out, &first, depth + 1, "type");
  json_str(out, armlet_ast_node_type_names[n->type]);

  if ((flags & AST_PRINT_SPANS) && n->source) {
    armlet_line_info li =
        armlet_source_line_info_one(&n->source->source, n->source->span);
    jf(out, &first, depth + 1, "span");
    fprintf(out, "{\"file\": ");
    json_str(out, n->source->source.file);
    fprintf(out, ", \"line\": %zu, \"col_start\": %zu, \"col_end\": %zu}",
            li.lineno, li.col_start, li.col_end);
  }

  switch (n->type) {
  case AST_CMP:
    jf(out, &first, depth + 1, "op");
    json_str(out, cmpop_str(n->cmp->op));
    jf_node(out, &first, depth + 1, "left", n->cmp->left, flags,
            imported_files);
    jf_node(out, &first, depth + 1, "right", n->cmp->right, flags,
            imported_files);
    break;

  case AST_BINOP:
    jf(out, &first, depth + 1, "op");
    json_str(out, binop_str(n->binop->op));
    jf_node(out, &first, depth + 1, "left", n->binop->left, flags,
            imported_files);
    jf_node(out, &first, depth + 1, "right", n->binop->right, flags,
            imported_files);
    break;

  case AST_UNARY:
    jf(out, &first, depth + 1, "op");
    json_str(out, n->unary->op == UNARY_MINUS ? "-" : "!");
    jf_node(out, &first, depth + 1, "operand", n->unary->node, flags, NULL);
    break;

  case AST_ASSIGNMENT:
    jf_node(out, &first, depth + 1, "target", n->assignment->target, flags,
            NULL);
    jf_node(out, &first, depth + 1, "source", n->assignment->source, flags,
            NULL);
    break;

  case AST_CALL:
    jf_node(out, &first, depth + 1, "name", n->call->name, flags, NULL);
    if (n->call->num_parameters > 0) {
      jf_arr_open(out, &first, depth + 1, "args");
      bool fe = true;
      for (size_t i = 0; i < n->call->num_parameters; i++)
        jarr_node(out, &fe, depth + 2, n->call->parameters[i], flags, NULL);
      jf_arr_close(out, depth + 1, fe);
    }
    break;

  case AST_FUNDEF: {
    armlet_ast_callable_definition *cd = n->callable_def;
    jf(out, &first, depth + 1, "kind");
    json_str(out, callable_str(cd->type));
    jf_node(out, &first, depth + 1, "name", cd->name, flags, NULL);
    if (cd->type == CALLABLE_SETTER) {
      if (cd->input_type) {
        jf(out, &first, depth + 1, "input_type");
        json_vardef(out, cd->input_type, flags, depth + 1);
      }
    } else {
      jf_node(out, &first, depth + 1, "return_type", cd->return_type, flags,
              NULL);
    }
    if (cd->num_parameters > 0) {
      jf_arr_open(out, &first, depth + 1, "params");
      bool fe = true;
      for (size_t i = 0; i < cd->num_parameters; i++) {
        if (!fe)
          fputs(",\n", out);
        else
          fe = false;
        json_indent(out, depth + 2);
        json_vardef(out, cd->parameters[i], flags, depth + 2);
      }
      jf_arr_close(out, depth + 1, fe);
    }
    if (cd->body && cd->body->num_nodes > 0) {
      jf_arr_open(out, &first, depth + 1, "body");
      bool fe = true;
      for (size_t i = 0; i < cd->body->num_nodes; i++)
        jarr_node(out, &fe, depth + 2, cd->body->nodes[i], flags, NULL);
      jf_arr_close(out, depth + 1, fe);
    }
    break;
  }

  case AST_BITSEL:
  case AST_BITSEL_RANGE:
    jf_node(out, &first, depth + 1, "source", n->bitselect->source, flags,
            NULL);
    if (n->bitselect->num_selections > 0) {
      jf_arr_open(out, &first, depth + 1, "selections");
      bool fe = true;
      for (size_t i = 0; i < n->bitselect->num_selections; i++)
        jarr_node(out, &fe, depth + 2, n->bitselect->selections[i], flags,
                  NULL);
      jf_arr_close(out, depth + 1, fe);
    }
    break;

  case AST_FIELDSEL:
    jf_node(out, &first, depth + 1, "source", n->fieldselect->source, flags,
            NULL);
    if (n->fieldselect->num_selections > 0) {
      jf_arr_open(out, &first, depth + 1, "selections");
      bool fe = true;
      for (size_t i = 0; i < n->fieldselect->num_selections; i++)
        jarr_node(out, &fe, depth + 2, n->fieldselect->selections[i], flags,
                  NULL);
      jf_arr_close(out, depth + 1, fe);
    }
    break;

  case AST_BITSLURP:
    if (n->bitslurp->num_elements > 0) {
      jf_arr_open(out, &first, depth + 1, "elements");
      bool fe = true;
      for (size_t i = 0; i < n->bitslurp->num_elements; i++)
        jarr_node(out, &fe, depth + 2, n->bitslurp->elements[i], flags, NULL);
      jf_arr_close(out, depth + 1, fe);
    }
    break;

  case AST_IF:
    if (n->if_->num_conditions > 0) {
      jf_arr_open(out, &first, depth + 1, "branches");
      bool fe = true;
      for (size_t i = 0; i < n->if_->num_conditions; i++) {
        if (!fe)
          fputs(",\n", out);
        else
          fe = false;
        json_indent(out, depth + 2);
        fprintf(out, "{\n");
        bool bf = true;
        jf_node(out, &bf, depth + 3, "condition",
                n->if_->conditions[i]->condition, flags, NULL);
        jf_node(out, &bf, depth + 3, "consequence",
                n->if_->conditions[i]->consequence, flags, NULL);
        fputc('\n', out);
        json_indent(out, depth + 2);
        fputc('}', out);
      }
      jf_arr_close(out, depth + 1, fe);
    }
    jf_node(out, &first, depth + 1, "else", n->if_->alternative, flags, NULL);
    break;

  case AST_WHEN:
    jf_node(out, &first, depth + 1, "match", n->case_->match, flags, NULL);
    if (n->case_->num_cases > 0) {
      jf_arr_open(out, &first, depth + 1, "cases");
      bool fe = true;
      for (size_t i = 0; i < n->case_->num_cases; i++) {
        if (!fe)
          fputs(",\n", out);
        else
          fe = false;
        json_indent(out, depth + 2);
        fprintf(out, "{\n");
        bool bf = true;
        jf_node(out, &bf, depth + 3, "condition", n->case_->cases[i]->condition,
                flags, NULL);
        jf_node(out, &bf, depth + 3, "consequence",
                n->case_->cases[i]->consequence, flags, NULL);
        fputc('\n', out);
        json_indent(out, depth + 2);
        fputc('}', out);
      }
      jf_arr_close(out, depth + 1, fe);
    }
    jf_node(out, &first, depth + 1, "otherwise", n->case_->otherwise, flags,
            NULL);
    break;

  case AST_TUPLE:
    if (n->tuple->num_elements > 0) {
      jf_arr_open(out, &first, depth + 1, "elements");
      bool fe = true;
      for (size_t i = 0; i < n->tuple->num_elements; i++)
        jarr_node(out, &fe, depth + 2, n->tuple->elements[i], flags,
                  imported_files);
      jf_arr_close(out, depth + 1, fe);
    }
    break;

  case AST_VALUE:
    switch (n->value->tag) {
    case VAL_NAME:
      jf(out, &first, depth + 1, "tag");
      json_str(out, "name");
      jf(out, &first, depth + 1, "value");
      json_str(out, n->value->name);
      break;
    case VAL_DEREF: {
      jf(out, &first, depth + 1, "tag");
      json_str(out, "deref");
      jf_arr_open(out, &first, depth + 1, "path");
      bool fe = true;
      for (size_t i = 0; i < n->value->deref.num_names; i++)
        jarr_str(out, &fe, depth + 2, n->value->deref.names[i]);
      jf_arr_close(out, depth + 1, fe);
      break;
    }
    case VAL_IMMEDIATE:
      jf(out, &first, depth + 1, "tag");
      json_str(out, "immediate");
      switch (n->value->imm.tag) {
      case IMM_INTEGER: {
        char *s = mpz_get_str(NULL, 10, n->value->imm.integer);
        jf(out, &first, depth + 1, "imm_type");
        json_str(out, "integer");
        jf(out, &first, depth + 1, "value");
        json_str(out, s);
        free(s);
        break;
      }
      case IMM_FLOAT:
        jf(out, &first, depth + 1, "imm_type");
        json_str(out, "float");
        jf(out, &first, depth + 1, "value");
        fprintf(out, "%g", (double)n->value->imm.real);
        break;
      case IMM_BITSTRING:
        jf(out, &first, depth + 1, "imm_type");
        json_str(out, "bitstring");
        jf(out, &first, depth + 1, "value");
        json_str(out, n->value->imm.bits);
        break;
      case IMM_BOOLEAN:
        jf(out, &first, depth + 1, "imm_type");
        json_str(out, "boolean");
        jf(out, &first, depth + 1, "value");
        fputs(n->value->imm.boolean ? "true" : "false", out);
        break;
      case IMM_STRING:
        jf(out, &first, depth + 1, "imm_type");
        json_str(out, "string");
        jf(out, &first, depth + 1, "value");
        json_str(out, n->value->imm.string);
        break;
      }
      break;
    }
    break;

  case AST_TYPE:
    jf(out, &first, depth + 1, "name");
    json_str(out, n->type_def->name);
    if (n->type_def->num_fields > 0) {
      jf_arr_open(out, &first, depth + 1, "fields");
      bool fe = true;
      for (size_t i = 0; i < n->type_def->num_fields; i++)
        jarr_node(out, &fe, depth + 2, n->type_def->fields[i], flags,
                  imported_files);
      jf_arr_close(out, depth + 1, fe);
    }
    break;

  case AST_ENUM:
    jf(out, &first, depth + 1, "name");
    json_str(out, n->enum_def->name);
    if (n->enum_def->num_elements > 0) {
      jf_arr_open(out, &first, depth + 1, "variants");
      bool fe = true;
      for (size_t i = 0; i < n->enum_def->num_elements; i++)
        jarr_str(out, &fe, depth + 2, n->enum_def->elements[i]);
      jf_arr_close(out, depth + 1, fe);
    }
    break;

  case AST_RETURN:
    jf_node(out, &first, depth + 1, "value", n->return_->return_, flags,
            imported_files);
    break;

  case AST_LOOP: {
    jf(out, &first, depth + 1, "loop_type");
    json_str(out, loop_str(n->loop->loop_type));
    if (n->loop->loop_type == LOOP_FOR_TO ||
        n->loop->loop_type == LOOP_FOR_DOWNTO) {
      jf_node(out, &first, depth + 1, "var", n->loop->range->name, flags,
              imported_files);
      jf_node(out, &first, depth + 1, "from", n->loop->range->start, flags,
              imported_files);
      jf_node(out, &first, depth + 1, "to", n->loop->range->end, flags,
              imported_files);
    } else {
      jf_node(out, &first, depth + 1, "condition", n->loop->condition, flags,
              imported_files);
    }
    jf_node(out, &first, depth + 1, "body", n->loop->block, flags,
            imported_files);
    break;
  }

  case AST_ASSERT:
    jf_node(out, &first, depth + 1, "condition", n->assert->condition, flags,
            imported_files);
    break;

  case AST_SUITE:
    if (n->num_suite > 0) {
      jf_arr_open(out, &first, depth + 1, "body");
      bool fe = true;
      for (size_t i = 0; i < n->num_suite; i++)
        jarr_node(out, &fe, depth + 2, n->suite[i], flags, imported_files);
      jf_arr_close(out, depth + 1, fe);
    }
    break;

  case AST_VAR_DEF:
    jf_node(out, &first, depth + 1, "type", n->var_def->type, flags,
            imported_files);
    if (n->var_def->num_names == 1) {
      jf_node(out, &first, depth + 1, "name", n->var_def->names[0], flags,
              imported_files);
    } else if (n->var_def->num_names > 1) {
      jf_arr_open(out, &first, depth + 1, "names");
      bool fe = true;
      for (size_t i = 0; i < n->var_def->num_names; i++)
        jarr_node(out, &fe, depth + 2, n->var_def->names[i], flags,
                  imported_files);
      jf_arr_close(out, depth + 1, fe);
    }
    break;

  case AST_TYPE_SPEC:
    if (n->type_spec->constant) {
      jf(out, &first, depth + 1, "constant");
      fputs("true", out);
    }
    jf_node(out, &first, depth + 1, "name", n->type_spec->name, flags,
            imported_files);
    jf_node(out, &first, depth + 1, "size", n->type_spec->size, flags,
            imported_files);
    break;

  case AST_PARAMETER:
    jf_node(out, &first, depth + 1, "type", n->parameter->type, flags,
            imported_files);
    jf_node(out, &first, depth + 1, "name", n->parameter->name, flags,
            imported_files);
    break;

  case AST_SET:
    if (n->set->num_members > 0) {
      jf_arr_open(out, &first, depth + 1, "members");
      bool fe = true;
      for (size_t i = 0; i < n->set->num_members; i++)
        jarr_node(out, &fe, depth + 2, n->set->members[i], flags,
                  imported_files);
      jf_arr_close(out, depth + 1, fe);
    }
    break;

  case AST_BLOCK:
    if (n->block->num_nodes > 0) {
      jf_arr_open(out, &first, depth + 1, "nodes");
      bool fe = true;
      for (size_t i = 0; i < n->block->num_nodes; i++)
        jarr_node(out, &fe, depth + 2, n->block->nodes[i], flags,
                  imported_files);
      jf_arr_close(out, depth + 1, fe);
    }
    break;

  case AST_INLINE_IF:
    if (n->inline_if->num_conditions > 0) {
      jf_arr_open(out, &first, depth + 1, "branches");
      bool fe = true;
      for (size_t i = 0; i < n->inline_if->num_conditions; i++) {
        if (!fe)
          fputs(",\n", out);
        else
          fe = false;
        json_indent(out, depth + 2);
        fprintf(out, "{\n");
        bool bf = true;
        jf_node(out, &bf, depth + 3, "condition",
                n->inline_if->conditions[i]->condition, flags, imported_files);
        jf_node(out, &bf, depth + 3, "consequence",
                n->inline_if->conditions[i]->consequence, flags,
                imported_files);
        fputc('\n', out);
        json_indent(out, depth + 2);
        fputc('}', out);
      }
      jf_arr_close(out, depth + 1, fe);
    }
    jf_node(out, &first, depth + 1, "else", n->inline_if->alternative, flags,
            imported_files);
    break;

  case AST_ARRAY:
    jf_node(out, &first, depth + 1, "elem_type", n->array->type, flags,
            imported_files);
    jf_node(out, &first, depth + 1, "name", n->array->name, flags,
            imported_files);
    jf_node(out, &first, depth + 1, "start", n->array->start, flags,
            imported_files);
    jf_node(out, &first, depth + 1, "end", n->array->end, flags,
            imported_files);
    break;

  case AST_ARRAY_ACCESS:
    jf_node(out, &first, depth + 1, "name", n->array_access->name, flags,
            imported_files);
    if (n->array_access->num_indices > 0) {
      jf_arr_open(out, &first, depth + 1, "indices");
      bool fe = true;
      for (size_t i = 0; i < n->array_access->num_indices; i++)
        jarr_node(out, &fe, depth + 2, n->array_access->indices[i], flags,
                  imported_files);
      jf_arr_close(out, depth + 1, fe);
    }
    break;

  case AST_TYPE_ALIAS:
    jf_node(out, &first, depth + 1, "from", n->type_alias->from, flags,
            imported_files);
    jf_node(out, &first, depth + 1, "to", n->type_alias->to, flags,
            imported_files);
    break;

  case AST_IMPORT:
    jf(out, &first, depth + 1, "path");
    json_str(out, n->import->path);
    armlet_ast_node *imported = armlet_parse_import(n, imported_files, false);
    if (imported != NULL) {
      jf(out, &first, depth + 1, "contents");
      json_node(imported, out, flags, depth + 1, imported_files);
    }
    break;

  case AST_USE:
    jf_node(out, &first, depth + 1, "target", n->use->target, flags,
            imported_files);
    break;

  case AST_ARRAY_LITERAL:
    if (n->array_literal->num_members > 0) {
      jf_arr_open(out, &first, depth + 1, "members");
      bool fe = true;
      for (size_t i = 0; i < n->array_literal->num_members; i++)
        jarr_node(out, &fe, depth + 2, n->array_literal->members[i], flags,
                  imported_files);
      jf_arr_close(out, depth + 1, fe);
    }
    break;

  case AST_UNKNOWN:
    jf_node(out, &first, depth + 1, "type_name", n->unknown->name, flags,
            imported_files);
    jf_node(out, &first, depth + 1, "size", n->unknown->size, flags,
            imported_files);
    break;

  case AST_IMPLEMENTATION_DEFINED:
    jf(out, &first, depth + 1, "key");
    json_str(out, n->implementation_defined->key);
    jf_node(out, &first, depth + 1, "type", n->implementation_defined->type,
            flags, imported_files);
    break;

  case AST_TRAP:
    jf(out, &first, depth + 1, "trap_type");
    json_str(out, trap_str(n->trap->trap_type));
    if (n->trap->context) {
      jf(out, &first, depth + 1, "context");
      json_str(out, n->trap->context);
    }
    break;

  case AST_QUALIFIED_LHS:
    jf_node(out, &first, depth + 1, "inner", n->qualified->inner, flags,
            imported_files);
    break;

  case AST_BITLAYOUT: {
    jf(out, &first, depth + 1, "name");
    json_str(out, n->bitlayout->name);
    jf(out, &first, depth + 1, "total_bits");
    fprintf(out, "%zu", n->bitlayout->total);
    if (n->bitlayout->argument_name) {
      jf(out, &first, depth + 1, "argument");
      json_str(out, n->bitlayout->argument_name);
    }
    if (n->bitlayout->num_members > 0) {
      jf_arr_open(out, &first, depth + 1, "members");
      bool fe = true;
      for (size_t i = 0; i < n->bitlayout->num_members; i++) {
        armlet_ast_bitlayout_member *m = n->bitlayout->members[i];
        if (!fe)
          fputs(",\n", out);
        else
          fe = false;
        json_indent(out, depth + 2);
        if (m->mtype == BITLAYOUT_IMMEDIATE) {
          fprintf(out, "{\"kind\": \"immediate\", \"bits\": ");
          json_str(out, m->immediate);
          fputc('}', out);
        } else {
          fprintf(out, "{\"kind\": \"named\", \"name\": ");
          json_str(out, m->name);
          fprintf(out, ", \"size\": %zu}", m->size);
        }
      }
      jf_arr_close(out, depth + 1, fe);
    }
    jf_node(out, &first, depth + 1, "handler", n->bitlayout->handler, flags,
            imported_files);
    break;
  }
  }

  fputc('\n', out);
  json_indent(out, depth);
  fputc('}', out);
}

void armlet_ast_print(const armlet_ast_node *node, FILE *out, int flags) {
  if (node == NULL)
    return;
  Hashtable *imported_files;
  hashtable_new(32, &imported_files);
  if (flags & AST_PRINT_JSON) {
    json_node(node, out, flags, 0, imported_files);
    fputc('\n', out);
  } else {
    tree_node(node, out, flags, NULL, true, imported_files);
  }
}
