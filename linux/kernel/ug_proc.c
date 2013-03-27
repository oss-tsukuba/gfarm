#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/module.h>
#include <asm/uaccess.h>
#include <linux/sunrpc/cache.h>
#include "ug_idmap.h"
#include <netdb.h>

static int
ug_idmap_test_open(struct inode *inode, struct file *file)
{
	return (0);
}

static int
ug_idmap_test_release(struct inode *inode, struct file *file)
{
	return (0);
}

static ssize_t
ug_idmap_test_read(struct file *file, char __user *buf,
				    size_t size, loff_t *_pos)
{
	static const char msg[] = "enter name or id\n";
	static int mlen = sizeof(msg);

	if (size <= 1)
		return (-EINVAL);
	if (*_pos > mlen)
		return (0);

	if (size < mlen)
		mlen = size;

	if (copy_to_user(buf, msg, mlen) != 0)
		return (-EFAULT);

	*_pos = mlen;
	return (size);
}

static ssize_t
ug_idmap_test_name2uid(struct file *file, const char __user *buf,
				    size_t size, loff_t *_pos)
{
	int ret, len;
	char kbuf[UG_IDMAP_NAMESZ], name[UG_IDMAP_NAMESZ], *bufp = kbuf;
	__u32 id;

	if (size <= 1 || size >= UG_IDMAP_NAMESZ)
		return (-EINVAL);

	if (copy_from_user(kbuf, buf, size) != 0)
		return (-EFAULT);

	if ((len = qword_get(&bufp, name, sizeof(name))) <= 0) {
		printk(KERN_DEBUG "%s:qword_get\n", __func__);
		return (-EINVAL);
	}
	if ((ret = ug_map_name_to_uid(name, len, &id)) < 0) {
		printk(KERN_DEBUG "%s:ug_map_name_to_uid\n", __func__);
		return (ret);
	}
	printk(KERN_INFO "%s:name=%s id=%d\n", __func__, name, id);
	*_pos += bufp - kbuf;
	return (bufp - kbuf);
}

static ssize_t
ug_idmap_test_uid2name(struct file *file, const char __user *buf,
				    size_t size, loff_t *_pos)
{
	int ret, len;
	char kbuf[UG_IDMAP_NAMESZ], name[UG_IDMAP_NAMESZ], *bufp = kbuf, *bp;
	__u32 id;

	if (size <= 1 || size >= UG_IDMAP_NAMESZ)
		return (-EINVAL);

	if (copy_from_user(kbuf, buf, size) != 0)
		return (-EFAULT);

	if ((len = qword_get(&bufp, name, sizeof(name))) <= 0) {
		printk(KERN_DEBUG "%s:qword_get\n", __func__);
		return (-EINVAL);
	}
	id = simple_strtoul(name, &bp, 10);
	if (name == bp) {
		printk(KERN_DEBUG "%s:simple_strtoul\n", __func__);
		return (-EINVAL);
	}

	if ((ret = ug_map_uid_to_name(id, name, sizeof(name))) < 0) {
		printk(KERN_DEBUG "%s:ug_map_uid_to_name\n", __func__);
		return (ret);
	}
	printk(KERN_INFO "%s:name=%s id=%d\n", __func__, name, id);
	*_pos += bufp - kbuf;
	return (bufp - kbuf);
}
static ssize_t
ug_idmap_test_hostaddr(struct file *file, const char __user *buf,
				    size_t size, loff_t *_pos)
{
	int ret, len;
	char kbuf[UG_IDMAP_NAMESZ], name[UG_IDMAP_NAMESZ], *bufp = kbuf;
	struct hostent *ent;
	struct addrinfo *info, *ai;
	int i;

	if (size <= 1 || size >= UG_IDMAP_NAMESZ)
		return (-EINVAL);

	if (copy_from_user(kbuf, buf, size) != 0)
		return (-EFAULT);

	if ((len = qword_get(&bufp, name, sizeof(name))) <= 0) {
		printk(KERN_WARNING "%s:qword_get\n", __func__);
		return (-EINVAL);
	}

	if (!(ent = gethostbyname(name))) {
		printk(KERN_WARNING "%s:gethostbyname\n", __func__);
		return (-ENOENT);
	}
	for (i = 0; ent->h_aliases[i]; i++) {
		printk(KERN_INFO "%s:name=%s alias=%s\n", __func__, name,
				ent->h_aliases[i]);
	}
	for (i = 0; ent->h_addr_list[i]; i++) {
		printk(KERN_INFO "%s:name=%s addr=0x%x\n", __func__, name,
				*(int *)ent->h_addr_list[i]);
	}
	free_gethost_buff(ent);
	if ((ret = getaddrinfo(name, "1234", NULL, &info))) {
		printk(KERN_WARNING "%s:getaddrinfo\n", __func__);
		return (ret);
	}
	for (ai = info; ai; ai = ai->ai_next) {
		struct sockaddr_in *in = (struct sockaddr_in *)ai->ai_addr;
		printk(KERN_INFO "%s:flags=0x%x family=%d type=%d proto=%d"
			" addrlen=%d family=%d port=%d addr=0x%x name=%s\n",
			__func__,
			ai->ai_flags, ai->ai_family, ai->ai_socktype,
			ai->ai_protocol, ai->ai_addrlen,
			in->sin_family, in->sin_port, in->sin_addr.s_addr,
			ai->ai_canonname ? ai->ai_canonname : "");
	}
	freeaddrinfo(info);

	*_pos += bufp - kbuf;
	return (bufp - kbuf);
}

static const struct file_operations ug_idmap_uid2name_fops = {
	.owner		= THIS_MODULE,
	.open		= ug_idmap_test_open,
	.read		= ug_idmap_test_read,
	.write		= ug_idmap_test_uid2name,
	.llseek		= no_llseek,
	.release	= ug_idmap_test_release,
};

static const struct file_operations ug_idmap_name2uid_fops = {
	.owner		= THIS_MODULE,
	.open		= ug_idmap_test_open,
	.read		= ug_idmap_test_read,
	.write		= ug_idmap_test_name2uid,
	.llseek		= no_llseek,
	.release	= ug_idmap_test_release,
};

static const struct file_operations ug_idmap_hostaddr_fops = {
	.owner		= THIS_MODULE,
	.open		= ug_idmap_test_open,
	.read		= ug_idmap_test_read,
	.write		= ug_idmap_test_hostaddr,
	.llseek		= no_llseek,
	.release	= ug_idmap_test_release,
};

/*
 * initialise the /proc/fs/ug_idmap/ directory
 */
int __init ug_idmap_proc_init(void)
{

	if (!proc_mkdir("fs/ug_idmap", NULL))
		goto error_dir;

	if (!proc_create("fs/ug_idmap/uid2name", S_IFREG | 0666, NULL,
			 &ug_idmap_uid2name_fops))
		goto error_uid2name;

	if (!proc_create("fs/ug_idmap/name2uid", S_IFREG | 0666, NULL,
			 &ug_idmap_name2uid_fops))
		goto error_name2uid;

	if (!proc_create("fs/ug_idmap/hostaddr", S_IFREG | 0666, NULL,
			 &ug_idmap_hostaddr_fops))
		goto error_hostaddr;

	return (0);

error_hostaddr:
	remove_proc_entry("fs/ug_idmap/name2uid", NULL);
error_name2uid:
	remove_proc_entry("fs/ug_idmap/uid2name", NULL);
error_uid2name:
	remove_proc_entry("fs/ug_idmap", NULL);
error_dir:
	return (-ENOMEM);
}

/*
 * clean up the /proc/fs/ug_idmap/ directory
 */
void ug_idmap_proc_cleanup(void)
{
	remove_proc_entry("fs/ug_idmap/name2uid", NULL);
	remove_proc_entry("fs/ug_idmap/uid2name", NULL);
	remove_proc_entry("fs/ug_idmap/hostaddr", NULL);
	remove_proc_entry("fs/ug_idmap", NULL);
}
