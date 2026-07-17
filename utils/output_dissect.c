// SPDX-License-Identifier: GPL-2.0-or-later
#include <stdlib.h>
#include <string.h>

#include "output.h"

struct output_row {
	const char* file;
	unsigned line;
	unsigned column;
	int is_definition;
	size_t index;
};

static char kind_to_dissect_char(semindex_symbol_kind_t kind)
{
	switch (kind) {
		case SEMINDEX_SYMBOL_FIELD:    return 'm';
		case SEMINDEX_SYMBOL_STRUCT:
		case SEMINDEX_SYMBOL_UNION:    return 's';
		case SEMINDEX_SYMBOL_FUNCTION: return 'f';
		default:                       return 'v';
	}
}

static const char* symbol_name_for_dissect(const char* owner, const char* name,
    char* buf, size_t len)
{
	if (owner && owner[0]) {
		snprintf(buf, len, "%s.%s", owner, name ? name : "");
		return buf;
	}

	return name ? name : "";
}

static const char* mode_to_string(unsigned mode)
{
	static char str[4];

	str[0] = '-';
	str[1] = '-';
	str[2] = '-';
	str[3] = '\0';

	if (mode & SEMINDEX_MODE_R_AOF)
		str[0] = 'r';
	if (mode & SEMINDEX_MODE_W_AOF)
		str[0] = str[0] == 'r' ? 'm' : 'w';
	if (mode & SEMINDEX_MODE_R_VAL)
		str[1] = 'r';
	if (mode & SEMINDEX_MODE_W_VAL)
		str[1] = str[1] == 'r' ? 'm' : 'w';
	if (mode & SEMINDEX_MODE_R_PTR)
		str[2] = 'r';
	if (mode & SEMINDEX_MODE_W_PTR)
		str[2] = str[2] == 'r' ? 'm' : 'w';

	return str;
}

static int compare_rows(const void* a, const void* b)
{
	const struct output_row* ra = a;
	const struct output_row* rb = b;
	int cmp = strcmp(ra->file, rb->file);

	if (cmp)
		return cmp;
	if (ra->line != rb->line)
		return ra->line < rb->line ? -1 : 1;
	if (ra->column != rb->column)
		return ra->column < rb->column ? -1 : 1;
	if (ra->is_definition != rb->is_definition)
		return ra->is_definition ? -1 : 1;
	return 0;
}

static void print_dissect_symbol(FILE* out, const semindex_symbol_t* sym)
{
	char name[256];

	fprintf(out, "%4u:%-3u %-16s def %c %c %-32s %s\n",
	    sym->line, sym->column, sym->context,
	    sym->local ? '.' : ' ',
	    kind_to_dissect_char(sym->kind),
	    symbol_name_for_dissect(sym->owner, sym->name, name, sizeof(name)),
	    sym->type);
}

static void print_dissect_use(FILE* out, const semindex_use_t* use)
{
	char name[256];

	fprintf(out, "%4u:%-3u %-16s %s %c %c %-32s %s\n",
	    use->line, use->column, use->context,
	    mode_to_string(use->mode),
	    use->local ? '.' : ' ',
	    kind_to_dissect_char(use->symbol_kind),
	    symbol_name_for_dissect(use->owner, use->name, name, sizeof(name)),
	    use->type);
}

int output_dissect(FILE* out, semindex_t* s)
{
	size_t symbol_count = semindex_symbol_count(s);
	size_t use_count = semindex_use_count(s);
	size_t row_count = symbol_count + use_count;
	struct output_row* rows = calloc(row_count, sizeof(*rows));
	const char* current_file = NULL;

	if (!rows)
		return -1;

	for (size_t i = 0; i < symbol_count; i++) {
		const semindex_symbol_t* sym = semindex_get_symbol(s, i);

		rows[i].file = sym->file;
		rows[i].line = sym->line;
		rows[i].column = sym->column;
		rows[i].is_definition = 1;
		rows[i].index = i;
	}

	for (size_t i = 0; i < use_count; i++) {
		const semindex_use_t* use = semindex_get_use(s, i);
		size_t row = symbol_count + i;

		rows[row].file = use->file;
		rows[row].line = use->line;
		rows[row].column = use->column;
		rows[row].is_definition = 0;
		rows[row].index = i;
	}

	qsort(rows, row_count, sizeof(*rows), compare_rows);

	for (size_t i = 0; i < row_count; i++) {
		if (!current_file || strcmp(current_file, rows[i].file)) {
			current_file = rows[i].file;
			fprintf(out, "\nFILE: %s\n\n", current_file);
		}

		if (rows[i].is_definition)
			print_dissect_symbol(out,
			    semindex_get_symbol(s, rows[i].index));
		else
			print_dissect_use(out,
			    semindex_get_use(s, rows[i].index));
	}

	free(rows);
	return ferror(out) ? -1 : 0;
}
