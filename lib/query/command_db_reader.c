// SPDX-License-Identifier: GPL-2.0-or-later
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "command_db.h"
#include "command_db_internal.h"
#include "sqlite.h"

struct command_db_command {
	semindex_compile_command_t command;

	char *directory;
	char *file;
	void *arguments;
	const char **argv;
};

static int unpack_arguments(command_db_command_t *command, const void *arguments, size_t size)
{
	const unsigned char *blob = arguments;
	size_t argc = 0;
	size_t offset = 0;
	size_t i;

	if (!size || !arguments || blob[size - 1] != '\0')
		return -1;

	for (i = 0; i < size; i++) {
		if (!blob[i])
			argc++;
	}

	command->arguments = malloc(size);
	command->argv = calloc(argc, sizeof(*command->argv));

	if (!command->arguments || !command->argv)
		return -1;

	memcpy(command->arguments, arguments, size);

	for (i = 0; i < argc; i++) {
		command->argv[i] = (const char *)command->arguments + offset;
		offset += strlen(command->argv[i]) + 1;
	}

	command->command.argc = argc;
	command->command.argv = command->argv;

	return 0;
}

int command_db_load(const char *path, const char *variant, const char *file, command_db_command_t **result)
{
	static const char *sql = "SELECT directory, file, arguments FROM commands WHERE variant = ?1 AND file = ?2";

	command_db_command_t *command = NULL;
	const unsigned char *directory;
	const unsigned char *stored_file;
	const void *arguments;

	sqlite3_stmt *stmt = NULL;
	sqlite3 *db = NULL;
	char *abs_file = NULL;
	size_t arguments_size;
	int step;
	int ret = -1;

	if (!path || !variant || !variant[0] || !file || !result)
		return -1;

	*result = NULL;
	abs_file = command_db_absolute_path(".", file);

	if (!abs_file)
		goto out;

	if (command_db_open_reader(path, &db) < 0 || command_db_prepare(db, sql, &stmt) < 0)
		goto out;

	SEMINDEX_SQLITE_BIND_OR_GOTO(out, semindex_sqlite_bind_text(stmt, 1, variant));
	SEMINDEX_SQLITE_BIND_OR_GOTO(out, semindex_sqlite_bind_text(stmt, 2, abs_file));

	step = sqlite3_step(stmt);

	if (step == SQLITE_DONE) {
		ret = 1;
		goto out;
	}

	if (step != SQLITE_ROW) {
		fprintf(stderr, "semindex: command database: %s\n", sqlite3_errmsg(db));
		goto out;
	}

	directory = sqlite3_column_text(stmt, 0);
	stored_file = sqlite3_column_text(stmt, 1);
	arguments = sqlite3_column_blob(stmt, 2);
	arguments_size = sqlite3_column_bytes(stmt, 2);

	if (!directory || !stored_file || !arguments || !arguments_size) {
		fprintf(stderr, "semindex: command database contains malformed command\n");
		goto out;
	}

	command = calloc(1, sizeof(*command));

	if (!command)
		goto out;

	command->directory = strdup((const char *)directory);
	command->file = strdup((const char *)stored_file);

	if (!command->directory || !command->file || unpack_arguments(command, arguments, arguments_size) < 0) {
		fprintf(stderr, "semindex: command database contains malformed command\n");
		goto out;
	}

	command->command.directory = command->directory;
	command->command.file = command->file;
	*result = command;
	command = NULL;
	ret = 0;
out:
	command_db_command_free(command);
	sqlite3_finalize(stmt);

	if (db)
		sqlite3_close(db);

	free(abs_file);

	return ret;
}

const semindex_compile_command_t *command_db_command_get(const command_db_command_t *command)
{
	return command ? &command->command : NULL;
}

void command_db_command_free(command_db_command_t *command)
{
	if (!command)
		return;

	free(command->argv);
	free(command->arguments);
	free(command->file);
	free(command->directory);
	free(command);
}
