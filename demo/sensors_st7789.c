// SPDX-License-Identifier: GPL-2.0
/*
 * sensors.c – live BME280 readout on ST7789 240×240 framebuffer
 *
 * Displays:
 *   – Current time   HH:MM:SS   (top, large)
 *   – Current date   DD:MM:YYYY (top, small)
 *   – Temperature (°C), read from in_temp_input    (milli-°C)
 *   – Pressure    (kPa), read from in_pressure_input (kPa)
 *
 * IIO sysfs paths (BME280 on LPI2C4, iio:device0):
 *   /sys/bus/iio/devices/iio:device0/in_temp_input
 *   /sys/bus/iio/devices/iio:device0/in_pressure_input
 *
 * Cross-compile: aarch64-linux-gnu-gcc -O2 -o sensors sensors.c
 * Run on board:  ./sensors [/dev/st7789]
 */

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include "lkss_st7789.h"

#define TEMP_PATH  "/sys/bus/iio/devices/iio:device0/in_temp_input"
#define PRESS_PATH "/sys/bus/iio/devices/iio:device0/in_pressure_input"

/* ── framebuffer helpers ─────────────────────────────────────────────────── */

static inline uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b)
{
	return ((uint16_t)(r & 0xF8) << 8) |
	       ((uint16_t)(g & 0xFC) << 3) |
	       (b >> 3);
}

struct fb_ctx {
	int                      fd;
	struct fb_var_screeninfo vinfo;
	struct fb_fix_screeninfo finfo;
	uint16_t                *buf;
	size_t                   size;
};

static int fb_open(struct fb_ctx *ctx, const char *dev)
{
	ctx->fd = open(dev, O_RDWR);
	if (ctx->fd < 0) { perror("open"); return -1; }
	ctx->vinfo.xres           = ST7789_WIDTH;
	ctx->vinfo.yres           = ST7789_HEIGHT;
	ctx->vinfo.bits_per_pixel = ST7789_BPP;
	ctx->finfo.line_length    = ST7789_STRIDE;
	ctx->finfo.smem_len       = ST7789_FBSIZE;
	ctx->size = ST7789_FBSIZE;
	ctx->buf  = mmap(NULL, ctx->size, PROT_READ|PROT_WRITE, MAP_SHARED,
	                 ctx->fd, 0);
	if (ctx->buf == MAP_FAILED) { perror("mmap"); close(ctx->fd); return -1; }
	return 0;
}

static void fb_close(struct fb_ctx *ctx)
{
	munmap(ctx->buf, ctx->size);
	close(ctx->fd);
}

static inline void pixel(struct fb_ctx *ctx, int x, int y, uint16_t c)
{
	unsigned stride = ctx->finfo.line_length / 2;
	if ((unsigned)x < ctx->vinfo.xres && (unsigned)y < ctx->vinfo.yres)
		ctx->buf[y * stride + x] = c;
}

static void fill_rect(struct fb_ctx *ctx,
                      int x0, int y0, int x1, int y1, uint16_t c)
{
	int x, y;
	for (y = y0; y <= y1; y++)
		for (x = x0; x <= x1; x++)
			pixel(ctx, x, y, c);
}

static void flush(struct fb_ctx *ctx)
{
	ioctl(ctx->fd, ST7789_FLUSH);
}

/* ── 3×5 pixel font: digits, letters, ':', ' ', '.' ─────────────────────── */

static const uint8_t FONT[39][5] = {
	/* 0–9 */
	{7,5,5,5,7}, {2,6,2,2,7}, {7,1,7,4,7}, {7,1,3,1,7}, {5,5,7,1,1},
	{7,4,7,1,7}, {7,4,7,5,7}, {7,1,1,1,1}, {7,5,7,5,7}, {7,5,7,1,7},
	/* A–Z */
	{2,5,7,5,5}, /* A */  {6,5,6,5,6}, /* B */  {6,4,4,4,6}, /* C */
	{6,5,5,5,6}, /* D */  {7,4,6,4,7}, /* E */  {7,4,6,4,4}, /* F */
	{3,4,5,5,3}, /* G */  {5,5,7,5,5}, /* H */  {7,2,2,2,7}, /* I */
	{7,1,1,5,2}, /* J */  {5,6,4,6,5}, /* K */  {4,4,4,4,7}, /* L */
	{5,7,5,5,5}, /* M */  {7,5,5,5,5}, /* N */  {7,5,5,5,7}, /* O */
	{6,5,6,4,4}, /* P */  {7,5,5,1,1}, /* Q */  {6,5,6,4,1}, /* R */
	{3,4,7,1,6}, /* S */  {7,2,2,2,2}, /* T */  {5,5,5,5,7}, /* U */
	{5,5,5,2,2}, /* V */  {5,7,5,5,5}, /* W */  {5,5,2,5,5}, /* X */
	{5,5,2,2,2}, /* Y */  {7,1,2,4,7}, /* Z */
	/* special */
	{0,2,0,2,0}, /* ':' (36) */
	{0,0,0,0,0}, /* ' ' (37) */
	{0,0,0,0,2}, /* '.' (38) */
};

static int char_to_glyph(char c)
{
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'A' && c <= 'Z') return 10 + (c - 'A');
	if (c == ':') return 36;
	if (c == '.') return 38;
	return 37; /* space */
}

static void draw_glyph(struct fb_ctx *ctx,
                       int x, int y, int g, int s, uint16_t c)
{
	int r, col;
	for (r = 0; r < 5; r++)
		for (col = 0; col < 3; col++)
			if (FONT[g][r] & (4 >> col))
				fill_rect(ctx, x+col*s, y+r*s,
				          x+col*s+s-1, y+r*s+s-1, c);
}

static int draw_string(struct fb_ctx *ctx, int x, int y,
                       const char *str, int s, uint16_t c)
{
	while (*str) {
		draw_glyph(ctx, x, y, char_to_glyph(*str++), s, c);
		x += 4 * s;
	}
	return x;
}

static int string_width(const char *str, int s)
{
	return ((int)strlen(str) * 4 - 1) * s;
}

/*
 * Draw a value with one decimal place: val_x10=234 → "23.4"
 */
static int draw_decimal(struct fb_ctx *ctx, int x, int y,
                        int val_x10, int s, uint16_t c)
{
	char buf[16];
	snprintf(buf, sizeof(buf), "%d.%d", val_x10 / 10, abs(val_x10) % 10);
	return draw_string(ctx, x, y, buf, s, c);
}

/* ── Panel drawing (same look as weather.c) ──────────────────────────────── */

#define BAR_X     10
#define BAR_W    220
#define BAR_H      8

static void draw_panel(struct fb_ctx *ctx, int py, int ph,
                       uint16_t bg, uint16_t accent)
{
	fill_rect(ctx, 0, py, 239, py + ph - 1, bg);
	fill_rect(ctx, 0, py, 239, py + 2, accent); /* accent strip */
}

static void draw_bar(struct fb_ctx *ctx, int py, int ph, int val, int max_val,
                     uint16_t fill_c, uint16_t empty_c)
{
	int bar_y = py + ph - 14;
	int filled = val < 0 ? 0 : BAR_W * val / max_val;
	if (filled > BAR_W) filled = BAR_W;
	fill_rect(ctx, BAR_X,          bar_y, BAR_X + filled - 1, bar_y + BAR_H, fill_c);
	fill_rect(ctx, BAR_X + filled, bar_y, BAR_X + BAR_W - 1,  bar_y + BAR_H, empty_c);
}

/* ── Sensor reading ──────────────────────────────────────────────────────── */

/* Reads a single numeric value out of a sysfs attribute file. */
static int read_sysfs_double(const char *path, double *val)
{
	FILE *f = fopen(path, "r");
	if (!f) return -1;
	int n = fscanf(f, "%lf", val);
	fclose(f);
	return (n == 1) ? 0 : -1;
}

/* in_temp_input is in milli-°C; returns temperature ×10 (e.g. 234 = 23.4°C). */
static int read_temp_x10(void)
{
	double milli_c;
	if (read_sysfs_double(TEMP_PATH, &milli_c) < 0)
		return 0;
	return (int)(milli_c / 100.0);
}

/* in_pressure_input is in kPa; returns pressure ×10 (e.g. 1013 = 101.3 kPa). */
static int read_press_x10(void)
{
	double kpa;
	if (read_sysfs_double(PRESS_PATH, &kpa) < 0)
		return 0;
	return (int)(kpa * 10.0);
}

/* ── Full display render ─────────────────────────────────────────────────── */

#define HEADER_H 40
#define PANEL_H 100

static void render(struct fb_ctx *ctx, struct tm *t,
                   int temp_x10, int press_x10)
{
	char date[32], time_s[16];
	int x;

	snprintf(date, sizeof(date), "%02d:%02d:%04d",
		 t->tm_mday, t->tm_mon + 1, t->tm_year + 1900);
	snprintf(time_s, sizeof(time_s), "%02d:%02d:%02d",
		 t->tm_hour, t->tm_min, t->tm_sec);

	/* ── header: date + time ── */
	fill_rect(ctx, 0, 0, 239, HEADER_H - 1, rgb565(8, 8, 20));
	x = (240 - string_width(time_s, 4)) / 2;
	draw_string(ctx, x, 4, time_s, 4, rgb565(210, 235, 255));
	x = (240 - string_width(date, 2)) / 2;
	draw_string(ctx, x, 28, date, 2, rgb565(120, 130, 160));

	/* ── temperature panel ── */
	{
		uint16_t bg     = rgb565(40, 15, 0);
		uint16_t accent = rgb565(255, 130, 0);
		uint16_t empty  = rgb565(70, 30, 0);
		int py = HEADER_H;

		draw_panel(ctx, py, PANEL_H, bg, accent);
		x = draw_decimal(ctx, 16, py + 30, temp_x10, 5, accent);
		draw_string(ctx, x + 10, py + 30, "C", 5, accent);
		draw_bar(ctx, py, PANEL_H, temp_x10, 400, accent, empty);
	}

	/* ── pressure panel ── */
	{
		uint16_t bg     = rgb565(0, 30, 30);
		uint16_t accent = rgb565(0, 220, 200);
		uint16_t empty  = rgb565(0, 50, 50);
		int py = HEADER_H + PANEL_H;

		draw_panel(ctx, py, PANEL_H, bg, accent);
		x = draw_decimal(ctx, 16, py + 30, press_x10, 5, accent);
		draw_string(ctx, x + 10, py + 30, "KPA", 5, accent);
		draw_bar(ctx, py, PANEL_H, press_x10 - 950, 150, accent, empty);
	}

	flush(ctx);
}

/* ── Main ────────────────────────────────────────────────────────────────── */

int main(int argc, char *argv[])
{
	const char *dev = argc > 1 ? argv[1] : "/dev/st7789";
	struct fb_ctx ctx;

	if (fb_open(&ctx, dev) < 0) return 1;
	if (ctx.vinfo.bits_per_pixel != 16) {
		fprintf(stderr, "Need 16 bpp framebuffer\n");
		fb_close(&ctx);
		return 1;
	}

	printf("Sensor panel running.  Press Ctrl-C to exit.\n");

	while (1) {
		time_t     now = time(NULL);
		struct tm *t   = localtime(&now);
		int temp_x10  = read_temp_x10();
		int press_x10 = read_press_x10();

		render(&ctx, t, temp_x10, press_x10);
		printf("T=%d.%d°C  P=%d.%dkPa\n",
		       temp_x10 / 10, abs(temp_x10) % 10,
		       press_x10 / 10, press_x10 % 10);

		sleep(1);
	}

	fb_close(&ctx); /* unreachable */
	return 0;
}
