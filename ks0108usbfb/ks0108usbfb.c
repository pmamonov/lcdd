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
	struct kref		kref;
	unsigned char		ks0108bmp[128 * 64 / 8];
	unsigned char		vbuff[128 * 64 / 8];
	struct fb_info		*fb;
	struct workqueue_struct	*workqueue;
	char			update;
	struct mutex		fb_mutex;
};
#define to_skel_dev(d) container_of(d, struct usb_skel, kref)

static struct usb_driver skel_driver;

static void ks0108usbfb_update(struct work_struct *wrk)
{
	struct usb_skel *dev = (struct usb_skel *)wrk;
	int pipe = usb_sndctrlpipe(dev->udev, 0);
	int byte, bit, x, y;
	unsigned char b;

	mutex_lock(&dev->fb_mutex);

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
		queue_delayed_work(dev->workqueue, &dev->wrk, HZ / 10);
	mutex_unlock(&dev->fb_mutex);
}

static void update_start(struct usb_skel *dev)
{
	dev->update = 1;
	ks0108usbfb_update(&dev->wrk.work);
}

static void update_stop(struct usb_skel *dev)
{
	mutex_lock(&dev->fb_mutex);
	dev->update = 0;
	cancel_delayed_work(&dev->wrk);
	flush_workqueue(dev->workqueue);
	mutex_unlock(&dev->fb_mutex);
}

static void skel_delete(struct kref *kref)
{
	struct usb_skel *dev = to_skel_dev(kref);

	usb_put_dev(dev->udev);
	if (dev->fb) {
		update_stop(dev);
		destroy_workqueue(dev->workqueue);
		unregister_framebuffer(dev->fb);
		framebuffer_release(dev->fb);
	}
	kfree(dev);
}

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
	int i;
	int retval = -ENOMEM;

 	struct fb_info *info;

	/* allocate memory for our device state and initialize it */
	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev) {
		err("Out of memory");
		goto error;
	}
	kref_init(&dev->kref);

	dev->udev = usb_get_dev(interface_to_usbdev(interface));
	dev->interface = interface;

	/* save our data pointer in this interface device */
	usb_set_intfdata(interface, dev);

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
	for (i = 0; i < 128 * 64 / 8; i++) {
		if (i / (128 / 8) % 2)
			dev->vbuff[i] = 0xaa;
		else
			dev->vbuff[i] = 0x55;
	}

	/* start updating screen */
	dev->workqueue = create_singlethread_workqueue("ks0108usbfb");
	INIT_DELAYED_WORK((struct delayed_work *)dev, ks0108usbfb_update);
	mutex_init(&dev->fb_mutex);
	update_start(dev);

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

	/* prevent more I/O from starting */
	mutex_lock(&dev->fb_mutex);
	dev->interface = NULL;
	mutex_unlock(&dev->fb_mutex);

	/* decrement our usage count */
	kref_put(&dev->kref, skel_delete);

	dev_info(&interface->dev, "USB Skeleton #%d now disconnected", minor);
}

static int skel_suspend(struct usb_interface *intf, pm_message_t message)
{
	struct usb_skel *dev = usb_get_intfdata(intf);
	if (!dev)
		return 0;
	update_stop(dev);
	return 0;
}

static int skel_resume(struct usb_interface *intf)
{
	struct usb_skel *dev = usb_get_intfdata(intf);
	if (!dev)
		return 0;
	update_start(dev);
	return 0;
}

static int skel_pre_reset(struct usb_interface *intf)
{
	struct usb_skel *dev = usb_get_intfdata(intf);

	mutex_lock(&dev->fb_mutex);

	return 0;
}

static int skel_post_reset(struct usb_interface *intf)
{
	struct usb_skel *dev = usb_get_intfdata(intf);

	/* we are sure no URBs are active - no locking needed */
	mutex_unlock(&dev->fb_mutex);

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
