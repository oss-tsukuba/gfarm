#ifndef _GFSK_FS_H_
#define _GFSK_FS_H_
#include <gfsk_if.h>
#include "gfsk.h"
#include <linux/fs.h>

struct gfarm_context;
struct gfsk_fdstruct;
struct fuse_conn;

struct gfsk_fs_context{
	struct super_block	*gf_sb;
	struct gfsk_mount_data	gf_mdata;
	struct gfarm_context	*gf_gfarm_ctxp;
	struct gfsk_fdstruct	*gf_fdstructp;
	struct fuse_conn	*gf_fc;
};

int gfsk_gfarm_init(uid_t uid);
void gfsk_gfarm_fini(void);

int gfsk_client_mount(struct super_block *sb, void *arg);
void gfsk_client_unmount(void);
void gfsk_client_fini(void);

int gfsk_fdset_init(void);
void gfsk_fdset_umount(void);
void gfsk_fdset_fini(void);
int gfsk_fd_file_set(struct file *file);
int gfsk_fd_unset(int fd);
int gfsk_fd2file(int fd, struct file **res);
int gfsk_fd_set(int taskfd, int type);


int gfsk_dev_init(void);
void gfsk_dev_fini(void);
int gfsk_conn_init(int fd);
void gfsk_conn_umount(int fd);
void gfsk_conn_fini(void);
int gfsk_conn_fd_set(int fd);
int gfsk_req_term(void);
int gfsk_req_connectmd(uid_t uid, struct gfsk_req_connect *inarg,
				struct gfsk_rpl_connect *outarg);


#define GFSK_CTX_DECLARE	\
	struct  gfsk_task_context *__task_contextp = NULL, __task_context

#define GFSK_CTX_DECLARE_ZERO	\
 GFSK_CTX_DECLARE = {	\
 .gk_fs_ctxp = NULL,	\
 .gk_gfarm_ctxp = NULL,	\
 .gk_uname[0] = 0, \
 .gk_errno = 0}

#define GFSK_CTX_DECLARE_SB(sb)	\
 GFSK_CTX_DECLARE = {	\
 .gk_fs_ctxp = (struct gfsk_fs_context *)(sb)->s_fs_info,	\
 .gk_gfarm_ctxp = ((struct gfsk_fs_context *)(sb)->s_fs_info)->gf_gfarm_ctxp, \
 .gk_uname[0] = 0, \
 .gk_errno = 0}

#define GFSK_CTX_DECLARE_FILE(file) GFSK_CTX_DECLARE_SB((file)->f_dentry->d_sb)
#define GFSK_CTX_DECLARE_INODE(inode)	GFSK_CTX_DECLARE_SB((inode)->i_sb)
#define GFSK_CTX_DECLARE_DENTRY(dentry) GFSK_CTX_DECLARE_SB((dentry)->d_sb)

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

/*---------------------------------------------------------*/
#include <linux/stat.h>
struct gfarm_inode {
	struct inode inode;

	uint64_t	i_gen;
	uint64_t	i_ncopy;
	loff_t		i_direntsize;
};

extern struct inode *gfsk_get_inode(struct super_block *sb, int mode,
			unsigned long ino, u64 gen);
extern int gfsk_make_node(struct inode *dir, struct dentry *dentry, int mode,
		unsigned long ino, u64 gen);
extern void invalidate_dir_pages(struct inode *inode);

#define GFARM_ROOT_INO	2
#define GFARM_ROOT_IGEN	0

extern const struct address_space_operations gfarm_file_aops;
extern const struct file_operations gfarm_file_operations;
extern const struct inode_operations gfarm_file_inode_operations;

extern const struct address_space_operations gfarm_dir_aops;
extern const struct inode_operations gfarm_dir_inode_operations;
extern const struct file_operations gfarm_dir_operations;



static inline struct gfarm_inode *get_gfarm_inode(struct inode *inode)
{
	return (container_of(inode, struct gfarm_inode, inode));
}


extern int ug_map_name_to_uid(const char *name, size_t namelen, __u32 *id);
extern int ug_map_name_to_gid(const char *name, size_t namelen, __u32 *id);
extern int ug_map_uid_to_name(__u32 id, char *name, size_t namelen);
extern int ug_map_gid_to_name(__u32 id, char *name, size_t namelen);

#endif /* _GFSK_FS_H_ */
