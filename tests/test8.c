// SPDX-License-Identifier: GPL-2.0-or-later
typedef int counter_t;

struct Pair {
	int x;
};
typedef struct Pair pair_t;

typedef struct {
	int y;
} anon_t;

counter_t c;
pair_t p;
anon_t a;
