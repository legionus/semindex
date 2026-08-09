// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "semindex_database.h"

#include <cstdint>
#include <string>

struct SemindexIdentity {
	std::string variant;
	std::string symbol;
	semindex_symbol_kind_t kind;
	uint64_t usr_id;

	semindex_db_identity_t view() const;
	bool operator<(const SemindexIdentity &other) const;
};
