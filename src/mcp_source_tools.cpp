// SPDX-License-Identifier: GPL-2.0-or-later
#include "mcp_tools_internal.h"

#include "index_updater.h"
#include "source_resolver.h"

#include <limits>
#include <utility>

static constexpr unsigned DEFAULT_CONTEXT_LINES = 20;
static constexpr unsigned MAX_CONTEXT_LINES = 200;
static constexpr size_t DEFAULT_CONTEXT_BYTES = 16 * 1024;
static constexpr size_t MAX_CONTEXT_BYTES = 64 * 1024;

namespace McpTools
{

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

const char *diagnosticSeverityName(semindex_diagnostic_severity_t severity)
{
	switch (severity) {
	case SEMINDEX_DIAGNOSTIC_NOTE:
		return "note";
	case SEMINDEX_DIAGNOSTIC_WARNING:
		return "warning";
	case SEMINDEX_DIAGNOSTIC_ERROR:
		return "error";
	}

	return "unknown";
}

const char *updateStatusName(SemindexIndexUpdateResult::Status status)
{
	switch (status) {
	case SemindexIndexUpdateResult::Status::Clean:
		return "clean";
	case SemindexIndexUpdateResult::Status::Partial:
		return "partial";
	case SemindexIndexUpdateResult::Status::Failed:
		return "failed";
	}

	return "failed";
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

McpToolResult indexStatus(semindex_db_t *database, const llvm::json::Object *arguments,
	const std::filesystem::path &workspace, const std::string &configured_variant,
	const SemindexIndexUpdater &updater, const McpRequestControl &control)
{
	auto path = allowedPath(arguments, workspace);
	auto selected = selectedVariant(arguments, configured_variant);

	if (!path || !selected || selected->empty())
		return { McpToolStatus::InvalidParams, {}, "Invalid source path or variant" };

	auto result = sourceRequest(database, arguments, workspace, configured_variant, false, control);

	if (result.status != McpToolStatus::Success)
		return result;

	int available = updater.commandAvailable(path->string(), *selected);

	if (available < 0)
		return { McpToolStatus::DatabaseError, {}, "Compiler command lookup failed" };

	result.data["compilerCommandAvailable"] = available == 0;

	return result;
}

McpToolResult reindexFile(const llvm::json::Object *arguments, const std::filesystem::path &workspace,
	const std::string &configured_variant, const SemindexIndexUpdater &updater, const McpRequestControl &control)
{
	auto path = allowedPath(arguments, workspace);
	auto selected = selectedVariant(arguments, configured_variant);

	if (!path || !selected || selected->empty())
		return { McpToolStatus::InvalidParams, {}, "Invalid source path or variant" };

	if (control.stopped())
		return stoppedResult(control);

	auto update = updater.update(SemindexIndexUpdateRequest{
		.file = path->string(),
		.variant = *selected,
		.diagnostic_limit = 100,
		.store_partial = true,
		.stopped = [&control] { return control.stopped(); },
	});

	if (control.stopped())
		return stoppedResult(control);

	llvm::json::Array diagnostics;

	for (const auto &diagnostic : update.diagnostics) {
		diagnostics.push_back(llvm::json::Object{
			{ "severity", diagnosticSeverityName(diagnostic.severity) },
			{ "message", diagnostic.message },
			{ "path", diagnostic.file },
			{ "line", diagnostic.line },
			{ "column", diagnostic.column },
		});
	}

	llvm::json::Object result{
		{ "variant", *selected },
		{ "path", path->string() },
		{ "status", updateStatusName(update.status) },
		{ "compilerCommandAvailable", update.command_available },
		{ "diagnostics", std::move(diagnostics) },
		{ "diagnosticsTruncated", update.diagnostics_truncated },
	};

	if (!update.error.empty())
		result["error"] = update.error;

	return { McpToolStatus::Success, std::move(result), {} };
}

} // namespace McpTools
