// SPDX-License-Identifier: GPL-2.0-or-later
#include "source_resolver.h"

#include <fstream>
#include <utility>

SemindexSourceResolver::SemindexSourceResolver() : root(std::filesystem::current_path())
{
}

bool SemindexSourceResolver::setWorkspaceRoot(const std::filesystem::path &path)
{
	if (!path.is_absolute())
		return false;

	root = path.lexically_normal();

	return true;
}

std::filesystem::path SemindexSourceResolver::resolve(const std::filesystem::path &path) const
{
	if (path.is_absolute())
		return path.lexically_normal();

	return (root / path).lexically_normal();
}

std::vector<std::string> SemindexSourceResolver::databasePaths(const std::filesystem::path &path) const
{
	std::vector<std::string> result;
	std::filesystem::path normalized = path.lexically_normal();

	if (!normalized.is_absolute())
		return result;

	result.push_back(normalized.string());
	std::filesystem::path relative = normalized.lexically_relative(root);

	if (!relative.empty() && *relative.begin() != "..")
		result.push_back(relative.string());

	std::filesystem::path cwd_relative = normalized.lexically_relative(std::filesystem::current_path());
	bool inside_cwd = !cwd_relative.empty() && *cwd_relative.begin() != "..";
	bool distinct = cwd_relative != relative || result.size() == 1;

	if (inside_cwd && distinct)
		result.push_back(cwd_relative.string());

	return result;
}

bool SemindexSourceResolver::readLine(const std::filesystem::path &path, unsigned line, std::string &text) const
{
	std::ifstream input(resolve(path), std::ios::binary);

	if (!input)
		return false;

	for (unsigned current = 0; current <= line; current++) {
		if (!std::getline(input, text))
			return false;
	}

	if (!text.empty() && text.back() == '\r')
		text.pop_back();

	return true;
}

void SemindexSourceResolver::readLines(const std::filesystem::path &path, std::vector<std::string> &lines) const
{
	std::ifstream input(resolve(path), std::ios::binary);
	std::string line;

	lines.clear();

	if (!input)
		return;

	while (std::getline(input, line)) {
		if (!line.empty() && line.back() == '\r')
			line.pop_back();

		lines.push_back(std::move(line));
	}
}
