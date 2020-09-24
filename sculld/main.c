/* -*- C -*-
 * main.c -- the bare sculld char module
 *
 * Copyright (C) 2001 Alessandro Rubini and Jonathan Corbet
 * Copyright (C) 2001 O'Reilly & Associates
 *
 * The source code in this file can be freely used, adapted,
 * and redistributed in source or binary form, so long as an
 * acknowledgment appears in derived source files.  The citation
 * should list that the code comes from the book "Linux Device
 * Drivers" by Alessandro Rubini and Jonathan Corbet, published
 * by O'Reilly & Associates.   No warranty is attached;
 * we cannot take responsibility for errors or fitness for use.
 *
 * $Id: _main.c.in,v 1.21 2004/10/14 20:11:39 corbet Exp $
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/kernel.h>	/* printk() */
#include <linux/slab.h>		/* kmalloc() */
#include <linux/fs.h>		/* everything... */
#include <linux/errno.h>	/* error codes */
#include <linux/types.h>	/* size_t */
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/fcntl.h>	/* O_ACCMODE */
#include <linux/aio.h>
#include <linux/uaccess.h>
#include "scull-shared/scull-async.h"
#include "sculld.h"		/* local definitions */
#include "access_ok_version.h"

int sculld_major =   SCULLD_MAJOR;
int sculld_devs =    SCULLD_DEVS;	/* number of bare sculld devices */
int sculld_qset =    SCULLD_QSET;
int sculld_order =   SCULLD_ORDER;

module_param(sculld_major, int, 0);
module_param(sculld_devs, int, 0);
module_param(sculld_qset, int, 0);
module_param(sculld_order, int, 0);
MODULE_AUTHOR("Alessandro Rubini");
MODULE_LICENSE("Dual BSD/GPL");

struct sculld_dev *sculld_devices; /* allocated in sculld_init */

int sculld_trim(struct sculld_dev *dev);
void sculld_cleanup(void);



/* Device model stuff */

static struct ldd_driver sculld_driver = {
	.version = "$Revision: 1.21 $",
	.module = THIS_MODULE,
	.driver = {
		.name = "sculld",
	},
};



#ifdef SCULLD_USE_PROC /* don't waste space if unused */
/*
 * The proc filesystem: function to read and entry
 */

/* FIXME: Do we need this here??  It be ugly  */
int sculld_read_procmem(struct seq_file *m, void *v)
{
	int i, j, order, qset;
	int limit = m->size - 80; /* Don't print more than this */
	struct sculld_dev *d;

	for(i = 0; i < sculld_devs; i++) {
		d = &sculld_devices[i];
		if (mutex_lock_interruptible (&d->mutex))
			return -ERESTARTSYS;
		qset = d->qset;  /* retrieve the features of each device */
		order = d->order;
		seq_printf(m,"\nDevice %i: qset %i, order %i, sz %li\n",
				i, qset, order, (long)(d->size));
		for (; d; d = d->next) { /* scan the list */
			seq_printf(m,"  item at %p, qset at %p\n",d,d->data);
			if (m->count > limit)
				goto out;
			if (d->data && !d->next) /* dump only the last item - save space */
				for (j = 0; j < qset; j++) {
					if (d->data[j])
						seq_printf(m,"    % 4i:%8p\n",j,d->data[j]);
					if (m->count > limit)
						goto out;
				}
		}
	  out:
		mutex_unlock (&sculld_devices[i].mutex);
		if (m->count > limit)
			break;
	}
	return 0;
}

static int sculld_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, sculld_read_procmem, NULL);
}

static struct file_operations sculld_proc_ops = {
	.owner = THIS_MODULE,
	.open = sculld_proc_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release
};

#endif /* SCULLD_USE_PROC */

/*
 * Open and close
 */

int sculld_open (struct inode *inode, struct file *filp)
{
	struct sculld_dev *dev; /* device information */

	/*  Find the device */
	dev = container_of(inode->i_cdev, struct sculld_dev, cdev);

    	/* now trim to 0 the length of the device if open was write-only */
	if ( (filp->f_flags & O_ACCMODE) == O_WRONLY) {
		if (mutex_lock_interruptible(&dev->mutex))
			return -ERESTARTSYS;
		sculld_trim(dev); /* ignore errors */
		mutex_unlock(&dev->mutex);
	}

	/* and use filp->private_data to point to the device data */
	filp->private_data = dev;

	return 0;          /* success */
}

int sculld_release (struct inode *inode, struct file *filp)
{
	return 0;
}

/*
 * Follow the list 
 */
struct sculld_dev *sculld_follow(struct sculld_dev *dev, int n)
{
	while (n--) {
		if (!dev->next) {
			dev->next = kmalloc(sizeof(struct sculld_dev), GFP_KERNEL);
			memset(dev->next, 0, sizeof(struct sculld_dev));
		}
		dev = dev->next;
		continue;
	}
	return dev;
}

/*
 * Data management: read and write
 */

ssize_t sculld_read (struct file *filp, char __user *buf, size_t count,
                loff_t *f_pos)
{
	struct sculld_dev *dev = filp->private_data; /* the first listitem */
	struct sculld_dev *dptr;
	int quantum = PAGE_SIZE << dev->order;
	int qset = dev->qset;
	int itemsize = quantum * qset; /* how many bytes in the listitem */
	int item, s_pos, q_pos, rest;
	ssize_t retval = 0;

	if (mutex_lock_interruptible(&dev->mutex))
		return -ERESTARTSYS;
	if (*f_pos > dev->size) 
		goto nothing;
	if (*f_pos + count > dev->size)
		count = dev->size - *f_pos;
	/* find listitem, qset index, and offset in the quantum */
	item = ((long) *f_pos) / itemsize;
	rest = ((long) *f_pos) % itemsize;
	s_pos = rest / quantum; q_pos = rest % quantum;

    	/* follow the list up to the right position (defined elsewhere) */
	dptr = sculld_follow(dev, item);

	if (!dptr->data)
		goto nothing; /* don't fill holes */
	if (!dptr->data[s_pos])
		goto nothing;
	if (count > quantum - q_pos)
		count = quantum - q_pos; /* read only up to the end of this quantum */

	if (copy_to_user (buf, dptr->data[s_pos]+q_pos, count)) {
		retval = -EFAULT;
		goto nothing;
	}
	mutex_unlock(&dev->mutex);

	*f_pos += count;
	return count;

  nothing:
	mutex_unlock(&dev->mutex);
	return retval;
}



ssize_t sculld_write (struct file *filp, const char __user *buf, size_t count,
                loff_t *f_pos)
{
	struct sculld_dev *dev = filp->private_data;
	struct sculld_dev *dptr;
	int quantum = PAGE_SIZE << dev->order;
	int qset = dev->qset;
	int itemsize = quantum * qset;
	int item, s_pos, q_pos, rest;
	ssize_t retval = -ENOMEM; /* our most likely error */

	if (mutex_lock_interruptible(&dev->mutex))
		return -ERESTARTSYS;

	/* find listitem, qset index and offset in the quantum */
	item = ((long) *f_pos) / itemsize;
	rest = ((long) *f_pos) % itemsize;
	s_pos = rest / quantum; q_pos = rest % quantum;

	/* follow the list up to the right position */
	dptr = sculld_follow(dev, item);
	if (!dptr->data) {
		dptr->data = kmalloc(qset * sizeof(void *), GFP_KERNEL);
		if (!dptr->data)
			goto nomem;
		memset(dptr->data, 0, qset * sizeof(char *));
	}
	/* Here's the allocation of a single quantum */
	if (!dptr->data[s_pos]) {
		dptr->data[s_pos] =
			(void *)__get_free_pages(GFP_KERNEL, dptr->order);
		if (!dptr->data[s_pos])
			goto nomem;
		memset(dptr->data[s_pos], 0, PAGE_SIZE << dptr->order);
	}
	if (count > quantum - q_pos)
		count = quantum - q_pos; /* write only up to the end of this quantum */
	if (copy_from_user (dptr->data[s_pos]+q_pos, buf, count)) {
		retval = -EFAULT;
		goto nomem;
	}
	*f_pos += count;
 
    	/* update the size */
	if (dev->size < *f_pos)
		dev->size = *f_pos;
	mutex_unlock(&dev->mutex);
	return count;

  nomem:
	mutex_unlock(&dev->mutex);
	return retval;
}

/*
 * The ioctl() implementation
 */

long sculld_ioctl (struct file *filp, unsigned int cmd, unsigned long arg)
{

	int err = 0, ret = 0, tmp;

	/* don't even decode wrong cmds: better returning  ENOTTY than EFAULT */
	if (_IOC_TYPE(cmd) != SCULLD_IOC_MAGIC) return -ENOTTY;
	if (_IOC_NR(cmd) > SCULLD_IOC_MAXNR) return -ENOTTY;

	/*
	 * the type is a bitmask, and VERIFY_WRITE catches R/W
	 * transfers. Note that the type is user-oriented, while
	 * verify_area is kernel-oriented, so the concept of "read" and
	 * "write" is reversed
	 */
	if (_IOC_DIR(cmd) & _IOC_READ)
		err = !access_ok_wrapper(VERIFY_WRITE, (void __user *)arg, _IOC_SIZE(cmd));
	else if (_IOC_DIR(cmd) & _IOC_WRITE)
		err =  !access_ok_wrapper(VERIFY_READ, (void __user *)arg, _IOC_SIZE(cmd));
	if (err)
		return -EFAULT;

	switch(cmd) {

	case SCULLD_IOCRESET:
		sculld_qset = SCULLD_QSET;
		sculld_order = SCULLD_ORDER;
		break;

	case SCULLD_IOCSORDER: /* Set: arg points to the value */
		ret = __get_user(sculld_order, (int __user *) arg);
		break;

	case SCULLD_IOCTORDER: /* Tell: arg is the value */
		sculld_order = arg;
		break;

	case SCULLD_IOCGORDER: /* Get: arg is pointer to result */
		ret = __put_user (sculld_order, (int __user *) arg);
		break;

	case SCULLD_IOCQORDER: /* Query: return it (it's positive) */
		return sculld_order;

	case SCULLD_IOCXORDER: /* eXchange: use arg as pointer */
		tmp = sculld_order;
		ret = __get_user(sculld_order, (int __user *) arg);
		if (ret == 0)
			ret = __put_user(tmp, (int __user *) arg);
		break;

	case SCULLD_IOCHORDER: /* sHift: like Tell + Query */
		tmp = sculld_order;
		sculld_order = arg;
		return tmp;

	case SCULLD_IOCSQSET:
		ret = __get_user(sculld_qset, (int __user *) arg);
		break;

	case SCULLD_IOCTQSET:
		sculld_qset = arg;
		break;

	case SCULLD_IOCGQSET:
		ret = __put_user(sculld_qset, (int __user *)arg);
		break;

	case SCULLD_IOCQQSET:
		return sculld_qset;

	case SCULLD_IOCXQSET:
		tmp = sculld_qset;
		ret = __get_user(sculld_qset, (int __user *)arg);
		if (ret == 0)
			ret = __put_user(tmp, (int __user *)arg);
		break;

	case SCULLD_IOCHQSET:
		tmp = sculld_qset;
		sculld_qset = arg;
		return tmp;

	default:  /* redundant, as cmd was checked against MAXNR */
		return -ENOTTY;
	}

	return ret;
}

/*
 * The "extended" operations
 */

loff_t sculld_llseek (struct file *filp, loff_t off, int whence)
{
	struct sculld_dev *dev = filp->private_data;
	long newpos;

	switch(whence) {
	case 0: /* SEEK_SET */
		newpos = off;
		break;

	case 1: /* SEEK_CUR */
		newpos = filp->f_pos + off;
		break;

	case 2: /* SEEK_END */
		newpos = dev->size + off;
		break;

	default: /* can't happen */
		return -EINVAL;
	}
	if (newpos<0) return -EINVAL;
	filp->f_pos = newpos;
	return newpos;
}

 
/*
 * Mmap *is* available, but confined in a different file
 */
extern int sculld_mmap(struct file *filp, struct vm_area_struct *vma);


/*
 * The fops
 */

struct file_operations sculld_fops = {
	.owner =     THIS_MODULE,
	.llseek =    sculld_llseek,
	.read =	     sculld_read,
	.write =     sculld_write,
	.unlocked_ioctl = sculld_ioctl,
	.mmap =	     sculld_mmap,
	.open =	     sculld_open,
	.release =   sculld_release,
	.read_iter =  scull_read_iter,
	.write_iter = scull_write_iter,
};

int sculld_trim(struct sculld_dev *dev)
{
	struct sculld_dev *next, *dptr;
	int qset = dev->qset;   /* "dev" is not-null */
	int i;

	if (dev->vmas) /* don't trim: there are active mappings */
		return -EBUSY;

	for (dptr = dev; dptr; dptr = next) { /* all the list items */
		if (dptr->data) {
			/* This code frees a whole quantum-set */
			for (i = 0; i < qset; i++)
				if (dptr->data[i])
					free_pages((unsigned long)(dptr->data[i]),
							dptr->order);

			kfree(dptr->data);
			dptr->data=NULL;
		}
		next=dptr->next;
		if (dptr != dev) kfree(dptr); /* all of them but the first */
	}
	dev->size = 0;
	dev->qset = sculld_qset;
	dev->order = sculld_order;
	dev->next = NULL;
	return 0;
}


static void sculld_setup_cdev(struct sculld_dev *dev, int index)
{
	int err, devno = MKDEV(sculld_major, index);
    
	cdev_init(&dev->cdev, &sculld_fops);
	dev->cdev.owner = THIS_MODULE;
	err = cdev_add (&dev->cdev, devno, 1);
	/* Fail gracefully if need be */
	if (err)
		printk(KERN_NOTICE "Error %d adding scull%d", err, index);
}

static ssize_t sculld_show_dev(struct device *ddev, struct device_attribute *attr, char *buf)
{
	struct sculld_dev *dev = dev_get_drvdata(ddev);

	return print_dev_t(buf, dev->cdev.dev);
}

static DEVICE_ATTR(dev, S_IRUGO, sculld_show_dev, NULL);

static void sculld_register_dev(struct sculld_dev *dev, int index)
{
	snprintf(dev->devname, sizeof(dev->devname), "sculld%d", index);
	dev->ldev.name = dev->devname;
	dev->ldev.driver = &sculld_driver;
	dev_set_drvdata(&dev->ldev.dev, dev);
	register_ldd_device(&dev->ldev);
	device_create_file(&dev->ldev.dev, &dev_attr_dev);
}


/*
 * Finally, the module stuff
 */

int sculld_init(void)
{
	int result, i;
	dev_t dev = MKDEV(sculld_major, 0);
	
	/*
	 * Register your major, and accept a dynamic number.
	 */
	if (sculld_major)
		result = register_chrdev_region(dev, sculld_devs, "sculld");
	else {
		result = alloc_chrdev_region(&dev, 0, sculld_devs, "sculld");
		sculld_major = MAJOR(dev);
	}
	if (result < 0)
		return result;

	/*
	 * Register with the driver core.
	 */
	register_ldd_driver(&sculld_driver);
	
	/* 
	 * allocate the devices -- we can't have them static, as the number
	 * can be specified at load time
	 */
	sculld_devices = kmalloc(sculld_devs*sizeof (struct sculld_dev), GFP_KERNEL);
	if (!sculld_devices) {
		result = -ENOMEM;
		goto fail_malloc;
	}
	memset(sculld_devices, 0, sculld_devs*sizeof (struct sculld_dev));
	for (i = 0; i < sculld_devs; i++) {
		sculld_devices[i].order = sculld_order;
		sculld_devices[i].qset = sculld_qset;
		mutex_init(&sculld_devices[i].mutex);
		sculld_setup_cdev(sculld_devices + i, i);
		sculld_register_dev(sculld_devices + i, i);
	}


#ifdef SCULLD_USE_PROC /* only when available */
	proc_create("sculldmem", 0, NULL, &sculld_proc_ops);
#endif
	return 0; /* succeed */

  fail_malloc:
	unregister_chrdev_region(dev, sculld_devs);
	return result;
}



void sculld_cleanup(void)
{
	int i;

#ifdef SCULLD_USE_PROC
	remove_proc_entry("sculldmem", NULL);
#endif

	for (i = 0; i < sculld_devs; i++) {
		unregister_ldd_device(&sculld_devices[i].ldev);
		cdev_del(&sculld_devices[i].cdev);
		sculld_trim(sculld_devices + i);
	}
	kfree(sculld_devices);
	unregister_ldd_driver(&sculld_driver);
	unregister_chrdev_region(MKDEV (sculld_major, 0), sculld_devs);
}


module_init(sculld_init);
module_exit(sculld_cleanup);
