// SPDX-License-Identifier: GPL-2.0-or-later
#include <string.h>

#include "output.h"

static char kind_to_dissect_char(semindex_symbol_kind_t kind)
{
	switch (kind) {
	case SEMINDEX_SYMBOL_FIELD:
		return 'm';
	case SEMINDEX_SYMBOL_STRUCT:
	case SEMINDEX_SYMBOL_UNION:
		return 's';
	case SEMINDEX_SYMBOL_ENUM:
		return 'e';
	case SEMINDEX_SYMBOL_ENUM_CONSTANT:
		return 'v';
	case SEMINDEX_SYMBOL_TYPEDEF:
		return 't';
	case SEMINDEX_SYMBOL_FUNCTION:
		return 'f';
	case SEMINDEX_SYMBOL_MACRO:
		return 'd';
	case SEMINDEX_SYMBOL_FILE:
		return 'i';
	default:
		return 'v';
	}
}

static const char *symbol_name_for_dissect(const char *owner, const char *name, char *buf, size_t len)
{
	if (owner && owner[0]) {
		snprintf(buf, len, "%s.%s", owner, name ? name : "");
		return buf;
	}

	return name ? name : "";
}

static const char *mode_to_string(unsigned mode)
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

static int same_string(const char *a, const char *b)
{
	if (!a)
		a = "";
	if (!b)
		b = "";

	return !strcmp(a, b);
}

static int same_dissect_use(const semindex_use_t *a, const semindex_use_t *b)
{
	return a->line == b->line && a->column == b->column && a->local == b->local && a->mode == b->mode &&
		a->symbol_kind == b->symbol_kind && same_string(a->context, b->context) &&
		same_string(a->owner, b->owner) && same_string(a->name, b->name) && same_string(a->type, b->type);
}

static void print_dissect_symbol(FILE *out, const semindex_symbol_t *sym)
{
	char name[256];
	const char *symbol_name;
	const char *action = sym->definition ? "def" : "decl";

	symbol_name = symbol_name_for_dissect(sym->owner, sym->name, name, sizeof(name));
	if (sym->type && sym->type[0])
		fprintf(out, "%4u:%-3u %-16s %s %c %c %-32s %s\n", sym->line, sym->column, sym->context, action,
			sym->local ? '.' : ' ', kind_to_dissect_char(sym->kind), symbol_name, sym->type);
	else
		fprintf(out, "%4u:%-3u %-16s %s %c %c %s\n", sym->line, sym->column, sym->context, action,
			sym->local ? '.' : ' ', kind_to_dissect_char(sym->kind), symbol_name);
}

static void print_dissect_use(FILE *out, const semindex_use_t *use)
{
	char name[256];
	const char *symbol_name;

	symbol_name = symbol_name_for_dissect(use->owner, use->name, name, sizeof(name));
	if (use->type && use->type[0])
		fprintf(out, "%4u:%-3u %-16s %s %c %c %-32s %s\n", use->line, use->column, use->context,
			mode_to_string(use->mode), use->local ? '.' : ' ', kind_to_dissect_char(use->symbol_kind),
			symbol_name, use->type);
	else
		fprintf(out, "%4u:%-3u %-16s %s %c %c %s\n", use->line, use->column, use->context,
			mode_to_string(use->mode), use->local ? '.' : ' ', kind_to_dissect_char(use->symbol_kind),
			symbol_name);
}

int output_dissect(FILE *out, semindex_t *s)
{
	size_t symbol_count = semindex_symbol_count(s);
	size_t use_count = semindex_use_count(s);
	size_t symbol_index = 0;
	size_t use_index = 0;
	const char *current_file = NULL;
	const semindex_use_t *prev_use = NULL;
	int prev_was_use = 0;

	while (symbol_index < symbol_count || use_index < use_count) {
		const semindex_symbol_t *sym = NULL;
		const semindex_use_t *use = NULL;
		int is_symbol;

		if (symbol_index < symbol_count)
			sym = semindex_get_symbol(s, symbol_index);
		if (use_index < use_count)
			use = semindex_get_use(s, use_index);

		is_symbol = use_index >= use_count || (sym && sym->order <= use->order);

		if (is_symbol) {
			if (!current_file || strcmp(current_file, sym->file)) {
				current_file = sym->file;
				fprintf(out, "\nFILE: %s\n\n", current_file);
			}

			print_dissect_symbol(out, sym);
			symbol_index++;
			prev_was_use = 0;
			continue;
		}

		if (prev_was_use && prev_use && same_string(current_file, use->file) &&
			same_dissect_use(use, prev_use)) {
			use_index++;
			continue;
		}

		if (!current_file || strcmp(current_file, use->file)) {
			current_file = use->file;
			fprintf(out, "\nFILE: %s\n\n", current_file);
		}

		print_dissect_use(out, use);
		prev_use = use;
		prev_was_use = 1;
		use_index++;
	}

	return ferror(out) ? -1 : 0;
}
