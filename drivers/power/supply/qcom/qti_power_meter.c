// SPDX-License-Identifier: GPL-2.0
/*
 * qti_power_meter.c
 *
 * Copyright (c) 2022 Juhyung Park (arter97)
 */

//#define pr_fmt(fmt) "qti_power_meter: " fmt

#include <linux/cdev.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/power_supply.h>
#include <linux/uaccess.h>

#define QTIPM_DEV "qtipm"
static dev_t qtipm_device_no;

// Name exposed at /sys/class/power_supply
#define POWER_SUPPLY_NAME "battery"

static struct workqueue_struct *record_wq;
static void record_worker(struct work_struct *work);
static DECLARE_DELAYED_WORK(record_work, record_worker);
static DEFINE_MUTEX(record_mutex);

static unsigned short *buf;
static int buffer_size = SZ_1M;
module_param(buffer_size, int, 0644);

// PMIC doesn't update more often than this limit
#define SAMPLES_PER_SECOND_LIMIT 10

static int samples_per_second = 10;
module_param(samples_per_second, int, 0644);

static struct record_priv_struct {
	int idx;
	int max_idx;
	s64 start_time_ms;
	s64 expected_queue_ms;
	bool recording;
	struct smb_charger *chg;
} record_priv;

static struct read_priv_struct {
	int read_idx;
	bool reading; // TODO: use atomic
	struct completion comp;
} read_priv;

static void stop_worker(void)
{
	if (mutex_trylock(&record_mutex)) {
		// Nothing to stop
		pr_err("record worker isn't running\n");
		mutex_unlock(&record_mutex);
		return;
	}

	cancel_delayed_work_sync(&record_work);
	destroy_workqueue(record_wq);
	mutex_unlock(&record_mutex);
	record_priv.recording = false;
	barrier();
	pr_info("record worker stopped\n");
}

static unsigned short get_power(void)
{
	union power_supply_propval pval = { 0, };
	int rc, curr, volt;
	unsigned short ret;

	// Get current
	rc = smblib_get_prop_usb_current_now(record_priv.chg, &pval);
	if (unlikely(rc < 0)) {
		pr_err("failed to get current\n");
		return 0;
	}
	curr = pval.intval;

	// Get voltage
	rc = smblib_get_prop_usb_voltage_now(record_priv.chg, &pval);
	if (unlikely(rc < 0)) {
		pr_err("failed to get voltage\n");
		return 0;
	}
	volt = pval.intval;

	// PMIC reports in uA, convert it to mA
	curr /= 1000;

	ret = (curr * volt) / 1000;

	//pr_info("idx: %d, current: %d, voltage: %d, watt = %u.%03u\n",
	//	record_priv.idx, curr, volt, ret / 1000, ret % 1000);

	return ret;
}

static void record_worker(struct work_struct *work)
{
	unsigned short *dst;
	s64 cur_time_ms, queue_in_ms;

	// Check if buffer is enough currently
	if (unlikely(record_priv.idx > record_priv.max_idx)) {
		pr_err("buffer too small to handle idx: %d, ending work\n", record_priv.idx);
		stop_worker();
		return;
	}

	// Get power and record to buf[idx];
	dst = buf + record_priv.idx;
	*dst = get_power();

	// Calculate timer drift and adjust as per realworld's clock
	cur_time_ms = ktime_to_ms(ktime_get_real());

	queue_in_ms = (record_priv.start_time_ms + ((record_priv.idx + 1) * record_priv.expected_queue_ms)) - cur_time_ms;
	if (unlikely(queue_in_ms < 0)) {
		pr_err("timer drifted too far: %lld, queueing immediately\n", queue_in_ms);
		queue_in_ms = 0;
	} else if (unlikely(queue_in_ms < (record_priv.expected_queue_ms / 2))) {
		// Warn if queue_in_ms is less than half of what's expected
		pr_err("queue_in_ms = %lld is too small, expected %lld; is the worker taking too long?\n",
			queue_in_ms, record_priv.expected_queue_ms);
	}

	// Done
	pr_info("idx: %d, queue_in_ms: %lld\n", record_priv.idx, queue_in_ms);
	WRITE_ONCE(record_priv.idx, record_priv.idx + 1);
	if (read_priv.reading)
		complete(&read_priv.comp);

	queue_delayed_work(record_wq, &record_work, msecs_to_jiffies(queue_in_ms));
}

static int start_record(void)
{
	int ret;
	int seconds;

	// Sanity check
	if (buffer_size == 0) {
		pr_err("buffer_size is set to 0!\n");
		return -EINVAL;
	}

	if (samples_per_second == 0) {
		pr_err("samples_per_second is set to 0!\n");
		return -EINVAL;
	}

	if (samples_per_second > SAMPLES_PER_SECOND_LIMIT) {
		pr_err("samples_per_second (%d) exceeds the hardware limit (%d)!\n",
			samples_per_second, SAMPLES_PER_SECOND_LIMIT);
		return -EINVAL;
	}

	if (smb_main_charger == NULL)
		return -EBUSY;

	if (mutex_trylock(&record_mutex) == 0)
		return -EBUSY;

	// Allocate memory and print how long it can last
	if (buf != NULL)
		kfree(buf);
	buf = kmalloc(buffer_size, GFP_KERNEL);
	if (buf == NULL) {
		pr_err("failed to allocate %d memory for recording\n", buffer_size);
		ret = -ENOMEM;
		goto err;
	}

	seconds = buffer_size / sizeof(unsigned short) / samples_per_second;
	pr_info("recording will work for %d seconds (%dh %02dm)\n",
		seconds, seconds / 3600, seconds / 60 % 60);

	// Initialize struct record_priv
	record_priv.chg = smb_main_charger;
	record_priv.idx = 0;
	record_priv.max_idx = buffer_size / sizeof(unsigned short);
	record_priv.start_time_ms = ktime_to_ms(ktime_get_real());
	record_priv.expected_queue_ms = 1000 / samples_per_second;
	record_priv.recording = true;

	// Allocate a workqueue
	record_wq = create_singlethread_workqueue("record_wq");

	// Finally, queue the worker thread
	barrier();
	queue_delayed_work(record_wq, &record_work, 0);

	return 0;

err:
	mutex_unlock(&record_mutex);
	return ret;
}

static int qtipm_open(struct inode *inode, struct file *filp)
{
	// TODO: allow only one reader
	read_priv.read_idx = 0;
	init_completion(&read_priv.comp);
	WRITE_ONCE(read_priv.reading, true);

	return 0;
}

static int qtipm_release(struct inode *inode, struct file *filp)
{
	read_priv.reading = false;

	return 0;
}

static ssize_t qtipm_read(struct file *filp, char __user *ubuf, size_t count, loff_t * offset)
{
	ssize_t ret = count;
	int local_idx, sret, off = 0;
	#define i (read_priv.read_idx)
	char str[8]; // 00.000\n\n
	static char kbuf[4096];

	while (i == (local_idx = record_priv.idx)) {
		if (!record_priv.recording)
			return 0;
		wait_for_completion(&read_priv.comp);
	}

	for (; i < local_idx; i++) {
		sret = sprintf(kbuf + off, "%d.%03d\n", buf[i] / 1000, buf[i] % 1000);
		//pr_info("wrote \"%s\" to buf[%d]\n", kbuf + off, off);
		off += sret;

		if (off + sizeof(str) - 1 >= count)
			break;
	}

	// kbuf[off++] = '\0';
	if (unlikely(copy_to_user(ubuf, kbuf, off))) {
		pr_err("%s: failed to copy data to user\n", __func__);
		ret = -EFAULT;
	}
	#undef i

	return off;
}

static ssize_t qtipm_write(struct file *filp, const char __user * buf, size_t count, loff_t * offset)
{
	int ret, len;
	char str[4] = { 0, };

	if (count > 4)
		return -EINVAL;

	if (copy_from_user(&str, buf, count)) {
		pr_err("failed to copy data from user\n");
		return -EFAULT;
	}

	pr_info("received \"%s\"\n", str);

	// Remove trailing '\n'
	len = strlen(str);
	if (str[len - 1] == '\n') {
		str[len - 1] = '\0';
		len = strlen(str);
	}

	if (str[1] != '\0')
		return -EINVAL;

	switch (str[0]) {
	case '0':
		stop_worker();
		break;
	case '1':
		ret = start_record();
		if (ret != 0)
			return ret;
		break;
	default:
		return -EINVAL;
	}

	return count;
}

// Warning: /dev/qtipm is not reentrant
static const struct file_operations qtipm_fops = {
	.open = qtipm_open,
	.release = qtipm_release,
	.read = qtipm_read,
	.write = qtipm_write,
};

static struct class *qtipm_class;
static struct device *qtipm_device;
static struct cdev cdev;
int qtipm_init_module(void)
{
	int ret;

	qtipm_class = class_create(THIS_MODULE, QTIPM_DEV);
	if (IS_ERR(qtipm_class)) {
		ret = PTR_ERR(qtipm_class);
		pr_warn("Failed to register class qtipm\n");
		goto error1;
	}

	ret = alloc_chrdev_region(&qtipm_device_no, 0, 1, QTIPM_DEV);
	if (ret < 0) {
		pr_err("alloc_chrdev_region failed %d\n", ret);
		goto error0;
	}

	qtipm_device = device_create(qtipm_class, NULL, qtipm_device_no, NULL, QTIPM_DEV);
	if (IS_ERR(qtipm_device)) {
		pr_warn("failed to create qtipm device\n");
		goto error3;
	}

	cdev_init(&cdev, &qtipm_fops);
	cdev.owner = THIS_MODULE;
	ret = cdev_add(&cdev, MKDEV(MAJOR(qtipm_device_no), 0), 1);
	if (ret) {
		pr_warn("Failed to add cdev for /dev/qtipm\n");
		goto error2;
	}

	pr_info("qtipm registered\n");

	return 0;

error3:
	cdev_del(&cdev);
error2:
	class_destroy(qtipm_class);
error1:
	unregister_chrdev_region(qtipm_device_no, 1);
error0:

	return ret;
}

module_init(qtipm_init_module);
