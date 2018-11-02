/* for heartbeat */
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/kthread.h>
#include <linux/clk.h>

/* Pin Function Controller */
#include "pfc-rza1.c"

static int __init rskrza1_pinmux_init(void)
{
	struct device_node *root;
	struct device_node *np;


	/* Only execute this function on the RSK boards */
	if (!of_machine_is_compatible("renesas,rskrza1"))
		return 0;

	//printk("=== %s ===\n",__func__);

	root = of_find_node_by_path("/");

	r7s72100_pinmux_setup();

	/* ------------ Pin setup section ---------------*/
	r7s72100_pfc_pin_assign(P1_15, ALT1, DIIO_PBDC_DIS);	/* AD7 */

	/* Audio */
	r7s72100_pfc_pin_assign(P4_4, ALT5, DIIO_PBDC_EN);	/* SSISCK0 */
	r7s72100_pfc_pin_assign(P4_5, ALT5, DIIO_PBDC_EN);	/* SSIWS0 */
	r7s72100_pfc_pin_assign(P4_6, ALT5, DIIO_PBDC_DIS);	/* SSIRxD0 */
	r7s72100_pfc_pin_assign(P4_7, ALT5, SWIO_OUT_PBDCDIS);	/* SSITxD0 */

	/* NOTE: USB pins are dedicated */

	/* SCIF 1 pins */
	np = of_find_node_by_path("/serial@e8007800");
	if (np) {
		if (of_device_is_available(np)) {
#if 1
			/* SCIF1 on JA6 */
			printk("=== SCIF1 Enabled on JA6 ===\n");
			r7s72100_pfc_pin_assign(P4_12, ALT7, DIIO_PBDC_DIS);	/* SCIF1 TX */
			r7s72100_pfc_pin_assign(P4_13, ALT7, DIIO_PBDC_DIS);	/* SCIF1 RX */
#else
			/* SCIF1 with flow control on JA1 (used for Bluetooth testing) */
			printk("=== SCIF1 with CTS/RTS Enabled on JA1 ===\n");
			r7s72100_pfc_pin_assign(P2_3, ALT6, DIIO_PBDC_DIS);	/* SCIF1 CTS1 */
			r7s72100_pfc_pin_assign(P2_5, ALT6, DIIO_PBDC_DIS);	/* SCIF1 TXD1 */
			r7s72100_pfc_pin_assign(P2_6, ALT6, DIIO_PBDC_DIS);	/* SCIF1 RXD1 */
			r7s72100_pfc_pin_assign(P2_7, ALT6, DIIO_PBDC_DIS);	/* SCIF1 RTS1 */
			r7s72100_pfc_pin_assign(P2_4, PMODE, PORT_OUT_LOW);	/* BT chip reset */
			mdelay(6);
			r7s72100_pfc_pin_assign(P2_4, PMODE, PORT_OUT_HIGH);	/* BT chip reset */
#endif
		}
		of_node_put(np);
	}

	if (root)
		of_node_put(root);

	return 0;
}
early_initcall(rskrza1_pinmux_init);
/* HINT: When you declare a function with early_initcall, that function
 * automatically be run early in the boot process before device drivers
 * have started loading.
 */

static int __init rskrza1_init_early(void)
{
	//printk("=== %s ===\n",__func__);

	return 0;
}
early_initcall(rskrza1_init_early);
/* HINT: When you declare a function with early_initcall, that function
 * automatically be run early in the boot process before device drivers
 * have started loading.
 */


/* Write to I2C device */
/* Based off of board-sx1.c */
int rza1_i2c_write_byte(u8 ch, u8 devaddr, u8 regoffset, u8 value)
{
	struct i2c_adapter *adap;
	int err;
	struct i2c_msg msg[1];
	unsigned char data[2];

	adap = i2c_get_adapter(ch);
	if (!adap)
		return -ENODEV;
	msg->addr = devaddr;	/* I2C address of chip */
	msg->flags = 0;
	msg->len = 2;
	msg->buf = data;
	data[0] = regoffset;	/* register num */
	data[1] = value;		/* register data */
	err = i2c_transfer(adap, msg, 1);
	i2c_put_adapter(adap);
	if (err >= 0)
		return 0;
	return err;
}

/* Read from I2C device */
/* Based off of board-sx1.c */
int rza1_i2c_read_byte(u8 ch, u8 devaddr, u8 regoffset, u8 *value)
{
	struct i2c_adapter *adap;
	int err;
	struct i2c_msg msg[1];
	unsigned char data[2];

	adap = i2c_get_adapter(ch);
	if (!adap)
		return -ENODEV;

	msg->addr = devaddr;	/* I2C address of chip */
	msg->flags = 0;
	msg->len = 1;
	msg->buf = data;
	data[0] = regoffset;	/* register num */
	err = i2c_transfer(adap, msg, 1);
	if (err < 0)
		return err;

	msg->addr = devaddr;	/* I2C address */
	msg->flags = I2C_M_RD;
	msg->len = 1;
	msg->buf = data;
	err = i2c_transfer(adap, msg, 1);
	*value = data[0];
	i2c_put_adapter(adap);

	if (err >= 0)
		return 0;
	return err;
}

static int heartbeat(void * data)
{
	u8 index = 0;
	u8 value;
	int ret;
	static const u8 pattern[8] = {7,6,5,3,7,3,5,6};

	while(1) {
		/* Register address 1 is the Output Control register */
		ret = rza1_i2c_read_byte(3, 0x20, 0x01, &value);
		value &= ~0x7;
		value |= pattern[index++];
		index &= 0x7;

		if( !ret )
			rza1_i2c_write_byte(3, 0x20, 0x01, value);

		msleep_interruptible(250);
		//msleep(250);
	}

	return 0;
}

static int __init rskrza1_init_late(void)
{
	struct device_node *root = of_find_node_by_path("/");
	struct clk *clk;

	/* Only execute this function on the RSK boards */
	if (!of_machine_is_compatible("renesas,rskrza1"))
		return 0;

	//printk("=== %s ===\n",__func__);

	/* Keep XIP QSPI Alive
	 * At the end of kernel boot, the function clk_disable_unused will
	 * get called which will disable peripheral clocks that are not
	 * being used. However, since we set up XIP QSPI in u-boot, the
	 * kernel does not know that we should not power down the QSPI
	 * interface. Therefore, we will re-enable the clock here which will
	 * inform the kernel that at least someone is using it.
	 *
	 * Another way would have been to add "clk_ignore_unused" to the
	 * kernel command line and no clocks would get disabled.
	 */
	clk = clk_get_sys(NULL,"spibsc0");
	if (IS_ERR(clk))
		pr_err("%s: Error: Could not find QSPI clock\n", __func__);
	else
		clk_prepare_enable(clk);

	/* Add "no-heartbeat" to the device tree to disable heartbeat */
	if (!of_property_read_bool(root, "no-heartbeat")) {
		/* Start heartbeat kernel thread */
		kthread_run(heartbeat, NULL,"heartbeat");
	}

	if (root)
		of_node_put(root);

	return 0;
}
late_initcall(rskrza1_init_late);
/* HINT: When you declare a function with late_initcall, that function
 * automatically be run at the end of kernel boot after all the drivers
 * have been loaded, but before the file system is mounted.
 */
