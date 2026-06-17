// SPDX-License-Identifier: GPL-2.0
/*
 * weather.c – simulated weather-station panel for ST7789 240×240 framebuffer
 *
 * Four panels display simulated sensor readings that drift slowly over time:
 *   • Temperature  (°C)    – orange panel, thermometer bar
 *   • Humidity     (%)     – blue panel, drop bar
 *   • Pressure     (hPa)   – teal panel, gauge bar
 *   • Wind speed   (km/h)  – purple panel, speed bar + compass rose
 *
 * Values are updated every 3 seconds with a small random walk.
 *
 * Cross-compile: aarch64-linux-gnu-gcc -O2 -o weather weather.c
 * Run on board:  ./weather [/dev/fb0]
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
#include <linux/fb.h>

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
	if (ioctl(ctx->fd, FBIOGET_VSCREENINFO, &ctx->vinfo) ||
	    ioctl(ctx->fd, FBIOGET_FSCREENINFO, &ctx->finfo)) {
		perror("ioctl"); close(ctx->fd); return -1;
	}
	ctx->size = ctx->finfo.smem_len;
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
	struct fb_var_screeninfo v = ctx->vinfo;
	v.yoffset = 0;
	ioctl(ctx->fd, FBIOPAN_DISPLAY, &v);
}

/* ── 3×5 pixel font (digits only) ───────────────────────────────────────── */

static const uint8_t FONT[10][5] = {
	{7,5,5,5,7}, {2,6,2,2,7}, {7,1,7,4,7}, {7,1,3,1,7}, {5,5,7,1,1},
	{7,4,7,1,7}, {7,4,7,5,7}, {7,1,1,1,1}, {7,5,7,5,7}, {7,5,7,1,7},
};

static void draw_digit(struct fb_ctx *ctx, int x, int y, int d, int s, uint16_t c)
{
	int r, col;
	if (d < 0 || d > 9) return;
	for (r = 0; r < 5; r++)
		for (col = 0; col < 3; col++)
			if (FONT[d][r] & (4 >> col))
				fill_rect(ctx, x+col*s, y+r*s,
				          x+col*s+s-1, y+r*s+s-1, c);
}

/*
 * Draw a non-negative integer at scale s; returns x after the last digit.
 * Width per character = 3*s (glyph) + s (gap) = 4*s pixels.
 */
static int draw_number(struct fb_ctx *ctx, int x, int y,
                       int val, int s, uint16_t c)
{
	char buf[12];
	int i;
	snprintf(buf, sizeof(buf), "%d", val);
	for (i = 0; buf[i]; i++, x += 4 * s)
		draw_digit(ctx, x, y, buf[i] - '0', s, c);
	return x;
}

/*
 * Draw a value with one decimal place: val_x10=234 → "23.4"
 * The decimal point is a small filled square at the baseline.
 */
static void draw_decimal(struct fb_ctx *ctx, int x, int y,
                         int val_x10, int s, uint16_t c)
{
	x = draw_number(ctx, x, y, val_x10 / 10, s, c);
	/* decimal dot at bottom of character cell */
	fill_rect(ctx, x, y + 4 * s, x + s - 1, y + 5 * s - 1, c);
	x += 2 * s;
	draw_digit(ctx, x, y, val_x10 % 10, s, c);
}

/* ── Compass rose ────────────────────────────────────────────────────────── */
/*
 * Draw a simple compass at (cx, cy) with radius r and an arrow in one of the
 * 8 cardinal/intercardinal directions (0=N, 1=NE, … 7=NW).
 */
static void draw_compass(struct fb_ctx *ctx, int cx, int cy, int r, int dir8,
                         uint16_t ring_c, uint16_t arrow_c)
{
	static const int CDX[8] = { 0, 1, 1, 1, 0,-1,-1,-1};
	static const int CDY[8] = {-1,-1, 0, 1, 1, 1, 0,-1}; /* screen Y */
	int x, y, i;

	/* circle outline */
	for (y = cy - r; y <= cy + r; y++) {
		for (x = cx - r; x <= cx + r; x++) {
			int dx = x - cx, dy = y - cy;
			int d2 = dx * dx + dy * dy;
			if (d2 >= (r-1)*(r-1) && d2 <= r*r)
				pixel(ctx, x, y, ring_c);
		}
	}

	/* arrow: thick line from centre to rim */
	for (i = 2; i <= r - 2; i++) {
		int ax = cx + CDX[dir8] * i;
		int ay = cy + CDY[dir8] * i;
		fill_rect(ctx, ax - 1, ay - 1, ax + 1, ay + 1, arrow_c);
	}
	/* arrowhead: wider tip */
	{
		int ax = cx + CDX[dir8] * (r - 2);
		int ay = cy + CDY[dir8] * (r - 2);
		fill_rect(ctx, ax - 2, ay - 2, ax + 2, ay + 2, arrow_c);
	}

	/* cardinal tick marks */
	for (i = 0; i < 4; i++) {
		int tx = cx + CDX[i * 2] * (r - 2);
		int ty = cy + CDY[i * 2] * (r - 2);
		fill_rect(ctx, tx - 1, ty - 1, tx + 1, ty + 1, ring_c);
	}
}

/* ── Panel drawing ───────────────────────────────────────────────────────── */

#define PANEL_H   58   /* height of each metric panel */
#define PANEL_GAP  3   /* gap between panels          */
#define BAR_X     10   /* bar graph left edge         */
#define BAR_W    220   /* bar graph width             */
#define BAR_H      8   /* bar graph height            */

/*
 * draw_panel – render one metric panel.
 *
 * @py        top y of panel
 * @bg        dark background colour for panel
 * @accent    bright colour for header strip, bar fill, numbers
 * @label_c   colour for the small icon block in top-left
 */
static void draw_panel(struct fb_ctx *ctx, int py, uint16_t bg,
                       uint16_t accent, uint16_t label_c)
{
	/* background */
	fill_rect(ctx, 0, py, 239, py + PANEL_H - 1, bg);
	/* 3-pixel accent strip along the top */
	fill_rect(ctx, 0, py, 239, py + 2, accent);
	/* 8×8 label icon square */
	fill_rect(ctx, 4, py + 6, 12, py + 14, label_c);
}

/*
 * draw_bar – horizontal bar graph.
 *
 * @val   current value (0–max_val range mapped to 0–BAR_W)
 */
static void draw_bar(struct fb_ctx *ctx, int py, int val, int max_val,
                     uint16_t fill_c, uint16_t empty_c)
{
	int bar_y = py + PANEL_H - 14;
	int filled = BAR_W * val / max_val;
	if (filled > BAR_W) filled = BAR_W;
	fill_rect(ctx, BAR_X,          bar_y, BAR_X + filled - 1, bar_y + BAR_H, fill_c);
	fill_rect(ctx, BAR_X + filled, bar_y, BAR_X + BAR_W - 1,  bar_y + BAR_H, empty_c);
}

/* ── Sensor simulation ───────────────────────────────────────────────────── */

/*
 * Sensor values are stored as integers:
 *   temp_x10  – temperature × 10  (e.g. 234 = 23.4 °C),  range 100–400
 *   humid     – relative humidity (%), range 20–95
 *   press     – pressure (hPa),        range 970–1040
 *   wind      – wind speed (km/h),     range 0–50
 *   wind_dir  – 0=N … 7=NW (increments of 45°)
 */
static int temp_x10, humid, press, wind, wind_dir;

static void sensors_init(void)
{
	temp_x10  = 200 + rand() % 100; /* 20–30 °C */
	humid     = 40  + rand() % 40;  /* 40–80 %  */
	press     = 1000 + rand() % 20; /* 1000–1020 hPa */
	wind      = 5   + rand() % 15;  /* 5–20 km/h */
	wind_dir  = rand() % 8;
}

static void sensors_update(void)
{
	/* random walk with clamping */
	temp_x10  += (rand() % 7) - 3;
	humid     += (rand() % 5) - 2;
	press     += (rand() % 3) - 1;
	wind      += (rand() % 5) - 2;
	if (rand() % 4 == 0) wind_dir = (wind_dir + (rand()%3 - 1) + 8) % 8;

	if (temp_x10 < 50)   temp_x10 = 50;
	if (temp_x10 > 450)  temp_x10 = 450;
	if (humid < 10)      humid = 10;
	if (humid > 99)      humid = 99;
	if (press < 960)     press = 960;
	if (press > 1060)    press = 1060;
	if (wind < 0)        wind = 0;
	if (wind > 60)       wind = 60;
}

/* ── Full display render ─────────────────────────────────────────────────── */

static void render(struct fb_ctx *ctx)
{
	/*
	 * Four panels stacked vertically with a small gap between each.
	 * Panel 0 (y=0):   Temperature
	 * Panel 1 (y=61):  Humidity
	 * Panel 2 (y=122): Pressure
	 * Panel 3 (y=183): Wind
	 */
	const int PY[4] = { 0, 61, 122, 183 };

	/* ── Temperature ── */
	{
		uint16_t bg     = rgb565(40, 15, 0);
		uint16_t accent = rgb565(255, 130, 0);
		uint16_t empty  = rgb565(70, 30, 0);
		draw_panel(ctx, PY[0], bg, accent, accent);
		/* value: "XX.X" at scale 4 (each digit 12×20 px) */
		draw_decimal(ctx, 16, PY[0] + 18, temp_x10, 4, accent);
		/* degree symbol: small circle */
		fill_rect(ctx, 140, PY[0] + 18, 144, PY[0] + 22, accent);
		fill_rect(ctx, 141, PY[0] + 19, 143, PY[0] + 21, bg);
		/* 'C' indicator */
		draw_digit(ctx, 146, PY[0] + 20, 0, 2, accent); /* reuse '0' shape as 'C' proxy */
		draw_bar(ctx, PY[0], temp_x10 - 50, 400, accent, empty);
	}

	/* ── Humidity ── */
	{
		uint16_t bg     = rgb565(0, 10, 50);
		uint16_t accent = rgb565(60, 140, 255);
		uint16_t empty  = rgb565(10, 25, 70);
		draw_panel(ctx, PY[1], bg, accent, accent);
		draw_number(ctx, 16, PY[1] + 18, humid, 4, accent);
		/* '%' indicator: two small dots and a diagonal */
		fill_rect(ctx, 100, PY[1] + 18, 104, PY[1] + 22, accent);
		fill_rect(ctx, 110, PY[1] + 30, 114, PY[1] + 34, accent);
		{
			int i;
			for (i = 0; i < 12; i++)
				pixel(ctx, 104 + i, PY[1] + 34 - i, accent);
		}
		draw_bar(ctx, PY[1], humid - 10, 90, accent, empty);
	}

	/* ── Pressure ── */
	{
		uint16_t bg     = rgb565(0, 30, 30);
		uint16_t accent = rgb565(0, 220, 200);
		uint16_t empty  = rgb565(0, 50, 50);
		draw_panel(ctx, PY[2], bg, accent, accent);
		draw_number(ctx, 16, PY[2] + 18, press, 4, accent);
		draw_bar(ctx, PY[2], press - 960, 100, accent, empty);
	}

	/* ── Wind ── */
	{
		uint16_t bg     = rgb565(20, 0, 40);
		uint16_t accent = rgb565(180, 80, 255);
		uint16_t empty  = rgb565(40, 10, 70);
		draw_panel(ctx, PY[3], bg, accent, accent);
		draw_number(ctx, 16, PY[3] + 18, wind, 4, accent);
		/* compass rose on the right side of the wind panel */
		draw_compass(ctx, 195, PY[3] + PANEL_H / 2,
		             22, wind_dir,
		             rgb565(100, 50, 150), accent);
		draw_bar(ctx, PY[3], wind, 60, accent, empty);
	}

	flush(ctx);
}

/* ── Main ────────────────────────────────────────────────────────────────── */

int main(int argc, char *argv[])
{
	const char *dev = argc > 1 ? argv[1] : "/dev/fb0";
	struct fb_ctx ctx;
	int i;

	if (fb_open(&ctx, dev) < 0) return 1;
	if (ctx.vinfo.bits_per_pixel != 16) {
		fprintf(stderr, "Need 16 bpp framebuffer\n");
		fb_close(&ctx);
		return 1;
	}

	srand((unsigned)time(NULL));
	sensors_init();

	/* clear display */
	fill_rect(&ctx, 0, 0, 239, 239, 0x0000u);

	/* run for 60 updates (~3 minutes) */
	for (i = 0; i < 60; i++) {
		render(&ctx);
		printf("T=%d.%d°C  H=%d%%  P=%dhPa  W=%dkm/h dir=%d\n",
		       temp_x10 / 10, temp_x10 % 10,
		       humid, press, wind, wind_dir);
		sleep(3);
		sensors_update();
	}

	fb_close(&ctx);
	return 0;
}
