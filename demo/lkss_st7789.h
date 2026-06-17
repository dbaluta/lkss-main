/* SPDX-License-Identifier: GPL-2.0 */
/*
 * lkss_st7789.h – userspace API for the LKSS ST7789 miscdevice driver
 *
 * Include this instead of <linux/fb.h> in demos that target /dev/st7789.
 * The display is a fixed 240×240 RGB565 panel; all geometry is a
 * compile-time constant rather than being queried at runtime.
 *
 * Usage pattern:
 *
 *   fd  = open("/dev/st7789", O_RDWR);
 *   buf = mmap(NULL, ST7789_FBSIZE, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
 *   // ... write RGB565 pixels into buf ...
 *   ioctl(fd, ST7789_FLUSH);   // push to panel
 */
#ifndef LKSS_ST7789_H
#define LKSS_ST7789_H

#include <stdint.h>
#include <sys/ioctl.h>

/* Fixed display geometry */
#define ST7789_WIDTH   240
#define ST7789_HEIGHT  240
#define ST7789_BPP     16
#define ST7789_STRIDE  (ST7789_WIDTH * 2)               /* bytes per row  */
#define ST7789_FBSIZE  (ST7789_HEIGHT * ST7789_STRIDE)  /* total FB bytes */

/* Single ioctl: push the mmap'd framebuffer to the panel over SPI */
#define ST7789_IOC_MAGIC  'V'
#define ST7789_FLUSH      _IO(ST7789_IOC_MAGIC, 0)

/*
 * Minimal screen-info stubs – only the fields that demo drawing code
 * actually references.  Populated with the compile-time constants above
 * by fb_open() so existing draw helpers (pixel, fill_rect …) keep working.
 */
struct fb_var_screeninfo {
	uint32_t xres;
	uint32_t yres;
	uint32_t bits_per_pixel;
};

struct fb_fix_screeninfo {
	uint32_t line_length;
	uint32_t smem_len;
};

#endif /* LKSS_ST7789_H */
