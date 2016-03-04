#include "conf.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <err.h>
#include <getopt.h>
#include <limits.h>
#include <sys/stat.h>
#include "usage.txt.h"
#include "synopsis.txt.h"
#include "version.h"
#include "common/fs.h"

struct config cfg;

static struct filter *
add_filter(struct filter *list, char* f)
{
	char *s = strtok(f, ",");
	int days = atoi(s);
	if (days <= 0)
		err(1, "invalid filter: %s", f);

	s = strtok(NULL, ",");

	struct filter *fn = calloc(1, sizeof(struct filter));

	fn->days_before = days;
	fn->gmail_filter = strdup(s);
	fn->next = list;
	list = fn;

	return list;
}

static struct filter *
reverse_filter(struct filter *list)
{
	struct filter *prev = NULL;
	struct filter *curr = list;

	while (curr != NULL) {
		struct filter *next = curr->next;
		curr->next = prev;
		prev = curr;
		curr = next;
	}

	return prev;
}

enum section
{
	SECTION_NONE,
	SECTION_IMAP,
	SECTION_PURGE_FILTERS,
	SECTION_SUMMARIZE_FILTERS
};

static enum section
parse_section_name(const char *s)
{
	if (strcmp("[imap]", s) == 0)
		return SECTION_IMAP;
	else if (strcmp("[purge]", s) == 0)
		return SECTION_PURGE_FILTERS;
	else if (strcmp("[summarize]", s) == 0)
		return SECTION_SUMMARIZE_FILTERS;
	else
		err(1, "invalid section name: %s", s);
}

static void
cut_kv(int line, char *s, char **key, char **value)
{
	if (strstr(s, "=") == NULL)
		errx(1,
			"%s:%d: error: invalid format: %s. Should be: key = value"
			" (spaces are required before and after equal sign).",
			cfg.config_fname, line, s);

	char *ptr = strstr(s, " = ");

	if (ptr == NULL)
		errx(1, "%s:%d: error: spaces are required before and after equal sign: %s",
			cfg.config_fname, line, s);

	*key = s;
	*ptr = 0;
	ptr += 3;
	*value = ptr;
}

static void
ensure_dir(const char *path)
{
	if (exists(path))
		return;

	int rc = mkdir(path, 0700);

	if (rc != 0)
		err(1, "cannot create local dir %s", path);
}

static void
ensure_local_dir()
{
	char path[PATH_MAX];

	strcpy(path, getenv("HOME"));
	strcat(path, "/.local");
	ensure_dir(path);
	strcat(path, "/mailbot");
	ensure_dir(path);
	strcat(path, "/");
	strcat(path, cfg.name);
	ensure_dir(path);
}

int
config_init(int argc, char **argv)
{
	FILE *f;
	char *ptr, s[200], *key, *value;
	enum section curr_section = SECTION_NONE;
	int ch, line = 0;
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

		if (s[0] == '[') {
			curr_section = parse_section_name(s);
			continue;
		}

		cut_kv(line, s, &key, &value);

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
				cfg.purge_filters = add_filter(cfg.purge_filters, value);
			} else {
				errx(1, "%s:%d: error: unknown parameter: %s", cfg.config_fname, line, s);
			}
		} else if (curr_section == SECTION_SUMMARIZE_FILTERS) {
			if (strcmp("filter", key) == 0) {
				cfg.summarize_filters = add_filter(cfg.summarize_filters, value);
			} else {
				errx(1, "%s:%d: error: unknown parameter: %s", cfg.config_fname, line, s);
			}
		} else {
			errx(1, "%s:%d: error: unknown parameter: %s", cfg.config_fname, line, s);
		}
	}

	fclose(f);
	cfg.purge_filters = reverse_filter(cfg.purge_filters);
	cfg.summarize_filters = reverse_filter(cfg.summarize_filters);

	const char *start = strrchr(cfg.config_fname, '/');
	if (start == NULL)
		start = cfg.config_fname;

	const char *end = strrchr(cfg.config_fname, '.');
	if (end == NULL)
		end = cfg.config_fname + strlen(cfg.config_fname);

	if (end <= start)
		errx(1, "invalid config file name");

	int len = end - start - 1;
	asprintf(&cfg.name, "%.*s", len, start + 1);
	asprintf(&cfg.local_dir, "%s/.local/mailbot/%s", getenv("HOME"), cfg.name);
	asprintf(&cfg.log_fname, "%s/.local/mailbot/mailbot.log", getenv("HOME"));
	asprintf(&cfg.uids_fname, "%s/uids.txt", cfg.local_dir);

	ensure_local_dir();

	if (show_config) {
		config_dump();
		exit(1);
	}

	return 0;
}

void
config_dump()
{
	printf("[internal]\n"
	       "name = %s\n"
	       "config_fname = %s\n"
	       "log_fname = %s\n"
	       "local_dir = %s\n"
	       "\n",
	       cfg.name,
	       cfg.config_fname,
	       cfg.log_fname,
	       cfg.local_dir
	);

	printf(
		"[imap]\n"
		"url = %s\n"
		"login = %s\n"
		"password = %s\n",
		cfg.imap.url,
		cfg.imap.login,
		cfg.imap.password
		);

	printf("\n[purge]\n");
	for (struct filter *f = cfg.purge_filters; f != NULL; f = f->next) {
		printf("filter = %d,%s\n", f->days_before, f->gmail_filter);
	}

	printf("\n[summarize]\n");
	for (struct filter *f = cfg.summarize_filters; f != NULL; f = f->next) {
		printf("filter = %d,%s\n", f->days_before, f->gmail_filter);
	}
}

void
config_free()
{
}
















