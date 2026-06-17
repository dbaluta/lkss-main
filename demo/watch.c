// SPDX-License-Identifier: GPL-2.0
/*
 * watch.c – digital watch face for ST7789 240×240 framebuffer
 *
 * Displays:
 *   - System time as HH:MM:SS in large digits at the centre
 *   - Simulated temperature (°C) above the clock in amber
 *   - A randomly chosen Greek island name below the clock in teal
 *     (10 islands, cycling every 30 seconds)
 *
 * The face is styled as a round watch with a gold bezel ring and twelve
 * hour-marker tick marks.  Colons blink at 1 Hz.
 *
 * Cross-compile: aarch64-linux-gnu-gcc -O2 -o watch watch.c
 * Run on board:  ./watch [/dev/fb0]
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

/* ── 3×5 pixel font ──────────────────────────────────────────────────────── */
/*
 * FONT[g][row]: 3-bit row bitmask, bit2 = left column, bit0 = right column.
 *
 * Index mapping:
 *   0–9   digits '0'–'9'
 *   10–35 uppercase letters 'A'–'Z'  (index = 10 + c – 'A')
 *   36    ':'   colon (for HH:MM:SS)
 *   37    ' '   space
 *   38    '.'   period
 *   39    '-'   dash
 */
static const uint8_t FONT[40][5] = {
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
	{0,2,0,2,0}, /* ':' – two centre dots (36) */
	{0,0,0,0,0}, /* ' ' (37) */
	{0,0,0,0,2}, /* '.' (38) */
	{0,0,7,0,0}, /* '-' (39) */
};

static int char_to_glyph(char c)
{
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'A' && c <= 'Z') return 10 + (c - 'A');
	if (c == ':') return 36;
	if (c == ' ') return 37;
	if (c == '.') return 38;
	if (c == '-') return 39;
	return 37;
}

static void draw_glyph(struct fb_ctx *ctx,
                       int x, int y, int g, int s, uint16_t c)
{
	int r, col;
	if (g < 0 || g >= 40) return;
	for (r = 0; r < 5; r++)
		for (col = 0; col < 3; col++)
			if (FONT[g][r] & (4 >> col))
				fill_rect(ctx, x+col*s, y+r*s,
				          x+col*s+s-1, y+r*s+s-1, c);
}

static void draw_string(struct fb_ctx *ctx, int x, int y,
                        const char *str, int s, uint16_t c)
{
	while (*str) {
		draw_glyph(ctx, x, y, char_to_glyph(*str++), s, c);
		x += 4 * s;
	}
}

/* Draw non-negative integer; returns x after last glyph step. */
static int draw_number(struct fb_ctx *ctx, int x, int y,
                       int val, int s, uint16_t c)
{
	char buf[12];
	int i;
	snprintf(buf, sizeof(buf), "%d", val);
	for (i = 0; buf[i]; i++, x += 4 * s)
		draw_glyph(ctx, x, y, buf[i] - '0', s, c);
	return x;
}

/* ── Watch-face decorations ──────────────────────────────────────────────── */

/*
 * draw_ring() – draw a filled annular ring.
 * All pixels with r_inner² ≤ dx²+dy² ≤ r_outer² are painted.
 */
static void draw_ring(struct fb_ctx *ctx,
                      int cx, int cy, int r, int t, uint16_t c)
{
	int ri = r - t, ro = r, x, y;
	for (y = cy - ro; y <= cy + ro; y++) {
		int dy = y - cy;
		for (x = cx - ro; x <= cx + ro; x++) {
			int d2 = (x-cx)*(x-cx) + dy*dy;
			if (d2 >= ri*ri && d2 <= ro*ro)
				pixel(ctx, x, y, c);
		}
	}
}

/*
 * Twelve tick-mark positions on a circle of radius ≈105, centre (120,120).
 * Computed as (120 + 105·sin θ, 120 − 105·cos θ) for θ = 0°,30°,…,330°.
 * Values are pre-rounded to integers.
 */
static const int TICK_X[12] = {120,173,211,225,211,173,120, 67, 29, 15, 29, 67};
static const int TICK_Y[12] = { 15, 29, 67,120,173,211,225,211,173,120, 67, 29};

static void draw_ticks(struct fb_ctx *ctx, uint16_t c_main, uint16_t c_minor)
{
	int i;
	for (i = 0; i < 12; i++) {
		int x = TICK_X[i], y = TICK_Y[i];
		/* 12, 3, 6, 9 o'clock are larger and brighter */
		if (i % 3 == 0) {
			fill_rect(ctx, x-3, y-3, x+3, y+3, c_main);
		} else {
			fill_rect(ctx, x-1, y-1, x+1, y+1, c_minor);
		}
	}
}

/* ── Temperature display ─────────────────────────────────────────────────── */
/*
 * temp_x10: temperature × 10 (e.g. 234 = 23.4 °C).
 * Draws the integer part followed by a hollow-square degree mark and 'C'.
 */
static void draw_temperature(struct fb_ctx *ctx, int temp_x10, uint16_t col)
{
	int s     = 4; /* scale: each digit 12×20 px */
	int val   = temp_x10 / 10;
	int ndig  = (val >= 100) ? 3 : 2;
	/* total pixel width: digits + gap + degree(2s) + gap(s) + C-glyph(3s) */
	int total = ndig * 4 * s + s + 2 * s + s + 3 * s;
	int x     = (240 - total) / 2;
	int y     = 65;

	x = draw_number(ctx, x, y, val, s, col);
	/* hollow degree mark: outer 2s×2s box, inner cleared */
	x += s; /* small gap */
	fill_rect(ctx, x, y, x + 2*s - 1, y + 2*s - 1, col);
	fill_rect(ctx, x+1, y+1, x + 2*s - 2, y + 2*s - 2, 0x0000u);
	x += 3 * s;
	draw_glyph(ctx, x, y, char_to_glyph('C'), s, col);
}

/* ── Clock display ───────────────────────────────────────────────────────── */
/*
 * HH:MM:SS using SCALE_CLOCK=6 → each digit 18×30 px.
 * Total width = 8 glyphs × 24 step − trailing gap + 18 = 186 px.
 * Centred: x_start = (240−186)/2 = 27.
 * Colons alternate colour each second to create a 1 Hz blink.
 */
#define SCALE_CLOCK 6
#define X_CLOCK     27
#define Y_CLOCK    100

static void draw_clock(struct fb_ctx *ctx,
                       int hh, int mm, int ss,
                       uint16_t col_digit, uint16_t col_colon)
{
	int x = X_CLOCK;

	draw_glyph(ctx, x, Y_CLOCK, hh/10, SCALE_CLOCK, col_digit); x += 4*SCALE_CLOCK;
	draw_glyph(ctx, x, Y_CLOCK, hh%10, SCALE_CLOCK, col_digit); x += 4*SCALE_CLOCK;
	draw_glyph(ctx, x, Y_CLOCK, 36,    SCALE_CLOCK, col_colon); x += 4*SCALE_CLOCK;
	draw_glyph(ctx, x, Y_CLOCK, mm/10, SCALE_CLOCK, col_digit); x += 4*SCALE_CLOCK;
	draw_glyph(ctx, x, Y_CLOCK, mm%10, SCALE_CLOCK, col_digit); x += 4*SCALE_CLOCK;
	draw_glyph(ctx, x, Y_CLOCK, 36,    SCALE_CLOCK, col_colon); x += 4*SCALE_CLOCK;
	draw_glyph(ctx, x, Y_CLOCK, ss/10, SCALE_CLOCK, col_digit); x += 4*SCALE_CLOCK;
	draw_glyph(ctx, x, Y_CLOCK, ss%10, SCALE_CLOCK, col_digit);
}

/* ── Island display ──────────────────────────────────────────────────────── */

#define SCALE_ISLAND 2
#define Y_ISLAND    158

static void draw_island(struct fb_ctx *ctx, const char *name, uint16_t col)
{
	int s   = SCALE_ISLAND;
	int len = (int)strlen(name);
	int w   = (len * 4 - 1) * s; /* pixel width without trailing gap */
	int x   = (240 - w) / 2;

	draw_string(ctx, x, Y_ISLAND, name, s, col);
	/* thin underline two pixels below the glyph */
	fill_rect(ctx, x, Y_ISLAND + 5*s + 2, x + w - 1, Y_ISLAND + 5*s + 2, col);
}

/* ── Full-face redraw ────────────────────────────────────────────────────── */

static uint16_t COL_BG, COL_BEZEL, COL_INNER_RING;
static uint16_t COL_TICK_MAIN, COL_TICK_MINOR;
static uint16_t COL_DIGIT, COL_COLON_ON, COL_COLON_OFF;
static uint16_t COL_TEMP, COL_ISLAND;

static void draw_face(struct fb_ctx *ctx,
                      int hh, int mm, int ss,
                      int temp_x10, const char *island)
{
	uint16_t col_colon = (ss & 1) ? COL_COLON_ON : COL_COLON_OFF;

	fill_rect(ctx, 0, 0, 239, 239, COL_BG);
	draw_ring(ctx, 120, 120, 118, 5, COL_BEZEL);
	draw_ring(ctx, 120, 120, 111, 1, COL_INNER_RING);
	draw_ticks(ctx, COL_TICK_MAIN, COL_TICK_MINOR);
	draw_temperature(ctx, temp_x10, COL_TEMP);
	draw_clock(ctx, hh, mm, ss, COL_DIGIT, col_colon);
	draw_island(ctx, island, COL_ISLAND);
}

/* ── Greek islands ───────────────────────────────────────────────────────── */

static const char *ISLANDS[10] = {
	"SANTORINI", "MYKONOS",   "CRETE",     "RHODES",
	"CORFU",     "ZAKYNTHOS", "NAXOS",     "PAROS",
	"KEFALONIA", "HYDRA",
};

/* ── Main ────────────────────────────────────────────────────────────────── */

int main(int argc, char *argv[])
{
	const char *dev = argc > 1 ? argv[1] : "/dev/fb0";
	struct fb_ctx ctx;

	if (fb_open(&ctx, dev) < 0) return 1;
	if (ctx.vinfo.bits_per_pixel != 16) {
		fprintf(stderr, "Need 16 bpp framebuffer\n");
		fb_close(&ctx);
		return 1;
	}

	srand((unsigned)time(NULL));

	COL_BG         = rgb565(6,  6,  18);
	COL_BEZEL      = rgb565(190, 155, 55);  /* gold  */
	COL_INNER_RING = rgb565(45,  45,  70);  /* dim   */
	COL_TICK_MAIN  = rgb565(220, 190, 80);  /* gold  */
	COL_TICK_MINOR = rgb565(70,  70,  95);  /* dim   */
	COL_DIGIT      = rgb565(210, 235, 255); /* cool white */
	COL_COLON_ON   = rgb565(210, 235, 255);
	COL_COLON_OFF  = rgb565(35,  45,  60);  /* dim colon */
	COL_TEMP       = rgb565(255, 165, 40);  /* amber */
	COL_ISLAND     = rgb565(55,  200, 175); /* teal  */

	/* initial sensor state */
	int island_idx = rand() % 10;
	int temp_x10   = 180 + rand() % 120; /* 18.0–30.0 °C */
	int last_min   = -1;                  /* track minute for island changes */
	int last_30s   = -1;                  /* track 30-second marks for temp  */

	printf("Watch face running.  Press Ctrl-C to exit.\n");

	while (1) {
		time_t     now  = time(NULL);
		struct tm *t    = localtime(&now);
		int hh = t->tm_hour;
		int mm = t->tm_min;
		int ss = t->tm_sec;

		/* change island every minute */
		if (mm != last_min) {
			island_idx = (island_idx + 1) % 10;
			last_min   = mm;
			printf("  Location: %s\n", ISLANDS[island_idx]);
		}

		/* drift temperature every 30 seconds */
		{
			int mark30 = (hh * 60 + mm) * 2 + (ss / 30);
			if (mark30 != last_30s) {
				temp_x10 += (rand() % 7) - 3; /* ±0.3 °C */
				if (temp_x10 < 100) temp_x10 = 100;
				if (temp_x10 > 400) temp_x10 = 400;
				last_30s = mark30;
			}
		}

		draw_face(&ctx, hh, mm, ss, temp_x10, ISLANDS[island_idx]);
		flush(&ctx);

		usleep(500000); /* 2 Hz – fast enough for 1 Hz colon blink */
	}

	fb_close(&ctx); /* unreachable */
	return 0;
}
