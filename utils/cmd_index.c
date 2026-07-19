// SPDX-License-Identifier: GPL-2.0-or-later
#include <getopt.h>
#include <stdio.h>

#include "index_db.h"
#include "semindex_cli.h"

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
	       "  -d, --database=PATH        path to the semindex database\n"
	       "                             (default: .semindex/semindex.db)\n"
	       "      --variant=NAME          store records in the named variant\n"
	       "                             (default: general)\n"
	       "      --include-local         store local symbols and their uses\n"
	       "  -h, --help                 display this help and exit\n"
	       "\n"
	       "Report bugs to authors.\n"
	       "\n");
}

int cmd_index(int argc, char **argv)
{
	static const struct option long_options[] = {
		{ "include-local", no_argument, NULL, 1 },
		{ "variant", required_argument, NULL, 2 },
		{ "format", required_argument, NULL, 'f' },
		{ "scope", required_argument, NULL, 's' },
		{ "compile-commands", required_argument, NULL, 'c' },
		{ "database", required_argument, NULL, 'd' },
		{ "help", no_argument, NULL, 'h' },
		{ NULL, 0, NULL, 0 },
	};
	enum output_format format = FORMAT_DEFAULT;
	semindex_scope_t scope = SEMINDEX_SCOPE_PROJECT;
	const char *source_file = NULL;
	const char *compile_commands = ".";
	const char *database = ".semindex/semindex.db";
	const char *variant = "general";
	semindex_t *s;
	int ret;
	int include_local = 0;
	int opt;

	optind = 1;
	while ((opt = getopt_long(argc, argv, "f:s:c:d:h", long_options, NULL)) != -1) {
		switch (opt) {
		case 1:
			include_local = 1;
			break;
		case 2:
			variant = optarg;
			break;
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
		case 'd':
			database = optarg;
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
	if (!variant[0]) {
		fprintf(stderr, "semindex: variant name must not be empty\n");
		return 1;
	}

	s = semindex_create();
	semindex_set_scope(s, scope);

	if (semindex_index_file(s, compile_commands, source_file) != 0) {
		fprintf(stderr, "semindex: failed to index '%s' using '%s'\n", source_file, compile_commands);
		semindex_destroy(s);
		return 1;
	}
	if (index_db_store(database, s, source_file, variant, include_local) < 0) {
		semindex_destroy(s);
		return 1;
	}

	ret = output_index(format, s);

	semindex_destroy(s);
	return ret ? 1 : 0;
}
