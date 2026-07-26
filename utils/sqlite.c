// SPDX-License-Identifier: GPL-2.0-or-later
#include <limits.h>
#include <stdio.h>

#include "sqlite.h"

int semindex_sqlite_prepare(sqlite3 *db, const char *sql, sqlite3_stmt **stmt)
{
	if (sqlite3_prepare_v3(db, sql, -1, SQLITE_PREPARE_PERSISTENT, stmt, NULL) == SQLITE_OK)
		return 0;

	fprintf(stderr, "semindex: sqlite: %s\n", sqlite3_errmsg(db));

	return -1;
}

int semindex_sqlite_open_readonly(const char *path, sqlite3 **db)
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
