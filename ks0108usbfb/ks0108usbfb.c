/*
 * USB Skeleton driver - 2.2
 *
 * Copyright (C) 2001-2004 Greg Kroah-Hartman (greg@kroah.com)
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License as
 *	published by the Free Software Foundation, version 2.
 *
 * This driver is based on the 2.6.3 version of drivers/usb/usb-skeleton.c
 * but has been rewritten to be easier to read and use.
 *
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/kref.h>
#include <linux/uaccess.h>
#include <linux/usb.h>
#include <linux/mutex.h>
#include <linux/workqueue.h>
#include <linux/fb.h>
#include <linux/mm.h>

/* Define these values to match your devices */
#define USB_SKEL_VENDOR_ID	0x16c0
#define USB_SKEL_PRODUCT_ID	0x05dc

/* table of devices that work with this driver */
static const struct usb_device_id skel_table[] = {
	{ USB_DEVICE(USB_SKEL_VENDOR_ID, USB_SKEL_PRODUCT_ID) },
	{ }					/* Terminating entry */
};
MODULE_DEVICE_TABLE(usb, skel_table);


/* Get a minor range for your devices from the usb maintainer */
#define USB_SKEL_MINOR_BASE	192

/* our private defines. if this grows any larger, use your own .h file */
#define MAX_TRANSFER		(PAGE_SIZE - 512)
/* MAX_TRANSFER is chosen so that the VM is not stressed by
   allocations > PAGE_SIZE and the number of packets in a page
   is an integer 512 is the largest possible packet on EHCI */
#define WRITES_IN_FLIGHT	8
/* arbitrarily chosen */

/* Structure to hold all of our device specific stuff */
struct usb_skel {
	struct delayed_work	wrk;
	struct usb_device	*udev;			/* the usb device for this device */
	struct usb_interface	*interface;		/* the interface for this device */
	struct semaphore	limit_sem;		/* limiting the number of writes in progress */
	struct usb_anchor	submitted;		/* in case we need to retract our submissions */
	struct urb		*bulk_in_urb;		/* the urb to read data with */
	unsigned char           *bulk_in_buffer;	/* the buffer to receive data */
	size_t			bulk_in_size;		/* the size of the receive buffer */
	size_t			bulk_in_filled;		/* number of bytes in the buffer */
	size_t			bulk_in_copied;		/* already copied to user space */
	__u8			bulk_in_endpointAddr;	/* the address of the bulk in endpoint */
	__u8			bulk_out_endpointAddr;	/* the address of the bulk out endpoint */
	int			errors;			/* the last request tanked */
	int			open_count;		/* count the number of openers */
	bool			ongoing_read;		/* a read is going on */
	bool			processed_urb;		/* indicates we haven't processed the urb */
	spinlock_t		err_lock;		/* lock for errors */
	struct kref		kref;
	struct mutex		io_mutex;		/* synchronize I/O with disconnect */
	struct completion	bulk_in_completion;	/* to wait for an ongoing read */
	unsigned char ks0108bmp[128 * 64 / 8];
	unsigned char vbuff[128 * 64 / 8];
	struct fb_info *fb;
	struct workqueue_struct *workqueue;
	char update;
};
#define to_skel_dev(d) container_of(d, struct usb_skel, kref)

static struct usb_driver skel_driver;
static void skel_draw_down(struct usb_skel *dev);

static void ks0108usbfb_update(struct work_struct *wrk)
{
	struct usb_skel *dev = (struct usb_skel *)wrk;
	int pipe = usb_sndctrlpipe(dev->udev, 0);
	int byte, bit, x, y;
	unsigned char b;

	for (byte = 0; byte < 128 * 64 / 8; byte++) {
		b = 0;
		for (bit = 0; bit < 8; bit++) {
			x = byte % 128;
			y = byte / 128 * 8 + bit;
			if (dev->vbuff[128 / 8 * y + x / 8] & (1 << (x % 8)))
				b |= 1 << bit;
		}
		dev->ks0108bmp[byte] = b;
	}

	usb_control_msg(dev->udev, pipe, 4,
			USB_TYPE_VENDOR | USB_RECIP_DEVICE | USB_DIR_OUT,
			0, 0, dev->ks0108bmp,
			sizeof(dev->ks0108bmp), 1000);
	if (dev->update)
		queue_delayed_work(dev->workqueue, wrk, HZ / 10);
}

static void skel_delete(struct kref *kref)
{
	struct usb_skel *dev = to_skel_dev(kref);

	usb_free_urb(dev->bulk_in_urb);
	usb_put_dev(dev->udev);
	kfree(dev->bulk_in_buffer);
	if (dev->fb) {
		unregister_framebuffer(dev->fb);
		framebuffer_release(dev->fb);
		dev->update = 0;
		flush_workqueue(dev->workqueue);
		destroy_workqueue(dev->workqueue);
	}

	kfree(dev);
}

static int skel_open(struct inode *inode, struct file *file)
{
	struct usb_skel *dev;
	struct usb_interface *interface;
	int subminor;
	int retval = 0;

	subminor = iminor(inode);

	interface = usb_find_interface(&skel_driver, subminor);
	if (!interface) {
		err("%s - error, can't find device for minor %d",
		     __func__, subminor);
		retval = -ENODEV;
		goto exit;
	}

	dev = usb_get_intfdata(interface);
	if (!dev) {
		retval = -ENODEV;
		goto exit;
	}

	/* increment our usage count for the device */
	kref_get(&dev->kref);

	/* lock the device to allow correctly handling errors
	 * in resumption */
	mutex_lock(&dev->io_mutex);

	if (!dev->open_count++) {
		retval = usb_autopm_get_interface(interface);
			if (retval) {
				dev->open_count--;
				mutex_unlock(&dev->io_mutex);
				kref_put(&dev->kref, skel_delete);
				goto exit;
			}
	} /* else { //uncomment this block if you want exclusive open
		retval = -EBUSY;
		dev->open_count--;
		mutex_unlock(&dev->io_mutex);
		kref_put(&dev->kref, skel_delete);
		goto exit;
	} */
	/* prevent the device from being autosuspended */

	/* save our object in the file's private structure */
	file->private_data = dev;
	mutex_unlock(&dev->io_mutex);

exit:
	return retval;
}

static int skel_release(struct inode *inode, struct file *file)
{
	struct usb_skel *dev;

	dev = file->private_data;
	if (dev == NULL)
		return -ENODEV;

	/* allow the device to be autosuspended */
	mutex_lock(&dev->io_mutex);
	if (!--dev->open_count && dev->interface)
		usb_autopm_put_interface(dev->interface);
	mutex_unlock(&dev->io_mutex);

	/* decrement the count on our device */
	kref_put(&dev->kref, skel_delete);
	return 0;
}

static int skel_flush(struct file *file, fl_owner_t id)
{
	struct usb_skel *dev;
	int res;

	dev = file->private_data;
	if (dev == NULL)
		return -ENODEV;

	/* wait for io to stop */
	mutex_lock(&dev->io_mutex);
	skel_draw_down(dev);

	/* read out errors, leave subsequent opens a clean slate */
	spin_lock_irq(&dev->err_lock);
	res = dev->errors ? (dev->errors == -EPIPE ? -EPIPE : -EIO) : 0;
	dev->errors = 0;
	spin_unlock_irq(&dev->err_lock);

	mutex_unlock(&dev->io_mutex);

	return res;
}

static void skel_read_bulk_callback(struct urb *urb)
{
	struct usb_skel *dev;

	dev = urb->context;

	spin_lock(&dev->err_lock);
	/* sync/async unlink faults aren't errors */
	if (urb->status) {
		if (!(urb->status == -ENOENT ||
		    urb->status == -ECONNRESET ||
		    urb->status == -ESHUTDOWN))
			err("%s - nonzero write bulk status received: %d",
			    __func__, urb->status);

		dev->errors = urb->status;
	} else {
		dev->bulk_in_filled = urb->actual_length;
	}
	dev->ongoing_read = 0;
	spin_unlock(&dev->err_lock);

	complete(&dev->bulk_in_completion);
}

static int skel_do_read_io(struct usb_skel *dev, size_t count)
{
	int rv;

	/* prepare a read */
	usb_fill_bulk_urb(dev->bulk_in_urb,
			dev->udev,
			usb_rcvbulkpipe(dev->udev,
				dev->bulk_in_endpointAddr),
			dev->bulk_in_buffer,
			min(dev->bulk_in_size, count),
			skel_read_bulk_callback,
			dev);
	/* tell everybody to leave the URB alone */
	spin_lock_irq(&dev->err_lock);
	dev->ongoing_read = 1;
	spin_unlock_irq(&dev->err_lock);

	/* do it */
	rv = usb_submit_urb(dev->bulk_in_urb, GFP_KERNEL);
	if (rv < 0) {
		err("%s - failed submitting read urb, error %d",
			__func__, rv);
		dev->bulk_in_filled = 0;
		rv = (rv == -ENOMEM) ? rv : -EIO;
		spin_lock_irq(&dev->err_lock);
		dev->ongoing_read = 0;
		spin_unlock_irq(&dev->err_lock);
	}

	return rv;
}

static ssize_t skel_read(struct file *file, char *buffer, size_t count,
			 loff_t *ppos)
{
	struct usb_skel *dev;
	int rv;
	bool ongoing_io;

	dev = file->private_data;

	/* if we cannot read at all, return EOF */
	if (!dev->bulk_in_urb || !count)
		return 0;

	/* no concurrent readers */
	rv = mutex_lock_interruptible(&dev->io_mutex);
	if (rv < 0)
		return rv;

	if (!dev->interface) {		/* disconnect() was called */
		rv = -ENODEV;
		goto exit;
	}

	/* if IO is under way, we must not touch things */
retry:
	spin_lock_irq(&dev->err_lock);
	ongoing_io = dev->ongoing_read;
	spin_unlock_irq(&dev->err_lock);

	if (ongoing_io) {
		/* nonblocking IO shall not wait */
		if (file->f_flags & O_NONBLOCK) {
			rv = -EAGAIN;
			goto exit;
		}
		/*
		 * IO may take forever
		 * hence wait in an interruptible state
		 */
		rv = wait_for_completion_interruptible(&dev->bulk_in_completion);
		if (rv < 0)
			goto exit;
		/*
		 * by waiting we also semiprocessed the urb
		 * we must finish now
		 */
		dev->bulk_in_copied = 0;
		dev->processed_urb = 1;
	}

	if (!dev->processed_urb) {
		/*
		 * the URB hasn't been processed
		 * do it now
		 */
		wait_for_completion(&dev->bulk_in_completion);
		dev->bulk_in_copied = 0;
		dev->processed_urb = 1;
	}

	/* errors must be reported */
	rv = dev->errors;
	if (rv < 0) {
		/* any error is reported once */
		dev->errors = 0;
		/* to preserve notifications about reset */
		rv = (rv == -EPIPE) ? rv : -EIO;
		/* no data to deliver */
		dev->bulk_in_filled = 0;
		/* report it */
		goto exit;
	}

	/*
	 * if the buffer is filled we may satisfy the read
	 * else we need to start IO
	 */

	if (dev->bulk_in_filled) {
		/* we had read data */
		size_t available = dev->bulk_in_filled - dev->bulk_in_copied;
		size_t chunk = min(available, count);

		if (!available) {
			/*
			 * all data has been used
			 * actual IO needs to be done
			 */
			rv = skel_do_read_io(dev, count);
			if (rv < 0)
				goto exit;
			else
				goto retry;
		}
		/*
		 * data is available
		 * chunk tells us how much shall be copied
		 */

		if (copy_to_user(buffer,
				 dev->bulk_in_buffer + dev->bulk_in_copied,
				 chunk))
			rv = -EFAULT;
		else
			rv = chunk;

		dev->bulk_in_copied += chunk;

		/*
		 * if we are asked for more than we have,
		 * we start IO but don't wait
		 */
		if (available < count)
			skel_do_read_io(dev, count - chunk);
	} else {
		/* no data in the buffer */
		rv = skel_do_read_io(dev, count);
		if (rv < 0)
			goto exit;
		else if (!(file->f_flags & O_NONBLOCK))
			goto retry;
		rv = -EAGAIN;
	}
exit:
	mutex_unlock(&dev->io_mutex);
	return rv;
}

static void skel_write_bulk_callback(struct urb *urb)
{
	struct usb_skel *dev;

	dev = urb->context;

	/* sync/async unlink faults aren't errors */
	if (urb->status) {
		if (!(urb->status == -ENOENT ||
		    urb->status == -ECONNRESET ||
		    urb->status == -ESHUTDOWN))
			err("%s - nonzero write bulk status received: %d",
			    __func__, urb->status);

		spin_lock(&dev->err_lock);
		dev->errors = urb->status;
		spin_unlock(&dev->err_lock);
	}

	/* free up our allocated buffer */
	usb_free_coherent(urb->dev, urb->transfer_buffer_length,
			  urb->transfer_buffer, urb->transfer_dma);
	up(&dev->limit_sem);
}

static ssize_t skel_write(struct file *file, const char *user_buffer,
			  size_t count, loff_t *ppos)
{
	struct usb_skel *dev;
	int retval = 0;
	struct urb *urb = NULL;
	char *buf = NULL;
	size_t writesize = min(count, (size_t)MAX_TRANSFER);

	dev = file->private_data;

	/* verify that we actually have some data to write */
	if (count == 0)
		goto exit;

	/*
	 * limit the number of URBs in flight to stop a user from using up all
	 * RAM
	 */
	if (!(file->f_flags & O_NONBLOCK)) {
		if (down_interruptible(&dev->limit_sem)) {
			retval = -ERESTARTSYS;
			goto exit;
		}
	} else {
		if (down_trylock(&dev->limit_sem)) {
			retval = -EAGAIN;
			goto exit;
		}
	}

	spin_lock_irq(&dev->err_lock);
	retval = dev->errors;
	if (retval < 0) {
		/* any error is reported once */
		dev->errors = 0;
		/* to preserve notifications about reset */
		retval = (retval == -EPIPE) ? retval : -EIO;
	}
	spin_unlock_irq(&dev->err_lock);
	if (retval < 0)
		goto error;

	/* create a urb, and a buffer for it, and copy the data to the urb */
	urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!urb) {
		retval = -ENOMEM;
		goto error;
	}

	buf = usb_alloc_coherent(dev->udev, writesize, GFP_KERNEL,
				 &urb->transfer_dma);
	if (!buf) {
		retval = -ENOMEM;
		goto error;
	}

	if (copy_from_user(buf, user_buffer, writesize)) {
		retval = -EFAULT;
		goto error;
	}

	/* this lock makes sure we don't submit URBs to gone devices */
	mutex_lock(&dev->io_mutex);
	if (!dev->interface) {		/* disconnect() was called */
		mutex_unlock(&dev->io_mutex);
		retval = -ENODEV;
		goto error;
	}

	/* initialize the urb properly */
	usb_fill_bulk_urb(urb, dev->udev,
			  usb_sndbulkpipe(dev->udev, dev->bulk_out_endpointAddr),
			  buf, writesize, skel_write_bulk_callback, dev);
	urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;
	usb_anchor_urb(urb, &dev->submitted);

	/* send the data out the bulk port */
	retval = usb_submit_urb(urb, GFP_KERNEL);
	mutex_unlock(&dev->io_mutex);
	if (retval) {
		err("%s - failed submitting write urb, error %d", __func__,
		    retval);
		goto error_unanchor;
	}

	/*
	 * release our reference to this urb, the USB core will eventually free
	 * it entirely
	 */
	usb_free_urb(urb);


	return writesize;

error_unanchor:
	usb_unanchor_urb(urb);
error:
	if (urb) {
		usb_free_coherent(dev->udev, writesize, buf, urb->transfer_dma);
		usb_free_urb(urb);
	}
	up(&dev->limit_sem);

exit:
	return retval;
}

static const struct file_operations skel_fops = {
	.owner =	THIS_MODULE,
	.read =		skel_read,
	.write =	skel_write,
	.open =		skel_open,
	.release =	skel_release,
	.flush =	skel_flush,
	.llseek =	noop_llseek,
};

/*
 * usb class driver info in order to get a minor number from the usb core,
 * and to have the device registered with the driver core
 */
static struct usb_class_driver skel_class = {
	.name =		"ks0108usbfb%d",
	.fops =		&skel_fops,
	.minor_base =	USB_SKEL_MINOR_BASE,
};

static struct fb_fix_screeninfo ks0108usbfb_fix __devinitdata = {
	.id = "ks0108usbfb",
	.type = FB_TYPE_PACKED_PIXELS,
	.visual = FB_VISUAL_MONO10,
	.xpanstep = 0,
	.ypanstep = 0,
	.ywrapstep = 0,
	.line_length = 128 / 8,
	.accel = FB_ACCEL_NONE,
};

static struct fb_var_screeninfo ks0108usbfb_var __devinitdata = {
	.xres = 128,
	.yres = 64,
	.xres_virtual = 128,
	.yres_virtual = 64,
	.bits_per_pixel = 1,
	.nonstd = 1,
	.red = { 0, 1, 0 },
      	.green = { 0, 1, 0 },
      	.blue = { 0, 1, 0 },
	.left_margin = 0,
	.right_margin = 0,
	.upper_margin = 0,
	.lower_margin = 0,
	.vmode = FB_VMODE_NONINTERLACED,
};

static int ks0108usbfb_mmap(struct fb_info *info, struct vm_area_struct *vma)
{
	struct usb_skel *dev = info->par;
	return vm_insert_page(vma, vma->vm_start,
		virt_to_page(dev->vbuff));
}

static struct fb_ops ks0108usbfb_ops = {
	.owner = THIS_MODULE,
	.fb_read = fb_sys_read,
	.fb_write = fb_sys_write,
	.fb_fillrect = sys_fillrect,
	.fb_copyarea = sys_copyarea,
	.fb_imageblit = sys_imageblit,
	.fb_mmap = ks0108usbfb_mmap,
};


static int skel_probe(struct usb_interface *interface,
		      const struct usb_device_id *id)
{
	struct usb_skel *dev;
	struct usb_host_interface *iface_desc;
	struct usb_endpoint_descriptor *endpoint;
	size_t buffer_size;
	int i;
	int retval = -ENOMEM;
	int pipe;

 	struct fb_info *info;

	/* allocate memory for our device state and initialize it */
	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev) {
		err("Out of memory");
		goto error;
	}
	kref_init(&dev->kref);
	sema_init(&dev->limit_sem, WRITES_IN_FLIGHT);
	mutex_init(&dev->io_mutex);
	spin_lock_init(&dev->err_lock);
	init_usb_anchor(&dev->submitted);
	init_completion(&dev->bulk_in_completion);

	dev->udev = usb_get_dev(interface_to_usbdev(interface));
	dev->interface = interface;

	/* save our data pointer in this interface device */
	usb_set_intfdata(interface, dev);

	/* we can register the device now, as it is ready */
	retval = usb_register_dev(interface, &skel_class);
	if (retval) {
		/* something prevented us from registering this driver */
		err("Not able to get a minor for this device.");
		usb_set_intfdata(interface, NULL);
		goto error;
	}

	/* let the user know what node this device is now attached to */
	dev_info(&interface->dev,
		 "USB Skeleton device now attached to USBSkel-%d",
		 interface->minor);

	/* allocate framebuffer device */
	info = framebuffer_alloc(0, &dev->udev->dev);

	if (!info)
		goto error;

	info->screen_base = (char __iomem *) dev->vbuff;
	info->screen_size = 128 * 64 / 8;
	info->fbops = &ks0108usbfb_ops;
	info->fix = ks0108usbfb_fix;
	info->var = ks0108usbfb_var;
	info->pseudo_palette = NULL;
	info->par = dev;
	info->flags = FBINFO_FLAG_DEFAULT;

	if (register_framebuffer(info) < 0)
		goto fballoced;

	dev->fb = info;

	printk(KERN_INFO "fb%d: %s frame buffer device\n", info->node,
		info->fix.id);

	/* display checker pattern */
	for (i = 0; i < 128 * 64 / 8; i += 2) {
		dev->ks0108bmp[i] = 0xaa;
		dev->ks0108bmp[i+1] = 0x55;
	}

	/* start updating screen */
	dev->workqueue = create_singlethread_workqueue("ks0108usbfb");
	INIT_DELAYED_WORK((struct delayed_work *)dev, ks0108usbfb_update);
	dev->update = 1;
	ks0108usbfb_update(&dev->wrk);

	return 0;

fballoced:
	framebuffer_release(info);

error:
	if (dev)
		/* this frees allocated memory */
		kref_put(&dev->kref, skel_delete);
	return retval;
}

static void skel_disconnect(struct usb_interface *interface)
{
	struct usb_skel *dev;
	int minor = interface->minor;

	dev = usb_get_intfdata(interface);
	usb_set_intfdata(interface, NULL);

	/* give back our minor */
	usb_deregister_dev(interface, &skel_class);

	/* prevent more I/O from starting */
	mutex_lock(&dev->io_mutex);
	dev->interface = NULL;
	mutex_unlock(&dev->io_mutex);

	usb_kill_anchored_urbs(&dev->submitted);

	/* decrement our usage count */
	kref_put(&dev->kref, skel_delete);

	dev_info(&interface->dev, "USB Skeleton #%d now disconnected", minor);
}

static void skel_draw_down(struct usb_skel *dev)
{
	int time;

	time = usb_wait_anchor_empty_timeout(&dev->submitted, 1000);
	if (!time)
		usb_kill_anchored_urbs(&dev->submitted);
	usb_kill_urb(dev->bulk_in_urb);
}

static int skel_suspend(struct usb_interface *intf, pm_message_t message)
{
	struct usb_skel *dev = usb_get_intfdata(intf);

	if (!dev)
		return 0;
	skel_draw_down(dev);
	return 0;
}

static int skel_resume(struct usb_interface *intf)
{
	return 0;
}

static int skel_pre_reset(struct usb_interface *intf)
{
	struct usb_skel *dev = usb_get_intfdata(intf);

	mutex_lock(&dev->io_mutex);
	skel_draw_down(dev);

	return 0;
}

static int skel_post_reset(struct usb_interface *intf)
{
	struct usb_skel *dev = usb_get_intfdata(intf);

	/* we are sure no URBs are active - no locking needed */
	dev->errors = -EPIPE;
	mutex_unlock(&dev->io_mutex);

	return 0;
}

static struct usb_driver skel_driver = {
	.name =		"ks0108usbfb",
	.probe =	skel_probe,
	.disconnect =	skel_disconnect,
	.suspend =	skel_suspend,
	.resume =	skel_resume,
	.pre_reset =	skel_pre_reset,
	.post_reset =	skel_post_reset,
	.id_table =	skel_table,
	.supports_autosuspend = 1,
};

static int __init usb_skel_init(void)
{
	int result;

	/* register this driver with the USB subsystem */
	result = usb_register(&skel_driver);
	if (result)
		err("usb_register failed. Error number %d", result);

	return result;
}

static void __exit usb_skel_exit(void)
{
	/* deregister this driver with the USB subsystem */
	usb_deregister(&skel_driver);
}

module_init(usb_skel_init);
module_exit(usb_skel_exit);

MODULE_LICENSE("GPL");
