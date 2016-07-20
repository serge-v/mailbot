#include "common/log.h"
#include "common/fs.h"
#include "conf.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>

static CURL *curl;
static FILE *fnull;

static void
get_password(char *s, int len)
{
	char cmd[100];
	char fname[PATH_MAX];
	size_t was_read = 0;

	snprintf(fname, PATH_MAX-1, "%s/%s.txt", cfg.local_dir, cfg.imap.password);

	FILE *f = fopen(fname, "r");
	if (f == NULL) {

		snprintf(cmd, 99, "pass %s", cfg.imap.password);

		f = popen(cmd, "r");
		if (f == NULL)
			logfatal("cannot start password manager: %s", cmd);

		was_read = fread(s, 1, len - 1, f);

		pclose(f);
	} else {
		was_read = fread(s, 1, len - 1, f);
		fclose(f);
	}

	if (was_read == 0)
		logfatal("password is empty for %s entry", cfg.imap.password);

	s[was_read-1] = 0;
}

void
init_imap_client()
{
	if (curl != NULL)
		logfatal("curl already inited");

	char password[100];

	curl = curl_easy_init();
	if (curl == NULL)
		logfatal("cannot init curl");

	get_password(password, 100);

	curl_easy_setopt(curl, CURLOPT_USERNAME, cfg.imap.login);
	curl_easy_setopt(curl, CURLOPT_PASSWORD, password);
	curl_easy_setopt(curl, CURLOPT_USE_SSL, (long)CURLUSESSL_ALL);
	curl_easy_setopt(curl, CURLOPT_VERBOSE, cfg.verbose);

	fnull = fopen("/dev/null", "wb");
	if (fnull == NULL)
		logfatal("cannot open /dev/null");
}

void
close_imap_client()
{
	curl_easy_cleanup(curl);
	curl = NULL;
}

void
load_folder_list(const char *fname)
{
	CURLcode res = CURLE_OK;
	FILE *f = fopen(fname, "wb");
	curl_easy_setopt(curl, CURLOPT_URL, cfg.imap.url);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, f);
	res = curl_easy_perform(curl);

	if (res != CURLE_OK)
		logfatal("curl: %s", curl_easy_strerror(res));

	fclose(f);
	logi("folder list saved to %s", fname);
}

void
fetch_uids(const char *fname)
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

	while (1) {
		snprintf(cmd, 99, "UID FETCH %d:%d (UID)", start, start + count - 1);
		curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, cmd);
		long pos1 = ftell(f);
		res = curl_easy_perform(curl);
		if (res != CURLE_OK)
			logfatal("curl: %s", curl_easy_strerror(res));
		start += count;
		long pos2 = ftell(f);
		if (pos2 - pos1 < 12)
			break;
	}

	fclose(f);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, fnull);
	curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, NULL);
	rename(tmp_fname, fname);
	logi("%s saved", fname);
}

void
fetch_message(int seqnum, int uid)
{
	CURLcode res = CURLE_OK;
	char url[100];
	char fname[PATH_MAX];

	snprintf(fname, PATH_MAX-1, "%s/fetch-%d~.txt", cfg.local_dir, uid);

	if (exists(fname)) {
		logi("fetched already: %d", uid);
		return;
	}

	FILE *f = fopen(fname, "wb");
	if (f == NULL)
		logfatal("cannot open output file");

	curl_easy_setopt(curl, CURLOPT_WRITEDATA, f);

	snprintf(url, 99, "%s;UID=%d", cfg.imap.url, seqnum);
	curl_easy_setopt(curl, CURLOPT_URL, url);

	res = curl_easy_perform(curl);

	if (res != CURLE_OK)
		logfatal("curl: %s", curl_easy_strerror(res));

	fclose(f);
	logi("fetched: %d, %d", seqnum, uid);
}

void
delete_message(int seqnum)
{
	CURLcode res = CURLE_OK;
	char cmd[100];

	curl_easy_setopt(curl, CURLOPT_URL, cfg.imap.url);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, fnull);

	sprintf(cmd, "UID STORE %d +Flags \\Deleted", seqnum);
	curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, cmd);

	res = curl_easy_perform(curl);

	if (res != CURLE_OK)
		logfatal("curl: %s", curl_easy_strerror(res));

	logi("%d deleted", seqnum);
}

void
delete_messages(int *seqnums, int count)
{
	CURLcode res = CURLE_OK;

	for (int i = 0; i < count; i++) {
		delete_message(seqnums[i]);

		if (i % 200 == 0 || i == count-1) {
			curl_easy_setopt(curl, CURLOPT_WRITEDATA, fnull);
			curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "EXPUNGE");

			res = curl_easy_perform(curl);
			if (res != CURLE_OK)
				logfatal("curl: %s", curl_easy_strerror(res));

			logi("expunge after %d [seq=%d]", i, seqnums[i]);
		}
	}
	logi("total %d deleted", count);
}

void
load_ids(const char *fname, int **seqnums, int **uids, int *count)
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
			logfatal("invalid file %s", fname);

		if (seqnum == 0 || uid == 0)
			logfatal("invalid seqnum or uid %s", fname);

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

void
search(const char *query)
{
	char cmd[100];
	char fname[PATH_MAX];

	CURLcode res = CURLE_OK;

	snprintf(fname, PATH_MAX-1, "%s/query.txt", cfg.local_dir);
	FILE *f = fopen(fname, "wt");
	if (f == NULL)
		logfatal("cannot open file %s", fname);

//	snprintf(cmd, 99, "SEARCH UID X-GM-RAW \"%s\"", gmail_query);
	snprintf(cmd, 99, "UID SEARCH %s", query);

	curl_easy_setopt(curl, CURLOPT_URL, cfg.imap.url);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, f);
	curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, cmd);
	curl_easy_setopt(curl, CURLOPT_VERBOSE, cfg.debug);

	res = curl_easy_perform(curl);

	if (res != CURLE_OK)
		logfatal("curl_easy_perform() failed: %s\n", curl_easy_strerror(res));

	fclose(f);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, fnull);
	printf("search saved to %s\n", fname);
}

void
delete_found()
{
	char fname[PATH_MAX];
	char magic[20];

	snprintf(fname, PATH_MAX-1, "%s/query.txt", cfg.local_dir);

	FILE *f = fopen(fname, "rt");
	if (f == NULL)
		logfatal("cannot open file %s", fname);


	size_t was_read = fread(magic, 1, 10, f);

	if (was_read == 10 && strncmp("* SEARCH\r\n", magic, 10) == 0) {
		fclose(f);
		return;
	}

	if (was_read != 9 && strncmp("* SEARCH ", magic, 9) != 0)
		logfatal("invalid search results: %s", fname);

	int *seqnums = NULL;
	int count = 0;
	int cap = 0;

	while (!feof(f)) {
		int uid = 0;
		if (fscanf(f, "%d", &uid) != 1)
			break;

		if (count >= cap) {
			cap += 500;
			seqnums = realloc(seqnums, cap * sizeof(int));
		}

		seqnums[count] = uid;
		count++;
	}

	delete_messages(seqnums, count);
}

void
purge()
{
	char ts[100];
	char query[200];

	for (struct filter *f = cfg.purge_filters; f != NULL; f = f->next) {
		logi("purge for filter: %s", f->filter);
		get_timestamp(ts, 99, f->days_before);
		snprintf(query, 199, "(BEFORE %s) %s", ts, f->filter);
		search(query);
		delete_found();
	}
}
