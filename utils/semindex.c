// SPDX-License-Identifier: GPL-2.0-or-later
#include <stdio.h>
#include <string.h>

#include "output.h"

int main(int argc, char** argv)
{
	enum output_format format = FORMAT_DEFAULT;
	semindex_scope_t scope = SEMINDEX_SCOPE_PROJECT;
	const char* source_file = NULL;
	const char* compile_commands = ".";

	for (int i = 1; i < argc; i++) {
		if (!strncmp(argv[i], "--format=", 9)) {
			const char* value = argv[i] + 9;

			if (!strcmp(value, "default"))
				format = FORMAT_DEFAULT;
			else if (!strcmp(value, "dissect"))
				format = FORMAT_DISSECT;
			else {
				fprintf(stderr, "semindex: unknown format: %s\n", value);
				return 1;
			}
			continue;
		}

		if (!strncmp(argv[i], "--scope=", 8)) {
			const char* value = argv[i] + 8;

			if (!strcmp(value, "file"))
				scope = SEMINDEX_SCOPE_FILE;
			else if (!strcmp(value, "project"))
				scope = SEMINDEX_SCOPE_PROJECT;
			else if (!strcmp(value, "all"))
				scope = SEMINDEX_SCOPE_ALL;
			else {
				fprintf(stderr, "semindex: unknown scope: %s\n", value);
				return 1;
			}
			continue;
		}

		if (!source_file)
			source_file = argv[i];
		else if (compile_commands[0] == '.' && compile_commands[1] == '\0')
			compile_commands = argv[i];
		else {
			fprintf(stderr, "Usage: semindex [--format=default|dissect] [--scope=file|project|all] <source> [compile_commands]\n");
			return 1;
		}
	}

	if (!source_file) {
		fprintf(stderr, "Usage: semindex [--format=default|dissect] [--scope=file|project|all] <source> [compile_commands]\n");
		return 1;
	}

	semindex_t* s = semindex_create();
	semindex_set_scope(s, scope);

	if (semindex_index_file(s, compile_commands, source_file) != 0) {
		fprintf(stderr, "semindex: failed to index '%s' using '%s'\n",
		    source_file, compile_commands);
		semindex_destroy(s);
		return 1;
	}

	if (format == FORMAT_DISSECT)
		output_dissect(stdout, s);
	else
		output_default(stdout, s);

	semindex_destroy(s);

	return 0;
}
