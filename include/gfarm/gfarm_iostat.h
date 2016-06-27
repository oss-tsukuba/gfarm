#define GFARM_IOSTAT_MAGIC	0x53544132
#define GFARM_IOSTAT_NAME_MAX	30
#define GFARM_IOSTAT_TYPE_TOTAL	1
#define GFARM_IOSTAT_TYPE_CURRENT	2

#define GFARM_IOSTAT_TRAN_NUM	0
#define GFARM_IOSTAT_TRAN_NITEM	1

#define GFARM_IOSTAT_IO_RCOUNT	0
#define GFARM_IOSTAT_IO_WCOUNT	1
#define GFARM_IOSTAT_IO_RBYTES	2
#define GFARM_IOSTAT_IO_WBYTES	3
#define GFARM_IOSTAT_IO_NITEM	4

struct gfarm_iostat_head {
	unsigned int	s_magic;	/* GFARM_IOSTAT_MAGIC */
	unsigned int	s_nitem;
	unsigned int	s_row;		/* row size */
	unsigned int	s_rowcur;	/* maximum valid # row */
	unsigned int	s_rowmax;	/* maximum # row */
	unsigned int	s_item_size;
	unsigned int	s_ncolumn;
	unsigned int	s_dummy;
	gfarm_uint64_t	s_start_sec;
	gfarm_uint64_t	s_update_sec;
	gfarm_uint64_t	s_item_off;
	char s_name[GFARM_IOSTAT_NAME_MAX + 2];
};
struct gfarm_iostat_spec {
	char s_name[GFARM_IOSTAT_NAME_MAX + 1];
	char s_type;	/* GFARM_IOSTAT_TYPE_xxx */
};
struct gfarm_iostat_items {
	gfarm_uint64_t	s_valid;
	gfarm_int64_t	s_vals[1];	/* [s_nitem] */
};
