// SPDX-License-Identifier: GPL-2.0-or-later
int foo(void)
{
	return 1;
}

int call(int (*fn)(void))
{
	return fn();
}
