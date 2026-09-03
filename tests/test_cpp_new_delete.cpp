// SPDX-License-Identifier: GPL-2.0-or-later

#include <new>

class T
{
public:
	int v;

	T();
	T(int x);
};

char bufs[16];

int main()
{
	T *p = new T(3);
	T *np = new T[2];
	T *pp = new (bufs) T(1);
	T *up = new T;
	delete p;
	delete[] np;

	int *q = new int(4);
	delete q;

	return 0;
}
