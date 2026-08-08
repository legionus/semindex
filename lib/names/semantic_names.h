// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "semindex.h"

#ifdef __cplusplus
extern "C" {
#endif

const char *semindex_symbol_kind_name(semindex_symbol_kind_t kind);
int semindex_symbol_kind_parse(const char *name, semindex_symbol_kind_t *kind);

#ifdef __cplusplus
}
#endif
