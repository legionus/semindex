// SPDX-License-Identifier: GPL-2.0-or-later
#include "source_resolver.h"

#include <cerrno>
#include <fstream>
#include <sys/stat.h>
#include <utility>

namespace
{

struct IndexedFileMetadata {
	long long mtime_ns = 0;
	long long size = 0;
	bool found = false;
};

long long statMtime(const struct stat &st)
{
	return (long long)st.st_mtim.tv_sec * 1000000000LL + st.st_mtim.tv_nsec;
}

bool metadataMatches(const IndexedFileMetadata &metadata, const struct stat &st)
{
	return metadata.mtime_ns == statMtime(st) && metadata.size == st.st_size;
}

bool validRequest(const SemindexSourceRequest &request)
{
	if (!request.database || request.variant.empty() || request.path.empty())
		return false;

	return request.first_line && request.line_count && request.byte_limit;
}

int collectFileMetadata(void *data, const semindex_db_file_t *file)
{
	auto &metadata = *static_cast<IndexedFileMetadata *>(data);

	metadata.mtime_ns = file->mtime_ns;
	metadata.size = file->size;
	metadata.found = true;

	return 0;
}

bool readRange(const std::filesystem::path &path, const SemindexSourceRequest &request, SemindexSourceResult &result)
{
	std::ifstream input(path, std::ios::binary);
	std::string line;
	size_t bytes = 0;
	unsigned current = 1;

	if (!input)
		return false;

	while (current < request.first_line && std::getline(input, line))
		current++;

	while (result.lines.size() < request.line_count && std::getline(input, line)) {
		if (!line.empty() && line.back() == '\r')
			line.pop_back();

		if (line.size() + 1 > request.byte_limit - bytes) {
			result.byte_limit_hit = true;

			break;
		}

		bytes += line.size() + 1;
		result.lines.push_back(std::move(line));
	}

	return true;
}

} // namespace

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

int SemindexSourceResolver::readWorkingTree(const SemindexSourceRequest &request, SemindexSourceResult &result) const
{
	IndexedFileMetadata metadata;
	struct stat before;
	struct stat after;

	result = SemindexSourceResult{};

	if (!validRequest(request))
		return -1;

	if (semindex_db_find_file(request.database, request.variant.c_str(), request.path.c_str(), collectFileMetadata,
		    &metadata) < 0)
		return -1;

	if (!metadata.found)
		return 0;

	result.physical_path = resolve(request.path);
	result.first_line = request.first_line;

	if (stat(result.physical_path.c_str(), &before) < 0) {
		if (errno != ENOENT)
			return -1;

		result.status = SemindexSourceStatus::Missing;
		result.drifted = true;

		return 0;
	}

	if (!metadataMatches(metadata, before)) {
		result.status = SemindexSourceStatus::Drifted;
		result.drifted = true;

		return 0;
	}

	if (!readRange(result.physical_path, request, result))
		return -1;

	if (stat(result.physical_path.c_str(), &after) < 0)
		return -1;

	if (!metadataMatches(metadata, after)) {
		result = SemindexSourceResult{};
		result.status = SemindexSourceStatus::Drifted;
		result.physical_path = resolve(request.path);
		result.first_line = request.first_line;
		result.drifted = true;

		return 0;
	}

	result.status = SemindexSourceStatus::Current;
	result.origin = SemindexSourceOrigin::WorkingTree;

	return 0;
}
