struct filter
{
	int days_before;          /* Apply to messages older than (now - days_before) */
	const char *filter;
	const char *parser_name;
	struct filter *next;
};

struct config
{
	char *config_dir;         /* default config dir: ~/.config/mailbot */
	char *config_fname;       /* config file name */
	char *name;               /* ini base name without extension */
	char *log_fname;          /* log file name in ~/.local dir */
	char *local_dir;          /* mailbox local storage dir ~/.local/mailbot/<name> */
	char *uids_fname;         /* file with found uids for a mailbox */
	int debug;
	int verbose;
	int offline;
	int list_configs;         /* list available configs in .config/mailbot */

	struct
	{
		const char *url;
		const char *login;
		const char *password;
	} imap;
	
	struct filter *purge_filters;
	struct filter *fetch_filters;
};

extern struct config cfg;

int config_init(int argc, char **argv);
void config_dump();
