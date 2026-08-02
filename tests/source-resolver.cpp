// SPDX-License-Identifier: GPL-2.0-or-later
#include "source_resolver.h"

#include <filesystem>
#include <string>
#include <vector>

int main(int argc, char **argv)
{
	SemindexSourceResolver resolver;
	std::filesystem::path root;
	std::filesystem::path source;
	std::vector<std::string> paths;
	std::vector<std::string> lines;
	std::string line;

	if (argc != 3)
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
		return 1;

	return 0;
}
