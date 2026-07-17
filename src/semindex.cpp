// SPDX-License-Identifier: GPL-2.0-or-later
#include "semindex.h"

/* ==== LLVM / Clang ==== */
#include <clang/AST/AST.h>
#include <clang/AST/ParentMapContext.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/AST/TypeLoc.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/FrontendActions.h>
#include <clang/Index/USRGeneration.h>
#include <clang/Lex/MacroInfo.h>
#include <clang/Lex/PPCallbacks.h>
#include <clang/Lex/Preprocessor.h>
#include <clang/Tooling/CompilationDatabase.h>
#include <clang/Tooling/Tooling.h>

#include <llvm/ADT/SmallVector.h>
#include <llvm/Support/Path.h>
#include <llvm/Support/raw_ostream.h>

/* ==== STL ==== */
#include <memory>
#include <set>
#include <string>
#include <vector>

using namespace clang;
using namespace clang::tooling;

/* ============================================================
 * Helpers
 * ============================================================ */

static std::string locToFile(const ASTContext& ctx, SourceLocation loc, unsigned& line, unsigned& col)
{
	const SourceManager& sm = ctx.getSourceManager();
	PresumedLoc ploc = sm.getPresumedLoc(loc);

	if (!ploc.isValid()) {
		line = col = 0;
		return "<invalid>";
	}

	line = ploc.getLine();
	col = ploc.getColumn();
	return std::string(ploc.getFilename());
}

static unsigned displayColumnForLoc(const ASTContext& ctx, SourceLocation loc)
{
	const SourceManager& sm = ctx.getSourceManager();
	bool invalid = false;
	SourceLocation spelling = sm.getSpellingLoc(loc);
	const char* ptr = sm.getCharacterData(spelling, &invalid);
	unsigned col = 1;

	if (invalid || !ptr)
		return sm.getPresumedLoc(loc).getColumn();

	while (ptr > sm.getBufferData(sm.getFileID(spelling), &invalid).begin()
	    && ptr[-1] != '\n')
		ptr--;

	const char* end = sm.getCharacterData(spelling, &invalid);
	while (!invalid && ptr < end) {
		if (*ptr == '\t')
			col = ((col - 1) / 8 + 1) * 8 + 1;
		else
			col++;
		ptr++;
	}

	return col;
}

static std::string locToFileDisplayColumn(
    const ASTContext& ctx, SourceLocation loc, unsigned& line, unsigned& col)
{
	std::string file = locToFile(ctx, loc, line, col);
	col = displayColumnForLoc(ctx, loc);
	return file;
}

static std::string locToFile(const SourceManager& sm, SourceLocation loc, unsigned& line, unsigned& col)
{
	PresumedLoc ploc = sm.getPresumedLoc(loc);

	if (!ploc.isValid()) {
		line = col = 0;
		return "<invalid>";
	}

	line = ploc.getLine();
	col = ploc.getColumn();
	return std::string(ploc.getFilename());
}

static std::string getName(const Decl* D)
{
	if (const auto* ND = llvm::dyn_cast<NamedDecl>(D))
		return ND->getNameAsString();
	return "";
}

static std::string getUSR(const Decl* D, const ASTContext& ctx)
{
	llvm::SmallVector<char, 128> buf;
	if (index::generateUSRForDecl(D, buf, ctx.getLangOpts()))
		return "";
	return std::string(buf.begin(), buf.end());
}

static std::unique_ptr<CompilationDatabase> loadCompileCommands(
    const char* compile_commands_json, std::string& error)
{
	if (!compile_commands_json || !compile_commands_json[0]) {
		error = "missing compile_commands.json path";
		return nullptr;
	}

	llvm::StringRef path(compile_commands_json);
	std::string directory;

	if (llvm::sys::path::filename(path) == "compile_commands.json")
		directory = llvm::sys::path::parent_path(path).str();
	else
		directory = path.str();

	if (directory.empty())
		directory = ".";

	return CompilationDatabase::loadFromDirectory(directory, error);
}

static bool isDirectCallCallee(const Expr* E, ASTContext& ctx)
{
	const Expr* current = E;
	const Expr* target = E->IgnoreParenImpCasts();

	for (;;) {
		auto parents = ctx.getParents(*current);
		if (parents.empty())
			return false;

		const Stmt* parent = parents.begin()->get<Stmt>();
		if (!parent)
			return false;

		if (const auto* call = dyn_cast<CallExpr>(parent))
			return call->getCallee()->IgnoreParenImpCasts() == target;

		if (!isa<ParenExpr>(parent) && !isa<ImplicitCastExpr>(parent))
			return false;

		current = cast<Expr>(parent);
	}
}

static const Stmt* getParentStmt(const Expr* E, ASTContext& ctx)
{
	auto parents = ctx.getParents(*E);
	if (parents.empty())
		return nullptr;

	return parents.begin()->get<Stmt>();
}

static const Expr* ignoreParenImpCasts(const Expr* E)
{
	return E ? E->IgnoreParenImpCasts() : nullptr;
}

static bool isPointerReadOperand(const Expr* E, ASTContext& ctx)
{
	const Expr* current = E;

	for (;;) {
		const Stmt* parent = getParentStmt(current, ctx);
		if (!parent)
			return false;

		if (const auto* U = dyn_cast<UnaryOperator>(parent)) {
			if (U->getOpcode() == UO_Deref)
				return true;
		}

		if (const auto* M = dyn_cast<MemberExpr>(parent)) {
			if (M->isArrow()
			    && ignoreParenImpCasts(M->getBase()) == ignoreParenImpCasts(E))
				return true;
		}

		if (!isa<ParenExpr>(parent) && !isa<ImplicitCastExpr>(parent))
			return false;

		current = cast<Expr>(parent);
	}
}

static bool isCallCallee(const Expr* E, ASTContext& ctx)
{
	const Expr* current = E;
	const Expr* target = E->IgnoreParenImpCasts();

	for (;;) {
		const Stmt* parent = getParentStmt(current, ctx);
		if (!parent)
			return false;

		if (const auto* call = dyn_cast<CallExpr>(parent))
			return ignoreParenImpCasts(call->getCallee()) == target;

		if (!isa<ParenExpr>(parent) && !isa<ImplicitCastExpr>(parent))
			return false;

		current = cast<Expr>(parent);
	}
}

static semindex_use_kind_t classifyUse(const Expr* E, ASTContext& ctx)
{
	E = E->IgnoreParenImpCasts();

	const Stmt* parent = nullptr;

	auto parents = ctx.getParents(*E);
	if (!parents.empty()) {
		if (const Stmt* S = parents.begin()->get<Stmt>())
			parent = S;
	}

	/* &x */
	if (parent) {
		if (const auto* U = dyn_cast<UnaryOperator>(parent)) {
			if (U->getOpcode() == UO_AddrOf)
				return SEMINDEX_USE_ADDR;
		}
	}

	/* x = ... */
	if (parent) {
		if (const auto* B = dyn_cast<BinaryOperator>(parent)) {
			if (B->isAssignmentOp() && B->getLHS() == E)
				return SEMINDEX_USE_WRITE;
		}
	}

	/* ++x, x++ */
	if (parent) {
		if (const auto* U = dyn_cast<UnaryOperator>(parent)) {
			if (U->isIncrementDecrementOp())
				return SEMINDEX_USE_WRITE;
		}
	}

	return SEMINDEX_USE_READ;
}

static unsigned accessModeForUse(semindex_use_kind_t kind, const Expr* E, ASTContext& ctx)
{
	switch (kind) {
	case SEMINDEX_USE_ADDR:
		return SEMINDEX_MODE_R_AOF | SEMINDEX_MODE_W_AOF;
	case SEMINDEX_USE_WRITE:
		return SEMINDEX_MODE_W_VAL;
	case SEMINDEX_USE_CALL:
		return SEMINDEX_MODE_R_PTR;
	case SEMINDEX_USE_READ:
		if (isCallCallee(E, ctx) || isPointerReadOperand(E, ctx))
			return SEMINDEX_MODE_R_PTR;
		return SEMINDEX_MODE_R_VAL;
	}

	return 0;
}

static semindex_symbol_kind_t symbolKindForDecl(const ValueDecl* D)
{
	if (isa<FieldDecl>(D))
		return SEMINDEX_SYMBOL_FIELD;
	if (isa<EnumConstantDecl>(D))
		return SEMINDEX_SYMBOL_ENUM_CONSTANT;
	if (isa<FunctionDecl>(D))
		return SEMINDEX_SYMBOL_FUNCTION;
	return SEMINDEX_SYMBOL_VAR;
}

static std::string getOwnerName(const Decl* D)
{
	const auto* FD = dyn_cast<FieldDecl>(D);
	if (!FD)
		return "";

	const auto* RD = FD->getParent();
	if (!RD)
		return "";

	return getName(RD);
}

static const RecordDecl* recordDeclForType(QualType type)
{
	type = type.getCanonicalType();

	if (const auto* RT = type->getAs<RecordType>())
		return RT->getDecl();

	return nullptr;
}

static bool isTypedefType(QualType type)
{
	return type->getAs<TypedefType>() != nullptr;
}

static bool isWrittenAsTypedef(TypeSourceInfo* typeSourceInfo)
{
	return typeSourceInfo
	    && !typeSourceInfo->getTypeLoc()
	            .getAsAdjusted<TypedefTypeLoc>()
	            .isNull();
}

static bool isAnonymousRecord(const RecordDecl* D)
{
	return D && getName(D).empty();
}

static std::string anonymousRecordNameForVar(const VarDecl* D)
{
	return ":" + getName(D);
}

static std::string anonymousRecordNameForTypedef(const TypedefNameDecl* D)
{
	return ":" + getName(D);
}

static std::string typeNameForRecord(const RecordDecl* D, const std::string& name)
{
	return std::string(D->isUnion() ? "union " : "struct ") + name;
}

static std::string typeNameForTypedef(const TypedefNameDecl* D)
{
	const RecordDecl* anonymousRecord = recordDeclForType(D->getUnderlyingType());

	if (isAnonymousRecord(anonymousRecord))
		return typeNameForRecord(anonymousRecord,
		    anonymousRecordNameForTypedef(D));

	return D->getUnderlyingType().getAsString();
}

/* ============================================================
 * Internal C++ model
 * ============================================================ */

struct SemindexSymbol {
	semindex_symbol_kind_t kind;
	std::string name;
	std::string owner;
	std::string type;
	std::string usr;
	std::string context;
	std::string file;
	unsigned line;
	unsigned column;
	bool local;
	bool definition;
};

struct SemindexUse {
	semindex_use_kind_t kind;
	semindex_symbol_kind_t symbol_kind;
	unsigned mode;
	std::string name;
	std::string owner;
	std::string type;
	std::string usr;
	std::string context;
	std::string file;
	unsigned line;
	unsigned column;
	bool local;
};

struct semindex {
	std::vector<SemindexSymbol> symbols;
	std::vector<SemindexUse> uses;
	std::vector<semindex_symbol_t> symbol_records;
	std::vector<semindex_use_t> use_records;
};

class SemindexPPCallbacks : public PPCallbacks {
public:
	SemindexPPCallbacks(SourceManager& sm, semindex* out)
	    : sm(sm)
	    , out(out)
	{
	}

	void MacroDefined(const Token& macroNameTok,
	    const MacroDirective*) override
	{
		IdentifierInfo* ident = macroNameTok.getIdentifierInfo();
		if (!ident)
			return;

		SourceLocation loc = macroNameTok.getLocation();
		SourceLocation spelling = sm.getSpellingLoc(loc);
		if (!sm.isWrittenInMainFile(spelling))
			return;

		SemindexSymbol s;
		s.kind = SEMINDEX_SYMBOL_MACRO;
		s.name = ident->getName().str();
		s.owner = "";
		s.type = "";
		s.usr = "macro:" + s.name;
		s.context = "";
		s.file = locToFile(sm, loc, s.line, s.column);
		s.local = false;
		s.definition = true;

		out->symbols.push_back(std::move(s));
	}

	void MacroExpands(const Token& macroNameTok, const MacroDefinition&,
	    SourceRange, const MacroArgs*) override
	{
		IdentifierInfo* ident = macroNameTok.getIdentifierInfo();
		if (!ident)
			return;

		SourceLocation spelling = sm.getSpellingLoc(macroNameTok.getLocation());
		if (!sm.isWrittenInMainFile(spelling))
			return;

		SemindexUse u;
		u.kind = SEMINDEX_USE_READ;
		u.symbol_kind = SEMINDEX_SYMBOL_MACRO;
		u.mode = SEMINDEX_MODE_R_VAL;
		u.name = ident->getName().str();
		u.owner = "";
		u.type = "";
		u.usr = "macro:" + u.name;
		u.context = "";
		u.file = locToFile(sm, spelling, u.line, u.column);
		u.local = false;

		out->uses.push_back(std::move(u));
	}

private:
	SourceManager& sm;
	semindex* out;
};

static void rebuildRecords(semindex* s)
{
	s->symbol_records.clear();
	s->symbol_records.reserve(s->symbols.size());

	for (const auto& sym : s->symbols) {
		semindex_symbol_t rec;

		rec.kind = sym.kind;
		rec.name = sym.name.c_str();
		rec.owner = sym.owner.c_str();
		rec.type = sym.type.c_str();
		rec.usr = sym.usr.c_str();
		rec.context = sym.context.c_str();
		rec.file = sym.file.c_str();
		rec.line = sym.line;
		rec.column = sym.column;
		rec.local = sym.local;
		rec.definition = sym.definition;

		s->symbol_records.push_back(rec);
	}

	s->use_records.clear();
	s->use_records.reserve(s->uses.size());

	for (const auto& use : s->uses) {
		semindex_use_t rec;

		rec.kind = use.kind;
		rec.symbol_kind = use.symbol_kind;
		rec.mode = use.mode;
		rec.name = use.name.c_str();
		rec.owner = use.owner.c_str();
		rec.type = use.type.c_str();
		rec.usr = use.usr.c_str();
		rec.context = use.context.c_str();
		rec.file = use.file.c_str();
		rec.line = use.line;
		rec.column = use.column;
		rec.local = use.local;

		s->use_records.push_back(rec);
	}
}

/* ============================================================
 * AST Visitor
 * ============================================================ */

class SemindexVisitor : public RecursiveASTVisitor<SemindexVisitor> {
    public:
	SemindexVisitor(ASTContext& ctx, semindex* out)
	    : ctx(ctx)
	    , out(out)
	{
	}

	bool TraverseFunctionDecl(FunctionDecl* D)
	{
		if (!D)
			return true;

		std::string oldFunction = currentFunction;
		currentFunction = getName(D);
		bool ret = RecursiveASTVisitor<SemindexVisitor>::TraverseFunctionDecl(D);
		currentFunction = oldFunction;
		return ret;
	}

	bool VisitVarDecl(VarDecl* D)
	{
		if (isPrototypeParameter(D))
			return true;

		const RecordDecl* anonymousRecord = recordDeclForType(D->getType());
		std::string anonymousName;
		std::string typeName;

		if (!isTypedefType(D->getType())
		    && !isWrittenAsTypedef(D->getTypeSourceInfo())
		    && isAnonymousRecord(anonymousRecord)) {
			anonymousName = anonymousRecordNameForVar(D);
			typeName = typeNameForRecord(anonymousRecord, anonymousName);
			addAnonymousRecordSymbols(anonymousRecord, anonymousName);
		} else {
			typeName = D->getType().getAsString();
		}

		SemindexSymbol s;
		s.kind = SEMINDEX_SYMBOL_VAR;
		s.name = getName(D);
		s.owner = "";
		s.type = typeName;
		s.usr = getUSR(D, ctx);
		s.context = currentFunction;
		s.file = locToFile(ctx, D->getLocation(), s.line, s.column);
		s.local = !currentFunction.empty();
		s.definition = true;

		out->symbols.push_back(std::move(s));

		if (D->hasInit()) {
			SemindexUse u;
			u.kind = SEMINDEX_USE_WRITE;
			u.symbol_kind = SEMINDEX_SYMBOL_VAR;
			u.mode = SEMINDEX_MODE_W_VAL;
			u.name = getName(D);
			u.owner = "";
			u.type = typeName;
			u.usr = getUSR(D, ctx);
			u.context = currentFunction;
			u.file = locToFile(ctx, D->getLocation(), u.line, u.column);
			u.local = !currentFunction.empty();

			out->uses.push_back(std::move(u));
		}

		return true;
	}

	bool VisitFieldDecl(FieldDecl* D)
	{
		if (isAnonymousRecord(D->getParent()))
			return true;

		SemindexSymbol s;
		s.kind = SEMINDEX_SYMBOL_FIELD;
		s.name = getName(D);
		s.owner = getOwnerName(D);
		s.type = D->getType().getAsString();
		s.usr = getUSR(D, ctx);
		s.context = "";
		s.file = locToFile(ctx, D->getLocation(), s.line, s.column);
		s.local = false;
		s.definition = true;

		out->symbols.push_back(std::move(s));
		return true;
	}

	bool VisitTypedefNameDecl(TypedefNameDecl* D)
	{
		const RecordDecl* anonymousRecord = recordDeclForType(D->getUnderlyingType());
		std::string anonymousName;
		std::string typeName = typeNameForTypedef(D);

		if (isAnonymousRecord(anonymousRecord)) {
			anonymousName = anonymousRecordNameForTypedef(D);
			addAnonymousRecordSymbols(anonymousRecord, anonymousName);
		}

		SemindexSymbol s;
		s.kind = SEMINDEX_SYMBOL_TYPEDEF;
		s.name = getName(D);
		s.owner = "";
		s.type = typeName;
		s.usr = getUSR(D, ctx);
		s.context = currentFunction;
		s.file = locToFile(ctx, D->getLocation(), s.line, s.column);
		s.local = !currentFunction.empty();
		s.definition = true;

		out->symbols.push_back(std::move(s));
		return true;
	}

	bool VisitTypedefTypeLoc(TypedefTypeLoc TL)
	{
		const TypedefNameDecl* D = TL.getTypedefNameDecl();

		addTypeUse(D, SEMINDEX_SYMBOL_TYPEDEF, TL.getNameLoc(),
		    typeNameForTypedef(D));
		return true;
	}

	bool VisitRecordTypeLoc(RecordTypeLoc TL)
	{
		if (TL.isDefinition())
			return true;

		const RecordDecl* D = TL.getDecl();
		if (isAnonymousRecord(D))
			return true;

		addTypeUse(D,
		    D->isUnion() ? SEMINDEX_SYMBOL_UNION
		                 : SEMINDEX_SYMBOL_STRUCT,
		    TL.getNameLoc(), "");
		return true;
	}

	bool VisitEnumTypeLoc(EnumTypeLoc TL)
	{
		if (TL.isDefinition())
			return true;

		const EnumDecl* D = TL.getDecl();
		if (!D || getName(D).empty())
			return true;

		addTypeUse(D, SEMINDEX_SYMBOL_ENUM, TL.getNameLoc(), "");
		return true;
	}

	bool VisitDeclRefExpr(DeclRefExpr* E)
	{
		const ValueDecl* D = E->getDecl();
		if (!D)
			return true;

		if (isa<FunctionDecl>(D) && isDirectCallCallee(E, ctx))
			return true;

		SemindexUse u;
		u.kind = classifyUse(E, ctx);
		u.symbol_kind = symbolKindForDecl(D);
		u.mode = accessModeForUse(u.kind, E, ctx);
		u.name = getName(D);
		u.owner = getOwnerName(D);
		u.type = D->getType().getAsString();
		u.usr = getUSR(D, ctx);
		u.context = currentFunction;
		u.file = locToFile(ctx, E->getExprLoc(), u.line, u.column);
		u.local = !currentFunction.empty() && !D->hasExternalFormalLinkage();

		out->uses.push_back(std::move(u));
		return true;
	}

	bool VisitMemberExpr(MemberExpr* E)
	{
		const ValueDecl* D = E->getMemberDecl();
		if (!D)
			return true;

		semindex_use_kind_t kind = classifyUse(E, ctx);
		addValueUse(D, kind, accessModeForUse(kind, E, ctx),
		    currentFunction, E->getExprLoc(), false);
		return true;
	}

	bool VisitDesignatedInitExpr(DesignatedInitExpr* E)
	{
		for (const auto& designator : E->designators()) {
			if (!designator.isFieldDesignator())
				continue;

			const FieldDecl* D = designator.getFieldDecl();
			if (!D)
				continue;

			addValueUse(D, SEMINDEX_USE_WRITE, SEMINDEX_MODE_W_VAL,
			    currentFunction, designator.getFieldLoc(), false);
		}

		return true;
	}

	bool VisitRecordDecl(RecordDecl* D)
	{
		if (!D->isThisDeclarationADefinition())
			return true;
		if (isAnonymousRecord(D))
			return true;

		SemindexSymbol s;
		s.kind
		    = D->isUnion() ? SEMINDEX_SYMBOL_UNION : SEMINDEX_SYMBOL_STRUCT;

		s.name = getName(D);
		s.owner = "";
		s.type = ""; // TODO: fill it later
		s.usr = getUSR(D, ctx);
		s.context = "";
		s.file = locToFile(ctx, D->getLocation(), s.line, s.column);
		s.local = false;
		s.definition = true;

		out->symbols.push_back(std::move(s));
		return true;
	}

	bool VisitEnumDecl(EnumDecl* D)
	{
		if (!D->isThisDeclarationADefinition())
			return true;
		if (getName(D).empty())
			return true;

		SemindexSymbol s;
		s.kind = SEMINDEX_SYMBOL_ENUM;
		s.name = getName(D);
		s.owner = "";
		s.type = "";
		s.usr = getUSR(D, ctx);
		s.context = "";
		s.file = locToFile(ctx, D->getLocation(), s.line, s.column);
		s.local = false;
		s.definition = true;

		out->symbols.push_back(std::move(s));
		return true;
	}

	bool VisitEnumConstantDecl(EnumConstantDecl* D)
	{
		SemindexSymbol s;
		s.kind = SEMINDEX_SYMBOL_ENUM_CONSTANT;
		s.name = getName(D);
		s.owner = "";
		s.type = D->getType().getAsString();
		s.usr = getUSR(D, ctx);
		s.context = "";
		s.file = locToFile(ctx, D->getLocation(), s.line, s.column);
		s.local = false;
		s.definition = true;

		out->symbols.push_back(std::move(s));
		return true;
	}

	bool VisitFunctionDecl(FunctionDecl* D)
	{
		if (!D->isThisDeclarationADefinition() && !D->hasPrototype())
			return true;
		if (!functionSymbols.insert(getUSR(D, ctx)).second)
			return true;

		SemindexSymbol s;
		s.kind = SEMINDEX_SYMBOL_FUNCTION;
		s.name = getName(D);
		s.owner = "";
		s.type = D->getType().getAsString();
		s.usr = getUSR(D, ctx);
		s.context = "";
		s.file = locToFile(ctx, D->getLocation(), s.line, s.column);
		s.local = false;
		s.definition = D->isThisDeclarationADefinition();

		out->symbols.push_back(std::move(s));
		return true;
	}

	bool VisitCallExpr(CallExpr* E)
	{
		const FunctionDecl* FD = E->getDirectCallee();
		if (!FD)
			return true; /* indirect call: fp() */

		SemindexUse u;
		u.kind = SEMINDEX_USE_CALL;
		u.symbol_kind = SEMINDEX_SYMBOL_FUNCTION;
		u.mode = SEMINDEX_MODE_R_PTR;
		u.name = getName(FD);
		u.owner = "";
		u.type = FD->getType().getAsString();
		u.usr = getUSR(FD, ctx);
		u.context = currentFunction;
		u.file = locToFile(ctx, E->getExprLoc(), u.line, u.column);
		u.local = false;

		out->uses.push_back(std::move(u));
		return true;
	}

    private:
	static bool isPrototypeParameter(const VarDecl* D)
	{
		const auto* P = dyn_cast<ParmVarDecl>(D);
		if (!P)
			return false;

		const auto* F = dyn_cast_or_null<FunctionDecl>(P->getDeclContext());
		return F && !F->doesThisDeclarationHaveABody();
	}

	void addTypeUse(const TypeDecl* D, semindex_symbol_kind_t kind,
	    SourceLocation loc, const std::string& type)
	{
		if (!D || loc.isInvalid())
			return;

		const SourceManager& sm = ctx.getSourceManager();
		SourceLocation spelling = sm.getSpellingLoc(loc);
		if (!sm.isWrittenInMainFile(spelling))
			return;

		SemindexUse u;
		u.kind = SEMINDEX_USE_READ;
		u.symbol_kind = kind;
		u.mode = SEMINDEX_MODE_R_VAL;
		u.name = getName(D);
		u.owner = "";
		u.type = type;
		u.usr = getUSR(D, ctx);
		u.context = currentFunction;
		u.file = locToFile(ctx, spelling, u.line, u.column);
		u.local = !currentFunction.empty();

		std::string key = u.usr + "|" + u.file + "|"
		    + std::to_string(u.line) + "|" + std::to_string(u.column)
		    + "|" + u.context;
		if (!typeUses.insert(key).second)
			return;

		out->uses.push_back(std::move(u));
	}

	void addValueUse(const ValueDecl* D, semindex_use_kind_t kind,
	    unsigned mode, const std::string& context, SourceLocation loc,
	    bool local)
	{
		SemindexUse u;
		u.kind = kind;
		u.symbol_kind = symbolKindForDecl(D);
		u.mode = mode;
		u.name = getName(D);
		u.owner = getOwnerName(D);
		u.type = D->getType().getAsString();
		u.usr = getUSR(D, ctx);
		u.context = context;
		u.file = locToFile(ctx, loc, u.line, u.column);
		u.local = local;

		out->uses.push_back(std::move(u));
	}

	void addAnonymousRecordSymbols(const RecordDecl* D, const std::string& name)
	{
		SemindexSymbol s;
		s.kind = D->isUnion() ? SEMINDEX_SYMBOL_UNION : SEMINDEX_SYMBOL_STRUCT;
		s.name = name;
		s.owner = "";
		s.type = "";
		s.usr = getUSR(D, ctx);
		s.context = "";
		s.file = locToFileDisplayColumn(ctx,
		    D->getBraceRange().getBegin().isValid()
		        ? D->getBraceRange().getBegin()
		        : D->getLocation(),
		    s.line, s.column);
		s.local = false;
		s.definition = true;

		out->symbols.push_back(std::move(s));
		addAnonymousRecordFields(D, name);
	}

	void addAnonymousRecordFields(const RecordDecl* D, const std::string& owner)
	{
		for (const FieldDecl* field : D->fields()) {
			const RecordDecl* nested = recordDeclForType(field->getType());

			if (isAnonymousRecord(nested) && getName(field).empty()) {
				addAnonymousRecordFields(nested, owner);
				continue;
			}

			if (getName(field).empty())
				continue;

			SemindexSymbol s;
			s.kind = SEMINDEX_SYMBOL_FIELD;
			s.name = getName(field);
			s.owner = owner;
			s.type = field->getType().getAsString();
			s.usr = getUSR(field, ctx);
			s.context = "";
			s.file = locToFileDisplayColumn(ctx, field->getLocation(),
			    s.line, s.column);
			s.local = false;
			s.definition = true;

			out->symbols.push_back(std::move(s));
		}
	}

	ASTContext& ctx;
	semindex* out;
	std::string currentFunction;
	std::set<std::string> typeUses;
	std::set<std::string> functionSymbols;
};

/* ============================================================
 * AST Consumer / FrontendAction
 * ============================================================ */

class SemindexASTConsumer : public ASTConsumer {
    public:
	SemindexASTConsumer(ASTContext& ctx, semindex* out)
	    : visitor(ctx, out)
	{
	}

	void HandleTranslationUnit(ASTContext& ctx) override
	{
		visitor.TraverseDecl(ctx.getTranslationUnitDecl());
	}

    private:
	SemindexVisitor visitor;
};

class SemindexFrontendAction : public ASTFrontendAction {
    public:
	explicit SemindexFrontendAction(semindex* out)
	    : out(out)
	{
	}

	std::unique_ptr<ASTConsumer> CreateASTConsumer(
	    CompilerInstance& CI, StringRef) override
	{
		CI.getPreprocessor().addPPCallbacks(
		    std::make_unique<SemindexPPCallbacks>(
		        CI.getSourceManager(), out));

		return std::make_unique<SemindexASTConsumer>(
		    CI.getASTContext(), out);
	}

    private:
	semindex* out;
};

class SemindexActionFactory : public FrontendActionFactory {
    public:
	explicit SemindexActionFactory(semindex* out)
	    : out(out)
	{
	}

	std::unique_ptr<FrontendAction> create() override
	{
		return std::make_unique<SemindexFrontendAction>(out);
	}

    private:
	semindex* out;
};

/* ============================================================
 * C API implementation
 * ============================================================ */

extern "C" {

semindex_t* semindex_create(void) { return new semindex {}; }

void semindex_destroy(semindex_t* s) { delete s; }

int semindex_index_file(semindex_t* s, const char* compile_commands_json, const char* source_file)
{
	if (!s || !source_file)
		return -1;

	std::string error;
	std::unique_ptr<CompilationDatabase> db
	    = loadCompileCommands(compile_commands_json, error);

	if (!db) {
		llvm::errs() << "semindex: failed to load compilation database";
		if (compile_commands_json)
			llvm::errs() << " from '" << compile_commands_json << "'";
		if (!error.empty())
			llvm::errs() << ": " << error;
		llvm::errs() << "\n";
		return -1;
	}

	ClangTool tool(*db, { source_file });

	SemindexActionFactory factory(s);
	int ret = tool.run(&factory);
	if (ret == 0)
		rebuildRecords(s);

	return ret;
}

size_t semindex_symbol_count(const semindex_t* s)
{
	return s ? s->symbols.size() : 0;
}

const semindex_symbol_t* semindex_get_symbol(const semindex_t* s, size_t idx)
{
	if (!s || idx >= s->symbol_records.size())
		return nullptr;

	return &s->symbol_records[idx];
}

size_t semindex_use_count(const semindex_t* s) { return s ? s->uses.size() : 0; }

const semindex_use_t* semindex_get_use(const semindex_t* s, size_t idx)
{
	if (!s || idx >= s->use_records.size())
		return nullptr;

	return &s->use_records[idx];
}

} /* extern "C" */
