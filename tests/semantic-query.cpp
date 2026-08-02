// SPDX-License-Identifier: GPL-2.0-or-later
#include "semantic_query.h"

#include <filesystem>
#include <set>
#include <string>
#include <vector>

static bool validRecord(const SemindexQueryRecord &record, const std::string &path)
{
	if (record.path != path || record.symbol != "Outer.y")
		return false;

	if (record.kind != SEMINDEX_SYMBOL_FIELD || record.action != SEMINDEX_USE_WRITE)
		return false;

	return record.line == 14 && record.column == 3;
}

int main(int argc, char **argv)
{
	SemindexSourceResolver sources;
	semindex_db_t *database = nullptr;
	std::vector<SemindexQueryRecord> records;
	std::set<std::string> variants;
	std::filesystem::path root;
	std::filesystem::path source;
	std::string relative;
	int ret = 1;

	if (argc != 4)
		return 1;

	root = std::filesystem::path(argv[1]).lexically_normal();
	source = std::filesystem::path(argv[2]).lexically_normal();
	relative = source.lexically_relative(root).string();

	if (!sources.setWorkspaceRoot(root))
		return 1;

	if (semindex_db_open(argv[3], &database) < 0)
		return 1;

	SemindexQueryService queries(database, sources);
	SemindexPositionQuery query = {
		.path = source.string(),
		.line = 14,
		.column = 3,
	};

	if (queries.recordsAt(query, records) < 0 || records.size() != 2)
		goto out;

	for (const auto &record : records) {
		if (!validRecord(record, relative))
			goto out;

		variants.insert(record.variant);
	}

	if (variants != std::set<std::string>{ "debug", "general" })
		goto out;

	query.variant = "general";

	if (queries.recordsAt(query, records) < 0 || records.size() != 1)
		goto out;

	if (!validRecord(records.front(), relative) || records.front().variant != "general")
		goto out;

	query.column = 1;

	if (queries.recordsAt(query, records) < 0 || !records.empty())
		goto out;

	ret = 0;
out:
	semindex_db_close(database);

	return ret;
}
