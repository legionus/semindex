// SPDX-License-Identifier: GPL-2.0-or-later
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "command_db.h"
#include "compiler_command.h"
#include "index_pipeline.h"
#include "perf_trace.h"
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
		{ "no-include-local", no_argument, NULL, 1 },
		{ "variant", required_argument, NULL, 2 },
		{ "commands-database", required_argument, NULL, 3 },
		{ "no-store-command", no_argument, NULL, 4 },
		{ "trace", required_argument, NULL, 5 },
		{ "root", required_argument, NULL, 8 },
#ifdef SEMINDEX_HAVE_LIBGIT2
		{ "git-commit", required_argument, NULL, 6 },
		{ "no-git-commit", no_argument, NULL, 7 },
#endif
		{ "database", required_argument, NULL, 'd' },
		{ "format", required_argument, NULL, 'f' },
		{ "scope", required_argument, NULL, 's' },
		{ "help", no_argument, NULL, 'h' },
		{ NULL, 0, NULL, 0 },
	};
	enum output_format format = FORMAT_DISSECT;
	semindex_scope_t scope = SEMINDEX_SCOPE_PROJECT;
	const char *database = ".semindex/semindex.db";
	const char *commands_database = NULL;
	const char *variant = "general";
	const char *source_file = NULL;
	const char *trace_path = NULL;
	const char *repository_root = NULL;
	const char *git_commit = NULL;

	semindex_trace_t *trace = NULL;
	semindex_trace_time_t phase_start;
	semindex_trace_time_t total_start = 0;

	index_pipeline_request_t request;
	index_pipeline_result_t result = { 0 };
	index_pipeline_git_commit_t git_commit_mode = INDEX_PIPELINE_GIT_COMMIT_DISABLED;
	index_pipeline_storage_t storage;
	semindex_compile_command_t cmd;
	int compiler_argc;

	char **compiler_argv;
	char **default_argv = NULL;
	char *default_commands_database = NULL;
	int ret = 1;
	int print_output = 0;
	int include_local = 1;
	int store_command = 1;
	int opt;

	optind = 1;

	while ((opt = getopt_long(argc, argv, "+d:f:s:h", long_options, NULL)) != -1) {
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
		case 6:
			if (parse_git_commit(optarg, &git_commit_mode) < 0) {
				fprintf(stderr, "semindex: invalid Git commit: %s\n", optarg);

				return 1;
			}

			git_commit = git_commit_mode == INDEX_PIPELINE_GIT_COMMIT_EXPLICIT ? optarg : NULL;
			break;
		case 7:
			git_commit_mode = INDEX_PIPELINE_GIT_COMMIT_DISABLED;
			git_commit = NULL;
			break;
		case 8:
			repository_root = optarg;
			break;
		case 'd':
			database = optarg;
			break;
		case 'f':
			if (parse_format(optarg, &format) < 0) {
				fprintf(stderr, "semindex: unknown format: %s\n", optarg);
				return 1;
			}
			print_output = 1;
			break;
		case 's':
			if (parse_scope(optarg, &scope) < 0) {
				fprintf(stderr, "semindex: unknown scope: %s\n", optarg);
				return 1;
			}
			break;
		case 'h':
			compiler_help();
			return 0;

		default:
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
	if (!variant[0]) {
		fprintf(stderr, "semindex: variant name must not be empty\n");
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
	if (!print_output && store_command && !commands_database) {
		default_commands_database = command_db_default_path(database);

		if (!default_commands_database) {
			fprintf(stderr, "semindex: failed to allocate command database path\n");
			free(default_argv);
			return 1;
		}
		commands_database = default_commands_database;
	}

	if (trace_path) {
		trace = semindex_trace_open(trace_path, "compiler", source_file);

		if (!trace)
			goto out;

		total_start = semindex_trace_begin(trace);
	}

	cmd.directory = ".";
	cmd.file = source_file;
	cmd.argc = compiler_argc;
	cmd.argv = (const char *const *)compiler_argv;

	if (print_output)
		storage = INDEX_PIPELINE_OUTPUT_ONLY;
	else if (store_command)
		storage = INDEX_PIPELINE_STORE_SYMBOLS_AND_COMMAND;
	else
		storage = INDEX_PIPELINE_STORE_SYMBOLS;

	request = (index_pipeline_request_t){
		.input = INDEX_PIPELINE_COMMAND,
		.storage = storage,
		.partial = INDEX_PIPELINE_STORE_PARTIAL,
		.command = &cmd,
		.source_file = source_file,
		.symbol_database = database,
		.commands_database = commands_database,
		.variant = variant,
		.repository_root = repository_root,
		.git_commit = git_commit,
		.scope = scope,
		.git_commit_mode = git_commit_mode,
		.trace = trace,
		.include_local = include_local,
		.details = print_output,
	};

	if (index_pipeline_run(&request, &result) < 0) {
		if (result.failed_stage == INDEX_PIPELINE_STAGE_CREATE)
			fprintf(stderr, "semindex: failed to create indexer\n");
		else if (result.failed_stage == INDEX_PIPELINE_STAGE_REPOSITORY_ROOT)
			fprintf(stderr, "semindex: invalid project root: %s\n", repository_root);
		else if (result.failed_stage == INDEX_PIPELINE_STAGE_FRONTEND)
			fprintf(stderr, "semindex: failed to index compiler command for '%s'\n", source_file);
		else if (result.failed_stage == INDEX_PIPELINE_STAGE_FINGERPRINT)
			fprintf(stderr, "semindex: failed to fingerprint '%s'\n", source_file);

		goto out;
	}

	phase_start = semindex_trace_begin(trace);
	ret = print_output ? output_index(format, result.index) : 0;
	semindex_trace_end(trace, "output", phase_start);
	ret = ret ? 1 : 0;

out:
	index_pipeline_result_destroy(&result, trace);
	free(default_commands_database);
	free(default_argv);
	semindex_trace_end(trace, "total", total_start);

	if (semindex_trace_close(trace) < 0)
		ret = 1;
	return ret;
}
