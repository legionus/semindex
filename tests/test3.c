// SPDX-License-Identifier: GPL-2.0-or-later
int g;

int f(void)
{
	int x = 0;
	int *p;

	p = &g;
	x = *p;
	return x;
}
