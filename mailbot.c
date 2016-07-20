/*
 * Manages emails in imap inbox folder:
 *   - purges old emails by days old and keywords
 *   - sends summary of operations
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <limits.h>
#include <search.h>
#include <ctype.h>
#include "common/log.h"
#include "common/struct.h"
#include "common/regexp.h"
#include "common/fs.h"
#include "conf.h"
#include "imap.h"
#include "util.h"

const char ccc1[] =
	"A charge of \\(\\$USD\\) ([0-9\\.][0-9\\.]*) "
	"at (.*) "
	"has been authorized on "
	"([0-9][0-9]/[0-9][0-9]/20[0-9][0-9] [0-9][0-9]*:[0-9][0-9]:[0-9][0-9] [AP]M [A-Z]{3}).";

const char ccc2[] =
	"a gas station charge. This charge was authorized "
	"at (.*) on "
	"([0-9][0-9]/[0-9][0-9]/20[0-9][0-9] [0-9][0-9]*:[0-9][0-9]:[0-9][0-9] [AP]M [A-Z]{3}).";

static int
add_to_tranlist(int uid, struct buf *tranlist)
{
	char fname[PATH_MAX];
	char text[100000];
	char ts[50];
	char *amount, *place, *date;

	snprintf(fname, PATH_MAX-1, "%s/fetch-%d~.txt.edited", cfg.local_dir, uid);
	if (!exists(fname)) {
		snprintf(fname, PATH_MAX-1, "%s/fetch-%d~.txt", cfg.local_dir, uid);
		if (!exists(fname)) {
			fprintf(stderr, "%s: not found\n", fname);
			logwarn("file %s not found", fname);
			buf_appendf(tranlist, "%24s %6d      message is not found\n", "", uid);
			return 1;
		}
	}

	FILE *f = fopen(fname, "rt");
	if (f == NULL) {
		fprintf(stderr, "%s: cannot open\n", fname);
		logfatal("cannot open %s", fname);
	}

	size_t was_read = fread(text, 1, 100000-1, f);
	fclose(f);

	if (was_read == 0) {
		fprintf(stderr, "%s: file is empty\n", fname);
		logwarn("file %s is empty", fname);
		buf_appendf(tranlist, "%24s %6d      empty\n", "", uid);
		return 1;
	}

	regex_t rex;
	regex_compile(&rex, ccc1);

	regmatch_t match[10];

	int rc = regexec(&rex, text, 10, match, 0);

	if (rc == REG_NOMATCH) {
		regfree(&rex);
		regex_compile(&rex, ccc2);
		rc = regexec(&rex, text, 10, match, 0);
		if (rc == REG_NOMATCH) {
			fprintf(stderr, "%s: no match\n", fname);
			buf_appendf(tranlist, "%24s %6d      no match\n", "", uid);
			return 1;
		}

		amount = "56.44";
		place = &text[match[1].rm_so];
		text[match[1].rm_eo] = 0;
		date = &text[match[2].rm_so];
		text[match[2].rm_eo] = 0;
	} else {
		amount = &text[match[1].rm_so];
		text[match[1].rm_eo] = 0;
		place = &text[match[2].rm_so];
		text[match[2].rm_eo] = 0;
		date = &text[match[3].rm_so];
		text[match[3].rm_eo] = 0;
	}

	time_t t = parse_time(date);
	format_time(ts, 49, &t);
	buf_appendf(tranlist, "%-24s %6d    %6s  %-60s\n", ts, uid, amount, place);

	return 0;
}

static void
extract_transactions(const int *seqnums, const int *uids, int count)
{
	if (!cfg.offline) {
		for (int i = 0; i < count; i++)
			fetch_message(seqnums[i], uids[i]);
	}

	struct buf tranlist;
	buf_init(&tranlist);

	for (int i = 0; i < count; i++)
		add_to_tranlist(uids[i], &tranlist);

	char tranlist_fname[PATH_MAX];
	snprintf(tranlist_fname, PATH_MAX-1, "%s/transactions.txt", cfg.local_dir);
	FILE *fs = fopen(tranlist_fname, "wt");
	if (fs == NULL)
		logfatal("cannot open file %s", tranlist_fname);
	fwrite(tranlist.s, 1, tranlist.len, fs);
	fclose(fs);
	logi("summary saved: %s", tranlist_fname);
}

struct total_row
{
	char *name;
	float sum;
	int count;
	char category;
};

static int
cmp_total_row(const void *a, const void *b)
{
	struct total_row *x = (struct total_row *)a;
	struct total_row *y = (struct total_row *)b;

	if (x->sum > y->sum)
		return 1;

	if (x->sum < y->sum)
		return -1;

	return 0;
}

struct category
{
	const char *vendor_name;
	char category;
	const char *label;
};

static void
trims(char *s)
{
	char *p = strrchr(s, '\n');
	if (p == NULL)
		return;

	while (isspace(*p))
		p--;

	p++;
	*p = 0;
}

static void
aggregate_transactions(const char *fname, struct total_row **totals_table, int *count)
{
	char s[500], date[30], time[30], name[100];
	int uid;
	float amount;

	int ttcount = 0, ttcap = 0;
	struct total_row *table = NULL;

	FILE *f = fopen(fname, "rt");
	if (f == NULL)
		logfatal("cannot open file %s", fname);

	ENTRY item, *found_item;
	hcreate(500);

	while (!feof(f)) {
		fgets(s, 499, f);
		trims(s);
		int n = sscanf(s, "%s %s %d %f %[^\n]", date, time, &uid, &amount, name);
		if (n != 5) {
			logwarn("invalid line: %s", s);
			continue;
		}

		if (strcmp(date, "2016-01-01") < 0)
			continue;

		if (ttcount >= ttcap) {
			ttcap += 500;
			table = realloc(table, ttcap * sizeof(struct total_row));
		}

		item.key = name;
		found_item = hsearch(item, FIND);
		if (found_item != NULL) {
			struct total_row *tr = &table[(int)found_item->data];
			tr->sum += amount;
			tr->count++;
		} else {
			struct total_row *tr = &table[ttcount];
			tr->name = strdup(name);
			tr->sum = amount;
			tr->count = 1;
			item.data = (void*)(uint64_t)ttcount;
			item.key = tr->name;
			hsearch(item, ENTER);
			ttcount++;
		}
	}

	*totals_table = table;
	*count = ttcount;
}

static void
create_report()
{
	char transactions_fname[PATH_MAX];
	char unclassified_fname[PATH_MAX];
	int unclassified_count = 0;

	snprintf(transactions_fname, PATH_MAX-1, "%s/transactions.txt", cfg.local_dir);
	snprintf(unclassified_fname, PATH_MAX-1, "%s/unclassified.txt", cfg.local_dir);

	FILE *fc = fopen(unclassified_fname, "wt");
	if (fc == NULL)
		logfatal("cannot open file %s", unclassified_fname);

	struct total_row *totals_table = NULL;
	int ttcount = 0;

	aggregate_transactions(transactions_fname, &totals_table, &ttcount);

	qsort(totals_table, ttcount, sizeof(struct total_row), cmp_total_row);

	for (int i = 0; i < ttcount; i++) {
		struct total_row *tr = &totals_table[i];
		if (tr->sum > 0) {
			if (cfg.verbose)
				printf("  %-24s %3d  %5.0f\n", tr->name, tr->count, tr->sum);
			if (tr->name[0] != '=') {
				fprintf(fc, "%-24s %3d  %5.0f        .\n", tr->name, tr->count, tr->sum);
				unclassified_count++;
			}
		}
	}

	fclose(fc);
	printf("%d unclassified saved to: %s.\n"
	       "Run 'mailbot -e' to edit file\n",
		unclassified_count, unclassified_fname);
}

static void
edit_unclassified()
{
	char cmd[PATH_MAX];

	snprintf(cmd, PATH_MAX-1, "edit %s/unclassified.txt", cfg.local_dir);
	system(cmd);
}

int main(int argc, char **argv)
{
	if (config_init(argc, argv) != 0)
		return 1;

	log_open(cfg.log_fname);

	if (cfg.classify) {
		edit_unclassified();
		return 0;
	}

	logi("processing for mailbox: %s, user: %s", cfg.imap.url, cfg.imap.login);

	setenv("TZ", "UTC", 1);
	tzset();

	if (!cfg.offline) {
		init_imap_client();
		purge();
		fetch_uids(cfg.uids_fname);
	}

	int *uids = NULL;
	int *seqnums = NULL;
	int count = 0;

	if (cfg.report) {
		load_ids(cfg.uids_fname, &seqnums, &uids, &count);
		extract_transactions(seqnums, uids, count);
		create_report();
	}

	close_imap_client();
	logi("done");
	log_close();

	return 0;
}
