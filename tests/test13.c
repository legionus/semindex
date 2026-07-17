// SPDX-License-Identifier: GPL-2.0-or-later
#define FOO 1
#define BAZ 1

#ifdef FOO
int a;
#endif

#ifndef BAR
int b;
#endif

#if defined(BAZ)
int c;
#endif

#if defined(__GNUC__)
int d;
#endif
