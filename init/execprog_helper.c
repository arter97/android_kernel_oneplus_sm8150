// SPDX-License-Identifier: GPL-2.0
/*
 *  Copyright (c) 2019 Park Ju Hyung(arter97)
 *
 *  execprog_helper: Converts a binary to a header file that execprog.c accepts.
 *
 *  Built-in kernel firmware comes with significant drawbacks:
 *  - It's no longer supported
 *  - The memory isn't freed after loading
 *
 *  execprog_helper generates execprog.h with __init keyword.
 *  This allows the kernel to free its memory later.
 *
 *  execprog.c copies the static data to a dynamically allocated memory region
 *  and frees it later upon termination.
 *
 *  execprog.h contains a 2-level linked-list with char arrays each with 4096 items,
 *  which limits the containable data to 16M. Your binary must be smaller than 16M.
 *
 *  Usage: ./execprog_helper /path/to/program
 */

#include <stdio.h>
#include <stdbool.h>
#include <fcntl.h>
#include <unistd.h>

int main(int argc, char** argv)
{
	FILE *in, *out;
	int sub, i;
	unsigned char buf[4096];
	ssize_t nread;
	int last_item = 4096;
	bool now_null = false;

	in = fopen(argv[1], "r");
	if (!in) {
		perror("Failed to open input file");
		return 1;
	}

	out = fopen("execprog.h", "w");
	if (!out) {
		perror("Failed to create execprog.h");
		return 1;
	}

	fprintf(out, "// Created from %s\n\n", argv[1]);

	for (sub = 0; sub < 4096; sub++) {
		nread = fread(buf, 1, sizeof(buf), in);
		if (!nread)
			break;

		fprintf(out, "static const unsigned char sub%d[] __initconst = {\n    ", sub);
		for (i = 0; i < nread; i++) {
			fprintf(out, "%d, ", buf[i]);
		}
		fprintf(out, "\n};\n\n");

		if (nread != 4096)
			last_item = (int)nread;
	}

	fclose(in);

	fprintf(out, "static const unsigned char* const primary[] __initconst = {\n    ");
	for (i = 0; i < sub; i++) {
		fprintf(out, "sub%d, ", i);
	}
	fprintf(out, "\n};\n\n");

	fprintf(out, "static const int last_index __initconst = %d;\n", sub);
	fprintf(out, "static const int last_items __initconst = %d;\n", last_item);

	fclose(out);

	return 0;
}
