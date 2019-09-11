/****************************************************************************
 *  License : All rights reserved for TES Electronic Solutions GmbH
 *	      See included /docs/license.txt for details
 *  Project : D/AVE 2D
 *  Purpose : Device and module specific defines
 ****************************************************************************
 * Version Control Information :
 *  $Revision: $
 *  $Date: $
 *  $LastChangedBy: $
 ****************************************************************************/

#ifndef TES_DAVE_MODULE_H_
#define TES_DAVE_MODULE_H_
#include <linux/cdev.h>
#include <linux/device.h>

/* Character device settings */
#define DAVE2D_DEVICE_NAME		    "dave2d"
#define DAVE2D_DEVICE_CLASS		    "dave2d"
#define DAVE2D_DEVICE_CNT		    1

/* Device Tree Identifiers */
#define DAVE2D_OF_TYPE			    "renderer"
#define DAVE2D_OF_COMPATIBLE		    "tes,dave2d-1.0"

/* DAVE2D register mappings needed by this driver */
#define DAVE2D_REG_CONTROL1		    (0)
#define DAVE2D_REG_STATUS		    0
#define DAVE2D_REG_IRQCTL		    (48)
#define DAVE2D_REG_HWREVISION		    1
#define REG_HWREVISION_HV(hwr)		    (hwr & ((1 << 12) - 1))
#define REG_HWREVISION_TYPE(hwr)	    (hwr >> 12 & ((1 << 4) - 1))
#define DAVE2D_REG_STATUS_IRQ_DLIST	    (0x20)
#define DAVE2D_REG_STATUS_IRQ_ENUM	    (0x10)
#define DAVE2D_REG_IRQCTL_ENABLE_DLIST_IRQ  (0x02)
#define DAVE2D_REG_IRQCTL_CLR_DLIST_IRQ     (0x08)

/* Register access macros */
#define DAVE2D_IO_WREG(addr,data)	    iowrite32(data,addr)
#define DAVE2D_IO_RREG(addr)		    ioread32(addr)
#define DAVE2D_IO_RADDR(base,reg)	    ((void*)((unsigned long)base|((unsigned long)reg)<<2))

/* DAVE2D device info struct */
struct dave2d_dev
{
	unsigned long base_phys;
	unsigned long span;
	unsigned long mem_base_phys;
	unsigned long mem_span;
	void *base_virt;
	void *mem_base_virt;
	unsigned int irq_no;
	unsigned int irq_stat;
	spinlock_t irq_slck;
	wait_queue_head_t irq_waitq;
	dev_t dev;
	struct cdev cdev;
	struct device *device;
};

#endif /* TES_DAVE_MODULE_H_ */
