// SPDX-License-Identifier: GPL-2.0-or-later
struct Result {
	int value;
};

struct Result *direct(const int *);
struct Result *(*callback)(const int *input);

typedef void callback_t(int argument);

void accept(callback_t handler)
{
	handler(0);
}

void invoke(int input)
{
	direct(&input)->value = 1;
	(*direct)(&input)->value = 2;
	(&direct)(&input)->value = 3;
	callback(&input)->value = 4;
	(*callback)(&input)->value = 5;
}
