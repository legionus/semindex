// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef SEMINDEX_CLI_H
#define SEMINDEX_CLI_H

#include <stdio.h>

#include "semindex.h"
#include "index_pipeline.h"
#include "output.h"
#include "index_db.h"

int parse_format(const char *value, enum output_format *format);
int parse_scope(const char *value, semindex_scope_t *scope);
int parse_git_commit(const char *value, index_pipeline_git_commit_t *mode);
int parse_index_db_record(const char *value, index_db_record_t *record);
int parse_symbol_kind(const char *value, int *kind);
int output_index(enum output_format format, semindex_t *s);

#endif /* SEMINDEX_CLI_H */
