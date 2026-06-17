// SPDX-License-Identifier: GPL-2.0
/*
 * snake_auto.c – self-playing Snake demo for ST7789 240×240 framebuffer
 *
 * The snake is driven by a greedy AI: at each step it picks the direction
 * that minimises the Manhattan distance to the food without hitting a wall
 * or its own body.  Three games are played; a red flash marks each game over.
 *
 * Cross-compile: aarch64-linux-gnu-gcc -O2 -o snake_auto snake_auto.c
 * Run on board:  ./snake_auto [/dev/st7789]
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

/* ── 3×5 pixel digit font ────────────────────────────────────────────────── */
/*
 * FONT[d][row] is a 3-bit mask: bit2=left col, bit1=centre, bit0=right col.
 * Rows are top-to-bottom.
 */
static const uint8_t FONT[10][5] = {
	{7,5,5,5,7}, /* 0 */  {2,6,2,2,7}, /* 1 */
	{7,1,7,4,7}, /* 2 */  {7,1,3,1,7}, /* 3 */
	{5,5,7,1,1}, /* 4 */  {7,4,7,1,7}, /* 5 */
	{7,4,7,5,7}, /* 6 */  {7,1,1,1,1}, /* 7 */
	{7,5,7,5,7}, /* 8 */  {7,5,7,1,7}, /* 9 */
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

/* Draw a non-negative integer; returns x position after last digit. */
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

/* ── Snake game constants ────────────────────────────────────────────────── */

#define CELL   10                   /* pixel size of one grid cell          */
#define GCOLS  (240 / CELL)         /* 24 columns                           */
#define GROWS  (240 / CELL)         /* 24 rows                              */
#define HUD_H  (2 * CELL)           /* 20 px score strip at top             */
#define GY0    (HUD_H / CELL)       /* first playfield row in grid units    */
#define MAXLEN (GCOLS * (GROWS - GY0))

#define UP 0
#define RT 1
#define DN 2
#define LT 3

static const int DX[4] = { 0, 1, 0, -1 };
static const int DY[4] = { -1, 0, 1,  0 };

struct pt { int x, y; };

static struct pt body[MAXLEN];
static int       blen, bdir, score;
static struct pt food;
static uint8_t   grid[GROWS][GCOLS]; /* non-zero = occupied by snake body */

/* ── Game logic ──────────────────────────────────────────────────────────── */

static void game_init(void)
{
	int i;
	memset(grid, 0, sizeof(grid));
	bdir  = RT;
	blen  = 4;
	score = 0;
	for (i = 0; i < blen; i++) {
		body[i].x = GCOLS / 2 - i;
		body[i].y = GROWS / 2;
		grid[body[i].y][body[i].x] = 1;
	}
}

static void spawn_food(void)
{
	int x, y;
	do {
		x = rand() % GCOLS;
		y = GY0 + rand() % (GROWS - GY0);
	} while (grid[y][x]);
	food.x = x;
	food.y = y;
}

/*
 * Greedy AI: among all safe moves, pick the one that minimises the Manhattan
 * distance to the food.  Falls back to any non-reversing move if cornered.
 */
static int ai_dir(void)
{
	struct pt h = body[0];
	int best = -1, best_dist = 99999, d;

	for (d = 0; d < 4; d++) {
		int nx, ny, dist;
		/* cannot reverse onto own neck */
		if ((d == UP && bdir == DN) || (d == DN && bdir == UP)) continue;
		if ((d == LT && bdir == RT) || (d == RT && bdir == LT)) continue;
		nx = h.x + DX[d];
		ny = h.y + DY[d];
		if (nx < 0 || nx >= GCOLS || ny < GY0 || ny >= GROWS) continue;
		if (grid[ny][nx]) continue;
		dist = abs(food.x - nx) + abs(food.y - ny);
		if (dist < best_dist) { best_dist = dist; best = d; }
	}
	if (best >= 0) return best;

	/* no safe greedy move – take any non-reversing direction to survive */
	for (d = 0; d < 4; d++) {
		if ((d == UP && bdir == DN) || (d == DN && bdir == UP)) continue;
		if ((d == LT && bdir == RT) || (d == RT && bdir == LT)) continue;
		return d;
	}
	return bdir;
}

/* ── Drawing helpers ─────────────────────────────────────────────────────── */

static void draw_cell(struct fb_ctx *ctx, int gx, int gy, uint16_t c)
{
	/* 1-pixel gap between cells for a grid look */
	fill_rect(ctx, gx * CELL, gy * CELL,
	          gx * CELL + CELL - 2, gy * CELL + CELL - 2, c);
}

static void draw_hud(struct fb_ctx *ctx)
{
	fill_rect(ctx, 0, 0, 239, HUD_H - 1, rgb565(0, 0, 40));
	draw_number(ctx, 4, 4, score, 2, rgb565(0, 230, 80));
}

/* ── Game step: returns 1 while alive, 0 on collision ───────────────────── */

static int game_step(struct fb_ctx *ctx)
{
	struct pt nh, tail;
	int ate;

	bdir = ai_dir();
	nh.x = body[0].x + DX[bdir];
	nh.y = body[0].y + DY[bdir];

	if (nh.x < 0 || nh.x >= GCOLS || nh.y < GY0 || nh.y >= GROWS ||
	    grid[nh.y][nh.x])
		return 0;

	ate = (nh.x == food.x && nh.y == food.y);

	if (!ate) {
		/* remove tail before shift */
		tail = body[blen - 1];
		grid[tail.y][tail.x] = 0;
		draw_cell(ctx, tail.x, tail.y, rgb565(12, 12, 24));
		memmove(&body[1], &body[0], (blen - 1) * sizeof(struct pt));
	} else {
		/* grow by keeping tail in place; shift body up */
		if (blen < MAXLEN - 1) blen++;
		memmove(&body[1], &body[0], (blen - 1) * sizeof(struct pt));
		score++;
		/* erase consumed food cell, spawn a new one */
		draw_cell(ctx, food.x, food.y, rgb565(12, 12, 24));
		spawn_food();
		draw_cell(ctx, food.x, food.y, rgb565(220, 40, 40));
		draw_hud(ctx);
	}

	body[0] = nh;
	grid[nh.y][nh.x] = 1;

	/* incremental redraw: only head and old-head-now-body change colour */
	if (blen > 1)
		draw_cell(ctx, body[1].x, body[1].y, rgb565(0, 140, 45));
	draw_cell(ctx, body[0].x, body[0].y, rgb565(0, 255, 90));

	flush(ctx);
	return 1;
}

/* ── Main ────────────────────────────────────────────────────────────────── */

int main(int argc, char *argv[])
{
	const char *dev = argc > 1 ? argv[1] : "/dev/st7789";
	struct fb_ctx ctx;
	int game, i;

	if (fb_open(&ctx, dev) < 0) return 1;
	if (ctx.vinfo.bits_per_pixel != 16) {
		fprintf(stderr, "Need 16 bpp framebuffer\n");
		fb_close(&ctx);
		return 1;
	}

	srand((unsigned)time(NULL));

	for (game = 0; game < 3; game++) {
		printf("Game %d\n", game + 1);

		/* clear screen */
		fill_rect(&ctx, 0, 0, 239, 239, rgb565(12, 12, 24));
		game_init();
		spawn_food();

		draw_hud(&ctx);
		draw_cell(&ctx, food.x, food.y, rgb565(220, 40, 40));
		for (i = blen - 1; i >= 1; i--)
			draw_cell(&ctx, body[i].x, body[i].y, rgb565(0, 140, 45));
		draw_cell(&ctx, body[0].x, body[0].y, rgb565(0, 255, 90));
		flush(&ctx);

		while (game_step(&ctx))
			usleep(120000); /* ~8 fps */

		printf("  Score: %d\n", score);

		/* game-over flash */
		for (i = 0; i < 6; i++) {
			fill_rect(&ctx, 0, HUD_H, 239, 239,
			          i & 1 ? rgb565(12, 12, 24) : rgb565(100, 0, 0));
			flush(&ctx);
			usleep(200000);
		}
		sleep(1);
	}

	fb_close(&ctx);
	return 0;
}
