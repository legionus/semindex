// SPDX-License-Identifier: GPL-2.0-or-later
#include "index_pp.h"

#include <clang/Lex/MacroInfo.h>
#include <clang/Lex/PPCallbacks.h>
#include <clang/Lex/Preprocessor.h>

using namespace clang;

namespace
{

class SemindexPPCallbacks : public PPCallbacks
{
public:
	explicit SemindexPPCallbacks(SemindexContext index) : index(index)
	{
	}

	void MacroDefined(const Token &macroNameTok, const MacroDirective *) override
	{
		IdentifierInfo *ident = macroNameTok.getIdentifierInfo();
		if (!ident)
			return;

		SourceLocation loc = macroNameTok.getLocation();
		SemindexSymbol s;
		s.kind = SEMINDEX_SYMBOL_MACRO;
		s.name = ident->getName().str();
		s.owner = "";
		s.type = "";
		s.usr = "macro:" + s.name;
		s.context = "";
		s.loc = index.location(loc);
		s.local = false;
		s.definition = true;

		index.addSymbolInScope(std::move(s), loc);
	}

	void MacroExpands(const Token &macroNameTok, const MacroDefinition &, SourceRange, const MacroArgs *) override
	{
		SourceLocation spelling = index.spellingLoc(macroNameTok.getLocation());
		addMacroUse(macroNameTok, spelling);
	}

	void Defined(const Token &macroNameTok, const MacroDefinition &, SourceRange) override
	{
		addMacroUse(macroNameTok, macroNameTok.getLocation());
	}

	void Ifdef(SourceLocation, const Token &macroNameTok, const MacroDefinition &) override
	{
		addMacroUse(macroNameTok, macroNameTok.getLocation());
	}

	void Elifdef(SourceLocation, const Token &macroNameTok, const MacroDefinition &) override
	{
		addMacroUse(macroNameTok, macroNameTok.getLocation());
	}

	void Ifndef(SourceLocation, const Token &macroNameTok, const MacroDefinition &) override
	{
		addMacroUse(macroNameTok, macroNameTok.getLocation());
	}

	void Elifndef(SourceLocation, const Token &macroNameTok, const MacroDefinition &) override
	{
		addMacroUse(macroNameTok, macroNameTok.getLocation());
	}

	void InclusionDirective(SourceLocation, const Token &, StringRef fileName, bool isAngled,
		CharSourceRange filenameRange, OptionalFileEntryRef file, StringRef, StringRef, const Module *, bool,
		SrcMgr::CharacteristicKind) override
	{
		if (!file)
			return;

		addIncludeUse(fileName, isAngled, file, filenameRange.getBegin());
	}

private:
	static std::string includeTarget(StringRef fileName, bool isAngled, OptionalFileEntryRef file)
	{
		if (isAngled)
			return "<" + fileName.str() + ">";

		return file->getName().str();
	}

	void addIncludeUse(StringRef fileName, bool isAngled, OptionalFileEntryRef file, SourceLocation loc)
	{
		SourceLocation spelling = index.spellingLoc(loc);

		std::string target = includeTarget(fileName, isAngled, file);

		SemindexUse u;
		u.kind = SEMINDEX_USE_READ;
		u.symbol_kind = SEMINDEX_SYMBOL_FILE;
		u.mode = SEMINDEX_MODE_R_VAL;
		u.name = target;
		u.owner = "";
		u.type = "";
		u.usr = "file:" + target;
		u.context = "";
		u.loc = index.location(spelling);
		u.local = false;

		index.addUseInScope(std::move(u), spelling);
	}

	void addMacroUse(const Token &macroNameTok, SourceLocation loc)
	{
		IdentifierInfo *ident = macroNameTok.getIdentifierInfo();
		if (!ident)
			return;

		SourceLocation spelling = index.spellingLoc(loc);

		SemindexUse u;
		u.kind = SEMINDEX_USE_READ;
		u.symbol_kind = SEMINDEX_SYMBOL_MACRO;
		u.mode = SEMINDEX_MODE_R_VAL;
		u.name = ident->getName().str();
		u.owner = "";
		u.type = "";
		u.usr = "macro:" + u.name;
		u.context = "";
		u.loc = index.location(spelling);
		u.local = false;

		index.addUseInScope(std::move(u), spelling);
	}

	SemindexContext index;
};

} // namespace

std::unique_ptr<PPCallbacks> createSemindexPPCallbacks(SemindexContext index)
{
	return std::make_unique<SemindexPPCallbacks>(index);
}
