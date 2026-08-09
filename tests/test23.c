// SPDX-License-Identifier: GPL-2.0-or-later
int target_function(void)
{
	return 1;
}

int function_alias(void) __attribute__((alias("target_function")));

int target_variable = 1;
extern int variable_alias __attribute__((alias("target_variable")));

#define _alias_(target) typeof(target) __attribute__((alias(#target)))
extern _alias_(target_function) macro_alias;
