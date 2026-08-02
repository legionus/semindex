// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <filesystem>
#include <string>
#include <vector>

class SemindexSourceResolver
{
public:
	SemindexSourceResolver();

	bool setWorkspaceRoot(const std::filesystem::path &path);
	std::filesystem::path resolve(const std::filesystem::path &path) const;
	std::vector<std::string> databasePaths(const std::filesystem::path &path) const;
	bool readLine(const std::filesystem::path &path, unsigned line, std::string &text) const;
	void readLines(const std::filesystem::path &path, std::vector<std::string> &lines) const;

private:
	std::filesystem::path root;
};
