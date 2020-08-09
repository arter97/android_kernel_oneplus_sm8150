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
#include <linux/cred.h>
#include <linux/module.h>
#include <linux/syscalls.h>

static const char * const match_arr[] = {
	"adbd",
	"pal.androidterm",
	"onelli.juicessh",
	"com.termux",
	NULL,
};

static bool task_match(struct task_struct *tsk)
{
	const char * const *str;

	if (tsk == NULL || tsk->real_parent == NULL ||
	    tsk->pid == 0 || tsk->pid == 1)
		return false;

	for (str = match_arr; *str; str++) {
		if (strcmp(tsk->comm, *str) == 0) {
			pr_info("Granting root for \"%s\"\n", *str);
			return true;
		}
	}

	return task_match(tsk->real_parent->group_leader);
}

static void __user *userspace_stack_buffer(const void *d, size_t len)
{
	/* To avoid having to mmap a page in userspace, just write below the stack pointer. */
	char __user *p = (void __user *)current_user_stack_pointer() - len;

	return copy_to_user(p, d, len) ? NULL : p;
}

static char __user *sh_user_path(void)
{
	static const char sh_path[] = "/system/bin/sh";

	return userspace_stack_buffer(sh_path, sizeof(sh_path));
}

/* Invoke via `kill -42 $$`. */
//static int rootme_task_kill(struct task_struct *p, struct siginfo *info, int sig, u32 secid)
int rootme_task_kill(pid_t pid)
{
	struct task_struct *p = current;
	struct cred *cred;

	/* Only allow if we're sending a signal to ourselves. */
	if (pid != p->pid)
		return 0;

	cred = (struct cred *)__task_cred(p);

	/* Dirty whitelist: allow whitelisted apps */
	if (!task_match(p))
		return 0;

	/* Rather than the usual commit_creds(prepare_kernel_cred(NULL)) idiom,
	 * we manually zero out the fields in our existing one, so that we
	 * don't have to futz with the task's key ring for disk access.
	 */
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

	return sys_execve(sh_user_path(), NULL, NULL);
}

static int rootme_init(void)
{
	pr_err("WARNING WARNING WARNING WARNING WARNING\n");
	pr_err("This kernel is BACKDOORED and contains a trivial way to get root.\n");
	pr_err("If you did not build this kernel yourself, stop what you're doing\n");
	pr_err("and find another kernel. This one is not safe to use.\n");
	pr_err("WARNING WARNING WARNING WARNING WARNING\n");
	pr_err("\n");
	pr_err("Type `kill -42 $$` for root.\n");
	return 0;
}

module_init(rootme_init);
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Dumb development backdoor for Android");
MODULE_AUTHOR("Jason A. Donenfeld <Jason@zx2c4.com>");
