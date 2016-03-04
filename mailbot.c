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
#include <inttypes.h>
#include <regex.h>
#include <curl/curl.h>
#include "common/log.h"
#include "common/struct.h"
#include "common/regexp.h"
#include "common/fs.h"
#include "common/net.h"
#include "conf.h"

static CURL *curl;
static FILE *fnull;
static int verbose = 1;
static bool offline = false;

static void
get_password(char *s, int len)
{
	char fname[100];

	snprintf(fname, 99, "%s/%s", getenv("HOME"), cfg.imap.password);

	FILE *f = fopen(fname, "rt");
	if (f == NULL)
		logfatal("cannot open password file %s", fname);

	size_t was_read = fread(s, 1, len - 1, f);

	fclose(f);

	if (was_read == 0)
		logfatal("password is empty in file %s", fname); 

	s[was_read] = 0;
}

static void
get_timestamp(char *ts, size_t len, int days_ago)
{
	time_t now = time(NULL) - days_ago * 24 * 3600;
	struct tm *tm = gmtime(&now);
	strftime(ts, len - 1, "%Y-%m-%d", tm);
}

static void
init_curl()
{
	char password[100];

	curl = curl_easy_init();
	if (curl == NULL)
		logfatal("cannot init curl");

	get_password(password, 100);

	curl_easy_setopt(curl, CURLOPT_USERNAME, cfg.imap.login);
	curl_easy_setopt(curl, CURLOPT_PASSWORD, password);
	curl_easy_setopt(curl, CURLOPT_USE_SSL, (long)CURLUSESSL_ALL);
	
	fnull = fopen("/dev/null", "wb");
	if (fnull == NULL)
		logfatal("cannot open /dev/null");
}

static void
load_folder_list()
{
	CURLcode res = CURLE_OK;
	FILE *f = fopen("list~.txt", "wb");
	curl_easy_setopt(curl, CURLOPT_URL, cfg.imap.url);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, f);
	curl_easy_setopt(curl, CURLOPT_VERBOSE, verbose);
	res = curl_easy_perform(curl);

	if (res != CURLE_OK)
		logfatal("curl_easy_perform() failed: %s\n", curl_easy_strerror(res));

	fclose(f);
	printf("folder list loaded\n");
}

static void
search(const char *gmail_filter)
{
	char cmd[100];
	CURLcode res = CURLE_OK;
	FILE *f = fopen("search~.txt", "wb");

//	snprintf(cmd, 99, "SEARCH UID X-GM-RAW \"%s\"", gmail_filter);
	snprintf(cmd, 99, "UID SEARCH UID 4000:*");

	curl_easy_setopt(curl, CURLOPT_URL, cfg.imap.url);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, f);
	curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, cmd);
	curl_easy_setopt(curl, CURLOPT_VERBOSE, verbose);

	res = curl_easy_perform(curl);

	if (res != CURLE_OK)
		logfatal("curl_easy_perform() failed: %s\n", curl_easy_strerror(res));

	fclose(f);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, fnull);
}

static void
get_uids(const char *fname)
{
	char tmp_fname[PATH_MAX];
	char cmd[100];
	CURLcode res = CURLE_OK;

	logi("loading uids into %s", fname);
	snprintf(tmp_fname, PATH_MAX-1, "%s.tmp", fname);

	FILE *f = fopen(tmp_fname, "wt");
	if (f == NULL)
		logfatal("cannot open file %s", tmp_fname);

	int start = 1;
	int count = 100;

	curl_easy_setopt(curl, CURLOPT_URL, cfg.imap.url);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, f);
	curl_easy_setopt(curl, CURLOPT_VERBOSE, verbose);

	while (1) {
		snprintf(cmd, 99, "UID FETCH %d:%d (UID)", start, start + count - 1);
		curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, cmd);
		long pos1 = ftell(f);
		res = curl_easy_perform(curl);
		if (res != CURLE_OK)
			logfatal("curl: %s\n", curl_easy_strerror(res));
		start += count;
		long pos2 = ftell(f);
		if (pos2 - pos1 < 12)
			break;
	}

	fclose(f);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, fnull);
	rename(tmp_fname, fname);
	logi("%s saved", fname);
}

static void
fetch(int seqnum, int uid)
{
	CURLcode res = CURLE_OK;
	char url[100];
	char fname[PATH_MAX];

	snprintf(fname, PATH_MAX-1, "%s/fetch-%d~.txt", cfg.local_dir, uid);

	if (exists(fname)) {
		logi("fetched already: %" PRIu64, uid);
		return;
	}

	FILE *f = fopen(fname, "wb");
	if (f == NULL)
		logfatal("cannot open output file");

	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, f);
	curl_easy_setopt(curl, CURLOPT_VERBOSE, verbose);

	snprintf(url, 99, "%s;UID=%d", cfg.imap.url, seqnum);
	curl_easy_setopt(curl, CURLOPT_URL, url);

	res = curl_easy_perform(curl);

	if (res != CURLE_OK)
		logfatal("curl: %s\n", curl_easy_strerror(res));

	fclose(f);
	logi("fetched: %" PRIu64, uid);
}

static void
delete(uint64_t uid)
{
	CURLcode res = CURLE_OK;
	char cmd[100];

	curl_easy_setopt(curl, CURLOPT_URL, cfg.imap.url);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, fnull);
	curl_easy_setopt(curl, CURLOPT_VERBOSE, verbose);

	sprintf(cmd, "STORE %" PRIu64 " +Flags \\Deleted", uid);
	curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, cmd);
	
	res = curl_easy_perform(curl);

	if (res != CURLE_OK)
		logfatal("curl: %s\n", curl_easy_strerror(res));
}

static void
load_found_ids(const char *fname, int **seqnums, int **uids, int *count)
{
	FILE *f = fopen(fname, "rt");
	if (f == NULL)
		logfatal("cannot open file %s", fname);

	int seqnum = 0;
	int uid = 0;
	int cnt = 0;
	int cap = 100;
	int *arr1 = calloc(cap, sizeof(int));
	int *arr2 = calloc(cap, sizeof(int));
	char s[100];

	while (!feof(f)) {
		char *p = fgets(s, 99, f);
		if (p == NULL)
			break;

		int ret = sscanf(s, "* %d FETCH (UID %d)", &seqnum, &uid);
		if (ret != 2)
			logfatal(1, "invalid file %s", fname);

		if (seqnum == 0 || uid == 0)
			logfatal(1, "invalid seqnum or uid %s", fname);

		if (cnt >= cap) {
			cap += 100;
			arr1 = realloc(arr1, cap * sizeof(int));
			arr2 = realloc(arr2, cap * sizeof(int));
		}
		arr1[cnt] = seqnum;
		arr2[cnt] = uid;
		cnt++;
	}

	*seqnums = arr1;
	*uids = arr2;
	*count = cnt;

	fclose(f);
}

static int
delete_found_uids()
{
	CURLcode res = CURLE_OK;
	uint64_t uid = 0;
	int count = 0;

	FILE *f = fopen("search~.txt", "rt");
	int ret = fscanf(f, "* SEARCH %llu", &uid);
	if (ret != 1)
		return 0;

	while (uid > 0) {
		delete(uid);
		count++;
		logi("deleted: %d, %" PRIu64, count, uid);
		int ret = fscanf(f, "%llu", &uid);
		if (ret != 1 || count >= 1000)
			break;
	}
	fclose(f);

	curl_easy_setopt(curl, CURLOPT_WRITEDATA, fnull);
	curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "EXPUNGE");
	curl_easy_setopt(curl, CURLOPT_VERBOSE, verbose);

	res = curl_easy_perform(curl);
	if (res != CURLE_OK)
		logfatal("curl_easy_perform() failed: %s\n", curl_easy_strerror(res));

	return count;
}

static void
delete_filtered(const struct filter *filter)
{
	char gmail_filter[100];
	char timestamp[20];

	get_timestamp(timestamp, 19, filter->days_before);
	snprintf(gmail_filter, 99, "before:%s %s", timestamp, filter->gmail_filter);	
	logi("purging: \"%s\"", gmail_filter);

	search(gmail_filter);
	int count = delete_found_uids();
	logi("purged: %d", count);
}

static void
purge()
{
	for (struct filter *f = cfg.purge_filters; f != NULL; f = f->next) {
		delete_filtered(f);
	}
}

static time_t
parse_time(const char *tt)
{
	struct tm tm = {0};

	char *p = strptime(tt, "%m/%d/%Y %H:%M:%S %p ", &tm);
	if (p == NULL)
		return 0;

	time_t t = mktime(&tm);

	if (strcmp(p, "EST") == 0)
		t += 5 * 3600;
	else if (strcmp(p, "EDT") == 0)
		t += 4 * 3600;

	return t;
}

static void
format_time(char *ts, size_t len, time_t *t)
{
	struct tm *tm = gmtime(t);
	strftime(ts, len - 1, "%Y-%m-%d %H:%M:%S", tm);
}

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

		amount = "gas";
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

static int
summarize_by_uids(const int *seqnums, const int *uids, int count, struct buf *summary)
{
	if (!offline) {
		for (int i = 0; i < count; i++)
			fetch(seqnums[i], uids[i]);
	}

	for (int i = 0; i < count; i++)
		add_to_summary(uids[i], summary);

	return 0;
}

static void
summarize_filter(const struct filter *filter, struct buf *summary)
{
	char gmail_filter[100];
	char timestamp[20];

	get_timestamp(timestamp, 19, filter->days_before);
	snprintf(gmail_filter, 99, "before:%s %s", timestamp, filter->gmail_filter);
	logi("summarizing: \"%s\"", gmail_filter);

//	if (!offline)
//		get_uids(cfg.uids_fname);

	int count = summarize_found_uids(filter, summary);
	logi("summarized: %d", count);
	FILE *fs = fopen("summary~.txt", "wt");
	fwrite(summary->s, 1, summary->len, fs);
	fclose(fs);
}

int main(int argc, char **argv)
{
	if (config_init(argc, argv) != 0)
		return 1;

	log_open(cfg.log_fname);

	logi("processing for mailbox: %s, user: %s", cfg.imap.url, cfg.imap.login);

	setenv("TZ", "UTC", 1);
	tzset();

	offline = false;

	init_curl();

//	if (!offline)
//		get_uids(cfg.uids_fname);

	int *uids = NULL;
	int *seqnums = NULL;
	int count = 0;
	struct buf summary;

	load_found_ids(cfg.uids_fname, &seqnums, &uids, &count);

	buf_init(&summary);

	summarize_by_uids(seqnums, uids, count, &summary);

	char summary_fname[PATH_MAX];
	snprintf(summary_fname, PATH_MAX-1, "%s/summary.txt", cfg.local_dir);
	FILE *fs = fopen(summary_fname, "wt");
	fwrite(summary.s, 1, summary.len, fs);
	fclose(fs);
	logi("summary saved: %s", summary_fname);

	struct message m = {
		.to = cfg.imap.login,
		.subject = "summary",
		.from = "mailbot",
		.body = summary.s
	};

//	send_email(&m, cfg.imap.password);
//	purge();

	curl_easy_cleanup(curl);
	logi("done");
	log_close();

	return 0;
}
