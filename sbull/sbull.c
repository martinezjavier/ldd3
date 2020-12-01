/*
 * Sample disk driver, from the beginning.
 */

#include <linux/version.h> 	/* LINUX_VERSION_CODE  */
#include <linux/blk-mq.h>	
/* https://olegkutkov.me/2020/02/10/linux-block-device-driver/
   https://prog.world/linux-kernel-5-0-we-write-simple-block-device-under-blk-mq/           
   blk-mq and kernels >= 5.0
*/


#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>

#include <linux/sched.h>
#include <linux/kernel.h>	/* printk() */
#include <linux/slab.h>		/* kmalloc() */
#include <linux/fs.h>		/* everything... */
#include <linux/errno.h>	/* error codes */
#include <linux/timer.h>
#include <linux/types.h>	/* size_t */
#include <linux/fcntl.h>	/* O_ACCMODE */
#include <linux/hdreg.h>	/* HDIO_GETGEO */
#include <linux/kdev_t.h>
#include <linux/vmalloc.h>
#include <linux/genhd.h>
#include <linux/blkdev.h>
#include <linux/buffer_head.h>	/* invalidate_bdev */
#include <linux/bio.h>

MODULE_LICENSE("Dual BSD/GPL");

static int sbull_major = 0;
module_param(sbull_major, int, 0);
static int hardsect_size = 512;
module_param(hardsect_size, int, 0);
static int nsectors = 1024;	/* How big the drive is */
module_param(nsectors, int, 0);
static int ndevices = 4;
module_param(ndevices, int, 0);

/*
 * The different "request modes" we can use.
 */
enum {
	RM_SIMPLE  = 0,	/* The extra-simple request function */
	RM_FULL    = 1,	/* The full-blown version */
	RM_NOQUEUE = 2,	/* Use make_request */
};
static int request_mode = RM_SIMPLE;
module_param(request_mode, int, 0);

/*
 * Minor number and partition management.
 */
#define SBULL_MINORS	16
#define MINOR_SHIFT	4
#define DEVNUM(kdevnum)	(MINOR(kdev_t_to_nr(kdevnum)) >> MINOR_SHIFT

/*
 * We can tweak our hardware sector size, but the kernel talks to us
 * in terms of small sectors, always.
 */
#define KERNEL_SECTOR_SIZE	512

/*
 * After this much idle time, the driver will simulate a media change.
 */
#define INVALIDATE_DELAY	30*HZ

/*
 * The internal representation of our device.
 */
struct sbull_dev {
        int size;                       /* Device size in sectors */
        u8 *data;                       /* The data array */
        short users;                    /* How many users */
        short media_change;             /* Flag a media change? */
        spinlock_t lock;                /* For mutual exclusion */
	struct blk_mq_tag_set tag_set;	/* tag_set added */
        struct request_queue *queue;    /* The device request queue */
        struct gendisk *gd;             /* The gendisk structure */
        struct timer_list timer;        /* For simulated media changes */
};

static struct sbull_dev *Devices = NULL;

/**
* See https://github.com/openzfs/zfs/pull/10187/
*/
#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 9, 0))
static inline struct request_queue *
blk_generic_alloc_queue(make_request_fn make_request, int node_id)
#else
static inline struct request_queue *
blk_generic_alloc_queue(int node_id)
#endif
{
#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 7, 0))
	struct request_queue *q = blk_alloc_queue(GFP_KERNEL);
	if (q != NULL)
		blk_queue_make_request(q, make_request);

	return (q);
#elif (LINUX_VERSION_CODE < KERNEL_VERSION(5, 9, 0))
	return (blk_alloc_queue(make_request, node_id));
#else
	return (blk_alloc_queue(node_id));
#endif
}

/*
 * Handle an I/O request.
 */
static void sbull_transfer(struct sbull_dev *dev, unsigned long sector,
		unsigned long nsect, char *buffer, int write)
{
	unsigned long offset = sector*KERNEL_SECTOR_SIZE;
	unsigned long nbytes = nsect*KERNEL_SECTOR_SIZE;

	if ((offset + nbytes) > dev->size) {
		printk (KERN_NOTICE "Beyond-end write (%ld %ld)\n", offset, nbytes);
		return;
	}
	if (write)
		memcpy(dev->data + offset, buffer, nbytes);
	else
		memcpy(buffer, dev->data + offset, nbytes);
}

/*
 * The simple form of the request function.
 */
//static void sbull_request(struct request_queue *q)
static blk_status_t sbull_request(struct blk_mq_hw_ctx *hctx, const struct blk_mq_queue_data* bd)   /* For blk-mq */
{
	struct request *req = bd->rq;
	struct sbull_dev *dev = req->rq_disk->private_data;
        struct bio_vec bvec;
        struct req_iterator iter;
        sector_t pos_sector = blk_rq_pos(req);
	void	*buffer;
	blk_status_t  ret;

	blk_mq_start_request (req);

	if (blk_rq_is_passthrough(req)) {
		printk (KERN_NOTICE "Skip non-fs request\n");
                ret = BLK_STS_IOERR;  //-EIO
			goto done;
	}
	rq_for_each_segment(bvec, req, iter)
	{
		size_t num_sector = blk_rq_cur_sectors(req);
		printk (KERN_NOTICE "Req dev %u dir %d sec %lld, nr %ld\n",
                        (unsigned)(dev - Devices), rq_data_dir(req),
                        pos_sector, num_sector);
		buffer = page_address(bvec.bv_page) + bvec.bv_offset;
		sbull_transfer(dev, pos_sector, num_sector,
				buffer, rq_data_dir(req) == WRITE);
		pos_sector += num_sector;
	}
	ret = BLK_STS_OK;
done:
	blk_mq_end_request (req, ret);
	return ret;
}


/*
 * Transfer a single BIO.
 */
static int sbull_xfer_bio(struct sbull_dev *dev, struct bio *bio)
{
	struct bio_vec bvec;
	struct bvec_iter iter;
	sector_t sector = bio->bi_iter.bi_sector;

	/* Do each segment independently. */
	bio_for_each_segment(bvec, bio, iter) {
		//char *buffer = __bio_kmap_atomic(bio, i, KM_USER0);
		char *buffer = kmap_atomic(bvec.bv_page) + bvec.bv_offset;
		//sbull_transfer(dev, sector, bio_cur_bytes(bio) >> 9,
		sbull_transfer(dev, sector, (bio_cur_bytes(bio) / KERNEL_SECTOR_SIZE),
				buffer, bio_data_dir(bio) == WRITE);
		//sector += bio_cur_bytes(bio) >> 9;
		sector += (bio_cur_bytes(bio) / KERNEL_SECTOR_SIZE);
		//__bio_kunmap_atomic(buffer, KM_USER0);
		kunmap_atomic(buffer);
	}
	return 0; /* Always "succeed" */
}

/*
 * Transfer a full request.
 */
static int sbull_xfer_request(struct sbull_dev *dev, struct request *req)
{
	struct bio *bio;
	int nsect = 0;
    
	__rq_for_each_bio(bio, req) {
		sbull_xfer_bio(dev, bio);
		//nsect += bio->bi_size/KERNEL_SECTOR_SIZE;
		nsect += bio->bi_iter.bi_size/KERNEL_SECTOR_SIZE;
	}
	return nsect;
}



/*
 * Smarter request function that "handles clustering".
 */
//static void sbull_full_request(struct request_queue *q)
static blk_status_t sbull_full_request(struct blk_mq_hw_ctx * hctx, const struct blk_mq_queue_data * bd)
{
	struct request *req = bd->rq;
	int sectors_xferred;
	//struct sbull_dev *dev = q->queuedata;
	struct sbull_dev *dev = req->q->queuedata;
	blk_status_t  ret;

	blk_mq_start_request (req);
	//while ((req = blk_fetch_request(q)) != NULL) {
		//if (req->cmd_type != REQ_TYPE_FS) {
		if (blk_rq_is_passthrough(req)) {
			printk (KERN_NOTICE "Skip non-fs request\n");
			//__blk_end_request(req, -EIO, blk_rq_cur_bytes(req));
			ret = BLK_STS_IOERR; //-EIO;
			//continue;
			goto done;
		}
		sectors_xferred = sbull_xfer_request(dev, req);
		ret = BLK_STS_OK; 
	done:
		//__blk_end_request(req, 0, sectors_xferred);
		blk_mq_end_request (req, ret);
	//}
	return ret;
}



/*
 * The direct make request version.
 */
#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 9, 0))
//static void sbull_make_request(struct request_queue *q, struct bio *bio)
static blk_qc_t sbull_make_request(struct request_queue *q, struct bio *bio)
#else
static blk_qc_t sbull_make_request(struct bio *bio)
#endif
{
	//struct sbull_dev *dev = q->queuedata;
	struct sbull_dev *dev = bio->bi_disk->private_data;
	int status;

	status = sbull_xfer_bio(dev, bio);
	bio->bi_status = status;
	bio_endio(bio);
	return BLK_QC_T_NONE;
}


/*
 * Open and close.
 */

static int sbull_open(struct block_device *bdev, fmode_t mode)
{
	struct sbull_dev *dev = bdev->bd_disk->private_data;

	del_timer_sync(&dev->timer);
	//filp->private_data = dev;
	spin_lock(&dev->lock);
	if (! dev->users) 
	{
#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0))
		check_disk_change(bdev);
#else
                /* For newer kernels (as of 5.10), bdev_check_media_change()
                 * is used, in favor of check_disk_change(),
                 * with the modification that invalidation
                 * is no longer forced. */

                if(bdev_check_media_change(bdev))
                {
                        struct gendisk *gd = bdev->bd_disk;
                        const struct block_device_operations *bdo = gd->fops;
                        if (bdo && bdo->revalidate_disk)
                                bdo->revalidate_disk(gd);
                }
#endif
	}
	dev->users++;
	spin_unlock(&dev->lock);
	return 0;
}

static void sbull_release(struct gendisk *disk, fmode_t mode)
{
	struct sbull_dev *dev = disk->private_data;

	spin_lock(&dev->lock);
	dev->users--;

	if (!dev->users) {
		dev->timer.expires = jiffies + INVALIDATE_DELAY;
		add_timer(&dev->timer);
	}
	spin_unlock(&dev->lock);
}

/*
 * Look for a (simulated) media change.
 */
int sbull_media_changed(struct gendisk *gd)
{
	struct sbull_dev *dev = gd->private_data;
	
	return dev->media_change;
}

/*
 * Revalidate.  WE DO NOT TAKE THE LOCK HERE, for fear of deadlocking
 * with open.  That needs to be reevaluated.
 */
int sbull_revalidate(struct gendisk *gd)
{
	struct sbull_dev *dev = gd->private_data;
	
	if (dev->media_change) {
		dev->media_change = 0;
		memset (dev->data, 0, dev->size);
	}
	return 0;
}

/*
 * The "invalidate" function runs out of the device timer; it sets
 * a flag to simulate the removal of the media.
 */
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 15, 0)) && !defined(timer_setup)
void sbull_invalidate(unsigned long ldev)
{
        struct sbull_dev *dev = (struct sbull_dev *) ldev;
#else
void sbull_invalidate(struct timer_list * ldev)
{
        struct sbull_dev *dev = from_timer(dev, ldev, timer);
#endif

	spin_lock(&dev->lock);
	if (dev->users || !dev->data) 
		printk (KERN_WARNING "sbull: timer sanity check failed\n");
	else
		dev->media_change = 1;
	spin_unlock(&dev->lock);
}

/*
 * The ioctl() implementation
 */

int sbull_ioctl (struct block_device *bdev, fmode_t mode,
                 unsigned int cmd, unsigned long arg)
{
	long size;
	struct hd_geometry geo;
	struct sbull_dev *dev = bdev->bd_disk->private_data;

	switch(cmd) {
	    case HDIO_GETGEO:
        	/*
		 * Get geometry: since we are a virtual device, we have to make
		 * up something plausible.  So we claim 16 sectors, four heads,
		 * and calculate the corresponding number of cylinders.  We set the
		 * start of data at sector four.
		 */
		size = dev->size*(hardsect_size/KERNEL_SECTOR_SIZE);
		geo.cylinders = (size & ~0x3f) >> 6;
		geo.heads = 4;
		geo.sectors = 16;
		geo.start = 4;
		if (copy_to_user((void __user *) arg, &geo, sizeof(geo)))
			return -EFAULT;
		return 0;
	}

	return -ENOTTY; /* unknown command */
}



/*
 * The device operations structure.
 */
static struct block_device_operations sbull_ops = {
	.owner           = THIS_MODULE,
	.open 	         = sbull_open,
	.release 	 = sbull_release,
#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 9, 0))
	.media_changed   = sbull_media_changed,  // DEPRECATED in v5.9
#else
	.submit_bio      = sbull_make_request,
#endif
	.revalidate_disk = sbull_revalidate,
	.ioctl	         = sbull_ioctl
};

static struct blk_mq_ops mq_ops_simple = {
    .queue_rq = sbull_request,
};

static struct blk_mq_ops mq_ops_full = {
    .queue_rq = sbull_full_request,
};


/*
 * Set up our internal device.
 */
static void setup_device(struct sbull_dev *dev, int which)
{
	/*
	 * Get some memory.
	 */
	memset (dev, 0, sizeof (struct sbull_dev));
	dev->size = nsectors*hardsect_size;
	dev->data = vmalloc(dev->size);
	if (dev->data == NULL) {
		printk (KERN_NOTICE "vmalloc failure.\n");
		return;
	}
	spin_lock_init(&dev->lock);
	
	/*
	 * The timer which "invalidates" the device.
	 */
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 15, 0)) && !defined(timer_setup)
	init_timer(&dev->timer);
	dev->timer.data = (unsigned long) dev;
	dev->timer.function = sbull_invalidate;
#else
        timer_setup(&dev->timer, sbull_invalidate, 0);
#endif


	
	/*
	 * The I/O queue, depending on whether we are using our own
	 * make_request function or not.
	 */
	switch (request_mode) {
	    case RM_NOQUEUE:
#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 9, 0))
		dev->queue =  blk_generic_alloc_queue(sbull_make_request, NUMA_NO_NODE);
#else
		dev->queue =  blk_generic_alloc_queue(NUMA_NO_NODE);
#endif
		if (dev->queue == NULL)
			goto out_vfree;
		break;

	    case RM_FULL:
		//dev->queue = blk_init_queue(sbull_full_request, &dev->lock);
		dev->queue = blk_mq_init_sq_queue(&dev->tag_set, &mq_ops_full, 128, BLK_MQ_F_SHOULD_MERGE);
		if (dev->queue == NULL)
			goto out_vfree;
		break;

	    default:
		printk(KERN_NOTICE "Bad request mode %d, using simple\n", request_mode);
        	/* fall into.. */
	
	    case RM_SIMPLE:
		//dev->queue = blk_init_queue(sbull_request, &dev->lock);
		dev->queue = blk_mq_init_sq_queue(&dev->tag_set, &mq_ops_simple, 128, BLK_MQ_F_SHOULD_MERGE);
		if (dev->queue == NULL)
			goto out_vfree;
		break;
	}
	blk_queue_logical_block_size(dev->queue, hardsect_size);
	dev->queue->queuedata = dev;
	/*
	 * And the gendisk structure.
	 */
	dev->gd = alloc_disk(SBULL_MINORS);
	if (! dev->gd) {
		printk (KERN_NOTICE "alloc_disk failure\n");
		goto out_vfree;
	}
	dev->gd->major = sbull_major;
	dev->gd->first_minor = which*SBULL_MINORS;
	dev->gd->fops = &sbull_ops;
	dev->gd->queue = dev->queue;
	dev->gd->private_data = dev;
	snprintf (dev->gd->disk_name, 32, "sbull%c", which + 'a');
	set_capacity(dev->gd, nsectors*(hardsect_size/KERNEL_SECTOR_SIZE));
	add_disk(dev->gd);
	return;

  out_vfree:
	if (dev->data)
		vfree(dev->data);
}



static int __init sbull_init(void)
{
	int i;
	/*
	 * Get registered.
	 */
	sbull_major = register_blkdev(sbull_major, "sbull");
	if (sbull_major <= 0) {
		printk(KERN_WARNING "sbull: unable to get major number\n");
		return -EBUSY;
	}
	/*
	 * Allocate the device array, and initialize each one.
	 */
	Devices = kmalloc(ndevices*sizeof (struct sbull_dev), GFP_KERNEL);
	if (Devices == NULL)
		goto out_unregister;
	for (i = 0; i < ndevices; i++) 
		setup_device(Devices + i, i);
    
	return 0;

  out_unregister:
	unregister_blkdev(sbull_major, "sbd");
	return -ENOMEM;
}

static void sbull_exit(void)
{
	int i;

	for (i = 0; i < ndevices; i++) {
		struct sbull_dev *dev = Devices + i;

		del_timer_sync(&dev->timer);
		if (dev->gd) {
			del_gendisk(dev->gd);
			put_disk(dev->gd);
		}
		if (dev->queue) {
			if (request_mode == RM_NOQUEUE)
				//kobject_put (&dev->queue->kobj);
				blk_put_queue(dev->queue);
			else
				blk_cleanup_queue(dev->queue);
		}
		if (dev->data)
			vfree(dev->data);
	}
	unregister_blkdev(sbull_major, "sbull");
	kfree(Devices);
}
	
module_init(sbull_init);
module_exit(sbull_exit);
