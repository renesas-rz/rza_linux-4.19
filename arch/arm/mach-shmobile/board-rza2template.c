/* for heartbeat */
#include <linux/delay.h>
#include <linux/kthread.h>
#include <linux/clk.h>
#include <linux/of.h>
#include <linux/io.h>


#define PFC_BASE_ADDR	0xFCFFE000
void __iomem *PFC_BASE;
#define PDR_BASE	(PFC_BASE + 0x0000)	/* 16-bit, 2 bytes apart */
#define PODR_BASE	(PFC_BASE + 0x0040)	/* 8-bit, 1 byte apart */
#define PIDR_BASE	(PFC_BASE + 0x0060)	/* 8-bit, 1 byte apart */
#define PMR_BASE	(PFC_BASE + 0x0080)	/* 8-bit, 1 byte apart */
#define DSCR_BASE	(PFC_BASE + 0x0140)	/* 16-bit, 2 bytes apart */
#define PFS_BASE	(PFC_BASE + 0x0200)	/* 8-bit, 8 bytes apart */
#define PWPR		(PFC_BASE + 0x02FF)	/* 8-bit */
#define PFENET		(PFC_BASE + 0x0820)	/* 8-bit */
#define PPOC		(PFC_BASE + 0x0900)	/* 32-bit */
#define PHMOMO		(PFC_BASE + 0x0980)	/* 32-bit */
#define PMODEPFS	(PFC_BASE + 0x09C0)	/* 32-bit */
#define PCKIO		(PFC_BASE + 0x09D0)	/* 8-bit */

enum pfc_pin_port_name {P0=0, P1,P2,P3,P4,P5,P6,P7,P8,P9,PA,PB,PC,PD,PE,PF,PG,PH,PJ,PK,PL,PM};

void pfc_set_pin_function(u8 port, u8 pin, u8 func)
{
	u16 reg16;
	u16 mask16;

	if (!PFC_BASE)
		PFC_BASE = ioremap_nocache(PFC_BASE_ADDR, 0x300);

	/* Set pin to 'Non-use (Hi-z input protection)'  */
	reg16 = *(volatile u16 *)(PDR_BASE + (port * 2));
	mask16 = 0x03 << (pin * 2);
	reg16 &= ~mask16;
	*(volatile u16 *)(PDR_BASE + port * 2) = reg16;

	/* Temporary switch to GPIO */
	*(volatile u8 *)(PMR_BASE + port) &= ~(1 << pin);

	/* PFS Register Write Protect : OFF */
	*(volatile u8 *)PWPR = 0x00; /* B0WI=0, PFSWE=0 */
	*(volatile u8 *)PWPR = 0x40; /* B0WI=0, PFSWE=1 */

	/* Set Pin function (interrupt disabled, ISEL=0) */
	*(volatile u8 *)(PFS_BASE + (port * 8) + pin) = func;

	/* PFS Register Write Protect : ON */
	*(volatile u8 *)PWPR = 0x00; /* B0WI=0, PFSWE=0 */
	*(volatile u8 *)PWPR = 0x80; /* B0WI=1, PFSWE=1 */

	/* Port Mode  : Peripheral module pin functions */
	*(volatile u8 *)(PMR_BASE + port) |= (1 << pin);
}

#define GPIO_IN 0
#define GPIO_OUT 1
void pfc_set_gpio(u8 port, u8 pin, u8 dir)
{
	u16 reg16;
	u16 mask16;

	if (!PFC_BASE)
		PFC_BASE = ioremap_nocache(PFC_BASE_ADDR, 0x300);

	reg16 = *(volatile u16 *)(PDR_BASE + (port * 2));
	mask16 = 0x03 << (pin * 2);
	reg16 &= ~mask16;

	if (dir == GPIO_IN)
		reg16 |= 2 << (pin * 2);	// pin as input
	else
		reg16 |= 3 << (pin * 2);	// pin as output

	*(volatile u16 *)(PDR_BASE + port * 2) = reg16;
}


void gpio_set(u8 port, u8 pin, u8 value)
{
	if (!PFC_BASE)
		PFC_BASE = ioremap_nocache(PFC_BASE_ADDR, 0x300);

	if (value)
		*(volatile u8 *)(PODR_BASE + port) |= (1 << pin);
	else
		*(volatile u8 *)(PODR_BASE + port) &= ~(1 << pin);
}

u8 gpio_read(u8 port, u8 pin)
{
	if (!PFC_BASE)
		PFC_BASE = ioremap_nocache(PFC_BASE_ADDR, 0x300);

	return (*(volatile u8 *)(PIDR_BASE + port) >> pin) & 1;
}



static int __init rzatemplate_init_early(void)
{
	//printk("=== %s ===\n",__func__);

# if 0
	/* Example pin setup from board file instead of Device Tree */
	pfc_set_pin_function(P5, 7, 2);	/* IRQ3 */
#endif

	return 0;
}
/* HINT: When you declare a function with early_initcall, that function
 * automatically be run early in the boot process before device drivers
 * have started loading.
 */
early_initcall(rzatemplate_init_early);

#if 0
/* LED Heartbeat Function (example) */
static int heartbeat(void * data)
{
	u8 index = 0;

	while(1) {

		if ((index == 0) || (index == 2))  {
			/* Green LED on */
			//gpio_set(PC,1,1);
		}
		else {
			/* Green LED off */
			//gpio_set(PC,1,0);
		}

		index++;
		if (index >= 8)
			index = 0;

		msleep_interruptible(250);
	}

	return 0;
}
#endif

static int __init rzatemplate_init_late(void)
{
	struct device_node *root = of_find_node_by_path("/");

	//printk("=== %s ===\n",__func__);

#if 0 /* example */
	/* Add "no-heartbeat" to the device tree to disable heartbeat */
	if (!of_property_read_bool(root, "no-heartbeat")) {
		/* Start heartbeat kernel thread */
		kthread_run(heartbeat, NULL,"heartbeat");
	}
#endif

	if (root)
		of_node_put(root);

	return 0;
}
/* HINT: When you declare a function with late_initcall, that function
 * automatically be run at the end of kernel boot after all the drivers
 * have been loaded, but before the file system is mounted.
 */
late_initcall(rzatemplate_init_late);
