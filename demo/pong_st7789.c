// SPDX-License-Identifier: GPL-2.0
/*
 * pong.c – colourful auto-play Pong demo for ST7789 240×240 framebuffer
 *
 * Both paddles are AI-controlled and track the ball.  The ball gains speed
 * after each paddle hit.  First to 10 points wins; winning side flashes.
 *
 * Colour scheme: cyan left paddle, magenta right paddle, yellow ball.
 *
 * Cross-compile: aarch64-linux-gnu-gcc -O2 -o pong pong.c
 * Run on board:  ./pong [/dev/st7789]
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

/* ── 3×5 pixel font (digits + uppercase letters for player names) ────────── */
/*
 * FONT[g][row]: 3-bit row mask, bit2=left col, bit1=centre, bit0=right col.
 * Glyph indices: 0-9 = digits, 10='A', 11='B', 12='E', 13='M',
 *                14='P', 15='R', 16=' ' (space).
 */
static const uint8_t FONT[17][5] = {
	/* digits */
	{7,5,5,5,7}, {2,6,2,2,7}, {7,1,7,4,7}, {7,1,3,1,7}, {5,5,7,1,1},
	{7,4,7,1,7}, {7,4,7,5,7}, {7,1,1,1,1}, {7,5,7,5,7}, {7,5,7,1,7},
	/* letters */
	{2,5,7,5,5}, /* A: .X./X.X/XXX/X.X/X.X */
	{6,5,6,5,6}, /* B: XX./X.X/XX./X.X/XX. */
	{7,4,6,4,7}, /* E: XXX/X../XX./X../XXX */
	{5,7,5,5,5}, /* M: X.X/XXX/X.X/X.X/X.X */
	{6,5,6,4,4}, /* P: XX./X.X/XX./X../X.. */
	{6,5,6,4,1}, /* R: XX./X.X/XX./X../..X */
	{0,0,0,0,0}, /* ' ' (space) */
};

static int char_to_glyph(char c)
{
	if (c >= '0' && c <= '9') return c - '0';
	switch (c) {
	case 'A': return 10; case 'B': return 11; case 'E': return 12;
	case 'M': return 13; case 'P': return 14; case 'R': return 15;
	default:  return 16; /* space for any unknown character */
	}
}

static void draw_glyph(struct fb_ctx *ctx, int x, int y, int g, int s, uint16_t c)
{
	int r, col;
	if (g < 0 || g >= 17) return;
	for (r = 0; r < 5; r++)
		for (col = 0; col < 3; col++)
			if (FONT[g][r] & (4 >> col))
				fill_rect(ctx, x+col*s, y+r*s,
				          x+col*s+s-1, y+r*s+s-1, c);
}

static void draw_number(struct fb_ctx *ctx, int x, int y,
                        int val, int s, uint16_t c)
{
	char buf[12];
	int i;
	snprintf(buf, sizeof(buf), "%d", val);
	for (i = 0; buf[i]; i++, x += 4 * s)
		draw_glyph(ctx, x, y, buf[i] - '0', s, c);
}

static void draw_string(struct fb_ctx *ctx, int x, int y,
                        const char *str, int s, uint16_t c)
{
	while (*str) {
		draw_glyph(ctx, x, y, char_to_glyph(*str++), s, c);
		x += 4 * s; /* glyph width (3) + gap (1) = 4 columns */
	}
}

/* ── Button input via sysfs ──────────────────────────────────────────────── */

#define NUM_BTNS 4

static int btn_fd[NUM_BTNS] = { -1, -1, -1, -1 };

/*
 * Try to open the button sysfs files from the lab4 (or gpio-demo) driver.
 * Returns 0 if all four buttons were opened, -1 otherwise.
 */
static int btns_open(void)
{
	static const char * const bases[] = {
		"/sys/bus/platform/devices/lkss-lab4",
		"/sys/bus/platform/devices/lkss-gpio",
	};
	char path[256];
	int i, j;

	for (j = 0; j < (int)(sizeof(bases) / sizeof(bases[0])); j++) {
		for (i = 0; i < NUM_BTNS; i++) {
			snprintf(path, sizeof(path), "%s/button%d", bases[j], i);
			btn_fd[i] = open(path, O_RDONLY | O_NONBLOCK);
			if (btn_fd[i] < 0)
				break;
		}
		if (i == NUM_BTNS)
			return 0;
		while (--i >= 0) {
			close(btn_fd[i]);
			btn_fd[i] = -1;
		}
	}
	return -1;
}

static void btns_close(void)
{
	int i;
	for (i = 0; i < NUM_BTNS; i++) {
		if (btn_fd[i] >= 0) {
			close(btn_fd[i]);
			btn_fd[i] = -1;
		}
	}
}

/* Returns 1 if the button is currently pressed, 0 otherwise. */
static int btn_pressed(int idx)
{
	char buf[4];
	ssize_t n;
	lseek(btn_fd[idx], 0, SEEK_SET);
	n = read(btn_fd[idx], buf, sizeof(buf) - 1);
	if (n <= 0) return 0;
	return buf[0] == '1';
}

/* ── Pong game ───────────────────────────────────────────────────────────── */

#define W       240
#define H       240
#define HUD_H    34   /* header: name row + score row         */
#define PAD_W    10   /* paddle width in pixels               */
#define PAD_H    55   /* paddle height in pixels              */
#define PAD_STEP  4   /* max pixels a paddle moves per frame  */
#define BALL_R    4   /* ball half-size (square approximation)*/
#define WIN_SCORE 10

/* Fixed-point ball physics: positions and velocities stored ×16. */
#define FP 16

static int bx_fp, by_fp;   /* ball centre position × FP */
static int bdx_fp, bdy_fp; /* ball velocity × FP        */
static int lpy, rpy;       /* paddle top-edge Y         */
static int lscore, rscore;

/* Colour constants */
static uint16_t COL_BG, COL_LPAD, COL_RPAD, COL_BALL, COL_MIDLINE;

/* x position of each paddle's left edge */
#define LPAD_X  5
#define RPAD_X  (W - 5 - PAD_W)

static void serve(int left)
{
	bx_fp  = (W / 2) * FP;
	by_fp  = (H / 2) * FP;
	bdx_fp = (left ? -2 : 2) * FP;
	/* random vertical component: -2, -1, 1, or 2 pixels/frame */
	bdy_fp = ((rand() % 2) ? 1 : -1) * (1 + rand() % 2) * FP;
}

static void clamp_paddle(int *py)
{
	if (*py < HUD_H)          *py = HUD_H;
	if (*py + PAD_H > H)      *py = H - PAD_H;
}

static void ai_move_paddle(int *py, int target_y)
{
	int centre = *py + PAD_H / 2;
	int diff   = target_y - centre;
	if (diff >  PAD_STEP) diff =  PAD_STEP;
	if (diff < -PAD_STEP) diff = -PAD_STEP;
	*py += diff;
	clamp_paddle(py);
}

/* Move paddle by PAD_STEP each frame based on button states. */
static void human_move_paddle(int *py, int up, int dn)
{
	if (up && !dn) *py -= PAD_STEP;
	if (dn && !up) *py += PAD_STEP;
	clamp_paddle(py);
}

static void draw_midline(struct fb_ctx *ctx)
{
	int y;
	for (y = HUD_H; y < H; y += 10)
		fill_rect(ctx, W / 2 - 1, y, W / 2, y + 4, COL_MIDLINE);
}

static void draw_hud(struct fb_ctx *ctx)
{
	/* background + centre divider */
	fill_rect(ctx, 0,         0, W / 2 - 2, HUD_H - 1, rgb565(0, 0, 50));
	fill_rect(ctx, W / 2 - 1, 0, W / 2,     HUD_H - 1, rgb565(20, 20, 70));
	fill_rect(ctx, W / 2 + 1, 0, W - 1,     HUD_H - 1, rgb565(0, 0, 50));

	/* player names – scale 2, top row (y=4) */
	draw_string(ctx, 4,           4, "PAPA BEAR", 2, COL_LPAD);
	draw_string(ctx, W / 2 + 4,   4, "MAMA BEAR", 2, COL_RPAD);

	/* scores – scale 3, second row (y=17) */
	draw_number(ctx, 4,           17, lscore, 3, COL_LPAD);
	draw_number(ctx, W / 2 + 4,   17, rscore, 3, COL_RPAD);
}

static void draw_scene(struct fb_ctx *ctx)
{
	int bx = bx_fp / FP, by = by_fp / FP;

	fill_rect(ctx, 0, HUD_H, W - 1, H - 1, COL_BG);
	draw_midline(ctx);

	/* paddles */
	fill_rect(ctx, LPAD_X, lpy, LPAD_X + PAD_W - 1, lpy + PAD_H - 1, COL_LPAD);
	fill_rect(ctx, RPAD_X, rpy, RPAD_X + PAD_W - 1, rpy + PAD_H - 1, COL_RPAD);

	/* ball with small glow effect */
	fill_rect(ctx, bx - BALL_R - 1, by - BALL_R - 1,
	          bx + BALL_R + 1, by + BALL_R + 1, rgb565(80, 60, 0));
	fill_rect(ctx, bx - BALL_R, by - BALL_R, bx + BALL_R, by + BALL_R, COL_BALL);
}

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

	srand((unsigned)time(NULL));

	/* Open button sysfs files; fall back to AI if unavailable. */
	int buttons_ok = (btns_open() == 0);
	if (buttons_ok)
		printf("Buttons: BTN0/BTN1=left paddle up/down  "
		       "BTN2/BTN3=right paddle up/down\n");
	else
		printf("No buttons found – AI vs AI mode\n");

	COL_BG      = rgb565(8,  8,  24);
	COL_LPAD    = 0x07FFu; /* cyan    */
	COL_RPAD    = 0xF81Fu; /* magenta */
	COL_BALL    = 0xFFE0u; /* yellow  */
	COL_MIDLINE = rgb565(35, 35, 55);

	lscore = rscore = 0;
	lpy = rpy = H / 2 - PAD_H / 2;
	serve(rand() & 1);

	draw_scene(&ctx);
	draw_hud(&ctx);
	flush(&ctx);

	while (lscore < WIN_SCORE && rscore < WIN_SCORE) {
		int bx, by;

		/* update ball position */
		bx_fp += bdx_fp;
		by_fp += bdy_fp;
		bx = bx_fp / FP;
		by = by_fp / FP;

		/* bounce off top/bottom walls */
		if (by - BALL_R < HUD_H) {
			by_fp = (HUD_H + BALL_R) * FP;
			bdy_fp = -bdy_fp;
		}
		if (by + BALL_R >= H) {
			by_fp = (H - BALL_R - 1) * FP;
			bdy_fp = -bdy_fp;
		}

		bx = bx_fp / FP;
		by = by_fp / FP;

		/* left paddle collision */
		if (bdx_fp < 0 &&
		    bx - BALL_R <= LPAD_X + PAD_W &&
		    bx - BALL_R >= LPAD_X &&
		    by >= lpy - BALL_R && by <= lpy + PAD_H + BALL_R) {
			bx_fp  = (LPAD_X + PAD_W + BALL_R + 1) * FP;
			bdx_fp = -bdx_fp + FP; /* small speed-up on each hit */
			/* deflect angle based on where ball hits paddle */
			bdy_fp = (by - (lpy + PAD_H / 2)) * FP / 6;
			if (bdy_fp == 0) bdy_fp = FP;
		}

		/* right paddle collision */
		if (bdx_fp > 0 &&
		    bx + BALL_R >= RPAD_X &&
		    bx + BALL_R <= RPAD_X + PAD_W &&
		    by >= rpy - BALL_R && by <= rpy + PAD_H + BALL_R) {
			bx_fp  = (RPAD_X - BALL_R - 1) * FP;
			bdx_fp = -bdx_fp - FP;
			bdy_fp = (by - (rpy + PAD_H / 2)) * FP / 6;
			if (bdy_fp == 0) bdy_fp = -FP;
		}

		/* cap speed so the game stays playable */
		if (bdx_fp >  10 * FP) bdx_fp =  10 * FP;
		if (bdx_fp < -10 * FP) bdx_fp = -10 * FP;
		if (bdy_fp >   7 * FP) bdy_fp =   7 * FP;
		if (bdy_fp <  -7 * FP) bdy_fp =  -7 * FP;

		/* scoring: ball exits left or right */
		if (bx_fp < 0) {
			rscore++;
			printf("Right scores! %d – %d\n", lscore, rscore);
			draw_scene(&ctx); draw_hud(&ctx); flush(&ctx);
			usleep(800000);
			fill_rect(&ctx, 0, HUD_H, W-1, H-1, COL_BG);
			lpy = rpy = H / 2 - PAD_H / 2;
			serve(0);
			continue;
		}
		if (bx_fp >= W * FP) {
			lscore++;
			printf("Left scores!  %d – %d\n", lscore, rscore);
			draw_scene(&ctx); draw_hud(&ctx); flush(&ctx);
			usleep(800000);
			fill_rect(&ctx, 0, HUD_H, W-1, H-1, COL_BG);
			lpy = rpy = H / 2 - PAD_H / 2;
			serve(1);
			continue;
		}

		/* Paddle movement: human via buttons or AI */
		if (buttons_ok) {
			human_move_paddle(&lpy, btn_pressed(0), btn_pressed(1));
			human_move_paddle(&rpy, btn_pressed(2), btn_pressed(3));
		} else {
			ai_move_paddle(&lpy, by_fp / FP);
			ai_move_paddle(&rpy, by_fp / FP);
		}

		draw_scene(&ctx);
		draw_hud(&ctx);
		flush(&ctx);
		usleep(33000); /* ~30 fps */
	}

	/* winner flash */
	{
		uint16_t win_col = (lscore >= WIN_SCORE) ? COL_LPAD : COL_RPAD;
		int i;
		printf("%s wins!\n", lscore >= WIN_SCORE ? "Papa Bear" : "Mama Bear");
		for (i = 0; i < 10; i++) {
			fill_rect(&ctx, 0, HUD_H, W-1, H-1, i & 1 ? COL_BG : win_col);
			flush(&ctx);
			usleep(300000);
		}
	}

	if (buttons_ok) btns_close();
	fb_close(&ctx);
	return 0;
}
