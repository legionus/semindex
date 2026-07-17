// SPDX-License-Identifier: GPL-2.0-or-later
#include "semindex.h"

/* ==== LLVM / Clang ==== */
#include <clang/AST/AST.h>
#include <clang/AST/ParentMapContext.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/FrontendActions.h>
#include <clang/Index/USRGeneration.h>
#include <clang/Tooling/CompilationDatabase.h>
#include <clang/Tooling/Tooling.h>

#include <llvm/ADT/SmallVector.h>
#include <llvm/Support/Path.h>
#include <llvm/Support/raw_ostream.h>

/* ==== STL ==== */
#include <memory>
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

/* ============================================================
 * Internal C++ model
 * ============================================================ */

struct SemindexSymbol {
	semindex_symbol_kind_t kind;
	std::string name;
	std::string type;
	std::string usr;
	std::string file;
	unsigned line;
	unsigned column;
};

struct SemindexUse {
	semindex_use_kind_t kind;
	std::string usr;
	std::string file;
	unsigned line;
	unsigned column;
};

struct semindex {
	std::vector<SemindexSymbol> symbols;
	std::vector<SemindexUse> uses;
	std::vector<semindex_symbol_t> symbol_records;
	std::vector<semindex_use_t> use_records;
};

static void rebuildRecords(semindex* s)
{
	s->symbol_records.clear();
	s->symbol_records.reserve(s->symbols.size());

	for (const auto& sym : s->symbols) {
		semindex_symbol_t rec;

		rec.kind = sym.kind;
		rec.name = sym.name.c_str();
		rec.type = sym.type.c_str();
		rec.usr = sym.usr.c_str();
		rec.file = sym.file.c_str();
		rec.line = sym.line;
		rec.column = sym.column;

		s->symbol_records.push_back(rec);
	}

	s->use_records.clear();
	s->use_records.reserve(s->uses.size());

	for (const auto& use : s->uses) {
		semindex_use_t rec;

		rec.kind = use.kind;
		rec.usr = use.usr.c_str();
		rec.file = use.file.c_str();
		rec.line = use.line;
		rec.column = use.column;

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

	bool VisitVarDecl(VarDecl* D)
	{
		SemindexSymbol s;
		s.kind = SEMINDEX_SYMBOL_VAR;
		s.name = getName(D);
		s.type = D->getType().getAsString();
		s.usr = getUSR(D, ctx);
		s.file = locToFile(ctx, D->getLocation(), s.line, s.column);

		out->symbols.push_back(std::move(s));
		return true;
	}

	bool VisitFieldDecl(FieldDecl* D)
	{
		SemindexSymbol s;
		s.kind = SEMINDEX_SYMBOL_FIELD;
		s.name = getName(D);
		s.type = D->getType().getAsString();
		s.usr = getUSR(D, ctx);
		s.file = locToFile(ctx, D->getLocation(), s.line, s.column);

		out->symbols.push_back(std::move(s));
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
		u.usr = getUSR(D, ctx);
		u.file = locToFile(ctx, E->getExprLoc(), u.line, u.column);

		out->uses.push_back(std::move(u));
		return true;
	}

	bool VisitMemberExpr(MemberExpr* E)
	{
		const ValueDecl* D = E->getMemberDecl();
		if (!D)
			return true;

		SemindexUse u;
		u.kind = classifyUse(E, ctx);
		u.usr = getUSR(D, ctx);
		u.file = locToFile(ctx, E->getExprLoc(), u.line, u.column);

		out->uses.push_back(std::move(u));
		return true;
	}

	bool VisitRecordDecl(RecordDecl* D)
	{
		if (!D->isThisDeclarationADefinition())
			return true;

		SemindexSymbol s;
		s.kind
		    = D->isUnion() ? SEMINDEX_SYMBOL_UNION : SEMINDEX_SYMBOL_STRUCT;

		s.name = getName(D);
		s.type = ""; // TODO: fill it later
		s.usr = getUSR(D, ctx);
		s.file = locToFile(ctx, D->getLocation(), s.line, s.column);

		out->symbols.push_back(std::move(s));
		return true;
	}

	bool VisitFunctionDecl(FunctionDecl* D)
	{
		if (!D->isThisDeclarationADefinition())
			return true;

		SemindexSymbol s;
		s.kind = SEMINDEX_SYMBOL_FUNCTION;
		s.name = getName(D);
		s.type = D->getType().getAsString();
		s.usr = getUSR(D, ctx);
		s.file = locToFile(ctx, D->getLocation(), s.line, s.column);

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
		u.usr = getUSR(FD, ctx);
		u.file = locToFile(ctx, E->getExprLoc(), u.line, u.column);

		out->uses.push_back(std::move(u));
		return true;
	}

    private:
	ASTContext& ctx;
	semindex* out;
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
