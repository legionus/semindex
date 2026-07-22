// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "lsp_source.h"
#include "semindex_database.h"

#include <llvm/Support/JSON.h>

#include <string>

class LspCallHierarchy
{
public:
	enum class Status {
		Success,
		InvalidParams,
		DatabaseError,
	};

	LspCallHierarchy(semindex_db_t *database, const LspSourceMapper &sources, std::string variant);

	Status prepare(const llvm::json::Object *params, llvm::json::Value &result) const;
	Status incoming(const llvm::json::Object *params, llvm::json::Value &result) const;
	Status outgoing(const llvm::json::Object *params, llvm::json::Value &result) const;

private:
	Status calls(const llvm::json::Object *params, semindex_db_call_direction_t direction,
		llvm::json::Value &result) const;

	semindex_db_t *database;
	const LspSourceMapper &sources;
	std::string variant;
};
