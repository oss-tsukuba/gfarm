#include <gfarm/gflog.h>
#include <linux/slab.h>
#include <linux/file.h>
#include <linux/fs.h>
#include "gfsk_fs.h"

#define FILE2INODE(file)	((file)->f_path.dentry->d_inode)
/*
 *  set file descriptor of current task into the file table of currrent fs
 *  return index of file table, otherwise error.
 */
int
gfsk_fd_set(int usrfd, int type)
{
	int	err = -EBADF;
	struct file *file;

	if (!(file = fget(usrfd))) {
		gflog_error(0, "invalid fd=%d\n", usrfd);
		goto out;
	}
	if (type && ((FILE2INODE(file)->i_mode) & S_IFMT) != type) {
		gflog_error(0, "invalid type=%x:%x\n",
			FILE2INODE(file)->i_mode, type);
		goto out_fput;
	}
	if ((err = gfsk_fd_file_set(file)) < 0) {
		gflog_error(0, "fail gfsk_fd_file_set %d\n", err);
		goto out_fput;
	}
out_fput:
	fput(file);
out:
	return (err);
}

/*
 *  request helper daemon to connect server as user
 *  return index of file table of this fs.
 */
int
gfsk_client_connect(const char *hostname, int port, const char *source_ip,
		const char *user, int *sock)
{
	int	err = -EINVAL;
	struct gfsk_req_connect inarg;
	struct gfsk_rpl_connect outarg;

	*sock = -1;
	if (strlen(hostname) >= sizeof(inarg.r_hostname)) {
		gflog_error(0, "hostname(%s) too long\n", hostname);
		goto out;
	}
	if (gfsk_fsp->gf_mdata.m_mfd >= 0) {
		if (!strcmp(hostname, gfsk_fsp->gf_mdata.m_host)
			&& !strcmp(user, gfsk_fsp->gf_mdata.m_uidname)) {
			*sock = gfsk_fsp->gf_mdata.m_mfd;
			gfsk_fsp->gf_mdata.m_mfd = -1;
			err = 0;
			goto out;
		}
		gflog_debug(0, "(host,user)=(%s,%s) != mount.arg(%s,%s)",
			hostname, user, gfsk_fsp->gf_mdata.m_host,
			gfsk_fsp->gf_mdata.m_uidname);
	}
	if (strlen(user) >= sizeof(inarg.r_global)) {
		gflog_error(0, "user(%s) too long\n", user);
		goto out;
	}
	if (ug_map_name_to_uid(user, strlen(user), &inarg.r_uid)) {
		gflog_error(0, "invalid user(%s)\n", user);
		goto out;
	}
	if (source_ip && strlen(source_ip) >= sizeof(inarg.r_source_ip)) {
		gflog_error(0, "source_ip(%s) too long\n", source_ip);
		goto out;
	}
	strcpy(inarg.r_hostname, hostname);
	inarg.r_port = port;
	if (source_ip)
		strcpy(inarg.r_source_ip, source_ip);
	strcpy(inarg.r_global, user);
	if ((err = gfsk_req_connectmd(inarg.r_uid, &inarg, &outarg))) {
		gflog_error(0, "connect fail err=%d\n", err);
		goto out;
	}
	/* fd is converted into fsfd from taskfd */
	*sock = outarg.r_fd;
out:
	return (err);
}

int
gfsk_client_mount(struct super_block *sb, void *arg)
{
	struct gfsk_mount_data	*mdatap = (struct gfsk_mount_data *) arg;
	struct gfsk_fs_context	*fsp;
	int	i, err;

	if (mdatap->m_version != GFSK_VER) {
		gflog_error(GFARM_MSG_UNFIXED,
			"version is expected %x, but %x", GFSK_VER,
				mdatap->m_version);
		err = -EINVAL;
		goto out;
	}
	if (!(fsp = kmalloc(sizeof(*fsp), GFP_KERNEL))) {
		gflog_error(GFARM_MSG_UNFIXED,
			"can't alloc gfsk_fs_context %ld", sizeof(*fsp));
		err = -ENOMEM;
		goto out;
	}
	memset(fsp, 0, sizeof(*fsp));
	fsp->gf_sb = sb;
	sb->s_fs_info = fsp;
	gfsk_fsp = fsp;

	gfsk_fsp->gf_mdata.m_version = mdatap->m_version;
	gfsk_fsp->gf_mdata.m_mfd = gfsk_fsp->gf_mdata.m_dfd = -1;

	gfsk_fsp->gf_mdata.m_uid = mdatap->m_uid;
	ug_map_uid_to_name(mdatap->m_uid, gfsk_fsp->gf_mdata.m_uidname,
		sizeof(gfsk_fsp->gf_mdata.m_uidname));

	if ((err = gfsk_fdset_init())) {
		goto out;
	}

	err = -ENOMEM;
	for (i = 0; i < GFSK_FBUF_MAX; i++) {
		struct gfsk_fbuf *fbp = &mdatap->m_fbuf[i];
		struct gfsk_fbuf *tbp = &gfsk_fsp->gf_mdata.m_fbuf[i];
		if (!fbp->f_name.d_len)
			continue;
		if (!(tbp->f_name.d_buf = memdup_user(fbp->f_name.d_buf,
			fbp->f_name.d_len))) {
			gflog_error(GFARM_MSG_UNFIXED,
				"Can't allocate for %i name, size %d",
					i, fbp->f_name.d_len);
			goto out;
		}
		tbp->f_name.d_len = fbp->f_name.d_len;
		if (!(tbp->f_buf.d_buf = memdup_user(fbp->f_buf.d_buf,
			fbp->f_buf.d_len))) {
			gflog_error(GFARM_MSG_UNFIXED,
				"Can't allocate for %i name, size %d",
					i, fbp->f_buf.d_len);
			goto out;
		}
		tbp->f_buf.d_len = fbp->f_buf.d_len;
	}

	err = -EINVAL;
	if (mdatap->m_mfd < 0
		|| (err = gfsk_fd_set(mdatap->m_mfd, S_IFSOCK)) < 0) {
		gflog_error(GFARM_MSG_UNFIXED,
			"invalid m_mfd '%d'", mdatap->m_mfd);
		goto out;
	}
	gfsk_fsp->gf_mdata.m_mfd = err;
	memcpy(gfsk_fsp->gf_mdata.m_host, mdatap->m_host,
				sizeof(mdatap->m_host));
	err = -EINVAL;
	if (mdatap->m_dfd < 0
		|| (err = gfsk_fd_set(mdatap->m_dfd, S_IFCHR)) < 0) {
		gflog_error(GFARM_MSG_UNFIXED,
			"invalid m_dfd '%d'", mdatap->m_dfd);
		goto out;
	}
	gfsk_fsp->gf_mdata.m_dfd = err;

	if ((err = gfsk_conn_init(err))) {
		gflog_error(GFARM_MSG_UNFIXED,
			"conn_init fail '%d'", mdatap->m_dfd);
		goto out;
	}
	if ((err = gfsk_gfarm_init(mdatap->m_uid))) {
		goto out;
	}
	if (!fsp->gf_gfarm_ctxp && gfsk_task_ctxp &&
			gfsk_task_ctxp->gk_gfarm_ctxp)
		fsp->gf_gfarm_ctxp = gfsk_task_ctxp->gk_gfarm_ctxp;
out:
	return (err);
}

void
gfsk_client_unmount(void)
{
	struct gfsk_mount_data *mdatap;
	int	i;

	if (!gfsk_task_ctxp || !gfsk_fsp)
		return;

	mdatap = &gfsk_fsp->gf_mdata;

	if (mdatap->m_version <= 0)
		return;
	mdatap->m_version = -1;

	gfsk_gfarm_fini();
	gfsk_fsp->gf_gfarm_ctxp = NULL;


	if (mdatap->m_dfd >= 0) {
		gfsk_conn_umount(mdatap->m_dfd);
		gfsk_fd_unset(mdatap->m_dfd);
		mdatap->m_dfd = -1;
	}
	if (mdatap->m_mfd >= 0) {
		gfsk_fd_unset(mdatap->m_mfd);
		mdatap->m_mfd = -1;
	}
	for (i = 0; i < GFSK_FBUF_MAX; i++) {
		struct gfsk_fbuf *tbp = &gfsk_fsp->gf_mdata.m_fbuf[i];
		if (!tbp->f_name.d_len)
			continue;
		kfree(tbp->f_name.d_buf);
		tbp->f_name.d_len = 0;
		if (!tbp->f_buf.d_len)
			continue;
		kfree(tbp->f_buf.d_buf);
		tbp->f_buf.d_len = 0;
	}
	gfsk_fdset_umount();
}

void
gfsk_client_fini(void)
{
	struct gfsk_mount_data *mdatap;

	if (!gfsk_task_ctxp || !gfsk_fsp)
		return;

	mdatap = &gfsk_fsp->gf_mdata;
	if (!mdatap->m_version)
		return;
	if (mdatap->m_version > 0) {
		gfsk_client_unmount();
	}
	mdatap->m_version = 0;

	gfsk_conn_fini();
	gfsk_fdset_fini();
}
