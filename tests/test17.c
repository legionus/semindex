// SPDX-License-Identifier: GPL-2.0-or-later
struct OuterArray {
	struct InnerArray {
		int values[2];
	} items[2][2];
};

struct OuterArray arrays = {
	.items[0][1].values[1] = 7,
};
