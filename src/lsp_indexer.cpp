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

bool LspIndexer::update(const std::string &file, std::string &error) const
{
	command_db_command_t *saved = nullptr;
	const semindex_compile_command_t *command = nullptr;
	const semindex_index_result_t *result = nullptr;
	semindex_t *index = nullptr;
	std::error_code fs_error;
	std::filesystem::path old_directory;
	int index_ret;
	int loaded;
	bool changed_directory = false;
	bool success = false;

	loaded = command_db_load(commands_database.c_str(), variant.c_str(), file.c_str(), &saved);
	if (loaded > 0) {
		error = "no saved compiler command for '" + file + "' in variant '" + variant + "'";
		goto out;
	}
	if (loaded < 0) {
		error = "failed to load compiler command for '" + file + "'";
		goto out;
	}

	command = command_db_command_get(saved);
	old_directory = std::filesystem::current_path(fs_error);
	if (fs_error) {
		error = "failed to read the current directory: " + fs_error.message();
		goto out;
	}
	std::filesystem::current_path(command->directory, fs_error);
	if (fs_error) {
		error = "failed to enter compiler directory '" + std::string(command->directory) +
			"': " + fs_error.message();
		goto out;
	}
	changed_directory = true;

	index = semindex_create();
	if (!index) {
		error = "failed to create indexer";
		goto out;
	}
	semindex_set_scope(index, SEMINDEX_SCOPE_PROJECT);
	semindex_set_include_local(index, include_local);
	index_ret = semindex_index_command(index, command);
	result = semindex_get_index_result(index);
	if (index_ret != 0 || !result || result->status != SEMINDEX_INDEX_CLEAN) {
		error = "failed to index '" + file + "'";
		goto out;
	}
	if (semindex_build_file_fingerprints(index) < 0) {
		error = "failed to fingerprint '" + file + "'";
		goto out;
	}
	if (index_db_store(database.c_str(), index, command->file, variant.c_str(), include_local, nullptr) < 0) {
		error = "failed to store index for '" + file + "'";
		goto out;
	}
	success = true;
out:
	semindex_destroy(index);
	command_db_command_free(saved);
	if (changed_directory) {
		std::filesystem::current_path(old_directory, fs_error);
		if (fs_error) {
			error = "failed to restore the current directory: " + fs_error.message();
			return false;
		}
	}
	return success;
}
