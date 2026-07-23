// SPDX-License-Identifier: GPL-2.0-or-later
#include "lsp_call_hierarchy.h"

#include <charconv>
#include <iomanip>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <system_error>
#include <tuple>
#include <utility>
#include <vector>

static constexpr int FUNCTION_SYMBOL_KIND = 12;

struct FunctionIdentity {
	std::string variant;
	std::string symbol;
	unsigned long long usr_id;

	bool operator<(const FunctionIdentity &other) const
	{
		return std::tie(variant, symbol, usr_id) < std::tie(other.variant, other.symbol, other.usr_id);
	}
};

struct CallRecord {
	std::string variant;
	std::string path;
	std::string symbol;
	std::string context;
	semindex_db_record_type_t record;
	semindex_symbol_kind_t kind;
	unsigned action;
	unsigned mode;
	unsigned line;
	unsigned column;
	unsigned long long usr_id;
	unsigned long long context_usr_id;
	int local;

	semindex_db_record_t view() const
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
};

static CallRecord copyRecord(const semindex_db_record_t &record)
{
	return CallRecord{
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

static bool parsePosition(const llvm::json::Object *params, llvm::StringRef &uri, unsigned &line, unsigned &character)
{
	if (!params)
		return false;

	const llvm::json::Object *document = params->getObject("textDocument");
	const llvm::json::Object *position = params->getObject("position");

	if (!document || !position)
		return false;

	auto parsed_uri = document->getString("uri");
	auto parsed_line = position->getInteger("line");
	auto parsed_character = position->getInteger("character");

	if (!parsed_uri || !parsed_line || !parsed_character || *parsed_line < 0 || *parsed_character < 0 ||
		*parsed_line >= std::numeric_limits<unsigned>::max() ||
		*parsed_character > std::numeric_limits<unsigned>::max())
		return false;

	uri = *parsed_uri;
	line = *parsed_line;
	character = *parsed_character;
	return true;
}

static std::string formatId(unsigned long long id)
{
	std::ostringstream stream;

	stream << std::hex << std::setfill('0') << std::setw(16) << id;
	return stream.str();
}

static bool parseIdentity(const llvm::json::Object *params, const std::string &variant, FunctionIdentity &identity)
{
	const llvm::json::Object *item = params ? params->getObject("item") : nullptr;
	const llvm::json::Object *data = item ? item->getObject("data") : nullptr;

	if (!data)
		return false;

	auto parsed_variant = data->getString("variant");
	auto symbol = data->getString("symbol");
	auto id = data->getString("id");

	if (!parsed_variant || parsed_variant->empty() || !symbol || symbol->empty() || !id || id->empty() ||
		id->size() > 16 || (!variant.empty() && *parsed_variant != variant))
		return false;

	identity.variant = parsed_variant->str();
	identity.symbol = symbol->str();
	auto parsed = std::from_chars(id->begin(), id->end(), identity.usr_id, 16);
	return parsed.ec == std::errc() && parsed.ptr == id->end();
}

struct FunctionCollector {
	std::set<FunctionIdentity> identities;
};

static int collectFunctionIdentity(void *data, const semindex_db_record_t *record)
{
	auto &collector = *static_cast<FunctionCollector *>(data);

	if (record->kind == SEMINDEX_SYMBOL_FUNCTION && record->usr_id)
		collector.identities.insert(FunctionIdentity{ record->variant, record->symbol, record->usr_id });
	return 0;
}

struct FunctionRecordCollector {
	std::map<FunctionIdentity, CallRecord> records;
};

static int collectFunctionRecord(void *data, const semindex_db_record_t *record)
{
	auto &collector = *static_cast<FunctionRecordCollector *>(data);
	FunctionIdentity identity{ record->variant, record->symbol, record->usr_id };
	auto found = collector.records.find(identity);

	if (found == collector.records.end() || (!found->second.action && record->action))
		collector.records[std::move(identity)] = copyRecord(*record);
	return 0;
}

static int loadFunctionRecords(semindex_db_t *database, const std::vector<FunctionIdentity> &identities,
	FunctionRecordCollector &result)
{
	std::vector<semindex_db_function_t> functions;

	functions.reserve(identities.size());

	for (const auto &identity : identities)
		functions.push_back(semindex_db_function_t{
			.variant = identity.variant.c_str(),
			.symbol = identity.symbol.c_str(),
			.usr_id = identity.usr_id,
		});
	return semindex_db_query_functions(database, functions.data(), functions.size(), collectFunctionRecord,
		&result);
}

static llvm::json::Value hierarchyItem(const LspSourceMapper &sources, const FunctionIdentity &identity,
	const CallRecord &record)
{
	LspSourceMapper::Cache cache;
	semindex_db_record_t view = record.view();

	return llvm::json::Object{
		{ "name", identity.symbol },
		{ "kind", FUNCTION_SYMBOL_KIND },
		{ "detail", record.path },
		{ "uri", sources.uri(record.path.c_str()) },
		{ "range", sources.range(view, cache) },
		{ "selectionRange", sources.range(view, cache) },
		{ "data",
			llvm::json::Object{
				{ "variant", identity.variant },
				{ "symbol", identity.symbol },
				{ "id", formatId(identity.usr_id) },
			} },
	};
}

struct CallCollector {
	semindex_db_call_direction_t direction;
	std::map<FunctionIdentity, std::vector<CallRecord>> groups;
};

static int collectCall(void *data, const semindex_db_record_t *record)
{
	auto &collector = *static_cast<CallCollector *>(data);
	FunctionIdentity identity;

	identity.variant = record->variant;

	if (collector.direction == SEMINDEX_DB_CALLERS) {
		identity.symbol = record->context;
		identity.usr_id = record->context_usr_id;
	} else {
		identity.symbol = record->symbol;
		identity.usr_id = record->usr_id;
	}
	if (identity.symbol.empty() || !identity.usr_id)
		return 0;

	collector.groups[std::move(identity)].push_back(copyRecord(*record));
	return 0;
}

LspCallHierarchy::LspCallHierarchy(semindex_db_t *database, const LspSourceMapper &sources, std::string variant)
    : database(database), sources(sources), variant(std::move(variant))
{
}

LspCallHierarchy::Status LspCallHierarchy::prepare(const llvm::json::Object *params, llvm::json::Value &result) const
{
	llvm::StringRef uri;
	unsigned line;
	unsigned character;

	if (!parsePosition(params, uri, line, character))
		return Status::InvalidParams;

	auto column = sources.byteColumn(uri, line, character);

	if (!column) {
		result = nullptr;
		return Status::Success;
	}

	FunctionCollector collector;

	for (const auto &path : sources.databasePaths(uri)) {
		if (semindex_db_find_at(database, path.c_str(), variant.empty() ? nullptr : variant.c_str(), line + 1,
			    *column, collectFunctionIdentity, &collector) < 0)
			return Status::DatabaseError;

		if (!collector.identities.empty())
			break;
	}
	if (collector.identities.empty()) {
		result = nullptr;
		return Status::Success;
	}

	llvm::json::Array items;
	std::vector<FunctionIdentity> identities(collector.identities.begin(), collector.identities.end());
	FunctionRecordCollector functions;

	if (loadFunctionRecords(database, identities, functions) < 0)
		return Status::DatabaseError;

	for (const auto &identity : identities) {
		auto function = functions.records.find(identity);

		if (function != functions.records.end())
			items.push_back(hierarchyItem(sources, identity, function->second));
	}
	result = items.empty() ? llvm::json::Value(nullptr) : llvm::json::Value(std::move(items));
	return Status::Success;
}

LspCallHierarchy::Status LspCallHierarchy::calls(const llvm::json::Object *params,
	semindex_db_call_direction_t direction, llvm::json::Value &result) const
{
	FunctionIdentity requested;

	if (!parseIdentity(params, variant, requested))
		return Status::InvalidParams;

	semindex_db_call_options_t options = {};

	options.function = requested.symbol.c_str();
	options.variant = requested.variant.c_str();
	options.usr_id = requested.usr_id;
	options.direction = direction;
	CallCollector collector{ direction };

	if (semindex_db_query_calls(database, &options, collectCall, &collector) < 0)
		return Status::DatabaseError;

	std::vector<FunctionIdentity> identities;

	identities.reserve(collector.groups.size());

	for (const auto &entry : collector.groups)
		identities.push_back(entry.first);
	FunctionRecordCollector functions;

	if (loadFunctionRecords(database, identities, functions) < 0)
		return Status::DatabaseError;

	llvm::json::Array calls;

	for (const auto &[identity, records] : collector.groups) {
		auto function = functions.records.find(identity);
		const CallRecord &location = function != functions.records.end() ? function->second : records.front();
		llvm::json::Array ranges;
		LspSourceMapper::Cache cache;

		for (const auto &record : records) {
			semindex_db_record_t view = record.view();

			ranges.push_back(sources.range(view, cache));
		}
		calls.push_back(llvm::json::Object{
			{ direction == SEMINDEX_DB_CALLERS ? "from" : "to",
				hierarchyItem(sources, identity, location) },
			{ "fromRanges", std::move(ranges) },
		});
	}
	result = std::move(calls);
	return Status::Success;
}

LspCallHierarchy::Status LspCallHierarchy::incoming(const llvm::json::Object *params, llvm::json::Value &result) const
{
	return calls(params, SEMINDEX_DB_CALLERS, result);
}

LspCallHierarchy::Status LspCallHierarchy::outgoing(const llvm::json::Object *params, llvm::json::Value &result) const
{
	return calls(params, SEMINDEX_DB_CALLEES, result);
}
