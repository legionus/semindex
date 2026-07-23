// SPDX-License-Identifier: GPL-2.0-or-later
#include <sqlite3.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "command_db.h"
#include "sqlite.h"

#define COMMAND_SCHEMA_VERSION 1

struct command_db_command {
	semindex_compile_command_t command;
	char *directory;
	char *file;
	void *arguments;
	const char **argv;
};

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

static int prepare(sqlite3 *db, const char *sql, sqlite3_stmt **stmt)
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

static int mkdir_one(const char *path)
{
	if (mkdir(path, 0777) == 0 || errno == EEXIST)
		return 0;

	fprintf(stderr, "semindex: failed to create directory '%s': %s\n", path, strerror(errno));
	return -1;
}

static int ensure_parent_directory(const char *path)
{
	const char *slash = strrchr(path, '/');
	char *dir;
	char *p;

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

static int init_schema(sqlite3 *db)
{
	int version;

	if (schema_version(db, &version) < 0)
		return -1;
	if (version == COMMAND_SCHEMA_VERSION)
		return 0;
	if (version != 0) {
		fprintf(stderr, "semindex: command database schema version %d is incompatible\n", version);
		return -1;
	}
	if (exec_sql(db, "BEGIN IMMEDIATE") < 0)
		return -1;
	if (schema_version(db, &version) < 0)
		goto rollback;
	if (version == COMMAND_SCHEMA_VERSION)
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
	if (ensure_parent_directory(path) < 0)
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

static int open_reader(const char *path, sqlite3 **db)
{
	int version;

	if (sqlite3_open_v2(path, db, SQLITE_OPEN_READONLY, NULL) != SQLITE_OK) {
		fprintf(stderr, "semindex: failed to open command database '%s': %s\n", path,
			*db ? sqlite3_errmsg(*db) : "unknown error");
		return -1;
	}
	if (sqlite3_busy_timeout(*db, INT_MAX) != SQLITE_OK || schema_version(*db, &version) < 0)
		return -1;
	if (version != COMMAND_SCHEMA_VERSION) {
		fprintf(stderr, "semindex: command database schema version %d is incompatible\n", version);
		return -1;
	}

	return 0;
}

static char *absolute_path(const char *directory, const char *path)
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
	abs_file = absolute_path(abs_directory, file);
	arguments = pack_arguments(argc, argv, &blob_size);
	if (!abs_file || !arguments)
		goto out;
	if (open_writer(path, &db) < 0 || prepare(db, sql, &stmt) < 0)
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
	abs_file = absolute_path(".", file);
	if (!abs_file)
		goto out;
	if (open_reader(path, &db) < 0 || prepare(db, sql, &stmt) < 0)
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

static int write_json_string(FILE *out, const unsigned char *value, size_t len)
{
	size_t i;

	if (fputc('"', out) == EOF)
		return -1;
	for (i = 0; i < len; i++) {
		unsigned char ch = value[i];
		const char *escape = NULL;

		switch (ch) {
		case '"':
			escape = "\\\"";
			break;
		case '\\':
			escape = "\\\\";
			break;
		case '\b':
			escape = "\\b";
			break;
		case '\f':
			escape = "\\f";
			break;
		case '\n':
			escape = "\\n";
			break;
		case '\r':
			escape = "\\r";
			break;
		case '\t':
			escape = "\\t";
			break;
		}
		if (escape) {
			if (fputs(escape, out) == EOF)
				return -1;
		} else if (ch < 0x20) {
			if (fprintf(out, "\\u%04x", ch) < 0)
				return -1;
		} else if (fputc(ch, out) == EOF) {
			return -1;
		}
	}

	return fputc('"', out) == EOF ? -1 : 0;
}

static int write_arguments(FILE *out, const unsigned char *blob, size_t size)
{
	size_t offset = 0;
	int first = 1;

	if (fputc('[', out) == EOF)
		return -1;
	while (offset < size) {
		const unsigned char *end = memchr(blob + offset, '\0', size - offset);

		if (!end) {
			fprintf(stderr, "semindex: command database contains malformed arguments\n");
			return -1;
		}
		if (!first && fputs(", ", out) == EOF)
			return -1;
		if (write_json_string(out, blob + offset, end - (blob + offset)) < 0)
			return -1;
		first = 0;
		offset = end - blob + 1;
	}

	return fputc(']', out) == EOF ? -1 : 0;
}

int command_db_export(const char *path, const char *variant, FILE *out)
{
	static const char *sql = "SELECT directory, file, arguments FROM commands WHERE variant = ?1 ORDER BY file";
	sqlite3_stmt *stmt = NULL;
	sqlite3 *db = NULL;
	int first = 1;
	int step;
	int ret = -1;

	if (!path || !variant || !variant[0] || !out)
		return -1;
	if (open_reader(path, &db) < 0 || prepare(db, sql, &stmt) < 0)
		goto out;
	SEMINDEX_SQLITE_BIND_OR_GOTO(out, semindex_sqlite_bind_text(stmt, 1, variant));

	if (fputs("[\n", out) == EOF)
		goto out;
	while ((step = sqlite3_step(stmt)) == SQLITE_ROW) {
		const unsigned char *directory = sqlite3_column_text(stmt, 0);
		const unsigned char *file = sqlite3_column_text(stmt, 1);
		const unsigned char *arguments = sqlite3_column_blob(stmt, 2);
		size_t arguments_size = sqlite3_column_bytes(stmt, 2);

		if (!directory || !file || (!arguments && arguments_size))
			goto out;
		if (!first && fputs(",\n", out) == EOF)
			goto out;
		if (fputs("  {\n    \"directory\": ", out) == EOF ||
			write_json_string(out, directory, sqlite3_column_bytes(stmt, 0)) < 0 ||
			fputs(",\n    \"file\": ", out) == EOF ||
			write_json_string(out, file, sqlite3_column_bytes(stmt, 1)) < 0 ||
			fputs(",\n    \"arguments\": ", out) == EOF ||
			write_arguments(out, arguments, arguments_size) < 0 || fputs("\n  }", out) == EOF)
			goto out;
		first = 0;
	}
	if (step != SQLITE_DONE) {
		fprintf(stderr, "semindex: command database: %s\n", sqlite3_errmsg(db));
		goto out;
	}
	if (fputs("\n]\n", out) == EOF || ferror(out))
		goto out;
	ret = 0;
out:
	sqlite3_finalize(stmt);
	if (db)
		sqlite3_close(db);
	return ret;
}
