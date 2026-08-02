// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef SEMINDEX_COMPILER_COMMAND_H
#define SEMINDEX_COMPILER_COMMAND_H

int compiler_command_driver_is_omitted(const char *arg);
int compiler_command_find_source(int argc, char **argv, const char **source_file);

#endif
