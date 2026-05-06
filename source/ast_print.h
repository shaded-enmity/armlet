#ifndef __ARMLET_AST_PRINT__
#define __ARMLET_AST_PRINT__

#include "ast.h"
#include <stdio.h>

#define AST_PRINT_SPANS (1 << 0)
#define AST_PRINT_JSON  (1 << 1)

void armlet_ast_print(const armlet_ast_node *node, FILE *out, int flags);

#endif
