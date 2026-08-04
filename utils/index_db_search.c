// SPDX-License-Identifier: GPL-2.0-or-later
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "index_db.h"
#include "output.h"
#include "semindex_database.h"

struct variant_root {
	struct variant_root *next;
	char *variant;
	char *root;
};

struct search_output {
	output_search_t *formatter;
	struct variant_root *roots;
	const char *last_variant;
	const char *last_root;
};

static void free_variant_roots(struct variant_root *root)
{
	while (root) {
		struct variant_root *next = root->next;

		free(root->variant);
		free(root->root);
		free(root);
		root = next;
	}
}

static int collect_variant_root(void *data, const semindex_db_variant_t *variant)
{
	struct search_output *output = data;
	struct variant_root *root;

	root = calloc(1, sizeof(*root));

	if (!root)
		return -1;

	root->variant = strdup(variant->name);

	if (variant->repository_root)
		root->root = strdup(variant->repository_root);

	if (!root->variant || (variant->repository_root && !root->root)) {
		free_variant_roots(root);

		return -1;
	}

	root->next = output->roots;
	output->roots = root;

	return 0;
}

static const char *find_variant_root(struct search_output *output, const char *variant)
{
	const struct variant_root *root;

	if (output->last_variant && !strcmp(output->last_variant, variant))
		return output->last_root;

	for (root = output->roots; root; root = root->next) {
		if (!strcmp(root->variant, variant)) {
			output->last_variant = root->variant;
			output->last_root = root->root;

			return root->root;
		}
	}

	return NULL;
}

static int print_search_result(void *data, const semindex_db_record_t *record)
{
	struct search_output *output = data;

	output_search_record_t result = {
		.variant = record->variant,
		.file = record->path,
		.root = find_variant_root(output, record->variant),
		.line = record->line,
		.column = record->column,
		.symbol_record = record->record != SEMINDEX_DB_REFERENCE,
		.definition = record->record == SEMINDEX_DB_DEFINITION,
		.kind = record->kind,
		.symbol = record->symbol,
		.context = record->context,
		.mode = record->mode,
	};

	return output_search_write(output->formatter, &result);
}

int index_db_search(const char *path, const index_db_search_options_t *opts, FILE *out)
{
	index_db_search_options_t defaults = {
		.record = INDEX_DB_RECORD_ALL,
	};
	semindex_db_query_options_t query = { 0 };

	semindex_db_t *db = NULL;
	struct search_output output = { 0 };
	int ret = -1;

	if (!path || !out)
		return -1;

	if (!opts)
		opts = &defaults;

	if (semindex_db_open(path, &db) < 0)
		goto out;

	if (semindex_db_list_variants(db, collect_variant_root, &output) < 0)
		goto out;

	output.formatter = output_search_create(out, opts->format ? opts->format : OUTPUT_SEARCH_DEFAULT_FORMAT, path);

	if (!output.formatter)
		goto out;

	query.symbol = opts->pattern;
	query.path = opts->path;
	query.variant = opts->variant;
	query.mode = opts->mode;
	query.kind = opts->kind;
	query.has_mode = opts->has_mode && !opts->mode_definition;
	query.has_kind = opts->has_kind;

	if (opts->mode_definition)
		query.record = SEMINDEX_DB_RECORD_DEFINITION;

	else if (opts->record == INDEX_DB_RECORD_SYMBOL)
		query.record = SEMINDEX_DB_RECORD_SYMBOL;

	else if (opts->record == INDEX_DB_RECORD_USE)
		query.record = SEMINDEX_DB_RECORD_REFERENCE;

	ret = semindex_db_query(db, &query, print_search_result, &output);

	if (!ret && ferror(out))
		ret = -1;
out:
	output_search_destroy(output.formatter);
	free_variant_roots(output.roots);
	semindex_db_close(db);

	return ret;
}
