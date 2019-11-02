#include <linux/module.h>
#include <linux/seq_file.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>

#define CC_CTL_VERSION "2.0"
#define CC_CTL_NODE "cc_ctl"
#define CC_IOC_MAGIC 'c'
#define CC_IOC_COMMAND _IOWR(CC_IOC_MAGIC, 0, struct cc_command)
#define CC_IOC_MAX 1

static int cc_ctl_show(struct seq_file *m, void *v)
{
	seq_printf(m, "control center version: %s\n", CC_CTL_VERSION);
	return 0;
}

static int cc_ctl_open(struct inode *ip, struct file *fp)
{
	return single_open(fp, cc_ctl_show, NULL);
}

static int cc_ctl_close(struct inode *ip, struct file *fp)
{
	return 0;
}

static long cc_ctl_ioctl(struct file *file, unsigned int cmd, unsigned long __user arg)
{
	if (_IOC_TYPE(cmd) != CC_IOC_MAGIC) return -ENOTTY;
	if (_IOC_NR(cmd) > CC_IOC_MAX) return -ENOTTY;

	return 0;
}

static const struct file_operations cc_ctl_fops = {
	.owner = THIS_MODULE,
	.open = cc_ctl_open,
	.release = cc_ctl_close,
	.unlocked_ioctl = cc_ctl_ioctl,
	.compat_ioctl = cc_ctl_ioctl,

	.read = seq_read,
	.llseek = seq_lseek,
};

static struct device *class_dev;
static struct class *driver_class;
static struct cdev cdev;
static dev_t cc_ctl_dev;
static int __init cc_init(void)
{
	int rc;

	rc = alloc_chrdev_region(&cc_ctl_dev, 0, 1, CC_CTL_NODE);
	if (rc < 0) {
		pr_err("alloc_chrdev_region failed %d\n", rc);
		return 0;
	}

	driver_class = class_create(THIS_MODULE, CC_CTL_NODE);
	if (IS_ERR(driver_class)) {
		rc = -ENOMEM;
		pr_err("class_create failed %d\n", rc);
		goto exit_unreg_chrdev_region;
	}

	class_dev = device_create(driver_class, NULL, cc_ctl_dev, NULL, CC_CTL_NODE);
	if (IS_ERR(class_dev)) {
		pr_err("class_device_create failed %d\n", rc);
		rc = -ENOMEM;
		goto exit_destroy_class;
	}

	cdev_init(&cdev, &cc_ctl_fops);
	cdev.owner = THIS_MODULE;
	rc = cdev_add(&cdev, MKDEV(MAJOR(cc_ctl_dev), 0), 1);
	if (rc < 0) {
		pr_err("cdev_add failed %d\n", rc);
		goto exit_destroy_device;
	}

	return 0;

exit_destroy_device:
	device_destroy(driver_class, cc_ctl_dev);
exit_destroy_class:
	class_destroy(driver_class);
exit_unreg_chrdev_region:
	unregister_chrdev_region(cc_ctl_dev, 1);

	return 0;
}

pure_initcall(cc_init);
