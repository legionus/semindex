// SPDX-License-Identifier: GPL-2.0-or-later
#include "index_updater.h"

extern "C" {
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

SemindexIndexUpdater::SemindexIndexUpdater(SemindexIndexUpdaterOptions options)
    : database(std::move(options.database)), repository_root(std::move(options.repository_root)),
      command_resolver(std::move(options.command_resolver)), include_local(options.include_local)
{
	command_resolver.setRepositoryRoot(repository_root);
}

void SemindexIndexUpdater::setCompileCommands(std::string path)
{
	command_resolver.setCompileCommands(std::move(path));
}

void SemindexIndexUpdater::setRepositoryRoot(std::string path)
{
	repository_root = path;
	command_resolver.setRepositoryRoot(std::move(path));
}

int SemindexIndexUpdater::commandAvailable(const std::string &file, const std::string &variant) const
{
	return command_resolver.commandAvailable(file, variant);
}

SemindexIndexUpdateResult SemindexIndexUpdater::update(const SemindexIndexUpdateRequest &request) const
{
	std::lock_guard<std::mutex> guard(update_mutex);
	index_pipeline_request_t pipeline_request = {};
	index_pipeline_result_t pipeline = {};
	SemindexIndexUpdateResult result;
	SemindexCommandResolution resolution;
	const SemindexResolvedCommand *resolved;
	std::error_code fs_error;
	std::filesystem::path old_directory;
	bool changed_directory = false;

	if (request.stopped && request.stopped()) {
		result.error = "index update cancelled";

		return result;
	}

	resolution = command_resolver.resolve(request.file, request.variant);

	if (!resolution.command) {
		result.error = std::move(resolution.error);

		return result;
	}

	resolved = resolution.command.get();
	result.command_available = resolved->commandAvailable();
	result.directory = resolved->directory();

	if (resolved->source() == SemindexResolvedCommand::Source::Saved) {
		old_directory = std::filesystem::current_path(fs_error);

		if (fs_error) {
			result.error = "failed to read the current directory: " + fs_error.message();

			goto out;
		}

		std::filesystem::current_path(resolved->directory(), fs_error);

		if (fs_error) {
			result.error = "failed to enter compiler directory '" + resolved->directory() +
				"': " + fs_error.message();

			goto out;
		}

		changed_directory = true;
		pipeline_request.input = INDEX_PIPELINE_COMMAND;
		pipeline_request.storage = INDEX_PIPELINE_STORE_SYMBOLS;
		pipeline_request.command = resolved->command();
		pipeline_request.source_file = resolved->command()->file;
	} else if (resolved->source() == SemindexResolvedCommand::Source::CompilationDatabase) {
		pipeline_request.input = INDEX_PIPELINE_COMPILE_COMMANDS;
		pipeline_request.storage = INDEX_PIPELINE_STORE_SYMBOLS_AND_COMMAND;
		pipeline_request.compile_commands = resolved->compileCommands().c_str();
		pipeline_request.source_file = request.file.c_str();
		pipeline_request.commands_database = resolved->commandsDatabase().c_str();
	} else {
		pipeline_request.input = INDEX_PIPELINE_COMMAND;
		pipeline_request.storage = INDEX_PIPELINE_STORE_SYMBOLS;
		pipeline_request.command = resolved->command();
		pipeline_request.source_file = request.file.c_str();
	}

	pipeline_request.partial = request.store_partial ? INDEX_PIPELINE_STORE_PARTIAL : INDEX_PIPELINE_RETURN_PARTIAL;
	pipeline_request.symbol_database = database.c_str();
	pipeline_request.variant = request.variant.c_str();
	pipeline_request.repository_root = repository_root.empty() ? nullptr : repository_root.c_str();
	pipeline_request.scope = SEMINDEX_SCOPE_PROJECT;
	pipeline_request.include_local = include_local;
	pipeline_request.details = 1;

	if (index_pipeline_run(&pipeline_request, &pipeline) < 0) {
		copyDiagnostics(pipeline.index, result.directory.c_str(), request.diagnostic_limit, result);

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

	if (pipeline.command) {
		result.command_available = true;
		result.directory = pipeline.command->directory;
	}

	copyDiagnostics(pipeline.index, result.directory.c_str(), request.diagnostic_limit, result);

	if (pipeline.frontend_ret != 0 || pipeline.frontend->status == SEMINDEX_INDEX_PARTIAL) {
		result.status = SemindexIndexUpdateResult::Status::Partial;

		if (!request.store_partial) {
			result.partial_index.reset(pipeline.index);
			pipeline.index = nullptr;
		}

		goto out;
	}

	if (pipeline.persisted != pipeline_request.storage) {
		result.error = "index for '" + request.file + "' was not stored";

		goto out;
	}

	result.status = SemindexIndexUpdateResult::Status::Clean;
out:
	index_pipeline_result_destroy(&pipeline, nullptr);

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
