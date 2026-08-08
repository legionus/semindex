// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef SEMINDEX_COMMAND_DB_INTERNAL_H
#define SEMINDEX_COMMAND_DB_INTERNAL_H

#include <sqlite3.h>

#define COMMAND_DB_SCHEMA_VERSION 1

int command_db_prepare(sqlite3 *db, const char *sql, sqlite3_stmt **stmt);
int command_db_schema_version(sqlite3 *db, int *version);
int command_db_open_reader(const char *path, sqlite3 **db);
char *command_db_absolute_path(const char *directory, const char *path);

#endif /* SEMINDEX_COMMAND_DB_INTERNAL_H */
