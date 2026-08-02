// SPDX-License-Identifier: GPL-2.0-or-later
#include "source_resolver.h"

#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

static int checkStatus(const SemindexSourceResult &result, const std::string &expected)
{
	if (expected == "current" || expected == "git-drifted" || expected == "git-missing") {
		SemindexSourceOrigin origin =
			expected == "current" ? SemindexSourceOrigin::WorkingTree : SemindexSourceOrigin::GitCommit;
		bool drifted = expected != "current";

		if (expected == "current" && result.status != SemindexSourceStatus::Current)
			return -1;

		if (expected == "git-drifted" && result.status != SemindexSourceStatus::Drifted)
			return -1;

		if (expected == "git-missing" && result.status != SemindexSourceStatus::Missing)
			return -1;

		if (result.origin != origin || result.drifted != drifted)
			return -1;

		if (result.lines.size() != 2 || result.lines[0] != "struct S {")
			return -1;

		return 0;
	}

	SemindexSourceStatus status;

	if (expected == "drifted")
		status = SemindexSourceStatus::Drifted;
	else if (expected == "missing")
		status = SemindexSourceStatus::Missing;
	else
		return -1;

	if (result.status != status || !result.drifted || !result.lines.empty())
		return -1;

	return 0;
}

int main(int argc, char **argv)
{
	SemindexSourceResolver resolver;
	SemindexSourceResult result;
	SemindexSourceRequest request;
	semindex_db_t *database = nullptr;
	std::filesystem::path root;
	std::filesystem::path source;
	std::filesystem::path relative;
	std::vector<std::string> paths;
	std::vector<std::string> lines;
	std::string line;
	bool content_available;
	bool source_exists;
	int ret = 1;

	if (argc != 6)
		return 1;

	root = std::filesystem::path(argv[1]).lexically_normal();
	source = std::filesystem::path(argv[2]).lexically_normal();
	relative = source.lexically_relative(root);
	content_available = !strcmp(argv[5], "current") || !strncmp(argv[5], "git-", 4);
	source_exists = strcmp(argv[5], "missing") && strcmp(argv[5], "git-missing");

	if (resolver.setWorkspaceRoot("relative"))
		return 1;

	if (!resolver.setWorkspaceRoot(root))
		return 1;

	if (resolver.resolve(relative) != source)
		return 1;

	paths = resolver.databasePaths(source);

	if (paths.size() < 2 || paths[0] != source.string() || paths[1] != relative.string())
		return 1;

	if (source_exists) {
		if (!resolver.readLine(relative, 0, line) || line != "struct S {")
			return 1;

		resolver.readLines(relative, lines);

		if (lines.size() < 2 || lines[0] != line)
			return 1;
	}

	if (resolver.readLine("missing.c", 0, line))
		goto out;

	if (semindex_db_open(argv[3], &database) < 0)
		goto out;

	request = SemindexSourceRequest{
		.database = database,
		.variant = "general",
		.path = argv[4],
		.first_line = 1,
		.line_count = 2,
		.byte_limit = 1024,
	};

	if (resolver.readSource(request, result) < 0)
		goto out;

	if (checkStatus(result, argv[5]) < 0)
		goto out;

	request.path += ".not-indexed";

	if (resolver.readSource(request, result) < 0)
		goto out;

	if (result.status != SemindexSourceStatus::NotIndexed || result.drifted || !result.lines.empty())
		goto out;

	request.path = argv[4];
	request.byte_limit = 1;

	if (content_available) {
		if (resolver.readSource(request, result) < 0)
			goto out;

		if (result.origin == SemindexSourceOrigin::None || !result.byte_limit_hit || !result.lines.empty())
			goto out;
	}

	ret = 0;
out:
	semindex_db_close(database);

	return ret;
}
