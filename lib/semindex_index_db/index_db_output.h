// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef SEMINDEX_INDEX_DB_OUTPUT_H
#define SEMINDEX_INDEX_DB_OUTPUT_H

#include <stdio.h>

#include "semindex.h"

#define INDEX_DB_OUTPUT_DEFAULT_FORMAT "(%{mode}) %{variant_file}\\t%{line}\\t%{column}\\t%{context}\\t%{source}"

typedef struct index_db_output index_db_output_t;

typedef struct {
	const char *file;
	const char *variant;
	const char *root;
	int line;
	int column;
	int symbol_record;
	int definition;
	semindex_symbol_kind_t kind;
	unsigned mode;
	const char *symbol;
	const char *context;
} index_db_output_record_t;

index_db_output_t *index_db_output_create(FILE *out, const char *format, const char *root_hint);
void index_db_output_destroy(index_db_output_t *output);
int index_db_output_write(index_db_output_t *output, const index_db_output_record_t *record);

#endif /* SEMINDEX_INDEX_DB_OUTPUT_H */
