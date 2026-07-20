// SPDX-License-Identifier: GPL-2.0-or-later

static void helper(void)
{
}

void leaf(void)
{
}

#define INVOKE(fn) fn()

static void caller(void)
{
	helper();
	leaf();
	INVOKE(leaf);
	caller();
}

void entry_a(void)
{
	caller();
}

void indirect_a(void (*fn)(void))
{
	fn();
}
