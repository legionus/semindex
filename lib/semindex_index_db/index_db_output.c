// SPDX-License-Identifier: GPL-2.0-or-later
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "array_size.h"
#include "index_db_output.h"
#include "repository.h"
#include "semantic_output.h"
#include "semantic_names.h"

enum search_field {
	SEARCH_FIELD_FILE,
	SEARCH_FIELD_VARIANT,
	SEARCH_FIELD_VARIANT_FILE,
	SEARCH_FIELD_LINE,
	SEARCH_FIELD_COLUMN,
	SEARCH_FIELD_CONTEXT,
	SEARCH_FIELD_SYMBOL,
	SEARCH_FIELD_RECORD,
	SEARCH_FIELD_MODE,
	SEARCH_FIELD_KIND,
	SEARCH_FIELD_KIND_CHAR,
	SEARCH_FIELD_SOURCE,
};

struct search_field_name {
	const char *name;
	size_t length;
	enum search_field field;
};

enum search_token_kind {
	SEARCH_TOKEN_TEXT,
	SEARCH_TOKEN_CHARACTER,
	SEARCH_TOKEN_FIELD,
};

struct search_token {
	enum search_token_kind kind;
	const char *text;
	size_t length;
	union {
		char character;
		enum search_field field;
	};
};

#define SEARCH_FIELD(name, field) { name, sizeof(name) - 1, field }

static const struct search_field_name search_fields[] = {
	SEARCH_FIELD("file", SEARCH_FIELD_FILE),
	SEARCH_FIELD("variant", SEARCH_FIELD_VARIANT),
	SEARCH_FIELD("variant_file", SEARCH_FIELD_VARIANT_FILE),
	SEARCH_FIELD("line", SEARCH_FIELD_LINE),
	SEARCH_FIELD("column", SEARCH_FIELD_COLUMN),
	SEARCH_FIELD("context", SEARCH_FIELD_CONTEXT),
	SEARCH_FIELD("symbol", SEARCH_FIELD_SYMBOL),
	SEARCH_FIELD("record", SEARCH_FIELD_RECORD),
	SEARCH_FIELD("mode", SEARCH_FIELD_MODE),
	SEARCH_FIELD("kind", SEARCH_FIELD_KIND),
	SEARCH_FIELD("kind_char", SEARCH_FIELD_KIND_CHAR),
	SEARCH_FIELD("source", SEARCH_FIELD_SOURCE),
};

struct index_db_output {
	FILE *out;
	struct search_token *tokens;
	size_t token_count;

	char *path;
	char *root;
	FILE *source;
	char *text;
	size_t capacity;
	ssize_t length;
	int line;
};

static void print_mode(FILE *out, const index_db_output_record_t *record)
{
	char str[4];

	if (record->symbol_record) {
		fputs(record->definition ? "def" : "decl", out);
		return;
	}
	output_mode_string(record->mode, str);
	fputs(str, out);
}

static const char *record_name(const index_db_output_record_t *record)
{
	if (!record->symbol_record)
		return "reference";

	return record->definition ? "definition" : "declaration";
}

static int find_field(const char *name, size_t length, enum search_field *field)
{
	size_t i;

	for (i = 0; i < ARRAY_SIZE(search_fields); i++) {
		if (length == search_fields[i].length && !strncmp(name, search_fields[i].name, length)) {
			*field = search_fields[i].field;

			return 0;
		}
	}

	return -1;
}

static const char *parse_field(const char *format, enum search_field *field)
{
	const char *end;

	if (format[0] != '%' || format[1] != '{') {
		fprintf(stderr, "semindex: expected named format specification after '%%'\n");

		return NULL;
	}

	end = strchr(format + 2, '}');

	if (!end) {
		fprintf(stderr, "semindex: unterminated format specification\n");

		return NULL;
	}

	if (find_field(format + 2, end - format - 2, field) < 0) {
		fprintf(stderr, "semindex: unknown format field: %.*s\n", (int)(end - format - 2), format + 2);

		return NULL;
	}

	return end;
}

static void add_text_token(index_db_output_t *search, const char *text, size_t length)
{
	struct search_token *token;

	if (!length)
		return;

	token = &search->tokens[search->token_count++];
	token->kind = SEARCH_TOKEN_TEXT;
	token->text = text;
	token->length = length;
}

static void add_character_token(index_db_output_t *search, char character)
{
	struct search_token *token;

	token = &search->tokens[search->token_count++];

	token->kind = SEARCH_TOKEN_CHARACTER;
	token->character = character;
}

static void add_field_token(index_db_output_t *search, enum search_field field)
{
	struct search_token *token;

	token = &search->tokens[search->token_count++];

	token->kind = SEARCH_TOKEN_FIELD;
	token->field = field;
}

static int compile_format(index_db_output_t *search, const char *format)
{
	const char *text;
	const char *p;

	text = format;

	search->tokens = calloc(strlen(format) + 1, sizeof(*search->tokens));

	if (!search->tokens)
		return -1;

	for (p = format; *p; p++) {
		enum search_field field;
		char character;

		if (*p != '\\' && *p != '%')
			continue;

		add_text_token(search, text, p - text);

		if (*p == '\\') {
			character = *++p;

			if (!character) {
				text = p;
				break;
			}

			switch (character) {
			case 't':
				character = '\t';
				break;
			case 'r':
				character = '\r';
				break;
			case 'n':
				character = '\n';
				break;
			}

			add_character_token(search, character);
			text = p + 1;
			continue;
		}

		if (p[1] == '%') {
			add_character_token(search, '%');
			p++;
			text = p + 1;
			continue;
		}

		p = parse_field(p, &field);

		if (!p)
			return -1;

		add_field_token(search, field);
		text = p + 1;
	}

	add_text_token(search, text, p - text);

	return 0;
}

static int open_source(index_db_output_t *search, const char *root, const char *path)
{
	char *source_path;

	if (path[0] == '/' || (!root && !search->root))
		source_path = strdup(path);
	else {
		const char *base = root ? root : search->root;
		size_t length = strlen(base) + strlen(path) + 2;

		source_path = malloc(length);

		if (source_path)
			snprintf(source_path, length, "%s/%s", base, path);
	}

	if (!source_path)
		return -1;

	if (search->source && search->path && !strcmp(search->path, source_path)) {
		free(source_path);

		return 0;
	}

	if (search->source)
		fclose(search->source);
	free(search->path);
	search->path = source_path;
	search->source = NULL;
	search->line = 0;
	search->length = 0;

	search->source = fopen(source_path, "r");

	if (!search->source) {
		fprintf(stderr, "semindex: failed to open source file '%s': %s\n", path, strerror(errno));
		return -1;
	}

	return 0;
}

static int print_source_line(index_db_output_t *search, const char *root, const char *path, int line)
{
	if (open_source(search, root, path) < 0)
		return -1;

	if (line < search->line) {
		rewind(search->source);
		search->line = 0;
	}
	while (search->line < line) {
		errno = 0;
		search->length = getline(&search->text, &search->capacity, search->source);

		if (search->length < 0) {
			if (errno)
				fprintf(stderr, "semindex: failed to read source file '%s': %s\n", path,
					strerror(errno));
			else
				fprintf(stderr, "semindex: source line %d is missing from '%s'\n", line, path);
			return -1;
		}
		search->line++;
	}
	if (search->length > 0 && search->text[search->length - 1] == '\n')
		search->length--;

	if (fwrite(search->text, 1, search->length, search->out) != (size_t)search->length)
		return -1;

	return 0;
}

index_db_output_t *index_db_output_create(FILE *out, const char *format, const char *root_hint)
{
	index_db_output_t *search;

	if (!out)
		return NULL;

	if (!format)
		format = INDEX_DB_OUTPUT_DEFAULT_FORMAT;

	search = calloc(1, sizeof(*search));

	if (!search)
		return NULL;

	search->out = out;

	if (compile_format(search, format) < 0) {
		index_db_output_destroy(search);

		return NULL;
	}

	search->root = semindex_repository_root(root_hint);

	if (!search->root)
		search->root = semindex_repository_root(".");

	return search;
}

void index_db_output_destroy(index_db_output_t *search)
{
	if (!search)
		return;

	if (search->source)
		fclose(search->source);
	free(search->path);
	free(search->root);
	free(search->text);
	free(search->tokens);
	free(search);
}

int index_db_output_write(index_db_output_t *search, const index_db_output_record_t *record)
{
	size_t i;

	if (!search || !record)
		return -1;

	for (i = 0; i < search->token_count; i++) {
		const struct search_token *token;

		token = &search->tokens[i];

		if (token->kind == SEARCH_TOKEN_TEXT) {
			if (fwrite(token->text, 1, token->length, search->out) != token->length)
				return -1;

			continue;
		}

		if (token->kind == SEARCH_TOKEN_CHARACTER) {
			fputc(token->character, search->out);

			continue;
		}

		switch (token->field) {
		case SEARCH_FIELD_FILE:
			fputs(record->file, search->out);
			break;
		case SEARCH_FIELD_VARIANT_FILE:
			fprintf(search->out, "%s:%s", record->variant, record->file);
			break;
		case SEARCH_FIELD_VARIANT:
			fputs(record->variant, search->out);
			break;
		case SEARCH_FIELD_LINE:
			fprintf(search->out, "%d", record->line);
			break;
		case SEARCH_FIELD_COLUMN:
			fprintf(search->out, "%d", record->column);
			break;
		case SEARCH_FIELD_CONTEXT:
			fputs(record->context ? record->context : "", search->out);
			break;
		case SEARCH_FIELD_SYMBOL:
			fputs(record->symbol, search->out);
			break;
		case SEARCH_FIELD_RECORD:
			fputs(record_name(record), search->out);
			break;
		case SEARCH_FIELD_MODE:
			print_mode(search->out, record);
			break;
		case SEARCH_FIELD_KIND:
			fputs(semindex_symbol_kind_name(record->kind), search->out);
			break;
		case SEARCH_FIELD_KIND_CHAR:
			fputc(output_symbol_kind_char(record->kind), search->out);
			break;
		case SEARCH_FIELD_SOURCE:
			if (print_source_line(search, record->root, record->file, record->line) < 0)
				return -1;

			break;
		}
	}
	fputc('\n', search->out);

	return ferror(search->out) ? -1 : 0;
}
