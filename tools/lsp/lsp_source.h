// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "semindex_database.h"
#include "source_resolver.h"

#include <llvm/ADT/StringRef.h>
#include <llvm/Support/JSON.h>

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

class LspSourceMapper
{
public:
	struct Cache {
		std::string path;
		std::vector<std::string> lines;
	};

	LspSourceMapper();

	bool setRootUri(llvm::StringRef uri);
	std::optional<std::string> filePath(llvm::StringRef uri) const;
	std::vector<std::string> databasePaths(llvm::StringRef uri) const;
	std::optional<unsigned> byteColumn(llvm::StringRef uri, unsigned line, unsigned character) const;
	std::string displayPath(const char *path) const;
	std::string uri(const char *path) const;
	llvm::json::Value range(const semindex_db_record_t &record, Cache &cache) const;
	llvm::json::Value location(const semindex_db_record_t &record, Cache &cache) const;
	const SemindexSourceResolver &sourceResolver() const;

private:
	SemindexSourceResolver resolver;
};
