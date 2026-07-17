// SPDX-License-Identifier: GPL-2.0-or-later
#include "output.h"

static const char* kind_to_string(semindex_symbol_kind_t kind)
{
	switch (kind) {
		case SEMINDEX_SYMBOL_VAR:      return "var";
		case SEMINDEX_SYMBOL_FIELD:    return "field";
		case SEMINDEX_SYMBOL_STRUCT:   return "struct";
		case SEMINDEX_SYMBOL_UNION:    return "union";
		case SEMINDEX_SYMBOL_TYPEDEF:  return "typedef";
		case SEMINDEX_SYMBOL_FUNCTION: return "function";
		default:
			return "?";
	};
}

static const char* use_kind_to_string(semindex_use_kind_t k)
{
	switch (k) {
		case SEMINDEX_USE_READ:  return "READ";
		case SEMINDEX_USE_WRITE: return "WRITE";
		case SEMINDEX_USE_ADDR:  return "ADDR";
		case SEMINDEX_USE_CALL:  return "CALL";
		default:
			return "?";
	}
}

int output_default(FILE* out, semindex_t* s)
{
	fprintf(out, "SYMBOLS:\n");

	for (size_t i = 0; i < semindex_symbol_count(s); i++) {
		const semindex_symbol_t* sym = semindex_get_symbol(s, i);

		fprintf(out, "%s:%u:%u %-8s %-10s %s\n", sym->file,
		    sym->line, sym->column, kind_to_string(sym->kind),
		    sym->name, sym->type);
	}

	fprintf(out, "\nUSES:\n");

	for (size_t i = 0; i < semindex_use_count(s); i++) {
		const semindex_use_t* u = semindex_get_use(s, i);

		fprintf(out, "%s:%u:%u %-5s %s\n", u->file, u->line,
		    u->column, use_kind_to_string(u->kind), u->usr);
	}

	return ferror(out) ? -1 : 0;
}
