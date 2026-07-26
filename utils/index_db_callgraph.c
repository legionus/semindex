// SPDX-License-Identifier: GPL-2.0-or-later
#include <sqlite3.h>
#include <stdio.h>
#include <string.h>

#include "index_db.h"
#include "sqlite.h"

static int pattern_uses_glob(const char *pattern)
{
	return pattern && strpbrk(pattern, "*?[]");
}

static int print_callgraph_results(sqlite3 *db, const char *sql, int show_id, FILE *out)
{
	sqlite3_stmt *stmt = NULL;
	int step;
	int ret = -1;

	if (semindex_sqlite_prepare(db, sql, &stmt) < 0)
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

	if (semindex_sqlite_open_readonly(path, &db) < 0)
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
