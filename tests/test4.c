// SPDX-License-Identifier: GPL-2.0-or-later
struct P {
	int x;
	int y;
};

int f(struct P *p)
{
	return p->x + p->y;
}
