// SPDX-License-Identifier: GPL-2.0-or-later
#include "index_pp.h"
#include "index_asm.h"

#include <clang/Lex/MacroInfo.h>
#include <clang/Lex/PPCallbacks.h>
#include <clang/Lex/Preprocessor.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/FrontendActions.h>
#include <clang/Tooling/Tooling.h>

using namespace clang;

namespace
{

class SemindexPPCallbacks : public PPCallbacks
{
public:
	SemindexPPCallbacks(SemindexContext index, DiagnosticsEngine &diagnostics)
	    : index(index), diagnostics(diagnostics)
	{
	}

	bool FileNotFound(StringRef) override
	{
		return true;
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

		if (index.details())
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
		if (!file) {
			unsigned id = diagnostics.getCustomDiagID(DiagnosticsEngine::Error, "'%0' file not found");

			diagnostics.Report(filenameRange.getBegin(), id) << fileName;
		}

		addIncludeUse(fileName, isAngled, file, filenameRange.getBegin());
	}

private:
	std::string includeTarget(StringRef fileName, bool isAngled, OptionalFileEntryRef file) const
	{
		if (isAngled)
			return "<" + fileName.str() + ">";

		if (!file)
			return fileName.str();

		return index.commandPath(file->getName().str());
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

		if (index.details())
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

		if (index.details())
			u.usr = "macro:" + u.name;
		u.context = "";
		u.loc = index.location(spelling);
		u.local = false;

		index.addUseInScope(std::move(u), spelling);
	}

	SemindexContext index;
	DiagnosticsEngine &diagnostics;
};

class SemindexPreprocessorAction : public PreprocessOnlyAction
{
public:
	explicit SemindexPreprocessorAction(semindex *out) : out(out)
	{
	}

protected:
	void ExecuteAction() override
	{
		CompilerInstance &CI = getCompilerInstance();
		SemindexContext index(out, CI.getSourceManager());
		auto finish_asm_index = installSemindexAsmIndexer(index, CI.getPreprocessor());

		CI.getPreprocessor().addPPCallbacks(createSemindexPPCallbacks(index, CI.getDiagnostics()));
		PreprocessOnlyAction::ExecuteAction();
		finish_asm_index();
		out->has_index_data = true;
	}

private:
	semindex *out;
};

class SemindexPreprocessorActionFactory : public tooling::FrontendActionFactory
{
public:
	explicit SemindexPreprocessorActionFactory(semindex *out) : out(out)
	{
	}

	std::unique_ptr<FrontendAction> create() override
	{
		return std::make_unique<SemindexPreprocessorAction>(out);
	}

private:
	semindex *out;
};

} // namespace

std::unique_ptr<PPCallbacks> createSemindexPPCallbacks(SemindexContext index, DiagnosticsEngine &diagnostics)
{
	return std::make_unique<SemindexPPCallbacks>(index, diagnostics);
}

std::unique_ptr<tooling::FrontendActionFactory> createSemindexPreprocessorActionFactory(semindex *out)
{
	return std::make_unique<SemindexPreprocessorActionFactory>(out);
}
