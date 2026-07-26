// SPDX-License-Identifier: GPL-2.0-or-later
#include "lsp_indexer.h"

extern "C" {
#include "command_db.h"
#include "index_pipeline.h"
}

#include <filesystem>
#include <system_error>
#include <utility>

LspIndexer::LspIndexer(std::string database, std::string commands_database, std::string variant, bool include_local)
    : database(std::move(database)), commands_database(std::move(commands_database)), variant(std::move(variant)),
      include_local(include_local)
{
}

static void copyDiagnostics(const semindex_t *index, const char *directory, LspIndexResult &result)
{
	if (!index)
		return;

	for (size_t i = 0; i < semindex_diagnostic_count(index); i++) {
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

LspIndexResult LspIndexer::update(const std::string &file)
{
	command_db_command_t *saved = nullptr;
	const semindex_compile_command_t *command = nullptr;

	index_pipeline_request_t request = {};
	index_pipeline_result_t pipeline = {};
	LspIndexResult result;
	std::error_code fs_error;
	std::filesystem::path old_directory;
	int loaded;
	bool changed_directory = false;

	loaded = command_db_load(commands_database.c_str(), variant.c_str(), file.c_str(), &saved);

	if (loaded > 0) {
		result.error = "no saved compiler command for '" + file + "' in variant '" + variant + "'";
		goto out;
	}
	if (loaded < 0) {
		result.error = "failed to load compiler command for '" + file + "'";
		goto out;
	}

	command = command_db_command_get(saved);
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

	request.input = INDEX_PIPELINE_COMMAND;
	request.storage = INDEX_PIPELINE_STORE_SYMBOLS;
	request.partial = INDEX_PIPELINE_RETURN_PARTIAL;
	request.command = command;
	request.source_file = command->file;
	request.symbol_database = database.c_str();
	request.variant = variant.c_str();
	request.scope = SEMINDEX_SCOPE_PROJECT;
	request.include_local = include_local;
	request.details = 1;

	if (index_pipeline_run(&request, &pipeline) < 0) {
		copyDiagnostics(pipeline.index, command->directory, result);

		if (pipeline.failed_stage == INDEX_PIPELINE_STAGE_CREATE)
			result.error = "failed to create indexer";
		else if (pipeline.failed_stage == INDEX_PIPELINE_STAGE_FINGERPRINT)
			result.error = "failed to fingerprint '" + file + "'";
		else if (pipeline.failed_stage == INDEX_PIPELINE_STAGE_SYMBOL_DATABASE)
			result.error = "failed to store index for '" + file + "'";
		else
			result.error = "failed to index '" + file + "'";

		overlays.erase(file);
		goto out;
	}

	copyDiagnostics(pipeline.index, command->directory, result);

	if (pipeline.frontend_ret != 0 || pipeline.frontend->status == SEMINDEX_INDEX_PARTIAL) {
		result.status = LspIndexResult::Status::Partial;
		overlays.replace(file, command->directory, pipeline.index);
		goto out;
	}

	if (pipeline.persisted != INDEX_PIPELINE_STORE_SYMBOLS) {
		result.error = "index for '" + file + "' was not stored";
		goto out;
	}

	overlays.erase(file);
	result.status = LspIndexResult::Status::Clean;
out:
	index_pipeline_result_destroy(&pipeline, nullptr);
	command_db_command_free(saved);

	if (changed_directory) {
		std::filesystem::current_path(old_directory, fs_error);

		if (fs_error) {
			result.status = LspIndexResult::Status::Failed;
			result.error = "failed to restore the current directory: " + fs_error.message();
		}
	}
	if (result.status == LspIndexResult::Status::Failed)
		overlays.erase(file);
	return result;
}

const LspOverlay &LspIndexer::overlay() const
{
	return overlays;
}
