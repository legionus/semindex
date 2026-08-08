// SPDX-License-Identifier: GPL-2.0-or-later
#include <stddef.h>
#include <string.h>

#include "array_size.h"
#include "semantic_names.h"

struct symbol_kind_name {
	semindex_symbol_kind_t kind;
	const char *name;
};

static const struct symbol_kind_name symbol_kind_names[] = {
	{ SEMINDEX_SYMBOL_VAR, "variable" },
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

const char *semindex_symbol_kind_name(semindex_symbol_kind_t kind)
{
	size_t i;

	for (i = 0; i < ARRAY_SIZE(symbol_kind_names); i++) {
		if (kind == symbol_kind_names[i].kind)
			return symbol_kind_names[i].name;
	}

	return "unknown";
}

int semindex_symbol_kind_parse(const char *name, semindex_symbol_kind_t *kind)
{
	size_t i;

	for (i = 0; i < ARRAY_SIZE(symbol_kind_names); i++) {
		if (!strcmp(name, symbol_kind_names[i].name)) {
			*kind = symbol_kind_names[i].kind;

			return 0;
		}
	}

	return -1;
}
