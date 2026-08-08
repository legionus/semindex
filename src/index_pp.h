// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "semindex_internal.h"

#include <memory>

namespace clang
{
class DiagnosticsEngine;
class PPCallbacks;
} // namespace clang

std::unique_ptr<clang::PPCallbacks> createSemindexPPCallbacks(SemindexContext index,
	clang::DiagnosticsEngine &diagnostics);
