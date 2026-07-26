// SPDX-License-Identifier: GPL-2.0-or-later
#include <sqlite3.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "command_db.h"
#include "command_db_internal.h"

char *command_db_default_path(const char *index_database)
{
	const char *slash = strrchr(index_database, '/');
	size_t dir_len;

	char *path;

	if (!slash)
		return strdup("commands.db");

	dir_len = slash - index_database + 1;
	path = malloc(dir_len + sizeof("commands.db"));

	if (!path)
		return NULL;

	memcpy(path, index_database, dir_len);
	memcpy(path + dir_len, "commands.db", sizeof("commands.db"));

	return path;
}

int command_db_prepare(sqlite3 *db, const char *sql, sqlite3_stmt **stmt)
{
	int ret;

	do {
		ret = sqlite3_prepare_v3(db, sql, -1, SQLITE_PREPARE_PERSISTENT, stmt, NULL);

		if (ret == SQLITE_BUSY || ret == SQLITE_LOCKED)
			sqlite3_sleep(10);
	} while (ret == SQLITE_BUSY || ret == SQLITE_LOCKED);

	if (ret != SQLITE_OK) {
		fprintf(stderr, "semindex: command database: %s\n", sqlite3_errmsg(db));

		return -1;
	}

	return 0;
}

int command_db_schema_version(sqlite3 *db, int *version)
{
	sqlite3_stmt *stmt = NULL;
	int ret = -1;

	if (command_db_prepare(db, "PRAGMA user_version", &stmt) < 0)
		return -1;

	if (sqlite3_step(stmt) != SQLITE_ROW)
		goto out;

	*version = sqlite3_column_int(stmt, 0);
	ret = 0;
out:
	sqlite3_finalize(stmt);

	return ret;
}

int command_db_open_reader(const char *path, sqlite3 **db)
{
	int version;

	if (sqlite3_open_v2(path, db, SQLITE_OPEN_READONLY, NULL) != SQLITE_OK) {
		fprintf(stderr, "semindex: failed to open command database '%s': %s\n", path,
			*db ? sqlite3_errmsg(*db) : "unknown error");

		return -1;
	}

	if (sqlite3_busy_timeout(*db, INT_MAX) != SQLITE_OK || command_db_schema_version(*db, &version) < 0)
		return -1;

	if (version != COMMAND_DB_SCHEMA_VERSION) {
		fprintf(stderr, "semindex: command database schema version %d is incompatible\n", version);

		return -1;
	}

	return 0;
}

char *command_db_absolute_path(const char *directory, const char *path)
{
	char *joined;
	char *resolved;

	if (path[0] == '/')
		joined = strdup(path);
	else
		joined = sqlite3_mprintf("%s/%s", directory, path);

	if (!joined)
		return NULL;

	resolved = realpath(joined, NULL);

	if (!resolved)
		resolved = strdup(joined);

	if (path[0] == '/')
		free(joined);
	else
		sqlite3_free(joined);

	return resolved;
}
