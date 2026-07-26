// SPDX-License-Identifier: GPL-2.0-or-later
struct Container {
	union {
		int value;
	};
};

int read_value(struct Container *p)
{
	return p->value;
}

void write_value(struct Container *p)
{
	p->value = 1;
}
