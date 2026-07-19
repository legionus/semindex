// SPDX-License-Identifier: GPL-2.0-or-later
#include <sqlite3.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "index_db.h"

#define INDEX_SCHEMA_VERSION 4
#define STRINGIFY_VALUE(value) #value
#define STRINGIFY(value) STRINGIFY_VALUE(value)

enum stored_record_kind {
	STORED_RECORD_SYMBOL,
	STORED_RECORD_USE,
};

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
	if (ret != SQLITE_OK) {
		fprintf(stderr, "semindex: sqlite: %s\n", errmsg ? errmsg : sqlite3_errmsg(db));
		sqlite3_free(errmsg);
		return -1;
	}

	return 0;
}

static int prepare(sqlite3 *db, const char *sql, sqlite3_stmt **stmt)
{
	int ret;

	do {
		ret = sqlite3_prepare_v3(db, sql, -1, SQLITE_PREPARE_PERSISTENT, stmt, NULL);
		if (ret == SQLITE_BUSY || ret == SQLITE_LOCKED)
			sqlite3_sleep(10);
	} while (ret == SQLITE_BUSY || ret == SQLITE_LOCKED);
	if (ret != SQLITE_OK) {
		fprintf(stderr, "semindex: sqlite: %s\n", sqlite3_errmsg(db));
		return -1;
	}

	return 0;
}

static int bind_text(sqlite3_stmt *stmt, int idx, const char *value)
{
	return sqlite3_bind_text(stmt, idx, value ? value : "", -1, SQLITE_TRANSIENT) == SQLITE_OK ? 0 : -1;
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

static int mkdir_one(const char *path)
{
	if (mkdir(path, 0777) == 0 || errno == EEXIST)
		return 0;

	fprintf(stderr, "semindex: failed to create directory '%s': %s\n", path, strerror(errno));
	return -1;
}

static int ensure_parent_directory(const char *path)
{
	char *dir;
	char *p;
	const char *slash;

	slash = strrchr(path, '/');
	if (!slash || slash == path)
		return 0;

	dir = strdup(path);
	if (!dir)
		return -1;

	dir[slash - path] = '\0';
	for (p = dir + 1; *p; p++) {
		if (*p != '/')
			continue;
		*p = '\0';
		if (mkdir_one(dir) < 0)
			goto fail;
		*p = '/';
	}
	if (mkdir_one(dir) < 0)
		goto fail;

	free(dir);
	return 0;
fail:
	free(dir);
	return -1;
}

static int init_schema(sqlite3 *db)
{
	static const char *schema[] = {
		"CREATE TABLE files ("
		"  id INTEGER PRIMARY KEY,"
		"  path TEXT NOT NULL UNIQUE,"
		"  mtime_ns INTEGER NOT NULL,"
		"  size INTEGER NOT NULL"
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
		"  local INTEGER NOT NULL,"
		"  PRIMARY KEY(symbol, record, action, kind, mode, file_id, line, column)"
		") WITHOUT ROWID",
		"CREATE INDEX records_file_idx ON records(file_id)",
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

static int open_writer(const char *path, sqlite3 **db)
{
	if (ensure_parent_directory(path) < 0)
		return -1;
	if (sqlite3_open_v2(path, db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL) != SQLITE_OK) {
		fprintf(stderr, "semindex: failed to open database '%s': %s\n", path,
			*db ? sqlite3_errmsg(*db) : "unknown error");
		return -1;
	}
	if (sqlite3_busy_timeout(*db, INT_MAX) != SQLITE_OK)
		return -1;
	if (exec_sql(*db, "PRAGMA journal_mode = WAL") < 0 || exec_sql(*db, "PRAGMA synchronous = NORMAL") < 0 ||
		exec_sql(*db, "PRAGMA temp_store = MEMORY") < 0 || init_schema(*db) < 0)
		return -1;

	return 0;
}

static int create_staging(sqlite3 *db)
{
	if (exec_sql(db,
		    "CREATE TEMP TABLE staging_files ("
		    "  path TEXT PRIMARY KEY,"
		    "  mtime_ns INTEGER NOT NULL DEFAULT 0,"
		    "  size INTEGER NOT NULL DEFAULT 0,"
		    "  is_main INTEGER NOT NULL DEFAULT 0"
		    ") WITHOUT ROWID") < 0)
		return -1;

	return exec_sql(db,
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
		"  local INTEGER NOT NULL,"
		"  PRIMARY KEY(symbol, record, action, kind, mode, path, line, column)"
		") WITHOUT ROWID");
}

static char *qualified_name(const char *owner, const char *name)
{
	if (owner && owner[0])
		return sqlite3_mprintf("%s.%s", owner, name ? name : "");
	return sqlite3_mprintf("%s", name ? name : "");
}

static int stage_record(sqlite3 *db, sqlite3_stmt *stmt, int record, int action, int kind, unsigned mode,
	const char *file, unsigned line, unsigned column, const char *owner, const char *name, const char *context,
	int local)
{
	char *symbol = qualified_name(owner, name);
	int ret = -1;

	if (!symbol)
		return -1;
	sqlite3_reset(stmt);
	sqlite3_clear_bindings(stmt);
	if (bind_text(stmt, 1, symbol) < 0 || sqlite3_bind_int(stmt, 2, record) != SQLITE_OK ||
		sqlite3_bind_int(stmt, 3, action) != SQLITE_OK || sqlite3_bind_int(stmt, 4, kind) != SQLITE_OK ||
		sqlite3_bind_int64(stmt, 5, mode) != SQLITE_OK || bind_text(stmt, 6, file) < 0 ||
		sqlite3_bind_int64(stmt, 7, line) != SQLITE_OK || sqlite3_bind_int64(stmt, 8, column) != SQLITE_OK ||
		bind_text(stmt, 9, context) < 0 || sqlite3_bind_int(stmt, 10, local) != SQLITE_OK)
		goto out;
	if (sqlite3_step(stmt) != SQLITE_DONE) {
		fprintf(stderr, "semindex: sqlite: %s\n", sqlite3_errmsg(db));
		goto out;
	}

	ret = 0;
out:
	sqlite3_free(symbol);
	return ret;
}

static int stage_records(sqlite3 *db, semindex_t *s)
{
	static const char *sql =
		"INSERT OR IGNORE INTO staging_records(symbol, record, action, kind, mode, path, line, column, "
		"context, local) VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10)";
	sqlite3_stmt *stmt = NULL;
	size_t i;
	int ret = -1;

	if (prepare(db, sql, &stmt) < 0)
		return -1;

	for (i = 0; i < semindex_symbol_count(s); i++) {
		const semindex_symbol_t *sym = semindex_get_symbol(s, i);

		if (!sym)
			goto out;
		if (stage_record(db, stmt, STORED_RECORD_SYMBOL, sym->definition, sym->kind, 0, sym->file, sym->line,
			    sym->column, sym->owner, sym->name, sym->context, sym->local) < 0)
			goto out;
	}
	for (i = 0; i < semindex_use_count(s); i++) {
		const semindex_use_t *use = semindex_get_use(s, i);

		if (!use)
			goto out;
		if (stage_record(db, stmt, STORED_RECORD_USE, use->kind, use->symbol_kind, use->mode, use->file,
			    use->line, use->column, use->owner, use->name, use->context, use->local) < 0)
			goto out;
	}

	ret = 0;
out:
	sqlite3_finalize(stmt);
	return ret;
}

static long long stat_mtime_ns(const struct stat *st)
{
	return (long long)st->st_mtim.tv_sec * 1000000000LL + st->st_mtim.tv_nsec;
}

static int stage_files(sqlite3 *db, const char *main_file)
{
	sqlite3_stmt *select = NULL;
	sqlite3_stmt *update = NULL;
	struct stat main_stat;
	int have_main_stat = main_file && stat(main_file, &main_stat) == 0;
	int step;
	int ret = -1;

	if (exec_sql(db, "INSERT OR IGNORE INTO staging_files(path) SELECT DISTINCT path FROM staging_records") < 0)
		return -1;
	if (main_file) {
		sqlite3_stmt *insert = NULL;

		if (prepare(db, "INSERT OR IGNORE INTO staging_files(path, is_main) VALUES(?1, 1)", &insert) < 0)
			return -1;
		if (bind_text(insert, 1, main_file) < 0 || sqlite3_step(insert) != SQLITE_DONE) {
			sqlite3_finalize(insert);
			return -1;
		}
		sqlite3_finalize(insert);
	}

	if (prepare(db, "SELECT path FROM staging_files", &select) < 0 ||
		prepare(db, "UPDATE staging_files SET mtime_ns = ?2, size = ?3, is_main = ?4 WHERE path = ?1",
			&update) < 0)
		goto out;

	while ((step = sqlite3_step(select)) == SQLITE_ROW) {
		const char *path = (const char *)sqlite3_column_text(select, 0);
		struct stat st;
		long long mtime_ns = 0;
		long long size = 0;
		int is_main = main_file && !strcmp(path, main_file);

		if (stat(path, &st) == 0) {
			mtime_ns = stat_mtime_ns(&st);
			size = st.st_size;
			if (have_main_stat && st.st_dev == main_stat.st_dev && st.st_ino == main_stat.st_ino)
				is_main = 1;
		}

		sqlite3_reset(update);
		sqlite3_clear_bindings(update);
		if (bind_text(update, 1, path) < 0 || sqlite3_bind_int64(update, 2, mtime_ns) != SQLITE_OK ||
			sqlite3_bind_int64(update, 3, size) != SQLITE_OK ||
			sqlite3_bind_int(update, 4, is_main) != SQLITE_OK || sqlite3_step(update) != SQLITE_DONE)
			goto out;
	}
	if (step != SQLITE_DONE)
		goto out;

	ret = 0;
out:
	sqlite3_finalize(select);
	sqlite3_finalize(update);
	return ret;
}

static int merge_staging(sqlite3 *db)
{
	static const char *merge[] = {
		"INSERT OR IGNORE INTO files(path, mtime_ns, size) "
		"SELECT path, mtime_ns, size FROM staging_files",
		"DELETE FROM records WHERE file_id IN ("
		"  SELECT files.id FROM files JOIN staging_files ON staging_files.path = files.path"
		"  WHERE staging_files.is_main OR files.mtime_ns != staging_files.mtime_ns"
		"     OR files.size != staging_files.size"
		")",
		"UPDATE files SET"
		"  mtime_ns = (SELECT mtime_ns FROM staging_files WHERE path = files.path),"
		"  size = (SELECT size FROM staging_files WHERE path = files.path)"
		"WHERE path IN (SELECT path FROM staging_files)",
		"INSERT OR IGNORE INTO records(symbol, record, action, kind, mode, file_id, line, column, "
		"context, local) "
		"SELECT staging_records.symbol, staging_records.record, staging_records.action,"
		"  staging_records.kind, staging_records.mode, files.id, staging_records.line,"
		"  staging_records.column, staging_records.context, staging_records.local"
		" FROM staging_records JOIN files ON files.path = staging_records.path",
	};
	size_t i;
	int ret = -1;

	if (exec_sql(db, "BEGIN IMMEDIATE") < 0)
		return -1;
	for (i = 0; i < sizeof(merge) / sizeof(merge[0]); i++) {
		if (exec_sql(db, merge[i]) < 0)
			goto rollback;
	}
	if (exec_sql(db, "COMMIT") < 0)
		return -1;

	ret = 0;
	return ret;
rollback:
	exec_sql(db, "ROLLBACK");
	return -1;
}

int index_db_store(const char *path, semindex_t *s, const char *main_file)
{
	sqlite3 *db = NULL;
	int ret = -1;

	if (!path || !s)
		return -1;
	if (open_writer(path, &db) < 0)
		goto out;
	if (create_staging(db) < 0 || exec_sql(db, "BEGIN") < 0)
		goto out;
	if (stage_records(db, s) < 0 || stage_files(db, main_file) < 0) {
		exec_sql(db, "ROLLBACK");
		goto out;
	}
	if (exec_sql(db, "COMMIT") < 0 || merge_staging(db) < 0)
		goto out;

	ret = 0;
out:
	if (db)
		sqlite3_close(db);
	return ret;
}
