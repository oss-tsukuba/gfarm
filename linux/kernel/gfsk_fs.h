#ifndef _GFSK_FS_H_
#define _GFSK_FS_H_
#include <gfsk_if.h>
#include "gfsk.h"
#include <linux/fs.h>
#include <linux/backing-dev.h>

struct gfarm_context;
struct gfsk_fdstruct;
struct gfskdev_conn;
struct semaphore;
struct gfcc_ctx;
struct proc_dir_entry;

struct gfsk_fs_context{
	struct super_block	*gf_sb;
	struct backing_dev_info gf_bdi;
	struct gfsk_mount_data	gf_mdata;
	int	gf_actime;	/* jiffies: attribute cache timeout period */
	int	gf_pctime;	/* jiffies: page cache timeout period */
	int	gf_d_delete;
	int	gf_dirpage;
	int	gf_readahead;
	int	gf_ra_async;
	int	gf_mnt_id;
	char	gf_mnt_name[32];
	struct proc_dir_entry	*gf_pde;
	struct gfarm_context	*gf_gfarm_ctxp;
	struct gfsk_fdstruct	*gf_fdstructp;
	struct gfcc_ctx		*gf_cc_ctxp;
	struct gfskdev_conn	*gf_dc;
	struct mutex		gf_lock;
	struct list_head	gf_locallist;	/* mmaped local file list */
};

int gfsk_gfarm_init(uid_t uid);
void gfsk_gfarm_fini(void);

struct vfsmount;
struct gfsk_mount_arg {
	struct gfsk_mount_data *data;
	struct vfsmount *mnt;
};
int gfsk_client_mount(struct super_block *sb, void *arg);
void gfsk_client_unmount(void);
void gfsk_client_fini(void);

void gfsk_local_map_fini(struct gfsk_fs_context *fsp);

int gfsk_fdset_init(void);
void gfsk_fdset_umount(void);
void gfsk_fdset_fini(void);
int gfsk_fd_file_set(struct file *file);
int gfsk_fd_unset(int fd);
int gfsk_fd2file(int fd, struct file **res);
int gfsk_fd_set(int taskfd, int type);
int gfsk_localfd_set(int taskfd, int type);

int gfsk_dev_init(void);
void gfsk_dev_fini(void);
int gfsk_conn_init(int fd);
void gfsk_conn_umount(int fd);
void gfsk_conn_fini(void);
int gfsk_conn_fd_set(int fd);
int gfsk_req_term(void);
int gfsk_req_connect_sync(int cmd, uid_t uid, struct gfsk_req_connect *inarg,
				struct gfsk_rpl_connect *outarg);
int gfsk_req_connect_async(int cmd, uid_t uid, struct gfsk_req_connect *inarg,
	struct gfsk_rpl_connect *outarg, void **kevpp, int evfd);


#define GFSK_CTX_DECLARE	\
	struct  gfsk_task_context *__task_contextp = NULL, __task_context

#define GFSK_CTX_DECLARE_ZERO	\
 GFSK_CTX_DECLARE = {	\
 .gk_fs_ctxp = NULL,	\
 .gk_gfarm_ctxp = NULL,	\
 .gk_uname[0] = 0, \
 .gk_errno = 0}

#define GFSK_CTX_DECLARE_FSP(fsp)	\
 GFSK_CTX_DECLARE = {	\
 .gk_fs_ctxp = (fsp), \
 .gk_gfarm_ctxp = (fsp)->gf_gfarm_ctxp, \
 .gk_uname[0] = 0, \
 .gk_errno = 0}

#define GFSK_CTX_DECLARE_SB(sb)	\
	GFSK_CTX_DECLARE_FSP((struct gfsk_fs_context *)(sb)->s_fs_info)
#define GFSK_CTX_DECLARE_FILE(file) GFSK_CTX_DECLARE_SB((file)->f_dentry->d_sb)
#define GFSK_CTX_DECLARE_INODE(inode)	GFSK_CTX_DECLARE_SB((inode)->i_sb)
#define GFSK_CTX_DECLARE_DENTRY(dentry) GFSK_CTX_DECLARE_SB((dentry)->d_sb)

#define GFSK_CTX_DECLARE_SAVE \
	struct  gfsk_task_context *__task_contextp = current->journal_info

#define	GFSK_CTX_SET()	do {	\
	if (!gfsk_task_ctxp)		\
		current->journal_info = __task_contextp = &__task_context; \
	} while (0)

#define	GFSK_CTX_UNSET()	do {	\
	if (__task_contextp)			\
		current->journal_info = __task_contextp = NULL; \
	} while (0)

#define	GFSK_CTX_SET_FORCE()	do {	\
		__task_contextp = gfsk_task_ctxp;	\
		current->journal_info = &__task_context; \
	} while (0)

#define	GFSK_CTX_UNSET_FORCE()	do {	\
		current->journal_info = __task_contextp;	\
	} while (0)

#define GFSK_CTX_CLEAR() 	\
	do { current->journal_info = NULL; } while (0)
#define GFSK_CTX_RESTORE()	\
	do { current->journal_info = __task_contextp; } while (0)

/*---------------------------------------------------------*/
#include <linux/stat.h>
#define GFSK_INODE_STALE	1
#define GFSK_INODE_MAYSTALE	2
struct gfarm_inode {
	struct inode inode;
	struct mutex	i_mutex;
	uint64_t	i_gen;
	/* jiffies: limit time of file attributes are valid */
	uint64_t	i_actime;
	/* jiffies: limit time of file page caches are valid */
	uint64_t	i_pctime;
	struct list_head i_openfile;	/* open file list */
	int		i_wopencnt;	/* write open count */
	uint64_t	i_iflag;
};

extern struct inode *gfsk_get_inode(struct super_block *sb, int mode,
			unsigned long ino, u64 gen, int new);
extern void gfsk_invalidate_dir_pages(struct inode *inode);
extern struct inode *gfsk_find_inode(struct super_block *sb, int mode,
			unsigned long ino, u64 gen);


#define GFARM_ROOT_INO	2
#define GFARM_ROOT_IGEN	0

extern const struct address_space_operations gfarm_file_aops;
extern const struct file_operations gfarm_file_operations;
extern const struct inode_operations gfarm_file_inode_operations;

extern const struct inode_operations gfarm_dir_inode_operations;
extern const struct file_operations gfarm_dir_operations;

extern unsigned long gfsk_gettv(struct timespec *ts);


static inline struct gfarm_inode *get_gfarm_inode(struct inode *inode)
{
	return (container_of(inode, struct gfarm_inode, inode));
}
static inline void gfsk_iflag_set(struct inode *inode, int flag)
{
	struct gfarm_inode *gi = get_gfarm_inode(inode);
	if (!(gi->i_iflag & flag))
		gflog_info(GFARM_MSG_UNFIXED, "staled. "
		"ino=%lu, gen=%lu", inode->i_ino, (long unsigned)gi->i_gen);
	gi->i_iflag |= flag;
}
static inline int gfsk_iflag_isset(struct inode *inode, int flag)
{
	struct gfarm_inode *gi = get_gfarm_inode(inode);
	return (gi->i_iflag & flag);
}
static inline void gfsk_iflag_unset(struct inode *inode, int flag)
{
	struct gfarm_inode *gi = get_gfarm_inode(inode);
	gi->i_iflag &= ~(flag);
}


extern int ug_map_name_to_uid(const char *name, size_t namelen, __u32 *id);
extern int ug_map_name_to_gid(const char *name, size_t namelen, __u32 *id);
extern int ug_map_uid_to_name(__u32 id, char *name, size_t namelen);
extern int ug_map_gid_to_name(__u32 id, char *name, size_t namelen);

#define gflog_verbose(msg_no, ...)
#define gflog_verbosex  gflog_debug


#endif /* _GFSK_FS_H_ */
