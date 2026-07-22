// SPDX-License-Identifier: GPL-2.0-or-later
#include <sqlite3.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "semindex_database.h"

enum stored_record_kind {
	STORED_RECORD_SYMBOL,
	STORED_RECORD_USE,
};

_Static_assert(STORED_RECORD_USE == 1, "call query SQL assumes use record value 1");
_Static_assert(SEMINDEX_USE_CALL == 3, "call query SQL assumes call action value 3");
_Static_assert(SEMINDEX_SYMBOL_FUNCTION == 7, "call query SQL assumes function kind value 7");

struct semindex_db {
	sqlite3 *handle;
};

static int prepare(semindex_db_t *db, const char *sql, sqlite3_stmt **stmt)
{
	int ret;

	ret = sqlite3_prepare_v3(db->handle, sql, -1, SQLITE_PREPARE_PERSISTENT, stmt, NULL);
	if (ret != SQLITE_OK) {
		fprintf(stderr, "semindex: sqlite: %s\n", sqlite3_errmsg(db->handle));
		return -1;
	}

	return 0;
}

static int pattern_uses_glob(const char *pattern)
{
	return pattern && strpbrk(pattern, "*?[]");
}

static const char *column_text(sqlite3_stmt *stmt, int column)
{
	const char *value = (const char *)sqlite3_column_text(stmt, column);

	return value ? value : "";
}

static semindex_db_record_type_t record_type(int record, int action)
{
	if (record == STORED_RECORD_USE)
		return SEMINDEX_DB_REFERENCE;
	return action ? SEMINDEX_DB_DEFINITION : SEMINDEX_DB_DECLARATION;
}

static int emit_records(semindex_db_t *db, sqlite3_stmt *stmt, semindex_db_record_callback_t callback, void *data,
	unsigned position_column)
{
	int step;
	int ret = -1;

	while ((step = sqlite3_step(stmt)) == SQLITE_ROW) {
		int stored_record = sqlite3_column_int(stmt, 4);
		unsigned action = sqlite3_column_int(stmt, 5);
		semindex_db_record_t record = {
			.variant = column_text(stmt, 0),
			.path = column_text(stmt, 1),
			.line = sqlite3_column_int(stmt, 2),
			.column = sqlite3_column_int(stmt, 3),
			.record = record_type(stored_record, action),
			.action = action,
			.kind = sqlite3_column_int(stmt, 6),
			.symbol = column_text(stmt, 7),
			.context = column_text(stmt, 8),
			.mode = sqlite3_column_int64(stmt, 9),
			.usr_id = (unsigned long long)sqlite3_column_int64(stmt, 10),
			.context_usr_id = (unsigned long long)sqlite3_column_int64(stmt, 11),
			.local = sqlite3_column_int(stmt, 12),
		};

		if (position_column) {
			const char *name = strrchr(record.symbol, '.');
			size_t length;

			name = name ? name + 1 : record.symbol;
			length = strlen(name);
			if (position_column < record.column || position_column - record.column >= length)
				continue;
		}
		ret = callback(data, &record);
		if (ret)
			goto out;
	}
	if (step != SQLITE_DONE) {
		fprintf(stderr, "semindex: sqlite: %s\n", sqlite3_errmsg(db->handle));
		ret = -1;
		goto out;
	}
	ret = 0;
out:
	return ret;
}

int semindex_db_open(const char *path, semindex_db_t **result)
{
	semindex_db_t *db;

	if (!path || !result)
		return -1;
	*result = NULL;
	db = calloc(1, sizeof(*db));
	if (!db)
		return -1;
	if (sqlite3_open_v2(path, &db->handle, SQLITE_OPEN_READONLY, NULL) != SQLITE_OK) {
		fprintf(stderr, "semindex: failed to open database '%s': %s\n", path,
			db->handle ? sqlite3_errmsg(db->handle) : "unknown error");
		semindex_db_close(db);
		return -1;
	}
	if (sqlite3_busy_timeout(db->handle, INT_MAX) != SQLITE_OK) {
		semindex_db_close(db);
		return -1;
	}
	*result = db;
	return 0;
}

void semindex_db_close(semindex_db_t *db)
{
	if (!db)
		return;
	if (db->handle)
		sqlite3_close(db->handle);
	free(db);
}

int semindex_db_query(semindex_db_t *db, const semindex_db_query_options_t *opts,
	semindex_db_record_callback_t callback, void *data)
{
	semindex_db_query_options_t defaults = { 0 };
	sqlite3_str *query = NULL;
	sqlite3_stmt *stmt = NULL;
	char *sql = NULL;
	int ret = -1;

	if (!db || !callback)
		return -1;
	if (!opts)
		opts = &defaults;
	if (opts->record < SEMINDEX_DB_RECORD_ALL || opts->record > SEMINDEX_DB_RECORD_REFERENCE)
		return -1;

	query = sqlite3_str_new(db->handle);
	if (!query)
		goto out;
	sqlite3_str_appendall(query,
		"SELECT files.variant, files.path, records.line, records.column, records.record, records.action, "
		"records.kind, records.symbol, records.context, records.mode, records.usr_id, "
		"records.context_usr_id, records.local "
		"FROM records JOIN files ON files.id = records.file_id WHERE 1");
	if (opts->symbol)
		sqlite3_str_appendf(query, " AND records.symbol %s %Q", pattern_uses_glob(opts->symbol) ? "GLOB" : "=",
			opts->symbol);
	if (opts->path)
		sqlite3_str_appendf(query, " AND files.path %s %Q", pattern_uses_glob(opts->path) ? "GLOB" : "=",
			opts->path);
	if (opts->variant)
		sqlite3_str_appendf(query, " AND files.variant %s %Q", pattern_uses_glob(opts->variant) ? "GLOB" : "=",
			opts->variant);
	if (opts->context)
		sqlite3_str_appendf(query, " AND records.context = %Q", opts->context);
	switch (opts->record) {
	case SEMINDEX_DB_RECORD_ALL:
		break;
	case SEMINDEX_DB_RECORD_SYMBOL:
		sqlite3_str_appendf(query, " AND records.record = %d", STORED_RECORD_SYMBOL);
		break;
	case SEMINDEX_DB_RECORD_DECLARATION:
		sqlite3_str_appendf(query, " AND records.record = %d AND records.action = 0", STORED_RECORD_SYMBOL);
		break;
	case SEMINDEX_DB_RECORD_DEFINITION:
		sqlite3_str_appendf(query, " AND records.record = %d AND records.action != 0", STORED_RECORD_SYMBOL);
		break;
	case SEMINDEX_DB_RECORD_REFERENCE:
		sqlite3_str_appendf(query, " AND records.record = %d", STORED_RECORD_USE);
		break;
	}
	if (opts->has_mode) {
		if (!opts->mode)
			sqlite3_str_appendf(query, " AND records.record = %d AND records.mode = 0", STORED_RECORD_USE);
		else
			sqlite3_str_appendf(query, " AND records.record = %d AND (records.mode & %u) != 0",
				STORED_RECORD_USE, opts->mode);
	}
	if (opts->has_kind)
		sqlite3_str_appendf(query, " AND records.kind = %d", opts->kind);
	if (opts->has_usr_id)
		sqlite3_str_appendf(query, " AND records.usr_id = 0x%016llx", opts->usr_id);
	if (opts->has_local)
		sqlite3_str_appendf(query, " AND records.local = %d", !!opts->local);
	sqlite3_str_appendall(query,
		" ORDER BY files.variant, files.path, records.line, records.column, records.record");
	if (sqlite3_str_errcode(query) != SQLITE_OK)
		goto out;

	sql = sqlite3_str_finish(query);
	query = NULL;
	if (!sql || prepare(db, sql, &stmt) < 0)
		goto out;
	ret = emit_records(db, stmt, callback, data, 0);
out:
	if (query)
		sqlite3_str_finish(query);
	sqlite3_free(sql);
	sqlite3_finalize(stmt);
	return ret;
}

int semindex_db_query_calls(semindex_db_t *db, const semindex_db_call_options_t *opts,
	semindex_db_record_callback_t callback, void *data)
{
	static const char *callers =
		"SELECT files.variant, files.path, records.line, records.column, records.record, records.action, "
		"records.kind, records.symbol, records.context, records.mode, records.usr_id, "
		"records.context_usr_id, records.local "
		"FROM records JOIN files ON files.id = records.file_id "
		"WHERE records.symbol = ?1 AND records.usr_id = ?2 AND records.record = 1 AND records.action = 3 "
		"AND records.kind = 7 ORDER BY files.variant, records.context, files.path, records.line, "
		"records.column";
	static const char *callers_variant =
		"SELECT files.variant, files.path, records.line, records.column, records.record, records.action, "
		"records.kind, records.symbol, records.context, records.mode, records.usr_id, "
		"records.context_usr_id, records.local "
		"FROM records JOIN files ON files.id = records.file_id "
		"WHERE records.symbol = ?1 AND records.usr_id = ?2 AND files.variant = ?3 AND records.record = 1 "
		"AND records.action = 3 AND records.kind = 7 "
		"ORDER BY records.context, files.path, records.line, records.column";
	static const char *callees =
		"SELECT files.variant, files.path, records.line, records.column, records.record, records.action, "
		"records.kind, records.symbol, records.context, records.mode, records.usr_id, "
		"records.context_usr_id, records.local "
		"FROM records JOIN files ON files.id = records.file_id "
		"WHERE records.context = ?1 AND records.context_usr_id = ?2 AND records.record = 1 "
		"AND records.action = 3 AND records.kind = 7 "
		"ORDER BY files.variant, records.symbol, files.path, records.line, records.column";
	static const char *callees_variant =
		"SELECT files.variant, files.path, records.line, records.column, records.record, records.action, "
		"records.kind, records.symbol, records.context, records.mode, records.usr_id, "
		"records.context_usr_id, records.local "
		"FROM records JOIN files ON files.id = records.file_id "
		"WHERE records.context = ?1 AND records.context_usr_id = ?2 AND files.variant = ?3 "
		"AND records.record = 1 AND records.action = 3 AND records.kind = 7 "
		"ORDER BY records.symbol, files.path, records.line, records.column";
	const char *sql;
	sqlite3_stmt *stmt = NULL;
	int ret = -1;

	if (!db || !opts || !opts->function || !opts->function[0] || !callback)
		return -1;
	if (opts->direction != SEMINDEX_DB_CALLERS && opts->direction != SEMINDEX_DB_CALLEES)
		return -1;
	if (opts->direction == SEMINDEX_DB_CALLERS)
		sql = opts->variant ? callers_variant : callers;
	else
		sql = opts->variant ? callees_variant : callees;
	if (prepare(db, sql, &stmt) < 0)
		goto out;
	if (sqlite3_bind_text(stmt, 1, opts->function, -1, SQLITE_TRANSIENT) != SQLITE_OK ||
		sqlite3_bind_int64(stmt, 2, (sqlite3_int64)opts->usr_id) != SQLITE_OK ||
		(opts->variant && sqlite3_bind_text(stmt, 3, opts->variant, -1, SQLITE_TRANSIENT) != SQLITE_OK))
		goto out;
	ret = emit_records(db, stmt, callback, data, 0);
out:
	sqlite3_finalize(stmt);
	return ret;
}

static int query_function_batch(semindex_db_t *db, const semindex_db_function_t *functions, size_t count,
	semindex_db_record_callback_t callback, void *data)
{
	sqlite3_str *query = NULL;
	sqlite3_stmt *stmt = NULL;
	char *sql = NULL;
	size_t i;
	int ret = -1;

	query = sqlite3_str_new(db->handle);
	if (!query)
		goto out;
	sqlite3_str_appendall(query, "WITH requested(variant, symbol, usr_id) AS (VALUES ");
	for (i = 0; i < count; i++)
		sqlite3_str_appendf(query, "%s(?, ?, ?)", i ? ", " : "");
	sqlite3_str_appendall(query,
		") SELECT files.variant, files.path, records.line, records.column, records.record, records.action, "
		"records.kind, records.symbol, records.context, records.mode, records.usr_id, "
		"records.context_usr_id, records.local FROM requested "
		"JOIN records ON records.symbol = requested.symbol AND records.usr_id = requested.usr_id "
		"JOIN files ON files.id = records.file_id AND files.variant = requested.variant "
		"WHERE records.record = 0 AND records.kind = 7 "
		"ORDER BY files.variant, records.symbol, records.action DESC, files.path, records.line, "
		"records.column");
	if (sqlite3_str_errcode(query) != SQLITE_OK)
		goto out;
	sql = sqlite3_str_finish(query);
	query = NULL;
	if (!sql || prepare(db, sql, &stmt) < 0)
		goto out;
	for (i = 0; i < count; i++) {
		int column = i * 3 + 1;

		if (sqlite3_bind_text(stmt, column, functions[i].variant, -1, SQLITE_TRANSIENT) != SQLITE_OK ||
			sqlite3_bind_text(stmt, column + 1, functions[i].symbol, -1, SQLITE_TRANSIENT) != SQLITE_OK ||
			sqlite3_bind_int64(stmt, column + 2, (sqlite3_int64)functions[i].usr_id) != SQLITE_OK)
			goto out;
	}
	ret = emit_records(db, stmt, callback, data, 0);
out:
	if (query)
		sqlite3_str_finish(query);
	sqlite3_free(sql);
	sqlite3_finalize(stmt);
	return ret;
}

int semindex_db_query_functions(semindex_db_t *db, const semindex_db_function_t *functions, size_t count,
	semindex_db_record_callback_t callback, void *data)
{
	size_t batch_size;
	size_t i;
	size_t offset;

	if (!db || (!functions && count) || !callback)
		return -1;
	if (!count)
		return 0;
	for (i = 0; i < count; i++) {
		if (!functions[i].variant || !functions[i].variant[0] || !functions[i].symbol ||
			!functions[i].symbol[0])
			return -1;
	}
	batch_size = sqlite3_limit(db->handle, SQLITE_LIMIT_VARIABLE_NUMBER, -1) / 3;
	if (!batch_size)
		return -1;
	for (offset = 0; offset < count; offset += batch_size) {
		size_t remaining = count - offset;
		size_t current = remaining < batch_size ? remaining : batch_size;
		int ret = query_function_batch(db, functions + offset, current, callback, data);

		if (ret)
			return ret;
	}
	return 0;
}

int semindex_db_find_at(semindex_db_t *db, const char *path, const char *variant, unsigned line, unsigned column,
	semindex_db_record_callback_t callback, void *data)
{
	static const char *with_variant =
		"SELECT files.variant, files.path, records.line, records.column, records.record, records.action, "
		"records.kind, records.symbol, records.context, records.mode, records.usr_id, "
		"records.context_usr_id, records.local "
		"FROM files JOIN records ON records.file_id = files.id "
		"WHERE files.path = ?1 AND files.variant = ?2 AND records.line = ?3 AND records.column <= ?4 "
		"ORDER BY records.column DESC, records.record";
	static const char *all_variants =
		"SELECT files.variant, files.path, records.line, records.column, records.record, records.action, "
		"records.kind, records.symbol, records.context, records.mode, records.usr_id, "
		"records.context_usr_id, records.local "
		"FROM files JOIN records ON records.file_id = files.id "
		"WHERE files.path = ?1 AND records.line = ?2 AND records.column <= ?3 "
		"ORDER BY records.column DESC, records.record";
	sqlite3_stmt *stmt = NULL;
	int line_index = variant ? 3 : 2;
	int ret = -1;

	if (!db || !path || !path[0] || !line || !column || !callback)
		return -1;
	if (prepare(db, variant ? with_variant : all_variants, &stmt) < 0)
		goto out;
	if (sqlite3_bind_text(stmt, 1, path, -1, SQLITE_TRANSIENT) != SQLITE_OK ||
		(variant && sqlite3_bind_text(stmt, 2, variant, -1, SQLITE_TRANSIENT) != SQLITE_OK) ||
		sqlite3_bind_int64(stmt, line_index, line) != SQLITE_OK ||
		sqlite3_bind_int64(stmt, line_index + 1, column) != SQLITE_OK)
		goto out;
	ret = emit_records(db, stmt, callback, data, column);
out:
	sqlite3_finalize(stmt);
	return ret;
}
