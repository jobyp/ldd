#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/moduleparam.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <asm/uaccess.h>

MODULE_LICENSE("GPL");

#define SCULL_NUM_DEVS 4
#define SCULL_QUANTUM 4000
#define SCULL_QSET 1000

static dev_t scull_dev = 0;

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

static struct scull_dev *scull_devices = NULL;

static struct scull_qset *scull_follow(struct scull_dev *dev, int n)
{
	struct scull_qset *dptr = dev->data;

	if (!dev->data) {
		dev->data = dptr = kmalloc(sizeof(struct scull_qset), GFP_KERNEL);
		if (dptr == NULL)
			return NULL;
		memset(dptr, 0, sizeof(struct scull_qset));
	}
		
	while(n > 0) {
		if (!dptr->next) {
			dptr->next = kmalloc(sizeof(struct scull_qset), GFP_KERNEL);
			if (dptr->next == NULL)
				return NULL;
			memset(dptr->next, 0, sizeof(struct scull_qset));
		}
		dptr = dptr->next;
		--n;
	}

	return dptr;
}

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
	dev->quantum = SCULL_QUANTUM;
	dev->qset = SCULL_QSET;
	dev->data = NULL;
	return 0;
}

static int scull_open(struct inode *inode, struct file *filp)
{
	struct scull_dev *dev;
	
	dev = container_of(inode->i_cdev, struct scull_dev, cdev);
	filp->private_data = dev;
	
	if ((filp->f_flags & O_ACCMODE) == O_WRONLY)
		scull_trim(dev);
	
	return 0;
}

static int scull_release(struct inode *inode, struct file *filp)
{
	return 0;
}

static loff_t scull_llseek(struct file *filp, loff_t off, int whence)
{
	struct scull_dev *dev = filp->private_data;
	loff_t newpos;

	switch(whence) {
	case SEEK_SET:
		newpos = off;
		break;

	case SEEK_CUR:
		newpos = filp->f_pos + off;
		break;

	case SEEK_END:
		newpos = dev->size + off;
		break;

	default:
		return -EINVAL;
	}

	if (newpos < 0) return -EINVAL;
	filp->f_pos = newpos;
	return newpos;
}

static ssize_t scull_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
	struct scull_dev *dev = filp->private_data;
	struct scull_qset *dptr;
	int quantum = dev->quantum;
	int qset = dev->qset;
	int itemsize = quantum * qset;
	int item, s_pos, q_pos, rest;
	ssize_t retval = 0;
	
	if (down_interruptible(&dev->sem))
		return -ERESTARTSYS;
	if (*f_pos >= dev->size)
		goto out;
	if (*f_pos + count > dev->size)
		count = dev->size - *f_pos;
	
	/* find the listitem, qset index, and offset in the quantum */
	item = (long)*f_pos / itemsize;
	rest = (long)*f_pos % itemsize;
	s_pos = rest / quantum; 
	q_pos = rest % quantum;

	dptr = scull_follow(dev, item);
	
	if (dptr == NULL || !dptr->data || !dptr->data[s_pos])
		goto out;

	if (count > quantum - q_pos)
		count = quantum - q_pos;
	
	if (copy_to_user(buf, dptr->data[s_pos] + q_pos, count)) {
		retval = -EFAULT;
		goto out;
	}
	
	*f_pos += count;
	retval = count;
out:
	up(&dev->sem);
	return retval;
}

static ssize_t scull_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos)
{
	struct scull_dev *dev = filp->private_data;
	struct scull_qset *dptr;
	int quantum = dev->quantum;
	int qset = dev->qset;
	int itemsize = quantum * qset;
	int item, s_pos, q_pos, rest;
	ssize_t retval = -ENOMEM;

	if (down_interruptible(&dev->sem))
		return -ERESTARTSYS;
	
	item = (long)*f_pos / itemsize;
	rest = (long)*f_pos % itemsize;
	s_pos = rest / quantum;
	q_pos = rest % quantum;

	dptr = scull_follow(dev, item);
	if (dptr == NULL) 
		goto out;

	if (!dptr->data) {
		dptr->data = kmalloc(qset * sizeof(void *), GFP_KERNEL);
		if (!dptr->data)
			goto out;
		memset(dptr->data, 0, qset * sizeof(void *));
	}
	
	if (!dptr->data[s_pos]) {
		dptr->data[s_pos] = kmalloc(quantum, GFP_KERNEL);
		if (!dptr->data[s_pos])
			goto out;
	}

	if (count > quantum - q_pos)
		count = quantum - q_pos;
	
	if (copy_from_user(dptr->data[s_pos] + q_pos, buf, count)) {
		retval = -EFAULT;
		goto out;
	}

	*f_pos += count;
	retval = count;

	if (dev->size < *f_pos)
		dev->size = *f_pos;
			
out:
	up(&dev->sem);
	return retval;
}


static long scull_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	return 0;
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
	dev_t devno = MKDEV(MAJOR(scull_dev), MINOR(scull_dev) + index);

	cdev_init(&dev->cdev, &scull_fops);
	dev->cdev.owner = THIS_MODULE;
	err = cdev_add(&dev->cdev, devno, 1);
	
	if (err)
		pr_notice("scull: error %d adding scull%d\n", err, index);
	return;
}

static void scull_module_cleanup(void)
{
	int i;

	if (scull_devices) {
		for(i = 0; i < SCULL_NUM_DEVS; i++) {
			scull_trim(scull_devices + i);
			cdev_del(&scull_devices[i].cdev);
		}
		kfree(scull_devices);
	}

	if (MAJOR(scull_dev) != 0)
		unregister_chrdev_region(scull_dev, SCULL_NUM_DEVS);

	pr_alert("scull: exiting....\n");
}

static void __exit scull_cleanup(void)
{
	scull_module_cleanup();
}

static int __init scull_init(void)
{
	int i, result = 0;
	
	result = alloc_chrdev_region(&scull_dev, 0, SCULL_NUM_DEVS, "scull");
	if (result < 0) {
		pr_alert("scull: can't get major number\n");
		return result;
	}
	
	scull_devices = kmalloc(SCULL_NUM_DEVS * sizeof(struct scull_dev), GFP_KERNEL);
	if (!scull_devices) {
		result = -ENOMEM;
		goto fail;
	}

	memset(scull_devices, 0, SCULL_NUM_DEVS * sizeof(struct scull_dev));
	
	for(i = 0; i < SCULL_NUM_DEVS; i++) {
		scull_devices[i].quantum = SCULL_QUANTUM;
		scull_devices[i].qset = SCULL_QSET;
		sema_init(&scull_devices[i].sem, 1);
		scull_setup_cdev(&scull_devices[i], i);
	}
	pr_alert("scull: successfully initialized ....\n");
	return 0;

fail:
	scull_module_cleanup();
	return result;
}

module_init(scull_init);
module_exit(scull_cleanup);

