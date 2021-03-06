/*
  * Sample disk driver, from the beginning.
 */
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/kernel.h>       /* printk() */
#include <linux/slab.h>         /* kmalloc() */
#include <linux/fs.h>           /* everything... */
#include <linux/errno.h>        /* error codes */
#include <linux/timer.h>
#include <linux/types.h>        /* size_t */
#include <linux/fcntl.h>        /* O_ACCMODE */
#include <linux/hdreg.h>        /* HDIO_GETGEO */
#include <linux/kdev_t.h>
#include <linux/vmalloc.h>
#include <linux/genhd.h>
#include <linux/blkdev.h>
#include <linux/buffer_head.h>  /* invalidate_bdev */
#include <linux/bio.h>

MODULE_LICENSE("Dual BSD/GPL");

static int osurd_major = 0;
module_param(osurd_major, int, 0);
static int hardsect_size = 512;
module_param(hardsect_size, int, 0);
static int nsectors = 1024;     /* How big the drive is */
module_param(nsectors, int, 0);
static int ndevices = 4;
module_param(ndevices, int, 0);

/*
 * The different "request modes" we can use.
 */
enum {
        RM_SIMPLE  = 0, /* The extra-simple request function */
        RM_FULL    = 1, /* The full-blown version */
        RM_NOQUEUE = 2, /* Use make_request */
};
static int request_mode = RM_SIMPLE;
module_param(request_mode, int, 0);

/*
 * Minor number and partition management.
 */
#define osurd_MINORS    16
#define MINOR_SHIFT     4
#define DEVNUM(kdevnum) (MINOR(kdev_t_to_nr(kdevnum)) >> MINOR_SHIFT

/*
 * We can tweak our hardware sector size, but the kernel talks to us
 * in terms of small sectors, always.
 */
#define KERNEL_SECTOR_SIZE      512

/*
 * After this much idle time, the driver will simulate a media change.
 */
#define INVALIDATE_DELAY        30*HZ

/*
 * The internal representation of our device.
 */
struct osurd_dev {
        int size;                       /* Device size in sectors */
        u8 *data;                       /* The data array */
        short users;                    /* How many users */
        short media_change;             /* Flag a media change? */
        spinlock_t lock;                /* For mutual exclusion */
        struct request_queue *queue;    /* The device request queue */
        struct gendisk *gd;             /* The gendisk structure */
        struct timer_list timer;        /* For simulated media changes */
};

static struct osurd_dev *Devices = NULL;

/*
 * Handle an I/O request.
 */
static void osurd_transfer(struct osurd_dev *dev, unsigned long sector,
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
static void osurd_request(struct request_queue *q)
{
        struct request *req;

        while ((req = elv_next_request(q)) != NULL) {
                struct osurd_dev *dev = req->rq_disk->private_data;
                if (! blk_fs_request(req)) {
                        printk (KERN_NOTICE "Skip non-fs request\n");
                        blk_end_request(req, -EIO, req->current_nr_sectors << 9);
                        continue;
                }
    //          printk (KERN_NOTICE "Req dev %d dir %ld sec %ld, nr %d f %lx\n",
    //                          dev - Devices, rq_data_dir(req),
    //                          req->sector, req->current_nr_sectors,
    //                          req->flags);
                osurd_transfer(dev, req->sector, req->current_nr_sectors,
                                req->buffer, rq_data_dir(req));
                blk_end_request(req, 1, req->current_nr_sectors << 9);
        }
}


/*
 * Transfer a single BIO.
 */
static int osurd_xfer_bio(struct osurd_dev *dev, struct bio *bio)
{
        int i;
        struct bio_vec *bvec;
        sector_t sector = bio->bi_sector;

        /* Do each segment independently. */
        bio_for_each_segment(bvec, bio, i) {
                char *buffer = __bio_kmap_atomic(bio, i, KM_USER0);
                osurd_transfer(dev, sector, bio_cur_sectors(bio),
                                buffer, bio_data_dir(bio) == WRITE);
                sector += bio_cur_sectors(bio);
                __bio_kunmap_atomic(bio, KM_USER0);
        }
        return 0; /* Always "succeed" */
}

/*
 * Transfer a full request.
 */
static int osurd_xfer_request(struct osurd_dev *dev, struct request *req)
{

        struct req_iterator iter;
        int nsect = 0;
        struct bio_vec *bvec;

        /* Macro rq_for_each_bio is gone.
         * In most cases one should use rq_for_each_segment.
         */
        rq_for_each_segment(bvec, req, iter) {
                char *buffer = __bio_kmap_atomic(iter.bio, iter.i, KM_USER0);
                sector_t sector = iter.bio->bi_sector;
                osurd_transfer(dev, sector, bio_cur_sectors(iter.bio),
                               buffer, bio_data_dir(iter.bio) == WRITE);
                sector += bio_cur_sectors(iter.bio);
                __bio_kunmap_atomic(iter.bio, KM_USER0);
                nsect += iter.bio->bi_size/KERNEL_SECTOR_SIZE;
        }
        return nsect;
}


/*
 * Smarter request function that "handles clustering".
 */
static void osurd_full_request(struct request_queue *q)
{
        struct request *req;
        int sectors_xferred;
        struct osurd_dev *dev = q->queuedata;

        while ((req = elv_next_request(q)) != NULL) {
                if (! blk_fs_request(req)) {
                        printk (KERN_NOTICE "Skip non-fs request\n");
                        end_request(req, 0);
                        continue;
                }
                sectors_xferred = osurd_xfer_request(dev, req);
                __blk_end_request (req, 1, sectors_xferred << 9);
direction88                 /* The above includes a call to add_disk_randomness(). */
        }
}

/*
 * The direct make request version.
 */
   static int osurd_make_request(struct request_queue *q, struct bio *bio)
{
        struct osurd_dev *dev = q->queuedata;
        int status;

        status = osurd_xfer_bio(dev, bio);
        bio_endio(bio, status);
        return 0;
}


/*
 * Open and close.
 */

static int osurd_open(struct inode *inode, struct file *filp)
{
        struct osurd_dev *dev = inode->i_bdev->bd_disk->private_data;

        del_timer_sync(&dev->timer);
        filp->private_data = dev;
        spin_lock(&dev->lock);
        if (! dev->users) 
                check_disk_change(inode->i_bdev);
        dev->users++;
        spin_unlock(&dev->lock);
        return 0;
}

static int osurd_release(struct inode *inode, struct file *filp)
{
        struct osurd_dev *dev = inode->i_bdev->bd_disk->private_data;

        spin_lock(&dev->lock);
        dev->users--;

        if (!dev->users) {
                dev->timer.expires = jiffies + INVALIDATE_DELAY;
                add_timer(&dev->timer);
        }
        spin_unlock(&dev->lock);

        return 0;
}

/*
 * Look for a (simulated) media change.
 */
int osurd_media_changed(struct gendisk *gd)
{
        struct osurd_dev *dev = gd->private_data;
        
        return dev->media_change;
}

/*
 * Revalidate.  WE DO NOT TAKE THE LOCK HERE, for fear of deadlocking
 * with open.  That needs to be reevaluated.
 */
int osurd_revalidate(struct gendisk *gd)
{
        struct osurd_dev *dev = gd->private_data;
        
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
void osurd_invalidate(unsigned long ldev)
{
        struct osurd_dev *dev = (struct osurd_dev *) ldev;

        spin_lock(&dev->lock);
        if (dev->users || !dev->data) 
                printk (KERN_WARNING "osurd: timer sanity check failed\n");
        else
                dev->media_change = 1;
        spin_unlock(&dev->lock);
}

/*
 * The ioctl() implementation
 */

int osurd_ioctl (struct inode *inode, struct file *filp,
                 unsigned int cmd, unsigned long arg)
{
        long size;
        struct hd_geometry geo;
        struct osurd_dev *dev = filp->private_data;

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
static struct block_device_operations osurd_ops = {
        .owner           = THIS_MODULE,
        .open            = osurd_open,
        .release         = osurd_release,
        .media_changed   = osurd_media_changed,
        .revalidate_disk = osurd_revalidate,
        .ioctl           = osurd_ioctl
};


/*
 * Set up our internal device.
 */
static void setup_device(struct osurd_dev *dev, int which)
{
        /*
         * Get some memory.
         */
        memset (dev, 0, sizeof (struct osurd_dev));
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
        init_timer(&dev->timer);
        dev->timer.data = (unsigned long) dev;
        dev->timer.function = osurd_invalidate;
        
        /*
         * The I/O queue, depending on whether we are using our own
         * make_request function or not.
         */
        switch (request_mode) {
            case RM_NOQUEUE:
                dev->queue = blk_alloc_queue(GFP_KERNEL);
                if (dev->queue == NULL)
                        goto out_vfree;
                blk_queue_make_request(dev->queue, osurd_make_request);
                break;

            case RM_FULL:
                dev->queue = blk_init_queue(osurd_full_request, &dev->lock);
                if (dev->queue == NULL)
                        goto out_vfree;
                break;

            default:
                printk(KERN_NOTICE "Bad request mode %d, using simple\n", request_mode);
                /* fall into.. */
        
            case RM_SIMPLE:
                dev->queue = blk_init_queue(osurd_request, &dev->lock);
                if (dev->queue == NULL)
                        goto out_vfree;
                break;
        }
        blk_queue_hardsect_size(dev->queue, hardsect_size);
        dev->queue->queuedata = dev;
        /*
         * And the gendisk structure.
         */
        dev->gd = alloc_disk(osurd_MINORS);
        if (! dev->gd) {
                printk (KERN_NOTICE "alloc_disk failure\n");
                goto out_vfree;
        }
        dev->gd->major = osurd_major;
        dev->gd->first_minor = which*osurd_MINORS;
        dev->gd->fops = &osurd_ops;
        dev->gd->queue = dev->queue;
        dev->gd->private_data = dev;
        snprintf (dev->gd->disk_name, 32, "osurd%c", which + 'a');
        set_capacity(dev->gd, nsectors*(hardsect_size/KERNEL_SECTOR_SIZE));
        add_disk(dev->gd);
        return;

  out_vfree:
        if (dev->data)
                vfree(dev->data);
}



static int __init osurd_init(void)
{
        int i;
        /*
         * Get registered.
         */
        osurd_major = register_blkdev(osurd_major, "osurd");
        if (osurd_major <= 0) {
                printk(KERN_WARNING "osurd: unable to get major number\n");
                return -EBUSY;
        }
        /*
         * Allocate the device array, and initialize each one.
         */
        Devices = kmalloc(ndevices*sizeof (struct osurd_dev), GFP_KERNEL);
        if (Devices == NULL)
                goto out_unregister;
        for (i = 0; i < ndevices; i++) 
                setup_device(Devices + i, i);
    
        return 0;

  out_unregister:
        unregister_blkdev(osurd_major, "sbd");
        return -ENOMEM;
}

static void osurd_exit(void)
{
        int i;

        for (i = 0; i < ndevices; i++) {
                struct osurd_dev *dev = Devices + i;

                del_timer_sync(&dev->timer);
                if (dev->gd) {
                        del_gendisk(dev->gd);
                        put_disk(dev->gd);
                }
                if (dev->queue) {
                        if (request_mode == RM_NOQUEUE)
                                kobject_put (&dev->queue->kobj);
                                /* blk_put_queue() is no longer an exported symbol */
                        else
                                blk_cleanup_queue(dev->queue);
                }
                if (dev->data)
                        vfree(dev->data);
        }
        unregister_blkdev(osurd_major, "osurd");
        kfree(Devices);
}
        
module_init(osurd_init);
module_exit(osurd_exit);
