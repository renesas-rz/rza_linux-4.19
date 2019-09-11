/****************************************************************************
 *  License : All rights reserved for TES Electronic Solutions GmbH
 *	      See included /docs/license.txt for details
 *  Project : D/AVE 2D
 *  Purpose : Interrupt handling for kernel driver. Userspace signalling 
 *	      by blocking reads.
 ****************************************************************************
 * Version Control Information :
 *  $Revision: $
 *  $Date: $
 *  $LastChangedBy: $
 ****************************************************************************/

#include <linux/fs.h>
#include <linux/interrupt.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/uaccess.h>
#include <linux/spinlock.h>
#include <asm/io.h>
#include "tes_dave.h"
#include "tes_dave_module.h"
#include "tes_dave_irq.h"

/* Basic IRQ implementation, handles only DLIST IRQ so far as the official
 * driver does not use others.
 */
irqreturn_t std_irq_handler(int irq, void *dev_id)
{
	struct dave2d_dev *dev = dev_id;
	unsigned long flags;
	unsigned int sreg;

	sreg = DAVE2D_IO_RREG(DAVE2D_IO_RADDR(dev->base_virt, DAVE2D_REG_STATUS))
		& (DAVE2D_REG_STATUS_IRQ_DLIST | DAVE2D_REG_STATUS_IRQ_ENUM);

	if(!(sreg & DAVE2D_REG_STATUS_IRQ_DLIST))
	{
		dev_err(dev->device, "Spurious IRQ: IRQ_DLIST not set!\n");
	}
	else { 
		/* clear and leave enabled */
		DAVE2D_IO_WREG(DAVE2D_IO_RADDR(dev->base_virt, DAVE2D_REG_IRQCTL),
			DAVE2D_REG_IRQCTL_ENABLE_DLIST_IRQ |
			DAVE2D_REG_IRQCTL_CLR_DLIST_IRQ);

		spin_lock_irqsave(&dev->irq_slck, flags);
		dev->irq_stat = sreg;
		spin_unlock_irqrestore(&dev->irq_slck, flags);

		/* notify userspace driver by unblocking read */
		wake_up_interruptible(&dev->irq_waitq);
	}
	return IRQ_HANDLED;
}

int register_irq(struct dave2d_dev *dev)
{
	if(request_irq(dev->irq_no, std_irq_handler, 0, "TES DAVE2D", (void*) dev))
	{
		dev_err(dev->device, "irq cannot be registered\n");
		return -EBUSY;
	}

	DAVE2D_IO_WREG(DAVE2D_IO_RADDR(dev->base_virt, DAVE2D_REG_IRQCTL),
			DAVE2D_REG_IRQCTL_ENABLE_DLIST_IRQ);

	return 0;
}

void unregister_irq(struct dave2d_dev *dev)
{
	/* disable/clear IRQs first */
	DAVE2D_IO_WREG(DAVE2D_IO_RADDR(dev->base_virt, DAVE2D_REG_IRQCTL),
			DAVE2D_REG_IRQCTL_CLR_DLIST_IRQ);

	free_irq(dev->irq_no, (void*) dev);
}

ssize_t dave2d_read(struct file *fp, char __user *buff, size_t count, loff_t *offp)
{
	struct dave2d_dev *dev = fp->private_data;
	unsigned long flags;
	int temp;

	wait_event_interruptible(dev->irq_waitq, dev->irq_stat);

	spin_lock_irqsave(&dev->irq_slck, flags);
	temp = dev->irq_stat;
	dev->irq_stat = 0;
	spin_unlock_irqrestore(&dev->irq_slck, flags);

	if(count==sizeof(temp))
	{
		put_user(temp, buff);
		return sizeof(temp);
	}

	return 0;
}
