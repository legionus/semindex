// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "mcp_tools.h"
#include "semantic_query.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

class SemindexIndexUpdater;

namespace McpTools
{

bool parseLimit(const llvm::json::Object *arguments, size_t &limit);
bool parseGraphBounds(const llvm::json::Object *arguments, unsigned &depth, size_t &node_limit);
std::optional<unsigned long long> parseId(llvm::StringRef value);
std::string idString(unsigned long long id);
std::string encodeHex(llvm::StringRef input);
std::optional<std::string> decodeHex(llvm::StringRef input);
std::string recordCursor(const SemindexQueryRecord &record);
llvm::json::Object recordObject(const SemindexQueryRecord &record);
McpToolResult stoppedResult(const McpRequestControl &control);
llvm::json::Object recordsResult(std::vector<SemindexQueryRecord> &records, size_t limit);
std::optional<std::string> selectedVariant(const llvm::json::Object *arguments, const std::string &configured);
std::optional<std::filesystem::path> allowedPath(const llvm::json::Object *arguments,
	const std::filesystem::path &workspace);

McpToolResult callRecords(semindex_db_t *database, const llvm::json::Object *arguments,
	const std::string &configured_variant, semindex_db_call_direction_t direction,
	const McpRequestControl &control);
McpToolResult searchSymbols(semindex_db_t *database, const llvm::json::Object *arguments,
	const std::string &configured_variant, const McpRequestControl &control);
McpToolResult symbolAt(semindex_db_t *database, const llvm::json::Object *arguments,
	const std::filesystem::path &workspace, const std::string &configured_variant,
	const McpRequestControl &control);
McpToolResult relatedRecords(semindex_db_t *database, const llvm::json::Object *arguments,
	const std::string &configured_variant, semindex_db_record_filter_t filter, const McpRequestControl &control);
McpToolResult declaredTypes(semindex_db_t *database, const llvm::json::Object *arguments,
	const std::string &configured_variant, const McpRequestControl &control);
McpToolResult functionSignature(semindex_db_t *database, const llvm::json::Object *arguments,
	const std::string &configured_variant, const McpRequestControl &control);
McpToolResult listVariants(semindex_db_t *database, const llvm::json::Object *arguments,
	const std::string &configured_variant, const McpRequestControl &control);
McpToolResult sourceRequest(semindex_db_t *database, const llvm::json::Object *arguments,
	const std::filesystem::path &workspace, const std::string &configured_variant, bool include_source,
	const McpRequestControl &control);
McpToolResult indexStatus(semindex_db_t *database, const llvm::json::Object *arguments,
	const std::filesystem::path &workspace, const std::string &configured_variant,
	const SemindexIndexUpdater &updater, const McpRequestControl &control);
McpToolResult reindexFile(const llvm::json::Object *arguments, const std::filesystem::path &workspace,
	const std::string &configured_variant, const SemindexIndexUpdater &updater, const McpRequestControl &control);

} // namespace McpTools
