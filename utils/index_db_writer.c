// SPDX-License-Identifier: GPL-2.0-or-later
#include <sqlite3.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "filesystem.h"
#include "index_db.h"
#include "output.h"
#include "repository.h"
#include "semindex_database.h"
#include "sqlite.h"

#define INDEX_SCHEMA_VERSION 15
#define STRINGIFY_VALUE(value) #value
#define STRINGIFY(value) STRINGIFY_VALUE(value)

enum stored_record_kind {
	STORED_RECORD_SYMBOL,
	STORED_RECORD_USE,
};

struct optional_record_id {
	unsigned long long value;
	int present;
};

struct staging_record {
	enum stored_record_kind type;
	int action;
	semindex_symbol_kind_t symbol_kind;
	unsigned mode;
	const char *file;
	unsigned line;
	unsigned column;
	const char *owner;
	const char *name;
	const char *context;
	struct optional_record_id usr_id;
	struct optional_record_id context_usr_id;
	int local;
};

struct stored_paths {
	char *root;
	char *main_file;
	char **files;
	size_t count;
};

static void free_stored_paths(struct stored_paths *paths)
{
	size_t i;

	for (i = 0; i < paths->count; i++)
		free(paths->files[i]);

	free(paths->files);
	free(paths->main_file);
	free(paths->root);
}

static int init_stored_paths(struct stored_paths *paths, semindex_t *s, const char *main_file,
	const char *repository_root, int include_local)
{
	size_t i;

	memset(paths, 0, sizeof(*paths));
	paths->root = repository_root ? strdup(repository_root) : semindex_repository_root(main_file);
	paths->main_file = semindex_repository_path(paths->root, main_file);
	paths->count = semindex_file_fingerprint_count(s);

	if (main_file && !paths->main_file)
		goto fail;

	if (!paths->count)
		return 0;

	paths->files = calloc(paths->count, sizeof(*paths->files));

	if (!paths->files)
		goto fail;

	for (i = 0; i < paths->count; i++) {
		const semindex_file_fingerprint_t *fingerprint;

		fingerprint = semindex_get_file_fingerprint(s, i, include_local);

		if (!fingerprint)
			goto fail;

		paths->files[i] = semindex_repository_path(paths->root, fingerprint->file);

		if (!paths->files[i])
			goto fail;
	}

	return 0;

fail:
	free_stored_paths(paths);
	memset(paths, 0, sizeof(*paths));
	return -1;
}

static const char *record_path(const struct stored_paths *paths, size_t index, const char *fallback)
{
	if (index < paths->count && paths->files[index])
		return paths->files[index];

	return fallback;
}

static int exec_sql(sqlite3 *db, const char *sql)
{
	char *errmsg = NULL;
	int ret;

	do {
		sqlite3_free(errmsg);
		errmsg = NULL;

		ret = sqlite3_exec(db, sql, NULL, NULL, &errmsg);

		if (ret == SQLITE_BUSY || ret == SQLITE_LOCKED)
			sqlite3_sleep(10);

	} while (ret == SQLITE_BUSY || ret == SQLITE_LOCKED);

	if (ret == SQLITE_OK)
		return 0;

	fprintf(stderr, "semindex: sqlite: %s\n", errmsg ? errmsg : sqlite3_errmsg(db));
	sqlite3_free(errmsg);
	return -1;
}

static int trace_exec_sql(sqlite3 *db, const char *sql, semindex_trace_t *trace, const char *phase)
{
	semindex_trace_time_t start = semindex_trace_begin(trace);
	int ret = exec_sql(db, sql);

	semindex_trace_end(trace, phase, start);
	return ret;
}

static int prepare(sqlite3 *db, const char *sql, sqlite3_stmt **stmt)
{
	int ret;

	do {
		ret = sqlite3_prepare_v3(db, sql, -1, SQLITE_PREPARE_PERSISTENT, stmt, NULL);

		if (ret == SQLITE_BUSY || ret == SQLITE_LOCKED)
			sqlite3_sleep(10);

	} while (ret == SQLITE_BUSY || ret == SQLITE_LOCKED);

	if (ret == SQLITE_OK)
		return 0;

	fprintf(stderr, "semindex: sqlite: %s\n", sqlite3_errmsg(db));
	return -1;
}

static int schema_version(sqlite3 *db, int *version)
{
	sqlite3_stmt *stmt = NULL;
	int ret = -1;

	if (prepare(db, "PRAGMA user_version", &stmt) < 0)
		return -1;

	if (sqlite3_step(stmt) != SQLITE_ROW)
		goto out;

	*version = sqlite3_column_int(stmt, 0);
	ret = 0;
out:
	sqlite3_finalize(stmt);
	return ret;
}

static int has_user_tables(sqlite3 *db, int *has_tables)
{
	sqlite3_stmt *stmt = NULL;
	int ret = -1;

	if (prepare(db,
		    "SELECT EXISTS(SELECT 1 FROM sqlite_schema "
		    "WHERE type = 'table' AND name NOT LIKE 'sqlite_%')",
		    &stmt) < 0)
		return -1;

	if (sqlite3_step(stmt) != SQLITE_ROW)
		goto out;

	*has_tables = sqlite3_column_int(stmt, 0);
	ret = 0;
out:
	sqlite3_finalize(stmt);
	return ret;
}

static int init_schema(sqlite3 *db)
{
	static const char *schema[] = {
		"CREATE TABLE files ("
		"  id INTEGER PRIMARY KEY,"
		"  variant TEXT NOT NULL,"
		"  path TEXT NOT NULL,"
		"  mtime_ns INTEGER NOT NULL,"
		"  size INTEGER NOT NULL,"
		"  UNIQUE(variant, path)"
		")",
		"CREATE TABLE records ("
		"  symbol TEXT NOT NULL,"
		"  record INTEGER NOT NULL,"
		"  action INTEGER NOT NULL,"
		"  kind INTEGER NOT NULL,"
		"  mode INTEGER NOT NULL,"
		"  file_id INTEGER NOT NULL REFERENCES files(id) ON DELETE CASCADE,"
		"  line INTEGER NOT NULL,"
		"  column INTEGER NOT NULL,"
		"  context TEXT NOT NULL,"
		"  usr_id INTEGER,"
		"  context_usr_id INTEGER,"
		"  local INTEGER NOT NULL,"
		"  PRIMARY KEY(symbol, record, action, kind, mode, file_id, line, column)"
		") WITHOUT ROWID",
		"CREATE INDEX records_file_idx ON records(file_id)",
		"CREATE TABLE symbol_types ("
		"  symbol TEXT NOT NULL,"
		"  kind INTEGER NOT NULL,"
		"  usr_id INTEGER NOT NULL,"
		"  file_id INTEGER NOT NULL REFERENCES files(id) ON DELETE CASCADE,"
		"  declared_type TEXT NOT NULL,"
		"  canonical_type TEXT NOT NULL,"
		"  PRIMARY KEY(symbol, kind, usr_id, file_id, declared_type, canonical_type)"
		") WITHOUT ROWID",
		"CREATE INDEX symbol_types_file_idx ON symbol_types(file_id)",
		"CREATE INDEX records_call_context_idx ON records(context, context_usr_id)"
		" WHERE record = 1 AND action = 3 AND kind = 7",
		"CREATE TABLE variants ("
		"  name TEXT PRIMARY KEY,"
		"  git_commit TEXT,"
		"  repository_root TEXT"
		") WITHOUT ROWID",
		"CREATE TABLE file_fingerprints ("
		"  file_id INTEGER NOT NULL REFERENCES files(id) ON DELETE CASCADE,"
		"  fingerprint BLOB NOT NULL,"
		"  PRIMARY KEY(file_id, fingerprint)"
		") WITHOUT ROWID",
		"PRAGMA user_version = " STRINGIFY(INDEX_SCHEMA_VERSION),
	};
	size_t i;
	int has_tables;
	int version;

	if (exec_sql(db, "PRAGMA foreign_keys = ON") < 0 || schema_version(db, &version) < 0)
		return -1;

	if (version == INDEX_SCHEMA_VERSION)
		return 0;

	if (exec_sql(db, "BEGIN IMMEDIATE") < 0)
		return -1;

	if (schema_version(db, &version) < 0)
		goto rollback;

	if (version == INDEX_SCHEMA_VERSION)
		goto commit;

	if (version != 0) {
		fprintf(stderr, "semindex: database schema version %d is incompatible; remove the old index\n",
			version);
		goto rollback;
	}

	if (has_user_tables(db, &has_tables) < 0)
		goto rollback;

	if (has_tables) {
		fprintf(stderr, "semindex: database schema is incompatible; remove the old index\n");
		goto rollback;
	}

	for (i = 0; i < sizeof(schema) / sizeof(schema[0]); i++) {
		if (exec_sql(db, schema[i]) < 0)
			goto rollback;
	}
commit:
	if (exec_sql(db, "COMMIT") < 0)
		return -1;

	return 0;

rollback:
	exec_sql(db, "ROLLBACK");
	return -1;
}

static int open_writer(const char *path, sqlite3 **db, semindex_trace_t *trace)
{
	semindex_trace_time_t start = semindex_trace_begin(trace);

	if (semindex_ensure_parent_directory(path) < 0) {
		semindex_trace_end(trace, "db.mkdir", start);
		return -1;
	}

	semindex_trace_end(trace, "db.mkdir", start);

	start = semindex_trace_begin(trace);

	if (sqlite3_open_v2(path, db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, NULL) !=
		SQLITE_OK) {
		fprintf(stderr, "semindex: failed to open database '%s': %s\n", path,
			*db ? sqlite3_errmsg(*db) : "unknown error");
		semindex_trace_end(trace, "db.open", start);
		return -1;
	}

	semindex_trace_end(trace, "db.open", start);

	if (sqlite3_busy_timeout(*db, INT_MAX) != SQLITE_OK)
		return -1;

	if (trace_exec_sql(*db, "PRAGMA journal_mode = WAL", trace, "db.journal_mode") < 0 ||
		trace_exec_sql(*db, "PRAGMA synchronous = OFF", trace, "db.synchronous") < 0 ||
		trace_exec_sql(*db, "PRAGMA temp_store = MEMORY", trace, "db.temp_store") < 0)
		return -1;

	start = semindex_trace_begin(trace);

	if (init_schema(*db) < 0) {
		semindex_trace_end(trace, "db.schema", start);
		return -1;
	}

	semindex_trace_end(trace, "db.schema", start);

	return 0;
}

static int create_staging(sqlite3 *db)
{
	if (exec_sql(db,
		    "CREATE TEMP TABLE staging_files ("
		    "  path TEXT PRIMARY KEY,"
		    "  mtime_ns INTEGER NOT NULL DEFAULT 0,"
		    "  size INTEGER NOT NULL DEFAULT 0,"
		    "  is_main INTEGER NOT NULL DEFAULT 0,"
		    "  ordinal INTEGER,"
		    "  fingerprint BLOB,"
		    "  cached INTEGER NOT NULL DEFAULT 0"
		    ") WITHOUT ROWID") < 0)
		return -1;

	if (exec_sql(db,
		    "CREATE TEMP TABLE staging_records ("
		    "  symbol TEXT NOT NULL,"
		    "  record INTEGER NOT NULL,"
		    "  action INTEGER NOT NULL,"
		    "  kind INTEGER NOT NULL,"
		    "  mode INTEGER NOT NULL,"
		    "  path TEXT NOT NULL,"
		    "  line INTEGER NOT NULL,"
		    "  column INTEGER NOT NULL,"
		    "  context TEXT NOT NULL,"
		    "  usr_id INTEGER,"
		    "  context_usr_id INTEGER,"
		    "  local INTEGER NOT NULL,"
		    "  PRIMARY KEY(symbol, record, action, kind, mode, path, line, column)"
		    ") WITHOUT ROWID") < 0)
		return -1;

	return exec_sql(db,
		"CREATE TEMP TABLE staging_symbol_types ("
		"  symbol TEXT NOT NULL,"
		"  kind INTEGER NOT NULL,"
		"  usr_id INTEGER NOT NULL,"
		"  path TEXT NOT NULL,"
		"  declared_type TEXT NOT NULL,"
		"  canonical_type TEXT NOT NULL,"
		"  PRIMARY KEY(symbol, kind, usr_id, path, declared_type, canonical_type)"
		") WITHOUT ROWID");
}

static char *qualified_name(const char *owner, const char *name)
{
	if (owner && owner[0])
		return sqlite3_mprintf("%s.%s", owner, name ? name : "");

	return sqlite3_mprintf("%s", name ? name : "");
}

static int bind_optional_record_id(sqlite3_stmt *stmt, int index, const struct optional_record_id *id)
{
	if (id->present)
		return semindex_sqlite_bind_int64(stmt, index, (sqlite3_int64)id->value);

	return semindex_sqlite_bind_null(stmt, index);
}

static int stage_record(sqlite3_stmt *stmt, const struct staging_record *record)
{
	sqlite3 *db = sqlite3_db_handle(stmt);
	char *symbol = qualified_name(record->owner, record->name);
	int ret = -1;

	if (!symbol)
		return -1;

	sqlite3_reset(stmt);
	sqlite3_clear_bindings(stmt);

	SEMINDEX_SQLITE_BIND_OR_GOTO(out, semindex_sqlite_bind_text(stmt, 1, symbol));
	SEMINDEX_SQLITE_BIND_OR_GOTO(out, semindex_sqlite_bind_int(stmt, 2, record->type));
	SEMINDEX_SQLITE_BIND_OR_GOTO(out, semindex_sqlite_bind_int(stmt, 3, record->action));
	SEMINDEX_SQLITE_BIND_OR_GOTO(out, semindex_sqlite_bind_int(stmt, 4, record->symbol_kind));
	SEMINDEX_SQLITE_BIND_OR_GOTO(out, semindex_sqlite_bind_int64(stmt, 5, record->mode));
	SEMINDEX_SQLITE_BIND_OR_GOTO(out, semindex_sqlite_bind_text(stmt, 6, record->file));
	SEMINDEX_SQLITE_BIND_OR_GOTO(out, semindex_sqlite_bind_int64(stmt, 7, record->line));
	SEMINDEX_SQLITE_BIND_OR_GOTO(out, semindex_sqlite_bind_int64(stmt, 8, record->column));
	SEMINDEX_SQLITE_BIND_OR_GOTO(out, semindex_sqlite_bind_text(stmt, 9, record->context));
	SEMINDEX_SQLITE_BIND_OR_GOTO(out, bind_optional_record_id(stmt, 10, &record->usr_id));
	SEMINDEX_SQLITE_BIND_OR_GOTO(out, bind_optional_record_id(stmt, 11, &record->context_usr_id));
	SEMINDEX_SQLITE_BIND_OR_GOTO(out, semindex_sqlite_bind_int(stmt, 12, record->local));

	if (sqlite3_step(stmt) != SQLITE_DONE) {
		fprintf(stderr, "semindex: sqlite: %s\n", sqlite3_errmsg(db));
		goto out;
	}

	ret = 0;
out:
	sqlite3_free(symbol);
	return ret;
}

static int stage_symbol_type(sqlite3_stmt *stmt, const semindex_symbol_t *symbol, const char *path)
{
	sqlite3 *db = sqlite3_db_handle(stmt);
	char *name = qualified_name(symbol->owner, symbol->name);
	int ret = -1;

	if (!name)
		return -1;

	sqlite3_reset(stmt);
	sqlite3_clear_bindings(stmt);

	SEMINDEX_SQLITE_BIND_OR_GOTO(out, semindex_sqlite_bind_text(stmt, 1, name));
	SEMINDEX_SQLITE_BIND_OR_GOTO(out, semindex_sqlite_bind_int(stmt, 2, symbol->kind));
	SEMINDEX_SQLITE_BIND_OR_GOTO(out, semindex_sqlite_bind_int64(stmt, 3, (sqlite3_int64)symbol->usr_id));
	SEMINDEX_SQLITE_BIND_OR_GOTO(out, semindex_sqlite_bind_text(stmt, 4, path));
	SEMINDEX_SQLITE_BIND_OR_GOTO(out, semindex_sqlite_bind_text(stmt, 5, symbol->type));
	SEMINDEX_SQLITE_BIND_OR_GOTO(out, semindex_sqlite_bind_text(stmt, 6, symbol->canonical_type));

	if (sqlite3_step(stmt) != SQLITE_DONE) {
		fprintf(stderr, "semindex: sqlite: %s\n", sqlite3_errmsg(db));

		goto out;
	}

	ret = 0;
out:
	sqlite3_free(name);

	return ret;
}

static int stage_records(sqlite3 *db, semindex_t *s, const struct stored_paths *paths, int include_local,
	const unsigned char *cached, size_t cached_count, uint64_t *items_in, uint64_t *items_out, uint64_t *types_out)
{
	static const char *sql =
		"INSERT OR IGNORE INTO staging_records(symbol, record, action, kind, mode, path, line, column, "
		"context, usr_id, context_usr_id, local) VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, "
		"?12)";
	static const char *type_sql =
		"INSERT OR IGNORE INTO staging_symbol_types(symbol, kind, usr_id, path, declared_type, canonical_type)"
		" VALUES(?1, ?2, ?3, ?4, ?5, ?6)";
	sqlite3_stmt *stmt = NULL;
	sqlite3_stmt *type_stmt = NULL;
	sqlite3_int64 changes_before = sqlite3_total_changes64(db);
	size_t i;
	int ret = -1;

	*items_in = 0;
	*items_out = 0;
	*types_out = 0;

	if (prepare(db, sql, &stmt) < 0 || prepare(db, type_sql, &type_stmt) < 0)
		goto out;

	for (i = 0; i < semindex_symbol_count(s); i++) {
		const semindex_symbol_t *sym = semindex_get_symbol(s, i);
		struct staging_record record;

		if (!sym)
			goto out;

		if (sym->local && !include_local)
			continue;

		(*items_in)++;

		if (sym->file_index < cached_count && cached[sym->file_index])
			continue;

		record = (struct staging_record) {
			.type = STORED_RECORD_SYMBOL,
			.action = sym->definition,
			.symbol_kind = sym->kind,
			.file = record_path(paths, sym->file_index, sym->file),
			.line = sym->line,
			.column = sym->column,
			.owner = sym->owner,
			.name = sym->name,
			.context = sym->context,
			.usr_id = {
				.value = sym->usr_id,
				.present = sym->usr && sym->usr[0],
			},
			.local = sym->local,
		};

		if (stage_record(stmt, &record) < 0)
			goto out;

		if (sym->type && sym->type[0] && sym->usr && sym->usr[0]) {
			const char *path = record_path(paths, sym->file_index, sym->file);

			if (stage_symbol_type(type_stmt, sym, path) < 0)
				goto out;

			*types_out += sqlite3_changes64(db);
		}
	}

	for (i = 0; i < semindex_use_count(s); i++) {
		const semindex_use_t *use = semindex_get_use(s, i);
		struct staging_record record;

		if (!use)
			goto out;

		if (use->local && !include_local)
			continue;

		(*items_in)++;

		if (use->file_index < cached_count && cached[use->file_index])
			continue;

		record = (struct staging_record) {
			.type = STORED_RECORD_USE,
			.action = use->kind,
			.symbol_kind = use->symbol_kind,
			.mode = use->mode,
			.file = record_path(paths, use->file_index, use->file),
			.line = use->line,
			.column = use->column,
			.owner = use->owner,
			.name = use->name,
			.context = use->context,
			.usr_id = {
				.value = use->usr_id,
				.present = use->usr && use->usr[0],
			},
			.context_usr_id = {
				.value = use->context_usr_id,
				.present = use->context_usr && use->context_usr[0],
			},
			.local = use->local,
		};

		if (stage_record(stmt, &record) < 0)
			goto out;
	}

	ret = 0;
out:
	*items_out = (uint64_t)(sqlite3_total_changes64(db) - changes_before);
	*items_out -= *types_out;
	sqlite3_finalize(stmt);
	sqlite3_finalize(type_stmt);
	return ret;
}

static long long stat_mtime_ns(const struct stat *st)
{
	return (long long)st->st_mtim.tv_sec * 1000000000LL + st->st_mtim.tv_nsec;
}

static int stage_files(sqlite3 *db, semindex_t *s, const struct stored_paths *paths, const char *main_file,
	const char *variant, int include_local, unsigned char *cached, size_t cached_count, uint64_t *items_in,
	uint64_t *items_out)
{
	static const char *insert_sql =
		"INSERT INTO staging_files(path, mtime_ns, size, is_main, ordinal, fingerprint)"
		" VALUES(?1, ?2, ?3, ?4, ?5, ?6) ON CONFLICT(path) DO UPDATE SET"
		" mtime_ns = excluded.mtime_ns, size = excluded.size, is_main = excluded.is_main,"
		" ordinal = excluded.ordinal, fingerprint = excluded.fingerprint";

	sqlite3_stmt *insert = NULL;
	sqlite3_stmt *select = NULL;
	char *cache_sql = NULL;
	struct stat main_stat;
	int have_main_stat = main_file && stat(main_file, &main_stat) == 0;
	uint64_t cache_candidates = 0;
	size_t i;
	int step;
	int ret = -1;

	*items_in = 0;
	*items_out = 0;

	if (prepare(db, insert_sql, &insert) < 0)
		goto out;

	for (i = 0; i < cached_count; i++) {
		const semindex_file_fingerprint_t *fingerprint = semindex_get_file_fingerprint(s, i, include_local);
		struct stat st;
		long long mtime_ns = 0;
		long long size = 0;
		int is_main = 0;

		if (!fingerprint)
			goto out;

		if (!fingerprint->record_count)
			continue;

		if (stat(fingerprint->file, &st) == 0) {
			mtime_ns = stat_mtime_ns(&st);
			size = st.st_size;

			if (have_main_stat && st.st_dev == main_stat.st_dev && st.st_ino == main_stat.st_ino)
				is_main = 1;
		}

		if (!is_main)
			cache_candidates++;

		sqlite3_reset(insert);
		sqlite3_clear_bindings(insert);

		SEMINDEX_SQLITE_BIND_OR_GOTO(out, semindex_sqlite_bind_text(insert, 1, paths->files[i]));
		SEMINDEX_SQLITE_BIND_OR_GOTO(out, semindex_sqlite_bind_int64(insert, 2, mtime_ns));
		SEMINDEX_SQLITE_BIND_OR_GOTO(out, semindex_sqlite_bind_int64(insert, 3, size));
		SEMINDEX_SQLITE_BIND_OR_GOTO(out, semindex_sqlite_bind_int(insert, 4, is_main));
		SEMINDEX_SQLITE_BIND_OR_GOTO(out, semindex_sqlite_bind_int64(insert, 5, i));

		if (is_main)
			SEMINDEX_SQLITE_BIND_OR_GOTO(out, semindex_sqlite_bind_null(insert, 6));
		else
			SEMINDEX_SQLITE_BIND_OR_GOTO(out,
				semindex_sqlite_bind_blob64_static(insert, 6, fingerprint->data,
					sizeof(fingerprint->data)));

		if (sqlite3_step(insert) != SQLITE_DONE)
			goto out;
	}

	if (main_file) {
		sqlite3_stmt *main_insert = NULL;
		long long mtime_ns = have_main_stat ? stat_mtime_ns(&main_stat) : 0;
		long long size = have_main_stat ? main_stat.st_size : 0;

		if (prepare(db,
			    "INSERT INTO staging_files(path, mtime_ns, size, is_main) VALUES(?1, ?2, ?3, 1)"
			    " ON CONFLICT(path) DO UPDATE SET mtime_ns = excluded.mtime_ns,"
			    " size = excluded.size, is_main = 1, fingerprint = NULL",
			    &main_insert) < 0)
			goto out;

		SEMINDEX_SQLITE_BIND_OR_GOTO(main_out, semindex_sqlite_bind_text(main_insert, 1, paths->main_file));
		SEMINDEX_SQLITE_BIND_OR_GOTO(main_out, semindex_sqlite_bind_int64(main_insert, 2, mtime_ns));
		SEMINDEX_SQLITE_BIND_OR_GOTO(main_out, semindex_sqlite_bind_int64(main_insert, 3, size));

		if (sqlite3_step(main_insert) != SQLITE_DONE) {
			goto main_out;
		}

		goto main_done;

main_out:
		sqlite3_finalize(main_insert);
		goto out;

main_done:
		sqlite3_finalize(main_insert);
	}

	*items_in = cache_candidates;

	if (!cache_candidates) {
		ret = 0;
		goto out;
	}

	cache_sql =
		sqlite3_mprintf("UPDATE staging_files SET cached = 1 WHERE is_main = 0 AND mtime_ns != 0 AND EXISTS ("
				" SELECT 1 FROM files JOIN file_fingerprints ON file_fingerprints.file_id = files.id"
				" WHERE files.variant = %Q AND files.path = staging_files.path"
				" AND files.mtime_ns = staging_files.mtime_ns AND files.size = staging_files.size"
				" AND file_fingerprints.fingerprint = staging_files.fingerprint)",
			variant);

	if (!cache_sql || exec_sql(db, cache_sql) < 0) {
		sqlite3_free(cache_sql);
		cache_sql = NULL;
		goto out;
	}

	sqlite3_free(cache_sql);
	cache_sql = NULL;

	sqlite3_finalize(select);
	select = NULL;

	if (prepare(db, "SELECT ordinal FROM staging_files WHERE cached = 1", &select) < 0)
		goto out;

	while ((step = sqlite3_step(select)) == SQLITE_ROW) {
		sqlite3_int64 ordinal = sqlite3_column_int64(select, 0);

		if (ordinal < 0 || (uint64_t)ordinal >= cached_count)
			goto out;

		cached[ordinal] = 1;
		(*items_out)++;
	}

	if (step != SQLITE_DONE)
		goto out;

	ret = 0;
out:
	sqlite3_free(cache_sql);
	sqlite3_finalize(insert);
	sqlite3_finalize(select);

	return ret;
}

static int cached_files_valid(sqlite3 *db, const char *variant)
{
	static const char *sql = "SELECT 1 FROM staging_files WHERE cached = 1 AND NOT EXISTS ("
				 " SELECT 1 FROM files JOIN file_fingerprints ON file_fingerprints.file_id = files.id"
				 " WHERE files.variant = ?1 AND files.path = staging_files.path"
				 " AND files.mtime_ns = staging_files.mtime_ns AND files.size = staging_files.size"
				 " AND file_fingerprints.fingerprint = staging_files.fingerprint) LIMIT 1";
	sqlite3_stmt *stmt = NULL;
	int step;
	int ret = -1;

	if (prepare(db, sql, &stmt) < 0)
		goto out;

	if (semindex_sqlite_bind_text(stmt, 1, variant) < 0)
		goto out;

	step = sqlite3_step(stmt);

	if (step == SQLITE_DONE)
		ret = 1;

	else if (step == SQLITE_ROW)
		ret = 0;
	else
		fprintf(stderr, "semindex: sqlite: %s\n", sqlite3_errmsg(db));
out:
	sqlite3_finalize(stmt);

	return ret;
}

static int store_variant_metadata(sqlite3 *db, const char *variant, const char *repository_root, const char *git_commit)
{
	static const char *sql = "INSERT INTO variants(name, git_commit, repository_root) VALUES(?1, ?2, ?3)"
				 " ON CONFLICT(name) DO UPDATE SET"
				 " git_commit = coalesce(excluded.git_commit, variants.git_commit),"
				 " repository_root = coalesce(excluded.repository_root, variants.repository_root)"
				 " WHERE (excluded.git_commit IS NOT NULL"
				 " AND variants.git_commit IS NOT excluded.git_commit)"
				 " OR (excluded.repository_root IS NOT NULL"
				 " AND variants.repository_root IS NOT excluded.repository_root)";
	sqlite3_stmt *stmt = NULL;
	int step;
	int ret = -1;

	if (prepare(db, sql, &stmt) < 0)
		goto out;

	if (semindex_sqlite_bind_text(stmt, 1, variant) < 0)
		goto out;

	if (git_commit) {
		if (semindex_sqlite_bind_text(stmt, 2, git_commit) < 0)
			goto out;
	} else if (sqlite3_bind_null(stmt, 2) != SQLITE_OK) {
		fprintf(stderr, "semindex: sqlite: %s\n", sqlite3_errmsg(db));
		goto out;
	}

	if (repository_root) {
		if (semindex_sqlite_bind_text(stmt, 3, repository_root) < 0)
			goto out;
	} else if (sqlite3_bind_null(stmt, 3) != SQLITE_OK) {
		fprintf(stderr, "semindex: sqlite: %s\n", sqlite3_errmsg(db));
		goto out;
	}

	step = sqlite3_step(stmt);

	if (step == SQLITE_DONE)
		ret = 0;
	else
		fprintf(stderr, "semindex: sqlite: %s\n", sqlite3_errmsg(db));
out:
	sqlite3_finalize(stmt);

	return ret;
}

static int merge_staging(sqlite3 *db, const char *variant, uint64_t staged_records, uint64_t staged_types,
	uint64_t fingerprint_attempts, const index_db_provenance_t *provenance, semindex_trace_t *trace)
{
	static const char *phases[] = {
		"db.merge.files_insert",
		"db.merge.records_delete",
		"db.merge.symbol_types_delete",
		"db.merge.fingerprints_delete",
		"db.merge.files_update",
		"db.merge.records_insert",
		"db.merge.symbol_types_insert",
		"db.merge.fingerprints_insert",
	};
	char *merge[8] = { NULL };
	size_t i;
	int valid;
	int ret = -1;

	merge[0] = sqlite3_mprintf("INSERT OR IGNORE INTO files(variant, path, mtime_ns, size) "
				   "SELECT %Q, path, mtime_ns, size FROM staging_files",
		variant);
	merge[1] =
		sqlite3_mprintf("DELETE FROM records WHERE file_id IN ("
				"  SELECT files.id FROM files JOIN staging_files ON staging_files.path = files.path"
				"  WHERE files.variant = %Q AND (staging_files.is_main"
				"     OR files.mtime_ns != staging_files.mtime_ns OR files.size != staging_files.size)"
				")",
			variant);
	merge[2] =
		sqlite3_mprintf("DELETE FROM symbol_types WHERE file_id IN ("
				"  SELECT files.id FROM files JOIN staging_files ON staging_files.path = files.path"
				"  WHERE files.variant = %Q AND (staging_files.is_main"
				"     OR files.mtime_ns != staging_files.mtime_ns OR files.size != staging_files.size)"
				")",
			variant);
	merge[3] =
		sqlite3_mprintf("DELETE FROM file_fingerprints WHERE file_id IN ("
				"  SELECT files.id FROM files JOIN staging_files ON staging_files.path = files.path"
				"  WHERE files.variant = %Q AND (staging_files.is_main"
				"     OR files.mtime_ns != staging_files.mtime_ns OR files.size != staging_files.size)"
				")",
			variant);
	merge[4] = sqlite3_mprintf("UPDATE files SET"
				   "  mtime_ns = (SELECT mtime_ns FROM staging_files WHERE path = files.path),"
				   "  size = (SELECT size FROM staging_files WHERE path = files.path)"
				   "WHERE variant = %Q AND path IN (SELECT path FROM staging_files)",
		variant);
	merge[5] = sqlite3_mprintf(
		"INSERT OR IGNORE INTO records(symbol, record, action, kind, mode, file_id, line, column, context, "
		"usr_id, context_usr_id, local) "
		"SELECT staging_records.symbol, staging_records.record, staging_records.action,"
		"  staging_records.kind, staging_records.mode, files.id, staging_records.line,"
		"  staging_records.column, staging_records.context, staging_records.usr_id,"
		"  staging_records.context_usr_id, staging_records.local"
		" FROM staging_records JOIN files"
		" ON files.path = staging_records.path AND files.variant = %Q",
		variant);
	merge[6] = sqlite3_mprintf(
		"INSERT OR IGNORE INTO symbol_types(symbol, kind, usr_id, file_id, declared_type, canonical_type)"
		" SELECT staging_symbol_types.symbol, staging_symbol_types.kind, staging_symbol_types.usr_id,"
		"  files.id, staging_symbol_types.declared_type, staging_symbol_types.canonical_type"
		" FROM staging_symbol_types JOIN files"
		" ON files.path = staging_symbol_types.path AND files.variant = %Q",
		variant);
	merge[7] = sqlite3_mprintf("INSERT OR IGNORE INTO file_fingerprints(file_id, fingerprint)"
				   " SELECT files.id, staging_files.fingerprint FROM staging_files JOIN files"
				   " ON files.path = staging_files.path AND files.variant = %Q"
				   " WHERE staging_files.fingerprint IS NOT NULL",
		variant);

	for (i = 0; i < sizeof(merge) / sizeof(merge[0]); i++) {
		if (!merge[i])
			goto out;
	}

	if (trace_exec_sql(db, "BEGIN IMMEDIATE", trace, "db.merge.begin") < 0)
		goto out;

	valid = cached_files_valid(db, variant);

	if (valid < 0)
		goto rollback;

	if (!valid)
		goto stale;

	for (i = 0; i < sizeof(merge) / sizeof(merge[0]); i++) {
		if (i >= 5) {
			semindex_trace_time_t start = semindex_trace_begin(trace);
			int merge_ret = exec_sql(db, merge[i]);
			uint64_t inserted = merge_ret < 0 ? 0 : (uint64_t)sqlite3_changes64(db);
			uint64_t attempted = i == 5 ? staged_records : i == 6 ? staged_types : fingerprint_attempts;

			semindex_trace_end_counted(trace, phases[i], start, attempted, inserted);

			if (merge_ret < 0)
				goto rollback;

		} else if (trace_exec_sql(db, merge[i], trace, phases[i]) < 0) {
			goto rollback;
		}
	}

	{
		semindex_trace_time_t start = semindex_trace_begin(trace);
		int metadata_ret;

		metadata_ret = store_variant_metadata(db, variant, provenance->repository_root, provenance->git_commit);
		semindex_trace_end(trace, "db.merge.variant", start);

		if (metadata_ret < 0)
			goto rollback;
	}

	if (trace_exec_sql(db, "COMMIT", trace, "db.merge.commit") < 0)
		goto out;

	ret = 0;
	goto out;

rollback:
	exec_sql(db, "ROLLBACK");
	goto out;

stale:
	exec_sql(db, "ROLLBACK");
	ret = 1;
out:
	for (i = 0; i < sizeof(merge) / sizeof(merge[0]); i++)
		sqlite3_free(merge[i]);
	return ret;
}

int index_db_store(const index_db_store_request_t *request)
{
	sqlite3 *db = NULL;
	semindex_trace_time_t start;
	struct stored_paths paths;
	unsigned char *cached = NULL;
	size_t cached_count;
	uint64_t records_in;
	uint64_t records_staged;
	uint64_t types_staged;
	uint64_t files_in;
	uint64_t files_cached;
	int merge_ret;
	int ret = -1;

	if (!request)
		return -1;

	if (!request->path || !request->index || !request->variant || !request->variant[0])
		return -1;

	if (init_stored_paths(&paths, request->index, request->main_file, request->provenance.repository_root,
		    request->include_local) < 0)
		return -1;

	cached_count = semindex_file_fingerprint_count(request->index);

	if (cached_count) {
		cached = calloc(cached_count, sizeof(*cached));

		if (!cached)
			goto out;
	}

	if (open_writer(request->path, &db, request->trace) < 0)
		goto out;

	start = semindex_trace_begin(request->trace);

	if (create_staging(db) < 0) {
		semindex_trace_end(request->trace, "db.staging_schema", start);
		goto out;
	}

	semindex_trace_end(request->trace, "db.staging_schema", start);

	if (trace_exec_sql(db, "BEGIN", request->trace, "db.staging_begin") < 0)
		goto out;

	start = semindex_trace_begin(request->trace);

	if (stage_files(db, request->index, &paths, request->main_file, request->variant, request->include_local,
		    cached, cached_count, &files_in, &files_cached) < 0) {
		semindex_trace_end_counted(request->trace, "db.stage_files", start, files_in, files_cached);
		exec_sql(db, "ROLLBACK");
		goto out;
	}

	semindex_trace_end_counted(request->trace, "db.stage_files", start, files_in, files_cached);

	start = semindex_trace_begin(request->trace);

	if (stage_records(db, request->index, &paths, request->include_local, cached, cached_count, &records_in,
		    &records_staged, &types_staged) < 0) {
		semindex_trace_end_counted(request->trace, "db.stage_records", start, records_in, records_staged);
		exec_sql(db, "ROLLBACK");
		goto out;
	}

	semindex_trace_end_counted(request->trace, "db.stage_records", start, records_in, records_staged);

	if (trace_exec_sql(db, "COMMIT", request->trace, "db.staging_commit") < 0)
		goto out;

	merge_ret = merge_staging(db, request->variant, records_staged, types_staged, files_in, &request->provenance,
		request->trace);

	if (merge_ret < 0)
		goto out;

	if (merge_ret > 0) {
		uint64_t retry_in;
		uint64_t retry_staged;
		uint64_t retry_types;

		if (cached_count)
			memset(cached, 0, cached_count);

		if (trace_exec_sql(db, "BEGIN", request->trace, "db.staging_retry_begin") < 0)
			goto out;

		if (exec_sql(db, "UPDATE staging_files SET cached = 0") < 0) {
			exec_sql(db, "ROLLBACK");
			goto out;
		}

		start = semindex_trace_begin(request->trace);

		if (stage_records(db, request->index, &paths, request->include_local, cached, cached_count, &retry_in,
			    &retry_staged, &retry_types) < 0) {
			semindex_trace_end_counted(request->trace, "db.stage_records_retry", start, retry_in,
				retry_staged);
			exec_sql(db, "ROLLBACK");
			goto out;
		}

		semindex_trace_end_counted(request->trace, "db.stage_records_retry", start, retry_in, retry_staged);

		records_staged += retry_staged;
		types_staged += retry_types;

		if (trace_exec_sql(db, "COMMIT", request->trace, "db.staging_retry_commit") < 0 ||
			merge_staging(db, request->variant, records_staged, types_staged, files_in,
				&request->provenance, request->trace) != 0)
			goto out;
	}

	ret = 0;
out:
	if (db) {
		start = semindex_trace_begin(request->trace);
		sqlite3_close(db);
		semindex_trace_end(request->trace, "db.close", start);
	}

	free(cached);
	free_stored_paths(&paths);
	return ret;
}
