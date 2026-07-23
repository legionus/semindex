// SPDX-License-Identifier: GPL-2.0-or-later
#include "lsp_server.h"

#include <llvm/Support/Error.h>

#include <limits>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

static constexpr int PARSE_ERROR = -32700;
static constexpr int INVALID_REQUEST = -32600;
static constexpr int METHOD_NOT_FOUND = -32601;
static constexpr int INVALID_PARAMS = -32602;
static constexpr int INTERNAL_ERROR = -32603;
static constexpr int SERVER_NOT_INITIALIZED = -32002;
static constexpr int DOCUMENT_HIGHLIGHT_TEXT = 1;
static constexpr int DOCUMENT_HIGHLIGHT_READ = 2;
static constexpr int DOCUMENT_HIGHLIGHT_WRITE = 3;

struct CursorRecord {
	std::string variant;
	std::string path;
	std::string symbol;
	std::string context;
	int local;
};

struct LocationCollector {
	const LspSourceMapper &sources;
	llvm::json::Array locations;
	std::set<std::string> keys;
	LspSourceMapper::Cache source_cache;
	unsigned records = 0;
};

struct CursorCollector {
	std::vector<CursorRecord> records;
	std::set<std::string> keys;
};

struct HighlightRecord {
	llvm::json::Value range;
	int kind;
};

struct HighlightCollector {
	const LspSourceMapper &sources;
	std::vector<HighlightRecord> highlights;
	std::map<std::string, size_t> indices;
	LspSourceMapper::Cache source_cache;
};

static int collectCursorRecord(void *data, const semindex_db_record_t *record)
{
	auto &collector = *static_cast<CursorCollector *>(data);
	CursorRecord copy;
	copy.variant = record->variant;
	copy.path = record->path;
	copy.symbol = record->symbol;
	copy.context = record->context;
	copy.local = record->local;
	std::string key = copy.variant + '\n' + copy.path + '\n' + copy.symbol + '\n' + copy.context + '\n' +
		std::to_string(copy.local);

	if (collector.keys.insert(std::move(key)).second)
		collector.records.push_back(std::move(copy));
	return 0;
}

static int collectLocation(void *data, const semindex_db_record_t *record)
{
	auto &collector = *static_cast<LocationCollector *>(data);
	std::string key =
		std::string(record->path) + ':' + std::to_string(record->line) + ':' + std::to_string(record->column);

	collector.records++;
	if (collector.keys.insert(std::move(key)).second)
		collector.locations.push_back(collector.sources.location(*record, collector.source_cache));
	return 0;
}

static int highlightKind(const semindex_db_record_t &record)
{
	if (record.record != SEMINDEX_DB_REFERENCE)
		return DOCUMENT_HIGHLIGHT_TEXT;
	if (record.action == SEMINDEX_USE_READ)
		return DOCUMENT_HIGHLIGHT_READ;
	if (record.action == SEMINDEX_USE_WRITE)
		return DOCUMENT_HIGHLIGHT_WRITE;
	return DOCUMENT_HIGHLIGHT_TEXT;
}

static int collectHighlight(void *data, const semindex_db_record_t *record)
{
	auto &collector = *static_cast<HighlightCollector *>(data);
	std::string key =
		std::string(record->path) + ':' + std::to_string(record->line) + ':' + std::to_string(record->column);
	int kind = highlightKind(*record);
	auto [entry, inserted] = collector.indices.emplace(std::move(key), collector.highlights.size());

	if (inserted) {
		collector.highlights.push_back({ collector.sources.range(*record, collector.source_cache), kind });
	} else if (kind > collector.highlights[entry->second].kind) {
		collector.highlights[entry->second].kind = kind;
	}
	return 0;
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

static void setWorkspaceRoot(LspSourceMapper &sources, const llvm::json::Object *params)
{
	if (!params)
		return;
	if (auto root_uri = params->getString("rootUri")) {
		if (sources.setRootUri(*root_uri))
			return;
	}

	const llvm::json::Array *folders = params->getArray("workspaceFolders");
	if (!folders)
		return;
	for (const llvm::json::Value &folder : *folders) {
		const llvm::json::Object *object = folder.getAsObject();
		if (!object)
			continue;
		if (auto uri = object->getString("uri")) {
			if (sources.setRootUri(*uri))
				return;
		}
	}
}

LspServer::LspServer(LspTransport &transport, semindex_db_t *database, LspIndexer &indexer, std::string variant)
    : transport(transport), database(database), indexer(indexer), variant(std::move(variant)),
      call_hierarchy(database, sources, this->variant)
{
}

static int querySymbol(semindex_db_t *database, const CursorRecord &cursor, semindex_db_record_filter_t filter,
	LocationCollector &collector)
{
	semindex_db_query_options_t options = {};
	options.symbol = cursor.symbol.c_str();
	options.path = cursor.local ? cursor.path.c_str() : nullptr;
	options.variant = cursor.variant.c_str();
	options.context = cursor.local ? cursor.context.c_str() : nullptr;
	options.record = filter;
	options.has_local = 1;
	options.local = cursor.local;

	return semindex_db_query(database, &options, collectLocation, &collector);
}

static int symbolsAt(semindex_db_t *database, const std::string &variant, const LspSourceMapper &sources,
	llvm::StringRef uri, unsigned line, unsigned character, std::vector<CursorRecord> &records)
{
	auto column = sources.byteColumn(uri, line, character);
	if (!column)
		return 0;

	CursorCollector collector;
	for (const auto &path : sources.databasePaths(uri)) {
		if (semindex_db_find_at(database, path.c_str(), variant.empty() ? nullptr : variant.c_str(), line + 1,
			    *column, collectCursorRecord, &collector) < 0)
			return -1;
		if (!collector.records.empty())
			break;
	}
	records = std::move(collector.records);
	return 0;
}

bool LspServer::definition(const llvm::json::Value &id, const llvm::json::Object *params)
{
	llvm::StringRef uri;
	unsigned line;
	unsigned character;

	if (!parsePosition(params, uri, line, character))
		return error(&id, INVALID_PARAMS, "Invalid params");
	std::vector<CursorRecord> cursors;
	if (symbolsAt(database, variant, sources, uri, line, character, cursors) < 0)
		return error(&id, INTERNAL_ERROR, "Database query failed");
	if (cursors.empty())
		return reply(id, nullptr);

	LocationCollector collector{ sources };
	for (const auto &cursor : cursors) {
		unsigned before = collector.records;
		if (querySymbol(database, cursor, SEMINDEX_DB_RECORD_DEFINITION, collector) < 0)
			return error(&id, INTERNAL_ERROR, "Database query failed");
		if (collector.records == before &&
			querySymbol(database, cursor, SEMINDEX_DB_RECORD_DECLARATION, collector) < 0)
			return error(&id, INTERNAL_ERROR, "Database query failed");
	}
	if (collector.locations.empty())
		return reply(id, nullptr);
	return reply(id, std::move(collector.locations));
}

bool LspServer::references(const llvm::json::Value &id, const llvm::json::Object *params)
{
	llvm::StringRef uri;
	unsigned line;
	unsigned character;

	if (!parsePosition(params, uri, line, character))
		return error(&id, INVALID_PARAMS, "Invalid params");
	std::vector<CursorRecord> cursors;
	if (symbolsAt(database, variant, sources, uri, line, character, cursors) < 0)
		return error(&id, INTERNAL_ERROR, "Database query failed");
	LocationCollector collector{ sources };
	for (const auto &cursor : cursors) {
		if (querySymbol(database, cursor, SEMINDEX_DB_RECORD_REFERENCE, collector) < 0)
			return error(&id, INTERNAL_ERROR, "Database query failed");
	}

	bool include_declaration = false;
	if (const llvm::json::Object *context = params->getObject("context")) {
		if (auto include = context->getBoolean("includeDeclaration"))
			include_declaration = *include;
	}
	if (include_declaration) {
		for (const auto &cursor : cursors) {
			if (querySymbol(database, cursor, SEMINDEX_DB_RECORD_SYMBOL, collector) < 0)
				return error(&id, INTERNAL_ERROR, "Database query failed");
		}
	}
	return reply(id, std::move(collector.locations));
}

bool LspServer::documentHighlight(const llvm::json::Value &id, const llvm::json::Object *params)
{
	llvm::StringRef uri;
	unsigned line;
	unsigned character;

	if (!parsePosition(params, uri, line, character))
		return error(&id, INVALID_PARAMS, "Invalid params");
	std::vector<CursorRecord> cursors;
	if (symbolsAt(database, variant, sources, uri, line, character, cursors) < 0)
		return error(&id, INTERNAL_ERROR, "Database query failed");

	HighlightCollector collector{ sources };
	for (const auto &cursor : cursors) {
		semindex_db_query_options_t options = {};
		options.symbol = cursor.symbol.c_str();
		options.path = cursor.path.c_str();
		options.variant = cursor.variant.c_str();
		options.context = cursor.local ? cursor.context.c_str() : nullptr;
		options.record = SEMINDEX_DB_RECORD_ALL;
		options.has_local = 1;
		options.local = cursor.local;
		if (semindex_db_query(database, &options, collectHighlight, &collector) < 0)
			return error(&id, INTERNAL_ERROR, "Database query failed");
	}

	llvm::json::Array result;
	result.reserve(collector.highlights.size());
	for (auto &highlight : collector.highlights) {
		result.push_back(llvm::json::Object{
			{ "range", std::move(highlight.range) },
			{ "kind", highlight.kind },
		});
	}
	return reply(id, std::move(result));
}

bool LspServer::didSave(const llvm::json::Object *params)
{
	const llvm::json::Object *document = params ? params->getObject("textDocument") : nullptr;
	std::optional<std::string> file;
	std::string message;

	if (document) {
		if (auto uri = document->getString("uri"))
			file = sources.filePath(*uri);
	}
	if (!file) {
		std::cerr << "semindex-lsp: invalid textDocument/didSave parameters\n";
		return true;
	}
	if (!indexer.update(*file, message))
		std::cerr << "semindex-lsp: " << message << '\n';
	return true;
}

bool LspServer::hierarchyReply(const llvm::json::Value &id, LspCallHierarchy::Status status, llvm::json::Value result)
{
	if (status == LspCallHierarchy::Status::InvalidParams)
		return error(&id, INVALID_PARAMS, "Invalid params");
	if (status == LspCallHierarchy::Status::DatabaseError)
		return error(&id, INTERNAL_ERROR, "Database query failed");
	return reply(id, std::move(result));
}

bool LspServer::reply(const llvm::json::Value &id, llvm::json::Value result)
{
	return transport.write(llvm::json::Object{
		{ "jsonrpc", "2.0" },
		{ "id", id },
		{ "result", std::move(result) },
	});
}

bool LspServer::error(const llvm::json::Value *id, int code, const char *message)
{
	return transport.write(llvm::json::Object{
		{ "jsonrpc", "2.0" },
		{ "id", id ? *id : llvm::json::Value(nullptr) },
		{ "error",
			llvm::json::Object{
				{ "code", code },
				{ "message", message },
			} },
	});
}

bool LspServer::dispatch(const llvm::json::Object &message)
{
	const llvm::json::Value *id = message.get("id");
	auto jsonrpc = message.getString("jsonrpc");
	auto method = message.getString("method");

	if (!jsonrpc || *jsonrpc != "2.0" || !method)
		return error(id, INVALID_REQUEST, "Invalid Request");

	if (*method == "exit") {
		if (id)
			return error(id, INVALID_REQUEST, "Invalid Request");
		exit_status = state == State::Shutdown ? 0 : 1;
		exiting = true;
		return true;
	}

	if (*method == "initialize") {
		if (!id || state != State::Uninitialized)
			return error(id, INVALID_REQUEST, "Invalid Request");
		setWorkspaceRoot(sources, message.getObject("params"));
		state = State::Running;
		return reply(*id,
			llvm::json::Object{
				{ "capabilities",
					llvm::json::Object{
						{ "callHierarchyProvider", true },
						{ "definitionProvider", true },
						{ "documentHighlightProvider", true },
						{ "positionEncoding", "utf-16" },
						{ "referencesProvider", true },
						{ "textDocumentSync",
							llvm::json::Object{
								{ "change", 0 },
								{ "save", true },
							} },
					} },
				{ "serverInfo",
					llvm::json::Object{
						{ "name", "semindex" },
					} },
			});
	}

	if (state == State::Uninitialized)
		return id ? error(id, SERVER_NOT_INITIALIZED, "Server not initialized") : true;
	if (state == State::Shutdown)
		return id ? error(id, INVALID_REQUEST, "Invalid Request") : true;

	if (*method == "initialized")
		return id ? error(id, INVALID_REQUEST, "Invalid Request") : true;
	if (*method == "textDocument/definition")
		return id ? definition(*id, message.getObject("params")) : true;
	if (*method == "textDocument/documentHighlight")
		return id ? documentHighlight(*id, message.getObject("params")) : true;
	if (*method == "textDocument/references")
		return id ? references(*id, message.getObject("params")) : true;
	if (*method == "textDocument/prepareCallHierarchy") {
		if (!id)
			return true;
		llvm::json::Value result(nullptr);
		auto status = call_hierarchy.prepare(message.getObject("params"), result);

		return hierarchyReply(*id, status, std::move(result));
	}
	if (*method == "callHierarchy/incomingCalls") {
		if (!id)
			return true;
		llvm::json::Value result(nullptr);
		auto status = call_hierarchy.incoming(message.getObject("params"), result);

		return hierarchyReply(*id, status, std::move(result));
	}
	if (*method == "callHierarchy/outgoingCalls") {
		if (!id)
			return true;
		llvm::json::Value result(nullptr);
		auto status = call_hierarchy.outgoing(message.getObject("params"), result);

		return hierarchyReply(*id, status, std::move(result));
	}
	if (*method == "textDocument/didSave")
		return id ? error(id, INVALID_REQUEST, "Invalid Request") : didSave(message.getObject("params"));
	if (*method == "shutdown") {
		if (!id)
			return true;
		state = State::Shutdown;
		return reply(*id, nullptr);
	}

	return id ? error(id, METHOD_NOT_FOUND, "Method not found") : true;
}

int LspServer::run()
{
	while (!exiting) {
		std::string payload;
		auto read = transport.read(payload);

		if (read == LspTransport::ReadResult::EndOfFile)
			break;
		if (read == LspTransport::ReadResult::Error)
			return 1;

		auto parsed = llvm::json::parse(payload);
		if (!parsed) {
			llvm::consumeError(parsed.takeError());
			if (!error(nullptr, PARSE_ERROR, "Parse error"))
				return 1;
			continue;
		}
		const llvm::json::Object *message = parsed->getAsObject();
		if (!message) {
			if (!error(nullptr, INVALID_REQUEST, "Invalid Request"))
				return 1;
			continue;
		}
		if (!dispatch(*message))
			return 1;
	}
	return exiting ? exit_status : 0;
}
