// SPDX-License-Identifier: GPL-2.0-or-later
#include "lsp_text_position.h"

#include <limits>

bool lspParseTextPosition(const llvm::json::Object *params, LspTextPosition &result)
{
	if (!params)
		return false;

	const llvm::json::Object *document = params->getObject("textDocument");
	const llvm::json::Object *position = params->getObject("position");

	if (!document || !position)
		return false;

	auto uri = document->getString("uri");
	auto line = position->getInteger("line");
	auto character = position->getInteger("character");

	if (!uri || !line || !character || *line < 0 || *character < 0 ||
		*line >= std::numeric_limits<unsigned>::max() ||
		*character > std::numeric_limits<unsigned>::max())
		return false;

	result.uri = *uri;
	result.line = *line;
	result.character = *character;

	return true;
}
