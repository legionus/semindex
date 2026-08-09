// SPDX-License-Identifier: GPL-2.0-or-later
#include "semantic_identity.h"

#include <tuple>

semindex_db_identity_t SemindexIdentity::view() const
{
	return semindex_db_identity_t{
		.variant = variant.c_str(),
		.symbol = symbol.c_str(),
		.usr_id = usr_id,
		.kind = kind,
	};
}

bool SemindexIdentity::operator<(const SemindexIdentity &other) const
{
	return std::tie(variant, symbol, kind, usr_id) <
		std::tie(other.variant, other.symbol, other.kind, other.usr_id);
}
