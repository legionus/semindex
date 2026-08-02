// SPDX-License-Identifier: GPL-2.0-or-later
#include "mcp_tools.h"

#include "semantic_query.h"
#include "semindex_database.h"
#include "source_resolver.h"

#include <llvm/Support/Error.h>
#include <llvm/Support/raw_ostream.h>

#include <charconv>
#include <cstdio>
#include <deque>
#include <limits>
#include <memory>
#include <optional>
#include <set>
#include <tuple>
#include <utility>
#include <vector>

static constexpr size_t DEFAULT_LIMIT = 50;
static constexpr size_t MAX_LIMIT = 200;
static constexpr unsigned MAX_GRAPH_DEPTH = 16;
static constexpr size_t DEFAULT_GRAPH_NODES = 50;
static constexpr size_t MAX_GRAPH_NODES = 200;
static constexpr unsigned DEFAULT_CONTEXT_LINES = 20;
static constexpr unsigned MAX_CONTEXT_LINES = 200;
static constexpr size_t DEFAULT_CONTEXT_BYTES = 16 * 1024;
static constexpr size_t MAX_CONTEXT_BYTES = 64 * 1024;

namespace
{

struct DatabaseCloser {
	void operator()(semindex_db_t *database) const
	{
		semindex_db_close(database);
	}
};

using Database = std::unique_ptr<semindex_db_t, DatabaseCloser>;

struct RecordCollector {
	const McpRequestControl &control;
	size_t limit;
	std::vector<SemindexQueryRecord> records;
	bool stopped = false;
};

struct CallCollector {
	const McpRequestControl &control;
	size_t limit;
	std::string after;
	std::vector<SemindexQueryRecord> records;
	bool after_seen;
	bool stopped = false;
};

struct GraphFunctionKey {
	std::string variant;
	std::string symbol;
	unsigned long long usr_id;

	bool operator<(const GraphFunctionKey &other) const
	{
		return std::tie(variant, symbol, usr_id) < std::tie(other.variant, other.symbol, other.usr_id);
	}
};

struct GraphFunction {
	std::string variant;
	std::string symbol;
	unsigned long long usr_id;
	unsigned depth;
	std::set<GraphFunctionKey> ancestors;
};

struct GraphRecord {
	SemindexQueryRecord record;
	unsigned depth;
};

struct GraphCollector {
	const McpRequestControl &control;
	size_t limit;
	std::vector<SemindexQueryRecord> records;
	bool stopped = false;
};

struct VariantRecord {
	std::string name;
	std::string repository_root;
	std::string git_commit;
};

struct VariantCollector {
	const McpRequestControl &control;
	std::string selected;
	std::string after;
	size_t limit;
	std::vector<VariantRecord> variants;
	bool stopped = false;
};

const char *kindName(semindex_symbol_kind_t kind)
{
	switch (kind) {
	case SEMINDEX_SYMBOL_VAR:
		return "variable";
	case SEMINDEX_SYMBOL_FIELD:
		return "field";
	case SEMINDEX_SYMBOL_STRUCT:
		return "struct";
	case SEMINDEX_SYMBOL_UNION:
		return "union";
	case SEMINDEX_SYMBOL_ENUM:
		return "enum";
	case SEMINDEX_SYMBOL_ENUM_CONSTANT:
		return "enumerator";
	case SEMINDEX_SYMBOL_TYPEDEF:
		return "typedef";
	case SEMINDEX_SYMBOL_FUNCTION:
		return "function";
	case SEMINDEX_SYMBOL_MACRO:
		return "macro";
	case SEMINDEX_SYMBOL_FILE:
		return "file";
	}

	return "unknown";
}

std::optional<semindex_symbol_kind_t> parseKind(llvm::StringRef value)
{
	for (int kind = SEMINDEX_SYMBOL_VAR; kind <= SEMINDEX_SYMBOL_FILE; kind++) {
		auto parsed = static_cast<semindex_symbol_kind_t>(kind);

		if (value == kindName(parsed))
			return parsed;
	}

	return std::nullopt;
}

const char *recordName(semindex_db_record_type_t record)
{
	switch (record) {
	case SEMINDEX_DB_DECLARATION:
		return "declaration";
	case SEMINDEX_DB_DEFINITION:
		return "definition";
	case SEMINDEX_DB_REFERENCE:
		return "reference";
	}

	return "unknown";
}

const char *actionName(const SemindexQueryRecord &record)
{
	if (record.record != SEMINDEX_DB_REFERENCE)
		return "none";

	switch (record.action) {
	case SEMINDEX_USE_READ:
		return "read";
	case SEMINDEX_USE_WRITE:
		return "write";
	case SEMINDEX_USE_ADDR:
		return "address";
	case SEMINDEX_USE_CALL:
		return "call";
	}

	return "unknown";
}

std::string modeName(unsigned mode)
{
	std::string result(3, '-');
	static const unsigned read[] = { SEMINDEX_MODE_R_AOF, SEMINDEX_MODE_R_VAL, SEMINDEX_MODE_R_PTR };
	static const unsigned write[] = { SEMINDEX_MODE_W_AOF, SEMINDEX_MODE_W_VAL, SEMINDEX_MODE_W_PTR };

	for (size_t i = 0; i < result.size(); i++) {
		if ((mode & read[i]) && (mode & write[i]))
			result[i] = 'm';
		else if (mode & read[i])
			result[i] = 'r';
		else if (mode & write[i])
			result[i] = 'w';
	}

	return result;
}

std::string idString(unsigned long long id)
{
	char value[17];

	snprintf(value, sizeof(value), "%016llx", id);

	return value;
}

llvm::json::Object recordObject(const SemindexQueryRecord &record)
{
	llvm::json::Object result{
		{ "variant", record.variant },
		{ "path", record.path },
		{ "line", record.line },
		{ "column", record.column },
		{ "symbol", record.symbol },
		{ "kind", kindName(record.kind) },
		{ "record", recordName(record.record) },
		{ "action", actionName(record) },
		{ "mode", modeName(record.mode) },
		{ "context", record.context },
		{ "local", !!record.local },
	};

	if (record.usr_id)
		result["usrId"] = idString(record.usr_id);

	if (record.context_usr_id)
		result["contextUsrId"] = idString(record.context_usr_id);

	return result;
}

llvm::json::Object graphRecordObject(const GraphRecord &record)
{
	auto result = recordObject(record.record);

	result["depth"] = record.depth;

	return result;
}

bool parseLimit(const llvm::json::Object *arguments, size_t &limit)
{
	limit = DEFAULT_LIMIT;

	if (!arguments)
		return true;

	auto value = arguments->getInteger("limit");

	if (!value)
		return !arguments->get("limit");

	if (*value < 1 || static_cast<unsigned long long>(*value) > MAX_LIMIT)
		return false;

	limit = *value;

	return true;
}

bool parseGraphBounds(const llvm::json::Object *arguments, unsigned &depth, size_t &node_limit)
{
	depth = 1;
	node_limit = DEFAULT_GRAPH_NODES;

	if (!arguments)
		return true;

	if (auto value = arguments->getInteger("depth")) {
		if (*value < 1 || *value > MAX_GRAPH_DEPTH)
			return false;

		depth = *value;
	} else if (arguments->get("depth")) {
		return false;
	}

	if (auto value = arguments->getInteger("nodeLimit")) {
		if (*value < 1 || static_cast<unsigned long long>(*value) > MAX_GRAPH_NODES)
			return false;

		node_limit = *value;
	} else if (arguments->get("nodeLimit")) {
		return false;
	}

	return true;
}

std::optional<unsigned long long> parseId(llvm::StringRef value)
{
	unsigned long long id;
	auto parsed = std::from_chars(value.begin(), value.end(), id, 16);

	if (value.empty() || value.size() > 16 || parsed.ec != std::errc() || parsed.ptr != value.end())
		return std::nullopt;

	return id;
}

std::string encodeHex(llvm::StringRef input)
{
	static constexpr char digits[] = "0123456789abcdef";
	std::string result;

	result.reserve(input.size() * 2);

	for (unsigned char value : input) {
		result.push_back(digits[value >> 4]);
		result.push_back(digits[value & 0xf]);
	}

	return result;
}

std::optional<std::string> decodeHex(llvm::StringRef input)
{
	std::string result;

	if (input.size() % 2)
		return std::nullopt;

	result.reserve(input.size() / 2);

	for (size_t i = 0; i < input.size(); i += 2) {
		unsigned value;
		auto parsed = std::from_chars(input.begin() + i, input.begin() + i + 2, value, 16);

		if (parsed.ec != std::errc() || parsed.ptr != input.begin() + i + 2)
			return std::nullopt;

		result.push_back(static_cast<char>(value));
	}

	return result;
}

std::string recordCursor(const SemindexQueryRecord &record)
{
	llvm::json::Object cursor{
		{ "variant", record.variant },
		{ "path", record.path },
		{ "line", record.line },
		{ "column", record.column },
		{ "symbol", record.symbol },
		{ "record", static_cast<int>(record.record) },
		{ "action", record.action },
		{ "kind", static_cast<int>(record.kind) },
		{ "mode", record.mode },
	};
	std::string serialized;
	llvm::raw_string_ostream stream(serialized);

	stream << llvm::json::Value(std::move(cursor));
	stream.flush();

	return encodeHex(serialized);
}

struct OwnedCursor {
	std::string variant;
	std::string path;
	std::string symbol;
	semindex_db_cursor_t cursor = {};

	void updatePointers()
	{
		cursor.variant = variant.c_str();
		cursor.path = path.c_str();
		cursor.symbol = symbol.c_str();
	}
};

bool parseCursor(llvm::StringRef encoded, OwnedCursor &result)
{
	auto decoded = decodeHex(encoded);

	if (!decoded)
		return false;

	auto parsed = llvm::json::parse(*decoded);

	if (!parsed) {
		llvm::consumeError(parsed.takeError());

		return false;
	}

	const llvm::json::Object *object = parsed->getAsObject();

	if (!object)
		return false;

	auto variant = object->getString("variant");
	auto path = object->getString("path");
	auto symbol = object->getString("symbol");
	auto line = object->getInteger("line");
	auto column = object->getInteger("column");
	auto record = object->getInteger("record");
	auto action = object->getInteger("action");
	auto kind = object->getInteger("kind");
	auto mode = object->getInteger("mode");

	if (!variant || !path || !symbol || !line || !column || !record || !action || !kind || !mode)
		return false;

	if (*line < 1 || *line > std::numeric_limits<unsigned>::max() || *column < 1 ||
		*column > std::numeric_limits<unsigned>::max() || *record < SEMINDEX_DB_DECLARATION ||
		*record > SEMINDEX_DB_REFERENCE || *action < 0 || *action > std::numeric_limits<unsigned>::max() ||
		*kind < SEMINDEX_SYMBOL_VAR || *kind > SEMINDEX_SYMBOL_FILE || *mode < 0 ||
		*mode > std::numeric_limits<unsigned>::max())
		return false;

	result.variant = variant->str();
	result.path = path->str();
	result.symbol = symbol->str();
	result.cursor.record = static_cast<semindex_db_record_type_t>(*record);
	result.cursor.action = *action;
	result.cursor.kind = static_cast<semindex_symbol_kind_t>(*kind);
	result.cursor.mode = *mode;
	result.cursor.line = *line;
	result.cursor.column = *column;
	result.updatePointers();

	return true;
}

int collectRecord(void *data, const semindex_db_record_t *record)
{
	auto &collector = *static_cast<RecordCollector *>(data);

	if (collector.control.stopped()) {
		collector.stopped = true;

		return 1;
	}

	collector.records.push_back(semindexQueryRecord(*record));

	return collector.records.size() > collector.limit;
}

int collectCallRecord(void *data, const semindex_db_record_t *record)
{
	auto &collector = *static_cast<CallCollector *>(data);

	if (collector.control.stopped()) {
		collector.stopped = true;

		return 1;
	}

	SemindexQueryRecord copy = semindexQueryRecord(*record);

	if (!collector.after_seen) {
		if (recordCursor(copy) == collector.after)
			collector.after_seen = true;

		return 0;
	}

	collector.records.push_back(std::move(copy));

	return collector.records.size() > collector.limit;
}

int collectGraphRecord(void *data, const semindex_db_record_t *record)
{
	auto &collector = *static_cast<GraphCollector *>(data);

	if (collector.control.stopped()) {
		collector.stopped = true;

		return 1;
	}

	collector.records.push_back(semindexQueryRecord(*record));

	return collector.records.size() >= collector.limit;
}

int collectVariant(void *data, const semindex_db_variant_t *variant)
{
	auto &collector = *static_cast<VariantCollector *>(data);

	if (collector.control.stopped()) {
		collector.stopped = true;

		return 1;
	}

	if (!collector.after.empty() && collector.after >= variant->name)
		return 0;

	if (!collector.selected.empty() && collector.selected != variant->name)
		return 0;

	collector.variants.push_back(VariantRecord{
		.name = variant->name,
		.repository_root = variant->repository_root ? variant->repository_root : "",
		.git_commit = variant->git_commit ? variant->git_commit : "",
	});

	return collector.variants.size() > collector.limit;
}

McpToolResult stoppedResult(const McpRequestControl &control)
{
	if (control.cancelled)
		return { McpToolStatus::Cancelled, {}, "Request cancelled" };

	return { McpToolStatus::DeadlineExceeded, {}, "Request deadline exceeded" };
}

bool openDatabase(const std::string &path, Database &database)
{
	semindex_db_t *handle = nullptr;

	if (semindex_db_open(path.c_str(), &handle) < 0)
		return false;

	database.reset(handle);

	return true;
}

llvm::json::Object recordsResult(std::vector<SemindexQueryRecord> &records, size_t limit)
{
	bool more = records.size() > limit;

	if (more)
		records.resize(limit);

	llvm::json::Array items;

	items.reserve(records.size());

	for (const auto &record : records)
		items.push_back(recordObject(record));

	llvm::json::Object result{
		{ "records", std::move(items) },
		{ "truncated", more },
	};

	if (more && !records.empty())
		result["nextCursor"] = recordCursor(records.back());

	return result;
}

llvm::json::Object graphResult(std::vector<GraphRecord> &records, size_t limit, bool node_limit_hit,
	bool cycles_detected)
{
	bool more = records.size() > limit;

	if (more)
		records.resize(limit);

	llvm::json::Array items;

	items.reserve(records.size());

	for (const auto &record : records)
		items.push_back(graphRecordObject(record));

	return llvm::json::Object{
		{ "records", std::move(items) },
		{ "truncated", more || node_limit_hit },
		{ "nodeLimitHit", node_limit_hit },
		{ "cyclesDetected", cycles_detected },
	};
}

std::optional<std::string> selectedVariant(const llvm::json::Object *arguments, const std::string &configured)
{
	std::string selected = configured;

	if (arguments) {
		if (auto value = arguments->getString("variant")) {
			if (!configured.empty() && *value != configured)
				return std::nullopt;

			selected = value->str();
		} else if (arguments->get("variant")) {
			return std::nullopt;
		}
	}

	return selected;
}

bool isWithin(const std::filesystem::path &path, const std::filesystem::path &root)
{
	auto relative = path.lexically_relative(root);

	return !relative.empty() && !relative.is_absolute() && *relative.begin() != "..";
}

std::optional<std::filesystem::path> allowedPath(const llvm::json::Object *arguments,
	const std::filesystem::path &workspace)
{
	if (!arguments)
		return std::nullopt;

	auto value = arguments->getString("path");

	if (!value || value->empty())
		return std::nullopt;

	std::filesystem::path path(value->str());

	if (!path.is_absolute())
		path = workspace / path;

	std::error_code error;
	auto resolved = std::filesystem::weakly_canonical(path, error);

	if (error || !isWithin(resolved, workspace))
		return std::nullopt;

	return resolved;
}

McpToolResult searchSymbols(semindex_db_t *database, const llvm::json::Object *arguments,
	const std::string &configured_variant, const McpRequestControl &control)
{
	if (!arguments)
		return { McpToolStatus::InvalidParams, {}, "Arguments are required" };

	auto pattern = arguments->getString("pattern");
	auto selected = selectedVariant(arguments, configured_variant);
	size_t limit;

	if (!pattern || pattern->empty() || !selected || !parseLimit(arguments, limit))
		return { McpToolStatus::InvalidParams, {}, "Invalid search arguments" };

	semindex_db_query_options_t query = {
		.symbol = pattern->data(),
		.variant = selected->empty() ? nullptr : selected->c_str(),
		.limit = limit + 1,
	};
	OwnedCursor cursor;

	if (auto value = arguments->getString("cursor")) {
		if (!parseCursor(*value, cursor))
			return { McpToolStatus::InvalidParams, {}, "Invalid cursor" };

		query.after = &cursor.cursor;
	} else if (arguments->get("cursor")) {
		return { McpToolStatus::InvalidParams, {}, "Invalid cursor" };
	}

	if (auto value = arguments->getString("kind")) {
		auto kind = parseKind(*value);

		if (!kind)
			return { McpToolStatus::InvalidParams, {}, "Invalid symbol kind" };

		query.kind = *kind;
		query.has_kind = 1;
	} else if (arguments->get("kind")) {
		return { McpToolStatus::InvalidParams, {}, "Invalid symbol kind" };
	}

	RecordCollector collector{ control, limit };
	int ret = semindex_db_query(database, &query, collectRecord, &collector);

	if (collector.stopped || control.stopped())
		return stoppedResult(control);

	if (ret < 0)
		return { McpToolStatus::DatabaseError, {}, "Database query failed" };

	return { McpToolStatus::Success, recordsResult(collector.records, limit), {} };
}

McpToolResult symbolAt(semindex_db_t *database, const llvm::json::Object *arguments,
	const std::filesystem::path &workspace, const std::string &configured_variant, const McpRequestControl &control)
{
	auto path = allowedPath(arguments, workspace);
	auto selected = selectedVariant(arguments, configured_variant);

	if (!path || !selected || !arguments)
		return { McpToolStatus::InvalidParams, {}, "Invalid source path or variant" };

	auto line = arguments->getInteger("line");
	auto column = arguments->getInteger("column");

	if (!line || !column || *line < 1 || *column < 1 || *line > std::numeric_limits<unsigned>::max() ||
		*column > std::numeric_limits<unsigned>::max())
		return { McpToolStatus::InvalidParams, {}, "Invalid source position" };

	SemindexSourceResolver sources;

	if (!sources.setWorkspaceRoot(workspace))
		return { McpToolStatus::InvalidParams, {}, "Invalid workspace" };

	SemindexQueryService queries(database, sources);
	SemindexPositionQuery query = {
		.path = path->string(),
		.variant = *selected,
		.line = static_cast<unsigned>(*line),
		.column = static_cast<unsigned>(*column),
	};
	std::vector<SemindexQueryRecord> records;

	if (control.stopped())
		return stoppedResult(control);

	if (queries.recordsAt(query, records) < 0) {
		if (control.stopped())
			return stoppedResult(control);

		return { McpToolStatus::DatabaseError, {}, "Database query failed" };
	}

	return { McpToolStatus::Success, recordsResult(records, MAX_LIMIT), {} };
}

McpToolResult relatedRecords(semindex_db_t *database, const llvm::json::Object *arguments,
	const std::string &configured_variant, semindex_db_record_filter_t filter, const McpRequestControl &control)
{
	if (!arguments)
		return { McpToolStatus::InvalidParams, {}, "Arguments are required" };

	auto symbol = arguments->getString("symbol");
	auto selected = selectedVariant(arguments, configured_variant);
	size_t limit;

	if (!symbol || symbol->empty() || !selected || selected->empty() || !parseLimit(arguments, limit))
		return { McpToolStatus::InvalidParams, {}, "Invalid symbol identity" };

	OwnedCursor cursor;
	const semindex_db_cursor_t *after = nullptr;

	if (auto value = arguments->getString("cursor")) {
		if (!parseCursor(*value, cursor))
			return { McpToolStatus::InvalidParams, {}, "Invalid cursor" };

		after = &cursor.cursor;
	} else if (arguments->get("cursor")) {
		return { McpToolStatus::InvalidParams, {}, "Invalid cursor" };
	}

	RecordCollector collector{ control, limit };
	int ret;

	if (auto value = arguments->getString("usrId")) {
		auto id = parseId(*value);
		auto kind_value = arguments->getString("kind");
		auto kind = kind_value ? parseKind(*kind_value) : std::nullopt;

		if (!id || !*id || !kind)
			return { McpToolStatus::InvalidParams, {}, "Invalid stable identity" };

		semindex_db_identity_t identity = {
			.variant = selected->c_str(),
			.symbol = symbol->data(),
			.usr_id = *id,
			.kind = *kind,
		};
		semindex_db_identity_query_t query = {
			.identity = &identity,
			.after = after,
			.limit = limit + 1,
			.record = filter,
		};

		ret = semindex_db_query_identity(database, &query, collectRecord, &collector);
	} else {
		semindex_db_query_options_t query = {
			.symbol = symbol->data(),
			.variant = selected->c_str(),
			.after = after,
			.limit = limit + 1,
			.record = filter,
		};

		if (auto context = arguments->getString("context"))
			query.context = context->data();

		if (auto local = arguments->getBoolean("local")) {
			query.local = *local;
			query.has_local = 1;
		}

		ret = semindex_db_query(database, &query, collectRecord, &collector);
	}

	if (collector.stopped || control.stopped())
		return stoppedResult(control);

	if (ret < 0)
		return { McpToolStatus::DatabaseError, {}, "Database query failed" };

	return { McpToolStatus::Success, recordsResult(collector.records, limit), {} };
}

McpToolResult callRecords(semindex_db_t *database, const llvm::json::Object *arguments,
	const std::string &configured_variant, semindex_db_call_direction_t direction, const McpRequestControl &control)
{
	if (!arguments)
		return { McpToolStatus::InvalidParams, {}, "Arguments are required" };

	auto symbol = arguments->getString("symbol");
	auto id_value = arguments->getString("usrId");
	auto selected = selectedVariant(arguments, configured_variant);
	size_t limit;
	unsigned depth;
	size_t node_limit;

	if (!symbol || symbol->empty() || !id_value || !selected || selected->empty())
		return { McpToolStatus::InvalidParams, {}, "Invalid function identity" };

	if (!parseLimit(arguments, limit) || !parseGraphBounds(arguments, depth, node_limit))
		return { McpToolStatus::InvalidParams, {}, "Invalid function identity" };

	auto id = parseId(*id_value);

	if (!id || !*id)
		return { McpToolStatus::InvalidParams, {}, "Invalid function identity" };

	if (depth > 1 && arguments->get("cursor"))
		return { McpToolStatus::InvalidParams, {}, "Recursive call queries do not accept a cursor" };

	if (depth > 1) {
		std::deque<GraphFunction> pending;
		std::set<GraphFunctionKey> visited;
		std::vector<GraphRecord> records;
		bool node_limit_hit = false;
		bool cycles_detected = false;

		pending.push_back(GraphFunction{
			.variant = *selected,
			.symbol = symbol->str(),
			.usr_id = *id,
			.depth = 0,
			.ancestors = { GraphFunctionKey{ *selected, symbol->str(), *id } },
		});
		visited.insert(GraphFunctionKey{ *selected, symbol->str(), *id });

		while (!pending.empty() && records.size() <= limit) {
			GraphFunction function = std::move(pending.front());

			pending.pop_front();

			if (function.depth >= depth)
				continue;

			semindex_db_call_options_t query = {
				.function = function.symbol.c_str(),
				.variant = function.variant.c_str(),
				.usr_id = function.usr_id,
				.direction = direction,
			};
			size_t remaining = limit + 1 - records.size();
			GraphCollector collector{ control, remaining };
			int ret = semindex_db_query_calls(database, &query, collectGraphRecord, &collector);

			if (collector.stopped || control.stopped())
				return stoppedResult(control);

			if (ret < 0)
				return { McpToolStatus::DatabaseError, {}, "Database query failed" };

			for (auto &record : collector.records) {
				GraphFunction next;

				if (direction == SEMINDEX_DB_CALLERS) {
					next = GraphFunction{
						.variant = record.variant,
						.symbol = record.context,
						.usr_id = record.context_usr_id,
						.depth = function.depth + 1,
					};
				} else {
					next = GraphFunction{
						.variant = record.variant,
						.symbol = record.symbol,
						.usr_id = record.usr_id,
						.depth = function.depth + 1,
					};
				}

				GraphFunctionKey key{ next.variant, next.symbol, next.usr_id };
				bool cycle = function.ancestors.find(key) != function.ancestors.end();

				if (cycle) {
					cycles_detected = true;
				} else if (visited.find(key) == visited.end()) {
					if (visited.size() >= node_limit) {
						node_limit_hit = true;

						continue;
					}

					next.ancestors = function.ancestors;
					next.ancestors.insert(key);
					visited.insert(key);
					pending.push_back(std::move(next));
				}

				records.push_back(GraphRecord{
					.record = std::move(record),
					.depth = function.depth + 1,
				});

				if (records.size() > limit)
					break;
			}
		}

		return { McpToolStatus::Success, graphResult(records, limit, node_limit_hit, cycles_detected), {} };
	}

	semindex_db_call_options_t query = {
		.function = symbol->data(),
		.variant = selected->c_str(),
		.usr_id = *id,
		.direction = direction,
	};
	std::string cursor;

	if (auto value = arguments->getString("cursor"))
		cursor = value->str();
	else if (arguments->get("cursor"))
		return { McpToolStatus::InvalidParams, {}, "Invalid cursor" };

	CallCollector collector{ control, limit, cursor, {}, cursor.empty() };
	int ret = semindex_db_query_calls(database, &query, collectCallRecord, &collector);

	if (collector.stopped || control.stopped())
		return stoppedResult(control);

	if (ret < 0)
		return { McpToolStatus::DatabaseError, {}, "Database query failed" };

	if (!collector.after_seen)
		return { McpToolStatus::InvalidParams, {}, "Cursor does not match this query" };

	return { McpToolStatus::Success, recordsResult(collector.records, limit), {} };
}

McpToolResult listVariants(semindex_db_t *database, const llvm::json::Object *arguments,
	const std::string &configured_variant, const McpRequestControl &control)
{
	size_t limit;

	if (!parseLimit(arguments, limit))
		return { McpToolStatus::InvalidParams, {}, "Invalid limit" };

	VariantCollector collector{ control, configured_variant, {}, limit };

	if (arguments) {
		if (auto cursor = arguments->getString("cursor"))
			collector.after = cursor->str();
		else if (arguments->get("cursor"))
			return { McpToolStatus::InvalidParams, {}, "Invalid cursor" };
	}

	int ret = semindex_db_list_variants(database, collectVariant, &collector);

	if (collector.stopped || control.stopped())
		return stoppedResult(control);

	if (ret < 0)
		return { McpToolStatus::DatabaseError, {}, "Database query failed" };

	bool more = collector.variants.size() > limit;

	if (more)
		collector.variants.resize(limit);

	llvm::json::Array variants;

	for (const auto &variant : collector.variants) {
		llvm::json::Object value{
			{ "name", variant.name },
		};

		if (!variant.repository_root.empty())
			value["repositoryRoot"] = variant.repository_root;

		if (!variant.git_commit.empty())
			value["gitCommit"] = variant.git_commit;

		variants.push_back(std::move(value));
	}

	llvm::json::Object result{
		{ "variants", std::move(variants) },
		{ "truncated", more },
	};

	if (more && !collector.variants.empty())
		result["nextCursor"] = collector.variants.back().name;

	return { McpToolStatus::Success, std::move(result), {} };
}

const char *sourceStatusName(SemindexSourceStatus status)
{
	switch (status) {
	case SemindexSourceStatus::Current:
		return "current";
	case SemindexSourceStatus::NotIndexed:
		return "not-indexed";
	case SemindexSourceStatus::Missing:
		return "missing";
	case SemindexSourceStatus::Drifted:
		return "drifted";
	}

	return "unknown";
}

const char *sourceOriginName(SemindexSourceOrigin origin)
{
	switch (origin) {
	case SemindexSourceOrigin::None:
		return "none";
	case SemindexSourceOrigin::WorkingTree:
		return "working-tree";
	case SemindexSourceOrigin::GitCommit:
		return "git-commit";
	}

	return "unknown";
}

McpToolResult sourceRequest(semindex_db_t *database, const llvm::json::Object *arguments,
	const std::filesystem::path &workspace, const std::string &configured_variant, bool include_source,
	const McpRequestControl &control)
{
	auto path = allowedPath(arguments, workspace);
	auto selected = selectedVariant(arguments, configured_variant);

	if (!path || !selected || selected->empty() || !arguments)
		return { McpToolStatus::InvalidParams, {}, "Invalid source path or variant" };

	unsigned first_line = 1;
	unsigned line_count = include_source ? DEFAULT_CONTEXT_LINES : 1;
	size_t byte_limit = include_source ? DEFAULT_CONTEXT_BYTES : 1;

	if (auto value = arguments->getInteger("firstLine")) {
		if (*value < 1 || *value > std::numeric_limits<unsigned>::max())
			return { McpToolStatus::InvalidParams, {}, "Invalid first line" };

		first_line = *value;
	}

	if (include_source) {
		if (auto value = arguments->getInteger("lineCount")) {
			if (*value < 1 || *value > MAX_CONTEXT_LINES)
				return { McpToolStatus::InvalidParams, {}, "Invalid line count" };

			line_count = *value;
		}

		if (auto value = arguments->getInteger("byteLimit")) {
			if (*value < 1 || *value > MAX_CONTEXT_BYTES)
				return { McpToolStatus::InvalidParams, {}, "Invalid byte limit" };

			byte_limit = *value;
		}
	}

	if (control.stopped())
		return stoppedResult(control);

	SemindexSourceResolver sources;

	if (!sources.setWorkspaceRoot(workspace))
		return { McpToolStatus::InvalidParams, {}, "Invalid workspace" };

	SemindexSourceResult source;
	int ret = -1;
	std::string indexed_path;

	for (const auto &candidate : sources.databasePaths(*path)) {
		SemindexSourceRequest request = {
			.database = database,
			.variant = *selected,
			.path = candidate,
			.first_line = first_line,
			.line_count = line_count,
			.byte_limit = byte_limit,
		};

		ret = sources.readSource(request, source);

		if (ret < 0 || source.status != SemindexSourceStatus::NotIndexed) {
			indexed_path = candidate;

			break;
		}
	}

	if (control.stopped())
		return stoppedResult(control);

	if (ret < 0)
		return { McpToolStatus::DatabaseError, {}, "Source lookup failed" };

	llvm::json::Object result{
		{ "variant", *selected },
		{ "path", indexed_path.empty() ? path->string() : indexed_path },
		{ "status", sourceStatusName(source.status) },
		{ "origin", sourceOriginName(source.origin) },
		{ "drifted", source.drifted },
	};

	if (include_source) {
		llvm::json::Array lines;

		for (const auto &line : source.lines)
			lines.push_back(line);

		result["firstLine"] = source.first_line;
		result["lines"] = std::move(lines);
		result["byteLimitHit"] = source.byte_limit_hit;
	}

	return { McpToolStatus::Success, std::move(result), {} };
}

} // namespace

bool McpRequestControl::stopped() const
{
	return cancelled || timed_out || std::chrono::steady_clock::now() >= deadline;
}

void McpRequestControl::bind(semindex_db_t *handle)
{
	std::lock_guard<std::mutex> guard(database_mutex);

	database = handle;

	if (stopped())
		semindex_db_interrupt(database);
}

void McpRequestControl::unbind()
{
	std::lock_guard<std::mutex> guard(database_mutex);

	database = nullptr;
}

void McpRequestControl::cancel()
{
	cancelled = true;
	std::lock_guard<std::mutex> guard(database_mutex);

	if (database)
		semindex_db_interrupt(database);
}

void McpRequestControl::waitForDeadline()
{
	std::unique_lock<std::mutex> lock(completion_mutex);

	if (completion_changed.wait_until(lock, deadline, [&] { return completed; }))
		return;

	timed_out = true;
	lock.unlock();
	std::lock_guard<std::mutex> guard(database_mutex);

	if (database)
		semindex_db_interrupt(database);
}

void McpRequestControl::complete()
{
	{
		std::lock_guard<std::mutex> guard(completion_mutex);

		completed = true;
	}

	completion_changed.notify_all();
}

McpToolService::McpToolService(std::string database, std::filesystem::path workspace, std::string variant)
    : database(std::move(database)), workspace(std::move(workspace)), variant(std::move(variant))
{
}

McpToolResult McpToolService::call(llvm::StringRef name, const llvm::json::Object *arguments,
	McpRequestControl &control) const
{
	Database handle;

	if (control.stopped())
		return stoppedResult(control);

	if (!openDatabase(database, handle))
		return { McpToolStatus::DatabaseError, {}, "Failed to open database" };

	control.bind(handle.get());

	struct Unbind {
		McpRequestControl &control;

		~Unbind()
		{
			control.unbind();
		}
	} unbind{ control };

	if (control.stopped())
		return stoppedResult(control);

	if (name == "search_symbols")
		return searchSymbols(handle.get(), arguments, variant, control);

	if (name == "symbol_at")
		return symbolAt(handle.get(), arguments, workspace, variant, control);

	if (name == "find_definitions")
		return relatedRecords(handle.get(), arguments, variant, SEMINDEX_DB_RECORD_DEFINITION, control);

	if (name == "find_references")
		return relatedRecords(handle.get(), arguments, variant, SEMINDEX_DB_RECORD_REFERENCE, control);

	if (name == "find_callers")
		return callRecords(handle.get(), arguments, variant, SEMINDEX_DB_CALLERS, control);

	if (name == "find_callees")
		return callRecords(handle.get(), arguments, variant, SEMINDEX_DB_CALLEES, control);

	if (name == "read_source_context")
		return sourceRequest(handle.get(), arguments, workspace, variant, true, control);

	if (name == "list_variants")
		return listVariants(handle.get(), arguments, variant, control);

	if (name == "index_status")
		return sourceRequest(handle.get(), arguments, workspace, variant, false, control);

	return { McpToolStatus::UnknownTool, {}, "Unknown tool" };
}
