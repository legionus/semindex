// SPDX-License-Identifier: GPL-2.0-or-later
#include <stdio.h>

#include "semind.h"

int main(int argc, char **argv)
{
	if (argc == 1) {
		printf("Usage: semind <source>\n");
		return 1;
	}

	semind_t* s = semind_create();

	semind_index_file(s, "compile_commands.json", argv[1]);

	for (size_t i = 0; i < semind_symbol_count(s); i++) {
		const semind_symbol_t* sym = semind_get_symbol(s, i);

		//printf("%s %s %s\n", sym->usr, sym->name, sym->type);

		printf("%s:%u:%u %-8s %-10s %s\n", sym->file, sym->line,
		    sym->column, kind_to_string(sym->kind), sym->name,
		    sym->type);
	}

	semind_destroy(s);

	return 0;
}
