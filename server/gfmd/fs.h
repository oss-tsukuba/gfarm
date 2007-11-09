struct peer;

/* gfs from client */
gfarm_error_t gfm_server_compound_begin(struct peer *, int, int, int);
gfarm_error_t gfm_server_compound_end(struct peer *, int, int, int);
gfarm_error_t gfm_server_compound_on_error(struct peer *, int, int, int,
	gfarm_error_t *);

gfarm_error_t gfm_server_get_fd(struct peer *, int, int);
gfarm_error_t gfm_server_put_fd(struct peer *, int, int);
gfarm_error_t gfm_server_save_fd(struct peer *, int, int);
gfarm_error_t gfm_server_restore_fd(struct peer *, int, int);

gfarm_error_t gfm_server_create(struct peer *, int, int);
gfarm_error_t gfm_server_open(struct peer *, int, int);
gfarm_error_t gfm_server_open_root(struct peer *, int, int);
gfarm_error_t gfm_server_open_parent(struct peer *, int, int);
gfarm_error_t gfm_server_close(struct peer *, int, int);
gfarm_error_t gfm_server_close_read(struct peer *, int, int);
gfarm_error_t gfm_server_close_write(struct peer *, int, int);
gfarm_error_t gfm_server_verify_type(struct peer *, int, int);
gfarm_error_t gfm_server_verify_type_not(struct peer *, int, int);
gfarm_error_t gfm_server_fstat(struct peer *, int, int);
gfarm_error_t gfm_server_futimes(struct peer *, int, int);
gfarm_error_t gfm_server_fchmod(struct peer *, int, int);
gfarm_error_t gfm_server_fchown(struct peer *, int, int);
gfarm_error_t gfm_server_cksum_get(struct peer *, int, int);
gfarm_error_t gfm_server_cksum_set(struct peer *, int, int);
gfarm_error_t gfm_server_schedule_file(struct peer *, int, int);
gfarm_error_t gfm_server_schedule_file_with_program(struct peer *, int, int);
gfarm_error_t gfm_server_remove(struct peer *, int, int);
gfarm_error_t gfm_server_rmdir(struct peer *, int, int);
gfarm_error_t gfm_server_rename(struct peer *, int, int);
gfarm_error_t gfm_server_flink(struct peer *, int, int);
gfarm_error_t gfm_server_mkdir(struct peer *, int, int);
gfarm_error_t gfm_server_symlink(struct peer *, int, int);
gfarm_error_t gfm_server_readlink(struct peer *, int, int);
gfarm_error_t gfm_server_getdirpath(struct peer *, int, int);
gfarm_error_t gfm_server_getdirents(struct peer *, int, int);
gfarm_error_t gfm_server_seek(struct peer *, int, int);

/* gfs from gfsd */
gfarm_error_t gfm_server_reopen(struct peer *, int, int);
gfarm_error_t gfm_server_lock(struct peer *, int, int);
gfarm_error_t gfm_server_trylock(struct peer *, int, int);
gfarm_error_t gfm_server_unlock(struct peer *, int, int);
gfarm_error_t gfm_server_lock_info(struct peer *, int, int);

/* gfs_pio from client */
gfarm_error_t gfm_server_glob(struct peer *, int, int);
gfarm_error_t gfm_server_schedule(struct peer *, int, int);
gfarm_error_t gfm_server_pio_open(struct peer *, int, int);
gfarm_error_t gfm_server_pio_set_paths(struct peer *, int, int);
gfarm_error_t gfm_server_pio_close(struct peer *, int, int);
gfarm_error_t gfm_server_pio_visit(struct peer *, int, int);

/* replica management from client */
gfarm_error_t gfm_server_replica_list_by_name(struct peer *, int, int);
gfarm_error_t gfm_server_replica_list_by_host(struct peer *, int, int);
gfarm_error_t gfm_server_replica_remove_by_host(struct peer *, int, int);

/* replica management from gfsd */
gfarm_error_t gfm_server_replica_adding(struct peer *, int, int);
gfarm_error_t gfm_server_replica_added(struct peer *, int, int);
gfarm_error_t gfm_server_replica_remove(struct peer *, int, int);
gfarm_error_t gfm_server_replica_add(struct peer *, int, int);
