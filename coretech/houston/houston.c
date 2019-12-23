#include <linux/module.h>
#include <linux/seq_file.h>
#include <linux/cdev.h>
#include <linux/poll.h>
#include <linux/uaccess.h>
#include <linux/sched.h>
#include <linux/sched/cputime.h>
#include <linux/sched/task.h>

#define HT_CTL_NODE "ht_ctl"

// ioctl
#define HT_IOC_MAGIC 'k'
//#define HT_IOC_COLLECT _IOR(HT_IOC_MAGIC, 0, struct ai_parcel)
#define HT_IOC_SCHEDSTAT _IOWR(HT_IOC_MAGIC, 1, u64)
//#define HT_IOC_CPU_LOAD _IOWR(HT_IOC_MAGIC, 2, struct cpuload)
//#define HT_IOC_MAX 2

static int ais_enable = 0;
module_param(ais_enable, int, 0664);

static int ht_enable = 0;
module_param(ht_enable, int, 0664);

static int perf_ready = -1;
module_param(perf_ready, int, 0664);

static int sample_rate_ms = 3000;
module_param(sample_rate_ms, int, 0664);

static bool fps_boost_enable = true;
module_param(fps_boost_enable, bool, 0664);

static int null_store(const char *buf, const struct kernel_param *kp)
{
	return 0;
}

static struct kernel_param_ops null_store_ops = {
	.set = null_store,
};

module_param_cb(fps_boost, &null_store_ops, NULL, 0220);
module_param_cb(fps_data_sync, &null_store_ops, NULL, 0220);
module_param_cb(reset, &null_store_ops, NULL, 0664);

static unsigned int ht_ctl_poll(struct file *fp, poll_table *wait)
{
	return POLLIN;
}

static int ht_ctl_show(struct seq_file *m, void *v)
{
	seq_printf(m, "\n");
	return 0;
}

static int ht_ctl_open(struct inode *ip, struct file *fp)
{
	return single_open(fp, ht_ctl_show, NULL);
}

static int ht_ctl_close(struct inode *ip, struct file *fp)
{
	return 0;
}

static long ht_ctl_ioctl(struct file *file, unsigned int cmd, unsigned long __user arg)
{
	if (likely(cmd == HT_IOC_SCHEDSTAT)) {
		struct task_struct *task;
		u64 exec_ns, pid;

		if (copy_from_user(&pid, (u64 *) arg, sizeof(u64)))
			return 0;

		rcu_read_lock();
		task = find_task_by_vpid(pid);
		if (likely(task)) {
			get_task_struct(task);
			rcu_read_unlock();
			exec_ns = task_sched_runtime(task);
			put_task_struct(task);
		} else {
			exec_ns = 0;
			rcu_read_unlock();
		}

		if (copy_to_user((u64 *) arg, &exec_ns, sizeof(u64)))
			return 0;
	}

	return 0;
}

static const struct file_operations ht_ctl_fops = {
	.owner = THIS_MODULE,
	.poll = ht_ctl_poll,
	.open = ht_ctl_open,
	.release = ht_ctl_close,
	.unlocked_ioctl = ht_ctl_ioctl,
	.compat_ioctl = ht_ctl_ioctl,
	.read = seq_read,
	.llseek = seq_lseek,
};

static struct device *class_dev;
static struct class *driver_class;
static struct cdev cdev;
static dev_t ht_ctl_dev;
static int __init ht_ctl(void)
{
	int rc;

	rc = alloc_chrdev_region(&ht_ctl_dev, 0, 1, HT_CTL_NODE);
	if (rc < 0) {
		pr_err("alloc_chrdev_region failed %d\n", rc);
		return 0;
	}

	driver_class = class_create(THIS_MODULE, HT_CTL_NODE);
	if (IS_ERR(driver_class)) {
		rc = -ENOMEM;
		pr_err("class_create failed %d\n", rc);
		goto exit_unreg_chrdev_region;
	}

	class_dev = device_create(driver_class, NULL, ht_ctl_dev, NULL, HT_CTL_NODE);
	if (IS_ERR(class_dev)) {
		pr_err("class_device_create failed %d\n", rc);
		rc = -ENOMEM;
		goto exit_destroy_class;
	}

	cdev_init(&cdev, &ht_ctl_fops);
	cdev.owner = THIS_MODULE;
	rc = cdev_add(&cdev, MKDEV(MAJOR(ht_ctl_dev), 0), 1);
	if (rc < 0) {
		pr_err("cdev_add failed %d\n", rc);
		goto exit_destroy_device;
	}

	return 0;

exit_destroy_device:
	device_destroy(driver_class, ht_ctl_dev);
exit_destroy_class:
	class_destroy(driver_class);
exit_unreg_chrdev_region:
	unregister_chrdev_region(ht_ctl_dev, 1);

	return 0;
}

pure_initcall(ht_ctl);
