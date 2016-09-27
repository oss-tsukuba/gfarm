struct gfcc_param {
	int mtu;	/* mtu across network. 0: not specified */
	int gid_index;	/* not used. */
	int ib_port;	/* 0: not specified */
	int sl;		/* service level. 0: not specified */
	int qkey;	/* same value each other. 0: not specified */
	int num_rrpc; /* # expecting receive rpc. 0: not specified */
	int num_srpc; /* # sending rpc. 0: not specified */
	char devname[32];
	int tomsec;	/* timeout msec */
};
struct gfcc_ibaddr {
	uint32_t	ca_qpn;	/* from HCA */
	uint32_t	ca_psn;	/* RC: Packet Serial Number */
	struct {char raw[16]; } 	ca_gid;
	uint16_t	ca_lid;	/* from SubnetManeger */
	uint8_t		ca_sl;	/* service level within SubNet */
	uint32_t	ca_qkey;	/* UD: define AP each other */
};
struct gfcc_obj {
	gfarm_ino_t	co_ino;
	gfarm_uint64_t	co_gen;
	gfarm_off_t	co_off;
	gfarm_off_t	co_len;
};
struct gfcc_pblk {	/* data segment */
	uint64_t	cs_index;	/* page index */
	uint64_t	cs_npage;	/* # page */
};

#define GFCC_PAGES_MAX  (0x100000 / 0x1000)
struct page;
struct inode;
struct gfcc_pages {
	struct gfcc_obj cp_obj;
	int	cp_npage;
	struct inode *cp_inode;
	struct page *cp_pages[GFCC_PAGES_MAX];
};

gfarm_error_t gfarm_cc_register(struct gfcc_ibaddr *ibaddr);
gfarm_error_t gfarm_cc_find_host(struct gfcc_obj *, struct gfcc_ibaddr *);

struct gfcc_ctx;
struct gfcc_param *gfcc_param_init(void);
int gfcc_ib_init(void);
void gfcc_ib_fini(void);
int gfcc_ctx_init(struct gfcc_param *param, struct gfcc_ctx **ctxp);
void gfcc_ctx_fini(struct gfcc_ctx *ctx);
int gfsk_find_pages(struct gfcc_ctx *ctx, int npblk, struct gfcc_pblk *pblk,
			struct gfcc_pages *pages, int *npagep);
int gfsk_cc_read(struct gfcc_ctx *ctx, struct gfcc_ibaddr *ibaddr,
			struct gfcc_pages *pages, int async);
char *gfcc_report(char *buf, int size, struct gfcc_ctx *ctx);
void gfsk_validate_pages(struct gfcc_pages *pages, int npage, int invalidate);
void gfsk_release_pages(struct gfcc_pages *pages);
struct gfcc_pages *gfcc_pages_alloc(int flag);
void gfcc_pages_free(struct gfcc_pages *);
int gfsk_cc_find_host(struct gfcc_obj *obj, struct gfcc_ibaddr *ibaddr);
