/*
 * Copyright (C) 2013-2014 Renesas Solutions Corp.
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

#ifndef _VDC5FB_H_
#define _VDC5FB_H_

#ifdef __KERNEL__

#include <linux/fb.h>

#ifdef CONFIG_ARCH_R7S9210
#define VDC6
#else
#define VDC5
#endif

/* START AND TOTAL NUMBER OF IRQS */
#define	VDC5FB_IRQ_BASE(x)	(75 + (24 * (x)))
#define	VDC5FB_IRQ_SIZE		23

/* tcon_sel */
enum {				/* index */
	LCD_TCON0 = 0,
	LCD_TCON1,
	LCD_TCON2,
	LCD_TCON3,
	LCD_TCON4,
	LCD_TCON5,
	LCD_TCON6,
	LCD_MAX_TCON,
};
#define	TCON_SEL_STVA		0	/* STVA/VS (VSYNC) */
#define	TCON_SEL_STVB		1	/* STVB/VE (Vertical Enable)*/
#define	TCON_SEL_STH		2	/* STH/SP/HS (HSYNC) */
#define	TCON_SEL_STB		3	/* STB/LP/HE (Horizontal Enable) */
#define	TCON_SEL_CPV		4	/* CPV/GCK (Gate clock signal) */
#define	TCON_SEL_POLA		5	/* POLA (VCOM voltage polarity control signal) */
#define	TCON_SEL_POLB		6	/* POLB (VCOM voltage polarity control signal) */
#define	TCON_SEL_DE		7	/* DE (Data Enable) */
#define	TCON_SEL_UNUSED		0xff	/* pin not used */

/* out_format */
#define	OUT_FORMAT_RGB888	0
#define	OUT_FORMAT_RGB666	1
#define	OUT_FORMAT_RGB565	2

/* Frame buffer pixel format (see register GRn_FLM6) for GR0-GR3 */
#define	GR_RDSWA(x)		(((x) & 0x7u) << 10)
#define GR_YCC_SWAP(x) 		(((x) & 0xfu) << 13)
#define	GR_FORMAT(x)		(((x) & 0xfu) << 28)
enum {
	GR_FORMAT_RGB565,
	GR_FORMAT_RGB888,
	GR_FORMAT_ARGB1555,
	GR_FORMAT_ARGB4444,
	GR_FORMAT_ARGB8888,
	GR_FORMAT_CLUT8,
	GR_FORMAT_CLUT4,
	GR_FORMAT_CLUT1,
	GR_FORMAT_YCbCr422,
	GR_FORMAT_YCbCr444,
	GR_FORMAT_RGBA5551,
};

struct vdc5fb_layer {
	u32 xres;
	u32 yres;
	u32 x_offset;
	u32 y_offset;
	u32 base;
	u32 bpp;
	u32 format;	/* Will be OR-ed with GR_FLM6 */
	u32 blend;	/* blend with lower layer or not */
};

/* Choices for ".panel_icksel".
  When .use_lvds=0:
    This driver only supports ICKSEL_P1CLK because the math to
    calculate PANEL_DCDR[5:0] assumes a P1 clock rate.
  When .use_lvds=1
    This value is not used (PANEL_ICKSEL[1:0] is don't care when using LVDS)
  See "Panel Clock Control Register (SYSCNT_PANEL_CLK)" for more details */
enum {
	ICKSEL_INPSEL = 0,	/* Video image clock (VIDEO_X1 or DV_CLK based on INP_SEL=0,1) */
	ICKSEL_EXTCLK0 = 1,	/* External clock (LCD0_EXTCLK) */
#ifdef VDC5
	ICKSEL_EXTCLK1 = 2,	/* External clock (LCD1_EXTCLK) */
#endif
	ICKSEL_P1CLK = 3,	/* Peripheral clock 1 */
};

/* Choices for ".panel_ocksel".
  When .use_lvds=0, only OCKSEL_ICK should be selected
  When .use_lvds=1, OCKSEL_PLL or OCKSEL_PLL_DIV7 should be selected
  See "Panel Clock Control Register (SYSCNT_PANEL_CLK)" for more details */
enum {
	OCKSEL_ICK = 0,		/* Selected by PANEL_CLK[1:0] */
	OCKSEL_PLL = 1,		/* LVDS PLL clock */
	OCKSEL_PLL_DIV7 = 2,	/* LVDS PLL clock divided by 7 */
};

/*! The clock input to frequency divider 1 */
enum {
#ifdef VDC5
	VDC5_LVDS_INCLK_SEL_IMG = 0,	/*!< Video image clock (VIDEO_X1) */
	VDC5_LVDS_INCLK_SEL_DV_0,	/*!< Video image clock (DV_CLK 0) */
	VDC5_LVDS_INCLK_SEL_DV_1,	/*!< Video image clock (DV_CLK 1) */
	VDC5_LVDS_INCLK_SEL_EXT_0,	/*!< External clock (LCD_EXTCLK 0) */
	VDC5_LVDS_INCLK_SEL_EXT_1,	/*!< External clock (LCD_EXTCLK 1) */
#endif /* VDC5 */
#ifdef VDC6
	VDC5_LVDS_INCLK_SEL_DV_0 = 0,	/*!< Video image clock (DV_CLK 0) */
	VDC5_LVDS_INCLK_SEL_EXT_0,	/*!< External clock (LCD_EXTCLK 0) */
#endif /* VDC6 */
	VDC5_LVDS_INCLK_SEL_PERI,	/*!< Peripheral clock 1 */
	VDC5_LVDS_INCLK_SEL_NUM
};

/*! The frequency divider 1 (NIDIV) and frequency divider 2 (NODIV) */
enum {
	VDC5_LVDS_NDIV_1 = 0,		/*!< Div 1 */
	VDC5_LVDS_NDIV_2,		/*!< Div 2 */
	VDC5_LVDS_NDIV_4,		/*!< Div 4 */
	VDC5_LVDS_NDIV_NUM
};

/*! The frequency dividing value (NOD) for the output frequency */
enum {
	VDC5_LVDS_PLL_NOD_1 = 0,	/*!< Div 1 */
	VDC5_LVDS_PLL_NOD_2,		/*!< Div 2 */
	VDC5_LVDS_PLL_NOD_4,		/*!< Div 4 */
	VDC5_LVDS_PLL_NOD_8,		/*!< Div 8 */
	VDC5_LVDS_PLL_NOD_NUM
};

/* board-specific data */
struct vdc5fb_pdata {
	const char *name;
	struct fb_videomode *videomode;
	int channel;
	int panel_icksel;	/* should be ICKSEL_XXX */
	int panel_ocksel;	/* should be OCKSEL_XXX */
	unsigned long panel_width;
	unsigned long panel_height;
	unsigned long panel_pixel_xres;
	unsigned long panel_pixel_yres;
	unsigned long fb_phys_addr;
	unsigned long fb_phys_size;
	unsigned long flm_max;
	int out_format;
	int data_clk_phase;
	int use_lvds;
	int flags;
#define	FLAGS_RESIZE	0x01	/* enable resize */
#define	FLAGS_RETIMING	0x02	/* enable resize */
#define	FLAGS_DS	0x04	/* enable down-scale */
	unsigned char tcon_sel[LCD_MAX_TCON];
	struct vdc5fb_layer layers[4];
	int double_buffer;	/* double space to allow for double buffering */
};
#endif /* __KERNEL__ */

/*****************/
/* VDC5FB IOCTLs */
/*****************/

#define	FBIOGET_CONTRAST	_IOR('F', 0x21, int)
#define	FBIOPUT_CONTRAST	_IOW('F', 0x22, int)
#define	FBIOGET_BRIGHT		_IOR('F', 0x23, int)
#define	FBIOPUT_BRIGHT		_IOW('F', 0x24, int)
#define	FBIOGET_FRAME		_IOW('F', 0x25, int)
#define	FBIOPUT_FRAME		_IOW('F', 0x26, int)

struct fbio_bright {
	unsigned short pbrt_r;	/* 0-1023, inclusive */
	unsigned short pbrt_g;
	unsigned short pbrt_b;
};

struct fbio_contrast {
	unsigned short cont_r;	/* 0-255, inclusive */
	unsigned short cont_g;
	unsigned short cont_b;
};

struct fbio_frame {
	unsigned short fr_num;	/* 0-(frame_max-1), inclusive */
	unsigned short fr_max;
};

#endif /* _VDC5FB_H_ */
