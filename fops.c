#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/cdev.h>
#include <linux/semaphore.h>
#include <linux/sched.h>
#include <linux/ioctl.h>
#include <linux/poll.h>
#include <asm/uaccess.h>
#include "scull.h"

static int spacefree(struct scull_pipe *dev){
	if(dev->rp == dev->wp)
		return dev->buffersize -1;
	return ((dev->rp + dev->buffersize - dev->wp) % dev->buffersize - 1);
}

/* Wait for space for writing; caller must acquire dev semaphore before call 
 * this function.
 * On error, the semaphore will be released before returning
 */
static int scull_getwritespace(struct scull_pipe *dev, struct file *filp)
{
	while(spacefree(dev) == 0){ /* full */
		DEFINE_WAIT(wait);

		up(&dev->sem);
		if(filp->f_flags & O_NONBLOCK)
			return -EAGAIN;
		printk("\"%s\" writing: going to sleep\n", current->comm);

		prepare_to_wait(&dev->outq, &wait, TASK_INTERRUPTIBLE);
		if(spacefree(dev) == 0)
			schedule();
		finish_wait(&dev->outq, &wait);
		if(signal_pending(current))
			return -ERESTARTSYS; /* signal received, tell fs layer to handle this */
		if(down_interruptible(&dev->sem))
			return -ERESTARTSYS;
	}
	return 0;
}

int scull_p_open(struct inode *inode, struct file *filp)
{
	struct scull_pipe *dev; /* Device information */
	unsigned int flags = filp->f_flags;	
	dev = container_of(inode->i_cdev, struct scull_pipe, cdev);
	filp->private_data = dev; /* for other methods */

        if ((flags & O_ACCMODE) == O_RDONLY){ /* PQ ACCMODE ??? */

		dev->nreaders++;
		if(!dev->nwriters){
			if(flags & O_NONBLOCK)
				return -EWOULDBLOCK;

			if(wait_event_interruptible(dev->inq, dev->nwriters))
				return -ERESTARTSYS;
		}
		goto out;
	}

	if (flags & O_WRONLY){
		dev->nwriters++;
		wake_up_interruptible(&dev->inq);
		goto out;
	}

	if (flags & O_RDWR){
		dev->nreaders++;
		dev->nwriters++;
		wake_up_interruptible(&dev->inq);
		goto out;
	}

	out:
	return 0; /*success*/
}

/* Nothing special to do, since we don't have any hardware to shutdown for while */
int scull_p_release(struct inode *inode, struct file *filp)
{
	return 0;
}

static ssize_t scull_p_read(struct file *filp, char __user *buf, size_t count,
		loff_t *f_pos)
{
	struct scull_pipe *dev = filp->private_data;

	if(down_interruptible(&dev->sem))
		return -ERESTARTSYS;

	if(!dev->nwriters)
		return 0; /*nothing being written, just return */

	while(dev->rp == dev->wp) { /*Nothing to read*/
		up(&dev->sem); /* release the lock */
		
		if(filp->f_flags & O_NONBLOCK)
			return -EAGAIN;
		printk(KERN_WARNING "\"%s\" Reading: going to sleep\n", current->comm);

		if(wait_event_interruptible(dev->inq, (dev->rp != dev->wp)))
			return -ERESTARTSYS; /* signal received: tell fs layer to handle it */
		/* loop otherwise, but first reacquire the lock */
		if(down_interruptible(&dev->sem))
			return -ERESTARTSYS;
	}

	/* ok, there is data there, return something */
	if (dev->wp > dev->rp)
		count = min(count, (size_t)(dev->wp - dev->rp));
	else /*the write pointer was wrapped, returning up to dev->end */
		count = min(count, (size_t)(dev->end - dev->rp));
	
	if(copy_to_user(buf, dev->rp, count)){
		up(&dev->sem);
		return -EFAULT;
	}
	dev->rp += count;
	
	if(dev->rp == dev->end)
		dev->rp = dev->buffer; /*wrap*/
	up(&dev->sem);

	/*Finally, wake writers and return */
	
	wake_up_interruptible(&dev->outq);
	printk(KERN_WARNING "\"%s\" did read %li bytes\n",current->comm, (long)count);
	return count;
}

static ssize_t scull_p_write(struct file *filp, const char __user *buf, size_t count,
		loff_t *f_pos)
{
	struct scull_pipe *dev = filp->private_data;
	int result;

	if(down_interruptible(&dev->sem))
		return -ERESTARTSYS;

	/* Make sure there's space to write */
	result = scull_getwritespace(dev, filp);

	if(result)
		return result; /* scull_getwritespace called up(&dev->sem)*/

	/* Space is there, accept something */

	count = min(count, (size_t)spacefree(dev));
	
	if(dev->wp >= dev->rp)
		count = min(count, (size_t)(dev->end - dev->wp)); /* to end-of-buf */
	else	/* the write pointer has wrapped, fill up to rp-1 */
		count = min(count, (size_t)(dev->rp - dev->wp -1));
	printk(KERN_WARNING "Goint to accept %li bytes to %p from %p\n", (long)count, dev->wp, buf);

	if(copy_from_user(dev->wp, buf, count)){
		up(&dev->sem);
		return -EFAULT;
	}
	dev->wp += count;
	if(dev->wp == dev->end)
		dev->wp = dev->buffer; /* wrapped */
	up(&dev->sem);

	/* finally, awake any reader */
	wake_up_interruptible(&dev->inq); /* blocked in read() and select() */

	/* and signal asynchronous readers */
	if(dev->async_queue)
		kill_fasync(&dev->async_queue, SIGIO, POLL_IN);
	printk(KERN_WARNING "\"%s\" did write %li bytes\n", current->comm, (long)count);
	return count;
}

/* Temporary and badly commented, I'll fix the ioctl call 
 * when I have time
 */

//int scull_p_ioctl(struct file *filp, unsigned int cmd, unsigned long arg){
//
//       int retval = 0, tmp, err = 0;
//
//        /* extract the type and number bitfields, and don't
//         * decode wrong cmds: return ENOTTY before access_ok()
//         */
//        if(_IOC_TYPE(cmd) != SCULL_IOC_MAGIC)
//                return -ENOTTY;
//
//        if(_IOC_NR(cmd) > SCULL_IOC_MAXNR)
//                return -ENOTTY;
//
//       /* the direction field is a bitmask (2 bits), and
//        * VERIFY_WRITE catches R/W transfers. 'direction'
//        * bitfield is user-oriented, while acces_ok() is
//         * kernel-oriented, so the concept of "read" and
//         * "write" is reversed
//         */
//
//        /* access_ok() returns non-zero as success and 0
//	 * as error
//         */
//
//        if(_IOC_DIR(cmd) & _IOC_READ)
//                err = !access_ok(VERIFY_WRITE, (void __user*)arg, _IOC_SIZE(cmd));
//
//        else if(_IOC_DIR(cmd) & _IOC_WRITE)
//                err = !access_ok(VERIFY_READ, (void __user*)arg, _IOC_SIZE(cmd));
//
//        if(err)
//                return -EFAULT;
//
//        switch(cmd){
//
//               case SCULL_IOCRESET:
//                        scull_quantum = SCULL_QUANTUM;
//                        scull_qset = SCULL_QSET;
//                       break;
//
//                case SCULL_IOCSQUANTUM: /* Set: arg points to the value */
//                        if(!capable(CAP_SYS_ADMIN))
//                                return -EPERM;
//
//                        retval = __get_user(scull_quantum, (int __user*)arg);
//                        break;
//
//                case SCULL_IOCTQUANTUM: /* Tell: arg is the value */
//                        if(!capable(CAP_SYS_ADMIN))
//                                return -EPERM;
//
//                        scull_quantum = arg;
//                        break;
//
//                case SCULL_IOCGQUANTUM: /* Get: arg is pointer to result */
//                        retval = __put_user(scull_quantum, (int __user*)arg);
//                       break;
//
//                case SCULL_IOCQQUANTUM: /* Query: return it (it's positive) */
//                        return scull_quantum;
//
//                case SCULL_IOCXQUANTUM: /* eXchange: use arg as pointer */
//                        if(!capable(CAP_SYS_ADMIN))
//                                return -EPERM;
//                        tmp = scull_quantum;
//                        retval = __get_user(scull_quantum, (int __user*)arg);
//                        if(retval == 0)
//                                retval = __put_user(tmp, (int __user *)arg);
//                        break;
//
//                case SCULL_IOCHQUANTUM: /* sHift: like Tell + Query */
//                        if(!capable(CAP_SYS_ADMIN))
//                                return -EPERM;
//                        tmp = scull_quantum;
//                        scull_quantum = arg;
//                       return tmp;
//
//                default: /* Redundant, as cmd was checked against MAXNR */
//			return -ENOTTY;
//			
//        }
//        return retval;
//
//}

/*static unsigned int scull_p_poll(struct file *filp, struct poll_table *wait)
{
	struct scull_pipe *dev = filp->private_data;
	unsigned int mask = 0;

	*
 	 * The buffer is circular; it is considered full
 	 * if "wp" is right behind "rp" and empty if
 	 * "wp" is equal "rp".
 	 *

	down(&dev->sem);
	poll_wait(filp, &dev->inq, wait);
	poll_wait(filp, &dev->outq, wait);
	
	if (dev->rp != dev->wp)
		mask |= (POLLIN | POLLRDNORM);
	if (spacefree(dev))
		mask |= (POLLOUT | POLLWRNORM);
	if (!dev->nwriters)
		mask |= POLLHUP;
	up(&dev->sem);
	return mask;
}*/

struct file_operations scull_fops = {
	.owner = THIS_MODULE,
	/*.llseek = scull_llseek,*/
	.read = scull_p_read,
	.write = scull_p_write,
	.open = scull_p_open,
	.release = scull_p_release,
	/*.unlocked_ioctl = scull_p_ioctl,*/
	//.poll = scull_p_poll,
};
