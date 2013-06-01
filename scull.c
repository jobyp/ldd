/* -*- linux-c -*- */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/moduleparam.h>
#include <linux/slab.h>

MODULE_LICENSE("GPL");

static dev_t scull;
#define NUM_DEVS 4
static int scull_quantum;
static int qset;

struct scull_dev {
	struct scull_qset *data; 
	int quantum; 
	int qset;    
	unsigned long size;
	unsigned int access_key;
	struct semaphore sem;
	struct cdev cdev;
};

struct scull_qset {
	void **data;
	struct scull_qset *next;
};


static int scull_trim(struct scull_dev *dev)
{
	struct scull_qset *dptr, *next;
	int qset = dev->qset;  /* dev is not NULL */
	int i;

	for(dptr = dev->data; dptr; dptr = next) {
		if (dptr->data) {
			for(i = 0; i < qset; i++)
				kfree(dptr->data[i]);
			kfree(dptr->data);
			dptr->data = NULL;
		}
		next = dptr->next;
		kfree(dptr);
	}
	dev->size = 0;
	dev->quantum = scull_quantum;
	dev->qset = scull_qset;
	dev->data = NULL;
	return 0;
}

int scull_open(struct inode *inode, struct file *filp)
{
	struct scull_dev *dev;
	
	dev = container_of(inode->i_cdev, struct scull_dev, cdev);
	filp->private_data = dev;
	
	if ((filp->f_flags & O_ACCMODE) == O_WRONLY)
		scull_trim(dev);
	
	return 0;
}

int scull_release(struct inode *inode, struct file *filp)
{
	return 0;
}

/* ssize_t scull_read(struct file *filp, char __user *, size_t, loff_t *); */
/* ssize_t scull_write(struct file *filp, const char __user *, size_t, loff_t *); */
/* loff_t scull_llseek(struct file *filp, loff_t, int); */

long scull_ioctl(struct file *, unsigned int, unsigned long)
{
	
}

struct file_operations scull_fops = {
	.owner = THIS_MODULE,
	.llseek = scull_llseek,
	.read = scull_read,
	.write = scull_write,
	.unlocked_ioctl = scull_ioctl,
	.open = scull_open,
	.release = scull_release,
};

static void scull_setup_cdev(struct scull_dev *dev, int index)
{
	int err;
	dev_t devno = MKDEV(MAJOR(scull), MINOR(scull) + index);

	cdev_init(&dev->cdev, &scull_fops);
	dev->cdev.owner = THIS_MODULE;
	err = cdev_add(&dev->cdev, devno, 1);
	
	if (err)
		pr_notice("Error %d adding scull%d", err, index);
	return;
}

static int __init scull_init(void)
{
	int ret = 0;
	ret = alloc_chrdev_region(&scull, 0, NUM_DEVS, "scull");
	pr_alert("Scull's major number is %d\n", MAJOR(scull));
	return 0;
}

static void __exit scull_cleanup(void)
{
	/*CHECKME: Call scull_trim to free memory */
	if (MAJOR(scull) != 0)
		unregister_chrdev_region(scull, NUM_DEVS);
	pr_alert("Goodbye, cruel world\n");
}

module_init(scull_init);
module_exit(scull_cleanup);

