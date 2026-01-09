// SPDX-License-Identifier: GPL-2.0-or-later
#include <iostream>
#include <string>

#include "clang/AST/AST.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Index/USRGeneration.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/CommandLine.h"

using namespace clang;
using namespace clang::tooling;

static llvm::cl::OptionCategory SemindCategory("semind options");

static std::string locToString(const ASTContext& ctx, SourceLocation loc)
{
	const SourceManager& sm = ctx.getSourceManager();
	PresumedLoc ploc = sm.getPresumedLoc(loc);
	if (!ploc.isValid())
		return "<invalid>";
	return std::string(ploc.getFilename()) + ":"
	    + std::to_string(ploc.getLine()) + ":"
	    + std::to_string(ploc.getColumn());
}

static std::string getUSR(const Decl* D, const ASTContext& ctx)
{
	llvm::SmallVector<char, 128> buf;
	if (index::generateUSRForDecl(D, buf, ctx.getLangOpts()))
		return "<no-usr>";
	return std::string(buf.begin(), buf.end());
}

static std::string getName(const Decl* D)
{
	if (auto* ND = llvm::dyn_cast<NamedDecl>(D))
		return ND->getNameAsString();
	return "<anon>";
}

class SemindVisitor : public RecursiveASTVisitor<SemindVisitor> {
    public:
	explicit SemindVisitor(ASTContext& ctx)
	    : ctx(ctx)
	{
	}

	bool VisitVarDecl(VarDecl* D)
	{
		if (!D->isFileVarDecl())
			return true;

		reportDecl("var", D, D->getType());
		return true;
	}

	bool VisitFieldDecl(FieldDecl* D)
	{
		reportDecl("field", D, D->getType());
		return true;
	}

	bool VisitDeclRefExpr(DeclRefExpr* E)
	{
		const ValueDecl* D = E->getDecl();
		reportUse("use", D, E->getExprLoc());
		return true;
	}

	bool VisitMemberExpr(MemberExpr* E)
	{
		const ValueDecl* D = E->getMemberDecl();
		reportUse("member-use", D, E->getExprLoc());
		return true;
	}

    private:
	ASTContext& ctx;

	void reportDecl(const char* kind, const Decl* D, QualType qt)
	{
		std::cout << kind << " name=" << getName(D)
			  << " type=" << qt.getAsString()
			  << " loc=" << locToString(ctx, D->getLocation())
			  << " usr=" << getUSR(D, ctx) << "\n";
	}

	void reportUse(const char* kind, const Decl* D, SourceLocation loc)
	{
		std::cout << kind << " target=" << getName(D)
			  << " loc=" << locToString(ctx, loc)
			  << " usr=" << getUSR(D, ctx) << "\n";
	}
};

class SemindASTConsumer : public ASTConsumer {
    public:
	explicit SemindASTConsumer(ASTContext& ctx)
	    : visitor(ctx)
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
	std::unique_ptr<ASTConsumer> CreateASTConsumer(
	    CompilerInstance& CI, StringRef) override
	{
		return std::make_unique<SemindASTConsumer>(CI.getASTContext());
	}
};

int main(int argc, const char** argv)
{
	auto ExpectedParser
	    = CommonOptionsParser::create(argc, argv, SemindCategory);

	if (!ExpectedParser) {
		llvm::errs() << ExpectedParser.takeError();
		return 1;
	}

	CommonOptionsParser& OptionsParser = ExpectedParser.get();
	ClangTool Tool(
	    OptionsParser.getCompilations(), OptionsParser.getSourcePathList());

	return Tool.run(newFrontendActionFactory<SemindFrontendAction>().get());
}
