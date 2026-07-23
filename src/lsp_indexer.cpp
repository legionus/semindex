// SPDX-License-Identifier: GPL-2.0-or-later
#include "lsp_indexer.h"

extern "C" {
#include "command_db.h"
#include "index_db.h"
#include "semindex.h"
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
	const semindex_index_result_t *index_result = nullptr;

	semindex_t *index = nullptr;
	LspIndexResult result;
	std::error_code fs_error;
	std::filesystem::path old_directory;
	int index_ret;
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

	index = semindex_create();

	if (!index) {
		result.error = "failed to create indexer";
		goto out;
	}
	semindex_set_scope(index, SEMINDEX_SCOPE_PROJECT);
	semindex_set_include_local(index, include_local);
	index_ret = semindex_index_command(index, command);
	index_result = semindex_get_index_result(index);
	copyDiagnostics(index, command->directory, result);

	if (!index_result || index_result->status == SEMINDEX_INDEX_FAILED) {
		result.error = "failed to index '" + file + "'";
		overlays.erase(file);
		goto out;
	}
	if (index_ret != 0 || index_result->status == SEMINDEX_INDEX_PARTIAL) {
		result.status = LspIndexResult::Status::Partial;
		overlays.replace(file, command->directory, index);
		goto out;
	}
	if (semindex_build_file_fingerprints(index) < 0) {
		result.error = "failed to fingerprint '" + file + "'";
		goto out;
	}
	if (index_db_store(database.c_str(), index, command->file, variant.c_str(), include_local, nullptr) < 0) {
		result.error = "failed to store index for '" + file + "'";
		goto out;
	}
	overlays.erase(file);
	result.status = LspIndexResult::Status::Clean;
out:
	semindex_destroy(index);
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
