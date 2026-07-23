// SPDX-License-Identifier: GPL-2.0-or-later
int before_local_syntax;

void broken_local_syntax(void)
{
	int value = ;
}

int after_local_syntax;
