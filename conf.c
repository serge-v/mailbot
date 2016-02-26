#include "conf.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <err.h>
#include <getopt.h>
#include "usage.txt.h"
#include "synopsis.txt.h"

struct config cfg;

enum section
{
	SECTION_NONE,
	SECTION_IMAP
};

int
config_init(int argc, char **argv)
{
	FILE *f;
	char *ptr, s[200], key[100], value[100];
	enum section curr_section = SECTION_NONE;
	int ch, n, line = 0;
	bool show_config = false;

	while ((ch = getopt(argc, argv, "dhvgc:s")) != -1) {
		switch (ch) {
		case 'c':
			cfg.config_fname = strdup(optarg);
			break;
		case 'd':
			cfg.debug = 1;
			break;
		case 'g':
			show_config = true;
			break;
		case 'v':
			printf("version: %s\n", app_version);
			printf("date:    %s\n", app_date);
			exit(0);
		case 'h':
			puts(usage_txt);
			return 1;
		default:
			puts(synopsis_txt);
			exit(1);
		}
	}

	if (cfg.config_fname == NULL)
		errx(1, "no config file specified");

	f = fopen(cfg.config_fname, "rt");
	if (f == NULL)
		err(1, "cannot open %s", cfg.config_fname);

	while (!feof(f)) {
		ptr = fgets(s, 200, f);
		if (ptr == NULL)
			break;
		
		line++;
		s[strlen(s) - 1] = 0; /* trim EOL */

		if (s[0] == 0 || s[0] == '#')
			continue;

		if (strcmp("[imap]", s) == 0) {
			curr_section = SECTION_IMAP;
			continue;
		}

		if (strstr(s, "=") == NULL) {
			errx(1,
				"%s:%d: error: invalid format: %s. Should be: key = value"
				" (spaces are required before and after equal sign).",
				cfg.config_fname, line, s);
		}

		if (strstr(s, " = ") == NULL)
			errx(1, "%s:%d: error: spaces are required before and after equal sign: %s", cfg.config_fname, line, s);

		n = sscanf(s, "%s = %s\n", key, value);
		if (n == -1)
			continue;

		if (n != 2) {
			errx(1,
				"%s:%d: error: invalid format: %s. Should be: key = value"
				"(spaces are required before and after equal sign).",
				cfg.config_fname, line, s);
		}

		if (curr_section == SECTION_IMAP) {
			if (strcmp("host", key) == 0) {
				cfg.imap.host = strdup(value);
			} else if (strcmp("folder", key) == 0) {
				cfg.imap.folder = strdup(value);
			} else if (strcmp("user", key) == 0) {
				cfg.imap.user = strdup(value);
			} else if (strcmp("password", key) == 0) {
				cfg.imap.password = strdup(value);
			} else {
				errx(1, "%s:%d: error: unknown parameter: %s", cfg.config_fname, line, s);
			}
		} else {
			errx(1, "%s:%d: error: unknown parameter: %s", cfg.config_fname, line, s);
		}
	}

	fclose(f);

	if (show_config) {
		config_dump();
		exit(1);
	}

	return 0;
}

void
config_dump()
{
	printf(
		"[imap]\n"
		"host = %s\n"
		"folder = %s\n"
		"user = %s\n"
		"password = %s\n",
		cfg.imap.host,
		cfg.imap.folder,
		cfg.imap.user,
		cfg.imap.password,
		);
}
















