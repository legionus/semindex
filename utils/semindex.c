// SPDX-License-Identifier: GPL-2.0-or-later
#include <getopt.h>
#include <stdio.h>
#include <string.h>

#include "output.h"

static void usage(FILE *f)
{
	fprintf(f, "Usage: semindex COMMAND [OPTION]...\n");
}

static void help(void)
{
	usage(stdout);
	printf("\n"
	       "Index C source files using clang semantic information.\n"
	       "\n"
	       "Commands:\n"
	       "  index                      index a source file using "
	       "compile_commands.json\n"
	       "\n"
	       "Run 'semindex COMMAND --help' for command-specific help.\n"
	       "\n"
	       "Options:\n"
	       "  -h, --help                 display this help and exit\n"
	       "\n"
	       "Report bugs to authors.\n"
	       "\n");
}

static void index_usage(FILE *f)
{
	fprintf(f, "Usage: semindex index [OPTION]... SOURCE\n");
}

static void index_help(void)
{
	index_usage(stdout);
	printf("\n"
	       "Index a C source file using clang semantic information.\n"
	       "\n"
	       "Arguments:\n"
	       "  SOURCE                     C source file to index\n"
	       "\n"
	       "Options:\n"
	       "  -f, --format=FORMAT        select output format: default, "
	       "dissect\n"
	       "                             (default: default)\n"
	       "  -s, --scope=SCOPE          select indexed source scope: "
	       "file, project, all\n"
	       "                             (default: project)\n"
	       "  -c, --compile-commands=PATH\n"
	       "                             path to compile_commands.json or "
	       "its directory\n"
	       "                             (default: .)\n"
	       "  -h, --help                 display this help and exit\n"
	       "\n"
	       "Report bugs to authors.\n"
	       "\n");
}

static int parse_format(const char *value, enum output_format *format)
{
	if (!strcmp(value, "default"))
		*format = FORMAT_DEFAULT;
	else if (!strcmp(value, "dissect"))
		*format = FORMAT_DISSECT;
	else
		return -1;

	return 0;
}

static int parse_scope(const char *value, semindex_scope_t *scope)
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

static int cmd_index(int argc, char **argv)
{
	static const struct option long_options[] = {
		{ "format", required_argument, NULL, 'f' },
		{ "scope", required_argument, NULL, 's' },
		{ "compile-commands", required_argument, NULL, 'c' },
		{ "help", no_argument, NULL, 'h' },
		{ NULL, 0, NULL, 0 },
	};
	enum output_format format = FORMAT_DEFAULT;
	semindex_scope_t scope = SEMINDEX_SCOPE_PROJECT;
	const char *source_file = NULL;
	const char *compile_commands = ".";
	semindex_t *s;
	int ret;
	int opt;

	optind = 1;
	while ((opt = getopt_long(argc, argv, "f:s:c:h", long_options, NULL)) != -1) {
		switch (opt) {
		case 'f':
			if (parse_format(optarg, &format) < 0) {
				fprintf(stderr, "semindex: unknown format: %s\n", optarg);
				return 1;
			}
			break;
		case 's':
			if (parse_scope(optarg, &scope) < 0) {
				fprintf(stderr, "semindex: unknown scope: %s\n", optarg);
				return 1;
			}
			break;
		case 'c':
			compile_commands = optarg;
			break;
		case 'h':
			index_help();
			return 0;
		default:
			index_usage(stderr);
			return 1;
		}
	}

	if (optind < argc)
		source_file = argv[optind++];

	if (optind < argc) {
		index_usage(stderr);
		return 1;
	}

	if (!source_file) {
		index_usage(stderr);
		return 1;
	}

	s = semindex_create();
	semindex_set_scope(s, scope);

	if (semindex_index_file(s, compile_commands, source_file) != 0) {
		fprintf(stderr, "semindex: failed to index '%s' using '%s'\n", source_file, compile_commands);
		semindex_destroy(s);
		return 1;
	}

	if (format == FORMAT_DISSECT)
		ret = output_dissect(stdout, s);
	else
		ret = output_default(stdout, s);

	semindex_destroy(s);
	return ret ? 1 : 0;
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
			help();
			return 0;
		default:
			usage(stderr);
			return 1;
		}
	}

	if (optind >= argc) {
		usage(stderr);
		return 1;
	}

	if (!strcmp(argv[optind], "index"))
		return cmd_index(argc - optind, argv + optind);

	fprintf(stderr, "semindex: unknown command: %s\n", argv[optind]);
	usage(stderr);
	return 1;
}
