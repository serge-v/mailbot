#include "util.h"
#include <string.h>
#include <time.h>

void
get_timestamp(char *ts, size_t len, int days_ago)
{
	time_t now = time(NULL) - days_ago * 24 * 3600;
	struct tm *tm = gmtime(&now);
	strftime(ts, len - 1, "%d-%b-%Y", tm);
}
