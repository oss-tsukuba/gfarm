struct abstract_host_ops {
	/* downcast functions */
	struct host *(*abstract_host_to_host)(struct abstract_host *);
	struct mdhost *(*abstract_host_to_mdhost)(struct abstract_host *);

	const char *(*get_name)(struct abstract_host *);
	int (*get_port)(struct abstract_host *);
	void (*set_peer_locked)(struct abstract_host *, struct peer *);
	void (*set_peer_unlocked)(struct abstract_host *, struct peer *);
	void (*unset_peer)(struct abstract_host *, struct peer *);
	void (*disable)(struct abstract_host *);
	void (*disabled)(struct abstract_host *, struct peer *);
};

/* common struct of host and mdhost */
struct abstract_host {
	struct abstract_host_ops *ops;

	int invalid;	/* set when deleted */

	pthread_mutex_t mutex;
	/*
	 * resources which are protected by the abstrac_host::mutex
	 */
	pthread_cond_t ready_to_send, ready_to_receive;

	int can_send, can_receive;

	struct peer *peer;
	int protocol_version;
	int is_active;

	struct netsendq *sendq;
};

struct netsendq_manager;
gfarm_error_t abstract_host_init(
	struct abstract_host *, struct abstract_host_ops *,
	struct netsendq_manager *, const char *);
