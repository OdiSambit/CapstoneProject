// SPDX-License-Identifier: GPL-2.0
/* vsensor: simulated interrupt-driven sensor as a Linux char driver.
 * hrtimer = "hardware" raising an interrupt; handler pushes samples into a ring buffer. */
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/hrtimer.h>
#include <linux/ktime.h>
#include <linux/spinlock.h>
#include <linux/mutex.h>
#include <linux/wait.h>
#include <linux/poll.h>
#include <linux/uaccess.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/version.h>
#include "vsensor.h"

#define DEV_NAME "vsensor"
#define RING_SZ  256                       /* power of two */

static unsigned int period_ms = 100;
module_param(period_ms, uint, 0644);
MODULE_PARM_DESC(period_ms, "Initial sampling period in ms");

struct vs_dev {
	struct cdev cdev;
	struct hrtimer timer;
	struct vs_sample ring[RING_SZ];
	unsigned int head, tail;               /* guarded by lock (IRQ-safe) */
	spinlock_t lock;                       /* ring + stats: used in timer ctx */
	struct mutex cfg_lock;                 /* config: process ctx only, may sleep */
	wait_queue_head_t wq;
	struct fasync_struct *async;
	struct vs_stats stats;
	s32 threshold;
	u32 rng;
	bool running;
	ktime_t period;
};
static struct vs_dev vs;
static dev_t devno;
static struct class *vs_class;

static u32 xorshift(u32 *s) { u32 x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }

static enum hrtimer_restart vs_timer_cb(struct hrtimer *t)
{
	struct vs_sample s;
	unsigned long flags;
	bool alert;

	s.ts_ns = ktime_get_ns();
	s.value = 25000 + (s32)(xorshift(&vs.rng) % 20000) - 10000;   /* 15..35 C */

	spin_lock_irqsave(&vs.lock, flags);
	alert = s.value > vs.threshold;
	s.alert = alert;
	if (((vs.head + 1) & (RING_SZ - 1)) == vs.tail) {
		vs.stats.dropped++;                 /* overflow: drop newest */
	} else {
		vs.ring[vs.head] = s;
		vs.head = (vs.head + 1) & (RING_SZ - 1);
		vs.stats.produced++;
		if (alert) vs.stats.alerts++;
	}
	spin_unlock_irqrestore(&vs.lock, flags);

	wake_up_interruptible(&vs.wq);
	if (alert) kill_fasync(&vs.async, SIGIO, POLL_IN);

	hrtimer_forward_now(t, vs.period);
	return HRTIMER_RESTART;
}

static int vs_open(struct inode *i, struct file *f) { return 0; }
static int vs_fasync(int fd, struct file *f, int on) { return fasync_helper(fd, f, on, &vs.async); }
static int vs_release(struct inode *i, struct file *f) { vs_fasync(-1, f, 0); return 0; }

static bool ring_empty(void) { return READ_ONCE(vs.head) == READ_ONCE(vs.tail); }

static ssize_t vs_read(struct file *f, char __user *buf, size_t len, loff_t *off)
{
	size_t n = len / sizeof(struct vs_sample), done = 0;
	if (!n) return -EINVAL;

	while (done < n) {
		struct vs_sample s;
		unsigned long flags;

		spin_lock_irqsave(&vs.lock, flags);
		if (vs.head == vs.tail) {
			spin_unlock_irqrestore(&vs.lock, flags);
			if (done) break;
			if (f->f_flags & O_NONBLOCK) return -EAGAIN;
			if (wait_event_interruptible(vs.wq, !ring_empty())) return -ERESTARTSYS;
			continue;
		}
		s = vs.ring[vs.tail];
		vs.tail = (vs.tail + 1) & (RING_SZ - 1);
		vs.stats.consumed++;
		spin_unlock_irqrestore(&vs.lock, flags);

		if (copy_to_user(buf + done * sizeof(s), &s, sizeof(s))) return -EFAULT;
		done++;
	}
	return done * sizeof(struct vs_sample);
}

static __poll_t vs_poll(struct file *f, poll_table *wait)
{
	poll_wait(f, &vs.wq, wait);
	return ring_empty() ? 0 : (EPOLLIN | EPOLLRDNORM);
}

static long vs_ioctl(struct file *f, unsigned int cmd, unsigned long arg)
{
	long ret = 0;
	unsigned long flags;
	u32 ms; s32 th;

	mutex_lock(&vs.cfg_lock);
	switch (cmd) {
	case VS_IOC_START:
		if (!vs.running) {
			vs.running = true;
			hrtimer_start(&vs.timer, vs.period, HRTIMER_MODE_REL);
		}
		break;
	case VS_IOC_STOP:
		vs.running = false;
		hrtimer_cancel(&vs.timer);      /* safe: process context, not under spinlock */
		break;
	case VS_IOC_SET_RATE:
		if (get_user(ms, (u32 __user *)arg)) { ret = -EFAULT; break; }
		if (ms < 1 || ms > 60000) { ret = -EINVAL; break; }
		vs.period = ms_to_ktime(ms);    /* takes effect on next start */
		break;
	case VS_IOC_SET_THRESH:
		if (get_user(th, (s32 __user *)arg)) { ret = -EFAULT; break; }
		spin_lock_irqsave(&vs.lock, flags);
		vs.threshold = th;
		spin_unlock_irqrestore(&vs.lock, flags);
		break;
	case VS_IOC_GET_STATS: {
		struct vs_stats st;
		spin_lock_irqsave(&vs.lock, flags);
		st = vs.stats;
		spin_unlock_irqrestore(&vs.lock, flags);
		if (copy_to_user((void __user *)arg, &st, sizeof(st))) ret = -EFAULT;
		break;
	}
	case VS_IOC_CLEAR:
		spin_lock_irqsave(&vs.lock, flags);
		vs.head = vs.tail = 0;
		spin_unlock_irqrestore(&vs.lock, flags);
		break;
	default:
		ret = -ENOTTY;
	}
	mutex_unlock(&vs.cfg_lock);
	return ret;
}

static const struct file_operations vs_fops = {
	.owner = THIS_MODULE, .open = vs_open, .release = vs_release,
	.read = vs_read, .poll = vs_poll, .unlocked_ioctl = vs_ioctl,
	.fasync = vs_fasync,
};

static int vs_proc_show(struct seq_file *m, void *v)
{
	struct vs_stats st; unsigned long flags;
	spin_lock_irqsave(&vs.lock, flags); st = vs.stats; spin_unlock_irqrestore(&vs.lock, flags);
	seq_printf(m, "running:   %d\nperiod_ms: %lld\nthreshold: %d\nproduced:  %llu\n"
		      "consumed:  %llu\ndropped:   %llu\nalerts:    %llu\n",
		   vs.running, ktime_to_ms(vs.period), vs.threshold,
		   st.produced, st.consumed, st.dropped, st.alerts);
	return 0;
}
static int vs_proc_open(struct inode *i, struct file *f) { return single_open(f, vs_proc_show, NULL); }
static const struct proc_ops vs_proc_ops = {
	.proc_open = vs_proc_open, .proc_read = seq_read,
	.proc_lseek = seq_lseek, .proc_release = single_release,
};

static int __init vs_init(void)
{
	int ret;
	spin_lock_init(&vs.lock);
	mutex_init(&vs.cfg_lock);
	init_waitqueue_head(&vs.wq);
	vs.threshold = 30000;
	vs.rng = 0x2545F491;
	vs.period = ms_to_ktime(period_ms ? period_ms : 100);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 15, 0)
	hrtimer_setup(&vs.timer, vs_timer_cb, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
#else
	hrtimer_init(&vs.timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	vs.timer.function = vs_timer_cb;
#endif

	ret = alloc_chrdev_region(&devno, 0, 1, DEV_NAME);
	if (ret) return ret;
	cdev_init(&vs.cdev, &vs_fops);
	ret = cdev_add(&vs.cdev, devno, 1);
	if (ret) goto err_region;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 4, 0)
	vs_class = class_create(DEV_NAME);
#else
	vs_class = class_create(THIS_MODULE, DEV_NAME);
#endif
	if (IS_ERR(vs_class)) { ret = PTR_ERR(vs_class); goto err_cdev; }
	if (IS_ERR(device_create(vs_class, NULL, devno, NULL, DEV_NAME "0"))) { ret = -ENODEV; goto err_class; }
	proc_create("vsensor", 0444, NULL, &vs_proc_ops);
	pr_info("vsensor: loaded, /dev/%s0 (major %d)\n", DEV_NAME, MAJOR(devno));
	return 0;
err_class: class_destroy(vs_class);
err_cdev:  cdev_del(&vs.cdev);
err_region: unregister_chrdev_region(devno, 1);
	return ret;
}

static void __exit vs_exit(void)
{
	remove_proc_entry("vsensor", NULL);
	hrtimer_cancel(&vs.timer);
	device_destroy(vs_class, devno);
	class_destroy(vs_class);
	cdev_del(&vs.cdev);
	unregister_chrdev_region(devno, 1);
	pr_info("vsensor: unloaded\n");
}

module_init(vs_init);
module_exit(vs_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Your Name");
MODULE_DESCRIPTION("Virtual interrupt-driven sensor device");
