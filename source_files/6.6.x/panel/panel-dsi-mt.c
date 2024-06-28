// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2024 VOLUMIO SRL. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sub license,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Author: Andrew Seredyn <andser@gmail.com>
 *
 * This module is rewritten from panel-ilitek modules template
 */

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regulator/consumer.h>

#include <drm/drm_connector.h>
#include <drm/drm_crtc.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_panel.h>

#include <video/mipi_display.h>

#define RETRY_CMD	3		// Usually if it doesn't recover after the first or second failure, it won't recover at all.
#define RETRY_DELAY	120		// Retry wait time for the drm vc4 host transfer.

static atomic_t errorFlag = ATOMIC_INIT(0); // When broken atomic modeset userspace detected, reset from here.


/*
 * Use this descriptor struct to describe different panels using the
 * Motivo ILITEC based display controller template.
 */
struct panel_desc {
	const struct drm_display_mode *modes;
	unsigned int bpc;

	/**
	 * @width_mm: width of the panel's active display area
	 * @height_mm: height of the panel's active display area
	 */
	struct {
		unsigned int width_mm;
		unsigned int height_mm;
	} size;

	unsigned long mode_flags;
	enum mipi_dsi_pixel_format format;
	const struct panel_init_cmd *init_cmds;
	unsigned int lanes;
};

struct mtdsi {
	struct drm_panel base;
	struct mipi_dsi_device *dsi;

	const struct panel_desc *desc;

	enum drm_panel_orientation orientation;
	struct regulator *power;
	struct gpio_desc *reset;
};

enum dsi_cmd_type {
	INIT_DCS_CMD,
	DELAY_CMD,
};

struct panel_init_cmd {
	enum dsi_cmd_type type;
	size_t len;
	const char *data;
};

#define _INIT_DCS_CMD(...) { \
	.type = INIT_DCS_CMD, \
	.len = sizeof((char[]){__VA_ARGS__}), \
	.data = (char[]){__VA_ARGS__} }

#define _INIT_DELAY_CMD(...) { \
	.type = DELAY_CMD,\
	.len = sizeof((char[]){__VA_ARGS__}), \
	.data = (char[]){__VA_ARGS__} }

/* MTDSI-specific commands, add new commands as you decode them */
#define MTDSI_DCS_SWITCH_PAGE	0xFF

#define _INIT_SWITCH_PAGE_CMD(page) \
	_INIT_DCS_CMD(MTDSI_DCS_SWITCH_PAGE, 0x98, 0x81, (page))

static const struct panel_init_cmd mt1280800a_init_cmd[] = {
	_INIT_DELAY_CMD(5),
/*
*	_INIT_SWITCH_PAGE_CMD(0x03),
*	There seems to be more than one version of panel A
*	(1) 2019 and before;
*	(2) 2020 and after.
*	Type 1 does not have command page 3
*	Type 2 uses defaults and no overwrites
*	no need to match the defaults anyway.
*/

	_INIT_SWITCH_PAGE_CMD(0x04),
	_INIT_DCS_CMD(0x6E, 0x2B),
	//clamp 15V
	_INIT_DCS_CMD(0x6F, 0x33),
	_INIT_DCS_CMD(0x3A, 0xA4),
	_INIT_DCS_CMD(0x8D, 0x18),
	_INIT_DCS_CMD(0x87, 0xBA),
	_INIT_DCS_CMD(0x26, 0x76),
	_INIT_DCS_CMD(0xB2, 0xD1),
	_INIT_DCS_CMD(0xB5, 0x02),
	_INIT_DCS_CMD(0x3A, 0xA4),
	_INIT_DCS_CMD(0x35, 0x17),

	// Bist Mode scope Page4 set with parameters: 0x2F 0x01
	//_INIT_DCS_CMD(0x2F, 0x01),

	_INIT_SWITCH_PAGE_CMD(0x01),
	// Direction rotate selection holds sync in place
	_INIT_DCS_CMD(0x22, 0x30),
	// Direction rotate selection end
	_INIT_DCS_CMD(0x31, 0x00),
	_INIT_DCS_CMD(0x53, 0x7B),
	_INIT_DCS_CMD(0x55, 0x7B),
	_INIT_DCS_CMD(0x50, 0x95),
	_INIT_DCS_CMD(0x51, 0x95),
	_INIT_DCS_CMD(0x60, 0x14),

	_INIT_DCS_CMD(0xA0, 0x00),
	_INIT_DCS_CMD(0xA1, 0x0D),
	_INIT_DCS_CMD(0xA2, 0x25),
	_INIT_DCS_CMD(0xA3, 0x11),
	_INIT_DCS_CMD(0xA4, 0x0C),
	_INIT_DCS_CMD(0xA5, 0x23),
	_INIT_DCS_CMD(0xA6, 0x17),
	_INIT_DCS_CMD(0xA7, 0x1C),
	_INIT_DCS_CMD(0xA8, 0x82),
	_INIT_DCS_CMD(0xA9, 0x21),
	_INIT_DCS_CMD(0xAA, 0x2A),
	_INIT_DCS_CMD(0xAB, 0x6B),
	_INIT_DCS_CMD(0xAC, 0x19),
	_INIT_DCS_CMD(0xAD, 0x14),
	_INIT_DCS_CMD(0xAE, 0x45),
	_INIT_DCS_CMD(0xAF, 0x1D),
	_INIT_DCS_CMD(0xB0, 0x23),
	_INIT_DCS_CMD(0xB1, 0x52),
	_INIT_DCS_CMD(0xB2, 0x63),
	_INIT_DCS_CMD(0xB3, 0x39),

	_INIT_DCS_CMD(0xC0, 0x00),
	_INIT_DCS_CMD(0xC1, 0x0D),
	_INIT_DCS_CMD(0xC2, 0x1D),
	_INIT_DCS_CMD(0xC3, 0x11),
	_INIT_DCS_CMD(0xC4, 0x0C),
	_INIT_DCS_CMD(0xC5, 0x23),
	_INIT_DCS_CMD(0xC6, 0x17),
	_INIT_DCS_CMD(0xC7, 0x1C),
	_INIT_DCS_CMD(0xC8, 0x82),
	_INIT_DCS_CMD(0xC9, 0x21),
	_INIT_DCS_CMD(0xCA, 0x2A),
	_INIT_DCS_CMD(0xCB, 0x6B),
	_INIT_DCS_CMD(0xCC, 0x19),
	_INIT_DCS_CMD(0xCD, 0x14),
	_INIT_DCS_CMD(0xCE, 0x45),
	_INIT_DCS_CMD(0xCF, 0x1D),
	_INIT_DCS_CMD(0xD0, 0x23),
	_INIT_DCS_CMD(0xD1, 0x52),
	_INIT_DCS_CMD(0xD2, 0x63),
	_INIT_DCS_CMD(0xD3, 0x39),

	_INIT_SWITCH_PAGE_CMD(0x00),

	_INIT_DCS_CMD(MIPI_DCS_SOFT_RESET),
	_INIT_DELAY_CMD(5),
	_INIT_DCS_CMD(0x35, 0x00),  // Tearing Effect Line On
	_INIT_DCS_CMD(MIPI_DCS_EXIT_SLEEP_MODE), // Use MIPI_DCS generic commands instead of (0x11, 0x00) - breaks vc4 drm host transfer 
	_INIT_DELAY_CMD(120),
	_INIT_DCS_CMD(MIPI_DCS_SET_DISPLAY_ON), // Use MIPI_DCS generic commands instead of (0x29, 0x00) - breaks vc4 drm host transfer
	_INIT_DELAY_CMD(20),

	//PWM controlled by overlay
	_INIT_DCS_CMD(0x55, 0x03), // Medium Setting of CABC/DBLC

	{},
};

static const struct drm_display_mode mt1280800a_default_mode = {

#define HDA		800		// Horizontal Display pixel length
#define HFPA	52		// 40 - 80		| Horizontal Front Porch
#define HSLA	8		// 4 - 20		| Horizontal SYN Length
#define HBPA	48		// 30 - 90		| Horizontal Back Porch
#define VDA		1280	// Vertical Display pixel length
#define VFPA	16		//  4 - 40		| Vertical Front Porch
#define VSLA	6		//  4 - 20		| Vertical SYN Length
#define VBPA	16		//  4 - 40		| Vertical Back Porch

	.hdisplay		= HDA,
	.hsync_start	= HDA + HFPA,
	.hsync_end		= HDA + HFPA + HSLA,
	.htotal			= HDA + HFPA + HSLA + HBPA,

	.vdisplay		= VDA,
	.vsync_start	= VDA + VFPA,
	.vsync_end		= VDA + VFPA + VSLA,
	.vtotal			= VDA + VFPA + VSLA + VBPA,

	.clock			= (HDA + HFPA + HSLA + HBPA) * (VDA + VFPA + VSLA + VBPA) * 60 / 1000,

	.type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED,
};

static const struct panel_desc mt1280800a_desc = {
	.modes = &mt1280800a_default_mode,
	.bpc = 8,
	.size = {
		.width_mm = 107,
		.height_mm = 172,
	},
	.lanes = 4,
	.format = MIPI_DSI_FMT_RGB888,
	.mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_SYNC_PULSE |
		      MIPI_DSI_MODE_LPM,
	.init_cmds = mt1280800a_init_cmd,
};

static const struct panel_init_cmd mt1280800b_init_cmd[] = {
	_INIT_DELAY_CMD(5),
	_INIT_SWITCH_PAGE_CMD(0x03),
	//GIP_1
	_INIT_DCS_CMD(0x01, 0x00),
	_INIT_DCS_CMD(0x02, 0x00),
	_INIT_DCS_CMD(0x03, 0x53),
	_INIT_DCS_CMD(0x04, 0x53),
	_INIT_DCS_CMD(0x05, 0x13),
	_INIT_DCS_CMD(0x06, 0x04),
	_INIT_DCS_CMD(0x07, 0x02),
	_INIT_DCS_CMD(0x08, 0x02),
	_INIT_DCS_CMD(0x09, 0x00),
	_INIT_DCS_CMD(0x0a, 0x00),
	_INIT_DCS_CMD(0x0b, 0x00),
	_INIT_DCS_CMD(0x0c, 0x00),
	_INIT_DCS_CMD(0x0d, 0x00),
	_INIT_DCS_CMD(0x0e, 0x00),
	_INIT_DCS_CMD(0x0f, 0x00),
	_INIT_DCS_CMD(0x10, 0x00),
	_INIT_DCS_CMD(0x11, 0x00),
	_INIT_DCS_CMD(0x12, 0x00),
	_INIT_DCS_CMD(0x13, 0x00),
	_INIT_DCS_CMD(0x14, 0x00),
	_INIT_DCS_CMD(0x15, 0x00),
	_INIT_DCS_CMD(0x16, 0x00),
	_INIT_DCS_CMD(0x17, 0x00),
	_INIT_DCS_CMD(0x18, 0x00),
	_INIT_DCS_CMD(0x19, 0x00),
	_INIT_DCS_CMD(0x1a, 0x00),
	_INIT_DCS_CMD(0x1b, 0x00),
	_INIT_DCS_CMD(0x1c, 0x00),
	_INIT_DCS_CMD(0x1d, 0x00),
	_INIT_DCS_CMD(0x1e, 0xc0),
	_INIT_DCS_CMD(0x1f, 0x80),
	_INIT_DCS_CMD(0x20, 0x02),
	_INIT_DCS_CMD(0x21, 0x09),
	_INIT_DCS_CMD(0x22, 0x00),
	_INIT_DCS_CMD(0x23, 0x00),
	_INIT_DCS_CMD(0x24, 0x00),
	_INIT_DCS_CMD(0x25, 0x00),
	_INIT_DCS_CMD(0x26, 0x00),
	_INIT_DCS_CMD(0x27, 0x00),
	_INIT_DCS_CMD(0x28, 0x55),
	_INIT_DCS_CMD(0x29, 0x03),
	_INIT_DCS_CMD(0x2a, 0x00),
	_INIT_DCS_CMD(0x2b, 0x00),
	_INIT_DCS_CMD(0x2c, 0x00),
	_INIT_DCS_CMD(0x2d, 0x00),
	_INIT_DCS_CMD(0x2e, 0x00),
	_INIT_DCS_CMD(0x2f, 0x00),
	_INIT_DCS_CMD(0x30, 0x00),
	_INIT_DCS_CMD(0x31, 0x00),
	_INIT_DCS_CMD(0x32, 0x00),
	_INIT_DCS_CMD(0x33, 0x00),
	_INIT_DCS_CMD(0x34, 0x00),
	_INIT_DCS_CMD(0x35, 0x00),
	_INIT_DCS_CMD(0x36, 0x00),
	_INIT_DCS_CMD(0x37, 0x00),
	_INIT_DCS_CMD(0x38, 0x3C),	//VDD1&2 toggle 1sec
	_INIT_DCS_CMD(0x39, 0x00),
	_INIT_DCS_CMD(0x3a, 0x00),
	_INIT_DCS_CMD(0x3b, 0x00),
	_INIT_DCS_CMD(0x3c, 0x00),
	_INIT_DCS_CMD(0x3d, 0x00),
	_INIT_DCS_CMD(0x3e, 0x00),
	_INIT_DCS_CMD(0x3f, 0x00),
	_INIT_DCS_CMD(0x40, 0x00),
	_INIT_DCS_CMD(0x41, 0x00),
	_INIT_DCS_CMD(0x42, 0x00),
	_INIT_DCS_CMD(0x43, 0x00),
	_INIT_DCS_CMD(0x44, 0x00),

	//GIP_2
	_INIT_DCS_CMD(0x50, 0x01),
	_INIT_DCS_CMD(0x51, 0x23),
	_INIT_DCS_CMD(0x52, 0x45),
	_INIT_DCS_CMD(0x53, 0x67),
	_INIT_DCS_CMD(0x54, 0x89),
	_INIT_DCS_CMD(0x55, 0xab),
	_INIT_DCS_CMD(0x56, 0x01),
	_INIT_DCS_CMD(0x57, 0x23),
	_INIT_DCS_CMD(0x58, 0x45),
	_INIT_DCS_CMD(0x59, 0x67),
	_INIT_DCS_CMD(0x5a, 0x89),
	_INIT_DCS_CMD(0x5b, 0xab),
	_INIT_DCS_CMD(0x5c, 0xcd),
	_INIT_DCS_CMD(0x5d, 0xef),

	//GIP_3
	_INIT_DCS_CMD(0x5e, 0x01),
	_INIT_DCS_CMD(0x5f, 0x08),	//FW_GOUT_L1   STV2_ODD
	_INIT_DCS_CMD(0x60, 0x02),	//FW_GOUT_L2
	_INIT_DCS_CMD(0x61, 0x02),	//FW_GOUT_L3
	_INIT_DCS_CMD(0x62, 0x0A),	//FW_GOUT_L4   RESET_ODD
	_INIT_DCS_CMD(0x63, 0x15),	//FW_GOUT_L5
	_INIT_DCS_CMD(0x64, 0x14),	//FW_GOUT_L6
	_INIT_DCS_CMD(0x65, 0x02),	//FW_GOUT_L7
	_INIT_DCS_CMD(0x66, 0x11),	//FW_GOUT_L8   CK11
	_INIT_DCS_CMD(0x67, 0x10),	//FW_GOUT_L9   CK9
	_INIT_DCS_CMD(0x68, 0x02),	//FW_GOUT_L10
	_INIT_DCS_CMD(0x69, 0x0F),	//FW_GOUT_L11   CK7
	_INIT_DCS_CMD(0x6a, 0x0E),	//FW_GOUT_L12   CK5
	_INIT_DCS_CMD(0x6b, 0x02),	//FW_GOUT_L13
	_INIT_DCS_CMD(0x6c, 0x0D),	//FW_GOUT_L14   CK3
	_INIT_DCS_CMD(0x6d, 0x0C),	//FW_GOUT_L15   CK1
	_INIT_DCS_CMD(0x6e, 0x06),	//FW_GOUT_L16   STV1_ODD
	_INIT_DCS_CMD(0x6f, 0x02),	//FW_GOUT_L17
	_INIT_DCS_CMD(0x70, 0x02),	//FW_GOUT_L18
	_INIT_DCS_CMD(0x71, 0x02),	//FW_GOUT_L19
	_INIT_DCS_CMD(0x72, 0x02),	//FW_GOUT_L20
	_INIT_DCS_CMD(0x73, 0x02),	//FW_GOUT_L21
	_INIT_DCS_CMD(0x74, 0x02),	//FW_GOUT_L22

	_INIT_DCS_CMD(0x75, 0x06),	//BW_GOUT_L1   STV2_ODD
	_INIT_DCS_CMD(0x76, 0x02),	//BW_GOUT_L2
	_INIT_DCS_CMD(0x77, 0x02),	//BW_GOUT_L3
	_INIT_DCS_CMD(0x78, 0x0A),	//BW_GOUT_L4   RESET_ODD
	_INIT_DCS_CMD(0x79, 0x15),	//BW_GOUT_L5
	_INIT_DCS_CMD(0x7a, 0x14),	//BW_GOUT_L6
	_INIT_DCS_CMD(0x7b, 0x02),	//BW_GOUT_L7
	_INIT_DCS_CMD(0x7c, 0x10),	//BW_GOUT_L8    CK11
	_INIT_DCS_CMD(0x7d, 0x11),	//BW_GOUT_L9    CK9
	_INIT_DCS_CMD(0x7e, 0x02),	//BW_GOUT_L10
	_INIT_DCS_CMD(0x7f, 0x0C),	//BW_GOUT_L11   CK7
	_INIT_DCS_CMD(0x80, 0x0D),	//BW_GOUT_L12   CK5
	_INIT_DCS_CMD(0x81, 0x02),	//BW_GOUT_L13
	_INIT_DCS_CMD(0x82, 0x0E),	//BW_GOUT_L14   CK3
	_INIT_DCS_CMD(0x83, 0x0F),	//BW_GOUT_L15   CK1
	_INIT_DCS_CMD(0x84, 0x08),	//BW_GOUT_L16   STV1_ODD
	_INIT_DCS_CMD(0x85, 0x02),	//BW_GOUT_L17
	_INIT_DCS_CMD(0x86, 0x02),	//BW_GOUT_L18
	_INIT_DCS_CMD(0x87, 0x02),	//BW_GOUT_L19
	_INIT_DCS_CMD(0x88, 0x02),	//BW_GOUT_L20
	_INIT_DCS_CMD(0x89, 0x02),	//BW_GOUT_L21
	_INIT_DCS_CMD(0x8A, 0x02),	//BW_GOUT_L22

	_INIT_SWITCH_PAGE_CMD(0x04),
	_INIT_DCS_CMD(0x6C, 0x15),
	_INIT_DCS_CMD(0x6E, 0x30),	//VGH clamp 16.08V
	_INIT_DCS_CMD(0x6F, 0x33),	// reg vcl + pumping ratio VGH=3x VGL=-2x
	_INIT_DCS_CMD(0x8D, 0x1F),	//VGL clamp -12.03V
	_INIT_DCS_CMD(0x87, 0xBA),
	_INIT_DCS_CMD(0x26, 0x76),
	_INIT_DCS_CMD(0xB2, 0xD1),
	_INIT_DCS_CMD(0x35, 0x1F),
	_INIT_DCS_CMD(0x33, 0x14),
	_INIT_DCS_CMD(0x3A, 0xA9),
	_INIT_DCS_CMD(0x3B, 0x98),
	_INIT_DCS_CMD(0x38, 0x01),
	_INIT_DCS_CMD(0x39, 0x00),
	// Bist Mode scope Page4 set with parameters: 0x2F 0x01
	//_INIT_DCS_CMD(0x2F, 0x01),

	_INIT_SWITCH_PAGE_CMD(0x01),
	_INIT_DCS_CMD(0x22, 0x0A),
	_INIT_DCS_CMD(0x31, 0x00),	//column inversion
	_INIT_DCS_CMD(0x50, 0xC0),	//VREG1OUT=5V
	_INIT_DCS_CMD(0x51, 0xC0),	//VREG2OUT=-5V
	_INIT_DCS_CMD(0x53, 0x47),	//VCOM1
	_INIT_DCS_CMD(0x55, 0x7A),	//VCOM2
	_INIT_DCS_CMD(0x60, 0x28),	//SDT
	_INIT_DCS_CMD(0x2E, 0xC8),	//1280 GATE NL SEL

	_INIT_DCS_CMD(0xA0, 0x01),	//VP255 Gamma P
	_INIT_DCS_CMD(0xA1, 0x10),	//VP251
	_INIT_DCS_CMD(0xA2, 0x1B),	//VP247
	_INIT_DCS_CMD(0xA3, 0x0C),	//VP243
	_INIT_DCS_CMD(0xA4, 0x14),	//VP239
	_INIT_DCS_CMD(0xA5, 0x25),	//VP231
	_INIT_DCS_CMD(0xA6, 0x1A),	//VP219
	_INIT_DCS_CMD(0xA7, 0x1D),	//VP203
	_INIT_DCS_CMD(0xA8, 0x68),	//VP175
	_INIT_DCS_CMD(0xA9, 0x1B),	//VP144
	_INIT_DCS_CMD(0xAA, 0x26),	//VP111
	_INIT_DCS_CMD(0xAB, 0x5B),	//VP80
	_INIT_DCS_CMD(0xAC, 0x1B),	//VP52
	_INIT_DCS_CMD(0xAD, 0x17),	//VP36
	_INIT_DCS_CMD(0xAE, 0x4F),	//VP24
	_INIT_DCS_CMD(0xAF, 0x24),	//VP16
	_INIT_DCS_CMD(0xB0, 0x2A),	//VP12
	_INIT_DCS_CMD(0xB1, 0x4E),	//VP8
	_INIT_DCS_CMD(0xB2, 0x5F),	//VP4
	_INIT_DCS_CMD(0xB3, 0x39),	//VP0

	_INIT_DCS_CMD(0xC0, 0x0F),	//VN255 GAMMA N
	_INIT_DCS_CMD(0xC1, 0x1B),	//VN251
	_INIT_DCS_CMD(0xC2, 0x27),	//VN247
	_INIT_DCS_CMD(0xC3, 0x16),	//VN243
	_INIT_DCS_CMD(0xC4, 0x14),	//VN239
	_INIT_DCS_CMD(0xC5, 0x28),	//VN231
	_INIT_DCS_CMD(0xC6, 0x1D),	//VN219
	_INIT_DCS_CMD(0xC7, 0x21),	//VN203
	_INIT_DCS_CMD(0xC8, 0x6C),	//VN175
	_INIT_DCS_CMD(0xC9, 0x1B),	//VN144
	_INIT_DCS_CMD(0xCA, 0x26),	//VN111
	_INIT_DCS_CMD(0xCB, 0x5B),	//VN80
	_INIT_DCS_CMD(0xCC, 0x1B),	//VN52
	_INIT_DCS_CMD(0xCD, 0x1B),	//VN36
	_INIT_DCS_CMD(0xCE, 0x4F),	//VN24
	_INIT_DCS_CMD(0xCF, 0x24),	//VN16
	_INIT_DCS_CMD(0xD0, 0x2A),	//VN12
	_INIT_DCS_CMD(0xD1, 0x4E),	//VN8
	_INIT_DCS_CMD(0xD2, 0x5F),	//VN4
	_INIT_DCS_CMD(0xD3, 0x39),	//VN0

	_INIT_SWITCH_PAGE_CMD(0x00),

	_INIT_DCS_CMD(MIPI_DCS_SOFT_RESET),
	_INIT_DELAY_CMD(10),
	_INIT_DCS_CMD(0x35, 0x00),  // Tearing Effect Line On
	_INIT_DCS_CMD(MIPI_DCS_EXIT_SLEEP_MODE), // Use MIPI_DCS generic commands instead of (0x11, 0x00) - breaks vc4 drm host transfer 
	_INIT_DELAY_CMD(120),
	_INIT_DCS_CMD(MIPI_DCS_SET_DISPLAY_ON), // Use MIPI_DCS generic commands instead of (0x29, 0x00) - breaks vc4 drm host transfer
	_INIT_DELAY_CMD(20),

	//PWM controlled by overlay
	_INIT_DCS_CMD(0x55, 0x01), // Conservative Setting of CABC/DBLC

	{},
};

static const struct drm_display_mode mt1280800b_default_mode = {

#define HDB		800		// Horizontal Display pixel length
#define HFPB	40		// 40 - 80		| Horizontal Front Porch
#define HSLB	40		// 4 - 20		| Horizontal SYN Length
#define HBPB	20		// 30 - 90		| Horizontal Back Porch
#define VDB		1280	// Vertical Display pixel length
#define VFPB	8		//  4 - 40		| Vertical Front Porch
#define VSLB	8		//  4 - 20		| Vertical SYN Length
#define VBPB	4		//  4 - 40		| Vertical Back Porch

	.hdisplay		= HDB,
	.hsync_start	= HDB + HFPB,
	.hsync_end		= HDB + HFPB + HSLB,
	.htotal			= HDB + HFPB + HSLB + HBPB,

	.vdisplay		= VDB,
	.vsync_start	= VDB + VFPB,
	.vsync_end		= VDB + VFPB + VSLB,
	.vtotal			= VDB + VFPB + VSLB + VBPB,

	.clock			= (HDB + HFPB + HSLB + HBPB) * (VDB + VFPB + VSLB + VBPB) * 60 / 1000,

	.type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED,
};

static const struct panel_desc mt1280800b_desc = {
	.modes = &mt1280800b_default_mode,
	.bpc = 8,
	.size = {
		.width_mm = 107,
		.height_mm = 172,
	},
	.lanes = 4,
	.format = MIPI_DSI_FMT_RGB888,
	.mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_SYNC_PULSE |
		      MIPI_DSI_MODE_VIDEO_BURST | MIPI_DSI_MODE_LPM,
	.init_cmds = mt1280800b_init_cmd,
};

static inline struct mtdsi *to_mtdsi(struct drm_panel *panel)
{
	return container_of(panel, struct mtdsi, base);
}

static int mtdsi_init_dcs_cmd(struct mtdsi *ctx)
{
	struct mipi_dsi_device *dsi = ctx->dsi;
	struct drm_panel *panel = &ctx->base;
	int i, err = 0, retry = 0;

	if (ctx->desc->init_cmds) {
		const struct panel_init_cmd *init_cmds = ctx->desc->init_cmds;

		for (i = 0; init_cmds[i].len != 0; i++) {
			const struct panel_init_cmd *cmd = &init_cmds[i];
		do {
			switch (cmd->type) {
			case DELAY_CMD:
				msleep(cmd->data[0]);
				err = 0;
				break;

			case INIT_DCS_CMD:
				err = mipi_dsi_dcs_write(dsi, cmd->data[0],
							 cmd->len <= 1 ? NULL :
							 &cmd->data[1],
							 cmd->len - 1);
				break;

			default:
				err = -EINVAL;
			}

			if (err) msleep(RETRY_DELAY);
			++retry;
			}
			while (err && retry < RETRY_CMD);

			if (err < 0) {
				dev_err(panel->dev,
					"DSI: Failed to write init commands %u\n", i);
				return err;
			}
		}
	}
	return 0;
}

static int mtdsi_switch_page(struct mipi_dsi_device *dsi, u8 page)
{
	int retry,ret;
	const struct panel_init_cmd cmd = _INIT_SWITCH_PAGE_CMD(page);

    retry = 0;
    do {
	ret = mipi_dsi_dcs_write(dsi, cmd.data[0],
				 cmd.len <= 1 ? NULL :
				 &cmd.data[1],
				 cmd.len - 1);
		if (ret) msleep(RETRY_DELAY);
		++retry;
    }
    while (ret && retry < RETRY_CMD);
	if (ret) {
		dev_err(&dsi->dev,
			"DSI: Error switching panel controller page (%d)\n", ret);
		return ret;
	}

	return 0;
}

static int mtdsi_enter_sleep_mode(struct mtdsi *ctx)
{
	struct mipi_dsi_device *dsi = ctx->dsi;
	int retry,ret;

	dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	// MIPI needs to return to the LP11 state before enabling all blocks inside the display

    retry = 0;
    do {
		ret = mipi_dsi_dcs_nop(ctx->dsi);
		if (ret) msleep(RETRY_DELAY);
		++retry;
    }
    while (ret && retry < RETRY_CMD);
	if (ret < 0) {
		dev_err(&dsi->dev, "DSI: Failed to return to the LP11 state prior sleep mode enter: %d\n", ret);
//		return 0; //Do not error here yet. This is DSI transfer warmup.
	}

	usleep_range(1000, 20000);

    retry = 0;
    do {
		ret = mipi_dsi_dcs_set_display_off(dsi);
		if (ret) msleep(RETRY_DELAY);
		++retry;
    }
    while (ret && retry < RETRY_CMD);
	if (ret < 0) {
		dev_err(&dsi->dev, "DSI: Failed to set display off: %d\n", ret);
		return ret;
	}

	msleep(5);

    retry = 0;
    do {
		ret = mipi_dsi_dcs_enter_sleep_mode(dsi);
		if (ret) msleep(RETRY_DELAY);
		++retry;
    }
    while (ret && retry < RETRY_CMD);
	if (ret < 0) {
		dev_err(&dsi->dev, "DSI: Failed to enter sleep mode: %d\n", ret);
		return ret;
	}

	return 0;
}

static int mtdsi_disable(struct drm_panel *panel)
{
	struct mtdsi *ctx = to_mtdsi(panel);
	int ret;

	ret = mtdsi_enter_sleep_mode(ctx);
	if (ret < 0) {
		dev_err(panel->dev, "DSI: Failed to set panel off: %d\n", ret);
        atomic_set(&errorFlag, 1);
		return ret;
	}

	atomic_set(&errorFlag, 0);

	msleep(150);

	return 0;
}

static int mtdsi_unprepare(struct drm_panel *panel)
{
	struct mtdsi *ctx = to_mtdsi(panel);

	gpiod_set_value_cansleep(ctx->reset, 1);
	usleep_range(1000, 2000);
	regulator_disable(ctx->power);

	return 0;
}

static int mtdsi_prepare(struct drm_panel *panel)
{
	struct mtdsi *ctx = to_mtdsi(panel);
	int retry,ret;

    atomic_set(&errorFlag, 0); // Clear the error flag

	ret = regulator_enable(ctx->power);

	usleep_range(1000, 2000);

	// MIPI needs to keep the LP11 state before the lcm_reset pin is pulled high
    retry = 0;
    do {
		ret = mipi_dsi_dcs_nop(ctx->dsi);
		if (ret) msleep(RETRY_DELAY);
		++retry;
    }
    while (ret && retry < RETRY_CMD);
	if (ret < 0) {
		dev_err(panel->dev, "DSI: Failed to return to the LP11 state prior prepare: %d\n", ret);
//		return 0; //Do not error here yet. This is DSI transfer warmup.
	}
	usleep_range(1000, 2000);

	gpiod_set_value_cansleep(ctx->reset, 0);
	msleep(50);
	gpiod_set_value_cansleep(ctx->reset, 1);
	usleep_range(6000, 10000);

	ret = mtdsi_init_dcs_cmd(ctx);
	if (ret < 0) {
		dev_err(panel->dev, "DSI: Failed to initialize panel: %d\n", ret);
		goto poweroff;
	}

	return 0;

poweroff:
	regulator_disable(ctx->power);
	usleep_range(1000, 2000);
	gpiod_set_value_cansleep(ctx->reset, 0);

	return ret;
}

static int mtdsi_exit_sleep_mode(struct mtdsi *ctx)
{
	struct mipi_dsi_device *dsi = ctx->dsi;
	int retry,ret;

	dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	// MIPI needs to return to the LP11 state before enabling all blocks inside the display
    retry = 0;
    do {
		ret = mipi_dsi_dcs_nop(ctx->dsi);
		if (ret) msleep(RETRY_DELAY);
		++retry;
    }
    while (ret && retry < RETRY_CMD);
	if (ret < 0) {
		dev_err(&dsi->dev, "DSI: Failed to return to the LP11 state prior sleep mode exit: %d\n", ret);
//		return 0; //Do not error here yet. This is DSI transfer warmup.
	}
	usleep_range(1000, 20000);

    retry = 0;
    do {
		ret = mipi_dsi_dcs_exit_sleep_mode(dsi);
		if (ret) msleep(RETRY_DELAY);
		++retry;
    }
    while (ret && retry < RETRY_CMD);
	if (ret < 0) {
		dev_err(&dsi->dev, "DSI: Failed to exit sleep mode: %d\n", ret);
		return ret;
	}

	msleep(10);

    retry = 0;
    do {
		ret = mipi_dsi_dcs_set_display_on(dsi);
		if (ret) msleep(RETRY_DELAY);
		++retry;
    }
    while (ret && retry < RETRY_CMD);
	if (ret < 0) {
		dev_err(&dsi->dev, "DSI: Failed to set display on: %d\n", ret);
		return ret;
	}

	return 0;
}

static int mtdsi_enable(struct drm_panel *panel)
{
	struct mtdsi *ctx = to_mtdsi(panel);
	struct mipi_dsi_device *dsi = ctx->dsi;
	int ret;

	mtdsi_switch_page(dsi, 0x00);

	msleep(5);

	ret = mtdsi_exit_sleep_mode(ctx);
	if (ret < 0) {
		dev_err(panel->dev, "DSI: Failed to activate panel: %d\n", ret);
        atomic_set(&errorFlag, 1);
		return ret;
	}

	atomic_set(&errorFlag, 0);

	msleep(120);

	return 0;
}

static int mtdsi_get_modes(struct drm_panel *panel,
			      struct drm_connector *connector)
{
	struct mtdsi *ctx = to_mtdsi(panel);
	const struct drm_display_mode *m = ctx->desc->modes;
	struct drm_display_mode *mode;

	mode = drm_mode_duplicate(connector->dev, m);
	if (!mode) {
		dev_err(panel->dev, "DSI: Failed to add mode %ux%u@%u\n",
			m->hdisplay, m->vdisplay, drm_mode_vrefresh(m));
		return -ENOMEM;
	}

	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	drm_mode_set_name(mode);
	drm_mode_probed_add(connector, mode);

	connector->display_info.width_mm = ctx->desc->size.width_mm;
	connector->display_info.height_mm = ctx->desc->size.height_mm;
	connector->display_info.bpc = ctx->desc->bpc;

	/*
	 * TODO: Remove once all drm drivers call
	 * drm_connector_set_orientation_from_panel()
	 */

	drm_connector_set_panel_orientation(connector, DRM_MODE_PANEL_ORIENTATION_LEFT_UP);

	return 1;
}

static enum drm_panel_orientation mtdsi_get_orientation(struct drm_panel *panel)
{
	struct mtdsi *ctx = to_mtdsi(panel);

	return ctx->orientation;
}

static const struct drm_panel_funcs mtdsi_funcs = {
	.disable = mtdsi_disable,
	.unprepare = mtdsi_unprepare,
	.prepare = mtdsi_prepare,
	.enable = mtdsi_enable,
	.get_modes = mtdsi_get_modes,
	.get_orientation = mtdsi_get_orientation,
};

static int mtdsi_add(struct mtdsi *ctx)
{
	struct device *dev = &ctx->dsi->dev;
	int err;

	ctx->power = devm_regulator_get(dev, "power");
	if (IS_ERR(ctx->power))
		return PTR_ERR(ctx->power);

	ctx->reset = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(ctx->reset)) {
		dev_err(dev, "DSI: Cannot get reset-gpios %ld\n",
			PTR_ERR(ctx->reset));
		return PTR_ERR(ctx->reset);
	}

	gpiod_set_value_cansleep(ctx->reset, 0);

	drm_panel_init(&ctx->base, dev, &mtdsi_funcs,
		       DRM_MODE_CONNECTOR_DSI);
	err = of_drm_get_panel_orientation(dev->of_node, &ctx->orientation);
	if (err < 0) {
		dev_err(dev, "DSI: %pOF: Failed to get orientation %d\n", dev->of_node, err);
		return err;
	}

	err = drm_panel_of_backlight(&ctx->base);
	if (err)
		return err;

	ctx->base.funcs = &mtdsi_funcs;
	ctx->base.dev = &ctx->dsi->dev;

	ctx->base.prepare_prev_first = true;

	drm_panel_add(&ctx->base);

	return 0;
}

static int mtdsi_probe(struct mipi_dsi_device *dsi)
{
	struct mtdsi *ctx;
	int ret;
	const struct panel_desc *desc;

	ctx = devm_kzalloc(&dsi->dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	desc = of_device_get_match_data(&dsi->dev);
	dsi->lanes = desc->lanes;
	dsi->format = desc->format;
	dsi->mode_flags = desc->mode_flags;
	ctx->desc = desc;
	ctx->dsi = dsi;
	ret = mtdsi_add(ctx);
	if (ret < 0)
		return ret;

	mipi_dsi_set_drvdata(dsi, ctx);

	ret = mipi_dsi_attach(dsi);
	if (ret)
		drm_panel_remove(&ctx->base);

	return ret;
}

static void mtdsi_remove(struct mipi_dsi_device *dsi)
{
	struct mtdsi *ctx = mipi_dsi_get_drvdata(dsi);
	int ret;

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		dev_err(&dsi->dev, "DSI: Failed to detach from DSI host: %d\n", ret);

	if (ctx->base.dev)
		drm_panel_remove(&ctx->base);
}

static const struct of_device_id mtdsi_of_match[] = {
	{ .compatible = "motivo,mt1280800a", .data = &mt1280800a_desc },
	{ .compatible = "motivo,mt1280800b", .data = &mt1280800b_desc },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, mtdsi_of_match);

static struct mipi_dsi_driver mtdsi_driver = {
	.driver = {
		.name = "panel-dsi-mt",
		.of_match_table = mtdsi_of_match,
	},
	.probe = mtdsi_probe,
	.remove = mtdsi_remove,
};
module_mipi_dsi_driver(mtdsi_driver);

MODULE_AUTHOR("Andrew Seredyn <andser@gmail.com>");
MODULE_DESCRIPTION("DRM Driver for MOTIVO MIPI DSI panels.");
MODULE_LICENSE("GPL v2");
