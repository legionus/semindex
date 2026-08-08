// SPDX-License-Identifier: GPL-2.0-or-later
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>

#include "index_command.h"
#include "index_pipeline.h"
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
	       "      --root=DIR              project root for stored source paths\n");
#ifdef SEMINDEX_HAVE_LIBGIT2
	printf("      --git-commit=COMMIT     store COMMIT as variant provenance; COMMIT may\n"
	       "                             be a 40- or 64-digit hash, or auto\n"
	       "      --no-git-commit         do not store Git provenance\n");
#endif
	printf("      --no-store-command      do not store the selected compile command\n"
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
		{ "no-include-local", no_argument, NULL, INDEX_COMMAND_OPT_NO_INCLUDE_LOCAL },
		{ "variant", required_argument, NULL, INDEX_COMMAND_OPT_VARIANT },
		{ "commands-database", required_argument, NULL, INDEX_COMMAND_OPT_COMMANDS_DATABASE },
		{ "no-store-command", no_argument, NULL, INDEX_COMMAND_OPT_NO_STORE_COMMAND },
		{ "trace", required_argument, NULL, INDEX_COMMAND_OPT_TRACE },
		{ "root", required_argument, NULL, INDEX_COMMAND_OPT_ROOT },
#ifdef SEMINDEX_HAVE_LIBGIT2
		{ "git-commit", required_argument, NULL, INDEX_COMMAND_OPT_GIT_COMMIT },
		{ "no-git-commit", no_argument, NULL, INDEX_COMMAND_OPT_NO_GIT_COMMIT },
#endif
		{ "format", required_argument, NULL, 'f' },
		{ "scope", required_argument, NULL, 's' },
		{ "compile-commands", required_argument, NULL, 'c' },
		{ "database", required_argument, NULL, 'd' },
		{ "help", no_argument, NULL, 'h' },
		{ NULL, 0, NULL, 0 },
	};
	struct index_command_options options;
	enum output_format format = FORMAT_DISSECT;
	const char *source_file = NULL;
	const char *compile_commands = ".";
	semindex_trace_time_t phase_start;

	index_pipeline_request_t request;
	index_pipeline_result_t result = { 0 };

	int ret = 1;
	int output_only = 0;
	int parsed;
	int opt;

	index_command_options_init(&options);
	optind = 1;

	while ((opt = getopt_long(argc, argv, "f:s:c:d:h", long_options, NULL)) != -1) {
		switch (opt) {
		case 'f':
			if (parse_format(optarg, &format) < 0) {
				fprintf(stderr, "semindex: unknown format: %s\n", optarg);
				return 1;
			}
			output_only = 1;
			break;
		case 'c':
			compile_commands = optarg;
			break;
		case 'h':
			index_help();
			return 0;

		default:
			parsed = index_command_parse_option(&options, opt, optarg, "semindex");

			if (parsed > 0)
				break;

			if (parsed < 0)
				return 1;

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
	if (index_command_prepare(&options, "index", source_file, output_only, "semindex") < 0)
		goto out;

	request = (index_pipeline_request_t){
		.input = INDEX_PIPELINE_COMPILE_COMMANDS,
		.storage = index_command_storage(&options, output_only),
		.partial = INDEX_PIPELINE_STORE_PARTIAL,
		.compile_commands = compile_commands,
		.source_file = source_file,
		.details = 1,
	};
	index_command_fill_request(&options, &request);

	if (index_pipeline_run(&request, &result) < 0) {
		if (result.failed_stage == INDEX_PIPELINE_STAGE_CREATE)
			fprintf(stderr, "semindex: failed to create indexer\n");
		else if (result.failed_stage == INDEX_PIPELINE_STAGE_REPOSITORY_ROOT)
			fprintf(stderr, "semindex: invalid project root: %s\n", options.repository_root);
		else if (result.failed_stage == INDEX_PIPELINE_STAGE_FRONTEND)
			fprintf(stderr, "semindex: failed to index '%s' using '%s'\n", source_file, compile_commands);
		else if (result.failed_stage == INDEX_PIPELINE_STAGE_FINGERPRINT)
			fprintf(stderr, "semindex: failed to fingerprint '%s'\n", source_file);

		goto out;
	}

	phase_start = semindex_trace_begin(options.trace);
	ret = output_index(format, result.index);
	semindex_trace_end(options.trace, "output", phase_start);
	ret = ret ? 1 : 0;

out:
	index_pipeline_result_destroy(&result, options.trace);

	if (index_command_finish(&options) < 0)
		ret = 1;
	return ret;
}
