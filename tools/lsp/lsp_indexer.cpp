// SPDX-License-Identifier: GPL-2.0-or-later
#include "lsp_indexer.h"
#include "repository.h"

#include <cstdlib>
#include <filesystem>
#include <utility>

LspIndexer::LspIndexer(std::string database, std::string commands_database, std::string compile_commands,
	std::string variant, bool include_local)
    : variant(std::move(variant)), compile_commands_explicit(!compile_commands.empty()),
	      updater(SemindexIndexUpdaterOptions{
	      .database = std::move(database),
	      .command_resolver = {
		      .commands_database = std::move(commands_database),
		      .compile_commands = std::move(compile_commands),
		      .allow_document = true,
	      },
	      .include_local = include_local,
      })
{
}

void LspIndexer::setWorkspaceRoot(const std::string &root)
{
	char *discovered_root;
	std::string project_root = root;

	discovered_root = semindex_repository_root(root.c_str());

	if (discovered_root) {
		project_root = discovered_root;
		free(discovered_root);
	}

	updater.setRepositoryRoot(project_root);

	if (compile_commands_explicit)
		return;

	std::filesystem::path path = std::filesystem::path(project_root) / "compile_commands.json";

	if (std::filesystem::is_regular_file(path))
		updater.setCompileCommands(path.string());
}

LspIndexResult LspIndexer::update(const std::string &file)
{
	auto result = updater.update(SemindexIndexUpdateRequest{
		.file = file,
		.variant = variant,
		.store_partial = true,
	});

	if (result.transient_index) {
		overlays.replace(file, result.directory, result.transient_index.get());
	} else {
		overlays.erase(file);
	}

	return result;
}

const LspOverlay &LspIndexer::overlay() const
{
	return overlays;
}
