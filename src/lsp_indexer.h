// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "lsp_overlay.h"
#include "semindex.h"

#include <string>
#include <vector>

struct LspIndexDiagnostic {
	semindex_diagnostic_severity_t severity;
	std::string message;
	std::string file;
	unsigned line;
	unsigned column;
};

struct LspIndexResult {
	enum class Status {
		Clean,
		Partial,
		Failed,
	};

	Status status = Status::Failed;
	std::vector<LspIndexDiagnostic> diagnostics;
	std::string error;
};

class LspIndexer
{
public:
	LspIndexer(std::string database, std::string commands_database, std::string variant, bool include_local);

	LspIndexResult update(const std::string &file);
	const LspOverlay &overlay() const;

private:
	std::string database;
	std::string commands_database;
	std::string variant;
	bool include_local;
	LspOverlay overlays;
};
