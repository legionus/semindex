// SPDX-License-Identifier: GPL-2.0-or-later
#include "semindex.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

struct PartialCase {
	const char *file;
	semindex_index_status_t status;
	std::vector<const char *> symbols;
	size_t minimum_uses = 0;
};

static bool hasSymbol(const semindex_t *index, const char *name)
{
	for (size_t i = 0; i < semindex_symbol_count(index); i++) {
		const semindex_symbol_t *symbol = semindex_get_symbol(index, i);

		if (symbol && !std::strcmp(symbol->name, name))
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
	const semindex_index_result_t *result;
	int ret = -1;

	index = semindex_create();
	if (!index)
		return -1;
	ret = semindex_index_command(index, &command);
	result = semindex_get_index_result(index);
	if (!result || result->status != test.status) {
		std::fprintf(stderr, "%s: expected status %d, got %d\n", test.file, test.status,
			result ? result->status : -1);
		goto out;
	}
	if ((test.status == SEMINDEX_INDEX_CLEAN) != (ret == 0)) {
		std::fprintf(stderr, "%s: status disagrees with frontend result\n", test.file);
		goto out;
	}
	if (test.status == SEMINDEX_INDEX_CLEAN && result->errors) {
		std::fprintf(stderr, "%s: clean result has %u errors\n", test.file, result->errors);
		goto out;
	}
	if (test.status == SEMINDEX_INDEX_PARTIAL && !result->errors) {
		std::fprintf(stderr, "%s: partial result has no errors\n", test.file);
		goto out;
	}
	if (test.status == SEMINDEX_INDEX_FAILED && semindex_symbol_count(index)) {
		std::fprintf(stderr, "%s: failed result exposes symbols\n", test.file);
		goto out;
	}
	for (const char *name : test.symbols) {
		if (!hasSymbol(index, name)) {
			std::fprintf(stderr, "%s: missing recovered symbol '%s'\n", test.file, name);
			goto out;
		}
	}
	if (semindex_use_count(index) < test.minimum_uses) {
		std::fprintf(stderr, "%s: missing recovered uses\n", test.file);
		goto out;
	}
	ret = 0;
out:
	semindex_destroy(index);
	return ret;
}

int main(int argc, char **argv)
{
	const std::vector<PartialCase> tests = {
		{ "clean.c", SEMINDEX_INDEX_CLEAN, { "clean_symbol" } },
		{ "semantic.c", SEMINDEX_INDEX_PARTIAL, { "before_semantic", "after_semantic" } },
		{ "local-syntax.c", SEMINDEX_INDEX_PARTIAL, { "before_local_syntax", "after_local_syntax" } },
		{ "broken-delimiter.c", SEMINDEX_INDEX_PARTIAL, { "before_delimiter", "after_delimiter" }, 1 },
		{ "missing-header.c", SEMINDEX_INDEX_PARTIAL, { "before_missing_header", "after_missing_header" } },
		{ "not-present.c", SEMINDEX_INDEX_FAILED, {} },
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
