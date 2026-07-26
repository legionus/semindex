/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef SEMINDEX_EXPORT_H
#define SEMINDEX_EXPORT_H

#if defined(__GNUC__) || defined(__clang__)
#define SEMINDEX_API __attribute__((visibility("default")))
#else
#define SEMINDEX_API
#endif

#endif /* SEMINDEX_EXPORT_H */
