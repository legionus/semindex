// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "semindex_database.h"

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

enum class SemindexSourceStatus {
	Current,
	NotIndexed,
	Missing,
	Drifted,
};

enum class SemindexSourceOrigin {
	None,
	WorkingTree,
	GitCommit,
};

struct SemindexSourceRequest {
	semindex_db_t *database;
	std::string variant;
	std::string path;
	unsigned first_line;
	unsigned line_count;
	size_t byte_limit;
};

struct SemindexSourceResult {
	SemindexSourceStatus status = SemindexSourceStatus::NotIndexed;
	SemindexSourceOrigin origin = SemindexSourceOrigin::None;
	std::filesystem::path physical_path;
	std::vector<std::string> lines;
	unsigned first_line = 0;
	bool drifted = false;
	bool byte_limit_hit = false;
};

class SemindexSourceResolver
{
public:
	SemindexSourceResolver();

	static std::filesystem::path normalize(const std::filesystem::path &path);
	static std::filesystem::path resolveAgainst(const std::filesystem::path &directory,
		const std::filesystem::path &path);
	bool setWorkspaceRoot(const std::filesystem::path &path);
	std::filesystem::path resolve(const std::filesystem::path &path) const;
	std::vector<std::string> databasePaths(const std::filesystem::path &path) const;
	bool readLine(const std::filesystem::path &path, unsigned line, std::string &text) const;
	void readLines(const std::filesystem::path &path, std::vector<std::string> &lines) const;
	int readWorkingTree(const SemindexSourceRequest &request, SemindexSourceResult &result) const;
	int readSource(const SemindexSourceRequest &request, SemindexSourceResult &result) const;

private:
	std::filesystem::path root;
};
