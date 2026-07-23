/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef SEMINDEX_SQLITE_H
#define SEMINDEX_SQLITE_H

#include <sqlite3.h>

static inline int semindex_sqlite_bind_text(sqlite3_stmt *stmt, int index, const char *value)
{
	return sqlite3_bind_text(stmt, index, value ? value : "", -1, SQLITE_TRANSIENT) == SQLITE_OK ? 0 : -1;
}

static inline int semindex_sqlite_bind_int(sqlite3_stmt *stmt, int index, int value)
{
	return sqlite3_bind_int(stmt, index, value) == SQLITE_OK ? 0 : -1;
}

static inline int semindex_sqlite_bind_int64(sqlite3_stmt *stmt, int index, sqlite3_int64 value)
{
	return sqlite3_bind_int64(stmt, index, value) == SQLITE_OK ? 0 : -1;
}

static inline int semindex_sqlite_bind_blob64(sqlite3_stmt *stmt, int index, const void *value, sqlite3_uint64 size)
{
	return sqlite3_bind_blob64(stmt, index, value, size, SQLITE_TRANSIENT) == SQLITE_OK ? 0 : -1;
}

static inline int semindex_sqlite_bind_blob64_static(sqlite3_stmt *stmt, int index, const void *value,
	sqlite3_uint64 size)
{
	return sqlite3_bind_blob64(stmt, index, value, size, SQLITE_STATIC) == SQLITE_OK ? 0 : -1;
}

static inline int semindex_sqlite_bind_null(sqlite3_stmt *stmt, int index)
{
	return sqlite3_bind_null(stmt, index) == SQLITE_OK ? 0 : -1;
}

#define SEMINDEX_SQLITE_BIND_OR_GOTO(label, expression) \
	do {                                            \
		if ((expression) < 0)                   \
			goto label;                     \
	} while (0)

#endif /* SEMINDEX_SQLITE_H */
