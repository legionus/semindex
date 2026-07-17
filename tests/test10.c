// SPDX-License-Identifier: GPL-2.0-or-later
int declared(int);
int defined(int x);

int defined(int x)
{
	return x;
}

int call(void)
{
	return defined(1);
}
