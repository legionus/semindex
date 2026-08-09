// SPDX-License-Identifier: GPL-2.0-or-later
void weak_target(void)
{
}

static void function_ref(void) __attribute__((weakref("weak_target")));

int weak_value;
static int variable_ref __attribute__((weakref("weak_value")));

#define _weakref_(target) __attribute__((__weakref__(#target)))
static void macro_ref(void) _weakref_(weak_target);

static void split_ref(void) __attribute__((weakref, alias("weak_target")));
