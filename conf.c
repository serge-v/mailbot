#include "conf.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <err.h>
#include <getopt.h>
#include "usage.txt.h"
#include "synopsis.txt.h"
#include "version.h"

struct config cfg;

static void
add_filter(char* f)
{
	char *s = strtok(f, ",");
	int days = atoi(s);
	if (days <= 0)
		err(1, "invalid filter: %s", f);

	s = strtok(NULL, ",");

	struct filter *fn = calloc(1, sizeof(struct filter));

	fn->days_before = days;
	fn->gmail_filter = strdup(s);
	fn->next = cfg.purge_filters;
	cfg.purge_filters = fn;
}

enum section
{
	SECTION_NONE,
	SECTION_IMAP,
	SECTION_PURGE_FILTERS
};

int
config_init(int argc, char **argv)
{
	FILE *f;
	char *ptr, s[200], *key, *value;
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

	if (cfg.config_fname == NULL) {
		if (show_config)
			config_dump();
		errx(1, "no config file specified");
	}

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
		} else if (strcmp("[purge_filters]", s) == 0) {
			curr_section = SECTION_PURGE_FILTERS;
			continue;
		}

		if (strstr(s, "=") == NULL) {
			errx(1,
				"%s:%d: error: invalid format: %s. Should be: key = value"
				" (spaces are required before and after equal sign).",
				cfg.config_fname, line, s);
		}

		ptr = strstr(s, " = ");

		if (ptr == NULL)
			errx(1, "%s:%d: error: spaces are required before and after equal sign: %s", cfg.config_fname, line, s);

		key = s;
		*ptr = 0;
		ptr += 3;
		value = ptr;

		if (curr_section == SECTION_IMAP) {
			if (strcmp("url", key) == 0) {
				cfg.imap.url = strdup(value);
			} else if (strcmp("login", key) == 0) {
				cfg.imap.login = strdup(value);
			} else if (strcmp("password", key) == 0) {
				cfg.imap.password = strdup(value);
			} else {
				errx(1, "%s:%d: error: unknown parameter: %s", cfg.config_fname, line, s);
			}
		} else if (curr_section == SECTION_PURGE_FILTERS) {
			if (strcmp("filter", key) == 0) {
				add_filter(value);
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
		"url = %s\n"
		"login = %s\n"
		"password = %s\n",
		cfg.imap.url,
		cfg.imap.login,
		cfg.imap.password
		);

	printf("\n[purge_filters]\n");

	for (struct filter *f = cfg.purge_filters; f != NULL; f = f->next) {
		printf("filter = %d,%s\n", f->days_before, f->gmail_filter);
	}
}

void
config_free()
{
}
















