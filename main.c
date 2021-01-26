#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h> /* Parameters definition */
#include <linux/kdev_t.h>
#include <linux/proc_fs.h>
#include <linux/semaphore.h>
#include <linux/slab.h> /* kmalloc, kfree */
#include "scull.h"

unsigned int scull_major = SCULL_MAJOR;
unsigned int scull_minor = SCULL_MINOR;
unsigned int scull_nr_devs = SCULL_NR_DEVS; /* number of devices */

/* Device list */
struct scull_pipe *scull_devices;

/* This function is called whenever a cdev setup fail */

static int scull_fail_dev(struct scull_pipe *dev)
{
	kfree(dev->buffer);
	cdev_del(&dev->cdev);
	return 0;
}
/* Create devices */
static void scull_setup_cdev(struct scull_pipe *dev, int index)  /*dev struct not yet initialized into the code. FIXME*/ 
{
	int err, devno = MKDEV(scull_major, scull_minor + index);

	cdev_init(&dev->cdev, &scull_fops);
	dev->cdev.owner = THIS_MODULE;
	dev->cdev.ops = &scull_fops;
	init_waitqueue_head(&dev->inq);  /* Init read wait queue */
	init_waitqueue_head(&dev->outq); /* Init write wait queue */
	
	dev->buffer = kmalloc(SCULL_BUFFER, GFP_KERNEL);
	if(!dev->buffer){
		printk(KERN_NOTICE "Error allocating memory to device %d\n", index);
		return;
	}

	dev->end = dev->buffer + SCULL_BUFFER;
	dev->buffersize = SCULL_BUFFER;
	dev->rp = dev->buffer;
	dev->wp = dev->buffer;
	dev->nwriters = 0;
	dev->nreaders = 0;

	#if 0 
	dev->async_queue = fasync_alloc();
	#endif

	err = cdev_add(&dev->cdev, devno, 1);
	if(err){
		printk(KERN_NOTICE "Error %d adding scull%d. Cleaning",err, index);
		scull_fail_dev(dev);
	}
}
	


static int scull_init(void)
{
	
	int i;

	printk("Starting scull module...\n");
	if(create_dev() < 0)
		goto fail;

	scull_devices = kmalloc(sizeof(struct scull_pipe) * scull_nr_devs,GFP_KERNEL);
	if(!scull_devices)
		goto fail;

	memset(scull_devices,0,sizeof(struct scull_pipe)*scull_nr_devs);
	
	for(i=0; i<scull_nr_devs;i++){
		sema_init(&scull_devices[i].sem,1);
		scull_setup_cdev(&scull_devices[i],i);
	}

	return 0;

	fail:
		return -ENOMEM;		
}

static void scull_exit(void)
{
	int i = 0;

	printk("Unloading scull module...\n");
	if(scull_devices){
		for(i=0;i<scull_nr_devs; i++){
			printk(KERN_WARNING "removing dev %i..\n",i);
			unregister_chrdev_region(scull_devices[i].cdev.dev, scull_nr_devs);
			cdev_del(&scull_devices[i].cdev);
		}
	}
}

module_init(scull_init);
module_exit(scull_exit);

/* Module information */

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Carlos Eduardo Maiolino");
