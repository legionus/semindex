// SPDX-License-Identifier: GPL-2.0-or-later
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "compiler_command.h"
#include "index_command.h"
#include "index_pipeline.h"
#include "semindex_cli.h"
#include "semindex_paths.h"

enum index_error_policy {
	INDEX_ERRORS_WARN,
	INDEX_ERRORS_FAIL,
	INDEX_ERRORS_IGNORE,
};

struct cc_options {
	struct index_command_options index;
	enum index_error_policy error_policy;
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
	       "                             (default: " SEMINDEX_DEFAULT_SYMBOL_DATABASE ")\n"
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
		{ "no-include-local", no_argument, NULL, INDEX_COMMAND_OPT_NO_INCLUDE_LOCAL },
		{ "variant", required_argument, NULL, INDEX_COMMAND_OPT_VARIANT },
		{ "commands-database", required_argument, NULL, INDEX_COMMAND_OPT_COMMANDS_DATABASE },
		{ "no-store-command", no_argument, NULL, INDEX_COMMAND_OPT_NO_STORE_COMMAND },
		{ "trace", required_argument, NULL, INDEX_COMMAND_OPT_TRACE },
		{ "index-errors", required_argument, NULL, 6 },
		{ "root", required_argument, NULL, INDEX_COMMAND_OPT_ROOT },
#ifdef SEMINDEX_HAVE_LIBGIT2
		{ "git-commit", required_argument, NULL, INDEX_COMMAND_OPT_GIT_COMMIT },
		{ "no-git-commit", no_argument, NULL, INDEX_COMMAND_OPT_NO_GIT_COMMIT },
#endif
		{ "database", required_argument, NULL, 'd' },
		{ "scope", required_argument, NULL, 's' },
		{ "help", no_argument, NULL, 'h' },
		{ NULL, 0, NULL, 0 },
	};
	int separator = 0;
	int parsed;
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
		case 6:
			if (parse_error_policy(optarg, &options->error_policy) < 0) {
				fprintf(stderr, "semindex-cc: unknown index error policy: %s\n", optarg);

				return -1;
			}
			break;
		case 'h':
			cc_help();

			return 1;
		default:
			parsed = index_command_parse_option(&options->index, opt, optarg, "semindex-cc");

			if (parsed > 0)
				break;

			if (parsed < 0)
				return -1;

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

static int run_index(struct cc_options *options, int argc, char **argv, const char *source_file)
{
	index_pipeline_request_t request;
	index_pipeline_result_t result = { 0 };
	semindex_compile_command_t command;
	int stderr_copy = -1;
	int null_fd = -1;
	int ret = -1;

	if (options->error_policy == INDEX_ERRORS_IGNORE) {
		stderr_copy = dup(STDERR_FILENO);
		null_fd = open("/dev/null", O_WRONLY);

		if (stderr_copy < 0 || null_fd < 0 || dup2(null_fd, STDERR_FILENO) < 0)
			goto out;
	}

	if (index_command_prepare(&options->index, "cc", source_file, 0, "semindex-cc") < 0)
		goto out;

	command = (semindex_compile_command_t){
		.directory = ".",
		.file = source_file,
		.argc = argc,
		.argv = (const char *const *)argv,
	};
	request = (index_pipeline_request_t){
		.input = INDEX_PIPELINE_COMMAND,
		.storage = index_command_storage(&options->index, 0),
		.partial = INDEX_PIPELINE_STORE_PARTIAL,
		.command = &command,
		.source_file = source_file,
	};
	index_command_fill_request(&options->index, &request);

	ret = index_pipeline_run(&request, &result);

	if (ret < 0 && result.failed_stage == INDEX_PIPELINE_STAGE_REPOSITORY_ROOT)
		fprintf(stderr, "semindex-cc: invalid project root: %s\n", options->index.repository_root);

out:
	index_pipeline_result_destroy(&result, options->index.trace);

	if (index_command_finish(&options->index) < 0)
		ret = -1;

	if (stderr_copy >= 0) {
		if (dup2(stderr_copy, STDERR_FILENO) < 0)
			ret = -1;
	}

	if (null_fd >= 0)
		close(null_fd);

	if (stderr_copy >= 0)
		close(stderr_copy);

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
	struct cc_options options;
	const char *real_cc;
	const char *source_file = NULL;
	char **compiler_argv;
	char **allocated_argv = NULL;
	int compiler_argc;
	int compiler_index;
	int parse_ret;
	int index_ret;
	const char *value;

	index_command_options_init(&options.index);
	options.error_policy = INDEX_ERRORS_WARN;

	if (getenv("SEMINDEX_CC_ACTIVE")) {
		fprintf(stderr, "semindex-cc: recursive compiler invocation\n");

		return 127;
	}

	value = getenv("SEMINDEX_DATABASE");

	if (value && value[0])
		options.index.database = value;

	value = getenv("SEMINDEX_COMMANDS_DATABASE");

	if (value && value[0])
		options.index.commands_database = value;

	value = getenv("SEMINDEX_VARIANT");

	if (value)
		options.index.variant = value;

	value = getenv("SEMINDEX_INDEX_ERRORS");

	if (value && parse_error_policy(value, &options.error_policy) < 0) {
		fprintf(stderr, "semindex-cc: unknown index error policy: %s\n", value);

		return 1;
	}

	parse_ret = parse_options(argc, argv, &options, &compiler_index);

	if (parse_ret != 0)
		return parse_ret < 0;

	if (!options.index.variant[0]) {
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
