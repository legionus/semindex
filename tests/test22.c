// SPDX-License-Identifier: GPL-2.0-or-later
static void release_int(int *value)
{
	(void)value;
}

#define _cleanup_(function) __attribute__((cleanup(function)))
#define _cleanup_release_ _cleanup_(release_int)

void direct_cleanup(void)
{
	int value __attribute__((cleanup(release_int))) = 0;

	(void)value;
}

void named_cleanup(void)
{
	_cleanup_(release_int) int value = 0;

	(void)value;
}

void wrapped_cleanup(void)
{
	_cleanup_release_ int value = 0;

	(void)value;
}
