// SPDX-License-Identifier: GPL-2.0-or-later
#include "semantic_query.h"

#include <set>
#include <tuple>
#include <utility>

semindex_db_record_t SemindexQueryRecord::view() const
{
	return semindex_db_record_t{
		.variant = variant.c_str(),
		.path = path.c_str(),
		.symbol = symbol.c_str(),
		.context = context.c_str(),
		.record = record,
		.kind = kind,
		.action = action,
		.mode = mode,
		.line = line,
		.column = column,
		.usr_id = usr_id,
		.context_usr_id = context_usr_id,
		.local = local,
	};
}

namespace
{

struct RecordKey {
	std::string variant;
	std::string path;
	std::string symbol;
	std::string context;
	semindex_symbol_kind_t kind;
	unsigned long long usr_id;
	int local;

	bool operator<(const RecordKey &other) const
	{
		return std::tie(variant, symbol, kind, usr_id, path, context, local) <
			std::tie(other.variant, other.symbol, other.kind, other.usr_id, other.path, other.context,
				other.local);
	}
};

struct RecordCollector {
	std::vector<SemindexQueryRecord> records;
	std::set<RecordKey> keys;
};

} // namespace

SemindexQueryRecord semindexQueryRecord(const semindex_db_record_t &record)
{
	return SemindexQueryRecord{
		.variant = record.variant,
		.path = record.path,
		.symbol = record.symbol,
		.context = record.context,
		.record = record.record,
		.kind = record.kind,
		.action = record.action,
		.mode = record.mode,
		.line = record.line,
		.column = record.column,
		.usr_id = record.usr_id,
		.context_usr_id = record.context_usr_id,
		.local = record.local,
	};
}

namespace
{

RecordKey recordKey(const semindex_db_record_t &record)
{
	RecordKey key = {
		.variant = record.variant,
		.symbol = record.symbol,
		.kind = record.kind,
		.usr_id = record.usr_id,
	};

	if (!record.usr_id) {
		key.path = record.path;
		key.context = record.context;
		key.local = record.local;
	}

	return key;
}

int collectRecord(void *data, const semindex_db_record_t *record)
{
	auto &collector = *static_cast<RecordCollector *>(data);
	RecordKey key = recordKey(*record);

	if (collector.keys.insert(std::move(key)).second)
		collector.records.push_back(semindexQueryRecord(*record));

	return 0;
}

} // namespace

SemindexQueryService::SemindexQueryService(semindex_db_t *database, const SemindexSourceResolver &sources)
    : database(database), sources(sources)
{
}

int SemindexQueryService::recordsAt(const SemindexPositionQuery &query, std::vector<SemindexQueryRecord> &records) const
{
	RecordCollector collector;

	records.clear();

	if (!database || query.path.empty() || !query.line || !query.column)
		return -1;

	const char *variant = query.variant.empty() ? nullptr : query.variant.c_str();

	for (const auto &path : sources.databasePaths(query.path)) {
		if (query.overlay && query.overlay->contains(path)) {
			if (query.overlay->findAt(path, variant, query.line, query.column, collectRecord, &collector) <
				0)
				return -1;

			if (!collector.records.empty())
				break;
		}

		if (semindex_db_find_at(database, path.c_str(), variant, query.line, query.column, collectRecord,
			    &collector) < 0)
			return -1;

		if (!collector.records.empty())
			break;
	}

	records = std::move(collector.records);

	return 0;
}
