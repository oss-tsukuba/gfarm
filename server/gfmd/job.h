struct job_table_entry;

void job_table_init(int);
int job_table_remove(int, char *, struct job_table_entry **);
int job_get_id(struct job_table_entry *);

gfarm_error_t gfj_server_lock_register(struct peer *, int, int);
gfarm_error_t gfj_server_unlock_register(struct peer *, int, int);
gfarm_error_t gfj_server_register(struct peer *, int, int);
gfarm_error_t gfj_server_unregister(struct peer *, int, int);
gfarm_error_t gfj_server_register_node(struct peer *, int, int);
gfarm_error_t gfj_server_list(struct peer *, int, int);
gfarm_error_t gfj_server_info(struct peer *, int, int);
gfarm_error_t gfj_server_hostinfo(struct peer *, int, int);
