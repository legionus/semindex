// SPDX-License-Identifier: GPL-2.0-or-later
#include "mcp_tools.h"
#include "mcp_tools_internal.h"

#include "index_updater.h"
#include "semantic_query.h"
#include "semantic_names.h"
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

namespace McpTools
{

struct DatabaseCloser {
	void operator()(semindex_db_t *database) const
	{
		semindex_db_close(database);
	}
};

using Database = std::unique_ptr<semindex_db_t, DatabaseCloser>;

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
		{ "kind", semindex_symbol_kind_name(record.kind) },
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

} // namespace McpTools

using namespace McpTools;

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

McpToolService::McpToolService(McpToolOptions options)
    : options(std::move(options)), updater(std::make_unique<SemindexIndexUpdater>(SemindexIndexUpdaterOptions{
					   .database = this->options.database,
					   .repository_root = this->options.workspace.string(),
					   .command_resolver = {
						   .commands_database = this->options.commands_database,
					   },
					   .include_local = this->options.include_local,
				   }))
{
}

McpToolService::~McpToolService() = default;

bool McpToolService::canReindex() const
{
	return options.allow_reindex;
}

McpToolResult McpToolService::call(llvm::StringRef name, const llvm::json::Object *arguments,
	McpRequestControl &control) const
{
	Database handle;

	if (control.stopped())
		return stoppedResult(control);

	if (name == "reindex_file") {
		if (!options.allow_reindex)
			return { McpToolStatus::UnknownTool, {}, "Unknown tool" };

		return reindexFile(arguments, options.workspace, options.variant, *updater, control);
	}

	if (!openDatabase(options.database, handle))
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
		return searchSymbols(handle.get(), arguments, options.variant, control);

	if (name == "symbol_at")
		return symbolAt(handle.get(), arguments, options.workspace, options.variant, control);

	if (name == "find_definitions")
		return relatedRecords(handle.get(), arguments, options.variant, SEMINDEX_DB_RECORD_DEFINITION, control);

	if (name == "find_references")
		return relatedRecords(handle.get(), arguments, options.variant, SEMINDEX_DB_RECORD_REFERENCE, control);

	if (name == "find_declared_types")
		return declaredTypes(handle.get(), arguments, options.variant, control);

	if (name == "find_function_signature")
		return functionSignature(handle.get(), arguments, options.variant, control);

	if (name == "find_callers")
		return callRecords(handle.get(), arguments, options.variant, SEMINDEX_DB_CALLERS, control);

	if (name == "find_callees")
		return callRecords(handle.get(), arguments, options.variant, SEMINDEX_DB_CALLEES, control);

	if (name == "read_source_context")
		return sourceRequest(handle.get(), arguments, options.workspace, options.variant, true, control);

	if (name == "list_variants")
		return listVariants(handle.get(), arguments, options.variant, control);

	if (name == "index_status")
		return indexStatus(handle.get(), arguments, options.workspace, options.variant, *updater, control);

	return { McpToolStatus::UnknownTool, {}, "Unknown tool" };
}
