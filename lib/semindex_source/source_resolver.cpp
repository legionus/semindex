// SPDX-License-Identifier: GPL-2.0-or-later
#include "source_resolver.h"

#include "git_provenance.h"

#include <cerrno>
#include <cstring>
#include <fstream>
#include <optional>
#include <sys/stat.h>
#include <utility>

namespace
{

struct IndexedFileMetadata {
	long long mtime_ns = 0;
	long long size = 0;
	bool found = false;
};

struct VariantProvenance {
	std::string variant;
	std::filesystem::path repository_root;
	std::string commit;
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

int collectVariantProvenance(void *data, const semindex_db_variant_t *variant)
{
	auto &provenance = *static_cast<VariantProvenance *>(data);

	if (provenance.variant != variant->name)
		return 0;

	if (variant->repository_root)
		provenance.repository_root = variant->repository_root;

	if (variant->git_commit)
		provenance.commit = variant->git_commit;

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

void skipBlobLine(const char *content, size_t size, size_t &offset)
{
	const void *newline = memchr(content + offset, '\n', size - offset);

	offset = newline ? static_cast<const char *>(newline) - content + 1 : size;
}

void readBlobRange(const semindex_git_blob_data_t &blob, const SemindexSourceRequest &request,
	SemindexSourceResult &result)
{
	const char *content = static_cast<const char *>(blob.content);
	size_t bytes = 0;
	size_t offset = 0;
	unsigned current = 1;

	while (current < request.first_line && offset < blob.size) {
		skipBlobLine(content, blob.size, offset);
		current++;
	}

	while (result.lines.size() < request.line_count && offset < blob.size) {
		const void *newline = memchr(content + offset, '\n', blob.size - offset);
		size_t end = newline ? static_cast<const char *>(newline) - content : blob.size;
		size_t length = end - offset;

		if (length && content[offset + length - 1] == '\r')
			length--;

		if (length + 1 > request.byte_limit - bytes) {
			result.byte_limit_hit = true;

			break;
		}

		bytes += length + 1;
		result.lines.emplace_back(content + offset, length);
		offset = newline ? end + 1 : blob.size;
	}
}

std::optional<std::string> repositoryPath(const std::string &path, const std::filesystem::path &root)
{
	std::filesystem::path source(path);
	std::filesystem::path relative = source;

	if (source.is_absolute())
		relative = source.lexically_relative(root);

	relative = relative.lexically_normal();

	if (relative.empty() || relative.is_absolute() || *relative.begin() == "..")
		return std::nullopt;

	return relative.generic_string();
}

int readGitCommit(const SemindexSourceRequest &request, SemindexSourceResult &result)
{
	VariantProvenance provenance = {
		.variant = request.variant,
	};
	semindex_git_blob_t blob;
	int ret;

	ret = semindex_db_list_variants(request.database, collectVariantProvenance, &provenance);

	if (ret < 0)
		return -1;

	if (provenance.repository_root.empty() || provenance.commit.empty())
		return 0;

	auto path = repositoryPath(request.path, provenance.repository_root);

	if (!path)
		return 0;

	ret = semindex_git_blob(provenance.repository_root.c_str(), provenance.commit.c_str(), path->c_str(), &blob);

	if (ret < 0)
		return -1;

	if (!ret) {
		readBlobRange(blob.data, request, result);
		result.origin = SemindexSourceOrigin::GitCommit;
	}

	semindex_git_blob_destroy(&blob);

	return 0;
}

} // namespace

SemindexSourceResolver::SemindexSourceResolver() : root(std::filesystem::current_path())
{
}

std::filesystem::path SemindexSourceResolver::normalize(const std::filesystem::path &path)
{
	return path.lexically_normal();
}

std::filesystem::path SemindexSourceResolver::resolveAgainst(const std::filesystem::path &directory,
	const std::filesystem::path &path)
{
	if (path.is_absolute())
		return normalize(path);

	return normalize(directory / path);
}

bool SemindexSourceResolver::setWorkspaceRoot(const std::filesystem::path &path)
{
	if (!path.is_absolute())
		return false;

	root = normalize(path);

	return true;
}

std::filesystem::path SemindexSourceResolver::resolve(const std::filesystem::path &path) const
{
	return resolveAgainst(root, path);
}

std::vector<std::string> SemindexSourceResolver::databasePaths(const std::filesystem::path &path) const
{
	std::vector<std::string> result;
	std::filesystem::path normalized = normalize(path);

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

int SemindexSourceResolver::readSource(const SemindexSourceRequest &request, SemindexSourceResult &result) const
{
	int ret = readWorkingTree(request, result);

	if (ret < 0)
		return -1;

	if (result.status == SemindexSourceStatus::Current || result.status == SemindexSourceStatus::NotIndexed)
		return 0;

	return readGitCommit(request, result);
}
