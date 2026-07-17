// SPDX-License-Identifier: GPL-2.0-or-later
struct S {
	int x;
};

typedef int T;
int g;

int f(int x)
{
	int a = sizeof(x);
	int b = sizeof(struct S);
	int c = sizeof(T);
	int d = _Alignof(T);
	int e = _Generic(x, int: g, default: 0);
	__typeof__(x) y = g;

	return a + b + c + d + e + y;
}
