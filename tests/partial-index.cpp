// SPDX-License-Identifier: GPL-2.0-or-later
#include "semindex_internal.h"

#include <cstdio>
#include <string>
#include <vector>

struct PartialCase {
	const char *file;
	std::vector<const char *> symbols;
};

static bool hasSymbol(const semindex_t *index, const char *name)
{
	for (const auto &symbol : index->symbols) {
		if (symbol.name == name)
			return true;
	}
	return false;
}

static int runCase(const char *compiler, const char *directory, const PartialCase &test)
{
	std::string path = std::string(directory) + "/" + test.file;
	const char *arguments[] = {
		compiler,
		"--no-default-config",
		"-std=gnu11",
		"-fsyntax-only",
		path.c_str(),
	};
	semindex_compile_command_t command = {
		.directory = directory,
		.file = path.c_str(),
		.argc = sizeof(arguments) / sizeof(arguments[0]),
		.argv = arguments,
	};
	semindex_t *index;
	int ret = -1;

	index = semindex_create();
	if (!index)
		return -1;
	if (semindex_index_command(index, &command) == 0) {
		std::fprintf(stderr, "%s: indexing unexpectedly succeeded\n", test.file);
		goto out;
	}
	for (const char *name : test.symbols) {
		if (!hasSymbol(index, name)) {
			std::fprintf(stderr, "%s: missing recovered symbol '%s'\n", test.file, name);
			goto out;
		}
	}
	ret = 0;
out:
	semindex_destroy(index);
	return ret;
}

int main(int argc, char **argv)
{
	const std::vector<PartialCase> tests = {
		{ "semantic.c", { "before_semantic", "after_semantic" } },
		{ "local-syntax.c", { "before_local_syntax", "after_local_syntax" } },
		{ "broken-delimiter.c", { "before_delimiter", "after_delimiter" } },
		{ "missing-header.c", { "before_missing_header", "after_missing_header" } },
	};

	if (argc != 3) {
		std::fprintf(stderr, "usage: partial-index COMPILER FIXTURE-DIRECTORY\n");
		return 1;
	}
	for (const auto &test : tests) {
		if (runCase(argv[1], argv[2], test) < 0)
			return 1;
	}
	return 0;
}
