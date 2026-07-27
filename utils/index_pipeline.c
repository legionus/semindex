// SPDX-License-Identifier: GPL-2.0-or-later
#include <stdlib.h>
#include <string.h>

#include "command_db.h"
#include "git_provenance.h"
#include "index_db.h"
#include "index_pipeline.h"
#include "repository.h"

static int run_frontend(const index_pipeline_request_t *request, semindex_t *index)
{
	if (request->input == INDEX_PIPELINE_COMMAND)
		return semindex_index_command(index, request->command);

	return semindex_index_file(index, request->compile_commands, request->source_file);
}

static int store_command(const index_pipeline_request_t *request, const semindex_compile_command_t *command)
{
	if (!command)
		return -1;

	return command_db_store(request->commands_database, request->variant, command->directory, command->file,
		command->argc, command->argv);
}

int index_pipeline_run(const index_pipeline_request_t *request, index_pipeline_result_t *result)
{
	char *repository_root = NULL;
	const char *git_commit = NULL;
	index_db_store_request_t store_request;
	semindex_trace_time_t phase_start;
	int repository_root_owned_by_git = 0;
	int ret = -1;

	semindex_git_provenance_t git_provenance = { 0 };

	memset(result, 0, sizeof(*result));
	result->index = semindex_create();

	if (!result->index) {
		result->failed_stage = INDEX_PIPELINE_STAGE_CREATE;

		return -1;
	}

	semindex_set_scope(result->index, request->scope);
	semindex_set_details(result->index, request->details);
	semindex_set_include_local(result->index, request->include_local);

	phase_start = semindex_trace_begin(request->trace);
	result->frontend_ret = run_frontend(request, result->index);
	result->frontend = semindex_get_index_result(result->index);
	semindex_trace_end(request->trace, "parse", phase_start);

	if (!result->frontend || result->frontend->status == SEMINDEX_INDEX_FAILED) {
		result->failed_stage = INDEX_PIPELINE_STAGE_FRONTEND;

		return -1;
	}

	if (request->input == INDEX_PIPELINE_COMMAND)
		result->command = request->command;
	else
		result->command = semindex_get_compile_command(result->index);

	if (request->partial == INDEX_PIPELINE_RETURN_PARTIAL) {
		if (result->frontend_ret != 0)
			return 0;

		if (result->frontend->status == SEMINDEX_INDEX_PARTIAL)
			return 0;
	}

	if (request->storage == INDEX_PIPELINE_OUTPUT_ONLY)
		return 0;

	phase_start = semindex_trace_begin(request->trace);

	if (semindex_build_file_fingerprints(result->index) < 0) {
		semindex_trace_end(request->trace, "fingerprint", phase_start);
		result->failed_stage = INDEX_PIPELINE_STAGE_FINGERPRINT;

		return -1;
	}
	semindex_trace_end(request->trace, "fingerprint", phase_start);

	phase_start = semindex_trace_begin(request->trace);

	if (request->git_commit_mode == INDEX_PIPELINE_GIT_COMMIT_AUTO) {
		semindex_git_provenance(request->source_file, &git_provenance);
		repository_root = git_provenance.data.repository_root;
		repository_root_owned_by_git = repository_root != NULL;
	} else
		repository_root = semindex_repository_root(request->source_file);

	if (!repository_root)
		repository_root = semindex_repository_root(request->source_file);

	if (request->git_commit_mode == INDEX_PIPELINE_GIT_COMMIT_EXPLICIT)
		git_commit = request->git_commit;
	else if (request->git_commit_mode == INDEX_PIPELINE_GIT_COMMIT_AUTO)
		git_commit = git_provenance.data.commit;

	semindex_trace_end(request->trace, "provenance", phase_start);

	store_request = (index_db_store_request_t){
		.path = request->symbol_database,
		.index = result->index,
		.main_file = request->source_file,
		.variant = request->variant,
		.provenance = {
			.repository_root = repository_root,
			.git_commit = git_commit,
		},
		.trace = request->trace,
		.include_local = request->include_local,
	};

	phase_start = semindex_trace_begin(request->trace);

	if (index_db_store(&store_request) < 0) {
		semindex_trace_end(request->trace, "symbol_database", phase_start);
		result->failed_stage = INDEX_PIPELINE_STAGE_SYMBOL_DATABASE;

		goto out;
	}
	semindex_trace_end(request->trace, "symbol_database", phase_start);
	result->persisted = INDEX_PIPELINE_STORE_SYMBOLS;

	if (request->storage != INDEX_PIPELINE_STORE_SYMBOLS_AND_COMMAND) {
		ret = 0;

		goto out;
	}

	phase_start = semindex_trace_begin(request->trace);

	if (store_command(request, result->command) < 0) {
		semindex_trace_end(request->trace, "command_database", phase_start);
		result->failed_stage = INDEX_PIPELINE_STAGE_COMMAND_DATABASE;

		goto out;
	}
	semindex_trace_end(request->trace, "command_database", phase_start);
	result->persisted = INDEX_PIPELINE_STORE_SYMBOLS_AND_COMMAND;
	ret = 0;
out:
	semindex_git_provenance_destroy(&git_provenance);

	if (!repository_root_owned_by_git)
		free(repository_root);

	return ret;
}

void index_pipeline_result_destroy(index_pipeline_result_t *result, semindex_trace_t *trace)
{
	semindex_trace_time_t phase_start;

	phase_start = semindex_trace_begin(trace);
	semindex_destroy(result->index);
	memset(result, 0, sizeof(*result));
	semindex_trace_end(trace, "cleanup", phase_start);
}
