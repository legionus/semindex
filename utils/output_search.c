// SPDX-License-Identifier: GPL-2.0-or-later
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "output.h"

struct output_search {
	FILE *out;
	const char *format;
	char *path;
	FILE *source;
	char *text;
	size_t capacity;
	ssize_t length;
	int line;
};

static void print_mode(FILE *out, const output_search_record_t *record)
{
	char str[4];

	if (record->symbol_record) {
		fputs(record->definition ? "def" : "decl", out);
		return;
	}
	output_mode_string(record->mode, str);
	fputs(str, out);
}

static int open_source(output_search_t *search, const char *path)
{
	if (search->source && search->path && !strcmp(search->path, path))
		return 0;

	if (search->source)
		fclose(search->source);
	free(search->path);
	search->path = strdup(path);
	search->source = NULL;
	search->line = 0;
	search->length = 0;
	if (!search->path)
		return -1;
	search->source = fopen(path, "r");
	if (!search->source) {
		fprintf(stderr, "semindex: failed to open source file '%s': %s\n", path, strerror(errno));
		return -1;
	}

	return 0;
}

static int print_source_line(output_search_t *search, const char *path, int line)
{
	if (open_source(search, path) < 0)
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

output_search_t *output_search_create(FILE *out, const char *format)
{
	output_search_t *search;

	if (!out)
		return NULL;
	search = calloc(1, sizeof(*search));
	if (!search)
		return NULL;
	search->out = out;
	search->format = format ? format : OUTPUT_SEARCH_DEFAULT_FORMAT;

	return search;
}

void output_search_destroy(output_search_t *search)
{
	if (!search)
		return;
	if (search->source)
		fclose(search->source);
	free(search->path);
	free(search->text);
	free(search);
}

int output_search_write(output_search_t *search, const output_search_record_t *record)
{
	const char *p;

	if (!search || !record)
		return -1;
	for (p = search->format; *p; p++) {
		char c = *p;

		if (c == '\\') {
			c = *++p;
			if (!c)
				break;
			switch (c) {
			case 't':
				c = '\t';
				break;
			case 'r':
				c = '\r';
				break;
			case 'n':
				c = '\n';
				break;
			}
			fputc(c, search->out);
			continue;
		}
		if (c != '%') {
			fputc(c, search->out);
			continue;
		}

		c = *++p;
		if (!c) {
			fprintf(stderr, "semindex: unexpected end of format string\n");
			return -1;
		}
		switch (c) {
		case 'f':
			fputs(record->file, search->out);
			break;
		case 'F':
			fprintf(search->out, "%s:%s", record->variant, record->file);
			break;
		case 'v':
			fputs(record->variant, search->out);
			break;
		case 'l':
			fprintf(search->out, "%d", record->line);
			break;
		case 'c':
			fprintf(search->out, "%d", record->column);
			break;
		case 'C':
			fputs(record->context ? record->context : "", search->out);
			break;
		case 'n':
			fputs(record->symbol, search->out);
			break;
		case 'm':
			print_mode(search->out, record);
			break;
		case 'k':
			fputc(output_symbol_kind_char(record->kind), search->out);
			break;
		case 's':
			if (print_source_line(search, record->file, record->line) < 0)
				return -1;
			break;
		default:
			fprintf(stderr, "semindex: invalid format specification: %%%c\n", c);
			return -1;
		}
	}
	fputc('\n', search->out);

	return ferror(search->out) ? -1 : 0;
}
