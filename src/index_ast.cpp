// SPDX-License-Identifier: GPL-2.0-or-later
#include "index_pp.h"
#include "semindex_internal.h"

#include <clang/AST/AST.h>
#include <clang/AST/ParentMapContext.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/AST/TypeLoc.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/FrontendActions.h>
#include <clang/Index/USRGeneration.h>
#include <clang/Lex/Preprocessor.h>
#include <clang/Tooling/Tooling.h>

#include <llvm/ADT/SmallVector.h>
#include <llvm/Support/xxhash.h>

#include <memory>
#include <set>
#include <string>
#include <tuple>
#include <unordered_map>

using namespace clang;
using namespace clang::tooling;

static std::string getName(const Decl *D)
{
	if (const auto *ND = llvm::dyn_cast<NamedDecl>(D))
		return ND->getNameAsString();
	return "";
}

static std::string getUSR(const Decl *D, const ASTContext &ctx)
{
	llvm::SmallVector<char, 128> buf;
	if (index::generateUSRForDecl(D, buf, ctx.getLangOpts()))
		return "";
	return std::string(buf.begin(), buf.end());
}

static bool isCallWrapper(const Stmt *S)
{
	if (isa<ParenExpr>(S) || isa<ImplicitCastExpr>(S))
		return true;

	const auto *U = dyn_cast<UnaryOperator>(S);
	return U && (U->getOpcode() == UO_Deref || U->getOpcode() == UO_AddrOf);
}

static bool isDirectCallCallee(const Expr *E, ASTContext &ctx)
{
	const Expr *current = E;

	for (;;) {
		auto parents = ctx.getParents(*current);
		if (parents.empty())
			return false;

		const Stmt *parent = parents.begin()->get<Stmt>();
		if (!parent)
			return false;

		if (const auto *call = dyn_cast<CallExpr>(parent))
			return call->getCallee()->IgnoreParenImpCasts() == current->IgnoreParenImpCasts();

		if (!isCallWrapper(parent))
			return false;

		current = cast<Expr>(parent);
	}
}

static const Stmt *getParentStmt(const Expr *E, ASTContext &ctx)
{
	auto parents = ctx.getParents(*E);
	if (parents.empty())
		return nullptr;

	return parents.begin()->get<Stmt>();
}

static const Expr *ignoreParenImpCasts(const Expr *E)
{
	return E ? E->IgnoreParenImpCasts() : nullptr;
}

static bool isPointerReadOperand(const Expr *E, ASTContext &ctx)
{
	const Expr *current = E;

	for (;;) {
		const Stmt *parent = getParentStmt(current, ctx);
		if (!parent)
			return false;

		if (const auto *U = dyn_cast<UnaryOperator>(parent)) {
			if (U->getOpcode() == UO_Deref)
				return true;
		}

		if (const auto *M = dyn_cast<MemberExpr>(parent)) {
			if (M->isArrow() && ignoreParenImpCasts(M->getBase()) == ignoreParenImpCasts(E))
				return true;
		}

		if (!isa<ParenExpr>(parent) && !isa<ImplicitCastExpr>(parent))
			return false;

		current = cast<Expr>(parent);
	}
}

static bool isCallCallee(const Expr *E, ASTContext &ctx)
{
	const Expr *current = E;

	for (;;) {
		const Stmt *parent = getParentStmt(current, ctx);
		if (!parent)
			return false;

		if (const auto *call = dyn_cast<CallExpr>(parent))
			return ignoreParenImpCasts(call->getCallee()) == ignoreParenImpCasts(current);

		if (!isCallWrapper(parent))
			return false;

		current = cast<Expr>(parent);
	}
}

static semindex_use_kind_t classifyUse(const Expr *E, ASTContext &ctx)
{
	const Expr *current = E->IgnoreParenImpCasts();

	for (;;) {
		const Stmt *parent = getParentStmt(current, ctx);

		if (!parent)
			return SEMINDEX_USE_READ;

		/* &x */
		if (const auto *U = dyn_cast<UnaryOperator>(parent)) {
			if (U->getOpcode() == UO_AddrOf)
				return SEMINDEX_USE_ADDR;
			/* ++x, x++ */
			if (U->isIncrementDecrementOp())
				return SEMINDEX_USE_WRITE;
		} else if (const auto *B = dyn_cast<BinaryOperator>(parent)) {
			/* x = ... */
			if (B->isAssignmentOp() && B->getLHS() == current)
				return SEMINDEX_USE_WRITE;
		}

		if (!isa<ParenExpr>(parent) && !isa<ImplicitCastExpr>(parent))
			return SEMINDEX_USE_READ;
		current = cast<Expr>(parent);
	}
}

static unsigned accessModeForUse(semindex_use_kind_t kind, const Expr *E, ASTContext &ctx)
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

static semindex_symbol_kind_t symbolKindForDecl(const ValueDecl *D)
{
	if (isa<FieldDecl>(D))
		return SEMINDEX_SYMBOL_FIELD;
	if (isa<EnumConstantDecl>(D))
		return SEMINDEX_SYMBOL_ENUM_CONSTANT;
	if (isa<FunctionDecl>(D))
		return SEMINDEX_SYMBOL_FUNCTION;
	return SEMINDEX_SYMBOL_VAR;
}

static std::string getOwnerName(const Decl *D)
{
	const auto *FD = dyn_cast<FieldDecl>(D);
	if (!FD)
		return "";

	const auto *RD = FD->getParent();
	if (!RD)
		return "";

	return getName(RD);
}

static const RecordDecl *recordDeclForType(QualType type)
{
	type = type.getCanonicalType();

	if (const auto *RT = type->getAs<RecordType>())
		return RT->getDecl();

	return nullptr;
}

static bool isTypedefType(QualType type)
{
	return type->getAs<TypedefType>() != nullptr;
}

static bool isWrittenAsTypedef(TypeSourceInfo *typeSourceInfo)
{
	return typeSourceInfo && !typeSourceInfo->getTypeLoc().getAsAdjusted<TypedefTypeLoc>().isNull();
}

static bool isAnonymousRecord(const RecordDecl *D)
{
	return D && getName(D).empty();
}

static std::string anonymousRecordNameForVar(const VarDecl *D)
{
	return ":" + getName(D);
}

static std::string anonymousRecordNameForTypedef(const TypedefNameDecl *D)
{
	return ":" + getName(D);
}

static std::string typeNameForRecord(const RecordDecl *D, const std::string &name)
{
	return std::string(D->isUnion() ? "union " : "struct ") + name;
}

static std::string typeNameForTypedef(const TypedefNameDecl *D)
{
	const RecordDecl *anonymousRecord = recordDeclForType(D->getUnderlyingType());

	if (isAnonymousRecord(anonymousRecord))
		return typeNameForRecord(anonymousRecord, anonymousRecordNameForTypedef(D));

	return D->getUnderlyingType().getAsString();
}

/* ============================================================
 * AST Visitor
 * ============================================================ */

class SemindexVisitor : public RecursiveASTVisitor<SemindexVisitor>
{
public:
	SemindexVisitor(ASTContext &ctx, SemindexContext &index) : ctx(ctx), index(index), details(index.details())
	{
	}

	bool TraverseFunctionDecl(FunctionDecl *D)
	{
		if (!D)
			return true;

		std::string oldFunction = currentFunction;
		std::string oldFunctionUSR = currentFunctionUSR;
		unsigned long long oldFunctionUSRId = currentFunctionUSRId;
		currentFunction = getName(D);
		currentFunctionUSR = D->doesThisDeclarationHaveABody() ? functionUSR(D) : "";
		currentFunctionUSRId = currentFunctionUSR.empty() ? 0 : llvm::xxHash64(currentFunctionUSR);
		bool ret = RecursiveASTVisitor<SemindexVisitor>::TraverseFunctionDecl(D);
		currentFunction = oldFunction;
		currentFunctionUSR = oldFunctionUSR;
		currentFunctionUSRId = oldFunctionUSRId;
		return ret;
	}

	bool VisitVarDecl(VarDecl *D)
	{
		if (getName(D).empty() || isNonDefinitionParameter(D))
			return true;
		if (!index.includeLocal() && !currentFunction.empty())
			return true;

		const RecordDecl *anonymousRecord = recordDeclForType(D->getType());
		std::string anonymousName;
		std::string typeName;

		if (!isTypedefType(D->getType()) && !isWrittenAsTypedef(D->getTypeSourceInfo()) &&
			isAnonymousRecord(anonymousRecord)) {
			anonymousName = anonymousRecordNameForVar(D);
			if (details)
				typeName = typeNameForRecord(anonymousRecord, anonymousName);
			addAnonymousRecordSymbols(anonymousRecord, anonymousName);
		} else if (details) {
			typeName = D->getType().getAsString();
		}

		SemindexSymbol s;
		s.kind = SEMINDEX_SYMBOL_VAR;
		s.name = getName(D);
		s.owner = "";
		s.type = typeName;
		s.usr = detailedUSR(D);
		s.context = currentFunction;
		s.loc = index.location(D->getLocation());
		s.local = !currentFunction.empty();
		s.definition = true;

		index.addSymbolInScope(std::move(s), D->getLocation());

		if (D->hasInit()) {
			SemindexUse u;
			u.kind = SEMINDEX_USE_WRITE;
			u.symbol_kind = SEMINDEX_SYMBOL_VAR;
			u.mode = SEMINDEX_MODE_W_VAL;
			u.name = getName(D);
			u.owner = "";
			u.type = typeName;
			u.usr = detailedUSR(D);
			u.context = currentFunction;
			u.loc = index.location(D->getLocation());
			u.local = !currentFunction.empty();

			index.addUseInScope(std::move(u), D->getLocation());
		}

		return true;
	}

	bool VisitFieldDecl(FieldDecl *D)
	{
		if (isAnonymousRecord(D->getParent()))
			return true;
		const ValueInfo &info = valueInfo(D);
		SemindexSymbol s;
		s.kind = info.kind;
		s.name = info.name;
		s.owner = info.owner;
		s.type = info.type;
		s.usr = info.usr;
		s.context = "";
		s.loc = index.location(D->getLocation());
		s.local = false;
		s.definition = true;

		index.addSymbolInScope(std::move(s), D->getLocation());
		return true;
	}

	bool VisitTypedefNameDecl(TypedefNameDecl *D)
	{
		if (!index.includeLocal() && !currentFunction.empty())
			return true;

		const RecordDecl *anonymousRecord = recordDeclForType(D->getUnderlyingType());
		std::string anonymousName;
		std::string typeName = details ? typeNameForTypedef(D) : "";

		if (isAnonymousRecord(anonymousRecord)) {
			anonymousName = anonymousRecordNameForTypedef(D);
			addAnonymousRecordSymbols(anonymousRecord, anonymousName);
		}

		SemindexSymbol s;
		s.kind = SEMINDEX_SYMBOL_TYPEDEF;
		s.name = getName(D);
		s.owner = "";
		s.type = typeName;
		s.usr = detailedUSR(D);
		s.context = currentFunction;
		s.loc = index.location(D->getLocation());
		s.local = !currentFunction.empty();
		s.definition = true;

		index.addSymbolInScope(std::move(s), D->getLocation());
		return true;
	}

	bool VisitTypedefTypeLoc(TypedefTypeLoc TL)
	{
		const TypedefNameDecl *D = TL.getTypePtr()->getDecl();

		addTypeUse(D, SEMINDEX_SYMBOL_TYPEDEF, TL.getNameLoc(), details ? typeNameForTypedef(D) : "");
		return true;
	}

	bool VisitRecordTypeLoc(RecordTypeLoc TL)
	{
		if (TL.isDefinition())
			return true;

		const RecordDecl *D = TL.getDecl();
		if (isAnonymousRecord(D))
			return true;

		addTypeUse(D, D->isUnion() ? SEMINDEX_SYMBOL_UNION : SEMINDEX_SYMBOL_STRUCT, TL.getNameLoc(), "");
		return true;
	}

	bool VisitEnumTypeLoc(EnumTypeLoc TL)
	{
		if (TL.isDefinition())
			return true;

		const EnumDecl *D = TL.getDecl();
		if (!D || getName(D).empty())
			return true;

		addTypeUse(D, SEMINDEX_SYMBOL_ENUM, TL.getNameLoc(), "");
		return true;
	}

	bool VisitDeclRefExpr(DeclRefExpr *E)
	{
		const ValueDecl *D = E->getDecl();
		SourceLocation spelling;
		bool local;

		if (!D)
			return true;
		local = !currentFunction.empty() && !D->hasExternalFormalLinkage();
		if (local && !index.includeLocal())
			return true;

		if (isa<FunctionDecl>(D) && isDirectCallCallee(E, ctx))
			return true;

		const ValueInfo &info = valueInfo(D);
		spelling = index.spellingLoc(E->getExprLoc());
		SemindexUse u;
		u.kind = classifyUse(E, ctx);
		u.symbol_kind = info.kind;
		u.mode = accessModeForUse(u.kind, E, ctx);
		u.name = info.name;
		u.owner = info.owner;
		u.type = info.type;
		u.usr = info.usr;
		u.context = currentFunction;
		u.loc = index.location(spelling);
		u.local = local;

		index.addUseInScope(std::move(u), spelling);

		if (!isa<FunctionDecl>(D) && isCallCallee(E, ctx))
			addValueUse(D, SEMINDEX_USE_CALL, SEMINDEX_MODE_R_PTR, currentFunction, E->getExprLoc(), local);

		return true;
	}

	bool VisitMemberExpr(MemberExpr *E)
	{
		const ValueDecl *D = E->getMemberDecl();
		if (!D)
			return true;

		semindex_use_kind_t kind = classifyUse(E, ctx);
		addValueUse(D, kind, accessModeForUse(kind, E, ctx), currentFunction, E->getExprLoc(), false);
		return true;
	}

	bool VisitDesignatedInitExpr(DesignatedInitExpr *E)
	{
		for (const auto &designator : E->designators()) {
			if (!designator.isFieldDesignator())
				continue;

			const FieldDecl *D = designator.getFieldDecl();
			if (!D)
				continue;
			addValueUse(D, SEMINDEX_USE_WRITE, SEMINDEX_MODE_W_VAL, currentFunction,
				designator.getFieldLoc(), false);
		}

		return true;
	}

	bool VisitRecordDecl(RecordDecl *D)
	{
		if (!D->isThisDeclarationADefinition())
			return true;
		if (isAnonymousRecord(D))
			return true;

		SemindexSymbol s;
		s.kind = D->isUnion() ? SEMINDEX_SYMBOL_UNION : SEMINDEX_SYMBOL_STRUCT;

		s.name = getName(D);
		s.owner = "";
		s.type = ""; // TODO: fill it later
		s.usr = detailedUSR(D);
		s.context = "";
		s.loc = index.location(D->getLocation());
		s.local = false;
		s.definition = true;

		index.addSymbolInScope(std::move(s), D->getLocation());
		return true;
	}

	bool VisitEnumDecl(EnumDecl *D)
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
		s.usr = detailedUSR(D);
		s.context = "";
		s.loc = index.location(D->getLocation());
		s.local = false;
		s.definition = true;

		index.addSymbolInScope(std::move(s), D->getLocation());
		return true;
	}

	bool VisitEnumConstantDecl(EnumConstantDecl *D)
	{
		SemindexSymbol s;
		s.kind = SEMINDEX_SYMBOL_ENUM_CONSTANT;
		s.name = getName(D);
		s.owner = "";
		if (details)
			s.type = D->getType().getAsString();
		s.usr = detailedUSR(D);
		s.context = "";
		s.loc = index.location(D->getLocation());
		s.local = false;
		s.definition = true;

		index.addSymbolInScope(std::move(s), D->getLocation());
		return true;
	}

	bool VisitFunctionDecl(FunctionDecl *D)
	{
		if (!D->isThisDeclarationADefinition() && !D->hasPrototype())
			return true;
		if (!functionSymbols.insert(D->getCanonicalDecl()).second)
			return true;
		const ValueInfo &info = valueInfo(D);

		SemindexSymbol s;
		s.kind = SEMINDEX_SYMBOL_FUNCTION;
		s.name = info.name;
		s.owner = "";
		s.type = info.type;
		s.usr = functionUSR(D);
		s.context = "";
		s.loc = index.location(D->getLocation());
		s.local = false;
		s.definition = D->isThisDeclarationADefinition();

		index.addSymbolInScope(std::move(s), D->getLocation());
		return true;
	}

	bool VisitCallExpr(CallExpr *E)
	{
		const FunctionDecl *FD = E->getDirectCallee();
		SourceLocation spelling;
		if (!FD)
			return true; /* indirect call: fp() */

		spelling = index.spellingLoc(E->getExprLoc());
		const ValueInfo &info = valueInfo(FD);
		SemindexUse u;
		u.kind = SEMINDEX_USE_CALL;
		u.symbol_kind = SEMINDEX_SYMBOL_FUNCTION;
		u.mode = SEMINDEX_MODE_R_PTR;
		u.name = info.name;
		u.owner = "";
		u.type = info.type;
		u.usr = functionUSR(FD);
		u.context = currentFunction;
		u.context_usr = currentFunctionUSR;
		u.usr_id = u.usr.empty() ? 0 : llvm::xxHash64(u.usr);
		u.context_usr_id = currentFunctionUSRId;
		u.loc = index.location(spelling);
		u.local = false;

		index.addUseInScope(std::move(u), spelling);
		return true;
	}

private:
	struct ValueInfo {
		semindex_symbol_kind_t kind;
		std::string name;
		std::string owner;
		std::string type;
		std::string usr;
	};

	std::string detailedUSR(const Decl *D) const
	{
		return details ? getUSR(D, ctx) : "";
	}

	const ValueInfo &valueInfo(const ValueDecl *D)
	{
		auto [entry, inserted] = valueInfoCache.try_emplace(D);

		if (inserted) {
			entry->second.kind = symbolKindForDecl(D);
			entry->second.name = getName(D);
			entry->second.owner = getOwnerName(D);
			if (details) {
				entry->second.type = D->getType().getAsString();
				entry->second.usr = getUSR(D, ctx);
			}
		}
		return entry->second;
	}

	const std::string &functionUSR(const FunctionDecl *D)
	{
		D = D->getCanonicalDecl();
		auto [entry, inserted] = functionUSRCache.try_emplace(D);

		if (inserted)
			entry->second = getUSR(D, ctx);
		return entry->second;
	}

	static bool isNonDefinitionParameter(const VarDecl *D)
	{
		const auto *P = dyn_cast<ParmVarDecl>(D);
		if (!P)
			return false;

		const auto *F = dyn_cast_or_null<FunctionDecl>(P->getDeclContext());
		return !F || !F->doesThisDeclarationHaveABody();
	}

	void addTypeUse(const TypeDecl *D, semindex_symbol_kind_t kind, SourceLocation loc, const std::string &type)
	{
		if (!D || loc.isInvalid())
			return;
		if (!index.includeLocal() && !currentFunction.empty())
			return;

		SourceLocation spelling = index.spellingLoc(loc);
		if (!index.inScope(spelling))
			return;
		if (!typeUses.emplace(D->getCanonicalDecl(), spelling.getRawEncoding(), currentFunction).second)
			return;

		SemindexUse u;
		u.kind = SEMINDEX_USE_READ;
		u.symbol_kind = kind;
		u.mode = SEMINDEX_MODE_R_VAL;
		u.name = getName(D);
		u.owner = "";
		u.type = type;
		u.usr = detailedUSR(D);
		u.context = currentFunction;
		u.loc = index.location(spelling);
		u.local = !currentFunction.empty();

		index.addUseInScope(std::move(u), spelling);
	}

	void addValueUse(const ValueDecl *D, semindex_use_kind_t kind, unsigned mode, const std::string &context,
		SourceLocation loc, bool local)
	{
		const ValueInfo &info = valueInfo(D);
		SourceLocation spelling = index.spellingLoc(loc);
		SemindexUse u;
		u.kind = kind;
		u.symbol_kind = info.kind;
		u.mode = mode;
		u.name = info.name;
		u.owner = info.owner;
		u.type = info.type;
		u.usr = info.usr;
		u.context = context;
		u.loc = index.location(spelling);
		u.local = local;

		index.addUseInScope(std::move(u), spelling);
	}

	void addAnonymousRecordSymbols(const RecordDecl *D, const std::string &name)
	{
		SemindexSymbol s;
		s.kind = D->isUnion() ? SEMINDEX_SYMBOL_UNION : SEMINDEX_SYMBOL_STRUCT;
		s.name = name;
		s.owner = "";
		s.type = "";
		s.usr = detailedUSR(D);
		s.context = "";
		s.loc = index.displayLocation(ctx,
			D->getBraceRange().getBegin().isValid() ? D->getBraceRange().getBegin() : D->getLocation());
		s.local = false;
		s.definition = true;

		index.addSymbolInScope(std::move(s), D->getLocation());
		addAnonymousRecordFields(D, name);
	}

	void addAnonymousRecordFields(const RecordDecl *D, const std::string &owner)
	{
		for (const FieldDecl *field : D->fields()) {
			const RecordDecl *nested = recordDeclForType(field->getType());

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
			if (details)
				s.type = field->getType().getAsString();
			s.usr = detailedUSR(field);
			s.context = "";
			s.loc = index.displayLocation(ctx, field->getLocation());
			s.local = false;
			s.definition = true;

			index.addSymbolInScope(std::move(s), field->getLocation());
		}
	}

	ASTContext &ctx;
	SemindexContext &index;
	bool details;
	std::string currentFunction;
	std::string currentFunctionUSR;
	unsigned long long currentFunctionUSRId = 0;
	std::set<std::tuple<const Decl *, unsigned, std::string>> typeUses;
	std::set<const FunctionDecl *> functionSymbols;
	std::unordered_map<const ValueDecl *, ValueInfo> valueInfoCache;
	std::unordered_map<const FunctionDecl *, std::string> functionUSRCache;
};

/* ============================================================
 * AST Consumer / FrontendAction
 * ============================================================ */

class SemindexASTConsumer : public ASTConsumer
{
public:
	SemindexASTConsumer(semindex *out, ASTContext &ctx, SemindexContext index)
	    : out(out), index(index), visitor(ctx, this->index)
	{
	}

	void HandleTranslationUnit(ASTContext &ctx) override
	{
		out->has_index_data = true;
		visitor.TraverseDecl(ctx.getTranslationUnitDecl());
	}

private:
	semindex *out;
	SemindexContext index;
	SemindexVisitor visitor;
};

class SemindexFrontendAction : public ASTFrontendAction
{
public:
	explicit SemindexFrontendAction(semindex *out) : out(out)
	{
	}

	std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI, StringRef) override
	{
		SemindexContext index(out, CI.getSourceManager());

		CI.getPreprocessor().addPPCallbacks(createSemindexPPCallbacks(index));

		return std::make_unique<SemindexASTConsumer>(out, CI.getASTContext(), index);
	}

	void EndSourceFileAction() override
	{
		captureDiagnostics(out, getCompilerInstance().getDiagnostics());
	}

private:
	semindex *out;
};

class SemindexActionFactory : public FrontendActionFactory
{
public:
	explicit SemindexActionFactory(semindex *out) : out(out)
	{
	}

	std::unique_ptr<FrontendAction> create() override
	{
		return std::make_unique<SemindexFrontendAction>(out);
	}

private:
	semindex *out;
};

std::unique_ptr<FrontendActionFactory> createSemindexActionFactory(semindex *out)
{
	return std::make_unique<SemindexActionFactory>(out);
}
