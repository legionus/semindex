// SPDX-License-Identifier: GPL-2.0-or-later
#include <ctype.h>
#include <string.h>

#include "array_size.h"
#include "semindex_cli.h"

struct index_db_record_name {
	index_db_record_t record;
	const char *name;
};

struct symbol_kind_name {
	semindex_symbol_kind_t kind;
	const char *cli_name;
};

static const struct index_db_record_name index_db_record_names[] = {
	{ INDEX_DB_RECORD_ALL, "all" },
	{ INDEX_DB_RECORD_SYMBOL, "symbol" },
	{ INDEX_DB_RECORD_USE, "use" },
};

static const struct symbol_kind_name symbol_kind_names[] = {
	{ SEMINDEX_SYMBOL_VAR, "var" },
	{ SEMINDEX_SYMBOL_FIELD, "field" },
	{ SEMINDEX_SYMBOL_STRUCT, "struct" },
	{ SEMINDEX_SYMBOL_UNION, "union" },
	{ SEMINDEX_SYMBOL_ENUM, "enum" },
	{ SEMINDEX_SYMBOL_ENUM_CONSTANT, "enumerator" },
	{ SEMINDEX_SYMBOL_TYPEDEF, "typedef" },
	{ SEMINDEX_SYMBOL_FUNCTION, "function" },
	{ SEMINDEX_SYMBOL_MACRO, "macro" },
	{ SEMINDEX_SYMBOL_FILE, "file" },
};

int parse_format(const char *value, enum output_format *format)
{
	if (!strcmp(value, "dissect"))
		*format = FORMAT_DISSECT;

	else if (!strcmp(value, "json"))
		*format = FORMAT_JSON;
	else
		return -1;

	return 0;
}

int parse_scope(const char *value, semindex_scope_t *scope)
{
	if (!strcmp(value, "file"))
		*scope = SEMINDEX_SCOPE_FILE;

	else if (!strcmp(value, "project"))
		*scope = SEMINDEX_SCOPE_PROJECT;

	else if (!strcmp(value, "all"))
		*scope = SEMINDEX_SCOPE_ALL;
	else
		return -1;

	return 0;
}

int parse_git_commit(const char *value, index_pipeline_git_commit_t *mode)
{
	size_t length;
	size_t i;

	if (!strcmp(value, "auto")) {
		*mode = INDEX_PIPELINE_GIT_COMMIT_AUTO;

		return 0;
	}

	length = strlen(value);

	if (length != 40 && length != 64)
		return -1;

	for (i = 0; i < length; i++) {
		if (!isxdigit((unsigned char)value[i]))
			return -1;
	}

	*mode = INDEX_PIPELINE_GIT_COMMIT_EXPLICIT;

	return 0;
}

int output_index(enum output_format format, semindex_t *s)
{
	if (format == FORMAT_JSON)
		return output_json(stdout, s);

	return output_dissect(stdout, s);
}

int parse_index_db_record(const char *value, index_db_record_t *record)
{
	size_t i;

	for (i = 0; i < ARRAY_SIZE(index_db_record_names); i++) {
		if (!strcmp(value, index_db_record_names[i].name)) {
			*record = index_db_record_names[i].record;

			return 0;
		}
	}

	return -1;
}

int parse_symbol_kind(const char *value, int *kind)
{
	size_t i;

	for (i = 0; i < ARRAY_SIZE(symbol_kind_names); i++) {
		if (!strcmp(value, symbol_kind_names[i].cli_name)) {
			*kind = symbol_kind_names[i].kind;

			return 0;
		}
	}

	return -1;
}
