// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef SEMINDEX_COMMAND_DB_READER_H
#define SEMINDEX_COMMAND_DB_READER_H

#include <stdio.h>

#include "semindex.h"

typedef struct command_db_command command_db_command_t;

#ifdef __cplusplus
extern "C" {
#endif

/* Returns zero when found, one when absent, and minus one on error. */
int command_db_load(const char *path, const char *variant, const char *file, command_db_command_t **result);
const semindex_compile_command_t *command_db_command_get(const command_db_command_t *command);
void command_db_command_free(command_db_command_t *command);
int command_db_export(const char *path, const char *variant, FILE *out);

#ifdef __cplusplus
}
#endif

#endif /* SEMINDEX_COMMAND_DB_READER_H */
