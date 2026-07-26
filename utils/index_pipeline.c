// SPDX-License-Identifier: GPL-2.0-or-later
#include <string.h>

#include "command_db.h"
#include "index_db.h"
#include "index_pipeline.h"

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
	semindex_trace_time_t phase_start;

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

	if (index_db_store(request->symbol_database, result->index, request->source_file, request->variant,
		    request->include_local, request->trace) < 0) {
		semindex_trace_end(request->trace, "symbol_database", phase_start);
		result->failed_stage = INDEX_PIPELINE_STAGE_SYMBOL_DATABASE;

		return -1;
	}
	semindex_trace_end(request->trace, "symbol_database", phase_start);
	result->persisted = INDEX_PIPELINE_STORE_SYMBOLS;

	if (request->storage != INDEX_PIPELINE_STORE_SYMBOLS_AND_COMMAND)
		return 0;

	phase_start = semindex_trace_begin(request->trace);

	if (store_command(request, result->command) < 0) {
		semindex_trace_end(request->trace, "command_database", phase_start);
		result->failed_stage = INDEX_PIPELINE_STAGE_COMMAND_DATABASE;

		return -1;
	}
	semindex_trace_end(request->trace, "command_database", phase_start);
	result->persisted = INDEX_PIPELINE_STORE_SYMBOLS_AND_COMMAND;

	return 0;
}

void index_pipeline_result_destroy(index_pipeline_result_t *result, semindex_trace_t *trace)
{
	semindex_trace_time_t phase_start;

	phase_start = semindex_trace_begin(trace);
	semindex_destroy(result->index);
	memset(result, 0, sizeof(*result));
	semindex_trace_end(trace, "cleanup", phase_start);
}
