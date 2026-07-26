// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef SEMINDEX_CLI_H
#define SEMINDEX_CLI_H

#include <stdio.h>

#include "output.h"

int parse_format(const char *value, enum output_format *format);
int parse_scope(const char *value, semindex_scope_t *scope);
int output_index(enum output_format format, semindex_t *s);

#endif /* SEMINDEX_CLI_H */
