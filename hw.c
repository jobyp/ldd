/* -*- linux-c -*- */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/sched.h>

MODULE_LICENSE("GPL");

static int __init hello_init(void)
{
	printk(KERN_ALERT "Hello world!\n");
	printk(KERN_INFO "----- %s [%d]\n", current->comm, current->pid);
	return 0;
}

static void __exit hello_cleanup(void)
{
	printk(KERN_ALERT "Goodbye, cruel world\n");
}

module_init(hello_init);
module_exit(hello_cleanup);
