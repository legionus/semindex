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
	       "  -f, --format=STRING        set the output format\n"
	       "  -m, --mode=MODE            limit results to an access mode\n"
	       "      --variant=PATTERN       limit results to matching variants\n"
	       "  -r, --record=RECORD        select record type: all, symbol, use\n"
	       "                             (default: all)\n"
	       "  -k, --kind=KIND            limit results to a symbol kind\n"
	       "  -h, --help                 display this help and exit\n"
	       "\n"
	       "KIND is one of: var, field, struct, union, enum, enumerator,\n"
	       "typedef, function, macro, file.\n"
	       "\n"
	       "MODE is def, one of r, w, m, -, or a three-character access mode.\n"
	       "The three positions describe address, value, and pointer access.\n"
	       "\n"
	       "Format substitutions are: %%f file, %%F variant-qualified file,\n"
	       "%%v variant, %%l line, %%c column, %%C context, %%n symbol,\n"
	       "%%m access mode, %%k kind, and %%s source line.\n"
	       "Backslash escapes \\t, \\r, and \\n are supported.\n"
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

static int parse_mode(const char *value, index_db_search_options_t *opts)
{
	static const unsigned modes[] = {
		SEMINDEX_MODE_R_AOF,
		SEMINDEX_MODE_W_AOF,
		SEMINDEX_MODE_R_AOF | SEMINDEX_MODE_W_AOF,
		SEMINDEX_MODE_R_VAL,
		SEMINDEX_MODE_W_VAL,
		SEMINDEX_MODE_R_VAL | SEMINDEX_MODE_W_VAL,
		SEMINDEX_MODE_R_PTR,
		SEMINDEX_MODE_W_PTR,
		SEMINDEX_MODE_R_PTR | SEMINDEX_MODE_W_PTR,
	};
	const char *expanded = value;
	size_t len = strlen(value);
	int i;

	if (!strcmp(value, "def")) {
		opts->has_mode = 1;
		opts->mode_definition = 1;
		return 0;
	}
	if (len == 1) {
		switch (value[0]) {
		case 'r':
			expanded = "rrr";
			break;
		case 'w':
			expanded = "ww-";
			break;
		case 'm':
			expanded = "mmm";
			break;
		case '-':
			expanded = "---";
			break;
		default:
			return -1;
		}
	} else if (len != 3) {
		return -1;
	}

	opts->mode = 0;

	for (i = 0; i < 3; i++) {
		switch (expanded[i]) {
		case 'r':
			opts->mode |= modes[i * 3];
			break;
		case 'w':
			opts->mode |= modes[i * 3 + 1];
			break;
		case 'm':
			opts->mode |= modes[i * 3 + 2];
			break;
		case '-':
			break;
		default:
			return -1;
		}
	}
	opts->has_mode = 1;

	return 0;
}

int cmd_search(int argc, char **argv)
{
	static const struct option long_options[] = {
		{ "database", required_argument, NULL, 'd' },
		{ "variant", required_argument, NULL, 1 },
		{ "path", required_argument, NULL, 'p' },
		{ "format", required_argument, NULL, 'f' },
		{ "mode", required_argument, NULL, 'm' },
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

	while ((opt = getopt_long(argc, argv, "+d:p:f:m:r:k:h", long_options, NULL)) != -1) {
		switch (opt) {
		case 1:
			opts.variant = optarg;
			break;
		case 'd':
			database = optarg;
			break;
		case 'p':
			opts.path = optarg;
			break;
		case 'f':
			opts.format = optarg;
			break;
		case 'm':
			if (parse_mode(optarg, &opts) < 0) {
				fprintf(stderr, "semindex: invalid mode: %s\n", optarg);
				return 1;
			}
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
