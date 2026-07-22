// SPDX-License-Identifier: GPL-2.0-or-later
int target(int);

#define CALL_TARGET(value) target(value)

int invoke(int value)
{
	return CALL_TARGET(value);
}
