// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "lsp_transport.h"

#include <llvm/Support/JSON.h>

class LspServer
{
public:
	explicit LspServer(LspTransport &transport);

	int run();

private:
	enum class State {
		Uninitialized,
		Running,
		Shutdown,
	};

	bool dispatch(const llvm::json::Object &message);
	bool reply(const llvm::json::Value &id, llvm::json::Value result);
	bool error(const llvm::json::Value *id, int code, const char *message);

	LspTransport &transport;
	State state = State::Uninitialized;
	bool exiting = false;
	int exit_status = 0;
};
