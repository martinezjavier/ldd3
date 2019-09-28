/*
 * scull-async.c
 *
 * Copyright (C) 2019 Dan Walkes
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
#include <linux/uio.h>	/* iov_iter* */


/*
 * A simple asynchronous I/O implementation.
 */

struct async_work {
	struct delayed_work work;
	struct kiocb *iocb;
	struct iov_iter *tofrom;
};

/*
 * "Complete" an asynchronous operation.
 */
static void scull_do_deferred_op(struct work_struct *work)
{
	struct async_work *stuff = container_of(work, struct async_work, work.work);
	if( iov_iter_rw(stuff->tofrom) == WRITE ) {
		generic_file_write_iter(stuff->iocb, stuff->tofrom);
	} else {
		generic_file_read_iter(stuff->iocb, stuff->tofrom);
	}
	kfree(stuff);
}


static int scull_defer_op(struct kiocb *iocb, struct iov_iter *tofrom)
{
	struct async_work *stuff;
	int result;
	/* Otherwise defer the completion for a few milliseconds. */
	stuff = kmalloc (sizeof (*stuff), GFP_KERNEL);
	if (stuff == NULL)
		return result; /* No memory, just complete now */
	stuff->iocb = iocb;
	INIT_DELAYED_WORK(&stuff->work, scull_do_deferred_op);
	schedule_delayed_work(&stuff->work, HZ/100);
	return -EIOCBQUEUED;
}


ssize_t scull_read_iter(struct kiocb *iocb, struct iov_iter *to)
{
	/* If this is a synchronous IOCB, we return our status now. */
	if (is_sync_kiocb(iocb)) {
		return generic_file_read_iter(iocb,to);
	}
	return scull_defer_op(iocb, to);
}

ssize_t scull_write_iter(struct kiocb *iocb, struct iov_iter *from)
{
	if (is_sync_kiocb(iocb)) {
		return generic_file_write_iter(iocb,from);
	}
	return scull_defer_op(iocb, from);
}
