// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <functional>

class SemindexContext;

namespace clang
{
class Preprocessor;
}

std::function<void()> installSemindexAsmIndexer(SemindexContext index, clang::Preprocessor &preprocessor);
