struct filter
{
	int days_before;          /* Apply to messages older than (now - days_before) */
	const char *gmail_filter; /* Gmail X-GM-RAW filter format (Google imap extension) */
	struct filter *next;
};

struct config
{
	const char *config_fname;
	const char *log_fname;
	int debug;

	struct
	{
		const char *url;
		const char *login;
		const char *password;
	} imap;
	
	struct filter *purge_filters;
};

extern struct config cfg;

int config_init(int argc, char **argv);
void config_dump();
