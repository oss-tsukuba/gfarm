#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/module.h>
#include <asm/uaccess.h>
#include <linux/sunrpc/cache.h>
#include "gfsk_proc.h"

static int
gfarm_proc_open(struct inode *inode, struct file *file)
{
	struct gfsk_procop *op = PDE(file->f_path.dentry->d_inode)->data;
	return (op ? 0 : -EIO);
}

static int
gfarm_proc_release(struct inode *inode, struct file *file)
{
	return (0);
}

static ssize_t
gfarm_proc_read(struct file *file, char __user *buf,
				    size_t size, loff_t *_pos)
{
	int ret;
	struct gfsk_procop *op = PDE(file->f_path.dentry->d_inode)->data;
	char *kbuf;

	if (!op)
		return (-EIO);
	if (size < 1)
		return (-EINVAL);
	kbuf = kmalloc(size, GFP_KERNEL);
	if (!kbuf)
		return (-ENOMEM);

	ret = op->read(kbuf, size, *_pos, op);
	if (ret < 1) {
		kfree(kbuf);
		return (ret);
	}
	if (copy_to_user(buf, kbuf, ret) != 0) {
		kfree(kbuf);
		return (-EFAULT);
	}
	*_pos += ret;
	kfree(kbuf);
	return (ret);
}
static ssize_t
gfarm_proc_write(struct file *file, const char __user *buf,
				    size_t size, loff_t *_pos)
{
	int ret;
	struct gfsk_procop *op = PDE(file->f_path.dentry->d_inode)->data;
	char *kbuf;

	if (!op)
		return (-EIO);
	if (size < 1)
		return (-EINVAL);
	kbuf = kmalloc(size + 4, GFP_KERNEL);
	if (!kbuf)
		return (-ENOMEM);

	if (copy_from_user(kbuf, buf, size) != 0) {
		kfree(kbuf);
		return (-EFAULT);
	}
	kbuf[size] = 0;
	ret = op->write(kbuf, size, *_pos, op);
	*_pos += ret;
	kfree(kbuf);
	return (ret);
}
static ssize_t
gfarm_procvar_read(struct file *file, char __user *buf,
				    size_t size, loff_t *_pos)
{
	int ret;
	struct gfsk_procvar *var = PDE(file->f_path.dentry->d_inode)->data;
	char *kbuf;

	if (!var)
		return (-EIO);
	if (*_pos)
		return (0);
	if (size < 1)
		return (-EINVAL);
	kbuf = kmalloc(size, GFP_KERNEL);
	if (!kbuf)
		return (-ENOMEM);

	switch (var->type) {
	case GFSK_PDE_INT:
		ret = snprintf(kbuf, size, var->fmt, *(int *)var->addr);
		break;
	case GFSK_PDE_LONG:
		ret = snprintf(kbuf, size, var->fmt, *(long *)var->addr);
		break;
	case GFSK_PDE_STR:
		ret = snprintf(kbuf, size, var->fmt, var->addr);
		break;
	}
	if (ret > size)
		ret = size;
	if (ret < 0)
		ret = 0;

	if (copy_to_user(buf, kbuf, ret) != 0) {
		kfree(kbuf);
		return (-EFAULT);
	}
	*_pos += ret;
	kfree(kbuf);
	return (ret);
}
static ssize_t
gfarm_procvar_write(struct file *file, const char __user *buf,
				    size_t size, loff_t *_pos)
{
	int ret;
	struct gfsk_procvar *var = PDE(file->f_path.dentry->d_inode)->data;
	char *kbuf;

	if (!var)
		return (-EIO);
	if (size < 1)
		return (-EINVAL);
	kbuf = kmalloc(size + 4, GFP_KERNEL);
	if (!kbuf)
		return (-ENOMEM);

	if (copy_from_user(kbuf, buf, size) != 0) {
		kfree(kbuf);
		return (-EFAULT);
	}
	kbuf[size] = 0;
	ret = sscanf(kbuf, var->fmt, var->addr);

	*_pos += size;
	kfree(kbuf);
	return (ret);
}

static const struct file_operations gfarm_proc_rops = {
	.owner		= THIS_MODULE,
	.open		= gfarm_proc_open,
	.read		= gfarm_proc_read,
	.write		= NULL,
	.llseek		= no_llseek,
	.release	= gfarm_proc_release,
};
static const struct file_operations gfarm_proc_rwops = {
	.owner		= THIS_MODULE,
	.open		= gfarm_proc_open,
	.read		= gfarm_proc_read,
	.write		= gfarm_proc_write,
	.llseek		= no_llseek,
	.release	= gfarm_proc_release,
};
static const struct file_operations gfarm_procvar_rwops = {
	.owner		= THIS_MODULE,
	.open		= gfarm_proc_open,
	.read		= gfarm_procvar_read,
	.write		= gfarm_procvar_write,
	.llseek		= no_llseek,
	.release	= gfarm_proc_release,
};
struct proc_dir_entry *proc_gfarm;

void
gfarm_procop_remove(struct proc_dir_entry *parent, struct gfsk_procop *op)
{
	if (op->pde) {
		op->pde->data = NULL;
		remove_proc_entry(op->name, parent ? parent : proc_gfarm);
		op->pde = NULL;
	}
}
int
gfarm_procop_create(struct proc_dir_entry *parent, struct gfsk_procop *op)
{
	if (!(op->pde = proc_create_data(op->name, S_IFREG | op->mode,
		parent ? parent : proc_gfarm,
		 op->write ? &gfarm_proc_rwops : &gfarm_proc_rops, op))) {
		printk(KERN_WARNING "proc_create_data fail");
		return (-EINVAL);
	}
	return (0);
}
int
gfarm_procvar_create(struct proc_dir_entry *parent, struct gfsk_procvar *var,
	int num)
{
	int i;
	for (i = 0; i < num; i++, var++) {
		if (!(var->pde = proc_create_data(var->name,
			S_IFREG | var->mode, parent ? parent : proc_gfarm,
			 &gfarm_procvar_rwops, var))) {
			printk(KERN_WARNING "proc_create_data fail");
			return (-EINVAL);
		}
	}
	return (0);
}
void
gfarm_procvar_remove(struct proc_dir_entry *parent, struct gfsk_procvar *var,
	int num)
{
	int i;
	for (i = 0; i < num; i++, var++) {
		if (!var->pde)
			continue;
		remove_proc_entry(var->name, parent ? parent : proc_gfarm);
	}
}
struct proc_dir_entry *
gfarm_proc_mkdir(struct proc_dir_entry *parent, char *name)
{
	struct proc_dir_entry *pde;
	if (!(pde = proc_mkdir(name, parent ? parent : proc_gfarm))) {
		printk(KERN_WARNING "proc_mkdir fail");
	}
	return (pde);
}
void
gfarm_proc_rmdir(struct proc_dir_entry *parent, char *name)
{
	remove_proc_entry(name, parent ? parent : proc_gfarm);
}
/*
 * initialise the /proc/fs/gfarm/ directory
 */
int gfarm_proc_init(void)
{

	if (!(proc_gfarm = proc_mkdir("fs/gfarm", NULL)))
		goto error_dir;

	return (0);

error_dir:
	return (-ENOMEM);
}

/*
 * clean up the /proc/fs/gfarm/ directory
 */
void gfarm_proc_fini(void)
{
	remove_proc_entry("fs/gfarm", NULL);
}
