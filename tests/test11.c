// SPDX-License-Identifier: GPL-2.0-or-later
struct Inner {
	int x;
};

struct Outer {
	struct Inner inner;
	int y;
	int z[2];
};

struct Outer o = {
	.inner.x = 1,
	.y = 2,
	.z[1] = 3,
};
