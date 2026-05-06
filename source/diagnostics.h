#ifndef __ARMLET_DIAGNOSTICS__
#define __ARMLET_DIAGNOSTICS__

#include "ast.h"

void armlet_source_diagnostic(FILE *, const armlet_source *, armlet_span, bool,
                              const char *, const char *);

armlet_line_info armlet_source_line_info_one(const armlet_source *,
                                             armlet_span);

#endif
