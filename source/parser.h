#ifndef __ARMLET_PARSER__
#define __ARMLET_PARSER__

#include "ast.h"
#include "utils/hashtable.h"
#include <stdbool.h>
#include <stddef.h>


armlet_ast_node *armlet_parse_source_pure(const char *src, size_t len,
                                          const char *filename, bool debug);

armlet_ast_node *armlet_parse_file_pure(const char *path, bool debug);

armlet_ast_node *armlet_parse_import(const armlet_ast_node *n,
                                     Hashtable *imported_files, bool debug);

void armlet_parser_set_import_path(const char *path);

#endif
