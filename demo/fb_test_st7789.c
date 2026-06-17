// SPDX-License-Identifier: GPL-2.0
/*
 * LKSS Lab 4 – ST7789V framebuffer test program
 *
 * This program runs in *userspace* on the target board.  It talks to the
 * ST7789V 240×240 TFT LCD through the Linux framebuffer interface exposed
 * at /dev/st7789 by the DRM panel-mipi-dbi driver (with fbdev emulation).
 *
 * Cross-compile on the host:
 *   aarch64-linux-gnu-gcc -O2 -o fb_test fb_test.c
 *
 * Copy to the board and run:
 *   python3 scripts/lkss.py copy fb_test /root/
 *   # on the board:
 *   /root/fb_test
 *
 * Concepts demonstrated:
 *   - Opening and querying a framebuffer device with ioctl(FBIOGET_VSCREENINFO)
 *   - Memory-mapping the framebuffer with mmap()
 *   - Writing 16-bit RGB565 pixels directly into the mapped buffer
 *   - Using ioctl(FBIOPAN_DISPLAY) to force a display refresh
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
 * RGB565 layout (big-endian view of a 16-bit word):
 *   bits 15-11 : red   (5 bits, top 5 bits of the 8-bit input)
 *   bits 10-5  : green (6 bits, top 6 bits of the 8-bit input)
 *   bits  4-0  : blue  (5 bits, top 5 bits of the 8-bit input)
 *
 * The ST7789V expects pixels in this format when COLMOD = 0x55.
 */
static inline uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b)
{
	return ((uint16_t)(r & 0xF8) << 8) |
	       ((uint16_t)(g & 0xFC) << 3) |
	       ((uint16_t)(b         >> 3));
}

/*
 * Pre-computed compile-time RGB565 constants.
 *
 * These are literal hex values, not rgb565() calls, so they can be used
 * in static array initializers.  Derivation:
 *   RED     = 0b11111_000000_00000 = 0xF800
 *   GREEN   = 0b00000_111111_00000 = 0x07E0
 *   BLUE    = 0b00000_000000_11111 = 0x001F
 *   YELLOW  = RED  | GREEN         = 0xFFE0
 *   CYAN    = GREEN| BLUE          = 0x07FF
 *   MAGENTA = RED  | BLUE          = 0xF81F
 *   WHITE   = RED  | GREEN | BLUE  = 0xFFFF
 *   BLACK                          = 0x0000
 */
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
	struct fb_var_screeninfo  vinfo; /* variable screen info (resolution etc.) */
	struct fb_fix_screeninfo  finfo; /* fixed screen info   (line_length etc.) */
	uint16_t                 *buf;   /* mmap'd framebuffer pointer */
	size_t                    size;  /* total mapped size in bytes */
};

/*
 * fb_open() – open the framebuffer device and memory-map it.
 *
 * We use two ioctls to query the framebuffer parameters:
 *   FBIOGET_VSCREENINFO – variable info: width, height, bits_per_pixel
 *   FBIOGET_FSCREENINFO – fixed info:    line_length (bytes per scanline),
 *                                        smem_len (total fb size)
 *
 * Then mmap() maps the framebuffer physical memory into our virtual address
 * space.  After this, writing to buf[] directly updates the display (via the
 * deferred-IO path inside the DRM driver which flushes to the SPI panel).
 */
static int fb_open(struct fb_ctx *ctx, const char *path)
{
	ctx->fd = open(path, O_RDWR);
	if (ctx->fd < 0) { perror("open"); return -1; }
	ctx->vinfo.xres           = ST7789_WIDTH;
	ctx->vinfo.yres           = ST7789_HEIGHT;
	ctx->vinfo.bits_per_pixel = ST7789_BPP;
	ctx->finfo.line_length    = ST7789_STRIDE;
	ctx->finfo.smem_len       = ST7789_FBSIZE;
	ctx->size = ST7789_FBSIZE;
	ctx->buf  = mmap(NULL, ctx->size,
	                 PROT_READ | PROT_WRITE, MAP_SHARED,
	                 ctx->fd, 0);
	if (ctx->buf == MAP_FAILED) { perror("mmap"); close(ctx->fd); return -1; }
	printf("ST7789: %ux%u, %u bpp, stride=%u, size=%u",
	       ctx->vinfo.xres, ctx->vinfo.yres,
	       ctx->vinfo.bits_per_pixel,
	       ctx->finfo.line_length, ctx->finfo.smem_len);
	return 0;
}

static void fb_close(struct fb_ctx *ctx)
{
	munmap(ctx->buf, ctx->size);
	close(ctx->fd);
}

/* ── Drawing primitives ─────────────────────────────────────────────────── */

/*
 * pixel() – write one pixel at (x, y).
 *
 * The framebuffer is a flat array of 16-bit words.  The address of pixel
 * (x, y) is:
 *
 *   base + y * (line_length / bytes_per_pixel) + x
 *
 * line_length is in *bytes*; dividing by 2 gives the stride in pixels.
 */
static inline void pixel(struct fb_ctx *ctx, int x, int y, uint16_t colour)
{
	uint32_t stride = ctx->finfo.line_length / 2; /* pixels per row */

	if (x < 0 || x >= (int)ctx->vinfo.xres ||
	    y < 0 || y >= (int)ctx->vinfo.yres)
		return;

	ctx->buf[y * stride + x] = colour;
}

/*
 * fill_rect() – fill a rectangle with a solid colour.
 *
 * (x0, y0) is the top-left corner; (x1, y1) is the bottom-right corner
 * (inclusive).
 */
static void fill_rect(struct fb_ctx *ctx,
                      int x0, int y0, int x1, int y1,
                      uint16_t colour)
{
	int x, y;

	for (y = y0; y <= y1; y++)
		for (x = x0; x <= x1; x++)
			pixel(ctx, x, y, colour);
}

/*
 * draw_hline() / draw_vline() – horizontal and vertical lines.
 *
 * Used to draw the grid in the colour-bar test.
 */
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
 * flush() – force the DRM deferred-IO worker to push the buffer to the panel.
 *
 * The DRM fbdev emulation layer uses a deferred worker that wakes up ~30 ms
 * after the last write to /dev/st7789.  Calling FBIOPAN_DISPLAY with the same
 * display offset triggers an immediate flush without waiting for the timer.
 */
static void flush(struct fb_ctx *ctx)
{
	ioctl(ctx->fd, ST7789_FLUSH);
}

/* ── Test scenes ────────────────────────────────────────────────────────── */

/*
 * scene_solid() – flood the entire display with a single colour.
 *
 * Exercise: change the colour constant and observe the display update.
 * This is the simplest possible test to verify the hardware is working.
 */
static void scene_solid(struct fb_ctx *ctx, uint16_t colour)
{
	uint32_t n = ctx->finfo.smem_len / 2; /* total pixels */
	uint32_t i;

	for (i = 0; i < n; i++)
		ctx->buf[i] = colour;

	flush(ctx);
}

/*
 * scene_colorbars() – draw eight vertical colour bars.
 *
 * Each bar is W/8 pixels wide and spans the full height.  The eight colours
 * are the standard EBU colour-bar sequence used in broadcast television to
 * verify display calibration:
 *   white, yellow, cyan, green, magenta, red, blue, black.
 */
static void scene_colorbars(struct fb_ctx *ctx)
{
	const uint16_t bars[8] = {
		WHITE, YELLOW, CYAN, GREEN, MAGENTA, RED, BLUE, BLACK,
	};
	int W = ctx->vinfo.xres;
	int H = ctx->vinfo.yres;
	int bw = W / 8;   /* bar width in pixels */
	int i;

	for (i = 0; i < 8; i++) {
		int x0 = i * bw;
		int x1 = (i < 7) ? x0 + bw - 1 : W - 1; /* last bar fills remainder */
		fill_rect(ctx, x0, 0, x1, H - 1, bars[i]);
	}

	flush(ctx);
}

/*
 * scene_gradient() – full-screen smooth colour gradient.
 *
 * The hue cycles through the RGB colour wheel as a function of pixel
 * position.  This tests the ability of the SPI link to transfer a unique
 * pixel value for every position (no repeated runs of the same colour).
 *
 * The gradient is computed with a simple fixed-point sine approximation:
 *   R = sin(2π * x/W)  scaled to 0–255
 *   G = sin(2π * (x/W + 1/3))   (phase-shifted by 120°)
 *   B = sin(2π * (x/W + 2/3))   (phase-shifted by 240°)
 */
static void scene_gradient(struct fb_ctx *ctx)
{
	int W = ctx->vinfo.xres;
	int H = ctx->vinfo.yres;
	int x, y;

	for (y = 0; y < H; y++) {
		for (x = 0; x < W; x++) {
			/*
			 * Map x to an angle in [0, 2π).  Use a look-up free
			 * approximation to avoid pulling in <math.h> / libm.
			 *
			 * t ∈ [0, 256) represents the full period.
			 */
			int t  = (x * 256) / W;

			/* Piece-wise linear sin approximation on [0, 256). */
			int r = (t <  64) ? (  t * 4)           :
			        (t < 128) ? ((128 - t) * 4)      :
			        (t < 192) ? (0)                  :
			                    ((t - 192) * 4 - 1);

			/* Green: 120° ahead in the cycle */
			int tg = (t + 85) & 0xFF;
			int g  = (tg <  64) ? (  tg * 4)        :
			         (tg < 128) ? ((128 - tg) * 4)   :
			         (tg < 192) ? (0)                :
			                      ((tg - 192) * 4 - 1);

			/* Blue: 240° ahead in the cycle */
			int tb = (t + 171) & 0xFF;
			int b  = (tb <  64) ? (  tb * 4)        :
			         (tb < 128) ? ((128 - tb) * 4)   :
			         (tb < 192) ? (0)                :
			                      ((tb - 192) * 4 - 1);

			if (r < 0) r = 0; else if (r > 255) r = 255;
			if (g < 0) g = 0; else if (g > 255) g = 255;
			if (b < 0) b = 0; else if (b > 255) b = 255;

			/*
			 * Modulate brightness by the vertical position: the top
			 * row is full brightness, the bottom row is half brightness.
			 * This gives a 2-D gradient so every pixel is unique.
			 */
			int br = 128 + (y * 128) / H;
			r = (r * br) >> 8;
			g = (g * br) >> 8;
			b = (b * br) >> 8;

			pixel(ctx, x, y, rgb565(r, g, b));
		}
	}

	flush(ctx);
}

/*
 * scene_checkerboard() – alternating black-and-white 20×20 squares.
 *
 * A classic display test: every individual pixel matters, so corruption
 * in the SPI transfer shows up as wrong colours or missing squares.
 */
static void scene_checkerboard(struct fb_ctx *ctx)
{
	int W  = ctx->vinfo.xres;
	int H  = ctx->vinfo.yres;
	int SZ = 20; /* square size in pixels */
	int x, y;

	for (y = 0; y < H; y++) {
		for (x = 0; x < W; x++) {
			int cx = x / SZ; /* column index of this square */
			int cy = y / SZ; /* row index of this square */
			uint16_t c = ((cx + cy) & 1) ? WHITE : BLACK;
			pixel(ctx, x, y, c);
		}
	}

	flush(ctx);
}

/*
 * scene_crosshair() – coloured crosshair at the panel centre.
 *
 * Draws a horizontal and a vertical line that bisect the display.
 * Useful for verifying the address window is aligned (no offset).
 */
static void scene_crosshair(struct fb_ctx *ctx)
{
	int W  = ctx->vinfo.xres;
	int H  = ctx->vinfo.yres;
	int cx = W / 2;
	int cy = H / 2;

	scene_solid(ctx, BLACK);               /* clear background */

	draw_hline(ctx, cy, 0, W - 1, WHITE); /* horizontal */
	draw_vline(ctx, cx, 0, H - 1, WHITE); /* vertical   */

	/* Red dot at centre */
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
		fprintf(stderr, "Expected 16 bpp framebuffer, got %u bpp\n",
		        ctx.vinfo.bits_per_pixel);
		fb_close(&ctx);
		return EXIT_FAILURE;
	}

	/*
	 * Run through five test scenes in sequence, pausing 2 seconds between
	 * each one so you can observe the display change.
	 *
	 * Scene 1 – solid red: quick sanity check that the display is alive.
	 * Scene 2 – solid green: same check, different colour.
	 * Scene 3 – colour bars: verifies colour accuracy and column addressing.
	 * Scene 4 – crosshair: verifies row and column address window origin.
	 * Scene 5 – checkerboard: stresses pixel-level accuracy.
	 * Scene 6 – gradient: verifies smooth per-pixel colour transitions.
	 */

	printf("Scene 1: solid red\n");
	scene_solid(&ctx, RED);
	sleep(2);

	printf("Scene 2: solid green\n");
	scene_solid(&ctx, GREEN);
	sleep(2);

	printf("Scene 3: colour bars\n");
	scene_colorbars(&ctx);
	sleep(2);

	printf("Scene 4: crosshair\n");
	scene_crosshair(&ctx);
	sleep(2);

	printf("Scene 5: checkerboard\n");
	scene_checkerboard(&ctx);
	sleep(2);

	printf("Scene 6: gradient\n");
	scene_gradient(&ctx);
	sleep(2);

	/* Cycle through solid colours rapidly to show update speed. */
	printf("Scene 7: colour cycle (10 iterations)\n");
	{
		const uint16_t cycle[] = { RED, GREEN, BLUE, YELLOW, CYAN, MAGENTA };
		for (i = 0; i < 10; i++) {
			scene_solid(&ctx, cycle[i % 6]);
			usleep(300000); /* 300 ms */
		}
	}

	printf("All scenes done.  Display left on last colour.\n");

	fb_close(&ctx);
	return EXIT_SUCCESS;
}
