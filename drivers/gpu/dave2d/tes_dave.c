/****************************************************************************
 *	License : All rights reserved for TES Electronic Solutions GmbH
 *			  See included /docs/license.txt for details
 *	Project : D/AVE 2D
 *	Purpose : Main linux kernel module. Handles initialization and fops.
 ****************************************************************************
 * Version Control Information :
 *	$Revision: $
 *	$Date: $
 *	$LastChangedBy: $
 ****************************************************************************/

#include <linux/init.h>
#include <linux/module.h>
#include <linux/ioport.h>
#include <linux/types.h>
#include <linux/kdev_t.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/mm.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include "tes_dave.h"
#include "tes_dave_module.h"
#include "tes_dave_irq.h"

/* store class globally. So far, this module does not fully support multiple
 * dave instances but still... */
struct class *dave_class;

/* fops functions */
static int dave2d_open(struct inode *ip, struct file *fp)
{
	struct dave2d_dev *dev;

	/* extract the device structure and add it to the file pointer for easier
	 * access */
	dev = container_of(ip->i_cdev, struct dave2d_dev, cdev);
	fp->private_data = dev;

	return 0;
}

static int dave2d_mmap(struct file *fp, struct vm_area_struct *vma)
{
	struct dave2d_dev *dev = fp->private_data;
	pgprot_t prot;
	/* none-cached but write combined */
	prot = pgprot_writecombine(vma->vm_page_prot);

	if (io_remap_pfn_range(vma, vma->vm_start,
				dev->mem_base_phys>>PAGE_SHIFT,
				vma->vm_end - vma->vm_start,
				prot))
	{
		dev_err(fp->private_data,
			"Error while remapping IO memory to userspace\n");
		return -EAGAIN;
	}

	return 0;
}

static long dave2d_ioctl(struct file *fp, unsigned int cmd, unsigned long arg)
{
	struct dave2d_dev *dev = fp->private_data;
	unsigned int cmd_nr;
	dave2d_settings dset;

	if (_IOC_TYPE(cmd)!=DAVE2D_IOCTL_TYPE)
		return -ENOTTY;

	cmd_nr = _IOC_NR(cmd);
	if (_IOC_DIR(cmd) == _IOC_WRITE)
	{
		if (cmd_nr & DAVE2D_IOCTL_REG_PREFIX)
		{
			/* direct register write: Register value in argument */
			DAVE2D_IO_WREG(DAVE2D_IO_RADDR(dev->base_virt,
						DAVE2D_IOCTL_GET_REG(cmd_nr)),
					arg);
			return 0;
		}
	}
	else if (_IOC_DIR(cmd) == _IOC_READ)
	{
		if(cmd_nr & DAVE2D_IOCTL_REG_PREFIX)
		{ /* direct register read: Argument is a pointer */
			if(!put_user(DAVE2D_IO_RREG(DAVE2D_IO_RADDR(dev->base_virt,
						DAVE2D_IOCTL_GET_REG(cmd_nr))),
					(unsigned long*) arg))
				return -EFAULT;
			return 0;
		}

		switch(cmd_nr)
		{
		case DAVE2D_IOCTL_NR_SETTINGS:
			dset.base_phys = dev->base_phys;
			dset.span = dev->span;
			dset.mem_base_phys = dev->mem_base_phys;
			dset.mem_span = dev->mem_span;

			if(copy_to_user((void*) arg, (void*) &dset,
						sizeof(dave2d_settings)))
			{
				dev_err(fp->private_data,
					"error while copying settings to user space\n");
				return -EFAULT;
			}

			break;

		default:
			return -EINVAL;
		}
	}
	return -EINVAL;
}

static struct file_operations dave2d_fops = {
	.owner = THIS_MODULE,
	.open = dave2d_open,
	.mmap = dave2d_mmap,
	.unlocked_ioctl = dave2d_ioctl,
	.read = dave2d_read,
};

/* create character class and devices */
static int dave2d_setup_device(struct dave2d_dev *dev)
{
	int result = 0;

	result = alloc_chrdev_region(&dev->dev, 0, DAVE2D_DEVICE_CNT,
			DAVE2D_DEVICE_NAME);

	if (result < 0) {
		dev_err(dev->device, "can't alloc_chrdev_region\n");
		goto CHR_FAILED;
	}

	dave_class = class_create(THIS_MODULE, DAVE2D_DEVICE_CLASS);
	if(!dave_class)
	{
		dev_err(dev->device, "cannot create class %s\n", DAVE2D_DEVICE_CLASS);
		result = -EBUSY;
		goto CLASS_FAILED;
	}

	dev->device = device_create(dave_class, NULL, dev->dev, dev, DAVE2D_DEVICE_NAME);
	if(!dev->device)
	{
		dev_err(dev->device, "cannot create device %s\n", DAVE2D_DEVICE_NAME);
		result = -EBUSY;
		goto DEVICE_FAILED;
	}

	cdev_init(&dev->cdev, &dave2d_fops);
	dev->cdev.owner = THIS_MODULE;

	result = cdev_add(&dev->cdev, dev->dev, DAVE2D_DEVICE_CNT);
	if(result)
	{
		dev_err(dev->device, "can't register char device\n");
		goto DEV_FAILED;
	}

	return 0;

DEV_FAILED:
	device_destroy(dave_class, dev->dev);
DEVICE_FAILED:
	class_destroy(dave_class);
CLASS_FAILED:
	unregister_chrdev_region(dev->dev, DAVE2D_DEVICE_CNT);
CHR_FAILED:

	return result;
}

static void dave2d_log_params(struct dave2d_dev *dev)
{
	dev_info(dev->device, "Base address:\t0x%08lx - 0x%08lx\n",
			dev->base_phys, dev->base_phys + dev->span);
	dev_info(dev->device, "Video RAM:\t0x%08lx - 0x%08lx\n",
			dev->mem_base_phys, dev->mem_base_phys + dev->mem_span);
	dev_info(dev->device, "IRQ:\t%d\n", dev->irq_no);
}

static void dave2d_shutdown_device(struct dave2d_dev *dev)
{
	device_destroy(dave_class, dev->dev);
	class_destroy(dave_class);
	unregister_chrdev_region(dev->dev, DAVE2D_DEVICE_CNT);
	cdev_del(&dev->cdev);
}

/* platform device functions:
 * on probe (new device), copy all neccessary data from device tree description
 * to local data structure and initialize the driver part.
 * */
static int dave2d_probe(struct platform_device *pdev)
{
	struct device_node *np;
	struct resource rsrc;
	const void *ptr;
	int len;
	int result = 0;
	struct dave2d_dev *dave;

	dave = devm_kzalloc(&pdev->dev, sizeof(struct dave2d_dev), GFP_KERNEL);
	if(!dave)
	{
		dev_err(&pdev->dev,
			"Memory allocation for driver struct failed!\n");
		return -ENOMEM;
	}
	platform_set_drvdata(pdev, dave);
	dave->device = &pdev->dev;
	np = pdev->dev.of_node;
	if(!np)
	{
		dev_err(&pdev->dev,
			"driver should only be instanciated over device tree!\n");
		return -ENODEV;
	}

	of_address_to_resource(np, 0, &rsrc);
	dave->base_phys = rsrc.start;
	dave->span = rsrc.end - rsrc.start;
	dave->irq_no = of_irq_to_resource(np, 0, &rsrc);

	ptr = of_get_property(np, "vidmem", &len);
	if(!ptr || len != 8)
	{
		dev_err(&pdev->dev,
			"vidmem property in device tree missing or incorrect. Please specify vidmem = < base size >!\n");
		return -ENODEV;
	}

	dave->mem_base_phys = be32_to_cpup(ptr);
	dave->mem_span = be32_to_cpup(ptr + 4) - 1;

	dave2d_log_params(dave);

	spin_lock_init(&dave->irq_slck);
	init_waitqueue_head(&dave->irq_waitq);

	if (!request_mem_region(dave->base_phys, dave->span, "TES DAVE2D"))
	{
		dev_err(&pdev->dev, "memory region already in use\n");
		return -EBUSY;
	}

	if (!request_mem_region(dave->mem_base_phys, dave->mem_span,
				"DAVE2D Video RAM"))
	{
		dev_err(&pdev->dev, "video memory region already in use\n");
		result = -EBUSY;
		goto REQ_VID_FAILED;
	}

	dave->base_virt = ioremap_nocache(dave->base_phys, dave->span);
	if (!dave->base_virt)
	{
		dev_err(&pdev->dev, "ioremap failed\n");
		result = -EBUSY;
		goto IO_FAILED;
	}

	dave->mem_base_virt = ioremap_nocache(dave->mem_base_phys, dave->mem_span);
	if (!dave->mem_base_virt)
	{
		dev_err(&pdev->dev, "vidmem ioremap failed\n");
		result = -EBUSY;
		goto IO_VID_FAILED;
	}

	result = DAVE2D_IO_RREG(DAVE2D_IO_RADDR(dave->base_virt,
				DAVE2D_REG_HWREVISION));
	if(REG_HWREVISION_TYPE(result) > 1)
	{
		dev_warn(&pdev->dev,
				"Unknown DAVE2D found. Identifier: 0x%08X\n",
				result);
	}
	else
	{
		dev_info(&pdev->dev, "Found DAVE2D rev. 0x%08X (%s)\n",
				REG_HWREVISION_HV(result), REG_HWREVISION_TYPE(result) ?
				"D/AVE 2D-TL" : "D/AVE 2D-TS");
	}

	result = dave2d_setup_device(dave);
	if(result)
	{
		goto DEV_FAILED;
	}

	/* register dave IRQs */
	result = register_irq(dave);
	if(result)
	{
		dev_err(&pdev->dev, "can't register irq %d\n", dave->irq_no);
		goto IRQ_FAILED;
	}

	return 0;

IRQ_FAILED:
	dave2d_shutdown_device(dave);
DEV_FAILED:
	iounmap(dave->mem_base_virt);
IO_VID_FAILED:
	iounmap(dave->base_virt);
IO_FAILED:
	release_mem_region(dave->mem_base_phys, dave->mem_span);
REQ_VID_FAILED:
	release_mem_region(dave->base_phys, dave->span);

	return -EBUSY;
}

static int dave2d_remove(struct platform_device *pdev)
{
	struct dave2d_dev *dave = platform_get_drvdata(pdev);
	unregister_irq(dave);
	iounmap(dave->mem_base_virt);
	iounmap(dave->base_virt);
	release_mem_region(dave->base_phys, dave->span);
	release_mem_region(dave->mem_base_phys, dave->mem_span);
	dave2d_shutdown_device(dave);
	devm_kfree(&pdev->dev, dave);
	return 0;
}

static const struct of_device_id dave2d_of_ids[] = {
	{
		.compatible = DAVE2D_OF_COMPATIBLE,
	},
	{ }
};

static struct platform_driver dave2d_driver = {
	.driver = {
		.name = DAVE2D_DEVICE_NAME,
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(dave2d_of_ids),
	},
	.probe = dave2d_probe,
	.remove = dave2d_remove,
};

static int __init dave2d_init(void)
{
	int result = 0;

	result = platform_driver_register(&dave2d_driver);
	if(result)
		printk(KERN_ALERT "%s: failed to register platform driver\n", __func__);

	return result;

}

static void __exit dave2d_exit(void)
{
	platform_driver_unregister(&dave2d_driver);
}

module_init(dave2d_init);
module_exit(dave2d_exit);
MODULE_AUTHOR("TES Electronic Solutions GmbH - Christian Thaler");
MODULE_DESCRIPTION("Kernel driver for DAVE2D renderer.");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_DEVICE_TABLE(of, dave2d_of_ids);
