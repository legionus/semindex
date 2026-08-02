// SPDX-License-Identifier: GPL-2.0-or-later
#include "lsp_indexer.h"

#include <utility>

LspIndexer::LspIndexer(std::string database, std::string commands_database, std::string variant, bool include_local)
    : variant(std::move(variant)), updater(SemindexIndexUpdaterOptions{
					   .database = std::move(database),
					   .commands_database = std::move(commands_database),
					   .include_local = include_local,
				   })
{
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
