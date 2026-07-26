// SPDX-License-Identifier: GPL-2.0-or-later
#include <string.h>

#include "semindex_cli.h"

int parse_format(const char *value, enum output_format *format)
{
	if (!strcmp(value, "dissect"))
		*format = FORMAT_DISSECT;

	else if (!strcmp(value, "json"))
		*format = FORMAT_JSON;
	else
		return -1;

	return 0;
}

int parse_scope(const char *value, semindex_scope_t *scope)
{
	if (!strcmp(value, "file"))
		*scope = SEMINDEX_SCOPE_FILE;

	else if (!strcmp(value, "project"))
		*scope = SEMINDEX_SCOPE_PROJECT;

	else if (!strcmp(value, "all"))
		*scope = SEMINDEX_SCOPE_ALL;
	else
		return -1;

	return 0;
}

int output_index(enum output_format format, semindex_t *s)
{
	if (format == FORMAT_JSON)
		return output_json(stdout, s);

	return output_dissect(stdout, s);
}
