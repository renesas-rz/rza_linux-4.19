//#define DEBUG
/*
 * Copyright (C) 2016-2017 Renesas Electronics America
 * Copyright (C) 2013-2014 Renesas Solutions Corp.
 *
 * Based on drivers/video/ren_vdc4.c by Phil Edworthy
 * Copyright (c) 2012 Renesas Electronics Europe Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/uaccess.h>
#include <video/of_display_timing.h>
#include <video/of_videomode.h>
#include <video/videomode.h>
#include "vdc5fb.h"
#include "vdc5fb-regs.h"

/* Options */
#define PALETTE_NR 16

#define	PIX_FORMAT_RGB565	0
#define	PIX_FORMAT_RGB888	1
#define	PIX_FORMAT_ARGB8888	4
#define	PIX_FORMAT_RGBA8888	11

struct vdc5fb_priv {
	struct platform_device *pdev;
	struct vdc5fb_pdata *pdata;
	const char *dev_name;
	struct fb_videomode *videomode;	/* current */
	struct fb_videomode videomode_copy;	/* local copy */
	/* clock */
	struct clk *clk;
	/* framebuffers */
	void __iomem *base;
	void __iomem *base_lvds;
	dma_addr_t fb_phys_addr;
	unsigned long flm_off;
	unsigned long flm_num;
	int fb_nofree;
#if 0 /* interrupts not used */
	/* irq */
	struct {
		int start;		/* start irq number */
		int end;		/* end irq number, inclusive */
		u32 mask[3];		/* curremnt irq mask */
		char longname[VDC5FB_IRQ_SIZE][32];	/* ire name */
	} irq;
#endif

	/* display */
	struct fb_info *info;
	unsigned long dc;		/* dot clock in Hz */
	unsigned int dcdr;		/* dot clock divisor */
	unsigned int rr;		/* refresh rate in Hz */
	unsigned int res_fv;		/* vsync period (in fh) */
	unsigned int res_fh;		/* hsync period (in dc) */
	unsigned long panel_pixel_xres;
	unsigned long panel_pixel_yres;
	u32 pseudo_palette[PALETTE_NR];
};


static inline struct vdc5fb_pdata *priv_to_pdata(struct vdc5fb_priv *priv)
{
	return priv->pdata;
}

/* READ / WRITE VDC5 REGISTERS */
static void vdc5fb_write(struct vdc5fb_priv *priv, int reg, u32 data)
{
	if ((priv->pdata->use_lvds) && (reg >= LVDS_UPDATE) && (reg <= LPHYACC)) {
		iowrite32((u32)data, priv->base_lvds + (vdc5fb_offsets[reg] - vdc5fb_offsets[LVDS_UPDATE]));
		return;
	}

	if ((SYSCNT_PANEL_CLK == reg) || (SYSCNT_CLUT == reg))
		iowrite16((u16)data, (priv->base + vdc5fb_offsets[reg])); /* 16-bit */
	else
		iowrite32((u32)data, (priv->base + vdc5fb_offsets[reg]));
}

static unsigned long vdc5fb_read(struct vdc5fb_priv *priv, int reg)
{
	if ((priv->pdata->use_lvds) && (reg >= LVDS_UPDATE) && (reg <= LPHYACC))
		return ioread32(priv->base_lvds + (vdc5fb_offsets[reg] - vdc5fb_offsets[LVDS_UPDATE]));

	if ((SYSCNT_PANEL_CLK == reg) || (SYSCNT_CLUT == reg))
		return ioread16(priv->base + vdc5fb_offsets[reg]); /* 16-bit */
	else
		return ioread32(priv->base + vdc5fb_offsets[reg]);
}

#ifdef VDC5
static void vdc5fb_setbits(struct vdc5fb_priv *priv, int reg, u32 bits)
{
	u32 tmp;

	tmp = vdc5fb_read(priv, reg);
	tmp |= bits;
	vdc5fb_write(priv, reg, tmp);
}
#endif

/* INTERRUPT HANDLING */
#if 0 /* Interrupts are not used with this driver */
static irqreturn_t vdc5fb_irq(int irq, void *data)
{
	struct vdc5fb_priv *priv = (struct vdc5fb_priv *)data;

	irq = irq - priv->irq.start;
	switch (irq) {
	case S0_VI_VSYNC:	/* INT0 */
	case S0_LO_VSYNC:	/* INT1 */
	case S0_VSYNCERR:	/* INT2 */
	case GR3_VLINE:		/* INT3 */
	case S0_VFIELD:		/* INT4 */
	case IV1_VBUFERR:	/* INT5 */
	case IV3_VBUFERR:	/* INT6 */
	case IV5_VBUFERR:	/* INT7 */
		break;
	case IV6_VBUFERR:	/* INT8 */
	case S0_WLINE:		/* INT9 */
#ifdef VDC5
	case S1_VI_VSYNC:	/* INT10 */
	case S1_LO_VSYNC:	/* INT11 */
	case S1_VSYNCERR:	/* INT12 */
	case S1_VFIELD:		/* INT13 */
	case IV2_VBUFERR:	/* INT14 */
	case IV4_VBUFERR:	/* INT15 */
		break;
	case S1_WLINE:		/* INT16 */
	case OIR_VI_VSYNC:	/* INT17 */
	case OIR_LO_VSYNC:	/* INT18 */
	case OIR_VLINE:		/* INT19 */
	case OIR_VFIELD:	/* INT20 */
	case IV7_VBUFERR:	/* INT21 */
	case IV8_VBUFERR:	/* INT22 */
#endif /* VDC5 */
		break;
	default:
		dev_err(&priv->pdev->dev, "unexpected irq (%d+%d)\n",
			priv->irq.start, irq);
		break;
	}

	return IRQ_HANDLED;
}

static int vdc5fb_init_irqs(struct vdc5fb_priv *priv)
{
	int error = -EINVAL;
	struct platform_device *pdev;
	struct resource *res;
	int irq;

	pdev = priv->pdev;
	res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (!res)
		return error;

	priv->irq.start = res->start;
	priv->irq.end = res->end;
	BUG_ON((priv->irq.end - priv->irq.start + 1) != VDC5FB_MAX_IRQS);

	for (irq = 0; irq < VDC5FB_MAX_IRQS; irq++) {
		snprintf(priv->irq.longname[irq],
			sizeof(priv->irq.longname[0]), "%s: %s",
			priv->dev_name, irq_names[irq]);
		error = request_irq((priv->irq.start + irq),
			vdc5fb_irq, 0, priv->irq.longname[irq], priv);
		if (error < 0) {
			while (--irq >= 0)
				free_irq(priv->irq.start + irq, priv);
			return error;
		}
	}

	return 0;
}

static void vdc5fb_deinit_irqs(struct vdc5fb_priv *priv)
{
	int irq;

	for (irq = priv->irq.start; irq <= priv->irq.end; irq++)
		free_irq(irq, priv);
}
#endif /* interrupts not used */

/* For disabling the clocks */
static void vdc5fb_deinit_clocks(struct vdc5fb_priv *priv)
{
	if (priv->clk)
		clk_put(priv->clk);
}

/*
 * Black the screen by making all the frame buffer pixels zero.
 */
static void vdc5fb_clear_fb(struct vdc5fb_priv *priv)
{
#if 0
	struct vdc5fb_pdata *pdata = priv_to_pdata(priv);
	char *start;
	size_t size;

	start = (char *)priv->info->screen_base;
	size = pdata->videomode->xres * pdata->videomode->yres
		* (pdata->bpp / 8);

	memset(start, 0x0, size);
#endif
}

/*
 * Any time you modify a register you have to set the corresponding
 * "update" register to actually latch the new value it. For the most
 * part, the new value doesn't take effect until the next VSYNC
 */
static int vdc5fb_update_regs(struct vdc5fb_priv *priv,
	int reg, uint32_t bits, int wait)
{
	uint32_t tmp;
	long timeout;

	tmp = vdc5fb_read(priv, reg);
	tmp |= bits;
	vdc5fb_write(priv, reg, tmp);

	if (wait) {
		timeout = 100;
		do {
			tmp = vdc5fb_read(priv, reg);
			if ((tmp & bits) == 0)
				return 0;
			udelay(1000);
		} while (--timeout > 0);
	/* wait for max. 100 ms... */
	}

	/* Since not all VDC5 features are used, we can ignore some timeouts */
	if ((reg == SC0_SCL0_UPDATE) || (reg == IMGCNT_UPDATE) || (reg == SC0_SCL1_UPDATE))
		return 0;

	dev_err(&priv->pdev->dev, "update_regs timeout reg=%08lX, bits=%08lX, now=%08lX\n",
		vdc5fb_offsets[reg],
		(long unsigned int)bits,
		(long unsigned int)tmp);

	return -1;
}

/*
 * Given the desired pixel clock frequency specified by the user, calculate
 * what you'll need to put into the registers by getting as close as you can
 * given the restraints of the dividers.
 */
static int vdc5fb_set_panel_clock(struct vdc5fb_priv *priv,
	struct fb_videomode *mode)
{
	/* These are the divide by ratios for DCDR[5:0] */
	static const unsigned char dcdr_list[13] = {
		1, 2, 3, 4, 5, 6, 7, 8, 9, 12, 16, 24, 32,
	};

	uint64_t desired64 = 1000000000000; /* pixclock is in ps (pico seconds)*/
	unsigned long desired;
	unsigned long source;
	unsigned long used;
	int n;

	source = clk_get_rate(priv->clk);
	BUG_ON(source == 0);

	/* The board file will set fb_videomode.pixclock to what the panel
	   recomends, but should be a division of P1 clock. Now we need
	   to find out what divider bits for DCDR need to be (knowing that the
	   DCDR has a limited number of valid divider) */
	(void)do_div(desired64, mode->pixclock);
	desired = (unsigned long)desired64;
	for (n = 0; n < ARRAY_SIZE(dcdr_list); n++) {
		used = source / dcdr_list[n];
		if (used <= desired) {
			priv->dcdr = dcdr_list[n];
			priv->dc = used;
			return 0;
		}
	}
	return -1;
}

#define ROUNDED_DIV(a, b)	((a + b/2) / b)

/*
 * Initialize the LVDS interface, mainly the PLL clock source.
 * Note that while there are many ways you can set up the dividers and
 * multipliers for the PLL, we basically picked a configuration here and
 * at run-time we try to get as close as we can to what they user wants by
 * adjusting the clock dividers that come after the PLL.
 * For the most part, we'll probably always find a frequency that is still
 * within the specs of the panel.
 * If you #define DEBUG at the top of the file, it will print out the
 * frequency value so you can check it if you want.
 */
static int vdc5fb_init_lvds(struct vdc5fb_priv *priv)
{
	struct vdc5fb_pdata *pdata = priv_to_pdata(priv);
	uint64_t desired64 = 1000000000000; /* pixclock is in ps (pico seconds)*/
	u32 desired, source, fref, nfd, fvco, i, nod, nod_array[4], fout;
	u32 tmp;

	/*
	(Figure 40.3 from manual)
	Source-->NIDIV-->NRD--+-->VCO--+-->NOD--+-->NODIV-->Panel clock
	                      +--NFD<--+        +-->Freq divider 3-->Panel clock LVDS
                                                +------------------->Clock for LVDS output
		NIDIV = frequency divider 1
		NODIV = frequency divider 2
		NRD = input frequency divider
		NFD = feedback frequency divider
		NOD = output frequency divider

		FIN = Source / NIDIV (output of divider 1) (9 MHz to 30 MHz)
		FREF = FIN / NRD (Reference frequency)	(2.5 MHz to 30 MHz)
		FVCO = FIN × NFD / NRD (VCO output frequency ) (750 MHz to 1630 MHz)
		FOUT = FIN × NFD / (NRD × NOD) (LVDS PLL output frequency ) (609 MHz max)

	*/
	/*
	  To simplify:
	   - fix input clock to peripheral1 = 66650000 Hz
		LVDS_IN_CLK_SEL = 5 (P1 CLK)
	   - fix NIDIV to 4 => FIN = source / 4 = 16662500 Hz
		LVDS_IDIV_SET = 2 (NIDIV = 4)
	   - fix NRD to 5 => FREF = FIN / 5 = source / 20 = 3332500 Hz
	   - evaluate fout according ocksel
	   - evaluate NOD to allow fvco be the nearest to 1.1 GHz [sqrt(750*1630) MHz]
	*/

	/* LVDS Clock Select Register (LCLKSELR) Settings */
	#define MY_LVDS_SET_IN_CLK_SEL	VDC5_LVDS_INCLK_SEL_PERI /* P1 Clock (fixed) */
	#define MY_LVDS_IDIV_SET	VDC5_LVDS_NDIV_4	/* NIDIV = div by 4 */
	#define MY_LVDS_ODIV_SET	VDC5_LVDS_NDIV_4	/* NODIV = div by 4 */

	/* LVDS PLL Setting Register (LPLLSETR) Settings */
	/* LVDSPLL_FD is run-time calculated */			/* NFD = ??? */
	#define MY_LVDSPLL_RD	4				/* NRD = div by 5 */
	/* LVDSPLL_OD is run-time calculated  */		/* NOD = ??? */

	source = clk_get_rate(priv->clk);
	BUG_ON(source == 0);

	/* Calculate FREF */
	/* FREF = FIN / NRD */
	fref = source / (1 << MY_LVDS_IDIV_SET);
	fref = fref / (MY_LVDSPLL_RD + 1);

	/* Convert pixclock in ps to hertz */
	(void)do_div(desired64, pdata->videomode->pixclock);
	desired = (unsigned long)desired64;

	fout = desired;		/* our desired panel clock */

	/* The LVDS transmition rate (raw PLL output) will be 7x
	   the panel clock */
	if (pdata->panel_ocksel == OCKSEL_PLL_DIV7)
		fout *= 7;

	/* Calcualte NOD */
	/* Find a VCO output frequncy that gets us as close to 1.1GHz
	   [sqrt(750*1630) MHz] as possible (over or under). Try
	   all 4 possible NOD values
	   NOD = 2 ^ LVDSPLL_OD */
	for (i = 0, nod = 0; i < 4; i++) {
		/* How many times does 1.1GHz need to be halved
		   in order to equal fout? */
		nod_array[i] = (1105667219 >> i) - fout;
		if (nod_array[i] < 0)
			nod_array[i] = -nod_array[i];
		if (nod_array[i] < nod_array[nod])
			nod = i;	/* the closest value (+ or -) */
	}

	/* Our desired FVCO based on our desire pixelclock */
	fvco = fout * (1 << nod);	/* desired fvco */

	/* Determine our real values based on true clock source (FREF) */
	nfd = ROUNDED_DIV(fvco, fref);
	fvco = fref * nfd;		/* real fvco */
	fout = fvco / (1 << nod);	/* real fout */

	/* Clock Frequency Division Ratio of the VDC5 channel is fixed
	   to 1:1 (since we are doing all the work here to make sure
	   the panel clock is a good value) */
	priv->dcdr = 1;

	priv->dc = fout;

	/* Panel dot clock is 1/7 of LVDS tx clock */
	if (pdata->panel_ocksel == OCKSEL_PLL_DIV7)
		priv->dc /= 7;

	/* Print out values for user to confirm frequencies are
	   within correct ranges */
	dev_dbg(&priv->pdev->dev,
		"LVDS: target %u Hz, actual %lu Hz, Fref %u Hz, NFD %u, Fvco %u Hz, nod 1/%u, Fout %u Hz\n",
		desired, priv->dc, fref, nfd, fvco, 1 << nod, fout);

	tmp = vdc5fb_read(priv, SYSCNT_PANEL_CLK);
	tmp &= ~PANEL_ICKEN;
	vdc5fb_write(priv, SYSCNT_PANEL_CLK, tmp);

#if 1
	/* Specify the characteristics of LVDS output buffer: LPHYACC.SKEWC[1:0] = 01 */
	/* As required by hardware manual */
	tmp = vdc5fb_read(priv, LPHYACC);
	tmp &= ~0x0003;
	tmp |= 0x0001;
	vdc5fb_write(priv, LPHYACC, tmp);
#endif

	/* LCLKSELR: LVDS clock select register */

	/* Mask bits to 0 */
	tmp = vdc5fb_read(priv, LCLKSELR);
	tmp &= ~LVDS_LCLKSELR_MASK;
	vdc5fb_write(priv, LCLKSELR, tmp);

	/* The clock input to frequency divider 1 */
	tmp |= LVDS_SET_IN_CLK_SEL(MY_LVDS_SET_IN_CLK_SEL);
#ifdef VDC5
	vdc5fb_write(priv, LCLKSELR, tmp);

	/* The frequency dividing value (NIDIV) for frequency divider 1 */
	tmp |= LVDS_SET_IDIV(MY_LVDS_IDIV_SET);
#endif /* VDC5 */
	vdc5fb_write(priv, LCLKSELR, tmp);

	/* The frequency dividing value (NODIV) for frequency divider 2 */
	tmp |= LVDS_SET_ODIV(MY_LVDS_ODIV_SET);
#ifdef VDC5
	vdc5fb_write(priv, LCLKSELR, tmp);

	/* The VDC5 channel whose data is to be output through the LVDS */
	if (priv->pdata->channel)
		tmp |= LVDS_VDC_SEL;
#endif /* VDC5 */
	vdc5fb_write(priv, LCLKSELR, tmp);

	mdelay(1);

	/* LPLLSETR: LVDS PLL setting register */
	tmp = vdc5fb_read(priv, LPLLSETR);
	tmp &= ~LVDS_LPLLSETR_MASK;
	vdc5fb_write(priv, LPLLSETR, tmp);

	/* The frequency dividing value (NFD) for the feedback frequency */
	tmp |= LVDS_SET_FD(nfd);
	vdc5fb_write(priv, LPLLSETR, tmp);

	/* The frequency dividing value (NRD) for the input frequency */
	tmp |= LVDS_SET_RD(MY_LVDSPLL_RD);
	vdc5fb_write(priv, LPLLSETR, tmp);

	/* The frequency dividing value (NOD) for the output frequency */
	tmp |= LVDS_SET_OD(nod);
	vdc5fb_write(priv, LPLLSETR, tmp);

	tmp = vdc5fb_read(priv, LCLKSELR);
	/* Internal parameter setting for LVDS PLL */
	tmp |= LVDS_SET_TST(0x0008);
	vdc5fb_write(priv, LCLKSELR, tmp);

	mdelay(1);

	tmp = vdc5fb_read(priv, LPLLSETR);
	/* Controls power-down for the LVDS PLL: Normal operation */
	tmp &= ~LVDS_PLL_PD;
	vdc5fb_write(priv, LPLLSETR, tmp);

	msleep(1);

	tmp = vdc5fb_read(priv, LCLKSELR);
	tmp |= LVDS_CLK_EN;
	vdc5fb_write(priv, LCLKSELR, tmp);

	return 0;
}

/* Panel Clock Selection
 *
 * Use the values we calculated in vdc5fb_set_panel_clock() as well
 * as the user supplied info on what we want the input clock (icksel) and
 * output clock (ocksel) sources to be to set up the panel clock.
 */
static int vdc5fb_init_syscnt(struct vdc5fb_priv *priv)
{
	struct vdc5fb_pdata *pdata = priv_to_pdata(priv);
	u32 tmp;

	/* Ignore all irqs here */
#if 0
	priv->irq.mask[0] = 0;
	priv->irq.mask[1] = 0;
	priv->irq.mask[2] = 0;
	vdc5fb_write(priv, SYSCNT_INT4, priv->irq.mask[0]);
	vdc5fb_write(priv, SYSCNT_INT5, priv->irq.mask[1]);
#ifdef VDC5
	vdc5fb_write(priv, SYSCNT_INT6, priv->irq.mask[2]);
#endif /* VDC5 */
#else
	vdc5fb_write(priv, SYSCNT_INT4, 0);
	vdc5fb_write(priv, SYSCNT_INT5, 0);
#ifdef VDC5
	vdc5fb_write(priv, SYSCNT_INT6, 0);
#endif /* VDC5 */
#endif
	/* Clear all pending irqs */
	vdc5fb_write(priv, SYSCNT_INT1, 0);
	vdc5fb_write(priv, SYSCNT_INT2, 0);
#ifdef VDC5
	vdc5fb_write(priv, SYSCNT_INT3, 0);
#endif /* VDC5 */

	/* Setup panel clock */
	tmp = PANEL_DCDR(priv->dcdr);
	tmp |= PANEL_ICKEN;
	tmp |= PANEL_OCKSEL(pdata->panel_ocksel);
	tmp |= PANEL_ICKSEL(pdata->panel_icksel);
	vdc5fb_write(priv, SYSCNT_PANEL_CLK, tmp);

	return 0;
}
/* LCD Panel Timing Setup
 *
 * Even if we don't plan on using the Scalar layers at all, they still
 * need some setup related to the physical LCD panel timings.
 *
 * If you notice, we set what the full (native) resolution of the LCD
 * panel using the registers of the lowest layer (ie, scaler 0) because
 * it is the first layer in the VDC5 video pipeline.
 */
static int vdc5fb_init_sync(struct vdc5fb_priv *priv)
{
	struct fb_videomode *mode = priv->videomode;
	u32 tmp;

	/* (TODO) Freq. vsync masking and missing vsync
	 * compensation are not supported.
	 */
	vdc5fb_write(priv, SC0_SCL0_FRC1, 0);
	vdc5fb_write(priv, SC0_SCL0_FRC2, 0);
#ifdef VDC5
	vdc5fb_write(priv, SC1_SCL0_FRC1, 0);
	vdc5fb_write(priv, SC1_SCL0_FRC2, 0);
#endif /* VDC5 */

	/* Set the same free-running hsync/vsync period to
	 * all scalers (sc0, sc1 and oir). The hsync/vsync
	 * from scaler 0 is used by all scalers.
	 * (TODO) External input vsync is not supported.
	 */
	tmp = SC_RES_FH(priv->res_fh - 1);
	tmp |= SC_RES_FV(priv->res_fv - 1);
	vdc5fb_write(priv, SC0_SCL0_FRC4, tmp);
#ifdef VDC5
	vdc5fb_write(priv, SC1_SCL0_FRC4, tmp);
#endif /* VDC5 */
	tmp = (SC_RES_FLD_DLY_SEL | SC_RES_VSDLY(1));
	vdc5fb_write(priv, SC0_SCL0_FRC5, tmp);
#ifdef VDC5
	vdc5fb_write(priv, SC1_SCL0_FRC5, tmp);
#endif /* VDC5 */
	tmp = SC_RES_VSDLY(1);

	vdc5fb_write(priv, SC0_SCL0_FRC3, SC_RES_VS_SEL);
#ifdef VDC5
	vdc5fb_write(priv, SC1_SCL0_FRC3, (SC_RES_VS_SEL | SC_RES_VS_IN_SEL));
#endif /* VDC5 */
	/* Note that OIR is not enabled here */

	/* Set full-screen size (the native LCD panel size)
	 * These registers are located in the Scalar 0 layer
	 * just because it is the first block in the pipeline and needs
	 * to specify the overall timing for all the layers. */
	tmp = SC_RES_F_VW(priv->panel_pixel_yres);
	tmp |= SC_RES_F_VS(mode->vsync_len + mode->upper_margin);
	vdc5fb_write(priv, SC0_SCL0_FRC6, tmp);
#ifdef VDC5
	vdc5fb_write(priv, SC1_SCL0_FRC6, tmp);
#endif /* VDC5 */
	tmp = SC_RES_F_HW(priv->panel_pixel_xres);
	tmp |= SC_RES_F_HS(mode->hsync_len + mode->left_margin);
	vdc5fb_write(priv, SC0_SCL0_FRC7, tmp);
#ifdef VDC5
	vdc5fb_write(priv, SC1_SCL0_FRC7, tmp);
#endif /* VDC5 */

	/* Cascade on */
#ifdef VDC5
	vdc5fb_setbits(priv, GR1_AB1, GR1_CUS_CON_ON);
	/* Set GR0 as current, GR1 as underlaying */
	/* Set GR1 as current, GR0 as underlaying */
#endif /* VDC5 */
	tmp = vdc5fb_read(priv, GR_VIN_AB1);
	tmp &= ~GR_VIN_SCL_UND_SEL;
	vdc5fb_write(priv, GR_VIN_AB1, tmp);

	/* Do update here. */
	tmp = (SC_SCL0_UPDATE | SC_SCL0_VEN_B);
	vdc5fb_update_regs(priv, SC0_SCL0_UPDATE, tmp, 1);
#ifdef VDC5
	vdc5fb_update_regs(priv, SC1_SCL0_UPDATE, tmp, 1);
#endif /* VDC5 */
	tmp = (GR_UPDATE | GR_P_VEN);
#ifdef VDC5
	vdc5fb_update_regs(priv, GR1_UPDATE, tmp, 1);
#endif /* VDC5 */
	vdc5fb_update_regs(priv, GR_VIN_UPDATE, tmp, 1);

	return 0;
}

/* Scalar Layers Setup
 *
 * This setup is only needed if you plan on actually using the scalars
 * for display. While you might not need to 'scale' any images, they
 * do have the ability to display raw YUV (YCbCr) directly which might
 * be useful.
 */
static int vdc5fb_init_scalers(struct vdc5fb_priv *priv)
{
	struct fb_videomode *mode = priv->videomode;
	u32 tmp;

	/* Enable and setup scaler 0 */
	if( priv->pdata->layers[0].xres ) {
		vdc5fb_write(priv, SC0_SCL0_FRC3, SC_RES_VS_SEL);
		vdc5fb_update_regs(priv, SC0_SCL0_UPDATE, SC_SCL0_UPDATE, 1);

		vdc5fb_write(priv, SC0_SCL0_DS1, 0);
		vdc5fb_write(priv, SC0_SCL0_US1, 0);
		vdc5fb_write(priv, SC0_SCL0_OVR1, D_SC_RES_BK_COL);

		tmp = (mode->vsync_len + mode->upper_margin - 1) << 16;
		tmp |= mode->yres;
		vdc5fb_write(priv, SC0_SCL0_DS2, tmp);
		vdc5fb_write(priv, SC0_SCL0_US2, tmp);

		tmp = (mode->hsync_len + mode->left_margin) << 16;
		tmp |= mode->xres;
		vdc5fb_write(priv, SC0_SCL0_DS3, tmp);
		vdc5fb_write(priv, SC0_SCL0_US3, tmp);

		tmp = mode->yres << 16;
		tmp |= mode->xres;
		vdc5fb_write(priv, SC0_SCL0_DS7, tmp);

		tmp = SC_RES_IBUS_SYNC_SEL;
		vdc5fb_write(priv, SC0_SCL0_US8, tmp);
		vdc5fb_write(priv, SC0_SCL0_OVR1, D_SC_RES_BK_COL);
	}
	else {
		/* Disable scaler 0 */
		vdc5fb_write(priv, SC0_SCL0_DS1, 0);
		vdc5fb_write(priv, SC0_SCL0_US1, 0);
		vdc5fb_write(priv, SC0_SCL0_OVR1, D_SC_RES_BK_COL);
	}

#ifdef VDC5
	/* Enable and setup scaler 1 */
	if( priv->pdata->layers[1].xres ) {
		vdc5fb_write(priv, SC1_SCL0_FRC3, SC_RES_VS_SEL);
		vdc5fb_update_regs(priv, SC1_SCL0_UPDATE, SC_SCL0_UPDATE, 1);

		vdc5fb_write(priv, SC1_SCL0_DS1, 0);
		vdc5fb_write(priv, SC1_SCL0_US1, 0);
		vdc5fb_write(priv, SC1_SCL0_OVR1, D_SC_RES_BK_COL);

		tmp = (mode->vsync_len + mode->upper_margin - 1) << 16;
		tmp |= mode->yres;
		vdc5fb_write(priv, SC1_SCL0_DS2, tmp);
		vdc5fb_write(priv, SC1_SCL0_US2, tmp);

		tmp = (mode->hsync_len + mode->left_margin) << 16;
		tmp |= mode->xres;
		vdc5fb_write(priv, SC1_SCL0_DS3, tmp);
		vdc5fb_write(priv, SC1_SCL0_US3, tmp);

		tmp = mode->yres << 16;
		tmp |= mode->xres;
		vdc5fb_write(priv, SC1_SCL0_DS7, tmp);

		tmp = SC_RES_IBUS_SYNC_SEL;
		vdc5fb_write(priv, SC1_SCL0_US8, tmp);
		vdc5fb_write(priv, SC1_SCL0_OVR1, D_SC_RES_BK_COL);
	}
	else {
		/* Disable scaler 1 */
		vdc5fb_write(priv, SC1_SCL0_DS1, 0);
		vdc5fb_write(priv, SC1_SCL0_US1, 0);
		vdc5fb_write(priv, SC1_SCL0_OVR1, D_SC_RES_BK_COL);
	}
#endif /* VDC5 */

	return 0;
}

#define GR_UPDATE_OFFSET	0x00
#define GR_FLM_RD_OFFSET	0x04
#define GR_FLM1_OFFSET		0x08
#define GR_FLM2_OFFSET		0x0C
#define GR_FLM3_OFFSET		0x10
#define GR_FLM4_OFFSET		0x14
#define GR_FLM5_OFFSET		0x18
#define GR_FLM6_OFFSET		0x1C
#define GR_AB1_OFFSET		0x20
#define GR_AB2_OFFSET		0x24
#define GR_AB3_OFFSET		0x28
#define GR_AB7_OFFSET		0x38
#define GR_AB8_OFFSET		0x3C
#define GR_AB9_OFFSET		0x40
#define GR_BASE_OFFSET		0x4C

#define vdc5fb_iowrite32(d,r) iowrite32((u32) d, (void *) r)

/* Graphic Layers Setup
 *
 * This sets up the graphic layers (GR0,GR1,GR2,GR3) for displaying RGB
 * raster data.
 * There is a "graphics layer" located in each of the 4 images block
 * (Scalar x 2, Image Synthesizer x 2) in the VDC5 pipeline.
 *
 */
static int vdc5fb_init_graphics(struct vdc5fb_priv *priv)
{
	struct fb_videomode *mode = priv->videomode;
	u32 tmp;
	struct vdc5fb_layer *layer;
	u32 update_addr[4];
	int i;

	/* Need at least 1 graphic layer for /dev/fb0 */
	for (i=0;i<4;i++)
		if( priv->pdata->layers[i].xres )
			break;
	if( i == 4 )
	{
		printk("\n\n\n%s: You need to define at least 1 'layer' to be used as /dev/fb0\n\n\n",__func__);
		return -1;
	}

	update_addr[0] = (u32)priv->base + vdc5fb_offsets[GR0_UPDATE];
#ifdef VDC5
	update_addr[1] = (u32)priv->base + vdc5fb_offsets[GR1_UPDATE];
#endif /* VDC5 */
	update_addr[2] = (u32)priv->base + vdc5fb_offsets[GR2_UPDATE];
	update_addr[3] = (u32)priv->base + vdc5fb_offsets[GR3_UPDATE];

	for (i=0;i<4;i++) {
#ifdef VDC6
		if (i == 1)
			continue;
#endif /* VDC6 */
		/* Set Background color (really for debugging only) */
#ifdef DEBUG
		switch (i) {
			case 0:	tmp = 0x00800000;	// GR0 = Green
				break;
			case 1:	tmp = 0x00000080;	// GR1 = Red
				break;
			case 2:	tmp = 0x00008000;	// GR2 = Blue
				break;
			case 3:	tmp = 0x00008080;	// GR3 = Purple
		}
		vdc5fb_iowrite32(tmp, update_addr[i] + GR_BASE_OFFSET);	/* Background color (0-G-B-R) */
#endif

		layer = &priv->pdata->layers[i];
		if( layer->xres == 0 ) {
			/* not used */
			vdc5fb_iowrite32(0, update_addr[i] + GR_FLM_RD_OFFSET);
			if( i == 0 )
				vdc5fb_iowrite32(0, update_addr[i] + GR_AB1_OFFSET);	/* background graphics display */
			else
				vdc5fb_iowrite32(1, update_addr[i] + GR_AB1_OFFSET);	/* Lower-layer graphics display */
			continue;
		}

		vdc5fb_iowrite32(GR_R_ENB, update_addr[i] + GR_FLM_RD_OFFSET);
		vdc5fb_iowrite32(GR_FLM_SEL(1), update_addr[i] + GR_FLM1_OFFSET); /* scalers MUST use FLM_SEL */
		if( layer->base == 0 )
			layer->base = priv->fb_phys_addr;	/* Allocated during probe */
		if( layer->base >= 0xC0000000 )
			layer->base = virt_to_phys((void *)layer->base);	/* Convert to physical address */

		printk("vdc5fb: Layer %u Enabled (%ux%u @ 0x%08x)\n",i,layer->xres,layer->yres, layer->base);

		vdc5fb_iowrite32(layer->base, update_addr[i] + GR_FLM2_OFFSET);	/* frame buffer address*/
		tmp = GR_LN_OFF(layer->xres * layer->bpp / 8);	/* length of each line (and Frame Number=0)*/
		if ((layer->xres * layer->bpp / 8) % 32)
			dev_err(&priv->pdev->dev, "!!ERROR!! The width of a line must be a multiple of 32 bytes\n");
		vdc5fb_iowrite32(tmp, update_addr[i] + GR_FLM3_OFFSET);
		tmp = GR_FLM_LOOP(layer->yres - 1);
		tmp |= GR_FLM_LNUM(layer->yres - 1);
		vdc5fb_iowrite32(tmp, update_addr[i] + GR_FLM5_OFFSET);		/* lines per frame */
		tmp = layer->format;
		tmp |= GR_HW(layer->xres - 1);
		vdc5fb_iowrite32(tmp, update_addr[i] + GR_FLM6_OFFSET);	/* frame format */

		tmp = 0;
		//tmp |= (1U<<12);// Turns on/off alpha blending in a rectangular area.
		//tmp |= (1U<<8);// Turns on/off frame-line display of the image area for alpha blending in a rectangular area.
		//tmp |= (1U<<4);// Turns on/off frame-line display of the graphics image area.
		if( layer->blend )
			tmp |= GR_DISP_SEL(3);		/* Blended display of lower-layer graphics and current graphics */
		else
			tmp |= GR_DISP_SEL(2);		/* Current graphics display */
		vdc5fb_iowrite32(tmp, update_addr[i] + GR_AB1_OFFSET);

		tmp = GR_GRC_VW(layer->yres);
		tmp |= GR_GRC_VS(mode->vsync_len + mode->upper_margin + layer->y_offset);
		vdc5fb_iowrite32(tmp, update_addr[i] + GR_AB2_OFFSET);

		tmp = GR_GRC_HW(layer->xres);
		tmp |= GR_GRC_HS(mode->hsync_len + mode->left_margin + layer->x_offset);
		vdc5fb_iowrite32(tmp, update_addr[i] + GR_AB3_OFFSET);
	}

#ifdef VDC5
	/* Graphics VIN (Image Synthsizer) */
	/* Scaler 0 and Scaler 1 are blended together using this block. */
	/* GR0 = lower */
	/* GR1 = current */
	tmp = vdc5fb_read(priv, GR_VIN_AB1);
	tmp &= GR_AB1_MASK;
	if ( priv->pdata->layers[0].xres != 0 ) {
		if ( priv->pdata->layers[1].xres == 0 )
			// GR0 used, GR1 not used
			tmp |= GR_DISP_SEL(1);		/* lower only*/
		else
			// GR0 used, GR1 used
			tmp |= GR_DISP_SEL(3);		/* blend */
	}
	else if ( priv->pdata->layers[1].xres != 0 )
	{
			// GR0 not used, GR1 used
			tmp |= GR_DISP_SEL(2);		/* current only */
	}
	vdc5fb_write(priv, GR_VIN_AB1, tmp);
	vdc5fb_write(priv, GR_VIN_BASE, 0x00FF00);	/* Background color (0-G-B-R) */

	/* Set the LCD margins, otherwise the pixels will be cliped
	  (and background color will show through instead) */
	/* Basically, if the resolution of the VIN is set less than the scalers, the VIN will
	 * cut off the excess pixel data coming from the scallers. */
	tmp = GR_GRC_VW(priv->panel_pixel_yres);
	tmp |= GR_GRC_VS(mode->vsync_len + mode->upper_margin);
	vdc5fb_write(priv, GR_VIN_AB2, tmp);
	tmp = GR_GRC_HW(priv->panel_pixel_xres);
	tmp |= GR_GRC_HS(mode->hsync_len + mode->left_margin);
	vdc5fb_write(priv, GR_VIN_AB3, tmp);
#endif /* VDC5 */

#ifdef VDC6
	/* Graphics VIN (Image Synthsizer) */
	/* There is only 1 Scaler, so this block doesn't really do anything, but
	 * from a HW standpoint it is still requried to connect Scalers to
	 * Image Synthesizers in  a VDC pipeline. */
	tmp = vdc5fb_read(priv, GR_VIN_AB1);
	tmp &= GR_AB1_MASK;
	if ( priv->pdata->layers[0].xres != 0 ) {
			// GR0 used
			tmp |= GR_DISP_SEL(1);		/* lower only*/
	}
	vdc5fb_write(priv, GR_VIN_AB1, tmp);
#endif /* VDC6 */

	return 0;
}

/*
 * Output Image Control    (brightness, contrast, gamma)
 *
 * These apply after all the layers have been combine and just before
 * the data comes out of the pins to go to the LCD panel.
 */
static int vdc5fb_init_outcnt(struct vdc5fb_priv *priv)
{
	struct vdc5fb_pdata *pdata = priv_to_pdata(priv);
	u32 tmp;

	vdc5fb_write(priv, OUT_CLK_PHASE, OUTCNT_LCD_EDGE(pdata->data_clk_phase));

	vdc5fb_write(priv, OUT_BRIGHT1, PBRT_G(512));
	vdc5fb_write(priv, OUT_BRIGHT2, (PBRT_B(512) | PBRT_R(512)));
	tmp = (CONT_G(128) | CONT_B(128) | CONT_R(128));
	vdc5fb_write(priv, OUT_CONTRAST, tmp);

	vdc5fb_write(priv, GAM_SW, 0);

	tmp = D_OUT_PDTHA;
	tmp |= PDTHA_FORMAT(0);
	vdc5fb_write(priv, OUT_PDTHA, tmp);

	tmp = D_OUT_SET;
	tmp |= OUT_FORMAT(pdata->out_format);
	vdc5fb_write(priv, OUT_SET, tmp);

	return 0;
}
/* TCON pin Setup
 *
 * The TCON pins are used for the panel control signals (VSYNC, HSYNC, etc..)
 * The names of the registers and bits are confusing because in past VDC version,
 * the TCON pins were fixed such that a TCON pin could only be used for a specific
 * job (like HSYNC). But, starting in VDC4 (or maybe it was VDC3), you could
 * configure *any* TCON for *any* LCD signal.
 *
 * The TCON_xxx_SEL[2:0] bits in the TCON_TIM_xxx2 registers are completely
 * independent of the TCON_TIM_xxx1 registers.
 *
 */
static int vdc5fb_init_tcon(struct vdc5fb_priv *priv)
{
//	static const unsigned char tcon_sel[LCD_MAX_TCON]
//		= { 0, 1, 2, 7, 4, 5, 6, };
	struct fb_videomode *mode = priv->videomode;
	struct vdc5fb_pdata *pdata = priv_to_pdata(priv);
	u32 vs_s, vs_w, ve_s, ve_w;
	u32 hs_s, hs_w, he_s, he_w;
	u32 tmp1, tmp2;

	/* Data Enable */
	tmp1 = TCON_OFFSET(0);
	tmp1 |= TCON_HALF(priv->res_fh / 2);
	vdc5fb_write(priv, TCON_TIM, tmp1);
	tmp2 = 0;
#if 0
	tmp2 = TCON_DE_INV;
#endif
	vdc5fb_write(priv, TCON_TIM_DE, tmp2);

	/* Vertical Sync (VSYNC) pulse to start frame */
	/* when should go active, and for how long */
	vs_s = (2 * 0);
	vs_w = (2 * mode->vsync_len);
	tmp1 = TCON_VW(vs_w);
	tmp1 |= TCON_VS(vs_s);
	vdc5fb_write(priv, TCON_TIM_STVA1, tmp1);
	tmp2  = 0;
	if (!(mode->sync & FB_SYNC_VERT_HIGH_ACT))
		tmp2 |= TCON_INV;
	vdc5fb_write(priv, TCON_TIM_STVA2, tmp2);

	/* Vertical Enable Signal (only active when pixel data) */
	/* when should go active, and for how long */
	ve_s = (2 * (mode->vsync_len + mode->upper_margin));
	ve_w = (2 * priv->panel_pixel_yres);
	tmp1 = TCON_VW(ve_w);
	tmp1 |= TCON_VS(ve_s);
	vdc5fb_write(priv, TCON_TIM_STVB1, tmp1);
	tmp2  = 0;
#if 0
	tmp2 |= TCON_INV;
#endif
	vdc5fb_write(priv, TCON_TIM_STVB2, tmp2);

	/* Horizontal Sync (HSYNC) pulse to start line */
	/* when should go active, and for how long */
	hs_s = 0;
	hs_w = mode->hsync_len;
	tmp1 = TCON_HW(hs_w);
	tmp1 |= TCON_HS(hs_s);
	vdc5fb_write(priv, TCON_TIM_STH1, tmp1);
	tmp2  = 0;
#if 0
	tmp2 |= TCON_HS_SEL;
#endif
	if (!(mode->sync & FB_SYNC_HOR_HIGH_ACT))
		tmp2 |= TCON_INV;
	vdc5fb_write(priv, TCON_TIM_STH2, tmp2);

	/* Horizontal Enable Signal (only active when pixel data) */
	/* when should go active, and for how long */
	he_s = (mode->hsync_len + mode->left_margin);
	he_w = priv->panel_pixel_xres;
	tmp1 = TCON_HW(he_w);
	tmp1 |= TCON_HS(he_s);
	vdc5fb_write(priv, TCON_TIM_STB1, tmp1);
	tmp2  = 0;
#if 0
	tmp2 |= TCON_INV;
	tmp2 |= TCON_HS_SEL;
#endif
	vdc5fb_write(priv, TCON_TIM_STB2, tmp2);

	/* CPV Signal Pulse */
	tmp1 = TCON_HW(hs_w);
	tmp1 |= TCON_HS(hs_s);
	vdc5fb_write(priv, TCON_TIM_CPV1, tmp1);
	tmp2  = 0;
#if 0
	tmp2 |= TCON_INV;
	tmp2 |= TCON_HS_SEL;
#endif
	vdc5fb_write(priv, TCON_TIM_CPV2, tmp2);

	/* POLA Signal Pulse */
	tmp1 = TCON_HW(he_w);
	tmp1 |= TCON_HS(he_s);
	vdc5fb_write(priv, TCON_TIM_POLA1, tmp1);
	tmp2  = 0;
#if 0
	tmp2 |= TCON_HS_SEL;
	tmp2 |= TCON_INV;
	tmp2 |= TCON_MD;
#endif
	vdc5fb_write(priv, TCON_TIM_POLA2, tmp2);

	/* POLB Signal Pulse */
	tmp1 = TCON_HW(he_w);
	tmp1 |= TCON_HS(he_s);
	vdc5fb_write(priv, TCON_TIM_POLB1, tmp1);
	tmp2  = 0;
#if 0
	tmp2 |= TCON_INV;
	tmp2 |= TCON_HS_SEL;
	tmp2 |= TCON_MD;
#endif
	vdc5fb_write(priv, TCON_TIM_POLB2, tmp2);

	/* LCD_TCON0 pin */
	if (pdata->tcon_sel[LCD_TCON0] != TCON_SEL_UNUSED) {
		tmp2 = vdc5fb_read(priv, TCON_TIM_STVA2);
		tmp2 |= TCON_SEL(pdata->tcon_sel[LCD_TCON0]);
		vdc5fb_write(priv, TCON_TIM_STVA2, tmp2);
	}

	/* LCD_TCON1 pin */
	if (pdata->tcon_sel[LCD_TCON1] != TCON_SEL_UNUSED) {
		tmp2 = vdc5fb_read(priv, TCON_TIM_STVB2);
		tmp2 |= TCON_SEL(pdata->tcon_sel[LCD_TCON1]);
		vdc5fb_write(priv, TCON_TIM_STVB2, tmp2);
	}


	/* LCD_TCON2 pin */
	if (pdata->tcon_sel[LCD_TCON2] != TCON_SEL_UNUSED) {
		tmp2 = vdc5fb_read(priv, TCON_TIM_STH2);
		tmp2 |= TCON_SEL(pdata->tcon_sel[LCD_TCON2]);
		vdc5fb_write(priv, TCON_TIM_STH2, tmp2);
	}

	/* LCD_TCON3 pin */
	if (pdata->tcon_sel[LCD_TCON3] != TCON_SEL_UNUSED) {
		tmp2 = vdc5fb_read(priv, TCON_TIM_STB2);
		tmp2 |= TCON_SEL(pdata->tcon_sel[LCD_TCON3]);
		vdc5fb_write(priv, TCON_TIM_STB2, tmp2);
	}

	/* LCD_TCON4 pin */
	if (pdata->tcon_sel[LCD_TCON4] != TCON_SEL_UNUSED) {
		tmp2 = vdc5fb_read(priv, TCON_TIM_CPV2);
		tmp2 |= TCON_SEL(pdata->tcon_sel[LCD_TCON4]);
		vdc5fb_write(priv, TCON_TIM_CPV2, tmp2);
	}

	/* LCD_TCON5 pin */
	if (pdata->tcon_sel[LCD_TCON5] != TCON_SEL_UNUSED) {
		tmp2 = vdc5fb_read(priv, TCON_TIM_POLA2);
		tmp2 |= TCON_SEL(pdata->tcon_sel[LCD_TCON5]);
		vdc5fb_write(priv, TCON_TIM_POLA2, tmp2);
	}

	/* LCD_TCON6 pin */
	if (pdata->tcon_sel[LCD_TCON6] != TCON_SEL_UNUSED) {
		tmp2 = vdc5fb_read(priv, TCON_TIM_POLB2);
		tmp2 |= TCON_SEL(pdata->tcon_sel[LCD_TCON6]);
		vdc5fb_write(priv, TCON_TIM_POLB2, tmp2);
	}

	return 0;
}

/* Update All Layer
 *
 * Any time you modify a register you have to set the corresponding
 * "update" register to actually latch the new value it. For the most
 * part, the new value doesn't take effect until the next VSYNC.
 *
 * This function is simply take care of doing this for all the layers.
 */
static int vdc5fb_update_all(struct vdc5fb_priv *priv)
{
	u32 tmp;

	tmp = IMGCNT_VEN;
	vdc5fb_update_regs(priv, IMGCNT_UPDATE, tmp, 1);

	tmp = (SC_SCL0_VEN_A | SC_SCL0_VEN_B | SC_SCL0_UPDATE
		| SC_SCL0_VEN_C | SC_SCL0_VEN_D);
	vdc5fb_update_regs(priv, SC0_SCL0_UPDATE, tmp, 1);

	tmp = (SC_SCL1_VEN_A | SC_SCL1_VEN_B | SC_SCL1_UPDATE_A
		| SC_SCL1_UPDATE_B);
	vdc5fb_update_regs(priv, SC0_SCL1_UPDATE, tmp, 1);

	tmp = (GR_IBUS_VEN | GR_P_VEN | GR_UPDATE);
	vdc5fb_update_regs(priv, GR0_UPDATE, tmp, 1);
#ifdef VDC5
	vdc5fb_update_regs(priv, GR1_UPDATE, tmp, 1);
#endif /* VDC5 */

	tmp = ADJ_VEN;
	vdc5fb_write(priv, ADJ0_UPDATE, tmp);
#ifdef VDC5
	vdc5fb_write(priv, ADJ1_UPDATE, tmp);
#endif /* VDC5 */

	tmp = (GR_IBUS_VEN | GR_P_VEN | GR_UPDATE);
	vdc5fb_update_regs(priv, GR2_UPDATE, tmp, 1);
	vdc5fb_update_regs(priv, GR3_UPDATE, tmp, 1);

	tmp = (GR_P_VEN | GR_UPDATE);
	vdc5fb_update_regs(priv, GR_VIN_UPDATE, tmp, 1);

	tmp = OUTCNT_VEN;
	vdc5fb_update_regs(priv, OUT_UPDATE, tmp, 1);
	tmp = GAM_VEN;
	vdc5fb_update_regs(priv, GAM_G_UPDATE, tmp, 1);
	vdc5fb_update_regs(priv, GAM_B_UPDATE, tmp, 1);
	vdc5fb_update_regs(priv, GAM_R_UPDATE, tmp, 1);
	tmp = TCON_VEN;
	vdc5fb_update_regs(priv, TCON_UPDATE, tmp, 1);

	return 0;
}

/* Set a New Video mode (resolution)
 *
 * Assuming you want to support multiple resolutions or color depths,
 * you could use this to change your configuration during normal usage.
 * However for most cases, usually this is only done once after boot.
 */
static void vdc5fb_set_videomode(struct vdc5fb_priv *priv,
	struct fb_videomode *new, int bits_per_pixel)
{
	struct vdc5fb_pdata *pdata = priv_to_pdata(priv);
	struct fb_videomode *mode = pdata->videomode;
	u32 tmp;

	if (new)
		mode = new;
	priv->videomode = mode;

	/* Make a copy of the videomode struct (this one is only temporary).
	 * We need this when we want modify the VDC layers manually, but need
	 * to refer back to the info in this structure */
	priv->videomode_copy = *priv->videomode;
	/* switch our pointer to our local copy */
	priv->videomode = &(priv->videomode_copy);

	if (priv->info->screen_base)	/* sanity check */
		vdc5fb_clear_fb(priv);

	if (pdata->use_lvds) {
		vdc5fb_init_lvds(priv);
	}
	else {
		/* This driver assumes P1 clock will always be used
		   as the panel clock source for both RGB and LVDS */
		BUG_ON( pdata->panel_icksel != ICKSEL_P1CLK);

		if (vdc5fb_set_panel_clock(priv, mode) < 0)
				dev_err(&priv->pdev->dev, "cannot get dcdr\n");
	}

	dev_info(&priv->pdev->dev,
		"%s: [%s] dotclock %lu.%03u MHz, dcdr %u\n",
		priv->dev_name, pdata->name,
		(priv->dc / 1000000),
		(unsigned int)((priv->dc % 1000000) / 1000),
		priv->dcdr);

	priv->res_fh = mode->hsync_len + mode->left_margin + priv->panel_pixel_xres
		+ mode->right_margin;
	priv->res_fv = mode->vsync_len + mode->upper_margin + priv->panel_pixel_yres
		+ mode->lower_margin;
	priv->rr = (priv->dc / (priv->res_fh * priv->res_fv));

	tmp =  mode->xres * mode->yres * (bits_per_pixel / 8);
	priv->flm_off = tmp & ~0xfff;	/* page align */
	if (tmp & 0xfff)
		priv->flm_off += 0x1000;
	priv->flm_num = 0;

	vdc5fb_init_syscnt(priv);
	vdc5fb_init_sync(priv);
	vdc5fb_init_scalers(priv);
	vdc5fb_init_graphics(priv);
	vdc5fb_init_outcnt(priv);
	vdc5fb_init_tcon(priv);

	vdc5fb_update_all(priv);

	vdc5fb_clear_fb(priv);
}

/* Adjust Brightness
 * Done in the output controller (final block in video pipeline)
 */
static int vdc5fb_put_bright(struct vdc5fb_priv *priv,
	struct fbio_bright *param)
{
	uint32_t tmp;

	tmp = PBRT_G(param->pbrt_g);
	vdc5fb_write(priv, OUT_BRIGHT1, tmp);
	tmp = PBRT_B(param->pbrt_b);
	tmp |= PBRT_R(param->pbrt_r);
	vdc5fb_write(priv, OUT_BRIGHT2, tmp);
	vdc5fb_update_regs(priv, OUT_UPDATE, OUTCNT_VEN, 1);

	return 0;
}

static int vdc5fb_get_bright(struct vdc5fb_priv *priv,
	struct fbio_bright *param)
{
	uint32_t tmp;

	tmp = vdc5fb_read(priv, OUT_BRIGHT1);
	param->pbrt_g = (tmp & 0x3ffu);
	tmp = vdc5fb_read(priv, OUT_BRIGHT2);
	param->pbrt_b = ((tmp >> 16) & 0x3ffu);
	param->pbrt_r = (tmp & 0x3ffu);

	return 0;
}

/* Adjust Contrast
 * Done in the output controller (final block in video pipeline)
 */
static int vdc5fb_put_contrast(struct vdc5fb_priv *priv,
	struct fbio_contrast *param)
{
	uint32_t tmp;

	tmp = CONT_G(param->cont_g);
	tmp |= CONT_B(param->cont_b);
	tmp |= CONT_R(param->cont_r);
	vdc5fb_write(priv, OUT_CONTRAST, tmp);
	vdc5fb_update_regs(priv, OUT_UPDATE, OUTCNT_VEN, 1);

	return 0;
}

static int vdc5fb_get_contrast(struct vdc5fb_priv *priv,
	struct fbio_contrast *param)
{
	uint32_t tmp;

	tmp = vdc5fb_read(priv, OUT_CONTRAST);
	param->cont_g = ((tmp >> 16) & 0xffu);
	param->cont_b = ((tmp >> 8) & 0xffu);
	param->cont_r = (tmp & 0xffu);

	return 0;
}

/* for double buffering..not implemented */
static int vdc5fb_put_frame(struct vdc5fb_priv *priv,
	struct fbio_frame *param)
{

//	struct vdc5fb_pdata *pdata = priv_to_pdata(priv);
//	uint32_t tmp;
//
//	if (param->fr_num >= pdata->flm_max)
//		return -EINVAL;
//
//	tmp = vdc5fb_read(priv, GR_OIR_FLM3);
//	tmp &= ~0x3ffu;
//	tmp |= GR_FLM_NUM(param->fr_num);
//	vdc5fb_write(priv, GR_OIR_FLM3, tmp);
//	vdc5fb_update_regs(priv, GR_OIR_UPDATE, GR_IBUS_VEN, 1);

	return 0;
}

/* for double buffering..not implemented */
static int vdc5fb_get_frame(struct vdc5fb_priv *priv,
	struct fbio_frame *param)
{

//	struct vdc5fb_pdata *pdata = priv_to_pdata(priv);
//	uint32_t tmp;
//
//	tmp = vdc5fb_read(priv, GR_OIR_FLM3);
//	param->fr_max = pdata->flm_max;
//	param->fr_num = (tmp & 0x3ffu);

	return 0;
}

static int vdc5fb_setcolreg(u_int regno,
	u_int red, u_int green, u_int blue,
	u_int transp, struct fb_info *info)
{
	u32 *palette = info->pseudo_palette;

	if (regno >= PALETTE_NR)
		return -EINVAL;

	/* only FB_VISUAL_TRUECOLOR supported */
	red    >>= 16 - info->var.red.length;
	green  >>= 16 - info->var.green.length;
	blue   >>= 16 - info->var.blue.length;
	transp >>= 16 - info->var.transp.length;

	palette[regno] = (red << info->var.red.offset) |
		(green << info->var.green.offset) |
		(blue << info->var.blue.offset) |
		(transp << info->var.transp.offset);

	return 0;
}

static int vdc5fb_ioctl(struct fb_info *info, unsigned int cmd,
	unsigned long arg)
{
	struct vdc5fb_priv *priv = (struct vdc5fb_priv *)info->par;

	switch (cmd) {
	case FBIOGET_VSCREENINFO:	/* 0x00 */
	case FBIOGET_FSCREENINFO:	/* 0x02 */
		/* done by framework */
		break;
	case FBIOPUT_VSCREENINFO:	/* 0x01 */
		break;

	case FBIOPAN_DISPLAY:		/* 0x06 */
		break;

	case FBIOGETCMAP:		/* 0x04 */
	case FBIOPUTCMAP:		/* 0x05 */
	case FBIO_CURSOR:		/* 0x08 */
	case FBIOGET_CON2FBMAP:		/* 0x0F */
	case FBIOPUT_CON2FBMAP:		/* 0x10 */
	case FBIOBLANK:			/* 0x11 */
	/* Done by higher, NG */
		break;

	case FBIOGET_VBLANK:		/* 0x12 */
	case FBIO_ALLOC:		/* 0x13 */
	case FBIO_FREE:			/* 0x14 */
	case FBIOGET_GLYPH:		/* 0x15 */
	case FBIOGET_HWCINFO:		/* 0x16 */
	case FBIOPUT_MODEINFO:		/* 0x17 */
	case FBIOGET_DISPINFO:		/* 0x18 */
	case FBIO_WAITFORVSYNC:		/* 0x20 */
	/* Done by higher, NG (not supported) */
	/* vdc5fb_ioctl is also called */
		return -EINVAL;
		break;

	default:
	/* 0x03, 0x07, 0x09-0x0E, 0x19-0x1F, 0x21- (unknown) */
	/* vdc5fb_ioctl is called */
		return -EINVAL;
		break;

	case FBIOPUT_BRIGHT:
		{
			struct fbio_bright bright;

			if (copy_from_user(&bright, (void __user *)arg,
				sizeof(bright)))
				return -EFAULT;
			if (bright.pbrt_r > 1023)
				bright.pbrt_r = 1023;
			if (bright.pbrt_g > 1023)
				bright.pbrt_g = 1023;
			if (bright.pbrt_b > 1023)
				bright.pbrt_b = 1023;
			return vdc5fb_put_bright(priv, &bright);
		}
	case FBIOGET_BRIGHT:
		{
			struct fbio_bright bright;
			int ret;

			ret = vdc5fb_get_bright(priv, &bright);
			if (ret < 0)
				return ret;
			if (copy_to_user((void __user *)arg, &bright,
				sizeof(bright)))
				return -EFAULT;
			return 0;
		}

	case FBIOPUT_CONTRAST:
		{
			struct fbio_contrast contrast;

			if (copy_from_user(&contrast, (void __user *)arg,
				sizeof(contrast)))
				return -EFAULT;
			if (contrast.cont_r > 255)
				contrast.cont_r = 255;
			if (contrast.cont_g > 255)
				contrast.cont_g = 255;
			if (contrast.cont_b > 255)
				contrast.cont_b = 255;
			return vdc5fb_put_contrast(priv, &contrast);
		}
	case FBIOGET_CONTRAST:
		{
			struct fbio_contrast contrast;
			int ret;

			ret = vdc5fb_get_contrast(priv, &contrast);
			if (ret < 0)
				return ret;
			if (copy_to_user((void __user *)arg, &contrast,
				sizeof(contrast)))
				return -EFAULT;
			return 0;
		}
	case FBIOPUT_FRAME:
		{
			struct fbio_frame frame;

			if (copy_from_user(&frame, (void __user *)arg,
				sizeof(frame)))
				return -EFAULT;
			return vdc5fb_put_frame(priv, &frame);
		}
	case FBIOGET_FRAME:
		{
			struct fbio_frame frame;
			int ret;

			ret = vdc5fb_get_frame(priv, &frame);
			if (ret < 0)
				return ret;
			if (copy_to_user((void __user *)arg, &frame,
				sizeof(frame)))
				return -EFAULT;
			return 0;
		}
	}

	return 0;
}

static struct fb_fix_screeninfo vdc5fb_fix = {
	.id		= "vdc5fb",
	.type		= FB_TYPE_PACKED_PIXELS,
	.visual		= FB_VISUAL_TRUECOLOR,
	.accel		= FB_ACCEL_NONE,
};

static int vdc5fb_check_var(struct fb_var_screeninfo *var,
	struct fb_info *info)
{
/*	struct fb_fix_screeninfo *fix = &info->fix;	*/
	struct vdc5fb_priv *priv = info->par;
	struct vdc5fb_pdata *pdata = priv_to_pdata(priv);

	if (var->xres != pdata->videomode->xres)
		return -EINVAL;
	if (var->xres_virtual != pdata->videomode->xres)
		return -EINVAL;
	if (var->yres != pdata->videomode->yres)
		return -EINVAL;
	return 0;
}

static int vdc5fb_set_par(struct fb_info *info)
{
	struct fb_var_screeninfo *var = &info->var;
	struct vdc5fb_priv *priv = info->par;
	struct vdc5fb_pdata *pdata = priv_to_pdata(priv);
	struct fb_videomode mode;

/*	pm_runtime_get_sync();	*/

	memcpy(&mode, pdata->videomode, sizeof(mode));
	mode.name = NULL;
	mode.refresh = 0;
/*	mode.xres = var->xres;		*/
/*	mode.yres = var->yres;		*/
	mode.pixclock = var->pixclock;
	mode.left_margin = var->left_margin;
	mode.right_margin = var->right_margin;
	mode.upper_margin = var->upper_margin;
	mode.lower_margin = var->lower_margin;
	mode.hsync_len = var->hsync_len;
	mode.vsync_len = var->vsync_len;
	mode.sync = var->sync;
/*	mode.vmode = var->vmode;	*/
	mode.flag = 0;

	vdc5fb_set_videomode(priv, &mode, var->bits_per_pixel);

/*	pm_runtime_put_sync();	*/
	return 0;
}

static int vdc5fb_pan_display(struct fb_var_screeninfo *var,
	struct fb_info *info)
{
	struct vdc5fb_priv *priv = info->par;
	unsigned long start, end;
	u32 tmp;
/*	struct vdc5fb_pdata *pdata = priv_to_pdata(priv);	*/

/*	pm_runtime_get_sync();	*/

	start = var->yoffset * info->fix.line_length;
	start += var->xoffset * (info->var.bits_per_pixel >> 3);
	end = start + info->var.yres * info->fix.line_length;

//	vdc5fb_write(priv, GR_OIR_FLM2, priv->fb_phys_addr + start);
//	tmp = (GR_IBUS_VEN | GR_P_VEN | GR_UPDATE);
//	vdc5fb_update_regs(priv, GR_OIR_UPDATE, tmp, 1);

	/* Assume Graphics layer 2 */
	vdc5fb_write(priv, GR2_FLM2, priv->fb_phys_addr + start);
	tmp = (GR_IBUS_VEN | GR_P_VEN | GR_UPDATE);
	vdc5fb_update_regs(priv, GR2_UPDATE, tmp, 1);

/*	pm_runtime_put_sync();	*/
	return 0;
}

static struct fb_ops vdc5fb_ops = {
	.owner          = THIS_MODULE,
	.fb_read        = fb_sys_read,
	.fb_write       = fb_sys_write,
	.fb_check_var	= vdc5fb_check_var,
	.fb_set_par	= vdc5fb_set_par,
	.fb_setcolreg	= vdc5fb_setcolreg,
	.fb_fillrect	= sys_fillrect,
	.fb_copyarea	= sys_copyarea,
	.fb_imageblit	= sys_imageblit,
	.fb_pan_display	= vdc5fb_pan_display,
	.fb_ioctl	= vdc5fb_ioctl,
};

static int vdc5fb_set_bpp(struct fb_var_screeninfo *var, int bpp)
{
	switch (bpp) {
	case 16: /* RGB 565 */
		var->blue.offset = 0;
		var->blue.length = 5;
		var->green.offset = 5;
		var->green.length = 6;
		var->red.offset = 11;
		var->red.length = 5;
		var->transp.offset = 0;
		var->transp.length = 0;
		break;
	case 32: /* ARGB 8888 */
		var->blue.offset = 0;
		var->blue.length = 8;
		var->green.offset = 8;
		var->green.length = 8;
		var->red.offset = 16;
		var->red.length = 8;
		var->transp.offset = 24;
		var->transp.length = 8;
		break;
	default:
		return -EINVAL;
	}
	var->bits_per_pixel = bpp;
	var->red.msb_right = 0;
	var->green.msb_right = 0;
	var->blue.msb_right = 0;
	var->transp.msb_right = 0;
	return 0;
}

static int vdc5fb_start(struct vdc5fb_priv *priv)
{
	int error;

	error = clk_prepare_enable(priv->clk);
	if (error < 0)
		return error;

	return error;
}

static void vdc5fb_stop(struct vdc5fb_priv *priv)
{
	clk_disable_unprepare(priv->clk);
}

static int vdc5fb_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);

	vdc5fb_stop(platform_get_drvdata(pdev));
	return 0;
}

static int vdc5fb_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);

	return vdc5fb_start(platform_get_drvdata(pdev));
}

static const struct dev_pm_ops vdc5fb_dev_pm_ops = {
	.suspend = vdc5fb_suspend,
	.resume = vdc5fb_resume,
};

struct vdc5fb_pdata *vdc5fb_parse_dt(struct platform_device *pdev)
{
	struct vdc5fb_pdata *pdata;
	struct device_node *np = pdev->dev.of_node;
	u32 val;
	u32 val_array[7];

	pdata = devm_kzalloc(&pdev->dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata) {
		dev_err(&pdev->dev, "cannot allocate private data\n");
		return 0;
	}

	if (!of_property_read_string(np, "panel_name", (const char **)&val))
		pdata->name = (char *)val;

	if (!of_property_read_u32(np, "panel_icksel", &val))
		pdata->panel_icksel = val;

	if (!of_property_read_u32(np, "panel_ocksel", &val))
		pdata->panel_ocksel = val;

	if (!of_property_read_u32(np, "channel", &val))
		pdata->channel = val;

	if (!of_property_read_u32(np, "panel_pixel_xres", &val))
		pdata->panel_pixel_xres = val;

	if (!of_property_read_u32(np, "panel_pixel_yres", &val))
		pdata->panel_pixel_yres = val;

	if (!of_property_read_u32(np, "fb_phys_addr", &val))
		pdata->fb_phys_addr = val;

	if (!of_property_read_u32(np, "fb_phys_size", &val))
		pdata->fb_phys_size = val;

	if (!of_property_read_u32(np, "out_format", &val))
		pdata->out_format = val;

	if (!of_property_read_u32(np, "use_lvds", &val))
		pdata->use_lvds	= val;

	if (of_property_read_bool(np, "double_buffer"))
		pdata->double_buffer = 1;

	if (!of_property_read_u32_array(np, "tcon_sel", val_array, 7)) {
		pdata->tcon_sel[LCD_TCON0] = val_array[0];
		pdata->tcon_sel[LCD_TCON1] = val_array[1];
		pdata->tcon_sel[LCD_TCON2] = val_array[2];
		pdata->tcon_sel[LCD_TCON3] = val_array[3];
		pdata->tcon_sel[LCD_TCON4] = val_array[4];
		pdata->tcon_sel[LCD_TCON5] = val_array[5];
		pdata->tcon_sel[LCD_TCON6] = val_array[6];
	}

	return pdata;
}


/************************* sysfs attribute files ************************/
/* These sysfs files will show up under:
	RZA1: ch0 = /sys/devices/platform/fcff7400.display/
	      ch1 = /sys/devices/platform/fcff9400.display/
	RZA2: ch0 = /sys/devices/platform/fcff7400.display/
*/
const char format_names[24][12] = {
	"RGB565",
	"RGB888",
	"ARGB1555",
	"ARGB4444",
	"ARGB8888",
	"CLUT8",
	"CLUT4",
	"CLUT1",
	"YCbCr422",	// GR0_YCC_SWAP=0: Cb/Y0/Cr/Y1
	"YCbCr444",
	"RGBA5551",
	"RGBA8888",
	"prohibited",
	"prohibited",
	"prohibited",
	"prohibited",
	"YCbCr422_0",	// GR0_YCC_SWAP=0: Cb/Y0/Cr/Y1
	"YCbCr422_1",	// GR0_YCC_SWAP=1: Y0/Cb/Y1/Cr
	"YCbCr422_2",	// GR0_YCC_SWAP=2: Cr/Y0/Cb/Y1
	"YCbCr422_3",	// GR0_YCC_SWAP=3: Y0/Cr/Y1/Cb
	"YCbCr422_4",	// GR0_YCC_SWAP=4: Y1/Cr/Y0/Cb
	"YCbCr422_5",	// GR0_YCC_SWAP=5: Cr/Y1/Cb/Y0
	"YCbCr422_6",	// GR0_YCC_SWAP=6: Y1/Cb/Y0/Cr
	"YCbCr422_7",	// GR0_YCC_SWAP=7: Cb/Y1/Cr/Y0
};

const char readswap_names[8][14] = {
	"no_swap",
	"swap_8",
	"swap_16",
	"swap_16_8",
	"swap_32",
	"swap_32_8",
	"swap_32_16",
	"swap_32_16_8",
};

static ssize_t vdc5fb_show_layer(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct vdc5fb_priv *priv = dev_get_drvdata(dev);
	int layer = attr->attr.name[5] - '0';
	int count = 0;
	u32 gr_flm6;

	count += sprintf(buf + count, "xres = %u\n", priv->pdata->layers[layer].xres);
	count += sprintf(buf + count, "yres = %u\n", priv->pdata->layers[layer].yres);
	count += sprintf(buf + count, "x_offset = %u\n", priv->pdata->layers[layer].x_offset);
	count += sprintf(buf + count, "y_offset = %u\n", priv->pdata->layers[layer].y_offset);
	count += sprintf(buf + count, "base = 0x%08X\n", priv->pdata->layers[layer].base);
	count += sprintf(buf + count, "bpp = %u\n", priv->pdata->layers[layer].bpp);

	gr_flm6 = priv->pdata->layers[layer].format;
	if ((gr_flm6 >> 28) == GR_FORMAT_YCbCr422)
		gr_flm6 = 16 + ((gr_flm6 >> 13) & 0x7);	// also look at GR_YCC_SWAP[2:0]
	else
		gr_flm6 = gr_flm6 >> 28;
	count += sprintf(buf + count, "format = %s\n", format_names[gr_flm6]);

	gr_flm6 = (priv->pdata->layers[layer].format >> 10) & 7;
	count += sprintf(buf + count, "read_swap = %s\n", readswap_names[gr_flm6]);

	count += sprintf(buf + count, "blend = %u\n", priv->pdata->layers[layer].blend);

	/* Return the number characters (bytes) copied to the buffer */
	return count;
}

#define CHARS_TO_HEX(a,b) (a<<8 | b)
static ssize_t vdc5fb_store_layer(struct device *dev,
		struct device_attribute *attr, const char *buf,
		size_t count)
{
	struct vdc5fb_priv *priv = dev_get_drvdata(dev);
	int layer = attr->attr.name[5] - '0';
	int i, j;
	char val_name[20] = {0};
	u16 next_value;
	int found = 0;
	int find_next = 0;
	u32 gr_format;
	u32 gr_rdswa;
	char new_format[14];

	for (i=0; i < count; i++) {
		// Find next value to scan
		if (find_next) {
			// Advance to next item (line or comma separated)
			if ((buf[i] == '\n') || (buf[i] == ','))
				find_next = 0;

			continue;
		}

		// Remove any leading white spaces or blank lines
		if ((buf[i] == ' ') || (buf[i] == '\t') || (buf[i] == '\n') || (buf[i] == '\r'))
			continue;

		// Use first 2 characters to determine the value name.
		// Convert it to a 16-bit value so we can use switch-case
		next_value = CHARS_TO_HEX(buf[i], buf[i+1]);

		switch (next_value) {

		case CHARS_TO_HEX('x','r'):	//u32 xres;
			found = sscanf(buf + i, "%s = %u", val_name, &(priv->pdata->layers[layer].xres));
			break;
		case CHARS_TO_HEX('y','r'):	//u32 yres;
			found = sscanf(buf + i, "%s = %u", val_name, &(priv->pdata->layers[layer].yres));
			break;
		case CHARS_TO_HEX('x','_'):	//u32 x_offset;
			found = sscanf(buf + i, "%s = %u", val_name, &(priv->pdata->layers[layer].x_offset));
			break;
		case CHARS_TO_HEX('y','_'):	//u32 y_offset;
			found = sscanf(buf + i, "%s = %u", val_name, &(priv->pdata->layers[layer].y_offset));
			break;
		case CHARS_TO_HEX('b','a'):	//u32 base;
			found = sscanf(buf + i, "%s = %x", val_name, &(priv->pdata->layers[layer].base));
			break;
		case CHARS_TO_HEX('b','p'):	//u32 bpp;
			found = sscanf(buf + i, "%s = %u", val_name, &(priv->pdata->layers[layer].bpp));
			break;
		case CHARS_TO_HEX('f','o'):	//u32 format;
			// passed in as a string
			found = sscanf(buf + i, "%s = %s", val_name, new_format);
			gr_format = 0xFF;
			for (j=0; j<24; j++) {
				if (!strcmp(new_format, format_names[j])) {
					gr_format = j;
					break;
				}
			}
			if (gr_format == 0xFF) {
				printk("[ERROR] Unknown format: %s\n", new_format);
				return count;
			}
			if (gr_format == GR_FORMAT_RGB565)
				priv->pdata->layers[layer].format = GR_FORMAT(GR_FORMAT_RGB565) | GR_RDSWA(6);
			else if (gr_format == GR_FORMAT_ARGB8888)
				priv->pdata->layers[layer].format = GR_FORMAT(GR_FORMAT_ARGB8888) | GR_RDSWA(4);
			else if (gr_format >= 16)	// add in GR_YCC_SWAP
				priv->pdata->layers[layer].format = GR_FORMAT(GR_FORMAT_YCbCr422) | GR_YCC_SWAP(gr_format - 16);
			else
				priv->pdata->layers[layer].format = GR_FORMAT(gr_format);
			break;
		case CHARS_TO_HEX('r','e'):	// read_swap (part of 'fromat)
			// passed in as a string
			found = sscanf(buf + i, "%s = %s", val_name, new_format);
			gr_rdswa = 0xFF;
			for (j=0; j<8; j++) {
				if (!strcmp(new_format, readswap_names[j])) {
					gr_rdswa = j;
					break;
				}
			}
			if (gr_rdswa == 0xFF) {
				printk("[ERROR] Unknown read_swap: %s\n", new_format);
				return count;
			}
			// Just change the GR_RDSWA bits
			priv->pdata->layers[layer].format &= 0xFFFFE3FF;
			priv->pdata->layers[layer].format |= GR_RDSWA(gr_rdswa);
			break;
		case CHARS_TO_HEX('b','l'):	//u32 blend;
			found = sscanf(buf + i, "%s = %u", val_name, &(priv->pdata->layers[layer].blend));
			break;
		default:
			printk("[ERROR] Bad parameter passed staring at: %s\n",buf + i);
			break;
		}

		if (found != 2) {
			printk("[ERROR] Bad parameter passed: \"%s\"\n",val_name);
			return count;
		}

		find_next = 1;
	}

	/* Reinitialize all layers */
	vdc5fb_init_scalers(priv);
	vdc5fb_init_graphics(priv);
	vdc5fb_update_all(priv);

	/* Return the number of characters (bytes) we used from the buffer */
	return count;
}

static ssize_t vdc5fb_show_color_replace(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct vdc5fb_priv *priv = dev_get_drvdata(dev);
	int layer;
	int count = 0;
	u32 update_addr[4];
	uint32_t ab7, ab8, ab9;

	update_addr[0] = (u32)priv->base + vdc5fb_offsets[GR0_UPDATE];
#ifdef VDC5
	update_addr[1] = (u32)priv->base + vdc5fb_offsets[GR1_UPDATE];
#endif /* VDC5 */
	update_addr[2] = (u32)priv->base + vdc5fb_offsets[GR2_UPDATE];
	update_addr[3] = (u32)priv->base + vdc5fb_offsets[GR3_UPDATE];

	for (layer=0; layer<4; layer++) {
#ifdef VDC6
		if (layer == 1)
			continue;
#endif /* VDC6 */

		count += sprintf(buf + count, "layer%d = ", layer);

		/* Check AB7 to determine if enabled */
		ab7 = ioread32((void *)update_addr[layer] + GR_AB7_OFFSET);
		if (ab7 & 1) {
			ab8 = ioread32((void *)update_addr[layer] + GR_AB8_OFFSET);
			ab9 = ioread32((void *)update_addr[layer] + GR_AB9_OFFSET);
			count += sprintf(buf + count, "0x%08X -> 0x%08X\n", ab8, ab9);
		}
		else {
			count += sprintf(buf + count, "off\n");
		}
	}
	return count;
}

static ssize_t vdc5fb_store_color_replace(struct device *dev,
		struct device_attribute *attr, const char *buf,
		size_t count)
{
	struct vdc5fb_priv *priv = dev_get_drvdata(dev);
	int i;
	int found = 0;
	int find_next = 0;
	u32 new_ab7, new_ab8, new_ab9;
	u16 layer;
	char tmp[20];
	u32 update_addr[4];

	update_addr[0] = (u32)priv->base + vdc5fb_offsets[GR0_UPDATE];
#ifdef VDC5
	update_addr[1] = (u32)priv->base + vdc5fb_offsets[GR1_UPDATE];
#endif /* VDC5 */
	update_addr[2] = (u32)priv->base + vdc5fb_offsets[GR2_UPDATE];
	update_addr[3] = (u32)priv->base + vdc5fb_offsets[GR3_UPDATE];


	for (i=0; i < count; i++) {
		// Find next value to scan
		if (find_next) {
			// Advance to next item (line or comma separated)
			if ((buf[i] == '\n') || (buf[i] == ','))
				find_next = 0;

			continue;
		}

		// Remove any leading white spaces or blank lines
		if ((buf[i] == ' ') || (buf[i] == '\t') || (buf[i] == '\n') || (buf[i] == '\r'))
			continue;

		/* 6th character is the layer number "layer0" */
		layer = buf[i + 5] - '0';
		if (layer > 3) {
			printk("[ERROR] Bad parameter passed (layer number = %d)\n", layer);
			return count;
		}

		// Skip over layer0 = "
		i += 9;

		sscanf(buf + i, "%s", tmp);
		if (!strcmp(tmp, "off")) {

			// Just set AB7 bit 0 to zero and be done
			new_ab7 = ioread32((void *)update_addr[layer] + GR_AB7_OFFSET);
			new_ab7 &= ~1;
			vdc5fb_iowrite32(new_ab7, update_addr[layer] + GR_AB7_OFFSET);
			vdc5fb_iowrite32(0x10, update_addr[layer]);// (commit new register settings)
			return count;
		}
		else {
			found = sscanf(buf + i, "0x%X to 0x%X", &new_ab8, &new_ab9);
			if (found != 2) {
				printk("[ERROR] Bad parameter passed\n");
				return count;
			}
			/* Setting for RGB-Chroma-key support on Layer */
			vdc5fb_iowrite32(new_ab8, update_addr[layer] + GR_AB8_OFFSET);
			vdc5fb_iowrite32(new_ab9, update_addr[layer] + GR_AB9_OFFSET);
			new_ab7 = ioread32((void *)(update_addr[layer] + GR_AB7_OFFSET));
			new_ab7 |= 1;
			vdc5fb_iowrite32(new_ab7, update_addr[layer] + GR_AB7_OFFSET);
			vdc5fb_iowrite32(0x10, update_addr[layer]);// (commit new register settings)

			return count;
		}
	}

	/* Return the number of characters (bytes) we used from the buffer */
	return count;
}


static struct device_attribute vdc5fb_device_attributes[] = {
	__ATTR(	layer0, 			/* the name of the virtual file will appear as */
		0644, 				/* Virtual file permissions */
		vdc5fb_show_layer,		/* file read handler */
		vdc5fb_store_layer),		/* file write handler */
#ifdef VDC5
	__ATTR(layer1, 0644, vdc5fb_show_layer, vdc5fb_store_layer),
#endif
	__ATTR(layer2, 0644, vdc5fb_show_layer, vdc5fb_store_layer),
	__ATTR(layer3, 0644, vdc5fb_show_layer, vdc5fb_store_layer),
	__ATTR(color_replace, 0644, vdc5fb_show_color_replace, vdc5fb_store_color_replace),
};

static int vdc5fb_probe(struct platform_device *pdev)
{
	int error = -EINVAL;
	struct vdc5fb_priv *priv = NULL;
	struct vdc5fb_pdata *pdata;
	struct fb_info *info = NULL;
	struct resource *res;
	void *buf;
	struct fb_videomode *of_mode;
	struct device_node *np;
	int ret;
	u32 bpp;
	int i;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv) {
		dev_err(&pdev->dev, "cannot allocate private data\n");
		error = -ENOMEM;
		goto error;
	}
	platform_set_drvdata(pdev, priv);
	priv->pdev = pdev;
	priv->dev_name = dev_name(&pdev->dev);

	/* Platform Data */
	if (pdev->dev.of_node) {
		pdata = vdc5fb_parse_dt(pdev);
	}
	else {
		pdata = pdev->dev.platform_data;
	}
	if (!pdata) {
		dev_err(&pdev->dev, "cannot get platform data\n");
		goto error;
	}
	priv->pdata = pdata;

	/* Register Base Address */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "cannot get resources (reg)\n");
		goto error;
	}
	priv->base = ioremap_nocache(res->start, resource_size(res));
	if (!priv->base) {
		dev_err(&pdev->dev, "cannot ioremap (reg)\n");
		goto error;
	}

	/*
	 * LVDS Registers
	 * Because the LVDS registers sit between ch0 and ch1, we map
	 * the LVDS registers separately.
	 * See function vdc5fb_read() and vdc5fb_write()
	 */
	if (pdata->use_lvds) {
		res = platform_get_resource(pdev, IORESOURCE_MEM, 2);
		if (!res) {
			dev_err(&pdev->dev, "cannot get resources (lvds)\n");
			goto error;
		}
		priv->base_lvds = ioremap_nocache(res->start, resource_size(res));
		if (!priv->base_lvds) {
			dev_err(&pdev->dev, "cannot ioremap (lvds)\n");
			goto error;
		}
	}

	/* Clocks */
	priv->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(priv->clk)) {
		dev_err(&pdev->dev, "cannot get clock \n");
		goto error;
	}
	if (vdc5fb_start(priv) ) {
		dev_err(&pdev->dev, "cannot start hardware\n");
		goto error;
	}


#if 0 /* Interrupts are not used with this driver */
	error = vdc5fb_init_irqs(priv);
	if (error < 0) {
		dev_err(&pdev->dev, "cannot init irqs\n");
		goto error;
	}
#endif

	info = framebuffer_alloc(0, &pdev->dev);
	if (!info) {
		dev_err(&pdev->dev, "cannot allocate fb_info\n");
		goto error;
	}
	priv->info = info;

	/* Video Modes */
	if (pdev->dev.of_node) {

		np = of_parse_phandle(pdev->dev.of_node, "display", 0);
		if (!np) {
			dev_err(&pdev->dev, "No display defined in devicetree\n");
			return -1;
		}

		if (of_property_read_u32(np, "bits-per-pixel", &bpp)) {
			dev_err(&pdev->dev, "No bits-per-pixel found in \'display0\' node\n");
			goto error;
		}

		/* allocate a videomode for our DT to fill in */
		of_mode = devm_kzalloc(&pdev->dev, sizeof(*of_mode), GFP_KERNEL);
		if (!of_mode) {
			dev_err(&pdev->dev, "cannot allocate videomode\n");
			error = -ENOMEM;
			goto error;
		}

		ret = of_get_fb_videomode(np, of_mode, OF_USE_NATIVE_MODE);
		if (ret)
			dev_err(&pdev->dev, "Failed to get video mode from DT\n");
		else
			pdata->videomode = of_mode;

		/* A fb_videomode structure does not have all the same parameters
		 * that were specified in the DT.
		 * So we have to also get the full video mode from DT and pull
		 * out the information we need. */
		{
			struct videomode of_vm;
			ret = of_get_videomode(np, &of_vm, OF_USE_NATIVE_MODE);
			if (ret)
				dev_err(&pdev->dev, "Failed to get full video mode from DT\n");
			else {
				/* save value of pixelclk-active */
				if (of_vm.flags & DISPLAY_FLAGS_PIXDATA_POSEDGE)
					pdata->data_clk_phase = 1;
			}
		}
	}

	if (!pdata->videomode) {
		dev_err(&pdev->dev, "Failed to get video mode\n");
		goto error;

	}

	/* By default, just use graphics layer 2 */
	/* Graphics 2 - Image Synthesizer */
	/* Full LCD Panel - will be /dev/fb0 */
	pdata->layers[2].xres		= pdata->videomode->xres;
	pdata->layers[2].yres		= pdata->videomode->yres;
	pdata->layers[2].x_offset	= 0;
	pdata->layers[2].y_offset	= 0;
	pdata->layers[2].bpp		= bpp;	/* from display node */
	if (bpp == 16)
		pdata->layers[2].format	= GR_FORMAT(GR_FORMAT_RGB565) | GR_RDSWA(6);
	else
		pdata->layers[2].format	= GR_FORMAT(GR_FORMAT_ARGB8888) | GR_RDSWA(4);
	//pdata->layers[2].base		= (get this from IORESOURCE_MEM below)
	pdata->layers[2].blend		= 0;

	info->fbops = &vdc5fb_ops;
	INIT_LIST_HEAD(&info->modelist);
	fb_add_videomode(pdata->videomode, &info->modelist);


	info->var.xres = info->var.xres_virtual = pdata->videomode->xres;
	info->var.yres = info->var.yres_virtual = pdata->videomode->yres;
	if (pdata->double_buffer)
		info->var.yres_virtual *= 2;
	info->var.width = pdata->panel_width;
	info->var.height = pdata->panel_height;
	info->var.activate = FB_ACTIVATE_NOW;
	info->pseudo_palette = priv->pseudo_palette;
	error = vdc5fb_set_bpp(&info->var, bpp);
	if (error) {
		dev_err(&pdev->dev, "cannot set bpp\n");
		goto error;
	}

	info->fix = vdc5fb_fix;
	info->fix.type = FB_TYPE_PACKED_PIXELS;
	info->fix.type_aux = 0;
	info->fix.line_length = info->var.xres * (info->var.bits_per_pixel / 8);
	if (pdata->double_buffer)
		info->fix.smem_len = info->fix.line_length * info->var.yres * 2;
	else
		info->fix.smem_len = info->fix.line_length * info->var.yres;

	/* Frame Buffer (Physical) Address
	 *
	 * Set to 0 if you want driver to allocated it from system memory.
	 * However, you cannot do that if the system memory is SDRAM because
	 * the LCD controller cannot use a frame buffer located in SDRAM, it
	 * has to be located in internal memory
	 */
	if (priv->pdata->fb_phys_addr) {

		priv->fb_phys_addr = priv->pdata->fb_phys_addr;

		if (pdata->double_buffer)
			buf = ioremap_nocache(priv->pdata->fb_phys_addr,
					      priv->pdata->fb_phys_size * 2);
		else
			buf = ioremap_nocache(priv->pdata->fb_phys_addr,
					      priv->pdata->fb_phys_size);
		priv->fb_nofree = 1;
		pdata->layers[2].base = priv->pdata->fb_phys_addr;
	} else {
		/* Allocate a frame buffer from system memory */
#if 0
		/* write combine for better write efficiency, but not good
		 * if you do reads of the frame buffer */
		buf = dma_alloc_wc(&pdev->dev, info->fix.smem_len,
			&priv->fb_phys_addr, GFP_KERNEL);
#else
		buf = dma_alloc_coherent(&pdev->dev, info->fix.smem_len,
			&priv->fb_phys_addr, GFP_KERNEL);
#endif
		priv->fb_nofree = 0;
		if (!buf) {
			dev_err(&pdev->dev, "cannot allocate buffer\n");
			goto error;
		}
	}

	info->flags = FBINFO_FLAG_DEFAULT;

	error = fb_alloc_cmap(&info->cmap, PALETTE_NR, 0);
	if (error < 0) {
		dev_err(&pdev->dev, "cannot allocate cmap\n");
		goto error;
	}

	info->fix.smem_start = priv->fb_phys_addr;
	info->screen_base = buf;
	info->device = &pdev->dev;
	info->par = priv;

	info->var.xres_virtual = info->var.xres;
	if (pdata->double_buffer) {
		info->var.yres_virtual = info->var.yres * 2;
		info->fix.ypanstep = 1;	/* can step 1 line at a time */
	}
	else
		info->var.yres_virtual = info->var.yres;
	info->var.xoffset = 0;
	info->var.yoffset = 0;
	info->var.pixclock = pdata->videomode->pixclock;
	info->var.sync = 0;
	info->var.grayscale = 0;
	info->var.accel_flags = 0;
	info->var.left_margin = pdata->videomode->left_margin;
	info->var.right_margin = pdata->videomode->right_margin;
	info->var.upper_margin = pdata->videomode->upper_margin;
	info->var.lower_margin = pdata->videomode->lower_margin;
	info->var.hsync_len = pdata->videomode->hsync_len;
	info->var.vsync_len = pdata->videomode->vsync_len;

	if (!priv->pdata->panel_pixel_xres) {
		/* If panel_pixel_xres,_yres were not set in the platform data,
		 * assume the panel is the same size as in the video mode */
		priv->panel_pixel_xres = pdata->videomode->xres;
		priv->panel_pixel_yres = pdata->videomode->yres;
	}
	else {
		/* copy from platform data */
		priv->panel_pixel_xres = pdata->panel_pixel_xres;
		priv->panel_pixel_yres = pdata->panel_pixel_yres;
	}

	error = register_framebuffer(info);
	if (error < 0)
		goto error;

	/* Add our sysfs interface virtual files to the system */
	for (i = 0; i < ARRAY_SIZE(vdc5fb_device_attributes); i++) {
		ret = device_create_file(&(pdev->dev), &vdc5fb_device_attributes[i]);
		if (ret < 0) {
			printk(KERN_ERR "device_create_file error\n");
			break;
		}
	}

	dev_info(info->dev,
		"registered %s as %ux%u @ %u Hz, %d bpp.\n",
		priv->dev_name,
		info->var.xres,
		info->var.yres,
		priv->rr,
		info->var.bits_per_pixel);

	return 0;

error:
	dev_err(&pdev->dev, "VDC5 driver initialization Failed.\n");

	/* NOTE: In a real driver, you would probably try to unwind
	 * the resources your acquired and registered. However,
	 * if something is not working right, most like you are still
	 * in your development phase so there's no point in cleaning
	 * up the system.
	 */
	//vdc5fb_stop(priv);
	//unregister_framebuffer(priv->info);
	//fb_dealloc_cmap(&info->cmap);
	//if (priv->fb_nofree)
	//		iounmap(priv->base);
	//dma_free_writecombine(&pdev->dev, info->fix.smem_len,
	//		info->screen_base, info->fix.smem_start);
	//fb_destroy_modelist(&info->modelist);
	//framebuffer_release(info);
	//vdc5fb_deinit_irqs(priv);

	return error;
}

static int vdc5fb_remove(struct platform_device *pdev)
{
	struct vdc5fb_priv *priv = platform_get_drvdata(pdev);
	struct fb_info *info;

	if (priv->info->dev)
		unregister_framebuffer(priv->info);

	vdc5fb_stop(priv);

	info = priv->info;

	fb_dealloc_cmap(&info->cmap);
	if (priv->fb_nofree)
		iounmap(priv->base);
	else
		dma_free_writecombine(&pdev->dev, info->fix.smem_len,
			info->screen_base, info->fix.smem_start);

	fb_destroy_modelist(&info->modelist);
	framebuffer_release(info);
#if 0 /* interrupts not used */
	vdc5fb_deinit_irqs(priv);
#endif
	vdc5fb_deinit_clocks(priv);

	kfree(priv);

	return 0;
}

static const struct of_device_id of_vdc5fb_match[] = {
#ifdef VDC5
	{ .compatible = "renesas,r7s72100-vdc5fb",},
#endif /* VDC5 */
#ifdef VDC6
	{ .compatible = "renesas,r7s9210-vdc6fb",},
#endif /* VDC6 */
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, of_vdc5fb_match);

static struct platform_driver vdc5fb_driver = {
	.driver		= {
		.name		= "vdc5fb",
		.owner		= THIS_MODULE,
		.pm		= &vdc5fb_dev_pm_ops,
		.of_match_table = of_vdc5fb_match,
	},
	.probe		= vdc5fb_probe,
	.remove		= vdc5fb_remove,
};

static int __init vdc5fb_init(void)
{
	return platform_driver_register(&vdc5fb_driver);
}

static void __exit vdc5fb_exit(void)
{
	platform_driver_unregister(&vdc5fb_driver);
}

module_init(vdc5fb_init);
module_exit(vdc5fb_exit);

MODULE_DESCRIPTION("Renesas VDC5 Framebuffer driver");
MODULE_AUTHOR("Chris Brandt <chris.brandt@renesas.com>");
MODULE_LICENSE("GPL v2");

