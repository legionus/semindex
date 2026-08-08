// SPDX-License-Identifier: GPL-2.0-or-later
#include "index_asm.h"
#include "semindex_internal.h"

#include <clang/Lex/Lexer.h>
#include <clang/Lex/Preprocessor.h>

#include <algorithm>
#include <limits>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

using namespace clang;

namespace
{

class SemindexAsmIndexer
{
public:
	SemindexAsmIndexer(SemindexContext index, Preprocessor &preprocessor) : index(index), preprocessor(preprocessor)
	{
	}

	void observe(const Token &token)
	{
		observeCall(token);

		if (type_state != TypeState::None)
			observeType(token);
		else if (token.is(tok::period))
			type_state = TypeState::Dot;

		if (token.is(tok::colon) && !previous_identifier.empty())
			addLabel(previous_identifier, previous_location, previous_order);

		if (isIdentifier(token)) {
			previous_identifier = tokenText(token);
			previous_location = token.getLocation();
			previous_order = token_order;
		} else {
			previous_identifier.clear();
			previous_location = {};
		}

		token_order++;
	}

	void finish()
	{
		std::sort(functions.begin(), functions.end(),
			[](const Function &left, const Function &right) { return left.begin < right.begin; });

		for (size_t i = 0; i < functions.size(); i++) {
			if (functions[i].end < functions[i].begin)
				functions[i].end = i + 1 < functions.size() ? functions[i + 1].begin
									    : std::numeric_limits<size_t>::max();
		}

		for (const Call &call : calls) {
			const Function *function = containingFunction(call);

			if (function)
				addCall(call, *function);
		}
	}

private:
	struct Marker {
		SourceLocation location;
		size_t order;
	};

	struct Function {
		std::string name;
		size_t begin;
		size_t end;
	};

	struct Call {
		std::string target;
		SourceLocation location;
		size_t order;
	};

	enum class TypeState {
		None,
		Dot,
		Directive,
		EntName,
		EndName,
		Name,
		Comma,
		Marker,
	};

	enum class CallState {
		None,
		Direct,
		JalFirst,
		JalAfterFirst,
		SkipToComma,
	};

	bool isIdentifier(const Token &token) const
	{
		return token.is(tok::identifier) || token.is(tok::raw_identifier);
	}

	std::string tokenText(const Token &token) const
	{
		return Lexer::getSpelling(token, preprocessor.getSourceManager(), preprocessor.getLangOpts());
	}

	bool isFunctionMarker(const Token &token) const
	{
		std::string text = tokenText(token);

		return text == "function" || text == "@function" || text == "%function" || text == "#function" ||
			text == "STT_FUNC";
	}

	bool isMarkerPrefix(const Token &token) const
	{
		std::string text = tokenText(token);

		return text == "@" || text == "%" || text == "#";
	}

	bool isDirectCallMnemonic(const std::string &text) const
	{
		return text == "call" || text == "callq" || text == "bl" || text == "jbsr" || text == "bsr";
	}

	void observeCall(const Token &token)
	{
		if (call_state == CallState::Direct) {
			if (isIdentifier(token))
				addPendingCall(tokenText(token), token.getLocation(), token_order);

			call_state = CallState::None;
			return;
		}

		if (call_state == CallState::SkipToComma) {
			if (token.is(tok::comma))
				call_state = CallState::Direct;

			return;
		}

		if (call_state == CallState::JalFirst) {
			if (isIdentifier(token)) {
				jal_target = tokenText(token);
				jal_location = token.getLocation();
				jal_order = token_order;
				call_state = CallState::JalAfterFirst;
			} else {
				call_state = CallState::None;
			}

			return;
		}

		if (call_state == CallState::JalAfterFirst) {
			if (token.is(tok::comma)) {
				call_state = CallState::Direct;
			} else {
				addPendingCall(jal_target, jal_location, jal_order);
				call_state = CallState::None;
			}

			return;
		}

		if (!isIdentifier(token))
			return;

		std::string text = tokenText(token);

		if (isDirectCallMnemonic(text))
			call_state = CallState::Direct;
		else if (text == "jal")
			call_state = CallState::JalFirst;
		else if (text == "brasl")
			call_state = CallState::SkipToComma;
	}

	void addPendingCall(std::string target, SourceLocation location, size_t order)
	{
		calls.push_back({ std::move(target), location, order });
	}

	void observeType(const Token &token)
	{
		switch (type_state) {
		case TypeState::None:
			return;

		case TypeState::Dot:
			if (!isIdentifier(token)) {
				resetType(token);

				return;
			}

			if (tokenText(token) == "type")
				type_state = TypeState::Directive;
			else if (tokenText(token) == "ent")
				type_state = TypeState::EntName;
			else if (tokenText(token) == "end")
				type_state = TypeState::EndName;
			else
				resetType(token);

			return;

		case TypeState::EntName:
			if (isIdentifier(token))
				addFunctionType(tokenText(token), token.getLocation(), token_order);

			type_state = TypeState::None;
			return;

		case TypeState::EndName:
			if (isIdentifier(token))
				endFunction(tokenText(token), token_order);

			type_state = TypeState::None;
			return;

		case TypeState::Directive:
			if (isIdentifier(token)) {
				type_name = tokenText(token);
				type_location = token.getLocation();
				type_state = TypeState::Name;
			} else {
				resetType(token);
			}

			return;

		case TypeState::Name:
			if (token.is(tok::comma))
				type_state = TypeState::Comma;
			else
				resetType(token);

			return;

		case TypeState::Comma:
			if (isFunctionMarker(token)) {
				addFunctionType(type_name, type_location, token_order);
				type_state = TypeState::None;
			} else if (isMarkerPrefix(token)) {
				type_state = TypeState::Marker;
			} else {
				resetType(token);
			}

			return;

		case TypeState::Marker:
			if (isFunctionMarker(token) && tokenText(token) == "function")
				addFunctionType(type_name, type_location, token_order);

			type_state = TypeState::None;
			return;
		}
	}

	void resetType(const Token &token)
	{
		type_state = token.is(tok::period) ? TypeState::Dot : TypeState::None;
		type_name.clear();
		type_location = {};
	}

	void addFunctionType(const std::string &name, SourceLocation location, size_t order)
	{
		function_types.emplace(name, Marker{ location, order });
		tryAddFunction(name);
	}

	void addLabel(const std::string &name, SourceLocation location, size_t order)
	{
		labels.emplace(name, Marker{ location, order });
		tryAddFunction(name);
	}

	void endFunction(const std::string &name, size_t order)
	{
		for (Function &function : functions) {
			if (function.name == name)
				function.end = order;
		}
	}

	void tryAddFunction(const std::string &name)
	{
		auto type = function_types.find(name);
		auto label = labels.find(name);

		if (type == function_types.end() || label == labels.end() || !emitted.insert(name).second)
			return;

		SourceLocation location = index.spellingLoc(label->second.location);
		SemindexSymbol symbol;

		symbol.kind = SEMINDEX_SYMBOL_FUNCTION;
		symbol.name = name;
		symbol.owner = "";
		symbol.type = "";
		symbol.usr = "";
		symbol.context = "";
		symbol.loc = index.location(location);
		symbol.local = false;
		symbol.definition = true;

		index.addSymbolInScope(std::move(symbol), location);
		functions.push_back({ name, label->second.order,
			type->second.order > label->second.order ? type->second.order : 0 });
	}

	const Function *containingFunction(const Call &call) const
	{
		const Function *result = nullptr;

		for (const Function &function : functions) {
			if (function.begin > call.order)
				break;

			if (call.order <= function.end)
				result = &function;
		}

		return result;
	}

	void addCall(const Call &call, const Function &function)
	{
		SourceLocation location = index.spellingLoc(call.location);
		SemindexUse use;

		use.kind = SEMINDEX_USE_CALL;
		use.symbol_kind = SEMINDEX_SYMBOL_FUNCTION;
		use.mode = SEMINDEX_MODE_R_PTR;
		use.name = call.target;
		use.owner = "";
		use.type = "";
		use.usr = "";
		use.context = function.name;
		use.context_usr = "";
		use.loc = index.location(location);
		use.local = false;

		index.addUseInScope(std::move(use), location);
	}

	SemindexContext index;
	Preprocessor &preprocessor;
	TypeState type_state = TypeState::None;
	std::string type_name;
	SourceLocation type_location;
	std::string previous_identifier;
	SourceLocation previous_location;
	size_t previous_order = 0;
	size_t token_order = 0;
	CallState call_state = CallState::None;
	std::string jal_target;
	SourceLocation jal_location;
	size_t jal_order = 0;
	std::unordered_map<std::string, Marker> function_types;
	std::unordered_map<std::string, Marker> labels;
	std::set<std::string> emitted;
	std::vector<Function> functions;
	std::vector<Call> calls;
};

} // namespace

std::function<void()> installSemindexAsmIndexer(SemindexContext index, Preprocessor &preprocessor)
{
	auto asm_indexer = std::make_shared<SemindexAsmIndexer>(index, preprocessor);

	preprocessor.setTokenWatcher([asm_indexer](const Token &token) { asm_indexer->observe(token); });

	return [asm_indexer]() { asm_indexer->finish(); };
}
