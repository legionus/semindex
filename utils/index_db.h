// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef SEMINDEX_INDEX_DB_H
#define SEMINDEX_INDEX_DB_H

#include <stdio.h>

#include "semindex.h"

typedef enum {
	INDEX_DB_RECORD_ALL,
	INDEX_DB_RECORD_SYMBOL,
	INDEX_DB_RECORD_USE,
} index_db_record_t;

typedef struct {
	const char *pattern;
	const char *path;
	const char *format;
	const char *variant;
	index_db_record_t record;
	unsigned mode;
	int kind;
	int has_kind;
	int has_mode;
	int mode_definition;
} index_db_search_options_t;

typedef enum {
	INDEX_DB_CALLGRAPH_CALLERS,
	INDEX_DB_CALLGRAPH_CALLEES,
} index_db_callgraph_direction_t;

typedef struct {
	const char *function;
	const char *path;
	const char *variant;
	unsigned long long id;
	index_db_callgraph_direction_t direction;
	int has_id;
	int show_id;
} index_db_callgraph_options_t;

int index_db_store(const char *path, semindex_t *s, const char *main_file, const char *variant, int include_local);
int index_db_search(const char *path, const index_db_search_options_t *opts, FILE *out);
int index_db_callgraph(const char *path, const index_db_callgraph_options_t *opts, FILE *out);

#endif /* SEMINDEX_INDEX_DB_H */
