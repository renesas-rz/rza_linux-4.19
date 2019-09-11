/****************************************************************************
 *  License : All rights reserved for TES Electronic Solutions GmbH
 *	      See included /docs/license.txt for details
 *  Project : D/AVE 2D
 *  Purpose : Public definitions of the kernel module
 ****************************************************************************
 * Version Control Information :
 *  $Revision: $
 *  $Date: $
 *  $LastChangedBy: $
 ****************************************************************************/

#ifndef __TES_DAVE_H__
#define __TES_DAVE_H__

/* IOCTL Commands */
#define DAVE2D_IOCTL_TYPE 'D'
#define DAVE2D_IOCTL_REG_PREFIX		(0x80)
#define DAVE2D_IOCTL_NR_SETTINGS	(0x01)
#define DAVE2D_IOCTL_MAKE_REG(reg) (reg|DAVE2D_IOCTL_REG_PREFIX)
#define DAVE2D_IOCTL_GET_REG(nr) (nr&(~DAVE2D_IOCTL_REG_PREFIX))
#define DAVE2D_IOCTL_WREG(reg) (_IOW(DAVE2D_IOCTL_TYPE,DAVE2D_IOCTL_MAKE_REG(reg),unsigned long))
#define DAVE2D_IOCTL_RREG(reg) (_IOR(DAVE2D_IOCTL_TYPE,DAVE2D_IOCTL_MAKE_REG(reg),unsigned long))
#define DAVE2D_IOCTL_GET_SETTINGS (_IOR(DAVE2D_IOCTL_TYPE,DAVE2D_IOCTL_NR_SETTINGS,dave2d_settings))

/* DAVE2D physical memory layout */
typedef struct
{
	unsigned long base_phys; /* base start address */
	unsigned long span; /* last base offset */
	unsigned long mem_base_phys; /* video memory start address */
	unsigned long mem_span; /* last video memory cell offset */
} dave2d_settings;

#endif
