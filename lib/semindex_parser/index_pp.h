// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "semindex_internal.h"

#include <memory>

namespace clang
{
class DiagnosticsEngine;
class PPCallbacks;
class Preprocessor;
} // namespace clang

std::unique_ptr<clang::PPCallbacks> createSemindexPPCallbacks(SemindexContext index, clang::Preprocessor &preprocessor,
	clang::DiagnosticsEngine &diagnostics);
