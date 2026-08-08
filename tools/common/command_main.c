// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef SEMINDEX_COMMAND
#error "SEMINDEX_COMMAND must name the command entry point"
#endif

int SEMINDEX_COMMAND(int argc, char **argv);

int main(int argc, char **argv)
{
	return SEMINDEX_COMMAND(argc, argv);
}
