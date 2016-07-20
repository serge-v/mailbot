#include <time.h>

time_t parse_date(const char *tt);
time_t parse_time(const char *tt);
void format_time(char *ts, size_t len, time_t *t);
void get_timestamp(char *ts, size_t len, int days_ago);
