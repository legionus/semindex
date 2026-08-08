// SPDX-License-Identifier: GPL-2.0-or-later
#include <sqlite3.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "command_db.h"
#include "command_db_internal.h"
#include "filesystem.h"
#include "sqlite.h"

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
		fprintf(stderr, "semindex: command database: %s\n", errmsg ? errmsg : sqlite3_errmsg(db));
		sqlite3_free(errmsg);

		return -1;
	}

	return 0;
}

static int init_schema(sqlite3 *db)
{
	int version;

	if (command_db_schema_version(db, &version) < 0)
		return -1;

	if (version == COMMAND_DB_SCHEMA_VERSION)
		return 0;

	if (version != 0) {
		fprintf(stderr, "semindex: command database schema version %d is incompatible\n", version);

		return -1;
	}

	if (exec_sql(db, "BEGIN IMMEDIATE") < 0)
		return -1;

	if (command_db_schema_version(db, &version) < 0)
		goto rollback;

	if (version == COMMAND_DB_SCHEMA_VERSION)
		goto commit;

	if (exec_sql(db,
		    "CREATE TABLE commands ("
		    "  variant TEXT NOT NULL,"
		    "  file TEXT NOT NULL,"
		    "  directory TEXT NOT NULL,"
		    "  arguments BLOB NOT NULL,"
		    "  PRIMARY KEY(variant, file)"
		    ") WITHOUT ROWID") < 0 ||
		exec_sql(db, "PRAGMA user_version = 1") < 0)
		goto rollback;

commit:
	return exec_sql(db, "COMMIT");

rollback:
	exec_sql(db, "ROLLBACK");

	return -1;
}

static int open_writer(const char *path, sqlite3 **db)
{
	if (semindex_ensure_parent_directory(path) < 0)
		return -1;

	if (sqlite3_open_v2(path, db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL) != SQLITE_OK) {
		fprintf(stderr, "semindex: failed to open command database '%s': %s\n", path,
			*db ? sqlite3_errmsg(*db) : "unknown error");

		return -1;
	}

	if (sqlite3_busy_timeout(*db, INT_MAX) != SQLITE_OK)
		return -1;

	if (exec_sql(*db, "PRAGMA journal_mode = WAL") < 0 || exec_sql(*db, "PRAGMA synchronous = OFF") < 0 ||
		init_schema(*db) < 0)
		return -1;

	return 0;
}

static void *pack_arguments(size_t argc, const char *const *argv, sqlite3_uint64 *size)
{
	unsigned char *blob;
	unsigned char *p;
	size_t total = 0;
	size_t i;

	for (i = 0; i < argc; i++) {
		size_t len = strlen(argv[i]) + 1;

		if (len > SIZE_MAX - total)
			return NULL;

		total += len;
	}

	blob = malloc(total ? total : 1);

	if (!blob)
		return NULL;

	p = blob;

	for (i = 0; i < argc; i++) {
		size_t len = strlen(argv[i]) + 1;

		memcpy(p, argv[i], len);
		p += len;
	}

	*size = total;

	return blob;
}

int command_db_store(const char *path, const char *variant, const char *directory, const char *file, size_t argc,
	const char *const *argv)
{
	static const char *sql = "INSERT INTO commands(variant, file, directory, arguments) VALUES(?1, ?2, ?3, ?4) "
				 "ON CONFLICT(variant, file) DO UPDATE SET directory = excluded.directory, arguments = "
				 "excluded.arguments";
	sqlite3_uint64 blob_size;

	sqlite3_stmt *stmt = NULL;
	sqlite3 *db = NULL;
	char *abs_directory = NULL;
	char *abs_file = NULL;
	void *arguments = NULL;
	int ret = -1;

	if (!path || !variant || !variant[0] || !directory || !file || !argv || !argc)
		return -1;

	abs_directory = realpath(directory, NULL);

	if (!abs_directory)
		abs_directory = strdup(directory);

	if (!abs_directory)
		goto out;

	abs_file = command_db_absolute_path(abs_directory, file);
	arguments = pack_arguments(argc, argv, &blob_size);

	if (!abs_file || !arguments)
		goto out;

	if (open_writer(path, &db) < 0 || command_db_prepare(db, sql, &stmt) < 0)
		goto out;

	SEMINDEX_SQLITE_BIND_OR_GOTO(out, semindex_sqlite_bind_text(stmt, 1, variant));
	SEMINDEX_SQLITE_BIND_OR_GOTO(out, semindex_sqlite_bind_text(stmt, 2, abs_file));
	SEMINDEX_SQLITE_BIND_OR_GOTO(out, semindex_sqlite_bind_text(stmt, 3, abs_directory));
	SEMINDEX_SQLITE_BIND_OR_GOTO(out, semindex_sqlite_bind_blob64(stmt, 4, arguments, blob_size));

	if (sqlite3_step(stmt) != SQLITE_DONE) {
		fprintf(stderr, "semindex: command database: %s\n", sqlite3_errmsg(db));
		goto out;
	}

	ret = 0;
out:
	sqlite3_finalize(stmt);

	if (db)
		sqlite3_close(db);

	free(arguments);
	free(abs_file);
	free(abs_directory);

	return ret;
}
