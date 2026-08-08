// SPDX-License-Identifier: GPL-2.0-or-later
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "compiler_command.h"
#include "index_command.h"
#include "index_pipeline.h"
#include "semindex_cli.h"

static void compiler_usage(FILE *f)
{
	fprintf(f, "Usage: semindex compiler [OPTION]... -- [COMPILER] ARG...\n");
}

static void compiler_help(void)
{
	compiler_usage(stdout);
	printf("\n"
	       "Index a C or preprocessed assembly source file from an explicit\n"
	       "compiler argument vector.\n"
	       "\n"
	       "Arguments after '--' are treated as compiler arguments. COMPILER is\n"
	       "optional and defaults to `cc'.\n"
	       "Commands with exactly one .c or .S source file are indexed.\n"
	       "\n"
	       "Options:\n"
	       "  -f, --format=FORMAT        print index without storing it using selected\n"
	       "                             format: dissect, json\n"
	       "  -s, --scope=SCOPE          select indexed source scope: file, project, all\n"
	       "                             (default: project)\n"
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
	printf("      --no-store-command      do not store the compiler command\n"
	       "      --no-include-local      do not index local symbols or their uses\n"
	       "      --trace=FILE            append performance events to FILE\n"
	       "  -h, --help                 display this help and exit\n"
	       "\n"
	       "Report bugs to authors.\n"
	       "\n");
}

int cmd_compiler(int argc, char **argv)
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
		{ "database", required_argument, NULL, 'd' },
		{ "format", required_argument, NULL, 'f' },
		{ "scope", required_argument, NULL, 's' },
		{ "help", no_argument, NULL, 'h' },
		{ NULL, 0, NULL, 0 },
	};
	struct index_command_options options;
	enum output_format format = FORMAT_DISSECT;
	const char *source_file = NULL;
	semindex_trace_time_t phase_start;

	index_pipeline_request_t request;
	index_pipeline_result_t result = { 0 };
	semindex_compile_command_t cmd;
	int compiler_argc;

	char **compiler_argv;
	char **default_argv = NULL;
	int ret = 1;
	int print_output = 0;
	int parsed;
	int opt;

	index_command_options_init(&options);
	optind = 1;

	while ((opt = getopt_long(argc, argv, "+d:f:s:h", long_options, NULL)) != -1) {
		switch (opt) {
		case 'f':
			if (parse_format(optarg, &format) < 0) {
				fprintf(stderr, "semindex: unknown format: %s\n", optarg);
				return 1;
			}
			print_output = 1;
			break;
		case 'h':
			compiler_help();
			return 0;

		default:
			parsed = index_command_parse_option(&options, opt, optarg, "semindex");

			if (parsed > 0)
				break;

			if (parsed < 0)
				return 1;

			compiler_usage(stderr);
			return 1;
		}
	}

	if (optind < argc && !strcmp(argv[optind], "--"))
		optind++;

	if (optind >= argc) {
		compiler_usage(stderr);
		return 1;
	}
	compiler_argc = argc - optind;
	compiler_argv = argv + optind;

	if (compiler_command_driver_is_omitted(compiler_argv[0])) {
		default_argv = calloc(compiler_argc + 1, sizeof(*default_argv));

		if (!default_argv) {
			fprintf(stderr, "semindex: failed to allocate compiler arguments\n");
			return 1;
		}
		default_argv[0] = "cc";
		memcpy(default_argv + 1, compiler_argv, compiler_argc * sizeof(*compiler_argv));
		compiler_argv = default_argv;
		compiler_argc++;
	}
	if (compiler_command_find_source(compiler_argc, compiler_argv, &source_file) < 0) {
		fprintf(stderr, "semindex: unsupported compiler command\n");
		free(default_argv);
		return 1;
	}
	if (index_command_prepare(&options, "compiler", source_file, print_output, "semindex") < 0)
		goto out;

	cmd.directory = ".";
	cmd.file = source_file;
	cmd.argc = compiler_argc;
	cmd.argv = (const char *const *)compiler_argv;

	request = (index_pipeline_request_t){
		.input = INDEX_PIPELINE_COMMAND,
		.storage = index_command_storage(&options, print_output),
		.partial = INDEX_PIPELINE_STORE_PARTIAL,
		.command = &cmd,
		.source_file = source_file,
		.details = print_output,
	};
	index_command_fill_request(&options, &request);

	if (index_pipeline_run(&request, &result) < 0) {
		if (result.failed_stage == INDEX_PIPELINE_STAGE_CREATE)
			fprintf(stderr, "semindex: failed to create indexer\n");
		else if (result.failed_stage == INDEX_PIPELINE_STAGE_REPOSITORY_ROOT)
			fprintf(stderr, "semindex: invalid project root: %s\n", options.repository_root);
		else if (result.failed_stage == INDEX_PIPELINE_STAGE_FRONTEND)
			fprintf(stderr, "semindex: failed to index compiler command for '%s'\n", source_file);
		else if (result.failed_stage == INDEX_PIPELINE_STAGE_FINGERPRINT)
			fprintf(stderr, "semindex: failed to fingerprint '%s'\n", source_file);

		goto out;
	}

	phase_start = semindex_trace_begin(options.trace);
	ret = print_output ? output_index(format, result.index) : 0;
	semindex_trace_end(options.trace, "output", phase_start);
	ret = ret ? 1 : 0;

out:
	index_pipeline_result_destroy(&result, options.trace);
	free(default_argv);

	if (index_command_finish(&options) < 0)
		ret = 1;
	return ret;
}
