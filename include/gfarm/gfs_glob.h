typedef struct {
	unsigned char *array;
	int length, size;
} gfs_glob_t;

#define GFS_GLOB_ARRAY(glob)		(glob).array
#define GFS_GLOB_ELEM(glob, i)		(glob).array[i]
#define gfs_glob_length(glob)		(glob)->length
#define gfs_glob_elem(glob, i) \
	GFS_GLOB_ELEM(*(glob), i)

char *gfs_glob(const char *, gfarm_stringlist *, gfs_glob_t *);
char *gfs_glob_init(gfs_glob_t *);
void gfs_glob_free(gfs_glob_t *);
char *gfs_glob_add(gfs_glob_t *, int);
