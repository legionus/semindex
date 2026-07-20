// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef SEMINDEX_COMMAND_DB_H
#define SEMINDEX_COMMAND_DB_H

#include <stddef.h>

int command_db_store(const char *path, const char *variant, const char *directory, const char *file, size_t argc,
	const char *const *argv);

#endif /* SEMINDEX_COMMAND_DB_H */
