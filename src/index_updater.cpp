// SPDX-License-Identifier: GPL-2.0-or-later
#include "index_updater.h"

extern "C" {
#include "command_db.h"
#include "index_pipeline.h"
}

#include <filesystem>
#include <mutex>
#include <system_error>
#include <utility>

namespace
{

std::mutex update_mutex;

void copyDiagnostics(const semindex_t *index, const char *directory, size_t limit, SemindexIndexUpdateResult &result)
{
	if (!index)
		return;

	size_t count = semindex_diagnostic_count(index);

	for (size_t i = 0; i < count; i++) {
		if (limit && result.diagnostics.size() >= limit) {
			result.diagnostics_truncated = true;

			break;
		}

		const semindex_diagnostic_t *diagnostic = semindex_get_diagnostic(index, i);

		if (!diagnostic)
			continue;

		std::filesystem::path path(diagnostic->file);

		if (path.is_relative())
			path = std::filesystem::path(directory) / path;

		result.diagnostics.push_back({
			.severity = diagnostic->severity,
			.message = diagnostic->message,
			.file = path.lexically_normal().string(),
			.line = diagnostic->line,
			.column = diagnostic->column,
		});
	}
}

} // namespace

void SemindexIndexDeleter::operator()(semindex_t *index) const
{
	semindex_destroy(index);
}

SemindexIndexUpdater::SemindexIndexUpdater(SemindexIndexUpdaterOptions options) : options(std::move(options))
{
}

int SemindexIndexUpdater::commandAvailable(const std::string &file, const std::string &variant) const
{
	command_db_command_t *command = nullptr;
	std::error_code error;

	if (!std::filesystem::exists(options.commands_database, error))
		return error ? -1 : 1;

	int ret = command_db_load(options.commands_database.c_str(), variant.c_str(), file.c_str(), &command);

	command_db_command_free(command);

	return ret;
}

SemindexIndexUpdateResult SemindexIndexUpdater::update(const SemindexIndexUpdateRequest &request) const
{
	std::lock_guard<std::mutex> guard(update_mutex);
	command_db_command_t *saved = nullptr;
	const semindex_compile_command_t *command = nullptr;
	index_pipeline_request_t pipeline_request = {};
	index_pipeline_result_t pipeline = {};
	SemindexIndexUpdateResult result;
	std::error_code fs_error;
	std::filesystem::path old_directory;
	bool changed_directory = false;

	if (request.stopped && request.stopped()) {
		result.error = "index update cancelled";

		return result;
	}

	int loaded = command_db_load(options.commands_database.c_str(), request.variant.c_str(), request.file.c_str(),
		&saved);

	if (loaded > 0) {
		result.error =
			"no saved compiler command for '" + request.file + "' in variant '" + request.variant + "'";

		goto out;
	}

	if (loaded < 0) {
		result.error = "failed to load compiler command for '" + request.file + "'";

		goto out;
	}

	result.command_available = true;
	command = command_db_command_get(saved);
	result.directory = command->directory;
	old_directory = std::filesystem::current_path(fs_error);

	if (fs_error) {
		result.error = "failed to read the current directory: " + fs_error.message();

		goto out;
	}

	std::filesystem::current_path(command->directory, fs_error);

	if (fs_error) {
		result.error = "failed to enter compiler directory '" + std::string(command->directory) +
			"': " + fs_error.message();

		goto out;
	}

	changed_directory = true;
	pipeline_request.input = INDEX_PIPELINE_COMMAND;
	pipeline_request.storage = INDEX_PIPELINE_STORE_SYMBOLS;
	pipeline_request.partial = request.store_partial ? INDEX_PIPELINE_STORE_PARTIAL : INDEX_PIPELINE_RETURN_PARTIAL;
	pipeline_request.command = command;
	pipeline_request.source_file = command->file;
	pipeline_request.symbol_database = options.database.c_str();
	pipeline_request.variant = request.variant.c_str();
	pipeline_request.scope = SEMINDEX_SCOPE_PROJECT;
	pipeline_request.include_local = options.include_local;
	pipeline_request.details = 1;

	if (index_pipeline_run(&pipeline_request, &pipeline) < 0) {
		copyDiagnostics(pipeline.index, command->directory, request.diagnostic_limit, result);

		if (pipeline.failed_stage == INDEX_PIPELINE_STAGE_CREATE)
			result.error = "failed to create indexer";
		else if (pipeline.failed_stage == INDEX_PIPELINE_STAGE_FINGERPRINT)
			result.error = "failed to fingerprint '" + request.file + "'";
		else if (pipeline.failed_stage == INDEX_PIPELINE_STAGE_SYMBOL_DATABASE)
			result.error = "failed to store index for '" + request.file + "'";
		else
			result.error = "failed to index '" + request.file + "'";

		goto out;
	}

	copyDiagnostics(pipeline.index, command->directory, request.diagnostic_limit, result);

	if (pipeline.frontend_ret != 0 || pipeline.frontend->status == SEMINDEX_INDEX_PARTIAL) {
		result.status = SemindexIndexUpdateResult::Status::Partial;

		if (!request.store_partial) {
			result.partial_index.reset(pipeline.index);
			pipeline.index = nullptr;
		}

		goto out;
	}

	if (pipeline.persisted != INDEX_PIPELINE_STORE_SYMBOLS) {
		result.error = "index for '" + request.file + "' was not stored";

		goto out;
	}

	result.status = SemindexIndexUpdateResult::Status::Clean;
out:
	index_pipeline_result_destroy(&pipeline, nullptr);
	command_db_command_free(saved);

	if (changed_directory) {
		std::filesystem::current_path(old_directory, fs_error);

		if (fs_error) {
			result.status = SemindexIndexUpdateResult::Status::Failed;
			result.error = "failed to restore the current directory: " + fs_error.message();
			result.partial_index.reset();
		}
	}

	return result;
}
