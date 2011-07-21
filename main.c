#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h> /* Parameters definition */
#include <linux/kdev_t.h>
#include <linux/proc_fs.h>
#include <linux/semaphore.h>
#include "scull.h"

unsigned int scull_major = SCULL_MAJOR;
unsigned int scull_minor = SCULL_MINOR;
unsigned int scull_nr_devs = SCULL_NR_DEVS; /* number of devices */
/*unsigned int scull_quantum = SCULL_QUANTUM;*/
/*unsigned int scull_qset = SCULL_QSET;*/

/* Device list */
struct scull_pipe *scull_devices;

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
	dev->end = dev->buffer + SCULL_BUFFER;
	dev->buffersize = SCULL_BUFFER;
	dev->rp = dev->buffer;
	dev->wp = dev->buffer;
	err = cdev_add(&dev->cdev, devno, 1);
	dev->async_queue = NULL; /* not in use yet */

		 /*FIXME: should fail gracefully*/
	if(err)
		printk(KERN_NOTICE "Error %d adding scull%d",err, index);
}
	


static int scull_init(void)
{
	
	int i;

	printk("Starting scull module...\n");
	create_dev();

	scull_devices = kmalloc(sizeof(struct scull_pipe) * scull_nr_devs,GFP_KERNEL);
	memset(scull_devices,0,sizeof(struct scull_pipe)*scull_nr_devs);
	
	for(i=0; i<scull_nr_devs;i++){
		sema_init(&scull_devices[i].sem,1);
		scull_setup_cdev(&scull_devices[i],i);
	}

	return 0;
}

static void scull_exit(void)
{
	printk("Unloading scull module...\n");
}

module_init(scull_init);
module_exit(scull_exit);

/* Module information */

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Carlos Eduardo Maiolino");
