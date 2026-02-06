/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright(C) 2024 Emcraft Systems
 * Author(s): Vladimir Skvortsov <vskvortsov@emcraft.com>
 */
#include <linux/types.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/gpio.h>
#include <linux/mipi_dsi.h>
#include <video/mipi_display.h>

#include "mipi_dsi.h"

#define RPI_DSI_DRIVER_NAME "rpi-ts-dsi"

/* PPI layer registers */
#define PPI_STARTPPI		0x0104 /* START control bit */
#define PPI_LPTXTIMECNT		0x0114 /* LPTX timing signal */
#define PPI_D0S_ATMR		0x0144
#define PPI_D1S_ATMR		0x0148
#define PPI_D0S_CLRSIPOCOUNT	0x0164 /* Assertion timer for Lane 0 */
#define PPI_D1S_CLRSIPOCOUNT	0x0168 /* Assertion timer for Lane 1 */
#define PPI_START_FUNCTION	1

/* DSI layer registers */
#define DSI_STARTDSI		0x0204 /* START control bit of DSI-TX */
#define DSI_LANEENABLE		0x0210 /* Enables each lane */
#define DSI_RX_START		1

/* LCDC/DPI Host Registers, based on guesswork that this matches TC358764 */
#define LCDCTRL			0x0420 /* Video Path Control */
#define LCDCTRL_MSF		BIT(0) /* Magic square in RGB666 */
#define LCDCTRL_VTGEN		BIT(4)/* Use chip clock for timing */
#define LCDCTRL_UNK6		BIT(6) /* Unknown */
#define LCDCTRL_EVTMODE		BIT(5) /* Event mode */
#define LCDCTRL_RGB888		BIT(8) /* RGB888 mode */
#define LCDCTRL_HSPOL		BIT(17) /* Polarity of HSYNC signal */
#define LCDCTRL_DEPOL		BIT(18) /* Polarity of DE signal */
#define LCDCTRL_VSPOL		BIT(19) /* Polarity of VSYNC signal */
#define LCDCTRL_VSDELAY(v)	(((v) & 0xfff) << 20) /* VSYNC delay */

/* First parameter is in the 16bits, second is in the top 16bits */
#define LCD_HS_HBP		0x0424
#define LCD_HDISP_HFP		0x0428
#define LCD_VS_VBP		0x042c
#define LCD_VDISP_VFP		0x0430

/* SPI Master Registers */
#define SPICMR			0x0450
#define SPITCR			0x0454

/* System Controller Registers */
#define SYSCTRL			0x0464

/* System registers */
#define LPX_PERIOD		3

/* Lane enable PPI and DSI register bits */
#define LANEENABLE_CLEN		BIT(0)
#define LANEENABLE_L0EN		BIT(1)
#define LANEENABLE_L1EN		BIT(2)

struct rpi_touchscreen {
	struct device *dev;
	struct mipi_dsi_info *dsi;
	struct regulator *regulator;
	struct gpio_desc *reset_gpio;
};

static struct rpi_touchscreen *ts_dev = NULL;

static struct fb_videomode lcd_mode[] = {
	/* 800 x 480 */
	{
		"rpi", 60, 800, 480, 25979400 / 1000,
		131, 45,
		7, 22,
		2, 2,
		FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
		FB_VMODE_NONINTERLACED,
		0,
	}
};

static struct mipi_lcd_config lcd_config = {
	.virtual_ch	= 0x0,
	.data_lane_num  = 2,
	.max_phy_clk    = 800,
	.dpi_fmt	= MIPI_RGB888,
};

void mipid_rpi_get_lcd_videomode(struct fb_videomode **mode, int *size,
				     struct mipi_lcd_config **data)
{
	*mode = &lcd_mode[0];
	*size = ARRAY_SIZE(lcd_mode);
	*data = &lcd_config;
}

static int rpi_touchscreen_write(struct rpi_touchscreen *ts, u16 reg, u32 val)
{
	u8 data[6];

	data[0] = reg;
	data[1] = reg >> 8;
	data[2] = val;
	data[3] = val >> 8;
	data[4] = val >> 16;
	data[5] = val >> 24;

	if (ts->dsi->mipi_dsi_pkt_write(ts->dsi, MIPI_DSI_GENERIC_LONG_WRITE,
					(u32 *)data, sizeof(data))) {
		dev_err(&ts->dsi->pdev->dev, "DSI write failure!\n");
	}

	return 0;
}

static int rpi_touchscreen_enable(struct rpi_touchscreen *ts)
{
	u32 lcdctrl;
	int ret;

	pr_err("%s %i\n", __func__, __LINE__);
#if 0
	ret = regulator_enable(ts->regulator);
	if (ret < 0)
		dev_err(ts->dev, "error enabling regulators (%d)\n", ret);
	pr_err("%s %i enabled\n", __func__, __LINE__);

	if (ts->reset_gpio) {
		gpiod_set_value_cansleep(ts->reset_gpio, 1);
		usleep_range(5000, 10000);
	}
#endif
#if 1
	rpi_touchscreen_write(ts, DSI_LANEENABLE,
		       LANEENABLE_L0EN | LANEENABLE_CLEN);
	rpi_touchscreen_write(ts, PPI_D0S_CLRSIPOCOUNT, 5);
	rpi_touchscreen_write(ts, PPI_D1S_CLRSIPOCOUNT, 5);
	rpi_touchscreen_write(ts, PPI_D0S_ATMR, 0);
	rpi_touchscreen_write(ts, PPI_D1S_ATMR, 0);
	rpi_touchscreen_write(ts, PPI_LPTXTIMECNT, LPX_PERIOD);

	rpi_touchscreen_write(ts, SPICMR, 0x00);

	lcdctrl = LCDCTRL_VSDELAY(1) | LCDCTRL_RGB888 |
		  LCDCTRL_UNK6 | LCDCTRL_VTGEN;

	//if (ts->mode.flags & DRM_MODE_FLAG_NHSYNC)
		lcdctrl |= LCDCTRL_HSPOL;

		//if (ts->mode.flags & DRM_MODE_FLAG_NVSYNC)
		lcdctrl |= LCDCTRL_VSPOL;

	rpi_touchscreen_write(ts, LCDCTRL, lcdctrl);

	rpi_touchscreen_write(ts, SYSCTRL, 0x040f);

	rpi_touchscreen_write(ts, LCD_HS_HBP, (2) |
		       ((45) << 16));
	rpi_touchscreen_write(ts, LCD_HDISP_HFP, 800 |
		       ((131) << 16));
	rpi_touchscreen_write(ts, LCD_VS_VBP, (2) |
		       ((22) << 16));
	rpi_touchscreen_write(ts, LCD_VDISP_VFP, 480 |
		       ((7) << 16));
	msleep(100);

	rpi_touchscreen_write(ts, PPI_STARTPPI, PPI_START_FUNCTION);
	rpi_touchscreen_write(ts, DSI_STARTDSI, DSI_RX_START);

	msleep(100);
#endif
	return 0;
}

static int rpi_touchscreen_disable(struct rpi_touchscreen *ts)
{
	int ret;

	if (ts->reset_gpio)
		gpiod_set_value_cansleep(ts->reset_gpio, 0);

	ret = regulator_disable(ts->regulator);
	if (ret < 0)
		dev_err(ts->dev, "error disabling regulators (%d)\n", ret);
	pr_err("%s %i disabled\n", __func__, __LINE__);

	return 0;
}

int mipid_rpi_lcd_setup(struct mipi_dsi_info *mipi_dsi)
{
	int err = 0;

	dev_info(&mipi_dsi->pdev->dev, "MIPI DSI LCD RPI setup.\n");

	ts_dev = devm_kzalloc(&mipi_dsi->pdev->dev, sizeof(*ts_dev), GFP_KERNEL);

	if (!ts_dev) {
		dev_info(&mipi_dsi->pdev->dev, "MIPI DSI LCD RPI again.\n");
		err = -EPROBE_DEFER;
		goto err_out;
	}

	ts_dev->dev = &mipi_dsi->pdev->dev;
	ts_dev->dsi = mipi_dsi;

	rpi_touchscreen_enable(ts_dev);

	dev_info(&mipi_dsi->pdev->dev, "MIPI DSI LCD RPI setup2.\n");

	if (err) {
		goto err_out;
	}

 err_out:
	return err;
}
#if 0
static int mipi_rpi_lcd_probe(struct platform_device *pdev)
{
	dev_info(&pdev->dev, ">>>> RPi 7 touchscreen probe successful\n");
//	ts_dev = devm_kzalloc(&pdev->dev, sizeof(*ts_dev), GFP_KERNEL);
	if (!ts_dev) {
		return -ENOMEM;
	}

	ts_dev->dev = &pdev->dev;

	ts_dev->regulator = devm_regulator_get(ts_dev->dev, "vddc");
	if (IS_ERR(ts_dev->regulator))
		return PTR_ERR(ts_dev->regulator);

	/* Reset GPIO is optional */
	ts_dev->reset_gpio = devm_gpiod_get_optional(ts_dev->dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(ts_dev->reset_gpio))
		return PTR_ERR(ts_dev->reset_gpio);

	dev_info(&pdev->dev, "RPi 7 touchscreen probe successful\n");

	return 0;
}

static void mipi_rpi_lcd_remove(struct platform_device *pdev)
{
}

static const struct of_device_id rpi_lcd_of_ids[] = {
	{ .compatible = "raspberrypi,7inch-lcd" },
	{ } /* sentinel */
};
MODULE_DEVICE_TABLE(of, rpi_lcd_of_ids);

static struct platform_driver mipi_rpi_touchscreen_driver = {
	.driver = {
		.name = "rpi_lcd",
		.of_match_table = rpi_lcd_of_ids,
	},
	.probe = mipi_rpi_lcd_probe,
	.remove = mipi_rpi_lcd_remove,
};
module_platform_driver(mipi_rpi_touchscreen_driver);

static int __init rpi_touchscreen_init(void)
{
	int err;

	err = platform_driver_register(&rpi_touchscreen_driver);
	if (err) {
		pr_err("mipi_dsi_driver register failed\n");
		return err;
	}

	pr_debug("RPi 7inch LCD driver loaded: %s\n", rpi_touchscreen_driver.driver.name);

	return 0;
}
module_init(rpi_touchscreen_init);

static void __exit rpi_touchscreen_exit(void)
{
	platform_driver_unregister(&rpi_touchscreen_driver);
}
module_exit(rpi_touchscreen_exit);

MODULE_AUTHOR("Dmitry Konyshev <probables@emcraft.com>");
MODULE_DESCRIPTION("Raspberry Pi 7-inch LCD driver for framebuffer");
MODULE_LICENSE("GPL v2");
#endif
