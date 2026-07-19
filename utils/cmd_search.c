// SPDX-License-Identifier: GPL-2.0-or-later
#include <getopt.h>
#include <stdio.h>
#include <string.h>

#include "index_db.h"
#include "semindex_cli.h"

static void search_usage(FILE *f)
{
	fprintf(f, "Usage: semindex search [OPTION]... [PATTERN]\n");
}

static void search_help(void)
{
	search_usage(stdout);
	printf("\n"
	       "Search symbol records stored in the semindex database.\n"
	       "\n"
	       "PATTERN is matched against qualified symbol names. Patterns containing\n"
	       "`*', `?', or `[]' are treated as SQLite GLOB patterns; other patterns\n"
	       "must match exactly.\n"
	       "\n"
	       "Options:\n"
	       "  -d, --database=PATH        path to the semindex database\n"
	       "                             (default: .semindex/semindex.db)\n"
	       "  -p, --path=PATTERN         limit results to matching file paths\n"
	       "  -r, --record=RECORD        select record type: all, symbol, use\n"
	       "                             (default: all)\n"
	       "  -k, --kind=KIND            limit results to a symbol kind\n"
	       "  -h, --help                 display this help and exit\n"
	       "\n"
	       "KIND is one of: var, field, struct, union, enum, enumerator,\n"
	       "typedef, function, macro, file.\n"
	       "\n"
	       "Report bugs to authors.\n"
	       "\n");
}

static int parse_record(const char *value, index_db_record_t *record)
{
	if (!strcmp(value, "all"))
		*record = INDEX_DB_RECORD_ALL;
	else if (!strcmp(value, "symbol"))
		*record = INDEX_DB_RECORD_SYMBOL;
	else if (!strcmp(value, "use"))
		*record = INDEX_DB_RECORD_USE;
	else
		return -1;

	return 0;
}

static int parse_kind(const char *value, int *kind)
{
	if (!strcmp(value, "var"))
		*kind = SEMINDEX_SYMBOL_VAR;
	else if (!strcmp(value, "field"))
		*kind = SEMINDEX_SYMBOL_FIELD;
	else if (!strcmp(value, "struct"))
		*kind = SEMINDEX_SYMBOL_STRUCT;
	else if (!strcmp(value, "union"))
		*kind = SEMINDEX_SYMBOL_UNION;
	else if (!strcmp(value, "enum"))
		*kind = SEMINDEX_SYMBOL_ENUM;
	else if (!strcmp(value, "enumerator"))
		*kind = SEMINDEX_SYMBOL_ENUM_CONSTANT;
	else if (!strcmp(value, "typedef"))
		*kind = SEMINDEX_SYMBOL_TYPEDEF;
	else if (!strcmp(value, "function"))
		*kind = SEMINDEX_SYMBOL_FUNCTION;
	else if (!strcmp(value, "macro"))
		*kind = SEMINDEX_SYMBOL_MACRO;
	else if (!strcmp(value, "file"))
		*kind = SEMINDEX_SYMBOL_FILE;
	else
		return -1;

	return 0;
}

int cmd_search(int argc, char **argv)
{
	static const struct option long_options[] = {
		{ "database", required_argument, NULL, 'd' },
		{ "path", required_argument, NULL, 'p' },
		{ "record", required_argument, NULL, 'r' },
		{ "kind", required_argument, NULL, 'k' },
		{ "help", no_argument, NULL, 'h' },
		{ NULL, 0, NULL, 0 },
	};
	index_db_search_options_t opts = {
		.record = INDEX_DB_RECORD_ALL,
	};
	const char *database = ".semindex/semindex.db";
	int opt;

	optind = 1;
	while ((opt = getopt_long(argc, argv, "+d:p:r:k:h", long_options, NULL)) != -1) {
		switch (opt) {
		case 'd':
			database = optarg;
			break;
		case 'p':
			opts.path = optarg;
			break;
		case 'r':
			if (parse_record(optarg, &opts.record) < 0) {
				fprintf(stderr, "semindex: unknown record type: %s\n", optarg);
				return 1;
			}
			break;
		case 'k':
			if (parse_kind(optarg, &opts.kind) < 0) {
				fprintf(stderr, "semindex: unknown symbol kind: %s\n", optarg);
				return 1;
			}
			opts.has_kind = 1;
			break;
		case 'h':
			search_help();
			return 0;
		default:
			search_usage(stderr);
			return 1;
		}
	}

	if (argc - optind > 1) {
		search_usage(stderr);
		return 1;
	}
	if (optind < argc)
		opts.pattern = argv[optind];

	return index_db_search(database, &opts, stdout) < 0 ? 1 : 0;
}
