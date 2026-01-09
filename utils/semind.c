// SPDX-License-Identifier: GPL-2.0-or-later
#include <stdio.h>

#include "semind.h"

static const char* kind_to_string(semind_symbol_kind_t kind)
{
	switch (kind) {
		case SEMIND_SYMBOL_VAR:      return "var";
		case SEMIND_SYMBOL_FIELD:    return "field";
		case SEMIND_SYMBOL_STRUCT:   return "struct";
		case SEMIND_SYMBOL_UNION:    return "union";
		case SEMIND_SYMBOL_TYPEDEF:  return "typedef";
		case SEMIND_SYMBOL_FUNCTION: return "function";
		default:
			return "?";
	};
}

static const char* use_kind_to_string(semind_use_kind_t k)
{
	switch (k) {
		case SEMIND_USE_READ:  return "READ";
		case SEMIND_USE_WRITE: return "WRITE";
		case SEMIND_USE_ADDR:  return "ADDR";
	       case SEMIND_USE_CALL:   return "CALL";
		default:
			return "?";
	}
}

int main(int argc, char** argv)
{
	if (argc == 1) {
		printf("Usage: semind <source>\n");
		return 1;
	}

	semind_t* s = semind_create();

	semind_index_file(s, "compile_commands.json", argv[1]);

	printf("SYMBOLS:\n");

	for (size_t i = 0; i < semind_symbol_count(s); i++) {
		const semind_symbol_t* sym = semind_get_symbol(s, i);

		//printf("%s %s %s\n", sym->usr, sym->name, sym->type);

		printf("%s:%u:%u %-8s %-10s %s\n", sym->file, sym->line,
		    sym->column, kind_to_string(sym->kind), sym->name,
		    sym->type);
	}

	printf("\nUSES:\n");

	for (size_t i = 0; i < semind_use_count(s); i++) {
		const semind_use_t* u = semind_get_use(s, i);

		printf("%s:%u:%u %-5s %s\n", u->file, u->line, u->column,
		    use_kind_to_string(u->kind), u->usr);
	}

	semind_destroy(s);

	return 0;
}
