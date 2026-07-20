// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef SEMINDEX_COMMAND_DB_H
#define SEMINDEX_COMMAND_DB_H

#include <stddef.h>
#include <stdio.h>

char *command_db_default_path(const char *index_database);
int command_db_store(const char *path, const char *variant, const char *directory, const char *file, size_t argc,
	const char *const *argv);
int command_db_export(const char *path, const char *variant, FILE *out);

#endif /* SEMINDEX_COMMAND_DB_H */
