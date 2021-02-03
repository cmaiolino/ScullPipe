# If KERNELRELEASE is defined, we've been invoked from the
# kernel build system and can use its language.

#DEBUG SESSION:

#comment/uncomment the following line to disable/enable debug messages */
DEBUG = y

#add or not debug flags to CFLAGS

ifeq ($(DEBUG),y)
 DEBFLAGS = -O -g -DDEBUG_MODE # "-O" expand inlines
else
 DEBFLAGS = -O2
endif

EXTRA_CFLAGS+= $(DEBFLAGS)

ifneq ($(KERNELRELEASE),)
	scull_pipe-objs := devices.o fops.o main.o
	obj-m := scull_pipe.o#module target to be compiled


# Otherwise we were called directly from the command
# line; invoke the kernel build system. 
else

	KERNELDIR ?= /lib/modules/$(shell uname -r)/build
	PWD := $(shell pwd)

default:
	#make -C /lib/modules/2.6.34.7-56.fc13.i686.PAE/build M=/home/cmaiolino/Sources/LDD3/ modules
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules

endif

clean:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) clean
