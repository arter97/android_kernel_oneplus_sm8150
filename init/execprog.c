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

#define DELAY_MS 100 // Do we really need this to be configurable?

static char save_to[PATH_MAX] = CONFIG_EXECPROG_DST;
module_param_string(save_to, save_to, PATH_MAX, 0644);

/*
 * Set wait_for carefully.
 *
 * It's assumed that if wait_for is ready,
 * the path to save_to is also ready for writing.
 */
static char wait_for[PATH_MAX] = CONFIG_EXECPROG_WAIT_FOR;
module_param_string(wait_for, wait_for, PATH_MAX, 0644);

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
	char *argv[] = { save_to, NULL };
	u32 pos = 0;
	u32 diff;
	int ret;

	pr_info("worker started\n");

	if (wait_for[0]) {
		pr_info("waiting for %s\n", wait_for);
		while (kern_path(wait_for, LOOKUP_FOLLOW, &path))
			msleep(DELAY_MS);
	} else {
		pr_info("no file specified to wait for\n");
	}

	if (!save_to[0]) {
		pr_err("no path specified for the binary to be saved!\n");
		return;
	}

	pr_info("saving binary to userspace\n");
	file = file_open(save_to, O_CREAT | O_WRONLY | O_TRUNC, 0755);

	while (pos < size) {
		diff = size - pos;
		ret = kernel_write(file, data + pos,
				diff > 4096 ? 4096 : diff, pos);
		pos += ret;
	}

	filp_close(file, NULL);
	vfree(data);

	/*
	 * Wait for RCU grace period to end for the file to close properly.
	 * call_usermodehelper() will return -ETXTBUSY without this barrier.
	 */
	rcu_barrier();

	pr_info("executing %s\n", argv[0]);
	call_usermodehelper(argv[0], argv, NULL, UMH_WAIT_EXEC);
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
