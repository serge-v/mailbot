struct config
{
	const char *config_fname;
	const char *log_fname;
	int debug;

	struct
	{
		const char *host;
		const char *folder;
		const char *user;
		const char *password;
	} imap;
};

extern struct config cfg;

int config_init(int argc, char **argv);
void config_dump();
