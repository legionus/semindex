// SPDX-License-Identifier: GPL-2.0-or-later
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>

#include "command_db.h"
#include "index_pipeline.h"
#include "perf_trace.h"
#include "semindex_cli.h"

static void index_usage(FILE *f)
{
	fprintf(f, "Usage: semindex index [OPTION]... SOURCE\n");
}

static void index_help(void)
{
	index_usage(stdout);
	printf("\n"
	       "Index a C or preprocessed assembly source file using clang semantic\n"
	       "information.\n"
	       "\n"
	       "Arguments:\n"
	       "  SOURCE                     .c or .S source file to index\n"
	       "\n"
	       "Options:\n"
	       "  -f, --format=FORMAT        print index without storing it using selected\n"
	       "                             format: dissect, json\n"
	       "  -s, --scope=SCOPE          select indexed source scope: "
	       "file, project, all\n"
	       "                             (default: project)\n"
	       "  -c, --compile-commands=PATH\n"
	       "                             path to compile_commands.json or "
	       "its directory\n"
	       "                             (default: .)\n"
	       "  -d, --database=PATH        path to the semindex database\n"
	       "                             (default: .semindex/semindex.db)\n"
	       "      --commands-database=PATH\n"
	       "                             path to the compiler command database\n"
	       "                             (default: commands.db beside --database)\n"
	       "      --variant=NAME          store records in the named variant\n"
	       "                             (default: general)\n"
	       "      --no-store-command      do not store the selected compile command\n"
	       "      --no-include-local      do not index local symbols or their uses\n"
	       "      --trace=FILE            append performance events to FILE\n"
	       "  -h, --help                 display this help and exit\n"
	       "\n"
	       "Report bugs to authors.\n"
	       "\n");
}

int cmd_index(int argc, char **argv)
{
	static const struct option long_options[] = {
		{ "no-include-local", no_argument, NULL, 1 },
		{ "variant", required_argument, NULL, 2 },
		{ "commands-database", required_argument, NULL, 3 },
		{ "no-store-command", no_argument, NULL, 4 },
		{ "trace", required_argument, NULL, 5 },
		{ "format", required_argument, NULL, 'f' },
		{ "scope", required_argument, NULL, 's' },
		{ "compile-commands", required_argument, NULL, 'c' },
		{ "database", required_argument, NULL, 'd' },
		{ "help", no_argument, NULL, 'h' },
		{ NULL, 0, NULL, 0 },
	};
	enum output_format format = FORMAT_DISSECT;
	semindex_scope_t scope = SEMINDEX_SCOPE_PROJECT;
	const char *source_file = NULL;
	const char *compile_commands = ".";
	const char *database = ".semindex/semindex.db";
	const char *commands_database = NULL;
	const char *variant = "general";
	const char *trace_path = NULL;

	semindex_trace_t *trace = NULL;
	semindex_trace_time_t phase_start;
	semindex_trace_time_t total_start = 0;

	index_pipeline_request_t request;
	index_pipeline_result_t result = { 0 };
	index_pipeline_storage_t storage;

	char *default_commands_database = NULL;
	int ret = 1;
	int include_local = 1;
	int store_command = 1;
	int output_only = 0;
	int opt;

	optind = 1;

	while ((opt = getopt_long(argc, argv, "f:s:c:d:h", long_options, NULL)) != -1) {
		switch (opt) {
		case 1:
			include_local = 0;
			break;
		case 2:
			variant = optarg;
			break;
		case 3:
			commands_database = optarg;
			break;
		case 4:
			store_command = 0;
			break;
		case 5:
			trace_path = optarg;
			break;
		case 'f':
			if (parse_format(optarg, &format) < 0) {
				fprintf(stderr, "semindex: unknown format: %s\n", optarg);
				return 1;
			}
			output_only = 1;
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
	if (!output_only && store_command && !commands_database) {
		default_commands_database = command_db_default_path(database);

		if (!default_commands_database) {
			fprintf(stderr, "semindex: failed to allocate command database path\n");
			return 1;
		}
		commands_database = default_commands_database;
	}

	if (trace_path) {
		trace = semindex_trace_open(trace_path, "index", source_file);

		if (!trace)
			goto out;

		total_start = semindex_trace_begin(trace);
	}

	if (output_only)
		storage = INDEX_PIPELINE_OUTPUT_ONLY;
	else if (store_command)
		storage = INDEX_PIPELINE_STORE_SYMBOLS_AND_COMMAND;
	else
		storage = INDEX_PIPELINE_STORE_SYMBOLS;

	request = (index_pipeline_request_t){
		.input = INDEX_PIPELINE_COMPILE_COMMANDS,
		.storage = storage,
		.partial = INDEX_PIPELINE_STORE_PARTIAL,
		.compile_commands = compile_commands,
		.source_file = source_file,
		.symbol_database = database,
		.commands_database = commands_database,
		.variant = variant,
		.scope = scope,
		.trace = trace,
		.include_local = include_local,
		.details = 1,
	};

	if (index_pipeline_run(&request, &result) < 0) {
		if (result.failed_stage == INDEX_PIPELINE_STAGE_CREATE)
			fprintf(stderr, "semindex: failed to create indexer\n");
		else if (result.failed_stage == INDEX_PIPELINE_STAGE_FRONTEND)
			fprintf(stderr, "semindex: failed to index '%s' using '%s'\n", source_file, compile_commands);
		else if (result.failed_stage == INDEX_PIPELINE_STAGE_FINGERPRINT)
			fprintf(stderr, "semindex: failed to fingerprint '%s'\n", source_file);

		goto out;
	}

	phase_start = semindex_trace_begin(trace);
	ret = output_index(format, result.index);
	semindex_trace_end(trace, "output", phase_start);
	ret = ret ? 1 : 0;

out:
	index_pipeline_result_destroy(&result, trace);
	free(default_commands_database);
	semindex_trace_end(trace, "total", total_start);

	if (semindex_trace_close(trace) < 0)
		ret = 1;
	return ret;
}
