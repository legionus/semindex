// SPDX-License-Identifier: GPL-2.0-or-later

typedef void (*callback_t)(void);

struct operations {
	callback_t run;
};

static void target_a(void)
{
}

static void target_b(void)
{
}

callback_t global_callback = target_a;
struct operations global_operations = {
	.run = target_b,
};

void invoke_callbacks(int select)
{
	callback_t local = select ? target_a : target_b;
	callback_t copied = local;
	callback_t copied_global = global_callback;

	copied();
	copied_global();
	global_operations.run();
}
