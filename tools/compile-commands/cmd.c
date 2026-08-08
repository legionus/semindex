// SPDX-License-Identifier: GPL-2.0-or-later
#include <getopt.h>
#include <stdio.h>

#include "command_db.h"

static void compile_commands_usage(FILE *f)
{
	fprintf(f, "Usage: semindex compile-commands [OPTION]...\n");
}

static void compile_commands_help(void)
{
	compile_commands_usage(stdout);
	printf("\n"
	       "Export stored compiler arguments as compile_commands.json.\n"
	       "\n"
	       "Options:\n"
	       "  -d, --database=PATH        path to the compiler command database\n"
	       "                             (default: .semindex/commands.db)\n"
	       "      --variant=NAME          export commands from the named variant\n"
	       "                             (default: general)\n"
	       "  -o, --output=PATH          write output to PATH instead of stdout\n"
	       "  -h, --help                 display this help and exit\n"
	       "\n"
	       "Report bugs to authors.\n"
	       "\n");
}

int cmd_compile_commands(int argc, char **argv)
{
	static const struct option long_options[] = {
		{ "variant", required_argument, NULL, 1 },
		{ "database", required_argument, NULL, 'd' },
		{ "output", required_argument, NULL, 'o' },
		{ "help", no_argument, NULL, 'h' },
		{ NULL, 0, NULL, 0 },
	};
	const char *database = ".semindex/commands.db";
	const char *variant = "general";
	const char *output = NULL;

	FILE *out = stdout;
	int ret;
	int opt;

	optind = 1;

	while ((opt = getopt_long(argc, argv, "d:o:h", long_options, NULL)) != -1) {
		switch (opt) {
		case 1:
			variant = optarg;
			break;
		case 'd':
			database = optarg;
			break;
		case 'o':
			output = optarg;
			break;
		case 'h':
			compile_commands_help();
			return 0;

		default:
			compile_commands_usage(stderr);
			return 1;
		}
	}

	if (optind != argc) {
		compile_commands_usage(stderr);
		return 1;
	}
	if (!variant[0]) {
		fprintf(stderr, "semindex: variant name must not be empty\n");
		return 1;
	}
	if (output) {
		out = fopen(output, "w");

		if (!out) {
			perror("semindex: failed to open output file");
			return 1;
		}
	}

	ret = command_db_export(database, variant, out);

	if (output && fclose(out) != 0) {
		perror("semindex: failed to close output file");
		ret = -1;
	}
	return ret ? 1 : 0;
}
