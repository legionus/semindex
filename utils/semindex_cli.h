// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef SEMINDEX_CLI_H
#define SEMINDEX_CLI_H

#include <stdio.h>

#include "output.h"

void semindex_usage(FILE *f);
void semindex_help(void);

int parse_format(const char *value, enum output_format *format);
int parse_scope(const char *value, semindex_scope_t *scope);
int output_index(enum output_format format, semindex_t *s);

int cmd_index(int argc, char **argv);
int cmd_compiler(int argc, char **argv);

#endif /* SEMINDEX_CLI_H */
