// SPDX-License-Identifier: GPL-2.0-or-later
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "command_db.h"
#include "compiler_command.h"
#include "index_pipeline.h"
#include "perf_trace.h"
#include "semindex_cli.h"

enum index_error_policy {
	INDEX_ERRORS_WARN,
	INDEX_ERRORS_FAIL,
	INDEX_ERRORS_IGNORE,
};

struct cc_options {
	const char *database;
	const char *commands_database;
	const char *variant;
	const char *repository_root;
	const char *trace_path;
	const char *git_commit;
	semindex_scope_t scope;
	index_pipeline_git_commit_t git_commit_mode;
	enum index_error_policy error_policy;
	int include_local;
	int store_command;
};

static void cc_usage(FILE *f)
{
	fprintf(f, "Usage: semindex cc [OPTION]... -- [COMPILER] ARG...\n");
}

static void cc_help(void)
{
	cc_usage(stdout);
	printf("\n"
	       "Index eligible compiler commands and then replace the process with\n"
	       "the real compiler. With REAL_CC set, COMPILER may be omitted.\n"
	       "Without REAL_CC, the first argument is used as a compiler launcher.\n"
	       "\n"
	       "Options:\n"
	       "  -s, --scope=SCOPE          select indexed source scope: file, project, all\n"
	       "                             (default: project)\n"
	       "  -d, --database=PATH        path to the semindex database\n"
	       "                             (default: .semindex/semindex.db)\n"
	       "      --commands-database=PATH\n"
	       "                             path to the compiler command database\n"
	       "      --variant=NAME          store records in the named variant\n"
	       "      --root=DIR              project root for stored source paths\n"
	       "      --index-errors=POLICY   indexing failure policy: warn, fail, ignore\n"
	       "                             (default: warn)\n");
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
	       "Wrapper options are recognized only before '--'. Compiler arguments\n"
	       "without '--' are passed through unchanged.\n"
	       "\n"
	       "Report bugs to authors.\n\n");
}

static int parse_error_policy(const char *value, enum index_error_policy *policy)
{
	if (!strcmp(value, "warn"))
		*policy = INDEX_ERRORS_WARN;
	else if (!strcmp(value, "fail"))
		*policy = INDEX_ERRORS_FAIL;
	else if (!strcmp(value, "ignore"))
		*policy = INDEX_ERRORS_IGNORE;
	else
		return -1;

	return 0;
}

static int parse_options(int argc, char **argv, struct cc_options *options, int *compiler_index)
{
	static const struct option long_options[] = {
		{ "no-include-local", no_argument, NULL, 1 },
		{ "variant", required_argument, NULL, 2 },
		{ "commands-database", required_argument, NULL, 3 },
		{ "no-store-command", no_argument, NULL, 4 },
		{ "trace", required_argument, NULL, 5 },
		{ "index-errors", required_argument, NULL, 6 },
		{ "root", required_argument, NULL, 9 },
#ifdef SEMINDEX_HAVE_LIBGIT2
		{ "git-commit", required_argument, NULL, 7 },
		{ "no-git-commit", no_argument, NULL, 8 },
#endif
		{ "database", required_argument, NULL, 'd' },
		{ "scope", required_argument, NULL, 's' },
		{ "help", no_argument, NULL, 'h' },
		{ NULL, 0, NULL, 0 },
	};
	int separator = 0;
	int i;
	int opt;

	if (argc == 2 && !strcmp(argv[1], "--help")) {
		cc_help();

		return 1;
	}

	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--")) {
			separator = i;
			break;
		}
	}

	/* Raw CC and launcher invocations must not consume compiler options. */
	if (!separator) {
		*compiler_index = 1;

		return 0;
	}

	optind = 1;

	while ((opt = getopt_long(argc, argv, "+d:s:h", long_options, NULL)) != -1) {
		switch (opt) {
		case 1:
			options->include_local = 0;
			break;
		case 2:
			options->variant = optarg;
			break;
		case 3:
			options->commands_database = optarg;
			break;
		case 4:
			options->store_command = 0;
			break;
		case 5:
			options->trace_path = optarg;
			break;
		case 6:
			if (parse_error_policy(optarg, &options->error_policy) < 0) {
				fprintf(stderr, "semindex-cc: unknown index error policy: %s\n", optarg);

				return -1;
			}
			break;
		case 9:
			options->repository_root = optarg;
			break;
#ifdef SEMINDEX_HAVE_LIBGIT2
		case 7:
			if (parse_git_commit(optarg, &options->git_commit_mode) < 0) {
				fprintf(stderr, "semindex-cc: invalid Git commit: %s\n", optarg);

				return -1;
			}

			options->git_commit =
				options->git_commit_mode == INDEX_PIPELINE_GIT_COMMIT_EXPLICIT ? optarg : NULL;
			break;
		case 8:
			options->git_commit_mode = INDEX_PIPELINE_GIT_COMMIT_DISABLED;
			options->git_commit = NULL;
			break;
#endif
		case 'd':
			options->database = optarg;
			break;
		case 's':
			if (parse_scope(optarg, &options->scope) < 0) {
				fprintf(stderr, "semindex-cc: unknown scope: %s\n", optarg);

				return -1;
			}
			break;
		case 'h':
			cc_help();

			return 1;
		default:
			cc_usage(stderr);

			return -1;
		}
	}

	*compiler_index = optind;

	if (*compiler_index >= argc) {
		cc_usage(stderr);

		return -1;
	}

	return 0;
}

static int run_index(const struct cc_options *options, int argc, char **argv, const char *source_file)
{
	index_pipeline_request_t request;
	index_pipeline_result_t result = { 0 };
	index_pipeline_storage_t storage;
	semindex_compile_command_t command;
	semindex_trace_t *trace = NULL;
	semindex_trace_time_t total_start = 0;
	const char *commands_database = options->commands_database;
	char *allocated_commands_database = NULL;
	int stderr_copy = -1;
	int null_fd = -1;
	int ret = -1;

	if (options->store_command) {
		if (!commands_database) {
			allocated_commands_database = command_db_default_path(options->database);
			commands_database = allocated_commands_database;
		}

		if (!commands_database)
			goto out;
	}

	if (options->error_policy == INDEX_ERRORS_IGNORE) {
		stderr_copy = dup(STDERR_FILENO);
		null_fd = open("/dev/null", O_WRONLY);

		if (stderr_copy < 0 || null_fd < 0 || dup2(null_fd, STDERR_FILENO) < 0)
			goto out;
	}

	if (options->trace_path) {
		trace = semindex_trace_open(options->trace_path, "cc", source_file);

		if (!trace)
			goto out;

		total_start = semindex_trace_begin(trace);
	}

	command = (semindex_compile_command_t){
		.directory = ".",
		.file = source_file,
		.argc = argc,
		.argv = (const char *const *)argv,
	};
	storage = options->store_command ? INDEX_PIPELINE_STORE_SYMBOLS_AND_COMMAND : INDEX_PIPELINE_STORE_SYMBOLS;
	request = (index_pipeline_request_t){
		.input = INDEX_PIPELINE_COMMAND,
		.storage = storage,
		.partial = INDEX_PIPELINE_STORE_PARTIAL,
		.command = &command,
		.source_file = source_file,
		.symbol_database = options->database,
		.commands_database = commands_database,
		.variant = options->variant,
		.repository_root = options->repository_root,
		.git_commit = options->git_commit,
		.scope = options->scope,
		.git_commit_mode = options->git_commit_mode,
		.trace = trace,
		.include_local = options->include_local,
	};

	ret = index_pipeline_run(&request, &result);

	if (ret < 0 && result.failed_stage == INDEX_PIPELINE_STAGE_REPOSITORY_ROOT)
		fprintf(stderr, "semindex-cc: invalid project root: %s\n", options->repository_root);

out:
	index_pipeline_result_destroy(&result, trace);
	semindex_trace_end(trace, "total", total_start);

	if (semindex_trace_close(trace) < 0)
		ret = -1;

	if (stderr_copy >= 0) {
		if (dup2(stderr_copy, STDERR_FILENO) < 0)
			ret = -1;
	}

	if (null_fd >= 0)
		close(null_fd);

	if (stderr_copy >= 0)
		close(stderr_copy);

	free(allocated_commands_database);

	return ret;
}

static int exec_compiler(char **argv)
{
	int error;

	if (setenv("SEMINDEX_CC_ACTIVE", "1", 1) < 0) {
		fprintf(stderr, "semindex-cc: failed to set recursion guard: %s\n", strerror(errno));

		return 1;
	}

	execvp(argv[0], argv);
	error = errno;
	fprintf(stderr, "semindex-cc: failed to execute %s: %s\n", argv[0], strerror(error));

	return error == ENOENT ? 127 : 1;
}

int cmd_cc(int argc, char **argv)
{
	struct cc_options options = {
		.database = ".semindex/semindex.db",
		.variant = "general",
		.scope = SEMINDEX_SCOPE_PROJECT,
		.error_policy = INDEX_ERRORS_WARN,
		.include_local = 1,
		.store_command = 1,
	};
	const char *real_cc;
	const char *source_file = NULL;
	char **compiler_argv;
	char **allocated_argv = NULL;
	int compiler_argc;
	int compiler_index;
	int parse_ret;
	int index_ret;
	const char *value;

	if (getenv("SEMINDEX_CC_ACTIVE")) {
		fprintf(stderr, "semindex-cc: recursive compiler invocation\n");

		return 127;
	}

	value = getenv("SEMINDEX_DATABASE");

	if (value && value[0])
		options.database = value;

	value = getenv("SEMINDEX_COMMANDS_DATABASE");

	if (value && value[0])
		options.commands_database = value;

	value = getenv("SEMINDEX_VARIANT");

	if (value)
		options.variant = value;

	value = getenv("SEMINDEX_INDEX_ERRORS");

	if (value && parse_error_policy(value, &options.error_policy) < 0) {
		fprintf(stderr, "semindex-cc: unknown index error policy: %s\n", value);

		return 1;
	}

	parse_ret = parse_options(argc, argv, &options, &compiler_index);

	if (parse_ret != 0)
		return parse_ret < 0;

	if (!options.variant[0]) {
		fprintf(stderr, "semindex-cc: variant name must not be empty\n");

		return 1;
	}

	compiler_argc = argc - compiler_index;
	compiler_argv = argv + compiler_index;
	real_cc = getenv("REAL_CC");

	if (!compiler_argc || compiler_command_driver_is_omitted(compiler_argv[0])) {
		if (!real_cc || !real_cc[0])
			real_cc = "cc";

		allocated_argv = calloc(compiler_argc + 2, sizeof(*allocated_argv));

		if (!allocated_argv) {
			fprintf(stderr, "semindex-cc: failed to allocate compiler arguments\n");

			return 1;
		}

		allocated_argv[0] = (char *)real_cc;
		memcpy(allocated_argv + 1, compiler_argv, compiler_argc * sizeof(*compiler_argv));
		compiler_argv = allocated_argv;
		compiler_argc++;
	}

	if (compiler_command_find_source(compiler_argc, compiler_argv, &source_file) == 0) {
		index_ret = run_index(&options, compiler_argc, compiler_argv, source_file);

		if (index_ret < 0 && options.error_policy == INDEX_ERRORS_FAIL) {
			free(allocated_argv);

			return 1;
		}

		if (index_ret < 0 && options.error_policy == INDEX_ERRORS_WARN)
			fprintf(stderr, "semindex-cc: failed to index '%s'; continuing\n", source_file);
	}

	return exec_compiler(compiler_argv);
}
