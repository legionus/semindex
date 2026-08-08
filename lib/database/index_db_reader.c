// SPDX-License-Identifier: GPL-2.0-or-later
#include <sqlite3.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "semindex_database.h"
#include "sqlite.h"

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
	return semindex_sqlite_prepare(db->handle, sql, stmt);
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

static const char *optional_column_text(sqlite3_stmt *stmt, int column)
{
	if (sqlite3_column_type(stmt, column) == SQLITE_NULL)
		return NULL;

	return (const char *)sqlite3_column_text(stmt, column);
}

static semindex_db_record_type_t record_type(int record, int action)
{
	if (record == STORED_RECORD_USE)
		return SEMINDEX_DB_REFERENCE;

	return action ? SEMINDEX_DB_DEFINITION : SEMINDEX_DB_DECLARATION;
}

static int stored_record_type(semindex_db_record_type_t record, int *stored)
{
	switch (record) {
	case SEMINDEX_DB_DECLARATION:
	case SEMINDEX_DB_DEFINITION:
		*stored = STORED_RECORD_SYMBOL;
		break;
	case SEMINDEX_DB_REFERENCE:
		*stored = STORED_RECORD_USE;
		break;
	default:
		return -1;
	}

	return 0;
}

static int valid_cursor(const semindex_db_cursor_t *cursor)
{
	int stored;

	if (!cursor->variant || !cursor->variant[0] || !cursor->path || !cursor->path[0] || !cursor->symbol ||
		!cursor->symbol[0] || !cursor->line || !cursor->column)
		return 0;

	if (stored_record_type(cursor->record, &stored) < 0)
		return 0;

	if (cursor->record == SEMINDEX_DB_DECLARATION && cursor->action)
		return 0;

	if (cursor->record == SEMINDEX_DB_DEFINITION && !cursor->action)
		return 0;

	return 1;
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

	if (semindex_sqlite_open_readonly(path, &db->handle) < 0) {
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

void semindex_db_interrupt(semindex_db_t *db)
{
	if (db && db->handle)
		sqlite3_interrupt(db->handle);
}

int semindex_db_list_variants(semindex_db_t *db, semindex_db_variant_callback_t callback, void *data)
{
	static const char *sql = "SELECT name, repository_root, git_commit FROM variants ORDER BY name";
	sqlite3_stmt *stmt = NULL;
	int step;
	int ret = -1;

	if (!db || !callback)
		return -1;

	if (prepare(db, sql, &stmt) < 0)
		goto out;

	while ((step = sqlite3_step(stmt)) == SQLITE_ROW) {
		semindex_db_variant_t variant = {
			.name = column_text(stmt, 0),
			.repository_root = optional_column_text(stmt, 1),
			.git_commit = optional_column_text(stmt, 2),
		};

		ret = callback(data, &variant);

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
	sqlite3_finalize(stmt);

	return ret;
}

int semindex_db_find_file(semindex_db_t *db, const char *variant, const char *path,
	semindex_db_file_callback_t callback, void *data)
{
	static const char *sql = "SELECT variant, path, mtime_ns, size FROM files WHERE variant = ?1 AND path = ?2";
	sqlite3_stmt *stmt = NULL;
	int step;
	int ret = -1;

	if (!db || !variant || !variant[0] || !path || !path[0] || !callback)
		return -1;

	if (prepare(db, sql, &stmt) < 0)
		goto out;

	SEMINDEX_SQLITE_BIND_OR_GOTO(out, semindex_sqlite_bind_text(stmt, 1, variant));
	SEMINDEX_SQLITE_BIND_OR_GOTO(out, semindex_sqlite_bind_text(stmt, 2, path));

	step = sqlite3_step(stmt);

	if (step == SQLITE_ROW) {
		semindex_db_file_t file = {
			.variant = column_text(stmt, 0),
			.path = column_text(stmt, 1),
			.mtime_ns = sqlite3_column_int64(stmt, 2),
			.size = sqlite3_column_int64(stmt, 3),
		};

		ret = callback(data, &file);

		if (ret)
			goto out;

		step = sqlite3_step(stmt);
	}

	if (step != SQLITE_DONE) {
		fprintf(stderr, "semindex: sqlite: %s\n", sqlite3_errmsg(db->handle));
		ret = -1;
		goto out;
	}

	ret = 0;
out:
	sqlite3_finalize(stmt);

	return ret;
}

int semindex_db_query(semindex_db_t *db, const semindex_db_query_options_t *opts,
	semindex_db_record_callback_t callback, void *data)
{
	semindex_db_query_options_t defaults = { 0 };

	sqlite3_str *query = NULL;
	sqlite3_stmt *stmt = NULL;
	char *sql = NULL;
	int cursor_record;
	int ret = -1;

	if (!db || !callback)
		return -1;

	if (!opts)
		opts = &defaults;

	if (opts->record < SEMINDEX_DB_RECORD_ALL || opts->record > SEMINDEX_DB_RECORD_REFERENCE)
		return -1;

	if (opts->limit > LLONG_MAX)
		return -1;

	if (opts->after && !valid_cursor(opts->after))
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

	if (opts->after) {
		stored_record_type(opts->after->record, &cursor_record);
		sqlite3_str_appendf(query,
			" AND (files.variant, files.path, records.line, records.column, records.symbol,"
			" records.record, records.action, records.kind, records.mode) >"
			" (%Q, %Q, %u, %u, %Q, %d, %u, %d, %u)",
			opts->after->variant, opts->after->path, opts->after->line, opts->after->column,
			opts->after->symbol, cursor_record, opts->after->action, opts->after->kind, opts->after->mode);
	}

	sqlite3_str_appendall(query,
		" ORDER BY files.variant, files.path, records.line, records.column, records.symbol,"
		" records.record, records.action, records.kind, records.mode");

	if (opts->limit)
		sqlite3_str_appendf(query, " LIMIT %lld", (long long)opts->limit);

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

int semindex_db_query_identity(semindex_db_t *db, const semindex_db_identity_query_t *query,
	semindex_db_record_callback_t callback, void *data)
{
	semindex_db_query_options_t options;

	if (!db || !query || !query->identity || !callback)
		return -1;

	if (!query->identity->variant || !query->identity->variant[0] || !query->identity->symbol ||
		!query->identity->symbol[0] || !query->identity->usr_id)
		return -1;

	if (query->identity->kind < SEMINDEX_SYMBOL_VAR || query->identity->kind > SEMINDEX_SYMBOL_FILE)
		return -1;

	options = (semindex_db_query_options_t){
		.symbol = query->identity->symbol,
		.variant = query->identity->variant,
		.after = query->after,
		.limit = query->limit,
		.record = query->record,
		.usr_id = query->identity->usr_id,
		.kind = query->identity->kind,
		.has_usr_id = 1,
		.has_kind = 1,
	};

	return semindex_db_query(db, &options, callback, data);
}

int semindex_db_query_symbol_types(semindex_db_t *db, const semindex_db_symbol_type_query_t *query,
	semindex_db_symbol_type_callback_t callback, void *data)
{
	static const char *base_sql =
		"SELECT files.variant, files.path, symbol_types.symbol, symbol_types.declared_type,"
		" symbol_types.canonical_type, symbol_types.type_symbol, symbol_types.kind,"
		" symbol_types.type_kind, symbol_types.usr_id, symbol_types.type_usr_id"
		" FROM symbol_types JOIN files ON files.id = symbol_types.file_id"
		" WHERE symbol_types.symbol = ?1 AND symbol_types.kind = ?2 AND symbol_types.usr_id = ?3"
		" AND files.variant = ?4 ORDER BY symbol_types.declared_type, symbol_types.canonical_type,"
		" symbol_types.type_symbol, symbol_types.type_kind, symbol_types.type_usr_id, files.path";
	static const char *after_sql =
		"SELECT files.variant, files.path, symbol_types.symbol, symbol_types.declared_type,"
		" symbol_types.canonical_type, symbol_types.type_symbol, symbol_types.kind,"
		" symbol_types.type_kind, symbol_types.usr_id, symbol_types.type_usr_id"
		" FROM symbol_types JOIN files ON files.id = symbol_types.file_id"
		" WHERE symbol_types.symbol = ?1 AND symbol_types.kind = ?2 AND symbol_types.usr_id = ?3"
		" AND files.variant = ?4 AND (symbol_types.declared_type, symbol_types.canonical_type,"
		" symbol_types.type_symbol, symbol_types.type_kind, symbol_types.type_usr_id, files.path)"
		" > (?5, ?6, ?7, ?8, ?9, ?10)"
		" ORDER BY symbol_types.declared_type, symbol_types.canonical_type, symbol_types.type_symbol,"
		" symbol_types.type_kind, symbol_types.type_usr_id, files.path";
	sqlite3_stmt *stmt = NULL;
	const semindex_db_identity_t *identity;
	int step;
	int ret = -1;
	size_t count = 0;

	if (!db || !query || !query->identity || !callback)
		return -1;

	identity = query->identity;

	if (!identity->variant || !identity->variant[0])
		return -1;

	if (!identity->symbol || !identity->symbol[0] || !identity->usr_id)
		return -1;

	if (query->after) {
		if (!query->after->declared_type || !query->after->canonical_type || !query->after->type_symbol ||
			!query->after->path)
			return -1;
	}

	if (identity->kind < SEMINDEX_SYMBOL_VAR || identity->kind > SEMINDEX_SYMBOL_FILE)
		return -1;

	if (prepare(db, query->after ? after_sql : base_sql, &stmt) < 0)
		goto out;

	SEMINDEX_SQLITE_BIND_OR_GOTO(out, semindex_sqlite_bind_text(stmt, 1, identity->symbol));
	SEMINDEX_SQLITE_BIND_OR_GOTO(out, semindex_sqlite_bind_int(stmt, 2, identity->kind));
	SEMINDEX_SQLITE_BIND_OR_GOTO(out, semindex_sqlite_bind_int64(stmt, 3, (sqlite3_int64)identity->usr_id));
	SEMINDEX_SQLITE_BIND_OR_GOTO(out, semindex_sqlite_bind_text(stmt, 4, identity->variant));

	if (query->after) {
		SEMINDEX_SQLITE_BIND_OR_GOTO(out, semindex_sqlite_bind_text(stmt, 5, query->after->declared_type));
		SEMINDEX_SQLITE_BIND_OR_GOTO(out, semindex_sqlite_bind_text(stmt, 6, query->after->canonical_type));
		SEMINDEX_SQLITE_BIND_OR_GOTO(out, semindex_sqlite_bind_text(stmt, 7, query->after->type_symbol));
		SEMINDEX_SQLITE_BIND_OR_GOTO(out, semindex_sqlite_bind_int(stmt, 8, query->after->type_kind));
		SEMINDEX_SQLITE_BIND_OR_GOTO(out,
			semindex_sqlite_bind_int64(stmt, 9, (sqlite3_int64)query->after->type_usr_id));
		SEMINDEX_SQLITE_BIND_OR_GOTO(out, semindex_sqlite_bind_text(stmt, 10, query->after->path));
	}

	while ((!query->limit || count < query->limit) && (step = sqlite3_step(stmt)) == SQLITE_ROW) {
		semindex_db_symbol_type_t type = {
			.variant = column_text(stmt, 0),
			.path = column_text(stmt, 1),
			.symbol = column_text(stmt, 2),
			.declared_type = column_text(stmt, 3),
			.canonical_type = column_text(stmt, 4),
			.type_symbol = column_text(stmt, 5),
			.kind = sqlite3_column_int(stmt, 6),
			.type_kind = sqlite3_column_int(stmt, 7),
			.usr_id = (unsigned long long)sqlite3_column_int64(stmt, 8),
			.type_usr_id = (unsigned long long)sqlite3_column_int64(stmt, 9),
			.has_type_identity = sqlite3_column_int64(stmt, 9) != 0,
		};

		ret = callback(data, &type);
		count++;

		if (ret)
			goto out;
	}

	if (query->limit && count == query->limit) {
		ret = 0;

		goto out;
	}

	if (step != SQLITE_DONE) {
		fprintf(stderr, "semindex: sqlite: %s\n", sqlite3_errmsg(db->handle));

		goto out;
	}

	ret = 0;
out:
	sqlite3_finalize(stmt);

	return ret;
}

int semindex_db_query_function_types(semindex_db_t *db, const semindex_db_function_type_query_t *query,
	semindex_db_function_type_callback_t callback, void *data)
{
	static const char *sql =
		"SELECT files.variant, files.path, function_types.symbol, function_types.name,"
		" function_types.declared_type, function_types.canonical_type, function_types.type_symbol,"
		" function_types.position, function_types.type_kind, function_types.usr_id,"
		" function_types.type_usr_id, function_types.variadic"
		" FROM function_types JOIN files ON files.id = function_types.file_id"
		" WHERE function_types.symbol = ?1 AND function_types.usr_id = ?2 AND files.variant = ?3"
		" AND (?4 IS NULL OR (function_types.position, function_types.declared_type,"
		" function_types.canonical_type, function_types.type_symbol, function_types.type_kind,"
		" function_types.type_usr_id, files.path) > (?4, ?5, ?6, ?7, ?8, ?9, ?10))"
		" ORDER BY function_types.position, function_types.declared_type,"
		" function_types.canonical_type, function_types.type_symbol, function_types.type_kind,"
		" function_types.type_usr_id, files.path";
	const semindex_db_identity_t *identity;
	sqlite3_stmt *stmt = NULL;
	size_t count = 0;
	int step = SQLITE_DONE;
	int ret = -1;

	if (!db || !query || !query->identity || !callback)
		return -1;

	identity = query->identity;

	if (!identity->variant || !identity->variant[0] || !identity->symbol || !identity->symbol[0])
		return -1;

	if (!identity->usr_id || identity->kind != SEMINDEX_SYMBOL_FUNCTION)
		return -1;

	if (prepare(db, sql, &stmt) < 0)
		goto out;

	SEMINDEX_SQLITE_BIND_OR_GOTO(out, semindex_sqlite_bind_text(stmt, 1, identity->symbol));
	SEMINDEX_SQLITE_BIND_OR_GOTO(out, semindex_sqlite_bind_int64(stmt, 2, (sqlite3_int64)identity->usr_id));
	SEMINDEX_SQLITE_BIND_OR_GOTO(out, semindex_sqlite_bind_text(stmt, 3, identity->variant));

	if (query->after) {
		if (!query->after->declared_type || !query->after->canonical_type)
			goto out;

		if (!query->after->type_symbol || !query->after->path)
			goto out;

		SEMINDEX_SQLITE_BIND_OR_GOTO(out, semindex_sqlite_bind_int(stmt, 4, query->after->position));
		SEMINDEX_SQLITE_BIND_OR_GOTO(out, semindex_sqlite_bind_text(stmt, 5, query->after->declared_type));
		SEMINDEX_SQLITE_BIND_OR_GOTO(out, semindex_sqlite_bind_text(stmt, 6, query->after->canonical_type));
		SEMINDEX_SQLITE_BIND_OR_GOTO(out, semindex_sqlite_bind_text(stmt, 7, query->after->type_symbol));
		SEMINDEX_SQLITE_BIND_OR_GOTO(out, semindex_sqlite_bind_int(stmt, 8, query->after->type_kind));
		SEMINDEX_SQLITE_BIND_OR_GOTO(out,
			semindex_sqlite_bind_int64(stmt, 9, (sqlite3_int64)query->after->type_usr_id));
		SEMINDEX_SQLITE_BIND_OR_GOTO(out, semindex_sqlite_bind_text(stmt, 10, query->after->path));
	} else {
		SEMINDEX_SQLITE_BIND_OR_GOTO(out, sqlite3_bind_null(stmt, 4));
	}

	while ((!query->limit || count < query->limit) && (step = sqlite3_step(stmt)) == SQLITE_ROW) {
		semindex_db_function_type_t type = {
			.variant = column_text(stmt, 0),
			.path = column_text(stmt, 1),
			.symbol = column_text(stmt, 2),
			.name = column_text(stmt, 3),
			.declared_type = column_text(stmt, 4),
			.canonical_type = column_text(stmt, 5),
			.type_symbol = column_text(stmt, 6),
			.position = sqlite3_column_int(stmt, 7),
			.type_kind = sqlite3_column_int(stmt, 8),
			.usr_id = (unsigned long long)sqlite3_column_int64(stmt, 9),
			.type_usr_id = (unsigned long long)sqlite3_column_int64(stmt, 10),
			.variadic = sqlite3_column_int(stmt, 11),
			.has_type_identity = sqlite3_column_int64(stmt, 10) != 0,
		};

		ret = callback(data, &type);
		count++;

		if (ret)
			goto out;
	}

	if (query->limit && count == query->limit) {
		ret = 0;

		goto out;
	}

	if (step != SQLITE_DONE) {
		fprintf(stderr, "semindex: sqlite: %s\n", sqlite3_errmsg(db->handle));

		goto out;
	}

	ret = 0;
out:
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

	SEMINDEX_SQLITE_BIND_OR_GOTO(out, semindex_sqlite_bind_text(stmt, 1, opts->function));
	SEMINDEX_SQLITE_BIND_OR_GOTO(out, semindex_sqlite_bind_int64(stmt, 2, (sqlite3_int64)opts->usr_id));

	if (opts->variant)
		SEMINDEX_SQLITE_BIND_OR_GOTO(out, semindex_sqlite_bind_text(stmt, 3, opts->variant));

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

		SEMINDEX_SQLITE_BIND_OR_GOTO(out, semindex_sqlite_bind_text(stmt, column, functions[i].variant));
		SEMINDEX_SQLITE_BIND_OR_GOTO(out, semindex_sqlite_bind_text(stmt, column + 1, functions[i].symbol));
		SEMINDEX_SQLITE_BIND_OR_GOTO(out,
			semindex_sqlite_bind_int64(stmt, column + 2, (sqlite3_int64)functions[i].usr_id));
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
		"FROM files CROSS JOIN records ON records.file_id = files.id "
		"WHERE files.path = ?1 AND files.variant = ?2 AND records.line = ?3 AND records.column <= ?4 "
		"ORDER BY records.column DESC, records.record";
	static const char *all_variants =
		"SELECT files.variant, files.path, records.line, records.column, records.record, records.action, "
		"records.kind, records.symbol, records.context, records.mode, records.usr_id, "
		"records.context_usr_id, records.local "
		"FROM files CROSS JOIN records ON records.file_id = files.id "
		"WHERE files.path = ?1 AND records.line = ?2 AND records.column <= ?3 "
		"ORDER BY records.column DESC, records.record";
	sqlite3_stmt *stmt = NULL;
	int line_index = variant ? 3 : 2;
	int ret = -1;

	if (!db || !path || !path[0] || !line || !column || !callback)
		return -1;

	if (prepare(db, variant ? with_variant : all_variants, &stmt) < 0)
		goto out;

	SEMINDEX_SQLITE_BIND_OR_GOTO(out, semindex_sqlite_bind_text(stmt, 1, path));

	if (variant)
		SEMINDEX_SQLITE_BIND_OR_GOTO(out, semindex_sqlite_bind_text(stmt, 2, variant));

	SEMINDEX_SQLITE_BIND_OR_GOTO(out, semindex_sqlite_bind_int64(stmt, line_index, line));
	SEMINDEX_SQLITE_BIND_OR_GOTO(out, semindex_sqlite_bind_int64(stmt, line_index + 1, column));

	ret = emit_records(db, stmt, callback, data, column);
out:
	sqlite3_finalize(stmt);
	return ret;
}
