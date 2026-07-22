// SPDX-License-Identifier: GPL-2.0-or-later
#include "lsp_source.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <utility>

static int hexDigit(char value)
{
	if (value >= '0' && value <= '9')
		return value - '0';
	if (value >= 'a' && value <= 'f')
		return value - 'a' + 10;
	if (value >= 'A' && value <= 'F')
		return value - 'A' + 10;
	return -1;
}

static std::optional<std::filesystem::path> pathFromUri(llvm::StringRef uri)
{
	if (!uri.consume_front("file://"))
		return std::nullopt;
	if (!uri.starts_with('/')) {
		size_t slash = uri.find('/');
		llvm::StringRef authority = uri.take_front(slash);

		if (slash == llvm::StringRef::npos || authority != "localhost")
			return std::nullopt;
		uri = uri.drop_front(slash);
	}

	std::string decoded;
	decoded.reserve(uri.size());
	for (size_t i = 0; i < uri.size(); i++) {
		if (uri[i] != '%') {
			decoded.push_back(uri[i]);
			continue;
		}
		if (i + 2 >= uri.size())
			return std::nullopt;
		int high = hexDigit(uri[i + 1]);
		int low = hexDigit(uri[i + 2]);
		if (high < 0 || low < 0 || (!high && !low))
			return std::nullopt;
		decoded.push_back(static_cast<char>((high << 4) | low));
		i += 2;
	}
	return std::filesystem::path(decoded).lexically_normal();
}

static std::string uriFromPath(const std::filesystem::path &path)
{
	static constexpr char hex[] = "0123456789ABCDEF";
	std::string input = path.generic_string();
	std::string uri = "file://";

	for (unsigned char value : input) {
		if ((value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') ||
			(value >= '0' && value <= '9') || value == '/' || value == '-' || value == '_' ||
			value == '.' || value == '~') {
			uri.push_back(value);
			continue;
		}
		uri.push_back('%');
		uri.push_back(hex[value >> 4]);
		uri.push_back(hex[value & 0xf]);
	}
	return uri;
}

static bool readLine(const std::filesystem::path &path, unsigned line_number, std::string &line)
{
	std::ifstream input(path, std::ios::binary);

	if (!input)
		return false;
	for (unsigned current = 0; current <= line_number; current++) {
		if (!std::getline(input, line))
			return false;
	}
	if (!line.empty() && line.back() == '\r')
		line.pop_back();
	return true;
}

static void readLines(const std::filesystem::path &path, std::vector<std::string> &lines)
{
	std::ifstream input(path, std::ios::binary);
	std::string line;

	lines.clear();
	if (!input)
		return;
	while (std::getline(input, line)) {
		if (!line.empty() && line.back() == '\r')
			line.pop_back();
		lines.push_back(std::move(line));
	}
}

static bool nextCodePoint(llvm::StringRef text, size_t &offset, uint32_t &codepoint)
{
	if (offset >= text.size())
		return false;
	unsigned char first = text[offset++];
	unsigned length;

	if (first < 0x80) {
		codepoint = first;
		return true;
	}
	if ((first & 0xe0) == 0xc0) {
		codepoint = first & 0x1f;
		length = 2;
	} else if ((first & 0xf0) == 0xe0) {
		codepoint = first & 0x0f;
		length = 3;
	} else if ((first & 0xf8) == 0xf0) {
		codepoint = first & 0x07;
		length = 4;
	} else {
		return false;
	}
	if (offset + length - 1 > text.size())
		return false;
	for (unsigned i = 1; i < length; i++) {
		unsigned char continuation = text[offset++];
		if ((continuation & 0xc0) != 0x80)
			return false;
		codepoint = (codepoint << 6) | (continuation & 0x3f);
	}
	return codepoint <= 0x10ffff && !(codepoint >= 0xd800 && codepoint <= 0xdfff);
}

static std::optional<size_t> byteOffset(llvm::StringRef text, unsigned utf16_column)
{
	size_t offset = 0;
	unsigned units = 0;

	while (units < utf16_column) {
		uint32_t codepoint;
		if (!nextCodePoint(text, offset, codepoint))
			return std::nullopt;
		unsigned width = codepoint > 0xffff ? 2 : 1;
		if (units + width > utf16_column)
			return std::nullopt;
		units += width;
	}
	return offset;
}

static unsigned utf16Length(llvm::StringRef text)
{
	size_t offset = 0;
	unsigned units = 0;

	while (offset < text.size()) {
		uint32_t codepoint;
		if (!nextCodePoint(text, offset, codepoint))
			return text.size();
		units += codepoint > 0xffff ? 2 : 1;
	}
	return units;
}

LspSourceMapper::LspSourceMapper() : root(std::filesystem::current_path())
{
}

bool LspSourceMapper::setRootUri(llvm::StringRef uri)
{
	auto path = pathFromUri(uri);
	if (!path || !path->is_absolute())
		return false;
	root = std::move(*path);
	return true;
}

std::optional<std::string> LspSourceMapper::filePath(llvm::StringRef uri) const
{
	auto path = pathFromUri(uri);

	if (!path || !path->is_absolute())
		return std::nullopt;
	return path->string();
}

std::vector<std::string> LspSourceMapper::databasePaths(llvm::StringRef uri) const
{
	std::vector<std::string> result;
	auto path = pathFromUri(uri);
	if (!path || !path->is_absolute())
		return result;

	result.push_back(path->string());
	std::filesystem::path relative = path->lexically_relative(root);
	if (!relative.empty() && *relative.begin() != "..")
		result.push_back(relative.string());
	std::filesystem::path cwd_relative = path->lexically_relative(std::filesystem::current_path());
	if (!cwd_relative.empty() && *cwd_relative.begin() != ".." && (cwd_relative != relative || result.size() == 1))
		result.push_back(cwd_relative.string());
	return result;
}

std::optional<unsigned> LspSourceMapper::byteColumn(llvm::StringRef uri, unsigned line, unsigned character) const
{
	auto path = pathFromUri(uri);
	std::string text;

	if (!path || !readLine(*path, line, text))
		return std::nullopt;
	auto offset = byteOffset(text, character);
	if (!offset)
		return std::nullopt;
	return *offset + 1;
}

std::filesystem::path LspSourceMapper::resolve(const char *path) const
{
	std::filesystem::path result(path ? path : "");

	if (result.is_relative())
		result = root / result;
	return result.lexically_normal();
}

std::string LspSourceMapper::uri(const char *path) const
{
	return uriFromPath(resolve(path));
}

llvm::json::Value LspSourceMapper::range(const semindex_db_record_t &record, Cache &cache) const
{
	std::filesystem::path path = resolve(record.path);
	unsigned start = record.column ? record.column - 1 : 0;
	unsigned end;
	unsigned line_number = record.line ? record.line - 1 : 0;
	const char *leaf = record.symbol;

	if (const char *dot = strrchr(leaf, '.'))
		leaf = dot + 1;
	std::string path_string = path.string();
	if (cache.path != path_string) {
		cache.path = path_string;
		readLines(path, cache.lines);
	}
	if (record.line && record.line <= cache.lines.size()) {
		const std::string &line = cache.lines[record.line - 1];
		start = utf16Length(llvm::StringRef(line).take_front(std::min<size_t>(start, line.size())));
	}
	end = start + utf16Length(leaf);
	return llvm::json::Object{
		{ "start", llvm::json::Object{ { "line", line_number }, { "character", start } } },
		{ "end", llvm::json::Object{ { "line", line_number }, { "character", end } } },
	};
}

llvm::json::Value LspSourceMapper::location(const semindex_db_record_t &record, Cache &cache) const
{
	return llvm::json::Object{
		{ "uri", uri(record.path) },
		{ "range", range(record, cache) },
	};
}
