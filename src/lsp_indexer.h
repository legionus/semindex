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
	LspIndexer(std::string database, std::string commands_database, std::string variant, bool include_local);

	LspIndexResult update(const std::string &file);
	const LspOverlay &overlay() const;

private:
	std::string variant;
	SemindexIndexUpdater updater;
	LspOverlay overlays;
};
