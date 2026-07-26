// SPDX-License-Identifier: GPL-2.0-or-later
#include <sqlite3.h>
#include <stdio.h>
#include <string.h>

#include "command_db.h"
#include "command_db_internal.h"
#include "sqlite.h"

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

	if (command_db_open_reader(path, &db) < 0 || command_db_prepare(db, sql, &stmt) < 0)
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
