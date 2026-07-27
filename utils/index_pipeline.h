// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef SEMINDEX_INDEX_PIPELINE_H
#define SEMINDEX_INDEX_PIPELINE_H

#include "perf_trace.h"
#include "semindex.h"

typedef enum {
	INDEX_PIPELINE_COMMAND,
	INDEX_PIPELINE_COMPILE_COMMANDS,
} index_pipeline_input_t;

typedef enum {
	INDEX_PIPELINE_OUTPUT_ONLY,
	INDEX_PIPELINE_STORE_SYMBOLS,
	INDEX_PIPELINE_STORE_SYMBOLS_AND_COMMAND,
} index_pipeline_storage_t;

typedef enum {
	INDEX_PIPELINE_STORE_PARTIAL,
	INDEX_PIPELINE_RETURN_PARTIAL,
} index_pipeline_partial_t;

typedef enum {
	INDEX_PIPELINE_GIT_COMMIT_DISABLED,
	INDEX_PIPELINE_GIT_COMMIT_AUTO,
	INDEX_PIPELINE_GIT_COMMIT_EXPLICIT,
} index_pipeline_git_commit_t;

typedef enum {
	INDEX_PIPELINE_STAGE_NONE,
	INDEX_PIPELINE_STAGE_CREATE,
	INDEX_PIPELINE_STAGE_FRONTEND,
	INDEX_PIPELINE_STAGE_FINGERPRINT,
	INDEX_PIPELINE_STAGE_SYMBOL_DATABASE,
	INDEX_PIPELINE_STAGE_COMMAND_DATABASE,
} index_pipeline_stage_t;

typedef struct {
	index_pipeline_input_t input;
	index_pipeline_storage_t storage;
	index_pipeline_partial_t partial;
	const semindex_compile_command_t *command;
	const char *compile_commands;
	const char *source_file;
	const char *symbol_database;
	const char *commands_database;
	const char *variant;
	const char *git_commit;
	semindex_scope_t scope;
	index_pipeline_git_commit_t git_commit_mode;
	semindex_trace_t *trace;
	int include_local;
	int details;
} index_pipeline_request_t;

typedef struct {
	semindex_t *index;
	const semindex_index_result_t *frontend;
	const semindex_compile_command_t *command;
	index_pipeline_stage_t failed_stage;
	index_pipeline_storage_t persisted;
	int frontend_ret;
} index_pipeline_result_t;

int index_pipeline_run(const index_pipeline_request_t *request, index_pipeline_result_t *result);
void index_pipeline_result_destroy(index_pipeline_result_t *result, semindex_trace_t *trace);

#endif /* SEMINDEX_INDEX_PIPELINE_H */
