// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "semindex.h"

#include <clang/Basic/SourceLocation.h>

#include <memory>
#include <set>
#include <string>
#include <vector>

namespace clang {
class ASTContext;
class SourceManager;

namespace tooling {
class FrontendActionFactory;
}
}

struct SemindexSourceLocation {
	const std::string* file = nullptr;
	unsigned line = 0;
	unsigned column = 0;
};

struct SemindexSymbol {
	semindex_symbol_kind_t kind;
	std::string name;
	std::string owner;
	std::string type;
	std::string usr;
	std::string context;
	SemindexSourceLocation loc;
	bool local;
	bool definition;
	unsigned long long order;
};

struct SemindexUse {
	semindex_use_kind_t kind;
	semindex_symbol_kind_t symbol_kind;
	unsigned mode;
	std::string name;
	std::string owner;
	std::string type;
	std::string usr;
	std::string context;
	SemindexSourceLocation loc;
	bool local;
	unsigned long long order;
};

struct semindex {
	semindex_scope_t scope = SEMINDEX_SCOPE_PROJECT;
	unsigned long long next_order = 0;
	std::set<std::string> files;
	std::vector<SemindexSymbol> symbols;
	std::vector<SemindexUse> uses;
	std::vector<semindex_symbol_t> symbol_records;
	std::vector<semindex_use_t> use_records;
};

class SemindexContext {
public:
	SemindexContext(semindex* out, clang::SourceManager& sm);

	bool inScope(clang::SourceLocation loc) const;
	clang::SourceLocation spellingLoc(clang::SourceLocation loc) const;
	SemindexSourceLocation location(clang::SourceLocation loc);
	SemindexSourceLocation displayLocation(const clang::ASTContext& ast,
	    clang::SourceLocation loc);

	void addSymbolInScope(SemindexSymbol&& s, clang::SourceLocation loc);
	void addUseInScope(SemindexUse&& u, clang::SourceLocation loc);

	std::string locationKey(const SemindexSourceLocation& loc) const;

private:
	const std::string* internFile(std::string file);
	void addSymbol(SemindexSymbol&& s);
	void addUse(SemindexUse&& u);

	semindex* out;
	clang::SourceManager& sm;
};

void rebuildRecords(semindex* s);

std::unique_ptr<clang::tooling::FrontendActionFactory>
createSemindexActionFactory(semindex* out);
