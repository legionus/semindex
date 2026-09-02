// SPDX-License-Identifier: GPL-2.0-or-later

struct S {
	int v;
};
struct D : S {
};

void f()
{
	D d;
	static_cast<S>(d);
	S *p = (S *)&d;
	S *q = dynamic_cast<S *>(&d);
	int x = 42;
	long y = x;
	int w = (int)x;
}
