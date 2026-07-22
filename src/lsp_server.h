// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "lsp_call_hierarchy.h"
#include "lsp_indexer.h"
#include "lsp_source.h"
#include "lsp_transport.h"
#include "semindex_database.h"

#include <llvm/Support/JSON.h>

#include <string>

class LspServer
{
public:
	LspServer(LspTransport &transport, semindex_db_t *database, LspIndexer &indexer, std::string variant);

	int run();

private:
	enum class State {
		Uninitialized,
		Running,
		Shutdown,
	};

	bool dispatch(const llvm::json::Object &message);
	bool definition(const llvm::json::Value &id, const llvm::json::Object *params);
	bool documentHighlight(const llvm::json::Value &id, const llvm::json::Object *params);
	bool didSave(const llvm::json::Object *params);
	bool references(const llvm::json::Value &id, const llvm::json::Object *params);
	bool hierarchyReply(const llvm::json::Value &id, LspCallHierarchy::Status status, llvm::json::Value result);
	bool reply(const llvm::json::Value &id, llvm::json::Value result);
	bool error(const llvm::json::Value *id, int code, const char *message);

	LspTransport &transport;
	semindex_db_t *database;
	LspIndexer &indexer;
	std::string variant;
	LspSourceMapper sources;
	LspCallHierarchy call_hierarchy;
	State state = State::Uninitialized;
	bool exiting = false;
	int exit_status = 0;
};
