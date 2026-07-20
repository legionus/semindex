// SPDX-License-Identifier: GPL-2.0-or-later

static void helper(void)
{
}

static void caller(void)
{
	helper();
}

void entry_b(void)
{
	caller();
}
