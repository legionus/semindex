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
#include <clang/Lex/Lexer.h>
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
		if (E->getType()->isFunctionType() || isCallCallee(E, ctx) || isPointerReadOperand(E, ctx))
			return SEMINDEX_MODE_R_PTR;

		return SEMINDEX_MODE_R_VAL;
	}

	return 0;
}

static semindex_symbol_kind_t symbolKindForDecl(const ValueDecl *D)
{
	if (isa<FieldDecl>(D) || isa<IndirectFieldDecl>(D))
		return SEMINDEX_SYMBOL_FIELD;

	if (isa<EnumConstantDecl>(D))
		return SEMINDEX_SYMBOL_ENUM_CONSTANT;

	if (isa<FunctionDecl>(D))
		return SEMINDEX_SYMBOL_FUNCTION;

	return SEMINDEX_SYMBOL_VAR;
}

static bool hasRecordOwner(const Decl *D)
{
	return isa<FieldDecl>(D) || isa<IndirectFieldDecl>(D) || isa<CXXMethodDecl>(D);
}

static std::string namespacePrefix(const NamedDecl *D)
{
	std::string prefix;

	for (const DeclContext *DC = D->getDeclContext(); DC; DC = DC->getParent()) {
		const auto *NS = dyn_cast<NamespaceDecl>(DC);

		if (!NS)
			break;

		prefix = NS->getNameAsString() + "::" + prefix;
	}
	return prefix;
}

static std::string qualifiedRecordName(const RecordDecl *D)
{
	std::string prefix = namespacePrefix(D);

	if (!prefix.empty())
		prefix += D->getNameAsString();

	return prefix.empty() ? D->getNameAsString() : prefix;
}

static std::string qualifiedEnumName(const EnumDecl *D)
{
	std::string prefix = namespacePrefix(D);

	if (!prefix.empty())
		prefix += D->getNameAsString();

	return prefix.empty() ? D->getNameAsString() : prefix;
}

static std::string getOwnerName(const Decl *D)
{
	if (!hasRecordOwner(D))
		return "";

	const auto *RD = dyn_cast<RecordDecl>(D->getDeclContext());

	if (!RD)
		return "";

	return qualifiedRecordName(RD);
}

static semindex_symbol_kind_t recordKindForDecl(const RecordDecl *D)
{
	if (D->isUnion())
		return SEMINDEX_SYMBOL_UNION;

	if (isa<CXXRecordDecl>(D))
		return SEMINDEX_SYMBOL_CLASS;

	return SEMINDEX_SYMBOL_STRUCT;
}

/* A lambda closure is a CXXRecordDecl marked isLambda(); it is indexed as a
 * first-class symbol of kind SEMINDEX_SYMBOL_LAMBDA, and a direct call to the
 * lambda resolves to its operator() method whose parent is that closure. */
static bool isLambdaClosure(const RecordDecl *D)
{
	const auto *CXX = dyn_cast_or_null<CXXRecordDecl>(D);

	return CXX && CXX->isLambda();
}

static const CXXRecordDecl *lambdaClosureFor(const CXXMethodDecl *D)
{
	const CXXRecordDecl *R = D->getParent();

	return (R && R->isLambda()) ? R : nullptr;
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

static std::string stableTypeName(QualType type, const ASTContext &ctx)
{
	const Type *typePtr = type.getTypePtr();

	if (isa<TypeOfExprType>(typePtr) || isa<TypeOfType>(typePtr) || isa<TypedefType>(typePtr))
		return type.getAsString();

	const auto *enumType = type->getAs<EnumType>();

	if (!enumType)
		return type.getAsString();

	const EnumDecl *D = enumType->getDecl();

	if (!D || !getName(D).empty() || D->getTypedefNameForAnonDecl())
		return type.getAsString();

	PresumedLoc loc = ctx.getSourceManager().getPresumedLoc(D->getLocation());

	if (!loc.isValid())
		return "enum (unnamed)";

	return "enum (unnamed at " + std::string(loc.getFilename()) + ":" + std::to_string(loc.getLine()) + ":" +
		std::to_string(loc.getColumn()) + ")";
}

static std::string canonicalTypeName(QualType type, const ASTContext &ctx)
{
	return stableTypeName(type.getCanonicalType(), ctx);
}

struct DeclaredTypeIdentity {
	semindex_symbol_kind_t kind;
	const NamedDecl *decl = nullptr;
};

static DeclaredTypeIdentity declaredTypeIdentity(TypeSourceInfo *info)
{
	if (!info)
		return {};

	for (TypeLoc loc = info->getTypeLoc(); !loc.isNull(); loc = loc.getNextTypeLoc()) {
		if (auto typedefLoc = loc.getAs<TypedefTypeLoc>())
			return { SEMINDEX_SYMBOL_TYPEDEF, typedefLoc.getTypePtr()->getDecl() };

		if (auto recordLoc = loc.getAs<RecordTypeLoc>()) {
			const RecordDecl *D = recordLoc.getDecl();

			return { recordKindForDecl(D), D };
		}

		if (auto enumLoc = loc.getAs<EnumTypeLoc>())
			return { SEMINDEX_SYMBOL_ENUM, enumLoc.getDecl() };
	}

	return {};
}

static std::string typeIdentityName(const NamedDecl *D)
{
	if (const auto *RD = dyn_cast<RecordDecl>(D))
		return qualifiedRecordName(RD);

	if (const auto *ED = dyn_cast<EnumDecl>(D))
		return qualifiedEnumName(ED);

	return getName(D);
}

static void setDeclaredTypeIdentity(SemindexSymbol &symbol, TypeSourceInfo *info, const ASTContext &ctx)
{
	DeclaredTypeIdentity identity = declaredTypeIdentity(info);

	if (!identity.decl || getName(identity.decl).empty())
		return;

	symbol.type_kind = identity.kind;
	symbol.type_symbol = typeIdentityName(identity.decl);
	symbol.type_usr = getUSR(identity.decl, ctx);
}

static void setDeclaredTypeIdentity(SemindexParameter &parameter, TypeSourceInfo *info, const ASTContext &ctx)
{
	DeclaredTypeIdentity identity = declaredTypeIdentity(info);

	if (!identity.decl || getName(identity.decl).empty())
		return;

	parameter.type_kind = identity.kind;
	parameter.type_symbol = typeIdentityName(identity.decl);
	parameter.type_usr = getUSR(identity.decl, ctx);
}

static std::string typeNameForTypedef(const TypedefNameDecl *D, const ASTContext &ctx)
{
	const RecordDecl *anonymousRecord = recordDeclForType(D->getUnderlyingType());

	if (isAnonymousRecord(anonymousRecord))
		return typeNameForRecord(anonymousRecord, anonymousRecordNameForTypedef(D));

	return stableTypeName(D->getUnderlyingType(), ctx);
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

	void finishPointsToStats()
	{
		if (!index.pointsToAnalysis())
			return;

		pointsToStats.unique_direct_constraints = directConstraints.size();
		pointsToStats.unique_copy_constraints = copyConstraints.size();
		pointsToStats.pointer_identities = pointerIdentities.size();
		pointsToStats.function_identities = functionIdentities.size();
		index.setPointsToStats(pointsToStats);
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
		addAliasUse(D);

		if (index.pointsToAnalysis() && D->hasInit() && isFunctionPointer(D->getType()))
			addPointsToConstraints(D, D->getInit());

		if (const auto *cleanup = D->getAttr<CleanupAttr>())
			addFunctionCall(cleanup->getFunctionDecl(), cleanupLocation(D, cleanup));

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

			typeName = typeNameForRecord(anonymousRecord, anonymousName);
			addAnonymousRecordSymbols(anonymousRecord, anonymousName);
		} else {
			typeName = stableTypeName(D->getType(), ctx);
		}

		SemindexSymbol s;

		s.kind = SEMINDEX_SYMBOL_VAR;
		s.name = getName(D);
		s.owner = "";
		s.type = typeName;
		s.canonical_type = canonicalTypeName(D->getType(), ctx);
		s.usr = getUSR(D, ctx);
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

	bool VisitBinaryOperator(BinaryOperator *E)
	{
		if (!index.pointsToAnalysis())
			return true;

		if (E->getOpcode() != BO_Assign)
			return true;

		const ValueDecl *target = pointerDecl(E->getLHS());

		if (target)
			addPointsToConstraints(target, E->getRHS());

		return true;
	}

	bool VisitInitListExpr(InitListExpr *E)
	{
		if (!index.pointsToAnalysis())
			return true;

		if (InitListExpr *semantic = E->getSemanticForm())
			E = semantic;

		if (!pointsToInitLists.insert(E).second)
			return true;

		const RecordDecl *record = recordDeclForType(E->getType());

		if (!record)
			return true;

		if (const RecordDecl *definition = record->getDefinition())
			record = definition;

		if (record->isUnion()) {
			const FieldDecl *field = E->getInitializedFieldInUnion();

			if (field && E->getNumInits() && isFunctionPointer(field->getType()))
				addPointsToConstraints(field, E->getInit(0));

			return true;
		}

		auto field = record->field_begin();

		for (unsigned i = 0; i < E->getNumInits() && field != record->field_end(); i++, ++field) {
			if (isFunctionPointer((*field)->getType()))
				addPointsToConstraints(*field, E->getInit(i));
		}

		return true;
	}

	bool VisitFieldDecl(FieldDecl *D)
	{
		if (isAnonymousRecord(D->getParent()))
			return true;

		if (const auto *CXX = dyn_cast_or_null<CXXRecordDecl>(D->getParent()))
			if (CXX->getTemplateSpecializationKind() != TSK_Undeclared)
				return true;

		SemindexSymbol s;

		s.kind = symbolKindForDecl(D);
		s.name = getName(D);
		s.owner = getOwnerName(D);
		s.type = stableTypeName(D->getType(), ctx);
		s.canonical_type = canonicalTypeName(D->getType(), ctx);
		setDeclaredTypeIdentity(s, D->getTypeSourceInfo(), ctx);
		s.usr = getUSR(D, ctx);
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
		std::string typeName = typeNameForTypedef(D, ctx);

		if (isAnonymousRecord(anonymousRecord)) {
			anonymousName = anonymousRecordNameForTypedef(D);
			addAnonymousRecordSymbols(anonymousRecord, anonymousName);
		}

		SemindexSymbol s;

		s.kind = SEMINDEX_SYMBOL_TYPEDEF;
		s.name = getName(D);
		s.owner = "";
		s.type = typeName;
		s.canonical_type = canonicalTypeName(D->getUnderlyingType(), ctx);
		s.usr = getUSR(D, ctx);
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

		addTypeUse(D, SEMINDEX_SYMBOL_TYPEDEF, TL.getNameLoc(), details ? typeNameForTypedef(D, ctx) : "");
		return true;
	}

	bool VisitRecordTypeLoc(RecordTypeLoc TL)
	{
		if (TL.isDefinition())
			return true;

		const RecordDecl *D = TL.getDecl();

		if (isAnonymousRecord(D))
			return true;

		addTypeUse(D, recordKindForDecl(D), TL.getNameLoc(), "");
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

		if (const auto *field = dyn_cast<FieldDecl>(D)) {
			if (getName(field).empty())
				return true;

			if (isAnonymousRecord(field->getParent())) {
				const RecordDecl *baseRecord = memberBaseRecord(E);
				const IndirectFieldDecl *indirect = promotedFieldFor(field, baseRecord);

				if (indirect) {
					addValueUse(indirect, kind, accessModeForUse(kind, E, ctx), currentFunction,
						E->getExprLoc(), false);
					return true;
				}
			}
		}

		addValueUse(D, kind, accessModeForUse(kind, E, ctx), currentFunction, E->getExprLoc(), false);
		return true;
	}

	bool VisitDesignatedInitExpr(DesignatedInitExpr *E)
	{
		const RecordDecl *initializedRecord = initializedRecordFor(E);

		for (const auto &designator : E->designators()) {
			if (!designator.isFieldDesignator())
				continue;

			const FieldDecl *D = designator.getFieldDecl();

			if (!D)
				continue;

			if (!isAnonymousRecord(D->getParent()))
				addValueUse(D, SEMINDEX_USE_WRITE, SEMINDEX_MODE_W_VAL, currentFunction,
					designator.getFieldLoc(), false);

			if (initializedRecord)
				addPromotedFieldUses(D, initializedRecord, designator.getFieldLoc());
		}

		return true;
	}

	bool VisitRecordDecl(RecordDecl *D)
	{
		if (!D->isThisDeclarationADefinition())
			return true;

		if (isAnonymousRecord(D))
			return true;

		if (const auto *CXX = dyn_cast<CXXRecordDecl>(D))
			if (CXX->getTemplateSpecializationKind() != TSK_Undeclared)
				return true;

		SemindexSymbol s;

		s.kind = recordKindForDecl(D);
		s.name = qualifiedRecordName(D);
		s.owner = "";
		s.type = ""; // TODO: fill it later
		s.usr = getUSR(D, ctx);
		s.context = "";
		s.loc = index.location(D->getLocation());
		s.local = false;
		s.definition = true;

		index.addSymbolInScope(std::move(s), D->getLocation());

		for (Decl *member : D->decls()) {
			if (auto *indirect = dyn_cast<IndirectFieldDecl>(member))
				addIndirectField(indirect);
		}

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
		s.name = qualifiedEnumName(D);
		s.owner = "";
		s.type = "";
		s.usr = getUSR(D, ctx);
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

		s.type = stableTypeName(D->getType(), ctx);
		s.canonical_type = canonicalTypeName(D->getType(), ctx);
		s.usr = getUSR(D, ctx);
		s.context = "";
		s.loc = index.location(D->getLocation());
		s.local = false;
		s.definition = true;

		index.addSymbolInScope(std::move(s), D->getLocation());
		return true;
	}

	bool VisitFunctionDecl(FunctionDecl *D)
	{
		addAliasUse(D);

		if (const auto *M = dyn_cast_or_null<CXXMethodDecl>(D)) {
			const CXXRecordDecl *R = M->getParent();

			if (R && R->getTemplateSpecializationKind() != TSK_Undeclared)
				return true;
		}

		if (!D->isThisDeclarationADefinition() && !D->hasPrototype())
			return true;

		if (!D->isThisDeclarationADefinition() && D->getDefinition())
			return true;

		if (!functionSymbols.insert(D->getCanonicalDecl()).second)
			return true;

		SemindexSymbol s;

		s.kind = SEMINDEX_SYMBOL_FUNCTION;
		s.name = getName(D);
		s.owner = getOwnerName(D);
		s.type = stableTypeName(D->getType(), ctx);
		s.canonical_type = canonicalTypeName(D->getType(), ctx);
		s.usr = functionUSR(D);
		s.context = "";
		s.loc = index.location(D->getLocation());
		s.local = false;
		s.definition = D->isThisDeclarationADefinition();
		s.properties = functionProperties(D);

		index.addSymbolInScope(std::move(s), D->getLocation());

		SemindexFunctionSignature signature;

		signature.name = getName(D);
		signature.owner = getOwnerName(D);
		signature.usr = functionUSR(D);
		signature.return_type = stableTypeName(D->getReturnType(), ctx);
		signature.canonical_return_type = canonicalTypeName(D->getReturnType(), ctx);
		signature.loc = index.location(D->getLocation());
		signature.variadic = D->isVariadic();
		signature.parameters.reserve(D->getNumParams());

		for (const ParmVarDecl *parameterDecl : D->parameters()) {
			QualType parameterType = parameterDecl->getOriginalType();
			SemindexParameter parameter;

			parameter.name = getName(parameterDecl);
			parameter.type = stableTypeName(parameterType, ctx);
			parameter.canonical_type = canonicalTypeName(parameterType, ctx);
			setDeclaredTypeIdentity(parameter, parameterDecl->getTypeSourceInfo(), ctx);
			signature.parameters.push_back(std::move(parameter));
		}

		index.addFunctionSignatureInScope(std::move(signature), D->getLocation());

		return true;
	}

	bool VisitCallExpr(CallExpr *E)
	{
		const FunctionDecl *FD = E->getDirectCallee();
		SourceLocation spelling;

		if (!FD) {
			if (!index.pointsToAnalysis())
				return true;

			pointsToStats.indirect_callsites++;
			const ValueDecl *callee = pointerDecl(E->getCallee());

			if (callee) {
				std::string usr = getUSR(callee, ctx);

				if (!usr.empty()) {
					pointsToStats.identified_callsites++;
					pointerIdentities.insert(std::move(usr));
				}
			}

			return true; /* indirect call: fp() */
		}

		spelling = index.spellingLoc(E->getExprLoc());
		addFunctionCall(FD, spelling);
		return true;
	}

	bool VisitCXXConstructExpr(CXXConstructExpr *E)
	{
		const FunctionDecl *FD = E->getConstructor();

		if (FD) {
			SourceLocation spelling = index.spellingLoc(E->getExprLoc());

			addFunctionCall(FD, spelling);
		}

		return true;
	}

private:
	static unsigned long long functionProperties(const FunctionDecl *D)
	{
		unsigned long long properties = 0;

		for (const FunctionDecl *declaration : D->redecls()) {
			if (declaration->hasAttr<ConstructorAttr>())
				properties |= SEMINDEX_PROPERTY_CONSTRUCTOR;

			if (declaration->hasAttr<DestructorAttr>())
				properties |= SEMINDEX_PROPERTY_DESTRUCTOR;

			if (isa<CXXConstructorDecl>(declaration))
				properties |= SEMINDEX_PROPERTY_CONSTRUCTOR;

			if (isa<CXXDestructorDecl>(declaration))
				properties |= SEMINDEX_PROPERTY_DESTRUCTOR;
		}

		return properties;
	}

	static bool isFunctionPointer(QualType type)
	{
		return !type.isNull() && type->isFunctionPointerType();
	}

	static const Expr *stripPointsToWrappers(const Expr *E)
	{
		for (;;) {
			E = E->IgnoreParens();

			if (const auto *cast = dyn_cast<CastExpr>(E)) {
				E = cast->getSubExpr();
				continue;
			}

			if (const auto *unary = dyn_cast<UnaryOperator>(E)) {
				if (unary->getOpcode() == UO_AddrOf || unary->getOpcode() == UO_Deref) {
					E = unary->getSubExpr();
					continue;
				}
			}

			return E;
		}
	}

	static const ValueDecl *pointerDecl(const Expr *E)
	{
		E = stripPointsToWrappers(E);

		if (const auto *reference = dyn_cast<DeclRefExpr>(E)) {
			const auto *decl = dyn_cast<ValueDecl>(reference->getDecl());

			return decl && isFunctionPointer(decl->getType()) ? decl : nullptr;
		}

		if (const auto *member = dyn_cast<MemberExpr>(E)) {
			const auto *decl = dyn_cast<ValueDecl>(member->getMemberDecl());

			return decl && isFunctionPointer(decl->getType()) ? decl : nullptr;
		}

		return nullptr;
	}

	void addDirectConstraint(const std::string &target, const FunctionDecl *function)
	{
		pointsToStats.direct_constraints++;
		const std::string &functionUsr = functionUSR(function);

		if (target.empty() || functionUsr.empty()) {
			pointsToStats.rejected_constraints++;

			return;
		}

		pointerIdentities.insert(target);
		functionIdentities.insert(functionUsr);
		directConstraints.emplace(target, functionUsr);
	}

	void addCopyConstraint(const std::string &target, const ValueDecl *source)
	{
		pointsToStats.copy_constraints++;
		std::string sourceUsr = getUSR(source, ctx);

		if (target.empty() || sourceUsr.empty()) {
			pointsToStats.rejected_constraints++;

			return;
		}

		pointerIdentities.insert(target);
		pointerIdentities.insert(sourceUsr);
		copyConstraints.emplace(target, std::move(sourceUsr));
	}

	bool collectPointsToConstraints(const std::string &target, const Expr *E)
	{
		E = stripPointsToWrappers(E);

		if (const auto *conditional = dyn_cast<ConditionalOperator>(E)) {
			bool left = collectPointsToConstraints(target, conditional->getTrueExpr());
			bool right = collectPointsToConstraints(target, conditional->getFalseExpr());

			return left || right;
		}

		if (E->isNullPointerConstant(ctx, Expr::NPC_ValueDependentIsNotNull))
			return true;

		if (const auto *reference = dyn_cast<DeclRefExpr>(E)) {
			if (const auto *function = dyn_cast<FunctionDecl>(reference->getDecl())) {
				addDirectConstraint(target, function);

				return true;
			}
		}

		if (const ValueDecl *source = pointerDecl(E)) {
			addCopyConstraint(target, source);

			return true;
		}

		return false;
	}

	void addPointsToConstraints(const ValueDecl *target, const Expr *value)
	{
		std::string targetUsr = getUSR(target, ctx);

		if (!collectPointsToConstraints(targetUsr, value))
			pointsToStats.unsupported_constraints++;
	}

	struct ValueInfo {
		semindex_symbol_kind_t kind;
		std::string name;
		std::string owner;
		std::string type;
		std::string canonical_type;
		std::string usr;
	};

	std::string detailedUSR(const Decl *D) const
	{
		return details ? getUSR(D, ctx) : "";
	}

	const CXXRecordDecl *primaryRecord(const CXXRecordDecl *R) const
	{
		if (!R || R->getTemplateSpecializationKind() == TSK_Undeclared)
			return R;

		const CXXRecordDecl *pat = R->getTemplateInstantiationPattern();

		if (pat)
			return pat;

		if (const auto *CT = R->getDescribedClassTemplate())
			return CT->getTemplatedDecl();

		return R;
	}

	const ValueDecl *primaryValueDecl(const ValueDecl *D) const
	{
		if (const auto *M = dyn_cast_or_null<CXXMethodDecl>(D)) {
			const CXXRecordDecl *R = M->getParent();

			if (R && R->getTemplateSpecializationKind() != TSK_Undeclared) {
				const CXXRecordDecl *primary = primaryRecord(R);

				if (primary != R) {
					for (const Decl *P : primary->decls()) {
						const CXXMethodDecl *PM = dyn_cast_or_null<CXXMethodDecl>(P);

						if (PM && PM->getName() == M->getName() &&
							PM->getNumParams() == M->getNumParams())
							return PM;
					}
				}
			}
		} else if (const auto *F = dyn_cast_or_null<FieldDecl>(D)) {
			const CXXRecordDecl *R = dyn_cast_or_null<CXXRecordDecl>(F->getParent());

			if (R && R->getTemplateSpecializationKind() != TSK_Undeclared) {
				const CXXRecordDecl *primary = primaryRecord(R);

				if (primary != R) {
					for (const Decl *P : primary->decls()) {
						const FieldDecl *PF = dyn_cast_or_null<FieldDecl>(P);

						if (PF && PF->getName() == F->getName())
							return PF;
					}
				}
			}
		}

		return D;
	}

	const ValueInfo &valueInfo(const ValueDecl *D)
	{
		D = primaryValueDecl(D);
		auto [entry, inserted] = valueInfoCache.try_emplace(D);

		if (inserted) {
			entry->second.kind = symbolKindForDecl(D);
			entry->second.name = getName(D);
			entry->second.owner = getOwnerName(D);

			if (details) {
				entry->second.type = stableTypeName(D->getType(), ctx);
				entry->second.canonical_type = canonicalTypeName(D->getType(), ctx);
				entry->second.usr = getUSR(D, ctx);
			}
		}
		return entry->second;
	}

	const std::string &functionUSR(const FunctionDecl *D)
	{
		D = dyn_cast_or_null<FunctionDecl>(primaryValueDecl(D))->getCanonicalDecl();
		auto [entry, inserted] = functionUSRCache.try_emplace(D);

		if (inserted)
			entry->second = getUSR(D, ctx);
		return entry->second;
	}

	void addFunctionCall(const FunctionDecl *D, SourceLocation loc)
	{
		if (!D || loc.isInvalid())
			return;

		SemindexUse u;

		u.kind = SEMINDEX_USE_CALL;
		u.mode = SEMINDEX_MODE_R_PTR;
		u.context = currentFunction;
		u.context_usr = currentFunctionUSR;
		u.context_usr_id = currentFunctionUSRId;
		u.loc = index.location(loc);
		u.local = false;

		/* A direct call to a lambda resolves to its closure (the lambda symbol),
		 * not to the operator() method as a plain function call. */
		const auto *M = dyn_cast_or_null<CXXMethodDecl>(D);

		if (M) {
			const CXXRecordDecl *closure = lambdaClosureFor(M);

			if (closure) {
				u.symbol_kind = SEMINDEX_SYMBOL_LAMBDA;
				u.name = "__lambda";
				u.owner = "";
				u.usr = getUSR(closure, ctx);
				u.usr_id = u.usr.empty() ? 0 : llvm::xxHash64(u.usr);

				index.addUseInScope(std::move(u), loc);
				return;
			}
		}

		const ValueInfo &info = valueInfo(D);

		u.symbol_kind = SEMINDEX_SYMBOL_FUNCTION;
		u.name = info.name;
		u.owner = info.owner;
		u.type = info.type;
		u.usr = functionUSR(D);
		u.usr_id = u.usr.empty() ? 0 : llvm::xxHash64(u.usr);

		index.addUseInScope(std::move(u), loc);
	}

	SourceLocation cleanupLocation(const VarDecl *D, const CleanupAttr *cleanup) const
	{
		const SourceManager &sm = ctx.getSourceManager();
		SourceLocation argument = cleanup->getArgLoc();

		if (argument.isInvalid())
			argument = cleanup->getLocation();

		SourceLocation spelling = sm.getSpellingLoc(argument);
		SourceLocation expansion = sm.getExpansionLoc(argument);
		SourceLocation begin = sm.getExpansionLoc(D->getBeginLoc());
		SourceLocation end = sm.getExpansionLoc(D->getEndLoc());
		bool validRange;

		validRange = spelling.isValid() && begin.isValid() && end.isValid();

		if (validRange && sm.isPointWithin(spelling, begin, end))
			return spelling;

		return expansion;
	}

	const ValueDecl *aliasTarget(const ValueDecl *alias, const AliasAttr *attribute) const
	{
		IdentifierInfo &identifier = ctx.Idents.get(attribute->getAliasee());
		auto declarations = ctx.getTranslationUnitDecl()->lookup(&identifier);

		for (const NamedDecl *declaration : declarations) {
			const auto *target = dyn_cast<ValueDecl>(declaration);
			bool functionAlias;
			bool variableAlias;
			bool sameKind;

			if (!target || target->getCanonicalDecl() == alias->getCanonicalDecl())
				continue;

			functionAlias = isa<FunctionDecl>(alias) && isa<FunctionDecl>(target);
			variableAlias = isa<VarDecl>(alias) && isa<VarDecl>(target);
			sameKind = functionAlias || variableAlias;

			if (sameKind)
				return target;
		}

		return nullptr;
	}

	SourceLocation aliasLocation(const AliasAttr *attribute) const
	{
		const SourceManager &sm = ctx.getSourceManager();
		SourceLocation location = attribute->getLocation();

		if (location.isMacroID())
			return sm.getExpansionLoc(location);

		CharSourceRange range = CharSourceRange::getTokenRange(attribute->getRange());
		StringRef text = Lexer::getSourceText(range, sm, ctx.getLangOpts());
		size_t offset = text.find(attribute->getAliasee());

		if (offset != StringRef::npos)
			return range.getBegin().getLocWithOffset(offset);

		return location;
	}

	void addAliasUse(const ValueDecl *alias)
	{
		const auto *attribute = alias->getAttr<AliasAttr>();

		if (!attribute)
			return;

		const ValueDecl *target = aliasTarget(alias, attribute);

		if (!target)
			return;

		unsigned mode = SEMINDEX_MODE_R_AOF | SEMINDEX_MODE_W_AOF;

		addValueUse(target, SEMINDEX_USE_ADDR, mode, "", aliasLocation(attribute), false);
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
		bool local;

		if (!D || loc.isInvalid())
			return;

		local = D->getDeclContext()->isFunctionOrMethod();

		if (!index.includeLocal() && local)
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
		u.name = typeIdentityName(D);
		u.owner = "";
		u.type = type;
		u.usr = detailedUSR(D);
		u.context = currentFunction;
		u.loc = index.location(spelling);
		u.local = local;

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

	const RecordDecl *initializedRecordFor(const DesignatedInitExpr *E) const
	{
		DynTypedNode node = DynTypedNode::create(*E);

		for (;;) {
			auto parents = ctx.getParents(node);

			if (parents.empty())
				return nullptr;

			node = parents[0];

			if (const auto *D = node.get<VarDecl>())
				return recordDeclForType(D->getType());
		}
	}

	static bool indirectChainIsSuffix(const IndirectFieldDecl *inner, const IndirectFieldDecl *outer)
	{
		auto innerChain = inner->chain();
		auto outerChain = outer->chain();

		if (innerChain.size() > outerChain.size())
			return false;

		size_t offset = outerChain.size() - innerChain.size();

		for (size_t i = 0; i < innerChain.size(); i++) {
			if (innerChain[i] != outerChain[offset + i])
				return false;
		}

		return true;
	}

	bool promotedFieldIsVisible(const IndirectFieldDecl *field, const RecordDecl *initializedRecord)
	{
		const auto *owner = dyn_cast<RecordDecl>(field->getDeclContext());

		if (owner == initializedRecord)
			return true;

		auto [current, end] = indirectFields.equal_range(field->getAnonField());

		for (; current != end; ++current) {
			const IndirectFieldDecl *outer = current->second;

			if (outer->getDeclContext() == initializedRecord && indirectChainIsSuffix(field, outer))
				return true;
		}

		return false;
	}

	void addPromotedFieldUses(const FieldDecl *field, const RecordDecl *initializedRecord, SourceLocation loc)
	{
		auto [current, end] = indirectFields.equal_range(field);

		for (; current != end; ++current) {
			const IndirectFieldDecl *indirect = current->second;

			if (!promotedFieldIsVisible(indirect, initializedRecord))
				continue;

			addValueUse(indirect, SEMINDEX_USE_WRITE, SEMINDEX_MODE_W_VAL, currentFunction, loc, false);
		}
	}

	const RecordDecl *memberBaseRecord(const MemberExpr *E) const
	{
		const Expr *base = E->getBase()->IgnoreParenImpCasts();

		while (const auto *member = dyn_cast<MemberExpr>(base)) {
			base = member->getBase()->IgnoreParenImpCasts();
		}

		QualType type = base->getType();

		if (type->isPointerType())
			type = type->getPointeeType();

		return recordDeclForType(type);
	}

	const IndirectFieldDecl *promotedFieldFor(const FieldDecl *field, const RecordDecl *owner)
	{
		auto [current, end] = indirectFields.equal_range(field);

		for (; current != end; ++current) {
			const IndirectFieldDecl *indirect = current->second;

			if (indirect->getDeclContext() == owner)
				return indirect;
		}

		return nullptr;
	}

	void addIndirectField(IndirectFieldDecl *D)
	{
		const ValueInfo &info = valueInfo(D);
		SemindexSymbol s;

		indirectFields.emplace(D->getAnonField(), D);

		s.kind = info.kind;
		s.name = info.name;
		s.owner = info.owner;
		s.type = info.type;
		s.canonical_type = info.canonical_type;
		s.usr = info.usr;
		s.context = "";
		s.loc = index.location(D->getLocation());
		s.local = false;
		s.definition = true;

		index.addSymbolInScope(std::move(s), D->getLocation());
	}

	void addAnonymousRecordSymbols(const RecordDecl *D, const std::string &name)
	{
		SemindexSymbol s;

		if (isLambdaClosure(D)) {
			s.kind = SEMINDEX_SYMBOL_LAMBDA;
			s.name = "__lambda";
			s.local = !currentFunction.empty();
		} else {
			s.kind = recordKindForDecl(D);
			s.name = name;
			s.local = false;
		}

		s.owner = "";
		s.type = "";
		s.usr = getUSR(D, ctx);
		s.context = "";
		s.loc = index.displayLocation(ctx,
			D->getBraceRange().getBegin().isValid() ? D->getBraceRange().getBegin() : D->getLocation());
		s.definition = true;

		index.addSymbolInScope(std::move(s), D->getLocation());

		if (!isLambdaClosure(D))
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

			s.type = stableTypeName(field->getType(), ctx);
			s.canonical_type = canonicalTypeName(field->getType(), ctx);
			setDeclaredTypeIdentity(s, field->getTypeSourceInfo(), ctx);
			s.usr = getUSR(field, ctx);
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
	std::unordered_multimap<const FieldDecl *, const IndirectFieldDecl *> indirectFields;
	std::unordered_map<const ValueDecl *, ValueInfo> valueInfoCache;
	std::unordered_map<const FunctionDecl *, std::string> functionUSRCache;
	semindex_points_to_stats_t pointsToStats{};
	std::set<std::pair<std::string, std::string>> directConstraints;
	std::set<std::pair<std::string, std::string>> copyConstraints;
	std::set<const InitListExpr *> pointsToInitLists;
	std::set<std::string> pointerIdentities;
	std::set<std::string> functionIdentities;
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
		visitor.finishPointsToStats();
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

		CI.getPreprocessor().addPPCallbacks(
			createSemindexPPCallbacks(index, CI.getPreprocessor(), CI.getDiagnostics()));

		return std::make_unique<SemindexASTConsumer>(out, CI.getASTContext(), index);
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
