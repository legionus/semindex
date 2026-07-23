// SPDX-License-Identifier: GPL-2.0-or-later
#include "lsp_overlay.h"

#include <filesystem>
#include <string>

static std::string normalizedPath(const std::string &path)
{
	return std::filesystem::path(path).lexically_normal().string();
}

static std::string resolvedPath(const std::string &directory, const char *path)
{
	std::filesystem::path result(path ? path : "");

	if (result.is_relative())
		result = std::filesystem::path(directory) / result;
	return result.lexically_normal().string();
}

static std::string qualifiedName(const char *owner, const char *name)
{
	if (owner && owner[0])
		return std::string(owner) + "." + (name ? name : "");

	return name ? name : "";
}

static bool matchesFilter(semindex_db_record_type_t record, const semindex_db_query_options_t &options)
{
	switch (options.record) {
	case SEMINDEX_DB_RECORD_ALL:
		return true;

	case SEMINDEX_DB_RECORD_SYMBOL:
		return record != SEMINDEX_DB_REFERENCE;

	case SEMINDEX_DB_RECORD_DECLARATION:
		return record == SEMINDEX_DB_DECLARATION;

	case SEMINDEX_DB_RECORD_DEFINITION:
		return record == SEMINDEX_DB_DEFINITION;

	case SEMINDEX_DB_RECORD_REFERENCE:
		return record == SEMINDEX_DB_REFERENCE;
	}
	return false;
}

static bool matchesOptions(const semindex_db_record_t &record, const semindex_db_query_options_t &options)
{
	if (options.symbol && record.symbol != std::string(options.symbol))
		return false;

	if (options.path && normalizedPath(record.path) != normalizedPath(options.path))
		return false;

	if (options.context && record.context != std::string(options.context))
		return false;

	if (options.has_mode && record.mode != options.mode)
		return false;

	if (options.has_usr_id && record.usr_id != options.usr_id)
		return false;

	if (options.has_kind && record.kind != options.kind)
		return false;

	if (options.has_local && record.local != options.local)
		return false;

	return matchesFilter(record.record, options);
}

static const char *leafName(const std::string &symbol)
{
	size_t dot = symbol.rfind('.');

	return dot == std::string::npos ? symbol.c_str() : symbol.c_str() + dot + 1;
}

void LspOverlay::replace(const std::string &path, const std::string &directory, const semindex_t *index)
{
	std::string main_path = normalizedPath(path);
	Entry entry;

	for (size_t i = 0; i < semindex_symbol_count(index); i++) {
		const semindex_symbol_t *symbol = semindex_get_symbol(index, i);

		if (!symbol || resolvedPath(directory, symbol->file) != main_path)
			continue;
		entry.records.push_back({
			.symbol = qualifiedName(symbol->owner, symbol->name),
			.context = symbol->context,
			.record = symbol->definition ? SEMINDEX_DB_DEFINITION : SEMINDEX_DB_DECLARATION,
			.kind = symbol->kind,
			.action = static_cast<unsigned>(symbol->definition),
			.mode = 0,
			.line = symbol->line,
			.column = symbol->column,
			.usr_id = symbol->usr_id,
			.context_usr_id = 0,
			.local = symbol->local,
		});
	}
	for (size_t i = 0; i < semindex_use_count(index); i++) {
		const semindex_use_t *use = semindex_get_use(index, i);

		if (!use || resolvedPath(directory, use->file) != main_path)
			continue;
		entry.records.push_back({
			.symbol = qualifiedName(use->owner, use->name),
			.context = use->context,
			.record = SEMINDEX_DB_REFERENCE,
			.kind = use->symbol_kind,
			.action = use->kind,
			.mode = use->mode,
			.line = use->line,
			.column = use->column,
			.usr_id = use->usr_id,
			.context_usr_id = use->context_usr_id,
			.local = use->local,
		});
	}
	indices.insert_or_assign(std::move(main_path), std::move(entry));
}

void LspOverlay::erase(const std::string &path)
{
	indices.erase(normalizedPath(path));
}

bool LspOverlay::contains(const std::string &path) const
{
	return indices.find(normalizedPath(path)) != indices.end();
}

int LspOverlay::emitRecord(const Record &stored, const char *path, const char *variant,
	const semindex_db_query_options_t *options, unsigned line, unsigned column,
	semindex_db_record_callback_t callback, void *data)
{
	semindex_db_record_t record = {
		.variant = variant,
		.path = path,
		.symbol = stored.symbol.c_str(),
		.context = stored.context.c_str(),
		.record = stored.record,
		.kind = stored.kind,
		.action = stored.action,
		.mode = stored.mode,
		.line = stored.line,
		.column = stored.column,
		.usr_id = stored.usr_id,
		.context_usr_id = stored.context_usr_id,
		.local = stored.local,
	};

	if (options && !matchesOptions(record, *options))
		return 0;

	if (line) {
		size_t length = std::char_traits<char>::length(leafName(stored.symbol));

		if (record.line != line || record.column > column || column - record.column >= length)
			return 0;
	}
	return callback(data, &record);
}

int LspOverlay::findAt(const std::string &path, const char *variant, unsigned line, unsigned column,
	semindex_db_record_callback_t callback, void *data) const
{
	auto entry = indices.find(normalizedPath(path));

	if (entry == indices.end())
		return 0;

	if (!line || !column || !callback)
		return -1;

	for (const auto &record : entry->second.records) {
		int ret = emitRecord(record, entry->first.c_str(), variant ? variant : "", nullptr, line, column,
			callback, data);

		if (ret)
			return ret;
	}
	return 0;
}

int LspOverlay::query(const std::string &path, const char *variant, const semindex_db_query_options_t &options,
	semindex_db_record_callback_t callback, void *data) const
{
	auto entry = indices.find(normalizedPath(path));

	if (entry == indices.end())
		return 0;

	if (!callback)
		return -1;

	for (const auto &record : entry->second.records) {
		int ret = emitRecord(record, entry->first.c_str(), variant ? variant : "", &options, 0, 0, callback,
			data);

		if (ret)
			return ret;
	}
	return 0;
}
