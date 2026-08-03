// SPDX-License-Identifier: GPL-2.0-or-later
#include "semindex_internal.h"

#include <clang/AST/AST.h>
#include <clang/Basic/SourceManager.h>

#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/Support/BLAKE3.h>
#include <llvm/Support/xxhash.h>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <unordered_map>
#include <vector>

struct FileFingerprintState {
	llvm::BLAKE3 hash[2];
	size_t records[2] = { 0, 0 };
	bool has_local = false;
};

struct FingerprintRecord {
	int record;
	int action;
	int kind;
	uint64_t mode;
	const char *owner;
	const char *name;
	unsigned line;
	unsigned column;
	const char *context;
	uint64_t usr_id;
	uint64_t context_usr_id;
	int local;
	const char *type;
	const char *canonical_type;
	const char *type_symbol;
	int type_kind;
	uint64_t type_usr_id;
};

static void hashInteger(llvm::BLAKE3 &hash, uint64_t value)
{
	uint8_t bytes[8];

	for (size_t i = 0; i < sizeof(bytes); i++) {
		bytes[i] = value & 0xff;
		value >>= 8;
	}
	hash.update(llvm::ArrayRef<uint8_t>(bytes));
}

static void hashString(llvm::BLAKE3 &hash, const char *value)
{
	llvm::StringRef str(value ? value : "");

	hashInteger(hash, str.size());
	hash.update(str);
}

static void hashRecord(llvm::BLAKE3 &hash, const FingerprintRecord &record)
{
	hashInteger(hash, record.record);
	hashInteger(hash, record.action);
	hashInteger(hash, record.kind);
	hashInteger(hash, record.mode);
	hashString(hash, record.owner);
	hashString(hash, record.name);
	hashInteger(hash, record.line);
	hashInteger(hash, record.column);
	hashString(hash, record.context);
	hashInteger(hash, record.usr_id);
	hashInteger(hash, record.context_usr_id);
	hashInteger(hash, record.local);
	hashString(hash, record.type);
	hashString(hash, record.canonical_type);
	hashString(hash, record.type_symbol);
	hashInteger(hash, record.type_kind);
	hashInteger(hash, record.type_usr_id);
}

static void updateFingerprints(FileFingerprintState &state, const FingerprintRecord &record)
{
	if (record.local) {
		if (!state.has_local) {
			state.hash[1] = state.hash[0];
			state.records[1] = state.records[0];
			state.has_local = true;
		}
		hashRecord(state.hash[1], record);
		state.records[1]++;

		return;
	}

	hashRecord(state.hash[0], record);
	state.records[0]++;

	if (state.has_local) {
		hashRecord(state.hash[1], record);
		state.records[1]++;
	}
}

static void hashFunctionSignature(llvm::BLAKE3 &hash, const semindex_function_signature_t &signature)
{
	hashInteger(hash, 2);
	hashString(hash, signature.name);
	hashInteger(hash, signature.usr_id);
	hashString(hash, signature.return_type);
	hashString(hash, signature.canonical_return_type);
	hashInteger(hash, signature.variadic);
	hashInteger(hash, signature.parameter_count);

	for (size_t i = 0; i < signature.parameter_count; i++) {
		const semindex_parameter_t &parameter = signature.parameters[i];

		hashInteger(hash, i);
		hashString(hash, parameter.name);
		hashString(hash, parameter.type);
		hashString(hash, parameter.canonical_type);
		hashString(hash, parameter.type_symbol);
		hashInteger(hash, parameter.type_usr_id ? parameter.type_kind : -1);
		hashInteger(hash, parameter.type_usr_id);
	}
}

static void updateFunctionSignatureFingerprint(FileFingerprintState &state,
	const semindex_function_signature_t &signature)
{
	hashFunctionSignature(state.hash[0], signature);
	state.records[0]++;

	if (state.has_local) {
		hashFunctionSignature(state.hash[1], signature);
		state.records[1]++;
	}
}

static std::string locToFile(const clang::ASTContext &ctx, clang::SourceLocation loc, unsigned &line, unsigned &col)
{
	const clang::SourceManager &sm = ctx.getSourceManager();
	clang::PresumedLoc ploc = sm.getPresumedLoc(loc);

	if (!ploc.isValid()) {
		line = col = 0;
		return "<invalid>";
	}

	line = ploc.getLine();
	col = ploc.getColumn();
	return std::string(ploc.getFilename());
}

static unsigned displayColumnForLoc(const clang::ASTContext &ctx, clang::SourceLocation loc)
{
	const clang::SourceManager &sm = ctx.getSourceManager();
	bool invalid = false;
	clang::SourceLocation spelling = sm.getSpellingLoc(loc);
	const char *ptr = sm.getCharacterData(spelling, &invalid);
	unsigned col = 1;

	if (invalid || !ptr)
		return sm.getPresumedLoc(loc).getColumn();

	while (ptr > sm.getBufferData(sm.getFileID(spelling), &invalid).begin() && ptr[-1] != '\n')
		ptr--;

	const char *end = sm.getCharacterData(spelling, &invalid);

	while (!invalid && ptr < end) {
		if (*ptr == '\t')
			col = ((col - 1) / 8 + 1) * 8 + 1;
		else
			col++;
		ptr++;
	}

	return col;
}

static std::string locToFileDisplayColumn(const clang::ASTContext &ctx, clang::SourceLocation loc, unsigned &line,
	unsigned &col)
{
	std::string file = locToFile(ctx, loc, line, col);

	col = displayColumnForLoc(ctx, loc);
	return file;
}

static std::string locToFile(const clang::SourceManager &sm, clang::SourceLocation loc, unsigned &line, unsigned &col)
{
	clang::PresumedLoc ploc = sm.getPresumedLoc(loc);

	if (!ploc.isValid()) {
		line = col = 0;
		return "<invalid>";
	}

	line = ploc.getLine();
	col = ploc.getColumn();
	return std::string(ploc.getFilename());
}

static bool locInScope(const clang::SourceManager &sm, semindex_scope_t scope, clang::SourceLocation loc)
{
	clang::SourceLocation spelling;

	if (scope == SEMINDEX_SCOPE_ALL)
		return true;

	if (loc.isInvalid())
		return false;

	spelling = sm.getSpellingLoc(loc);

	if (scope == SEMINDEX_SCOPE_FILE)
		return sm.isWrittenInMainFile(spelling);

	return !sm.isInPredefinedFile(spelling) && !sm.isInSystemHeader(spelling);
}

SemindexContext::SemindexContext(semindex *out, clang::SourceManager &sm) : out(out), sm(sm)
{
}

bool SemindexContext::inScope(clang::SourceLocation loc) const
{
	return locInScope(sm, out->scope, loc);
}

bool SemindexContext::details() const
{
	return out->details;
}

bool SemindexContext::includeLocal() const
{
	return out->include_local;
}

bool SemindexContext::pointsToAnalysis() const
{
	return out->points_to_analysis;
}

clang::SourceLocation SemindexContext::spellingLoc(clang::SourceLocation loc) const
{
	return sm.getSpellingLoc(loc);
}

SemindexSourceLocation SemindexContext::location(clang::SourceLocation loc)
{
	SemindexSourceLocation ret;
	std::string file = locToFile(sm, loc, ret.line, ret.column);

	ret.file = internFile(std::move(file));
	return ret;
}

SemindexSourceLocation SemindexContext::displayLocation(const clang::ASTContext &ast, clang::SourceLocation loc)
{
	SemindexSourceLocation ret;
	std::string file = locToFileDisplayColumn(ast, loc, ret.line, ret.column);

	ret.file = internFile(std::move(file));
	return ret;
}

std::string SemindexContext::commandPath(std::string path) const
{
	std::filesystem::path base(out->command_directory);
	std::filesystem::path resolved(std::move(path));

	if (base.empty())
		return resolved.lexically_normal().generic_string();

	if (resolved.is_relative())
		resolved = base / resolved;

	resolved = resolved.lexically_normal();
	std::filesystem::path relative = resolved.lexically_relative(base.lexically_normal());

	if (!relative.empty())
		return relative.generic_string();

	return resolved.generic_string();
}

void SemindexContext::addSymbolInScope(SemindexSymbol &&s, clang::SourceLocation loc)
{
	if (!inScope(loc))
		return;

	addSymbol(std::move(s));
}

void SemindexContext::addUseInScope(SemindexUse &&u, clang::SourceLocation loc)
{
	if (!inScope(loc))
		return;

	addUse(std::move(u));
}

void SemindexContext::addFunctionSignatureInScope(SemindexFunctionSignature &&signature, clang::SourceLocation loc)
{
	if (!inScope(loc))
		return;

	out->function_signatures.push_back(std::move(signature));
}

void SemindexContext::setPointsToStats(const semindex_points_to_stats_t &stats)
{
	out->points_to_stats = stats;
}

std::string SemindexContext::locationKey(const SemindexSourceLocation &loc) const
{
	std::string file = loc.file ? *loc.file : "";

	return file + "|" + std::to_string(loc.line) + "|" + std::to_string(loc.column);
}

const std::string *SemindexContext::internFile(std::string file)
{
	auto ret = out->files.insert(std::move(file));

	return &*ret.first;
}

void SemindexContext::addSymbol(SemindexSymbol &&s)
{
	s.order = out->next_order++;
	out->symbols.push_back(std::move(s));
}

void SemindexContext::addUse(SemindexUse &&u)
{
	u.order = out->next_order++;
	out->uses.push_back(std::move(u));
}

void rebuildRecords(semindex *s)
{
	std::unordered_map<const std::string *, size_t> file_index;
	size_t index = 0;

	for (const auto &file : s->files)
		file_index[&file] = index++;

	s->symbol_records.clear();
	s->symbol_records.reserve(s->symbols.size());

	for (const auto &sym : s->symbols) {
		semindex_symbol_t rec;

		rec.kind = sym.kind;
		rec.name = sym.name.c_str();
		rec.owner = sym.owner.c_str();
		rec.type = sym.type.c_str();
		rec.canonical_type = sym.canonical_type.c_str();
		rec.type_kind = sym.type_kind;
		rec.type_symbol = sym.type_symbol.c_str();
		rec.type_usr = sym.type_usr.c_str();
		rec.type_usr_id = sym.type_usr.empty() ? 0 : llvm::xxHash64(sym.type_usr);
		rec.usr = sym.usr.c_str();
		rec.usr_id = sym.usr.empty() ? 0 : llvm::xxHash64(sym.usr);
		rec.context = sym.context.c_str();
		rec.file = sym.loc.file ? sym.loc.file->c_str() : "";
		rec.file_index = sym.loc.file ? file_index.at(sym.loc.file) : s->files.size();
		rec.line = sym.loc.line;
		rec.column = sym.loc.column;
		rec.local = sym.local;
		rec.definition = sym.definition;
		rec.order = sym.order;

		s->symbol_records.push_back(rec);
	}

	s->use_records.clear();
	s->use_records.reserve(s->uses.size());

	for (const auto &use : s->uses) {
		semindex_use_t rec;

		rec.kind = use.kind;
		rec.symbol_kind = use.symbol_kind;
		rec.mode = use.mode;
		rec.name = use.name.c_str();
		rec.owner = use.owner.c_str();
		rec.type = use.type.c_str();
		rec.usr = use.usr.c_str();
		rec.context = use.context.c_str();
		rec.context_usr = use.context_usr.c_str();
		rec.usr_id = use.usr.empty() ? 0 : llvm::xxHash64(use.usr);
		rec.context_usr_id = use.context_usr.empty() ? 0 : llvm::xxHash64(use.context_usr);
		rec.file = use.loc.file ? use.loc.file->c_str() : "";
		rec.file_index = use.loc.file ? file_index.at(use.loc.file) : s->files.size();
		rec.line = use.loc.line;
		rec.column = use.loc.column;
		rec.local = use.local;
		rec.order = use.order;

		s->use_records.push_back(rec);
	}

	s->parameter_records.clear();
	s->parameter_records.reserve(s->function_signatures.size());
	s->function_signature_records.clear();
	s->function_signature_records.reserve(s->function_signatures.size());

	for (const auto &signature : s->function_signatures) {
		auto &parameters = s->parameter_records.emplace_back();

		parameters.reserve(signature.parameters.size());

		for (const auto &parameter : signature.parameters) {
			parameters.push_back({
				.name = parameter.name.c_str(),
				.type = parameter.type.c_str(),
				.canonical_type = parameter.canonical_type.c_str(),
				.type_kind = parameter.type_kind,
				.type_symbol = parameter.type_symbol.c_str(),
				.type_usr = parameter.type_usr.c_str(),
				.type_usr_id = parameter.type_usr.empty() ? 0 : llvm::xxHash64(parameter.type_usr),
			});
		}

		s->function_signature_records.push_back({
			.name = signature.name.c_str(),
			.usr = signature.usr.c_str(),
			.usr_id = signature.usr.empty() ? 0 : llvm::xxHash64(signature.usr),
			.return_type = signature.return_type.c_str(),
			.canonical_return_type = signature.canonical_return_type.c_str(),
			.parameters = parameters.data(),
			.parameter_count = parameters.size(),
			.file = signature.loc.file ? signature.loc.file->c_str() : "",
			.file_index = signature.loc.file ? file_index.at(signature.loc.file) : s->files.size(),
			.variadic = signature.variadic,
		});
	}
}

void rebuildFingerprints(semindex *s)
{
	std::vector<FileFingerprintState> fingerprints(s->files.size());
	size_t index = 0;

	for (const auto &rec : s->symbol_records) {
		FingerprintRecord record = {
			.record = 0,
			.action = rec.definition,
			.kind = rec.kind,
			.mode = 0,
			.owner = rec.owner,
			.name = rec.name,
			.line = rec.line,
			.column = rec.column,
			.context = rec.context,
			.usr_id = rec.usr_id,
			.context_usr_id = 0,
			.local = rec.local,
			.type = rec.type,
			.canonical_type = rec.canonical_type,
			.type_symbol = rec.type_symbol,
			.type_kind = rec.type_usr_id ? rec.type_kind : -1,
			.type_usr_id = rec.type_usr_id,
		};

		if (rec.file_index >= fingerprints.size())
			continue;

		updateFingerprints(fingerprints[rec.file_index], record);
	}
	for (const auto &rec : s->use_records) {
		FingerprintRecord record = {
			.record = 1,
			.action = rec.kind,
			.kind = rec.symbol_kind,
			.mode = rec.mode,
			.owner = rec.owner,
			.name = rec.name,
			.line = rec.line,
			.column = rec.column,
			.context = rec.context,
			.usr_id = rec.usr_id,
			.context_usr_id = rec.context_usr_id,
			.local = rec.local,
			.type = "",
			.canonical_type = "",
			.type_symbol = "",
			.type_kind = -1,
			.type_usr_id = 0,
		};

		if (rec.file_index >= fingerprints.size())
			continue;

		updateFingerprints(fingerprints[rec.file_index], record);
	}
	for (const auto &signature : s->function_signature_records) {
		if (signature.file_index >= fingerprints.size())
			continue;

		updateFunctionSignatureFingerprint(fingerprints[signature.file_index], signature);
	}

	s->file_fingerprints[0].clear();
	s->file_fingerprints[1].clear();
	s->file_fingerprints[0].reserve(s->files.size());
	s->file_fingerprints[1].reserve(s->files.size());
	index = 0;

	for (const auto &file : s->files) {
		semindex_file_fingerprint_t nonlocal;
		semindex_file_fingerprint_t all;
		auto nonlocal_digest = fingerprints[index].hash[0].final();
		auto all_digest = fingerprints[index].has_local ? fingerprints[index].hash[1].final() : nonlocal_digest;

		nonlocal.file = file.c_str();
		std::memcpy(nonlocal.data, nonlocal_digest.data(), sizeof(nonlocal.data));
		nonlocal.record_count = fingerprints[index].records[0];
		all.file = file.c_str();
		std::memcpy(all.data, all_digest.data(), sizeof(all.data));
		all.record_count =
			fingerprints[index].has_local ? fingerprints[index].records[1] : fingerprints[index].records[0];
		s->file_fingerprints[0].push_back(nonlocal);
		s->file_fingerprints[1].push_back(all);
		index++;
	}
}
