// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <llvm/ADT/StringRef.h>
#include <llvm/Support/JSON.h>

struct LspTextPosition {
	llvm::StringRef uri;
	unsigned line;
	unsigned character;
};

bool lspParseTextPosition(const llvm::json::Object *params, LspTextPosition &result);
