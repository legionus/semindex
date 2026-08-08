// SPDX-License-Identifier: GPL-2.0-or-later
#include "mcp_tools_internal.h"

#include "semantic_names.h"
#include "source_resolver.h"

#include <limits>
#include <optional>
#include <utility>
#include <vector>

static constexpr size_t MAX_LIMIT = 200;

namespace McpTools
{

struct RecordCollector {
	const McpRequestControl &control;
	size_t limit;
	std::vector<SemindexQueryRecord> records;
	bool stopped = false;
};

struct SymbolTypeRecord {
	std::string variant;
	std::string path;
	std::string symbol;
	std::string declared_type;
	std::string canonical_type;
	std::string type_symbol;
	semindex_symbol_kind_t kind;
	int type_kind;
	unsigned long long usr_id;
	unsigned long long type_usr_id;
	bool has_type_identity;
};

struct SymbolTypeCollector {
	const McpRequestControl &control;
	std::vector<SymbolTypeRecord> records;
	bool stopped = false;
};

struct OwnedTypeCursor {
	std::string declared_type;
	std::string canonical_type;
	std::string type_symbol;
	int type_kind = -1;
	unsigned long long type_usr_id = 0;
	std::string path;
	semindex_db_symbol_type_cursor_t cursor = {};

	void updatePointers()
	{
		cursor.declared_type = declared_type.c_str();
		cursor.canonical_type = canonical_type.c_str();
		cursor.type_symbol = type_symbol.c_str();
		cursor.type_kind = type_kind;
		cursor.type_usr_id = type_usr_id;
		cursor.path = path.c_str();
	}
};

struct FunctionTypeRecord {
	std::string variant;
	std::string path;
	std::string symbol;
	std::string name;
	std::string declared_type;
	std::string canonical_type;
	std::string type_symbol;
	int position;
	int type_kind;
	unsigned long long usr_id;
	unsigned long long type_usr_id;
	bool variadic;
	bool has_type_identity;
};

struct FunctionTypeCollector {
	const McpRequestControl &control;
	std::vector<FunctionTypeRecord> records;
	bool stopped = false;
};

struct OwnedFunctionTypeCursor {
	std::string declared_type;
	std::string canonical_type;
	std::string type_symbol;
	int position = -1;
	int type_kind = -1;
	unsigned long long type_usr_id = 0;
	std::string path;
	semindex_db_function_type_cursor_t cursor = {};

	void updatePointers()
	{
		cursor.position = position;
		cursor.declared_type = declared_type.c_str();
		cursor.canonical_type = canonical_type.c_str();
		cursor.type_symbol = type_symbol.c_str();
		cursor.type_kind = type_kind;
		cursor.type_usr_id = type_usr_id;
		cursor.path = path.c_str();
	}
};

std::optional<semindex_symbol_kind_t> parseKind(llvm::StringRef value)
{
	semindex_symbol_kind_t kind;
	std::string name = value.str();

	if (semindex_symbol_kind_parse(name.c_str(), &kind) == 0)
		return kind;

	return std::nullopt;
}

std::string typeCursor(const SymbolTypeRecord &record)
{
	llvm::json::Object cursor{
		{ "declaredType", record.declared_type },
		{ "canonicalType", record.canonical_type },
		{ "typeSymbol", record.type_symbol },
		{ "typeKind", static_cast<int64_t>(record.type_kind) },
		{ "typeUsrId", idString(record.type_usr_id) },
		{ "path", record.path },
	};
	std::string serialized;
	llvm::raw_string_ostream stream(serialized);

	stream << llvm::json::Value(std::move(cursor));
	stream.flush();

	return encodeHex(serialized);
}

std::string functionTypeCursor(const FunctionTypeRecord &record)
{
	llvm::json::Object cursor{
		{ "position", record.position },
		{ "declaredType", record.declared_type },
		{ "canonicalType", record.canonical_type },
		{ "typeSymbol", record.type_symbol },
		{ "typeKind", static_cast<int64_t>(record.type_kind) },
		{ "typeUsrId", idString(record.type_usr_id) },
		{ "path", record.path },
	};
	std::string serialized;
	llvm::raw_string_ostream stream(serialized);

	stream << llvm::json::Value(std::move(cursor));
	stream.flush();

	return encodeHex(serialized);
}

bool parseTypeCursor(llvm::StringRef encoded, OwnedTypeCursor &result)
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

	auto declared_type = object->getString("declaredType");
	auto canonical_type = object->getString("canonicalType");
	auto type_symbol = object->getString("typeSymbol");
	auto type_kind = object->getInteger("typeKind");
	auto type_usr_id = object->getString("typeUsrId");
	auto path = object->getString("path");

	if (!declared_type || !canonical_type || !type_symbol || !type_kind || !type_usr_id || !path)
		return false;

	auto parsed_type_usr_id = parseId(*type_usr_id);

	if (!parsed_type_usr_id || *type_kind < -1 || *type_kind > SEMINDEX_SYMBOL_FILE)
		return false;

	result.declared_type = declared_type->str();
	result.canonical_type = canonical_type->str();
	result.type_symbol = type_symbol->str();
	result.type_kind = *type_kind;
	result.type_usr_id = *parsed_type_usr_id;
	result.path = path->str();
	result.updatePointers();

	return true;
}

bool parseFunctionTypeCursor(llvm::StringRef encoded, OwnedFunctionTypeCursor &result)
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

	auto position = object->getInteger("position");
	auto declared_type = object->getString("declaredType");
	auto canonical_type = object->getString("canonicalType");
	auto type_symbol = object->getString("typeSymbol");
	auto type_kind = object->getInteger("typeKind");
	auto type_usr_id = object->getString("typeUsrId");
	auto path = object->getString("path");

	if (!position || !type_kind || !type_usr_id)
		return false;

	if (!declared_type || !canonical_type || !type_symbol || !path)
		return false;

	auto parsed_type_usr_id = parseId(*type_usr_id);

	if (!parsed_type_usr_id || *position < -1 || *position > std::numeric_limits<int>::max())
		return false;

	if (*type_kind < -1 || *type_kind > SEMINDEX_SYMBOL_FILE)
		return false;

	result.position = *position;
	result.declared_type = declared_type->str();
	result.canonical_type = canonical_type->str();
	result.type_symbol = type_symbol->str();
	result.type_kind = *type_kind;
	result.type_usr_id = *parsed_type_usr_id;
	result.path = path->str();
	result.updatePointers();

	return true;
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

int collectSymbolType(void *data, const semindex_db_symbol_type_t *type)
{
	auto &collector = *static_cast<SymbolTypeCollector *>(data);

	if (collector.control.stopped()) {
		collector.stopped = true;

		return 1;
	}

	collector.records.push_back(SymbolTypeRecord{
		.variant = type->variant,
		.path = type->path,
		.symbol = type->symbol,
		.declared_type = type->declared_type,
		.canonical_type = type->canonical_type,
		.type_symbol = type->type_symbol,
		.kind = type->kind,
		.type_kind = type->type_kind,
		.usr_id = type->usr_id,
		.type_usr_id = type->type_usr_id,
		.has_type_identity = type->has_type_identity != 0,
	});

	return 0;
}

int collectFunctionType(void *data, const semindex_db_function_type_t *type)
{
	auto &collector = *static_cast<FunctionTypeCollector *>(data);

	if (collector.control.stopped()) {
		collector.stopped = true;

		return 1;
	}

	collector.records.push_back(FunctionTypeRecord{
		.variant = type->variant,
		.path = type->path,
		.symbol = type->symbol,
		.name = type->name,
		.declared_type = type->declared_type,
		.canonical_type = type->canonical_type,
		.type_symbol = type->type_symbol,
		.position = type->position,
		.type_kind = type->type_kind,
		.usr_id = type->usr_id,
		.type_usr_id = type->type_usr_id,
		.variadic = type->variadic != 0,
		.has_type_identity = type->has_type_identity != 0,
	});

	return 0;
}

llvm::json::Object symbolTypesResult(std::vector<SymbolTypeRecord> &records, size_t limit)
{
	bool more = records.size() > limit;

	if (more)
		records.resize(limit);

	llvm::json::Array items;

	items.reserve(records.size());

	for (const auto &record : records) {
		llvm::json::Object item{
			{ "variant", record.variant },
			{ "path", record.path },
			{ "symbol", record.symbol },
			{ "kind", semindex_symbol_kind_name(record.kind) },
			{ "usrId", idString(record.usr_id) },
			{ "declaredType", record.declared_type },
			{ "canonicalType", record.canonical_type },
		};

		if (record.has_type_identity) {
			item["typeIdentity"] = llvm::json::Object{
				{ "variant", record.variant },
				{ "symbol", record.type_symbol },
				{ "kind",
					semindex_symbol_kind_name(
						static_cast<semindex_symbol_kind_t>(record.type_kind)) },
				{ "usrId", idString(record.type_usr_id) },
			};
		}

		items.push_back(std::move(item));
	}

	llvm::json::Object result{
		{ "types", std::move(items) },
		{ "truncated", more },
	};

	if (more && !records.empty())
		result["nextCursor"] = typeCursor(records.back());

	return result;
}

llvm::json::Object functionTypesResult(std::vector<FunctionTypeRecord> &records, size_t limit)
{
	bool more = records.size() > limit;

	if (more)
		records.resize(limit);

	llvm::json::Array items;

	items.reserve(records.size());

	for (const auto &record : records) {
		llvm::json::Object item{
			{ "variant", record.variant },
			{ "path", record.path },
			{ "symbol", record.symbol },
			{ "usrId", idString(record.usr_id) },
			{ "position", record.position },
			{ "name", record.name },
			{ "declaredType", record.declared_type },
			{ "canonicalType", record.canonical_type },
			{ "variadic", record.variadic },
		};

		if (record.has_type_identity) {
			item["typeIdentity"] = llvm::json::Object{
				{ "variant", record.variant },
				{ "symbol", record.type_symbol },
				{ "kind",
					semindex_symbol_kind_name(
						static_cast<semindex_symbol_kind_t>(record.type_kind)) },
				{ "usrId", idString(record.type_usr_id) },
			};
		}

		items.push_back(std::move(item));
	}

	llvm::json::Object result{
		{ "types", std::move(items) },
		{ "truncated", more },
	};

	if (more && !records.empty())
		result["nextCursor"] = functionTypeCursor(records.back());

	return result;
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

McpToolResult declaredTypes(semindex_db_t *database, const llvm::json::Object *arguments,
	const std::string &configured_variant, const McpRequestControl &control)
{
	if (!arguments)
		return { McpToolStatus::InvalidParams, {}, "Arguments are required" };

	auto symbol = arguments->getString("symbol");
	auto id_value = arguments->getString("usrId");
	auto kind_value = arguments->getString("kind");
	auto selected = selectedVariant(arguments, configured_variant);
	size_t limit;

	if (!symbol || symbol->empty() || !id_value || !kind_value || !selected || selected->empty())
		return { McpToolStatus::InvalidParams, {}, "Invalid symbol identity" };

	if (!parseLimit(arguments, limit))
		return { McpToolStatus::InvalidParams, {}, "Invalid limit" };

	auto id = parseId(*id_value);
	auto kind = parseKind(*kind_value);

	if (!id || !*id || !kind)
		return { McpToolStatus::InvalidParams, {}, "Invalid symbol identity" };

	semindex_db_identity_t identity = {
		.variant = selected->c_str(),
		.symbol = symbol->data(),
		.usr_id = *id,
		.kind = *kind,
	};
	semindex_db_symbol_type_query_t query = {
		.identity = &identity,
		.limit = limit + 1,
	};
	OwnedTypeCursor cursor;

	if (auto value = arguments->getString("cursor")) {
		if (!parseTypeCursor(*value, cursor))
			return { McpToolStatus::InvalidParams, {}, "Invalid cursor" };

		query.after = &cursor.cursor;
	} else if (arguments->get("cursor")) {
		return { McpToolStatus::InvalidParams, {}, "Invalid cursor" };
	}

	SymbolTypeCollector collector{ control };
	int ret = semindex_db_query_symbol_types(database, &query, collectSymbolType, &collector);

	if (collector.stopped || control.stopped())
		return stoppedResult(control);

	if (ret < 0)
		return { McpToolStatus::DatabaseError, {}, "Database query failed" };

	return { McpToolStatus::Success, symbolTypesResult(collector.records, limit), {} };
}

McpToolResult functionSignature(semindex_db_t *database, const llvm::json::Object *arguments,
	const std::string &configured_variant, const McpRequestControl &control)
{
	if (!arguments)
		return { McpToolStatus::InvalidParams, {}, "Arguments are required" };

	auto symbol = arguments->getString("symbol");
	auto id_value = arguments->getString("usrId");
	auto selected = selectedVariant(arguments, configured_variant);
	size_t limit;

	if (!symbol || symbol->empty() || !id_value || !selected || selected->empty())
		return { McpToolStatus::InvalidParams, {}, "Invalid function identity" };

	if (!parseLimit(arguments, limit))
		return { McpToolStatus::InvalidParams, {}, "Invalid limit" };

	auto id = parseId(*id_value);

	if (!id || !*id)
		return { McpToolStatus::InvalidParams, {}, "Invalid function identity" };

	semindex_db_identity_t identity = {
		.variant = selected->c_str(),
		.symbol = symbol->data(),
		.usr_id = *id,
		.kind = SEMINDEX_SYMBOL_FUNCTION,
	};
	semindex_db_function_type_query_t query = {
		.identity = &identity,
		.limit = limit + 1,
	};
	OwnedFunctionTypeCursor cursor;

	if (auto value = arguments->getString("cursor")) {
		if (!parseFunctionTypeCursor(*value, cursor))
			return { McpToolStatus::InvalidParams, {}, "Invalid cursor" };

		query.after = &cursor.cursor;
	} else if (arguments->get("cursor")) {
		return { McpToolStatus::InvalidParams, {}, "Invalid cursor" };
	}

	FunctionTypeCollector collector{ control };
	int ret = semindex_db_query_function_types(database, &query, collectFunctionType, &collector);

	if (collector.stopped || control.stopped())
		return stoppedResult(control);

	if (ret < 0)
		return { McpToolStatus::DatabaseError, {}, "Database query failed" };

	return { McpToolStatus::Success, functionTypesResult(collector.records, limit), {} };
}

} // namespace McpTools
