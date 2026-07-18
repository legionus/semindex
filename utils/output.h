// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef SEMINDEX_OUTPUT_H
#define SEMINDEX_OUTPUT_H

#include <stdio.h>

#include "semindex.h"

enum output_format {
	FORMAT_DEFAULT,
	FORMAT_DISSECT,
};

int output_default(FILE *out, semindex_t *s);
int output_dissect(FILE *out, semindex_t *s);

#endif /* SEMINDEX_OUTPUT_H */
