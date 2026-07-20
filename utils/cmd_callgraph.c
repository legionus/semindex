// SPDX-License-Identifier: GPL-2.0-or-later
#include <errno.h>
#include <getopt.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "index_db.h"

static void callgraph_usage(FILE *f)
{
	fprintf(f, "Usage: semindex callgraph (--callers=FUNCTION | --callees=FUNCTION) [OPTION]...\n");
}

static void callgraph_help(void)
{
	callgraph_usage(stdout);
	printf("\n"
	       "Query direct function calls stored in the semindex database.\n"
	       "\n"
	       "Options:\n"
	       "      --callers=FUNCTION      show functions that call FUNCTION\n"
	       "      --callees=FUNCTION      show functions called by FUNCTION\n"
	       "      --id=ID                 disambiguate FUNCTION by its hexadecimal ID\n"
	       "      --show-id               include caller and callee IDs in output\n"
	       "      --variant=PATTERN       limit results to matching variants\n"
	       "  -p, --path=PATTERN          limit results to matching callsite paths\n"
	       "  -d, --database=PATH         path to the semindex database\n"
	       "                              (default: .semindex/semindex.db)\n"
	       "  -h, --help                  display this help and exit\n"
	       "\n"
	       "Variant and path patterns containing `*', `?', or `[]' use SQLite\n"
	       "GLOB matching. FUNCTION is always matched exactly.\n"
	       "\n"
	       "Report bugs to authors.\n"
	       "\n");
}

int cmd_callgraph(int argc, char **argv)
{
	static const struct option long_options[] = {
		{ "callers", required_argument, NULL, 1 },
		{ "callees", required_argument, NULL, 2 },
		{ "variant", required_argument, NULL, 3 },
		{ "id", required_argument, NULL, 4 },
		{ "show-id", no_argument, NULL, 5 },
		{ "path", required_argument, NULL, 'p' },
		{ "database", required_argument, NULL, 'd' },
		{ "help", no_argument, NULL, 'h' },
		{ NULL, 0, NULL, 0 },
	};
	index_db_callgraph_options_t opts = { 0 };
	const char *database = ".semindex/semindex.db";
	int have_direction = 0;
	int opt;

	optind = 1;
	while ((opt = getopt_long(argc, argv, "p:d:h", long_options, NULL)) != -1) {
		switch (opt) {
		case 1:
			if (have_direction) {
				fprintf(stderr, "semindex: specify exactly one of --callers or --callees\n");
				return 1;
			}
			opts.direction = INDEX_DB_CALLGRAPH_CALLERS;
			opts.function = optarg;
			have_direction = 1;
			break;
		case 2:
			if (have_direction) {
				fprintf(stderr, "semindex: specify exactly one of --callers or --callees\n");
				return 1;
			}
			opts.direction = INDEX_DB_CALLGRAPH_CALLEES;
			opts.function = optarg;
			have_direction = 1;
			break;
		case 3:
			opts.variant = optarg;
			break;
		case 4: {
			char *end;
			const char *p;

			if (!optarg[0] || strlen(optarg) > 16) {
				fprintf(stderr, "semindex: invalid function ID: %s\n", optarg);
				return 1;
			}
			for (p = optarg; *p; p++) {
				if (!isxdigit((unsigned char)*p)) {
					fprintf(stderr, "semindex: invalid function ID: %s\n", optarg);
					return 1;
				}
			}
			errno = 0;
			opts.id = strtoull(optarg, &end, 16);
			if (errno || end == optarg || *end) {
				fprintf(stderr, "semindex: invalid function ID: %s\n", optarg);
				return 1;
			}
			opts.has_id = 1;
			break;
		}
		case 5:
			opts.show_id = 1;
			break;
		case 'p':
			opts.path = optarg;
			break;
		case 'd':
			database = optarg;
			break;
		case 'h':
			callgraph_help();
			return 0;
		default:
			callgraph_usage(stderr);
			return 1;
		}
	}

	if (optind != argc) {
		callgraph_usage(stderr);
		return 1;
	}
	if (!have_direction || !opts.function[0]) {
		callgraph_usage(stderr);
		return 1;
	}
	return index_db_callgraph(database, &opts, stdout) < 0 ? 1 : 0;
}
