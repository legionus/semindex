// SPDX-License-Identifier: GPL-2.0-or-later
#include "semind.h"

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

static semind_use_kind_t classifyUse(const Expr* E, ASTContext& ctx)
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
				return SEMIND_USE_ADDR;
		}
	}

	/* x = ... */
	if (parent) {
		if (const auto* B = dyn_cast<BinaryOperator>(parent)) {
			if (B->isAssignmentOp() && B->getLHS() == E)
				return SEMIND_USE_WRITE;
		}
	}

	/* ++x, x++ */
	if (parent) {
		if (const auto* U = dyn_cast<UnaryOperator>(parent)) {
			if (U->isIncrementDecrementOp())
				return SEMIND_USE_WRITE;
		}
	}

	return SEMIND_USE_READ;
}

/* ============================================================
 * Internal C++ model
 * ============================================================ */

struct SemindSymbol {
	semind_symbol_kind_t kind;
	std::string name;
	std::string type;
	std::string usr;
	std::string file;
	unsigned line;
	unsigned column;
};

struct SemindUse {
	semind_use_kind_t kind;
	std::string usr;
	std::string file;
	unsigned line;
	unsigned column;
};

struct semind {
	std::vector<SemindSymbol> symbols;
	std::vector<SemindUse> uses;
};

/* ============================================================
 * AST Visitor
 * ============================================================ */

class SemindVisitor : public RecursiveASTVisitor<SemindVisitor> {
    public:
	SemindVisitor(ASTContext& ctx, semind* out)
	    : ctx(ctx)
	    , out(out)
	{
	}

	bool VisitVarDecl(VarDecl* D)
	{
		SemindSymbol s;
		s.kind = SEMIND_SYMBOL_VAR;
		s.name = getName(D);
		s.type = D->getType().getAsString();
		s.usr = getUSR(D, ctx);
		s.file = locToFile(ctx, D->getLocation(), s.line, s.column);

		out->symbols.push_back(std::move(s));
		return true;
	}

	bool VisitFieldDecl(FieldDecl* D)
	{
		SemindSymbol s;
		s.kind = SEMIND_SYMBOL_FIELD;
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

		SemindUse u;
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

		SemindUse u;
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

		SemindSymbol s;
		s.kind
		    = D->isUnion() ? SEMIND_SYMBOL_UNION : SEMIND_SYMBOL_STRUCT;

		s.name = getName(D);
		s.type = ""; // TODO: fill it later
		s.usr = getUSR(D, ctx);
		s.file = locToFile(ctx, D->getLocation(), s.line, s.column);

		out->symbols.push_back(std::move(s));
		return true;
	}

    private:
	ASTContext& ctx;
	semind* out;
};

/* ============================================================
 * AST Consumer / FrontendAction
 * ============================================================ */

class SemindASTConsumer : public ASTConsumer {
    public:
	SemindASTConsumer(ASTContext& ctx, semind* out)
	    : visitor(ctx, out)
	{
	}

	void HandleTranslationUnit(ASTContext& ctx) override
	{
		visitor.TraverseDecl(ctx.getTranslationUnitDecl());
	}

    private:
	SemindVisitor visitor;
};

class SemindFrontendAction : public ASTFrontendAction {
    public:
	explicit SemindFrontendAction(semind* out)
	    : out(out)
	{
	}

	std::unique_ptr<ASTConsumer> CreateASTConsumer(
	    CompilerInstance& CI, StringRef) override
	{
		return std::make_unique<SemindASTConsumer>(
		    CI.getASTContext(), out);
	}

    private:
	semind* out;
};

class SemindActionFactory : public FrontendActionFactory {
    public:
	explicit SemindActionFactory(semind* out)
	    : out(out)
	{
	}

	std::unique_ptr<FrontendAction> create() override
	{
		return std::make_unique<SemindFrontendAction>(out);
	}

    private:
	semind* out;
};

/* ============================================================
 * C API implementation
 * ============================================================ */

extern "C" {

semind_t* semind_create(void) { return new semind {}; }

void semind_destroy(semind_t* s) { delete s; }

int semind_index_file(semind_t* s, const char* compile_commands_json, const char* source_file)
{
	if (!s || !source_file)
		return -1;

	std::string error;
	std::unique_ptr<CompilationDatabase> db;

	if (compile_commands_json) {
		db = CompilationDatabase::loadFromDirectory(
		    compile_commands_json, error);
	}

	if (!db) {
		/* fallback: empty database */
		db = std::make_unique<FixedCompilationDatabase>(
		    ".", std::vector<std::string> {});
	}

	ClangTool tool(*db, { source_file });

	SemindActionFactory factory(s);
	return tool.run(&factory);
}

size_t semind_symbol_count(const semind_t* s)
{
	return s ? s->symbols.size() : 0;
}

const semind_symbol_t* semind_get_symbol(const semind_t* s, size_t idx)
{
	if (!s || idx >= s->symbols.size())
		return nullptr;

	static semind_symbol_t out;
	const auto& sym = s->symbols[idx];

	out.kind = sym.kind;
	out.name = sym.name.c_str();
	out.type = sym.type.c_str();
	out.usr = sym.usr.c_str();
	out.file = sym.file.c_str();
	out.line = sym.line;
	out.column = sym.column;

	return &out;
}

size_t semind_use_count(const semind_t* s) { return s ? s->uses.size() : 0; }

const semind_use_t* semind_get_use(const semind_t* s, size_t idx)
{
	if (!s || idx >= s->uses.size())
		return nullptr;

	static semind_use_t out;
	const auto& u = s->uses[idx];

	out.kind = u.kind;
	out.usr = u.usr.c_str();
	out.file = u.file.c_str();
	out.line = u.line;
	out.column = u.column;

	return &out;
}

} /* extern "C" */
