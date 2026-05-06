#include "serialization.h"
#include "interpreter.h"
#include "utils/common.h"

#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>


#define DESER_MAX_STRING_LEN   (16 * 1024 * 1024)
#define DESER_MAX_BITS_SIZE    (64 * 1024 * 1024)
#define DESER_MAX_INT_BYTES    (1 * 1024 * 1024)
#define DESER_MAX_ARRAY_ELEMS  (1 * 1024 * 1024)
#define DESER_MAX_HT_ITEMS     (1 * 1024 * 1024)
#define DESER_MAX_FIELDS       (16 * 1024)
#define DESER_MAX_NAMED_BITS   (16 * 1024)
#define DESER_MAX_TYPE_DEPTH   64
#define DESER_MAX_VAR_DEPTH    64

#define FNV_OFFSET_BASIS 0xcbf29ce484222325ULL
#define FNV_PRIME 0x100000001b3ULL

static inline size_t next_pow2(size_t v) {
  if (v <= 1)
    return 1;
  int clz = __builtin_clzl(v - 1);
  if (clz == 0)
    return v;
  return (size_t)1 << (sizeof(size_t) * 8 - clz);
}

static inline uint64_t fnv1a_init(void) { return FNV_OFFSET_BASIS; }

static inline uint64_t fnv1a_update_byte(uint64_t hash, uint8_t byte) {
  return (hash ^ byte) * FNV_PRIME;
}

static inline uint64_t fnv1a_update_bytes(uint64_t hash, const void *data,
                                          size_t len) {
  const uint8_t *bytes = (const uint8_t *)data;
  for (size_t i = 0; i < len; i++) {
    hash = fnv1a_update_byte(hash, bytes[i]);
  }
  return hash;
}

static inline uint64_t fnv1a_update_string(uint64_t hash, const char *str) {
  if (str == NULL)
    return fnv1a_update_byte(hash, 0);
  return fnv1a_update_bytes(hash, str, strlen(str));
}

static inline uint64_t fnv1a_update_uint8(uint64_t hash, uint8_t val) {
  return fnv1a_update_byte(hash, val);
}

static inline uint64_t fnv1a_update_uint64(uint64_t hash, uint64_t val) {
  return fnv1a_update_bytes(hash, &val, sizeof(val));
}

static uint64_t compute_type_digest(armlet_vm_type *t) {
  uint64_t hash = fnv1a_init();

  if (t == NULL) {
    return fnv1a_update_byte(hash, 0xFF);
  }

  hash = fnv1a_update_uint8(hash, t->tag);
  hash = fnv1a_update_string(hash, t->name);

  if (t->tag == T_ARRAY) {
    hash = fnv1a_update_uint64(hash, t->start);
    hash = fnv1a_update_uint64(hash, t->end);
  } else {
    hash = fnv1a_update_uint64(hash, t->size);
  }

  if (t->inner != NULL) {
    hash = fnv1a_update_uint64(hash, compute_type_digest(t->inner));
  }

  return hash;
}

static int param_compare(const void *a, const void *b, void *arg) {
  const armlet_vm_parameter *pa = *(const armlet_vm_parameter *const *)a;
  const armlet_vm_parameter *pb = *(const armlet_vm_parameter *const *)b;

  return strcmp(pa->name, pb->name);
}

static uint64_t compute_custom_type_digest(armlet_vm_custom_type *ct,
                                           bool strict) {
  uint64_t hash = fnv1a_init();

  hash = fnv1a_update_string(hash, ct->name);
  hash = fnv1a_update_uint64(hash, ct->num_fields);

  armlet_vm_parameter **fields = ct->fields;

  if (!strict) {
    fields = calloc(ct->num_fields, sizeof(*fields));
    if (!fields)
      BAIL("Out of memory: %s:%d\n", __FILE__, __LINE__);
    for (size_t i = 0; i < ct->num_fields; ++i) {
      fields[i] = ct->fields[i];
    }
    qsort_r(fields, ct->num_fields, sizeof(*fields), param_compare, NULL);
  }

  for (size_t i = 0; i < ct->num_fields; ++i) {
    hash = fnv1a_update_string(hash, fields[i]->name);
    hash = fnv1a_update_uint64(hash, compute_type_digest(ct->fields[i]->type));
  }

  if (!strict) {
    free(fields);
  }

  return hash;
}

static inline void write_uint8(uint8_t val, FILE *out) {
  fwrite(&val, sizeof(uint8_t), 1, out);
}

static inline void write_uint16(uint16_t val, FILE *out) {
  uint16_t le = htole16(val);
  fwrite(&le, sizeof(uint16_t), 1, out);
}

static inline void write_uint32(uint32_t val, FILE *out) {
  uint32_t le = htole32(val);
  fwrite(&le, sizeof(uint32_t), 1, out);
}

static inline void write_uint64(uint64_t val, FILE *out) {
  uint64_t le = htole64(val);
  fwrite(&le, sizeof(uint64_t), 1, out);
}

static inline void write_size_t(size_t val, FILE *out) {
  write_uint64((uint64_t)val, out);
}

static void write_string(const char *str, FILE *out) {
  if (str == NULL) {
    write_uint32(0, out);
    return;
  }
  uint32_t len = strlen(str);
  write_uint32(len, out);
  if (len > 0) {
    fwrite(str, 1, len, out);
  }
}

static inline int read_uint8(uint8_t *val, FILE *in) {
  return fread(val, sizeof(uint8_t), 1, in) == 1 ? 0 : -1;
}

static inline int read_uint16(uint16_t *val, FILE *in) {
  uint16_t le;
  if (fread(&le, sizeof(uint16_t), 1, in) != 1)
    return -1;
  *val = le16toh(le);
  return 0;
}

static inline int read_uint32(uint32_t *val, FILE *in) {
  uint32_t le;
  if (fread(&le, sizeof(uint32_t), 1, in) != 1)
    return -1;
  *val = le32toh(le);
  return 0;
}

static inline int read_uint64(uint64_t *val, FILE *in) {
  uint64_t le;
  if (fread(&le, sizeof(uint64_t), 1, in) != 1)
    return -1;
  *val = le64toh(le);
  return 0;
}

static inline int read_size_t(size_t *val, FILE *in) {
  uint64_t tmp;
  if (read_uint64(&tmp, in) != 0)
    return -1;
  *val = (size_t)tmp;
  return 0;
}

static char *read_string(FILE *in) {
  uint32_t len;
  if (read_uint32(&len, in) != 0)
    return NULL;

  if (len == 0)
    return NULL;

  if (len > DESER_MAX_STRING_LEN) {
    fprintf(stderr, "ERROR: String length %u exceeds limit %d\n", len,
            DESER_MAX_STRING_LEN);
    return NULL;
  }

  char *str = CHECKED_MALLOC(len + 1);

  if (fread(str, 1, len, in) != len) {
    free(str);
    return NULL;
  }

  str[len] = '\0';
  return str;
}

static int serialize_type(armlet_vm_type *t, FILE *out) {
  if (t == NULL) {
    write_uint8(0xFF, out);
    return 0;
  }

  write_uint8(t->tag, out);
  write_string(t->name, out);

  if (t->tag == T_ARRAY) {
    write_size_t(t->start, out);
    write_size_t(t->end, out);
  } else {
    write_size_t(t->size, out);
  }

  if (t->inner != NULL) {
    write_uint8(1, out);
    serialize_type(t->inner, out);
  } else {
    write_uint8(0, out);
  }

  return 0;
}

static armlet_vm_type *deserialize_type_depth(FILE *in, int depth) {
  if (depth > DESER_MAX_TYPE_DEPTH) {
    fprintf(stderr, "ERROR: Type nesting depth exceeds limit %d\n",
            DESER_MAX_TYPE_DEPTH);
    return NULL;
  }

  uint8_t tag;
  if (read_uint8(&tag, in) != 0)
    return NULL;

  if (tag == 0xFF)
    return NULL;

  armlet_vm_type *t = NEW0(armlet_vm_type);
  t->tag = tag;
  t->name = read_string(in);
  t->ref_count = 1;

  if (t->tag == T_ARRAY) {
    if (read_size_t(&t->start, in) != 0 || read_size_t(&t->end, in) != 0) {
      free(t->name);
      free(t);
      return NULL;
    }
  } else {
    if (read_size_t(&t->size, in) != 0) {
      free(t->name);
      free(t);
      return NULL;
    }
  }

  uint8_t has_inner;
  if (read_uint8(&has_inner, in) != 0) {
    free(t->name);
    free(t);
    return NULL;
  }

  if (has_inner) {
    t->inner = deserialize_type_depth(in, depth + 1);
    if (t->inner == NULL) {
      free(t->name);
      free(t);
      return NULL;
    }
  } else {
    t->inner = NULL;
  }

  return t;
}

static armlet_vm_type *deserialize_type(FILE *in) {
  return deserialize_type_depth(in, 0);
}

bool armlet_vm_var_is_serializable(armlet_vm_var *v) {
  if (v == NULL || v->type == NULL)
    return false;

  switch (v->type->tag) {
  case T_TYPE:
  case T_ENUMERATION_TYPE:
  case T_SCOPE:
  case T_FUNCTION:
  case T_GETTER:
  case T_SETTER:
  case T_BUILTIN:
  case T_TYPE_ALIAS:
    return false;
  default:
    return true;
  }
}

armlet_vm_custom_type *resolve_custom_type(armlet_vm_context *vm,
                                           const char *name) {
  armlet_vm_named_array *na = NULL;

  // A bit hackish search global scope (frame[0]) for type definitions
  if (hashtable_find_str(vm->frames[0]->symbols->symbols, name, (void **)&na) !=
      0)
    return NULL;

  if (na->num_items != 1)
    return NULL;

  armlet_vm_var *type = na->items[0]->var;
  if (type->type->tag != T_TYPE)
    return NULL;

  return type->custom_type;
}

static int serialize_var_value(armlet_vm_context *, armlet_vm_var *, FILE *);
static armlet_vm_var *deserialize_var_value(armlet_vm_context *,
                                            armlet_vm_var *, FILE *, int);
static armlet_vm_var *armlet_vm_var_deserialize_depth(armlet_vm_context *,
                                                      FILE *, int);

int armlet_vm_var_serialize(armlet_vm_context *vm, armlet_vm_var *v,
                            FILE *out) {
  if (v == NULL || out == NULL) {
    fprintf(stderr, "ERROR: NULL argument to armlet_vm_var_serialize\n");
    return -1;
  }

  if (!armlet_vm_var_is_serializable(v)) {
    fprintf(stderr, "ERROR: Cannot serialize type %s\n",
            armlet_vm_var_tag_names[v->type->tag]);
    return -1;
  }

  armlet_var_header hdr = {.tag = v->type->tag,
                           .flags = (v->is_const ? ARMLET_FLAG_CONST : 0) |
                                    (v->is_unset ? ARMLET_FLAG_UNSET : 0) |
                                    (v->is_bits_ref ? ARMLET_FLAG_BITS_REF : 0),
                           .reserved = 0};
  fwrite(&hdr, sizeof(hdr), 1, out);

  serialize_type(v->type, out);

  return serialize_var_value(vm, v, out);
}

static int serialize_var_value(armlet_vm_context *vm, armlet_vm_var *v,
                               FILE *out) {
  switch (v->type->tag) {
  case T_INTEGER: {
    int sign = mpz_sgn(v->integer);
    size_t count = (mpz_sizeinbase(v->integer, 2) + 7) / 8;

    write_uint8((int8_t)sign, out);
    write_size_t(count, out);

    if (count > 0 && sign != 0) {
      void *data = CHECKED_MALLOC(count);
      mpz_export(data, &count, 1, 1, 0, 0, v->integer);
      fwrite(data, 1, count, out);
      free(data);
    }
    break;
  }

  case T_REAL: {
    fwrite(&v->real, sizeof(float), 1, out);
    break;
  }

  case T_BOOLEAN: {
    write_uint8(v->boolean ? 1 : 0, out);
    break;
  }

  case T_STRING: {
    write_string(v->string, out);
    break;
  }

  case T_RANGE: {
    write_size_t(v->range_start, out);
    write_size_t(v->range_end, out);
    break;
  }

  case T_BITS: {
    size_t size = v->type->size;

    write_size_t(size, out);
    fwrite(v->bits, 1, size, out);

    if (v->named_bits != NULL && !v->is_bits_ref) {
      write_uint8(1, out);

      size_t count = 0;
      HASHTABLE_ITERATE(v->named_bits->ranges, char *, armlet_span *,
                        { count++; });

      write_size_t(count, out);

      HASHTABLE_ITERATE(v->named_bits->ranges, char *, armlet_span *, {
        write_string(key, out);
        write_uint32(value->start, out);
        write_uint32(value->end, out);
      });
    } else {
      write_uint8(0, out);
    }
    break;
  }

  case T_ENUMERATION: {
    write_string(v->enum_value->type->name, out);
    write_string(v->enum_value->name, out);
    write_uint64(v->enum_value->value, out);
    break;
  }

  case T_ARRAY:
  case T_TUPLE:
  case T_SET: {
    write_size_t(v->num_contents, out);

    if (v->type->tag == T_ARRAY) {
      write_size_t(v->contents_base, out);
    }

    if (v->contents_type != NULL) {
      write_uint8(1, out);
      serialize_type(v->contents_type, out);
    } else {
      write_uint8(0, out);
    }

    for (size_t i = 0; i < v->num_contents; i++) {
      if (armlet_vm_var_serialize(vm, v->contents[i], out) != 0) {
        return -1;
      }
    }
    break;
  }

  case T_INSTANCE: {
    armlet_vm_instance *inst = v->instance;
    armlet_vm_custom_type *ct = inst->class;

    write_string(ct->name, out);

    write_size_t(ct->num_fields, out);

    for (size_t i = 0; i < ct->num_fields; i++) {
      write_string(ct->fields[i]->name, out);
      serialize_type(ct->fields[i]->type, out);
    }

    uint64_t digest = compute_custom_type_digest(ct, vm->config.strict);
    write_uint64(digest, out);

    for (size_t i = 0; i < ct->num_fields; i++) {
      armlet_vm_var_named *field = NULL;
      if (hashtable_find_str(inst->fields, ct->fields[i]->name,
                             (void **)&field) == 0) {
        if (armlet_vm_var_serialize(vm, field->var, out) != 0) {
          return -1;
        }
      } else {
        fprintf(stderr, "ERROR: Missing field %s in instance\n",
                ct->fields[i]->name);
        return -1;
      }
    }
    break;
  }

  default:
    fprintf(stderr, "ERROR: Unsupported type for serialization: %s\n",
            armlet_vm_var_tag_names[v->type->tag]);
    return -1;
  }

  return 0;
}

static armlet_vm_var *armlet_vm_var_deserialize_depth(armlet_vm_context *vm,
                                                      FILE *in, int depth) {
  if (in == NULL) {
    fprintf(stderr, "ERROR: NULL file pointer to armlet_vm_var_deserialize\n");
    return NULL;
  }

  if (depth > DESER_MAX_VAR_DEPTH) {
    fprintf(stderr, "ERROR: Variable nesting depth exceeds limit %d\n",
            DESER_MAX_VAR_DEPTH);
    return NULL;
  }

  armlet_var_header hdr;
  if (fread(&hdr, sizeof(hdr), 1, in) != 1) {
    fprintf(stderr, "ERROR: Failed to read variable header\n");
    return NULL;
  }

  switch (hdr.tag) {
  case T_TYPE:
  case T_ENUMERATION_TYPE:
  case T_SCOPE:
  case T_FUNCTION:
  case T_GETTER:
  case T_SETTER:
  case T_BUILTIN:
  case T_TYPE_ALIAS:
    fprintf(stderr, "ERROR: Cannot deserialize type %s\n",
            armlet_vm_var_tag_names[hdr.tag]);
    return NULL;
  default:
    break;
  }

  armlet_vm_type *type = deserialize_type(in);
  if (type == NULL) {
    fprintf(stderr, "ERROR: Failed to deserialize type\n");
    return NULL;
  }

  armlet_vm_var *v = armlet_vm_var_new(type);
  v->is_const = (hdr.flags & ARMLET_FLAG_CONST) != 0;
  v->is_unset = (hdr.flags & ARMLET_FLAG_UNSET) != 0;
  v->is_bits_ref = (hdr.flags & ARMLET_FLAG_BITS_REF) != 0;

  if (deserialize_var_value(vm, v, in, depth) == NULL) {
    armlet_vm_var_release(v);
    return NULL;
  }

  return v;
}

armlet_vm_var *armlet_vm_var_deserialize(armlet_vm_context *vm, FILE *in) {
  return armlet_vm_var_deserialize_depth(vm, in, 0);
}

static armlet_vm_var *deserialize_var_value(armlet_vm_context *vm,
                                            armlet_vm_var *v, FILE *in,
                                            int depth) {
  switch (v->type->tag) {
  case T_INTEGER: {
    int8_t sign;
    size_t count;

    if (read_uint8((uint8_t *)&sign, in) != 0 || read_size_t(&count, in) != 0) {
      fprintf(stderr, "ERROR: Failed to read integer metadata\n");
      return NULL;
    }

    if (count > DESER_MAX_INT_BYTES) {
      fprintf(stderr, "ERROR: Integer byte count %zu exceeds limit %d\n", count,
              DESER_MAX_INT_BYTES);
      return NULL;
    }

    mpz_init(v->integer);

    if (count > 0 && sign != 0) {
      void *data = CHECKED_MALLOC(count);
      if (fread(data, 1, count, in) != count) {
        free(data);
        fprintf(stderr, "ERROR: Failed to read integer data\n");
        return NULL;
      }
      mpz_import(v->integer, count, 1, 1, 0, 0, data);
      if (sign < 0) {
        mpz_neg(v->integer, v->integer);
      }
      free(data);
    } else {
      mpz_set_ui(v->integer, 0);
    }
    break;
  }

  case T_REAL: {
    if (fread(&v->real, sizeof(float), 1, in) != 1) {
      fprintf(stderr, "ERROR: Failed to read real value\n");
      return NULL;
    }
    break;
  }

  case T_BOOLEAN: {
    uint8_t val;

    if (read_uint8(&val, in) != 0) {
      fprintf(stderr, "ERROR: Failed to read boolean value\n");
      return NULL;
    }

    v->boolean = val != 0;
    break;
  }

  case T_STRING: {
    v->string = read_string(in);

    if (v->string == NULL && ferror(in)) {
      fprintf(stderr, "ERROR: Failed to read string value\n");
      return NULL;
    }

    break;
  }

  case T_RANGE: {
    if (read_size_t(&v->range_start, in) != 0 ||
        read_size_t(&v->range_end, in) != 0) {
      fprintf(stderr, "ERROR: Failed to read range value\n");
      return NULL;
    }
    break;
  }

  case T_BITS: {
    size_t size;
    if (read_size_t(&size, in) != 0) {
      fprintf(stderr, "ERROR: Failed to read bits size\n");
      return NULL;
    }

    if (size > DESER_MAX_BITS_SIZE) {
      fprintf(stderr, "ERROR: Bits size %zu exceeds limit %d\n", size,
              DESER_MAX_BITS_SIZE);
      return NULL;
    }

    v->bits = CHECKED_MALLOC(size + 1);
    if (fread(v->bits, 1, size, in) != size) {
      fprintf(stderr, "ERROR: Failed to read bits data\n");
      return NULL;
    }
    v->bits[size] = '\0';

    uint8_t has_named;
    if (read_uint8(&has_named, in) != 0) {
      fprintf(stderr, "ERROR: Failed to read named_bits flag\n");
      return NULL;
    }

    if (has_named) {
      v->named_bits = NEW0(armlet_vm_named_bits);
      hashtable_new(16, &v->named_bits->ranges);

      size_t count;
      if (read_size_t(&count, in) != 0) {
        fprintf(stderr, "ERROR: Failed to read named_bits count\n");
        return NULL;
      }

      if (count > DESER_MAX_NAMED_BITS) {
        fprintf(stderr, "ERROR: Named bits count %zu exceeds limit %d\n",
                count, DESER_MAX_NAMED_BITS);
        return NULL;
      }

      for (size_t i = 0; i < count; i++) {
        char *name = read_string(in);
        if (name == NULL) {
          fprintf(stderr, "ERROR: Failed to read named_bits name\n");
          return NULL;
        }

        armlet_span *span = NEW0(armlet_span);
        if (read_uint32(&span->start, in) != 0 ||
            read_uint32(&span->end, in) != 0) {
          free(name);
          free(span);
          fprintf(stderr, "ERROR: Failed to read named_bits span\n");
          return NULL;
        }

        hashtable_add_str(v->named_bits->ranges, name, span);
      }
    }
    break;
  }

  case T_ENUMERATION: {
    char *type_name = read_string(in);
    char *elem_name = read_string(in);
    uint64_t value;

    if (type_name == NULL || elem_name == NULL ||
        read_uint64(&value, in) != 0) {
      free(type_name);
      free(elem_name);
      fprintf(stderr, "ERROR: Failed to read enumeration value\n");
      return NULL;
    }

    armlet_vm_type *enum_type = armlet_vm_make_enum_instance_type(
        armlet_vm_make_enum_type(type_name, 0), 0);

    v->enum_value = NEW0(armlet_vm_enum_element);
    v->enum_value->type = enum_type;
    v->enum_value->name = elem_name;
    v->enum_value->value = value;
    free(type_name);
    break;
  }

  case T_ARRAY:
  case T_TUPLE:
  case T_SET: {
    if (read_size_t(&v->num_contents, in) != 0) {
      fprintf(stderr, "ERROR: Failed to read array size\n");
      return NULL;
    }

    if (v->num_contents > DESER_MAX_ARRAY_ELEMS) {
      fprintf(stderr, "ERROR: Array size %zu exceeds limit %d\n",
              v->num_contents, DESER_MAX_ARRAY_ELEMS);
      return NULL;
    }

    if (v->type->tag == T_ARRAY) {
      if (read_size_t(&v->contents_base, in) != 0) {
        fprintf(stderr, "ERROR: Failed to read array base\n");
        return NULL;
      }
    }

    uint8_t has_contents_type;
    if (read_uint8(&has_contents_type, in) != 0) {
      fprintf(stderr, "ERROR: Failed to read contents_type flag\n");
      return NULL;
    }

    if (has_contents_type) {
      v->contents_type = deserialize_type(in);
      if (v->contents_type == NULL) {
        fprintf(stderr, "ERROR: Failed to deserialize contents_type\n");
        return NULL;
      }
    }

    v->contents = calloc(v->num_contents, sizeof(armlet_vm_var *));
    if (v->num_contents > 0 && !v->contents) {
      fprintf(stderr, "ERROR: Failed to allocate array of %zu elements\n",
              v->num_contents);
      return NULL;
    }

    for (size_t i = 0; i < v->num_contents; i++) {
      v->contents[i] = armlet_vm_var_deserialize_depth(vm, in, depth + 1);
      if (v->contents[i] == NULL) {
        fprintf(stderr, "ERROR: Failed to deserialize array element %zu\n", i);
        return NULL;
      }
    }
    break;
  }

  case T_INSTANCE: {
    char *class_name = read_string(in);
    bool strict_mode = vm->config.strict;

    size_t num_fields;
    if (class_name == NULL || read_size_t(&num_fields, in) != 0) {
      free(class_name);
      fprintf(stderr, "ERROR: Failed to read instance metadata\n");
      return NULL;
    }

    if (num_fields > DESER_MAX_FIELDS) {
      fprintf(stderr, "ERROR: Field count %zu exceeds limit %d\n", num_fields,
              DESER_MAX_FIELDS);
      free(class_name);
      return NULL;
    }

    armlet_vm_custom_type *ct = NEW0(armlet_vm_custom_type);
    ct->name = class_name;
    ct->type = armlet_vm_make_custom_type(class_name, num_fields);

    for (size_t i = 0; i < num_fields; i++) {
      armlet_vm_parameter *param = NEW0(armlet_vm_parameter);
      param->name = read_string(in);
      param->type = deserialize_type(in);

      if (param->name == NULL || param->type == NULL) {
        free(param->name);
        if (param->type)
          armlet_vm_type_release(param->type);
        free(param);
        fprintf(stderr, "ERROR: Failed to read field definition %zu\n", i);
        return NULL;
      }

      ARR_APPEND(ct, fields, param);
    }

    uint64_t stored_digest;
    if (read_uint64(&stored_digest, in) != 0) {
      fprintf(stderr, "ERROR: Failed to read type definition digest\n");
      return NULL;
    }

    uint64_t computed_digest = compute_custom_type_digest(ct, strict_mode);
    if (stored_digest != computed_digest) {
      fprintf(stderr,
              "ERROR: Serialization mismatch for '%s': stored digest "
              "0x%016llx != computed digest 0x%016llx\n",
              ct->name, (unsigned long long)stored_digest,
              (unsigned long long)computed_digest);
      fprintf(stderr,
              "       The serialization logic may have changed "
              "(field count: %zu)\n",
              num_fields);
      return NULL;
    }

    /* In strict mode also check the current and stored layout of a type
     *
     * At point A test.aml may have looked like:
     *
     *  type Foo is (
     *    bits(2) someBits
     *  )
     *
     *  Which was then instantiated and serialized.
     *
     *  At a later point B test.aml and Foo changed into:
     *
     *  type Foo is (
     *    bits(2) someBits,
     *    boolean val
     *  )
     *
     *  If we deserialize from the (A) file we'll get an instance from
     *  the old definition of Foo so if we're in strict mode we error out.
     */
    if (strict_mode) {
      armlet_vm_custom_type *current_scope_type =
          resolve_custom_type(vm, class_name);

      if (current_scope_type != NULL) {
        uint64_t digest =
            compute_custom_type_digest(current_scope_type, strict_mode);

        if (digest != computed_digest) {
          fprintf(
              stderr,
              "ERROR: Type definition mismatch for '%s': the type in the "
              "current "
              "scope has a digest 0x%016zx but computed digest was 0x%016zx\n",
              ct->name, digest, computed_digest);
          fprintf(stderr, "       The type definition may have changed\n");
          return NULL;
        }
      }
    }

    armlet_vm_instance *inst = NEW0(armlet_vm_instance);
    inst->class = ct;
    hashtable_new(next_pow2(num_fields), &inst->fields);

    for (size_t i = 0; i < num_fields; i++) {
      armlet_vm_var *field_val = armlet_vm_var_deserialize_depth(vm, in, depth + 1);
      if (field_val == NULL) {
        fprintf(stderr, "ERROR: Failed to deserialize field %zu value\n", i);
        return NULL;
      }

      armlet_vm_var_named *named =
          armlet_vm_named_from_var(field_val, ct->fields[i]->name);
      hashtable_add_str(inst->fields, ct->fields[i]->name, named);
    }

    v->instance = inst;
    break;
  }

  default:
    fprintf(stderr, "ERROR: Unknown tag %d during deserialization\n",
            v->type->tag);
    return NULL;
  }

  return v;
}

Hashtable *armlet_hashtable_deserialize(armlet_vm_context *, FILE *);
int armlet_hashtable_serialize(armlet_vm_context *, Hashtable *, FILE *);

int armlet_check_deserialize_header(armlet_serialize_header *hdr) {
  if (hdr->version != ARMLET_SERIALIZE_VERSION) {
    fprintf(stderr, "ERROR: Unsupported version (expected %d, got %d)\n",
            ARMLET_SERIALIZE_VERSION, hdr->version);
    return 1;
  }

  if (hdr->endianness != ARMLET_ENDIAN_LITTLE) {
    fprintf(stderr,
            "ERROR: File has different endianness, results may be incorrect\n");
    return 1;
  }

  return 0;
}

int armlet_vm_serialize(armlet_vm_context *vm, FILE *out,
                        armlet_serialize_value *value) {
  uint32_t magic = 0;

  switch (value->tag) {
  case SERIALIZE_VAR:
    magic = ARMLET_SERIALIZE_MAGIC;
    break;
  case SERIALIZE_HASHTABLE:
    magic = ARMLET_HASHTABLE_SERIALIZE_MAGIC;
    break;
  case SERIALIZE_INVALID:
    break;
  }

  armlet_serialize_header hdr = {.magic = magic,
                                 .version = ARMLET_SERIALIZE_VERSION,
                                 .strict = vm->config.strict,
                                 .endianness = ARMLET_ENDIAN_LITTLE,
                                 .reserved = 0};

  if (fwrite(&hdr, sizeof(hdr), 1, out) != 1) {
    return 1;
  }

  int rc;
  switch (value->tag) {
  case SERIALIZE_VAR:
    rc = armlet_vm_var_serialize(vm, value->var, out);
    break;
  case SERIALIZE_HASHTABLE:
    rc = armlet_hashtable_serialize(vm, value->hashtable, out);
    break;
  case SERIALIZE_INVALID:
    rc = 0;
    break;
  }

  if (rc != 0)
    return rc;

  if (ferror(out)) {
    fprintf(stderr, "ERROR: I/O error during serialization\n");
    return 1;
  }

  return 0;
}

int armlet_vm_deserialize(armlet_vm_context *vm, FILE *in,
                          armlet_serialize_value *value) {
  armlet_serialize_header hdr;
  if (fread(&hdr, sizeof(hdr), 1, in) != 1) {
    return 1;
  }

  if (armlet_check_deserialize_header(&hdr)) {
    return 1;
  }

  switch (hdr.magic) {
  case ARMLET_SERIALIZE_MAGIC:
    value->var = armlet_vm_var_deserialize(vm, in);
    value->tag = SERIALIZE_VAR;
    if (value->var == NULL)
      return 1;
    break;
  case ARMLET_HASHTABLE_SERIALIZE_MAGIC:
    value->hashtable = armlet_hashtable_deserialize(vm, in);
    value->tag = SERIALIZE_HASHTABLE;
    if (value->hashtable == NULL)
      return 1;
    break;
  default:
    fprintf(stderr, "ERROR: Invalid header magic: %u\n", hdr.magic);
    return 1;
  }

  return 0;
}

int armlet_hashtable_serialize(armlet_vm_context *vm, Hashtable *ht,
                               FILE *out) {
  if (ht == NULL || out == NULL) {
    fprintf(stderr, "ERROR: NULL argument to armlet_hashtable_serialize\n");
    return -1;
  }

  write_size_t(ht->num_items, out);

  HASHTABLE_ITERATE(ht, char *, armlet_vm_var *, {
    write_string(key, out);

    if (armlet_vm_var_serialize(vm, value, out) != 0) {
      fprintf(stderr,
              "ERROR: Failed to serialize hashtable value for key '%s'\n", key);
      return -1;
    }
  });

  return 0;
}

Hashtable *armlet_hashtable_deserialize(armlet_vm_context *vm, FILE *in) {
  if (in == NULL) {
    fprintf(stderr,
            "ERROR: NULL file pointer to armlet_hashtable_deserialize\n");
    return NULL;
  }

  size_t num_items;
  if (read_size_t(&num_items, in) != 0) {
    fprintf(stderr, "ERROR: Failed to read hashtable size\n");
    return NULL;
  }

  if (num_items > DESER_MAX_HT_ITEMS) {
    fprintf(stderr, "ERROR: Hashtable item count %zu exceeds limit %d\n",
            num_items, DESER_MAX_HT_ITEMS);
    return NULL;
  }

  Hashtable *ht;
  if (hashtable_new(next_pow2(num_items), &ht) != 0) {
    fprintf(stderr, "ERROR: Failed to create hashtable\n");
    return NULL;
  }

  for (size_t i = 0; i < num_items; i++) {
    char *key = read_string(in);

    if (key == NULL) {
      fprintf(stderr, "ERROR: Failed to read hashtable key at index %zu\n", i);
      hashtable_unref(ht);
      return NULL;
    }

    armlet_vm_var *value = armlet_vm_var_deserialize(vm, in);
    if (value == NULL) {
      fprintf(stderr,
              "ERROR: Failed to deserialize hashtable value at index %zu\n", i);
      free(key);
      hashtable_unref(ht);
      return NULL;
    }

    if (hashtable_add_str(ht, key, value) != 0) {
      fprintf(stderr, "ERROR: Failed to add entry to hashtable at index %zu\n",
              i);
      free(key);
      armlet_vm_var_release(value);
      hashtable_unref(ht);
      return NULL;
    }
  }

  return ht;
}
