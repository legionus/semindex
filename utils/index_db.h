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
	index_db_record_t record;
	unsigned mode;
	int kind;
	int has_kind;
	int has_mode;
	int mode_definition;
} index_db_search_options_t;

int index_db_store(const char *path, semindex_t *s, const char *main_file, int include_local);
int index_db_search(const char *path, const index_db_search_options_t *opts, FILE *out);

#endif /* SEMINDEX_INDEX_DB_H */
