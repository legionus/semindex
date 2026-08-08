// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "semindex.h"
#include "index_command_resolver.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

struct SemindexIndexDiagnostic {
	semindex_diagnostic_severity_t severity;
	std::string message;
	std::string file;
	unsigned line;
	unsigned column;
};

struct SemindexIndexDeleter {
	void operator()(semindex_t *index) const;
};

using SemindexOwnedIndex = std::unique_ptr<semindex_t, SemindexIndexDeleter>;

struct SemindexIndexUpdateResult {
	enum class Status {
		Clean,
		Partial,
		Failed,
	};

	Status status = Status::Failed;
	std::vector<SemindexIndexDiagnostic> diagnostics;
	std::string error;
	std::string directory;
	SemindexOwnedIndex partial_index;
	bool command_available = false;
	bool diagnostics_truncated = false;
};

struct SemindexIndexUpdaterOptions {
	std::string database;
	std::string repository_root;
	SemindexCommandResolverOptions command_resolver;
	bool include_local = true;
};

struct SemindexIndexUpdateRequest {
	std::string file;
	std::string variant;
	size_t diagnostic_limit = 0;
	bool store_partial = false;
	std::function<bool()> stopped;
};

class SemindexIndexUpdater
{
public:
	explicit SemindexIndexUpdater(SemindexIndexUpdaterOptions options);

	void setCompileCommands(std::string path);
	void setRepositoryRoot(std::string path);
	int commandAvailable(const std::string &file, const std::string &variant) const;
	SemindexIndexUpdateResult update(const SemindexIndexUpdateRequest &request) const;

private:
	std::string database;
	std::string repository_root;
	SemindexCommandResolver command_resolver;
	bool include_local;
};
