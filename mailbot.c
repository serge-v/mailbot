/*
 * Manages emails in imap inbox folder:
 *   - purges old emails by days old and keywords
 *   - sends summary of operations
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <curl/curl.h>
#include "common/log.h"
#include "conf.h"

static const char inbox_url[] = "imaps://imap.gmail.com:993/INBOX";
static const char username[] = "serge0x76@gmail.com";
static const char pwd_rel_fname[] = ".config/departures/smtp.txt";
static CURL *curl;
static FILE *fnull;
static int verbose = 0;

struct mail_filter
{
	int days_before;          /* Apply to messages older than (now - days_before) */
	const char *gmail_filter; /* Gmail X-GM-RAW filter format (Google imap extension) */
};

static struct mail_filter purge_filters[] = {
	{ 2, "from:bugzilla-noreply@freebsd.org" },
	{ 2, "bcc:serge0x76+cam@gmail.com" },
	{ 2, "Digest for concurrencykit" },
	{ 2, "to:freebsd-arm@freebsd.org" },
	{ 2, "to:freebsd-wireless@freebsd.org" },
	{ 2, "from:jenkins@appnexus.com" },
	{ 2, "from:updates@ce.homeadvisor.com" },
	{ 2, "from:notifications@github.com" },
	{ 2, "to:serge0x76+weather@gmail.com" },
	{ 0, NULL }
};

#define MAX_RESULTS 1000
static uint64_t uids[MAX_RESULTS];

static void
get_password(char *s, int len)
{
	char fname[100];

	snprintf(fname, 99, "%s/%s", getenv("HOME"), pwd_rel_fname);

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

	curl_easy_setopt(curl, CURLOPT_URL, inbox_url);
	curl_easy_setopt(curl, CURLOPT_USERNAME, username);
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
	snprintf(cmd, 99, "SEARCH X-GM-RAW \"%s\"", gmail_filter);

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
fetch(uint64_t uid)
{
	CURLcode res = CURLE_OK;
	char url[100];
	char fname[100];

	snprintf(fname, 99, "fetch-%" PRIu64 "~.txt", uid);
	FILE *f = fopen(fname, "wb");
	if (f == NULL)
		logfatal("cannot open output file");

	curl_easy_setopt(curl, CURLOPT_WRITEDATA, f);
	curl_easy_setopt(curl, CURLOPT_VERBOSE, verbose);

	sprintf(url, "%s;UID=%" PRIu64, inbox_url, uid);
	curl_easy_setopt(curl, CURLOPT_URL, url);
	
	res = curl_easy_perform(curl);

	if (res != CURLE_OK)
		logfatal("curl_easy_perform() failed: %s\n", curl_easy_strerror(res));

	fclose(f);
}

static void
delete(uint64_t uid)
{
	CURLcode res = CURLE_OK;
	char cmd[100];
	char fname[100];

	curl_easy_setopt(curl, CURLOPT_URL, inbox_url);
	curl_easy_setopt(curl, CURLOPT_VERBOSE, 0L);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, fnull);
	curl_easy_setopt(curl, CURLOPT_VERBOSE, verbose);

	sprintf(cmd, "STORE %" PRIu64 " +Flags \\Deleted", uid);
	curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, cmd);
	
	res = curl_easy_perform(curl);

	if (res != CURLE_OK)
		logfatal("curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
	
	logi("deleted: %" PRIu64, uid);
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
		int ret = fscanf(f, "%llu", &uid);
		count++;
		if (ret != 1 || count >= 100)
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
delete_filtered(const struct mail_filter *filter)
{
	char gmail_filter[100];
	char timestamp[20];

	get_timestamp(timestamp, 19, filter->days_before);
	snprintf(gmail_filter, 99, "before:%s %s", timestamp, filter->gmail_filter);	

	search(gmail_filter);
	int count = delete_found_uids();
	logi("purged for filter: \"%s\": %d", gmail_filter, count);
}

static void
purge()
{
	struct mail_filter *filter = purge_filters;
	while (filter->days_before != 0) {
		delete_filtered(filter);
		filter++;
	}
}

int main(int argc, char **argv)
{
	if (config_init(argc, argv) != 0)
		return 1;

	char log_fname[PATH_MAX+1];

	snprintf(log_fname, PATH_MAX, "%s/.local/mailbot/mailbot.log", getenv("HOME"));
	log_open(log_fname);

	init_curl();
	purge();
	curl_easy_cleanup(curl);
	log_close();

	return 0;
}
