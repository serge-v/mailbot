/*
 * mailbot manages emails in imap inbox folder:
 *   - fetches emails
 *   - purges old emails by days old and keywords
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "common/log.h"
#include "conf.h"
#include "imap.h"
#include "util.h"

int main(int argc, char **argv)
{
	if (config_init(argc, argv) != 0)
		return 1;

	log_open(cfg.log_fname);
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

	load_ids(cfg.uids_fname, &seqnums, &uids, &count);
	for (int i = 0; i < count; i++)
		fetch_message(seqnums[i], uids[i]);

	close_imap_client();
	logi("done");
	log_close();

	return 0;
}
