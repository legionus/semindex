// SPDX-License-Identifier: GPL-2.0-or-later
#include "semindex_internal.h"

#include <clang/AST/AST.h>
#include <clang/Basic/SourceManager.h>

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
	s->symbol_records.clear();
	s->symbol_records.reserve(s->symbols.size());

	for (const auto &sym : s->symbols) {
		semindex_symbol_t rec;

		rec.kind = sym.kind;
		rec.name = sym.name.c_str();
		rec.owner = sym.owner.c_str();
		rec.type = sym.type.c_str();
		rec.usr = sym.usr.c_str();
		rec.context = sym.context.c_str();
		rec.file = sym.loc.file ? sym.loc.file->c_str() : "";
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
		rec.usr_id = use.usr_id;
		rec.context_usr_id = use.context_usr_id;
		rec.file = use.loc.file ? use.loc.file->c_str() : "";
		rec.line = use.loc.line;
		rec.column = use.loc.column;
		rec.local = use.local;
		rec.order = use.order;

		s->use_records.push_back(rec);
	}
}
