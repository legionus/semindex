// SPDX-License-Identifier: GPL-2.0-or-later
int before_delimiter;

void broken_delimiter(void)
{
	int value = (1 + 2;
}

int after_delimiter;
