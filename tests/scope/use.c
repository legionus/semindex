// SPDX-License-Identifier: GPL-2.0-or-later
#include "api.h"

int scope_f(struct ScopeS *p)
{
	p->x = 1;
	return p->x;
}
