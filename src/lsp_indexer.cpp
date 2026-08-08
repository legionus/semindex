// SPDX-License-Identifier: GPL-2.0-or-later
#include "lsp_indexer.h"

#include <filesystem>
#include <utility>

LspIndexer::LspIndexer(std::string database, std::string commands_database, std::string compile_commands,
	std::string variant, bool include_local)
    : variant(std::move(variant)), compile_commands_explicit(!compile_commands.empty()),
      updater(SemindexIndexUpdaterOptions{
	      .database = std::move(database),
	      .commands_database = std::move(commands_database),
	      .compile_commands = std::move(compile_commands),
	      .include_local = include_local,
	      .allow_fallback_command = true,
      })
{
}

void LspIndexer::setWorkspaceRoot(const std::string &root)
{
	updater.setRepositoryRoot(root);

	if (compile_commands_explicit)
		return;

	std::filesystem::path path = std::filesystem::path(root) / "compile_commands.json";

	if (std::filesystem::is_regular_file(path))
		updater.setCompileCommands(path.string());
}

LspIndexResult LspIndexer::update(const std::string &file)
{
	auto result = updater.update(SemindexIndexUpdateRequest{
		.file = file,
		.variant = variant,
	});

	if (result.status == LspIndexResult::Status::Partial && result.partial_index) {
		overlays.replace(file, result.directory, result.partial_index.get());
	} else {
		overlays.erase(file);
	}

	return result;
}

const LspOverlay &LspIndexer::overlay() const
{
	return overlays;
}
