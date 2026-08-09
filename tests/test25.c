// SPDX-License-Identifier: GPL-2.0-or-later
static void startup(void) __attribute__((constructor(101)));

static void startup(void)
{
}

static void shutdown(void);
static void shutdown(void) __attribute__((destructor));

static void shutdown(void)
{
}
