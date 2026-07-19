// SPDX-License-Identifier: GPL-2.0-or-later
#include <string.h>

#include "output.h"

static const char *symbol_name_for_dissect(const char *owner, const char *name, char *buf, size_t len)
{
	if (owner && owner[0]) {
		snprintf(buf, len, "%s.%s", owner, name ? name : "");
		return buf;
	}

	return name ? name : "";
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
			sym->local ? '.' : ' ', output_symbol_kind_char(sym->kind), symbol_name, sym->type);
	else
		fprintf(out, "%4u:%-3u %-16s %s %c %c %s\n", sym->line, sym->column, sym->context, action,
			sym->local ? '.' : ' ', output_symbol_kind_char(sym->kind), symbol_name);
}

static void print_dissect_use(FILE *out, const semindex_use_t *use)
{
	char name[256];
	char mode[4];
	const char *symbol_name;

	symbol_name = symbol_name_for_dissect(use->owner, use->name, name, sizeof(name));
	output_mode_string(use->mode, mode);
	if (use->type && use->type[0])
		fprintf(out, "%4u:%-3u %-16s %s %c %c %-32s %s\n", use->line, use->column, use->context, mode,
			use->local ? '.' : ' ', output_symbol_kind_char(use->symbol_kind), symbol_name, use->type);
	else
		fprintf(out, "%4u:%-3u %-16s %s %c %c %s\n", use->line, use->column, use->context, mode,
			use->local ? '.' : ' ', output_symbol_kind_char(use->symbol_kind), symbol_name);
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
