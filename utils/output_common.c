// SPDX-License-Identifier: GPL-2.0-or-later
#include "output.h"

char output_symbol_kind_char(semindex_symbol_kind_t kind)
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
	case SEMINDEX_SYMBOL_VAR:
	default:
		return 'v';
	}
}

void output_mode_string(unsigned mode, char str[4])
{
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
}
