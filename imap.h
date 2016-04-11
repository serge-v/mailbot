void init_imap_client();
void close_imap_client();

void load_folder_list(const char *fname);

void fetch_uids(const char *fname);
void load_ids(const char *fname, int **seqnums, int **uids, int *count);

void fetch_message(int seqnum, int uid);
void delete_message(int uid);
void delete_messages(int *seqnums, int count);
void search(const char *query);
void delete_found();
void purge();
