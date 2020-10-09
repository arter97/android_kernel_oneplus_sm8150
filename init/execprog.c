// SPDX-License-Identifier: GPL-2.0
/*
 * init/execprog.c
 *
 * Copyright (c) 2019 Park Ju Hyung(arter97)
 *
 * This module injects a binary from the kernel's __init storage to the
 * userspace and executes it.
 *
 * This is useful in cases like Android, where you would want to avoid changing
 * a physical block device to execute a program just for your kernel.
 */

#define pr_fmt(fmt) "execprog: " fmt

#include <linux/buffer_head.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/module.h>
#include <linux/namei.h>
#include <linux/rcutree.h>
#include <linux/string.h>
#include <linux/vmalloc.h>

#include "execprog.h"

/*
 * Set CONFIG_EXECPROG_WAIT_FOR carefully.
 *
 * It's assumed that if CONFIG_EXECPROG_WAIT_FOR is ready,
 * the path to CONFIG_EXECPROG_DST is also ready for writing.
 */

// Do we really need these to be configurable?
#define DELAY_MS 10
#define SAVE_DST CONFIG_EXECPROG_DST
#define WAIT_FOR CONFIG_EXECPROG_WAIT_FOR

static struct delayed_work execprog_work;
static unsigned char* data;
static u32 size;

static struct file *file_open(const char *path, int flags, umode_t rights)
{
	struct file *filp;
	mm_segment_t oldfs;

	oldfs = get_fs();
	set_fs(get_ds());
	filp = filp_open(path, flags, rights);
	set_fs(oldfs);

	if (IS_ERR(filp))
		return NULL;

	return filp;
}

static void execprog_worker(struct work_struct *work)
{
	struct path path;
	struct file *file;
	char *argv[] = { SAVE_DST, NULL };
	loff_t off = 0;
	u32 pos = 0;
	u32 diff;
	int ret;

	pr_info("worker started\n");

	pr_info("waiting for %s\n", WAIT_FOR);
	while (kern_path(WAIT_FOR, LOOKUP_FOLLOW, &path))
		msleep(DELAY_MS);

	pr_info("saving binary to userspace\n");
	file = file_open(SAVE_DST, O_CREAT | O_WRONLY | O_TRUNC, 0755);
	if (file == NULL) {
		pr_err("failed to save to %s\n", SAVE_DST);
		return;
	}

	while (pos < size) {
		diff = size - pos;
		ret = kernel_write(file, data + pos,
				diff > 4096 ? 4096 : diff, &off);
		pos += ret;
	}

	filp_close(file, NULL);
	vfree(data);

	do {
		/*
		 * Wait for RCU grace period to end for the file to close properly.
		 * call_usermodehelper() will return -ETXTBSY without this barrier.
		 */
		rcu_barrier();
		msleep(10);

		ret = call_usermodehelper(argv[0], argv, NULL, UMH_WAIT_EXEC);
	} while (ret == -ETXTBSY);

	if (ret)
		pr_err("execution failed with return code: %d\n", ret);
	else
		pr_info("execution finished\n");
}

static int __init execprog_init(void)
{
	int i;

	pr_info("copying static data\n");

	// Allocate memory
	data = vmalloc(last_index * 4096);
	size = (last_index - 1) * 4096 + last_items;
	// Copy data from __init section
	for (i = 0; i < last_index - 1; i++)
		memcpy(data + (i * 4096), *(primary + i), 4096);
	i = (last_index - 1);
	memcpy(data + (i * 4096), *(primary + i), last_items);

	pr_info("finished copying\n");

	INIT_DELAYED_WORK(&execprog_work, execprog_worker);
	queue_delayed_work(system_freezable_power_efficient_wq,
			&execprog_work, DELAY_MS);

	return 0;
}

static void __exit execprog_exit(void)
{
	return;
}

module_init(execprog_init);
module_exit(execprog_exit);

MODULE_DESCRIPTION("Userspace binary injector");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Park Ju Hyung");
