// SPDX-License-Identifier: GPL-2.0-or-later

struct B {
	int v;
};
struct D1 : public B {
};
struct D2 : protected B {
};
struct D3 : private B {
};
struct X {
	int w;
};
