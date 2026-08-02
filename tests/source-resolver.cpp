// SPDX-License-Identifier: GPL-2.0-or-later
#include "source_resolver.h"

#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

static int checkStatus(const SemindexSourceResult &result, const std::string &expected)
{
	if (expected == "current") {
		if (result.status != SemindexSourceStatus::Current)
			return -1;

		if (result.origin != SemindexSourceOrigin::WorkingTree || result.drifted)
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
	std::vector<std::string> paths;
	std::vector<std::string> lines;
	std::string line;
	int ret = 1;

	if (argc != 6)
		return 1;

	root = std::filesystem::path(argv[1]).lexically_normal();
	source = std::filesystem::path(argv[2]).lexically_normal();

	if (resolver.setWorkspaceRoot("relative"))
		return 1;

	if (!resolver.setWorkspaceRoot(root))
		return 1;

	if (resolver.resolve("tests/test.c") != source)
		return 1;

	paths = resolver.databasePaths(source);

	if (paths.size() < 2 || paths[0] != source.string() || paths[1] != "tests/test.c")
		return 1;

	if (!resolver.readLine("tests/test.c", 0, line) || line != "struct S {")
		return 1;

	resolver.readLines("tests/test.c", lines);

	if (lines.size() < 2 || lines[0] != line)
		return 1;

	if (resolver.readLine("tests/missing.c", 0, line))
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

	if (resolver.readWorkingTree(request, result) < 0)
		goto out;

	if (checkStatus(result, argv[5]) < 0)
		goto out;

	request.path += ".not-indexed";

	if (resolver.readWorkingTree(request, result) < 0)
		goto out;

	if (result.status != SemindexSourceStatus::NotIndexed || result.drifted || !result.lines.empty())
		goto out;

	request.path = argv[4];
	request.byte_limit = 1;

	if (!strcmp(argv[5], "current")) {
		if (resolver.readWorkingTree(request, result) < 0)
			goto out;

		if (result.status != SemindexSourceStatus::Current || !result.byte_limit_hit || !result.lines.empty())
			goto out;
	}

	ret = 0;
out:
	semindex_db_close(database);

	return ret;
}
