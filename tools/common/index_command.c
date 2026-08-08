// SPDX-License-Identifier: GPL-2.0-or-later
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "command_db.h"
#include "index_command.h"
#include "repository.h"
#include "semindex_cli.h"
#include "semindex_paths.h"

void index_command_options_init(struct index_command_options *options)
{
	*options = (struct index_command_options){
		.variant = "general",
		.scope = SEMINDEX_SCOPE_PROJECT,
		.git_commit_mode = INDEX_PIPELINE_GIT_COMMIT_DISABLED,
		.include_local = 1,
		.store_command = 1,
	};
}

int index_command_parse_option(struct index_command_options *options, int option, const char *argument,
	const char *program)
{
	switch (option) {
	case INDEX_COMMAND_OPT_NO_INCLUDE_LOCAL:
		options->include_local = 0;
		break;
	case INDEX_COMMAND_OPT_VARIANT:
		options->variant = argument;
		break;
	case INDEX_COMMAND_OPT_COMMANDS_DATABASE:
		options->commands_database = argument;
		break;
	case INDEX_COMMAND_OPT_NO_STORE_COMMAND:
		options->store_command = 0;
		break;
	case INDEX_COMMAND_OPT_TRACE:
		options->trace_path = argument;
		break;
	case INDEX_COMMAND_OPT_GIT_COMMIT:
		if (parse_git_commit(argument, &options->git_commit_mode) < 0) {
			fprintf(stderr, "%s: invalid Git commit: %s\n", program, argument);

			return -1;
		}

		options->git_commit = options->git_commit_mode == INDEX_PIPELINE_GIT_COMMIT_EXPLICIT ? argument : NULL;
		break;
	case INDEX_COMMAND_OPT_NO_GIT_COMMIT:
		options->git_commit_mode = INDEX_PIPELINE_GIT_COMMIT_DISABLED;
		options->git_commit = NULL;
		break;
	case INDEX_COMMAND_OPT_ROOT:
		options->repository_root = argument;
		break;
	case 'd':
		options->database = argument;
		break;
	case 's':
		if (parse_scope(argument, &options->scope) < 0) {
			fprintf(stderr, "%s: unknown scope: %s\n", program, argument);

			return -1;
		}
		break;
	default:
		return 0;
	}

	return 1;
}

int index_command_prepare(struct index_command_options *options, const char *command, const char *source_file,
	int output_only, const char *program)
{
	if (!options->database) {
		options->allocated_database = semindex_default_database_path(source_file, SEMINDEX_SYMBOL_DATABASE);

		if (!options->allocated_database) {
			fprintf(stderr, "%s: failed to allocate symbol database path\n", program);

			return -1;
		}

		options->database = options->allocated_database;
	}

	if (!options->variant[0]) {
		fprintf(stderr, "%s: variant name must not be empty\n", program);

		return -1;
	}

	if (!output_only && options->store_command && !options->commands_database) {
		options->allocated_commands_database = command_db_default_path(options->database);

		if (!options->allocated_commands_database) {
			fprintf(stderr, "%s: failed to allocate command database path\n", program);

			return -1;
		}

		options->commands_database = options->allocated_commands_database;
	}

	if (!options->trace_path)
		return 0;

	options->trace = semindex_trace_open(options->trace_path, command, source_file);

	if (!options->trace)
		return -1;

	options->total_start = semindex_trace_begin(options->trace);

	return 0;
}

void index_command_fill_request(const struct index_command_options *options, index_pipeline_request_t *request)
{
	request->symbol_database = options->database;
	request->commands_database = options->commands_database;
	request->variant = options->variant;
	request->repository_root = options->repository_root;
	request->git_commit = options->git_commit;
	request->scope = options->scope;
	request->git_commit_mode = options->git_commit_mode;
	request->trace = options->trace;
	request->include_local = options->include_local;
}

index_pipeline_storage_t index_command_storage(const struct index_command_options *options, int output_only)
{
	if (output_only)
		return INDEX_PIPELINE_OUTPUT_ONLY;

	if (options->store_command)
		return INDEX_PIPELINE_STORE_SYMBOLS_AND_COMMAND;

	return INDEX_PIPELINE_STORE_SYMBOLS;
}

int index_command_finish(struct index_command_options *options)
{
	int ret;

	semindex_trace_end(options->trace, "total", options->total_start);
	ret = semindex_trace_close(options->trace);
	free(options->allocated_database);
	free(options->allocated_commands_database);

	return ret;
}
