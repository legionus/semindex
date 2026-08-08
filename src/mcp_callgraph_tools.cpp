// SPDX-License-Identifier: GPL-2.0-or-later
#include "mcp_tools_internal.h"

#include <deque>
#include <set>
#include <tuple>
#include <utility>
#include <vector>

namespace McpTools
{

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

llvm::json::Object graphRecordObject(const GraphRecord &record)
{
	auto result = recordObject(record.record);

	result["depth"] = record.depth;

	return result;
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

} // namespace McpTools
