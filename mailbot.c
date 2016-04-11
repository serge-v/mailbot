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
add_to_summary(int uid, struct buf *summary)
{
	char fname[PATH_MAX];
	char text[100000];
	char ts[50];
	char *amount, *place, *date;

	snprintf(fname, PATH_MAX-1, "%s/fetch-%d~.txt", cfg.local_dir, uid);
	if (!exists(fname)) {
		logwarn("file %s not found", fname);
		buf_appendf(summary, "%24s %6d      message is not found\n", "", uid);
//		logfatal("cannot found file %s for message uid %d", fname, uid);
		return 1;
	}

	FILE *f = fopen(fname, "rt");
	if (f == NULL)
		logfatal("cannot open %s", fname);

	size_t was_read = fread(text, 1, 100000-1, f);
	fclose(f);

	if (was_read == 0) {
		logwarn("file %s is empty", fname);
		buf_appendf(summary, "%24s %6d      empty\n", "", uid);
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
			buf_appendf(summary, "%24s %6d      no match\n", "", uid);
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
	buf_appendf(summary, "%-24s %6d    %6s  %-60s\n", ts, uid, amount, place);

	return 0;
}

static void
summarize_messages(const int *seqnums, const int *uids, int count)
{
	if (!cfg.offline) {
		for (int i = 0; i < count; i++)
			fetch_message(seqnums[i], uids[i]);
	}

	struct buf summary;
	buf_init(&summary);

	for (int i = 0; i < count; i++)
		add_to_summary(uids[i], &summary);

	char summary_fname[PATH_MAX];
	snprintf(summary_fname, PATH_MAX-1, "%s/summary.txt", cfg.local_dir);
	FILE *fs = fopen(summary_fname, "wt");
	if (fs == NULL)
		logfatal("cannot open file %s", summary_fname);
	fwrite(summary.s, 1, summary.len, fs);
	fclose(fs);
	logi("summary saved: %s", summary_fname);
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
load_categories()
{

}

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

		if (strcmp(date, "2015-03-01") < 0)
			continue;

		if (ttcount >= ttcap) {
			ttcap += 500;
			table = realloc(table, ttcap * sizeof(struct total_row));
		}

		// assign group
/*
		bool found = false;
		for (int i = 0; i < group_count; i++) {
			if (strstr(name, groups[i].name) == name) {
				strcpy(name, "==");
				strcat(name, groups[i].group);
				found = true;
				break;
			}
		}
*/
//		if (!found) {
			//			strcpy(name, "==MISC");
//		}

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
			item.data = ttcount;
			item.key = tr->name;
			hsearch(item, ENTER);
			ttcount++;
		}
	}

	*totals_table = table;
	*count = ttcount;
}

static void
aggregate_singles(struct total_row *totals_table, int ttcount)
{
	int singles_idx = -1;

	for (int i = 0; i < ttcount; i++) {
		struct total_row *tr = &totals_table[i];
		if (tr->count == 1 && tr->name[0] != '=' && tr->sum < 20) {
			if (singles_idx == -1) {
				singles_idx = i;
				strcpy(tr->name, "==SINGLE");
			} else {
				totals_table[singles_idx].count += tr->count;
				totals_table[singles_idx].sum += tr->sum;
				tr->sum = 0;
			}
		}
	}
}

static void
write_summary_header(FILE *f)
{
	fprintf(f,
		"# This is the summary of your transactions.\n"
		"# Uncategorized transactions are located at the beginning of the list\n"
		"# Categorize transactions by adding CATEGORY character after the dot on each line.\n"
		"# Categories:\n"
		"# c -- car\n"
		"# f -- food\n"
		"# g -- goods\n"
		"# h -- home\n"
		"# t -- clothes\n"
		"# u -- fun\n"
		"\n\n"
		"# NAME                 COUNT AMOUNT  CATEGORY\n"
	);
}

static void
create_report()
{
	char transactions_fname[PATH_MAX];
	char summary_fname[PATH_MAX];
	int unclassified_count = 0;

	snprintf(transactions_fname, PATH_MAX-1, "%s/transactions.txt", cfg.local_dir);
	snprintf(summary_fname, PATH_MAX-1, "%s/summary.txt", cfg.local_dir);

	FILE *fc = fopen(summary_fname, "wt");
	if (fc == NULL)
		logfatal("cannot open file %s", summary_fname);

	write_summary_header(fc);

	struct total_row *totals_table = NULL;
	int ttcount = 0;

	aggregate_transactions(transactions_fname, &totals_table, &ttcount);
	if (0) {
		aggregate_singles(totals_table, ttcount);
	}

	qsort(totals_table, ttcount, sizeof(struct total_row), cmp_total_row);

	for (int i = 0; i < ttcount; i++) {
		struct total_row *tr = &totals_table[i];
		if (tr->sum > 0) {
			printf("  %-24s %3d  %5.0f\n", tr->name, tr->count, tr->sum);
			if (tr->name[0] != '=') {
				fprintf(fc, "%-24s %3d  %5.0f        .\n", tr->name, tr->count, tr->sum);
				unclassified_count++;
			}
		}
	}

	fclose(fc);
	printf("%d unclassified saved to summary: %s.\n"
	       "Run 'mailbot -e' to edit summary file\n",
		unclassified_count, summary_fname);
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

	cfg.offline = 1;
//	cfg.verbose = 1;
	cfg.report = 0;

	init_imap_client();

	purge();

	if (!cfg.offline)
		fetch_uids(cfg.uids_fname);

	int *uids = NULL;
	int *seqnums = NULL;
	int count = 0;

	load_ids(cfg.uids_fname, &seqnums, &uids, &count);

	if (cfg.report) {
		summarize_messages(seqnums, uids, count);
		create_report();
	}

	close_imap_client();
	logi("done");
	log_close();

	return 0;
}
