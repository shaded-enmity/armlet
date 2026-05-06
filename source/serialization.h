#ifndef __ARMLET_SERIALIZATION__
#define __ARMLET_SERIALIZATION__

#include "interpreter.h"
#include "utils/hashtable.h"
#include <stdio.h>
#include <stdint.h>

#define ARMLET_SERIALIZE_MAGIC 0x4C4D5241  // 'ARML'
#define ARMLET_HASHTABLE_SERIALIZE_MAGIC 0x484D5241 // 'ARMH'
#define ARMLET_SERIALIZE_VERSION 1

#define ARMLET_ENDIAN_LITTLE 0
#define ARMLET_ENDIAN_BIG 1

typedef struct {
    uint32_t magic;
    uint8_t  version;
    uint8_t  endianness;
    uint8_t  strict;
    uint16_t reserved;
} __attribute__((packed)) armlet_serialize_header;

typedef struct {
    uint8_t  tag;
    uint8_t  flags;
    uint16_t reserved;
} __attribute__((packed)) armlet_var_header;

#define ARMLET_FLAG_CONST     0x01
#define ARMLET_FLAG_UNSET     0x02
#define ARMLET_FLAG_BITS_REF  0x04

typedef struct {
    FILE *file;
    int error;
    char error_msg[256];
} armlet_serialize_context;

enum armlet_serialize_tag {
  // Invalid default
  SERIALIZE_INVALID,
  // Serialize a single variable to a file
  SERIALIZE_VAR,
  // Serialize a hashtable to a file
  SERIALIZE_HASHTABLE,
};

typedef struct {
  enum armlet_serialize_tag tag;
  union {
    Hashtable *hashtable;
    armlet_vm_var *var;
  };
} armlet_serialize_value;

int armlet_vm_serialize(armlet_vm_context *, FILE *, armlet_serialize_value *);
int armlet_vm_deserialize(armlet_vm_context *, FILE *, armlet_serialize_value *);

bool armlet_vm_var_is_serializable(armlet_vm_var *v);

#endif
