// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef SEMINDEX_OUTPUT_H
#define SEMINDEX_OUTPUT_H

#include <stdio.h>

#include "semindex.h"

enum output_format {
	FORMAT_DEFAULT,
	FORMAT_DISSECT,
};

#define OUTPUT_SEARCH_DEFAULT_FORMAT "(%m) %F\\t%l\\t%c\\t%C\\t%s"

typedef struct output_search output_search_t;

typedef struct {
	const char *file;
	const char *variant;
	int line;
	int column;
	int symbol_record;
	int definition;
	semindex_symbol_kind_t kind;
	unsigned mode;
	const char *symbol;
	const char *context;
} output_search_record_t;

int output_default(FILE *out, semindex_t *s);
int output_dissect(FILE *out, semindex_t *s);
char output_symbol_kind_char(semindex_symbol_kind_t kind);
void output_mode_string(unsigned mode, char str[4]);
output_search_t *output_search_create(FILE *out, const char *format);
void output_search_destroy(output_search_t *search);
int output_search_write(output_search_t *search, const output_search_record_t *record);

#endif /* SEMINDEX_OUTPUT_H */
