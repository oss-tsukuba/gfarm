/*
 * $Id$
 */

typedef struct gfarm_dirtree gfarm_dirtree_t;

typedef struct gfarm_dirtree_entry {
	gfarm_off_t src_size;
	gfarm_off_t dst_size;
	gfarm_int64_t src_m_sec; /* mtime */
	gfarm_int64_t dst_m_sec;
	gfarm_int32_t src_m_nsec;
	gfarm_int32_t dst_m_nsec;
	gfarm_uint64_t n_pending;
	char *subpath; /* src and dst */
	char **src_copy;
	char **dst_copy;
	int src_ncopy;
	int dst_ncopy;
	int src_mode; /* 07777 */
	int src_nlink; /* XXX unused */
	unsigned char src_d_type;
	unsigned char dst_d_type;
	unsigned char dst_exist;
} gfarm_dirtree_entry_t;

gfarm_error_t gfarm_dirtree_open(gfarm_dirtree_t **, const char *,
				 const char *, int, int, int);
gfarm_error_t gfarm_dirtree_checknext(gfarm_dirtree_t *,
				       gfarm_dirtree_entry_t **);
gfarm_error_t gfarm_dirtree_next(gfarm_dirtree_t *, gfarm_dirtree_entry_t **);
void gfarm_dirtree_entry_free(gfarm_dirtree_entry_t *);
gfarm_error_t gfarm_dirtree_pending(gfarm_dirtree_t *);
gfarm_error_t gfarm_dirtree_delete(gfarm_dirtree_t *);
gfarm_error_t gfarm_dirtree_close(gfarm_dirtree_t *);
gfarm_error_t gfarm_dirtree_array(gfarm_dirtree_t *, int *,
				  gfarm_dirtree_entry_t ***);
void gfarm_dirtree_array_free(int, gfarm_dirtree_entry_t **);
