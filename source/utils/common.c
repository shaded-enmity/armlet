#include "common.h"
#include "string.h"

char BAIL_BUFFER[4096];

int op_map(const armlet_op_mapping map[], size_t size, char *c) {
  for (size_t i = 0; i < size; ++i) {
    if (streq(map[i].m, c))
      return map[i].s;
  }
  return -1;
}
