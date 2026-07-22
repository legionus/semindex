// SPDX-License-Identifier: GPL-2.0-or-later
#include <sqlite3.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "index_db.h"
#include "output.h"

#define INDEX_SCHEMA_VERSION 8
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
		"CREATE INDEX records_call_context_idx ON records(context, context_usr_id)"
		" WHERE record = 1 AND action = 3 AND kind = 7",
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
	semindex_trace_time_t start;

	start = semindex_trace_begin(trace);
	if (ensure_parent_directory(path) < 0) {
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
		"  usr_id INTEGER,"
		"  context_usr_id INTEGER,"
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
	const char *usr, unsigned long long usr_id, const char *context_usr, unsigned long long context_usr_id,
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
		bind_text(stmt, 9, context) < 0 ||
		(usr && usr[0] ? sqlite3_bind_int64(stmt, 10, (sqlite3_int64)usr_id) : sqlite3_bind_null(stmt, 10)) !=
			SQLITE_OK ||
		(context_usr && context_usr[0] ? sqlite3_bind_int64(stmt, 11, (sqlite3_int64)context_usr_id)
					       : sqlite3_bind_null(stmt, 11)) != SQLITE_OK ||
		sqlite3_bind_int(stmt, 12, local) != SQLITE_OK)
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

static int stage_records(sqlite3 *db, semindex_t *s, int include_local, const unsigned char *cached,
	size_t cached_count, uint64_t *items_in, uint64_t *items_out)
{
	static const char *sql =
		"INSERT OR IGNORE INTO staging_records(symbol, record, action, kind, mode, path, line, column, "
		"context, usr_id, context_usr_id, local) VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, "
		"?12)";
	sqlite3_stmt *stmt = NULL;
	sqlite3_int64 changes_before = sqlite3_total_changes64(db);
	size_t i;
	int ret = -1;

	*items_in = 0;
	*items_out = 0;

	if (prepare(db, sql, &stmt) < 0)
		goto out;

	for (i = 0; i < semindex_symbol_count(s); i++) {
		const semindex_symbol_t *sym = semindex_get_symbol(s, i);

		if (!sym)
			goto out;
		if (sym->local && !include_local)
			continue;
		(*items_in)++;
		if (sym->file_index < cached_count && cached[sym->file_index])
			continue;
		if (stage_record(db, stmt, STORED_RECORD_SYMBOL, sym->definition, sym->kind, 0, sym->file, sym->line,
			    sym->column, sym->owner, sym->name, sym->context, NULL, 0, NULL, 0, sym->local) < 0)
			goto out;
	}
	for (i = 0; i < semindex_use_count(s); i++) {
		const semindex_use_t *use = semindex_get_use(s, i);
		int direct_call;

		if (!use)
			goto out;
		if (use->local && !include_local)
			continue;
		(*items_in)++;
		if (use->file_index < cached_count && cached[use->file_index])
			continue;
		direct_call = use->kind == SEMINDEX_USE_CALL && use->symbol_kind == SEMINDEX_SYMBOL_FUNCTION;
		if (stage_record(db, stmt, STORED_RECORD_USE, use->kind, use->symbol_kind, use->mode, use->file,
			    use->line, use->column, use->owner, use->name, use->context, direct_call ? use->usr : NULL,
			    use->usr_id, direct_call ? use->context_usr : NULL, use->context_usr_id, use->local) < 0)
			goto out;
	}

	ret = 0;
out:
	*items_out = (uint64_t)(sqlite3_total_changes64(db) - changes_before);
	sqlite3_finalize(stmt);
	return ret;
}

static long long stat_mtime_ns(const struct stat *st)
{
	return (long long)st->st_mtim.tv_sec * 1000000000LL + st->st_mtim.tv_nsec;
}

static int stage_files(sqlite3 *db, semindex_t *s, const char *main_file, const char *variant, int include_local,
	unsigned char *cached, size_t cached_count, uint64_t *items_in, uint64_t *items_out)
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
		if (bind_text(insert, 1, fingerprint->file) < 0 ||
			sqlite3_bind_int64(insert, 2, mtime_ns) != SQLITE_OK ||
			sqlite3_bind_int64(insert, 3, size) != SQLITE_OK ||
			sqlite3_bind_int(insert, 4, is_main) != SQLITE_OK ||
			sqlite3_bind_int64(insert, 5, i) != SQLITE_OK ||
			(is_main ? sqlite3_bind_null(insert, 6)
				 : sqlite3_bind_blob(insert, 6, fingerprint->data, sizeof(fingerprint->data),
					   SQLITE_STATIC)) != SQLITE_OK ||
			sqlite3_step(insert) != SQLITE_DONE)
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
		if (bind_text(main_insert, 1, main_file) < 0 ||
			sqlite3_bind_int64(main_insert, 2, mtime_ns) != SQLITE_OK ||
			sqlite3_bind_int64(main_insert, 3, size) != SQLITE_OK ||
			sqlite3_step(main_insert) != SQLITE_DONE) {
			sqlite3_finalize(main_insert);
			goto out;
		}
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

	if (prepare(db, sql, &stmt) < 0 || bind_text(stmt, 1, variant) < 0)
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

static int merge_staging(sqlite3 *db, const char *variant, uint64_t staged_records, uint64_t fingerprint_attempts,
	semindex_trace_t *trace)
{
	static const char *phases[] = {
		"db.merge.files_insert",
		"db.merge.records_delete",
		"db.merge.fingerprints_delete",
		"db.merge.files_update",
		"db.merge.records_insert",
		"db.merge.fingerprints_insert",
	};
	char *merge[6] = { NULL };
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
		sqlite3_mprintf("DELETE FROM file_fingerprints WHERE file_id IN ("
				"  SELECT files.id FROM files JOIN staging_files ON staging_files.path = files.path"
				"  WHERE files.variant = %Q AND (staging_files.is_main"
				"     OR files.mtime_ns != staging_files.mtime_ns OR files.size != staging_files.size)"
				")",
			variant);
	merge[3] = sqlite3_mprintf("UPDATE files SET"
				   "  mtime_ns = (SELECT mtime_ns FROM staging_files WHERE path = files.path),"
				   "  size = (SELECT size FROM staging_files WHERE path = files.path)"
				   "WHERE variant = %Q AND path IN (SELECT path FROM staging_files)",
		variant);
	merge[4] = sqlite3_mprintf(
		"INSERT OR IGNORE INTO records(symbol, record, action, kind, mode, file_id, line, column, context, "
		"usr_id, context_usr_id, local) "
		"SELECT staging_records.symbol, staging_records.record, staging_records.action,"
		"  staging_records.kind, staging_records.mode, files.id, staging_records.line,"
		"  staging_records.column, staging_records.context, staging_records.usr_id,"
		"  staging_records.context_usr_id, staging_records.local"
		" FROM staging_records JOIN files"
		" ON files.path = staging_records.path AND files.variant = %Q",
		variant);
	merge[5] = sqlite3_mprintf("INSERT OR IGNORE INTO file_fingerprints(file_id, fingerprint)"
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
		if (i == 4 || i == 5) {
			semindex_trace_time_t start = semindex_trace_begin(trace);
			int merge_ret = exec_sql(db, merge[i]);
			uint64_t inserted = merge_ret < 0 ? 0 : (uint64_t)sqlite3_changes64(db);
			uint64_t attempted = i == 4 ? staged_records : fingerprint_attempts;

			semindex_trace_end_counted(trace, phases[i], start, attempted, inserted);
			if (merge_ret < 0)
				goto rollback;
		} else if (trace_exec_sql(db, merge[i], trace, phases[i]) < 0) {
			goto rollback;
		}
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

int index_db_store(const char *path, semindex_t *s, const char *main_file, const char *variant, int include_local,
	semindex_trace_t *trace)
{
	sqlite3 *db = NULL;
	semindex_trace_time_t start;
	unsigned char *cached = NULL;
	size_t cached_count;
	uint64_t records_in;
	uint64_t records_staged;
	uint64_t files_in;
	uint64_t files_cached;
	int merge_ret;
	int ret = -1;

	if (!path || !s || !variant || !variant[0])
		return -1;
	cached_count = semindex_file_fingerprint_count(s);
	if (cached_count) {
		cached = calloc(cached_count, sizeof(*cached));
		if (!cached)
			return -1;
	}
	if (open_writer(path, &db, trace) < 0)
		goto out;
	start = semindex_trace_begin(trace);
	if (create_staging(db) < 0) {
		semindex_trace_end(trace, "db.staging_schema", start);
		goto out;
	}
	semindex_trace_end(trace, "db.staging_schema", start);
	if (trace_exec_sql(db, "BEGIN", trace, "db.staging_begin") < 0)
		goto out;
	start = semindex_trace_begin(trace);
	if (stage_files(db, s, main_file, variant, include_local, cached, cached_count, &files_in, &files_cached) < 0) {
		semindex_trace_end_counted(trace, "db.stage_files", start, files_in, files_cached);
		exec_sql(db, "ROLLBACK");
		goto out;
	}
	semindex_trace_end_counted(trace, "db.stage_files", start, files_in, files_cached);
	start = semindex_trace_begin(trace);
	if (stage_records(db, s, include_local, cached, cached_count, &records_in, &records_staged) < 0) {
		semindex_trace_end_counted(trace, "db.stage_records", start, records_in, records_staged);
		exec_sql(db, "ROLLBACK");
		goto out;
	}
	semindex_trace_end_counted(trace, "db.stage_records", start, records_in, records_staged);
	if (trace_exec_sql(db, "COMMIT", trace, "db.staging_commit") < 0)
		goto out;
	merge_ret = merge_staging(db, variant, records_staged, files_in, trace);
	if (merge_ret < 0)
		goto out;
	if (merge_ret > 0) {
		uint64_t retry_in;
		uint64_t retry_staged;

		if (cached_count)
			memset(cached, 0, cached_count);
		if (trace_exec_sql(db, "BEGIN", trace, "db.staging_retry_begin") < 0)
			goto out;
		if (exec_sql(db, "UPDATE staging_files SET cached = 0") < 0) {
			exec_sql(db, "ROLLBACK");
			goto out;
		}
		start = semindex_trace_begin(trace);
		if (stage_records(db, s, include_local, cached, cached_count, &retry_in, &retry_staged) < 0) {
			semindex_trace_end_counted(trace, "db.stage_records_retry", start, retry_in, retry_staged);
			exec_sql(db, "ROLLBACK");
			goto out;
		}
		semindex_trace_end_counted(trace, "db.stage_records_retry", start, retry_in, retry_staged);
		records_staged += retry_staged;
		if (trace_exec_sql(db, "COMMIT", trace, "db.staging_retry_commit") < 0 ||
			merge_staging(db, variant, records_staged, files_in, trace) != 0)
			goto out;
	}

	ret = 0;
out:
	if (db) {
		start = semindex_trace_begin(trace);
		sqlite3_close(db);
		semindex_trace_end(trace, "db.close", start);
	}
	free(cached);
	return ret;
}

static int pattern_uses_glob(const char *pattern)
{
	return pattern && strpbrk(pattern, "*?[]");
}

static int append_search_filter(sqlite3_str *query, const index_db_search_options_t *opts)
{
	if (opts->pattern) {
		const char *op = pattern_uses_glob(opts->pattern) ? "GLOB" : "=";

		sqlite3_str_appendf(query, " AND records.symbol %s %Q", op, opts->pattern);
	}
	if (opts->path)
		sqlite3_str_appendf(query, " AND files.path GLOB %Q", opts->path);
	if (opts->variant) {
		const char *op = pattern_uses_glob(opts->variant) ? "GLOB" : "=";

		sqlite3_str_appendf(query, " AND files.variant %s %Q", op, opts->variant);
	}
	if (opts->record != INDEX_DB_RECORD_ALL)
		sqlite3_str_appendf(query, " AND records.record = %d",
			opts->record == INDEX_DB_RECORD_SYMBOL ? STORED_RECORD_SYMBOL : STORED_RECORD_USE);
	if (opts->has_mode) {
		if (opts->mode_definition) {
			sqlite3_str_appendf(query, " AND records.record = %d AND records.action != 0",
				STORED_RECORD_SYMBOL);
		} else if (!opts->mode) {
			sqlite3_str_appendf(query, " AND records.record = %d AND records.mode = 0", STORED_RECORD_USE);
		} else {
			sqlite3_str_appendf(query, " AND records.record = %d AND (records.mode & %u) != 0",
				STORED_RECORD_USE, opts->mode);
		}
	}
	if (opts->has_kind)
		sqlite3_str_appendf(query, " AND records.kind = %d", opts->kind);

	return sqlite3_str_errcode(query) == SQLITE_OK ? 0 : -1;
}

static int print_search_results(sqlite3 *db, const char *sql, const char *format, FILE *out)
{
	output_search_t *search = NULL;
	sqlite3_stmt *stmt = NULL;
	int step;
	int ret = -1;

	search = output_search_create(out, format);
	if (!search)
		return -1;
	if (prepare(db, sql, &stmt) < 0)
		goto out;
	while ((step = sqlite3_step(stmt)) == SQLITE_ROW) {
		output_search_record_t result = {
			.variant = (const char *)sqlite3_column_text(stmt, 0),
			.file = (const char *)sqlite3_column_text(stmt, 1),
			.line = sqlite3_column_int(stmt, 2),
			.column = sqlite3_column_int(stmt, 3),
			.symbol_record = sqlite3_column_int(stmt, 4) == STORED_RECORD_SYMBOL,
			.definition = sqlite3_column_int(stmt, 5),
			.kind = sqlite3_column_int(stmt, 6),
			.symbol = (const char *)sqlite3_column_text(stmt, 7),
			.context = (const char *)sqlite3_column_text(stmt, 8),
			.mode = sqlite3_column_int64(stmt, 9),
		};

		if (output_search_write(search, &result) < 0)
			goto out;
	}
	if (step != SQLITE_DONE) {
		fprintf(stderr, "semindex: sqlite: %s\n", sqlite3_errmsg(db));
		goto out;
	}

	ret = ferror(out) ? -1 : 0;
out:
	output_search_destroy(search);
	sqlite3_finalize(stmt);
	return ret;
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
	sqlite3_str *query = NULL;
	sqlite3 *db = NULL;
	char *sql = NULL;
	int ret = -1;

	if (!path || !out)
		return -1;
	if (!opts)
		opts = &defaults;
	if (open_reader(path, &db) < 0)
		goto out;

	query = sqlite3_str_new(db);
	if (!query)
		goto out;
	sqlite3_str_appendall(query,
		"SELECT files.variant, files.path, records.line, records.column, records.record, records.action, "
		"records.kind, records.symbol, records.context, records.mode "
		"FROM records JOIN files ON files.id = records.file_id WHERE 1");
	if (append_search_filter(query, opts) < 0)
		goto out;
	sqlite3_str_appendall(query,
		" ORDER BY files.variant, files.path, records.line, records.column, records.record");
	if (sqlite3_str_errcode(query) != SQLITE_OK)
		goto out;

	sql = sqlite3_str_finish(query);
	query = NULL;
	if (!sql)
		goto out;
	ret = print_search_results(db, sql, opts->format ? opts->format : OUTPUT_SEARCH_DEFAULT_FORMAT, out);
out:
	if (query)
		sqlite3_str_finish(query);
	sqlite3_free(sql);
	if (db)
		sqlite3_close(db);
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
		"WHERE records.record = %d AND records.action = %d AND records.kind = %d",
		STORED_RECORD_USE, SEMINDEX_USE_CALL, SEMINDEX_SYMBOL_FUNCTION);
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
