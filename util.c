#include "util.h"
#include <string.h>

time_t
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

time_t
parse_date(const char *tt)
{
	struct tm tm = {0};

	char *p = strptime(tt, "%m/%d/%Y", &tm);
	if (p == NULL)
		return 0;

	time_t t = mktime(&tm);
	return t;
}

void
format_time(char *ts, size_t len, time_t *t)
{
	struct tm *tm = gmtime(t);
	strftime(ts, len - 1, "%Y-%m-%d %H:%M:%S", tm);
}

void
get_timestamp(char *ts, size_t len, int days_ago)
{
	time_t now = time(NULL) - days_ago * 24 * 3600;
	struct tm *tm = gmtime(&now);
	strftime(ts, len - 1, "%d-%b-%Y", tm);
}
