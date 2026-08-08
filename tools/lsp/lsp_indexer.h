// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "index_updater.h"
#include "lsp_overlay.h"

#include <string>

using LspIndexDiagnostic = SemindexIndexDiagnostic;
using LspIndexResult = SemindexIndexUpdateResult;

class LspIndexer
{
public:
	LspIndexer(std::string database, std::string commands_database, std::string compile_commands,
		std::string variant, bool include_local);

	void setWorkspaceRoot(const std::string &root);
	LspIndexResult update(const std::string &file);
	const LspOverlay &overlay() const;

private:
	std::string variant;
	bool compile_commands_explicit;
	SemindexIndexUpdater updater;
	LspOverlay overlays;
};
