/****************************************************************************
 *  License : All rights reserved for TES Electronic Solutions GmbH
 *            See included /docs/license.txt for details
 *  Project : D/AVE 2D
 *  Purpose : 
 ****************************************************************************
 * Version Control Information :
 *  $Revision: $
 *  $Date: $
 *  $LastChangedBy: $
 ****************************************************************************/

#ifndef TES_DAVE_IRQ_H_
#define TES_DAVE_IRQ_H_

int register_irq(struct dave2d_dev *dave);
void unregister_irq(struct dave2d_dev *dave);
ssize_t dave2d_read(struct file *filp, char __user *buff, size_t count, loff_t *offp);

#endif /* TES_DAVE_IRQ_H_ */
