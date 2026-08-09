// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef SEMINDEX_SEMANTIC_OUTPUT_H
#define SEMINDEX_SEMANTIC_OUTPUT_H

#include "semindex.h"

char output_symbol_kind_char(semindex_symbol_kind_t kind);
void output_mode_string(unsigned mode, char str[4]);

#endif /* SEMINDEX_SEMANTIC_OUTPUT_H */
