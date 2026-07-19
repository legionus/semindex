// SPDX-License-Identifier: GPL-2.0-or-later
#include <getopt.h>
#include <stdio.h>
#include <string.h>

#include "semindex_cli.h"

void semindex_usage(FILE *f)
{
	fprintf(f, "Usage: semindex COMMAND [OPTION]...\n");
}

void semindex_help(void)
{
	semindex_usage(stdout);
	printf("\n"
	       "Index C source files using clang semantic information.\n"
	       "\n"
	       "Commands:\n"
	       "  compiler                   index from an explicit compiler argument vector\n"
	       "  index                      index a source file using `compile_commands.json'\n"
	       "  search                     search stored symbol and use records\n"
	       "\n"
	       "Run 'semindex COMMAND --help' for command-specific help.\n"
	       "\n"
	       "Options:\n"
	       "  -h, --help                 display this help and exit\n"
	       "\n"
	       "Report bugs to authors.\n"
	       "\n");
}

int parse_format(const char *value, enum output_format *format)
{
	if (!strcmp(value, "default"))
		*format = FORMAT_DEFAULT;
	else if (!strcmp(value, "dissect"))
		*format = FORMAT_DISSECT;
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
	if (format == FORMAT_DISSECT)
		return output_dissect(stdout, s);

	return output_default(stdout, s);
}

int main(int argc, char **argv)
{
	static const struct option long_options[] = {
		{ "help", no_argument, NULL, 'h' },
		{ NULL, 0, NULL, 0 },
	};
	int opt;

	while ((opt = getopt_long(argc, argv, "+h", long_options, NULL)) != -1) {
		switch (opt) {
		case 'h':
			semindex_help();
			return 0;
		default:
			semindex_usage(stderr);
			return 1;
		}
	}

	if (optind >= argc) {
		semindex_usage(stderr);
		return 1;
	}

	if (!strcmp(argv[optind], "compiler"))
		return cmd_compiler(argc - optind, argv + optind);
	if (!strcmp(argv[optind], "index"))
		return cmd_index(argc - optind, argv + optind);
	if (!strcmp(argv[optind], "search"))
		return cmd_search(argc - optind, argv + optind);

	fprintf(stderr, "semindex: unknown command: %s\n", argv[optind]);
	semindex_usage(stderr);
	return 1;
}
