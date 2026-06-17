// SPDX-License-Identifier: GPL-2.0
/*
 * LKSS Lab 3 – ST7789 framebuffer test skeleton
 *
 * Skeleton version of fb_test_st7789.c with three TODO blocks:
 *
 *   TODO U1 – open /dev/st7789 and mmap the framebuffer   (in fb_open)
 *   TODO U2 – implement flush via ioctl(ST7789_FLUSH)     (in flush)
 *   TODO U3 – add a test scene that draws something        (in main)
 *
 * See the theory in doc/2026/day3_bis.rst, Part 8 and Exercises 17-18.
 *
 * Cross-compile:
 *   aarch64-linux-gnu-gcc -O2 -o fb_test_st7789_skel demo/fb_test_st7789_skel.c
 *
 * Run on board (driver must be loaded first):
 *   ./fb_test_st7789_skel [/dev/st7789]
 *
 * Compare to the complete solution in demo/fb_test_st7789.c.
 */

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include "lkss_st7789.h"

/* ── RGB565 colour helpers ──────────────────────────────────────────────── */

/*
 * rgb565() – pack 8-bit R, G, B components into a 16-bit RGB565 word.
 *
 * RGB565 bit layout:
 *   bits 15-11 : red   (5 bits, top 5 bits of the 8-bit input)
 *   bits 10-5  : green (6 bits, top 6 bits of the 8-bit input)
 *   bits  4-0  : blue  (5 bits, top 5 bits of the 8-bit input)
 */
static inline uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b)
{
	return ((uint16_t)(r & 0xF8) << 8) |
	       ((uint16_t)(g & 0xFC) << 3) |
	       ((uint16_t)(b         >> 3));
}

#define BLACK   ((uint16_t)0x0000)
#define WHITE   ((uint16_t)0xFFFF)
#define RED     ((uint16_t)0xF800)
#define GREEN   ((uint16_t)0x07E0)
#define BLUE    ((uint16_t)0x001F)
#define YELLOW  ((uint16_t)0xFFE0)
#define CYAN    ((uint16_t)0x07FF)
#define MAGENTA ((uint16_t)0xF81F)

/* ── Framebuffer context ────────────────────────────────────────────────── */

struct fb_ctx {
	int                       fd;
	struct fb_var_screeninfo  vinfo;
	struct fb_fix_screeninfo  finfo;
	uint16_t                 *buf;
	size_t                    size;
};

/*
 * fb_open() – open /dev/st7789 and memory-map the framebuffer.
 *
 * Unlike a standard Linux framebuffer (/dev/fb0), the ST7789 miscdevice does
 * not support FBIOGET_VSCREENINFO/FBIOGET_FSCREENINFO ioctls.  Geometry is
 * fixed at 240×240 RGB565, so we populate the screen-info structs from the
 * compile-time constants in lkss_st7789.h.
 *
 * After mmap(), ctx->buf points to the kernel vmalloc_user buffer shared with
 * the driver.  Pixel writes go directly into that buffer; call flush() to push
 * it to the display over SPI.
 *
 * Returns 0 on success, -1 on error.
 */
static int fb_open(struct fb_ctx *ctx, const char *path)
{
	/*
	 * ==================================================================
	 * TODO U1 – Open the device and mmap the framebuffer
	 *
	 * Step 1: open(path, O_RDWR) – store result in ctx->fd.
	 *         On error: perror("open"); return -1;
	 *
	 * Step 2: Populate screen-info from compile-time constants:
	 *           ctx->vinfo.xres           = ST7789_WIDTH;
	 *           ctx->vinfo.yres           = ST7789_HEIGHT;
	 *           ctx->vinfo.bits_per_pixel = ST7789_BPP;
	 *           ctx->finfo.line_length    = ST7789_STRIDE;
	 *           ctx->finfo.smem_len       = ST7789_FBSIZE;
	 *           ctx->size                 = ST7789_FBSIZE;
	 *
	 * Step 3: mmap() the framebuffer into userspace:
	 *           ctx->buf = mmap(NULL, ctx->size,
	 *                           PROT_READ | PROT_WRITE, MAP_SHARED,
	 *                           ctx->fd, 0);
	 *           if (ctx->buf == MAP_FAILED) {
	 *               perror("mmap"); close(ctx->fd); return -1;
	 *           }
	 *
	 * Step 4: Print geometry and return 0:
	 *           printf("ST7789: %ux%u %u bpp stride=%u size=%u\n", ...);
	 *           return 0;
	 * ==================================================================
	 */
	(void)ctx; (void)path;    /* remove after implementing */
	fprintf(stderr, "fb_open: not implemented (TODO U1)\n");
	return -1;
}

static void fb_close(struct fb_ctx *ctx)
{
	munmap(ctx->buf, ctx->size);
	close(ctx->fd);
}

/* ── Drawing primitives ─────────────────────────────────────────────────── */

static inline void pixel(struct fb_ctx *ctx, int x, int y, uint16_t colour)
{
	uint32_t stride = ctx->finfo.line_length / 2;

	if (x < 0 || x >= (int)ctx->vinfo.xres ||
	    y < 0 || y >= (int)ctx->vinfo.yres)
		return;

	ctx->buf[y * stride + x] = colour;
}

static void fill_rect(struct fb_ctx *ctx,
                      int x0, int y0, int x1, int y1, uint16_t colour)
{
	int x, y;

	for (y = y0; y <= y1; y++)
		for (x = x0; x <= x1; x++)
			pixel(ctx, x, y, colour);
}

static void draw_hline(struct fb_ctx *ctx, int y, int x0, int x1, uint16_t c)
{
	int x;
	for (x = x0; x <= x1; x++)
		pixel(ctx, x, y, c);
}

static void draw_vline(struct fb_ctx *ctx, int x, int y0, int y1, uint16_t c)
{
	int y;
	for (y = y0; y <= y1; y++)
		pixel(ctx, x, y, c);
}

/*
 * flush() – push the mmap'd framebuffer to the ST7789 panel.
 *
 * The kernel ioctl handler acquires the driver mutex, byte-swaps every pixel
 * from little-endian (ARM native) to big-endian (ST7789 SPI protocol), and
 * streams the result to the display.  This call blocks until the SPI transfer
 * completes (~115200 bytes at 40 MHz ≈ 23 ms).
 */
static void flush(struct fb_ctx *ctx)
{
	/*
	 * ==================================================================
	 * TODO U2 – Call ioctl(ST7789_FLUSH) to push the framebuffer
	 *
	 * Single line:   ioctl(ctx->fd, ST7789_FLUSH);
	 *
	 * ST7789_FLUSH is defined in lkss_st7789.h as _IO('V', 0).
	 * No argument is needed — the kernel reads from priv->fbuf which is
	 * the same physical pages as ctx->buf after the mmap.
	 *
	 * Remove the fprintf below once done.
	 * ==================================================================
	 */
	(void)ctx;    /* remove after implementing */
	fprintf(stderr, "flush: not implemented (TODO U2)\n");
}

/* ── Test scenes ────────────────────────────────────────────────────────── */

static void scene_solid(struct fb_ctx *ctx, uint16_t colour)
{
	uint32_t n = ctx->finfo.smem_len / 2;
	uint32_t i;

	for (i = 0; i < n; i++)
		ctx->buf[i] = colour;
	flush(ctx);
}

static void scene_colorbars(struct fb_ctx *ctx)
{
	const uint16_t bars[8] = {
		WHITE, YELLOW, CYAN, GREEN, MAGENTA, RED, BLUE, BLACK,
	};
	int W  = ctx->vinfo.xres;
	int H  = ctx->vinfo.yres;
	int bw = W / 8;
	int i;

	for (i = 0; i < 8; i++) {
		int x0 = i * bw;
		int x1 = (i < 7) ? x0 + bw - 1 : W - 1;
		fill_rect(ctx, x0, 0, x1, H - 1, bars[i]);
	}
	flush(ctx);
}

static void scene_crosshair(struct fb_ctx *ctx)
{
	int W  = ctx->vinfo.xres;
	int H  = ctx->vinfo.yres;
	int cx = W / 2;
	int cy = H / 2;

	scene_solid(ctx, BLACK);
	draw_hline(ctx, cy, 0, W - 1, WHITE);
	draw_vline(ctx, cx, 0, H - 1, WHITE);
	fill_rect(ctx, cx - 3, cy - 3, cx + 3, cy + 3, RED);
	flush(ctx);
}

/* ── Main ───────────────────────────────────────────────────────────────── */

int main(int argc, char *argv[])
{
	const char *fbdev = (argc > 1) ? argv[1] : "/dev/st7789";
	struct fb_ctx ctx;
	int i;

	if (fb_open(&ctx, fbdev) < 0)
		return EXIT_FAILURE;

	if (ctx.vinfo.bits_per_pixel != 16) {
		fprintf(stderr, "Expected 16 bpp, got %u bpp\n",
		        ctx.vinfo.bits_per_pixel);
		fb_close(&ctx);
		return EXIT_FAILURE;
	}

	printf("Scene 1: solid red\n");
	scene_solid(&ctx, RED);
	sleep(2);

	printf("Scene 2: colour bars\n");
	scene_colorbars(&ctx);
	sleep(2);

	printf("Scene 3: crosshair\n");
	scene_crosshair(&ctx);
	sleep(2);

	/*
	 * ==================================================================
	 * TODO U3 – Add your own test scene
	 *
	 * Write a function (or inline code here) that draws something
	 * interesting using pixel() and fill_rect().  Ideas:
	 *
	 *   a) Checkerboard: alternate BLACK/WHITE in 20×20 squares.
	 *      For each pixel (x, y): c = (((x/20)+(y/20)) & 1) ? WHITE : BLACK;
	 *
	 *   b) Colour gradient: map x → hue using rgb565(r, g, b).
	 *      Fill one-pixel-wide vertical strips with fill_rect.
	 *
	 *   c) Diagonal stripes, concentric borders, or any pattern you like.
	 *
	 * After writing pixels, call flush() to push them to the display.
	 *
	 * Checkerboard skeleton:
	 *
	 *   static void scene_checkerboard(struct fb_ctx *ctx) {
	 *       int W = ctx->vinfo.xres, H = ctx->vinfo.yres, SZ = 20;
	 *       for (int y = 0; y < H; y++)
	 *           for (int x = 0; x < W; x++) {
	 *               uint16_t c = (((x/SZ)+(y/SZ)) & 1) ? WHITE : BLACK;
	 *               pixel(ctx, x, y, c);
	 *           }
	 *       flush(ctx);
	 *   }
	 *
	 * Then:
	 *   printf("Scene 4: your scene\n");
	 *   scene_checkerboard(&ctx);
	 *   sleep(2);
	 * ==================================================================
	 */

	/* Scene 4: TODO U3 – add here */

	printf("Done.\n");

	/* Colour cycle to show update speed */
	{
		const uint16_t cycle[] = { RED, GREEN, BLUE, YELLOW, CYAN, MAGENTA };
		for (i = 0; i < 6; i++) {
			scene_solid(&ctx, cycle[i]);
			usleep(300000);
		}
	}

	fb_close(&ctx);
	return EXIT_SUCCESS;
}
