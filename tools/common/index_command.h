// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "index_pipeline.h"

enum index_command_option {
	INDEX_COMMAND_OPT_NO_INCLUDE_LOCAL = 1000,
	INDEX_COMMAND_OPT_VARIANT,
	INDEX_COMMAND_OPT_COMMANDS_DATABASE,
	INDEX_COMMAND_OPT_NO_STORE_COMMAND,
	INDEX_COMMAND_OPT_TRACE,
	INDEX_COMMAND_OPT_GIT_COMMIT,
	INDEX_COMMAND_OPT_NO_GIT_COMMIT,
	INDEX_COMMAND_OPT_ROOT,
};

struct index_command_options {
	const char *database;
	const char *commands_database;
	const char *variant;
	const char *repository_root;
	const char *trace_path;
	const char *git_commit;
	semindex_scope_t scope;
	index_pipeline_git_commit_t git_commit_mode;
	semindex_trace_t *trace;
	semindex_trace_time_t total_start;
	char *allocated_commands_database;
	int include_local;
	int store_command;
};

void index_command_options_init(struct index_command_options *options);
int index_command_parse_option(struct index_command_options *options, int option, const char *argument,
	const char *program);
int index_command_prepare(struct index_command_options *options, const char *command, const char *source_file,
	int output_only, const char *program);
void index_command_fill_request(const struct index_command_options *options, index_pipeline_request_t *request);
index_pipeline_storage_t index_command_storage(const struct index_command_options *options, int output_only);
int index_command_finish(struct index_command_options *options);
