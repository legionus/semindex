// SPDX-License-Identifier: GPL-2.0-or-later
#include <sqlite3.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "index_db.h"
#include "output.h"
#include "semindex_database.h"

static int prepare(sqlite3 *db, const char *sql, sqlite3_stmt **stmt)
{
	if (sqlite3_prepare_v3(db, sql, -1, SQLITE_PREPARE_PERSISTENT, stmt, NULL) == SQLITE_OK)
		return 0;

	fprintf(stderr, "semindex: sqlite: %s\n", sqlite3_errmsg(db));
	return -1;
}

static int pattern_uses_glob(const char *pattern)
{
	return pattern && strpbrk(pattern, "*?[]");
}

struct search_output {
	output_search_t *formatter;
};

static int print_search_result(void *data, const semindex_db_record_t *record)
{
	struct search_output *output = data;

	output_search_record_t result = {
		.variant = record->variant,
		.file = record->path,
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

static int open_reader(const char *path, sqlite3 **db)
{
	if (sqlite3_open_v2(path, db, SQLITE_OPEN_READONLY, NULL) != SQLITE_OK) {
		fprintf(stderr, "semindex: failed to open database '%s': %s\n", path,
			*db ? sqlite3_errmsg(*db) : "unknown error");
		return -1;
	}

	if (sqlite3_busy_timeout(*db, INT_MAX) != SQLITE_OK)
		return -1;

	return 0;
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
	semindex_db_close(db);

	return ret;
}

static int print_callgraph_results(sqlite3 *db, const char *sql, int show_id, FILE *out)
{
	sqlite3_stmt *stmt = NULL;
	int step;
	int ret = -1;

	if (prepare(db, sql, &stmt) < 0)
		return -1;

	while ((step = sqlite3_step(stmt)) == SQLITE_ROW) {
		const char *variant = (const char *)sqlite3_column_text(stmt, 0);
		const char *caller = (const char *)sqlite3_column_text(stmt, 1);
		unsigned long long caller_id = (unsigned long long)sqlite3_column_int64(stmt, 2);
		const char *callee = (const char *)sqlite3_column_text(stmt, 3);
		unsigned long long callee_id = (unsigned long long)sqlite3_column_int64(stmt, 4);
		const char *file = (const char *)sqlite3_column_text(stmt, 5);
		int line = sqlite3_column_int(stmt, 6);
		int column = sqlite3_column_int(stmt, 7);

		if (show_id) {
			if (fprintf(out, "%s\t%016llx\t%s\t%016llx\t%s:%s:%d:%d\n", caller, caller_id, callee,
				    callee_id, variant, file, line, column) < 0)
				goto out;

		} else if (fprintf(out, "%s -> %s\t%s:%s:%d:%d\n", caller, callee, variant, file, line, column) < 0) {
			goto out;
		}
	}

	if (step != SQLITE_DONE) {
		fprintf(stderr, "semindex: sqlite: %s\n", sqlite3_errmsg(db));
		goto out;
	}

	ret = ferror(out) ? -1 : 0;
out:
	sqlite3_finalize(stmt);

	return ret;
}

int index_db_callgraph(const char *path, const index_db_callgraph_options_t *opts, FILE *out)
{
	sqlite3_str *query = NULL;
	sqlite3 *db = NULL;
	char *sql = NULL;
	int ret = -1;

	if (!path || !opts || !opts->function || !opts->function[0] || !out)
		return -1;

	if (open_reader(path, &db) < 0)
		goto out;

	query = sqlite3_str_new(db);

	if (!query)
		goto out;

	sqlite3_str_appendf(query,
		"SELECT files.variant, records.context, records.context_usr_id, records.symbol, records.usr_id, "
		"files.path, records.line, records.column "
		"FROM records JOIN files ON files.id = records.file_id "
		"WHERE records.record = 1 AND records.action = %d AND records.kind = %d",
		SEMINDEX_USE_CALL, SEMINDEX_SYMBOL_FUNCTION);

	if (opts->direction == INDEX_DB_CALLGRAPH_CALLERS) {
		sqlite3_str_appendf(query, " AND records.symbol = %Q", opts->function);

		if (opts->has_id)
			sqlite3_str_appendf(query, " AND records.usr_id = 0x%016llx", opts->id);
	} else {
		sqlite3_str_appendf(query, " AND records.context = %Q", opts->function);

		if (opts->has_id)
			sqlite3_str_appendf(query, " AND records.context_usr_id = 0x%016llx", opts->id);
	}

	if (opts->path)
		sqlite3_str_appendf(query, " AND files.path %s %Q", pattern_uses_glob(opts->path) ? "GLOB" : "=",
			opts->path);

	if (opts->variant)
		sqlite3_str_appendf(query, " AND files.variant %s %Q", pattern_uses_glob(opts->variant) ? "GLOB" : "=",
			opts->variant);

	sqlite3_str_appendall(query,
		" ORDER BY files.variant, records.context, records.symbol, files.path, records.line, records.column");

	if (sqlite3_str_errcode(query) != SQLITE_OK)
		goto out;

	sql = sqlite3_str_finish(query);
	query = NULL;

	if (!sql)
		goto out;

	ret = print_callgraph_results(db, sql, opts->show_id, out);
out:
	if (query)
		sqlite3_str_finish(query);

	sqlite3_free(sql);

	if (db)
		sqlite3_close(db);

	return ret;
}
