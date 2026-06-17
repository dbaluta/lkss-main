// SPDX-License-Identifier: GPL-2.0
/*
 * fb_demo.c – visual showcase for the ST7789 240×240 framebuffer
 *
 * Cycles through four animated scenes, each running for ~6 seconds:
 *
 *   1. Plasma      – overlapping colour waves computed per-pixel
 *   2. Starfield   – 150 stars expanding outward from the centre
 *   3. Bouncing balls – 8 coloured balls with trails
 *   4. Fire        – classic fire simulation with a colour palette
 *
 * No user input needed.  The demo loops indefinitely (Ctrl-C to exit).
 *
 * Cross-compile: aarch64-linux-gnu-gcc -O2 -o fb_demo fb_demo.c
 * Run on board:  ./fb_demo [/dev/fb0]
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

/* Fade all pixels toward black by shifting each channel right. */
static void fade(struct fb_ctx *ctx, int shift)
{
	uint32_t n = ctx->finfo.smem_len / 2;
	uint32_t i;
	for (i = 0; i < n; i++) {
		uint16_t p = ctx->buf[i];
		uint8_t r = (p >> 11) & 0x1F;
		uint8_t g = (p >>  5) & 0x3F;
		uint8_t b =  p        & 0x1F;
		r >>= shift; g >>= shift; b >>= shift;
		ctx->buf[i] = (r << 11) | (g << 5) | b;
	}
}

/* Map a value in [0,255] through a smooth colour-wheel hue. */
static uint16_t wheel(uint8_t pos)
{
	if (pos < 85)
		return rgb565(255 - pos * 3, pos * 3, 0);
	if (pos < 170) {
		pos -= 85;
		return rgb565(0, 255 - pos * 3, pos * 3);
	}
	pos -= 170;
	return rgb565(pos * 3, 0, 255 - pos * 3);
}

/* ── Scene 1: Plasma ─────────────────────────────────────────────────────── */
/*
 * Combines several overlapping periodic patterns (all integer arithmetic,
 * no libm needed) to produce a smoothly animating colour field.
 */
static void scene_plasma(struct fb_ctx *ctx, int frames)
{
	int f, x, y;
	for (f = 0; f < frames; f++) {
		uint32_t stride = ctx->finfo.line_length / 2;
		for (y = 0; y < 240; y++) {
			for (x = 0; x < 240; x++) {
				/*
				 * Four overlapping sine-like waves; integer
				 * masking with & 0xFF gives a periodic [0,255]
				 * range cheaply.
				 */
				int v = (((x + f)       & 0xFF) +
				         ((y + f / 2)   & 0xFF) +
				         ((x + y + f/3) & 0xFF) +
				         (((x-120)*(x-120)/32 +
				           (y-120)*(y-120)/32 + f) & 0xFF)) >> 2;
				ctx->buf[y * stride + x] = wheel((uint8_t)v);
			}
		}
		flush(ctx);
		usleep(50000); /* ~20 fps */
	}
}

/* ── Scene 2: Starfield ──────────────────────────────────────────────────── */
/*
 * 150 star particles stored as fixed-point offsets from the screen centre.
 * Each frame they move outward; when they leave the screen they wrap back
 * to a new random position near the centre.
 */
#define NSTARS 150

static void scene_stars(struct fb_ctx *ctx, int frames)
{
	/* Stars: position (×256 for sub-pixel), velocity */
	static int sx[NSTARS], sy[NSTARS], svx[NSTARS], svy[NSTARS];
	int i, f;

	/* Initialise randomly around centre */
	for (i = 0; i < NSTARS; i++) {
		sx[i]  = (rand() % 60 - 30) << 8;
		sy[i]  = (rand() % 60 - 30) << 8;
		svx[i] = (rand() % 5 + 1) * (sx[i] < 0 ? -1 : 1);
		svy[i] = (rand() % 5 + 1) * (sy[i] < 0 ? -1 : 1);
	}

	fill_rect(ctx, 0, 0, 239, 239, 0x0000u);

	for (f = 0; f < frames; f++) {
		fade(ctx, 1); /* trails */

		for (i = 0; i < NSTARS; i++) {
			int px, py;

			/* erase old position handled by fade */
			sx[i]  += svx[i];
			sy[i]  += svy[i];
			/* speed up as star moves away from centre */
			svx[i] += svx[i] > 0 ? 1 : -1;
			svy[i] += svy[i] > 0 ? 1 : -1;

			px = 120 + (sx[i] >> 8);
			py = 120 + (sy[i] >> 8);

			if (px < 0 || px > 239 || py < 0 || py > 239) {
				/* respawn near centre */
				int dx = rand() % 10 - 5;
				int dy = rand() % 10 - 5;
				if (dx == 0) dx = 1;
				if (dy == 0) dy = 1;
				sx[i]  = dx << 8;
				sy[i]  = dy << 8;
				svx[i] = dx > 0 ? 1 : -1;
				svy[i] = dy > 0 ? 1 : -1;
				continue;
			}

			/* brightness proportional to distance from centre */
			{
				int dist = abs(sx[i] >> 8) + abs(sy[i] >> 8);
				uint8_t bright = (uint8_t)(dist > 255 ? 255 : dist);
				uint16_t c = wheel((uint8_t)(i * 11 + f / 3));
				/* scale brightness */
				uint8_t r = (((c >> 11) & 0x1F) * bright) >> 8;
				uint8_t g = (((c >>  5) & 0x3F) * bright) >> 8;
				uint8_t b = ((c & 0x1F) * bright) >> 8;
				fill_rect(ctx, px - 1, py - 1, px + 1, py + 1,
				          rgb565(r << 3, g << 2, b << 3));
			}
		}

		flush(ctx);
		usleep(33000); /* ~30 fps */
	}
}

/* ── Scene 3: Bouncing balls ─────────────────────────────────────────────── */

#define NBALLS 8

static void scene_balls(struct fb_ctx *ctx, int frames)
{
	static int bx[NBALLS], by[NBALLS], bdx[NBALLS], bdy[NBALLS], br[NBALLS];
	static uint16_t bcol[NBALLS];
	int i, f;

	for (i = 0; i < NBALLS; i++) {
		br[i]   = 8 + rand() % 14;
		bx[i]   = br[i] + rand() % (240 - 2 * br[i]);
		by[i]   = br[i] + rand() % (240 - 2 * br[i]);
		bdx[i]  = (rand() % 5 + 2) * (rand() & 1 ? 1 : -1);
		bdy[i]  = (rand() % 5 + 2) * (rand() & 1 ? 1 : -1);
		bcol[i] = wheel((uint8_t)(i * 32));
	}

	fill_rect(ctx, 0, 0, 239, 239, 0x0000u);

	for (f = 0; f < frames; f++) {
		fade(ctx, 1); /* motion trail */

		for (i = 0; i < NBALLS; i++) {
			int r = br[i];

			bx[i] += bdx[i];
			by[i] += bdy[i];

			if (bx[i] - r < 0)   { bx[i] = r;       bdx[i] = -bdx[i]; }
			if (bx[i] + r > 239) { bx[i] = 239 - r; bdx[i] = -bdx[i]; }
			if (by[i] - r < 0)   { by[i] = r;        bdy[i] = -bdy[i]; }
			if (by[i] + r > 239) { by[i] = 239 - r;  bdy[i] = -bdy[i]; }

			/* Draw filled circle using Bresenham midpoint algorithm */
			{
				int cx = bx[i], cy = by[i];
				int ex = 0, ey = r, d = 3 - 2 * r;
				while (ey >= ex) {
					fill_rect(ctx, cx-ex, cy+ey, cx+ex, cy+ey, bcol[i]);
					fill_rect(ctx, cx-ex, cy-ey, cx+ex, cy-ey, bcol[i]);
					fill_rect(ctx, cx-ey, cy+ex, cx+ey, cy+ex, bcol[i]);
					fill_rect(ctx, cx-ey, cy-ex, cx+ey, cy-ex, bcol[i]);
					if (d < 0) d += 4 * ex + 6;
					else       { d += 4 * (ex - ey) + 10; ey--; }
					ex++;
				}
			}

			/* slowly rotate colour */
			bcol[i] = wheel((uint8_t)((bcol[i] >> 8) + 1));
		}

		flush(ctx);
		usleep(40000); /* ~25 fps */
	}
}

/* ── Scene 4: Fire ───────────────────────────────────────────────────────── */
/*
 * Classic fire effect: each row is computed by averaging three pixels in the
 * row below plus a cooling factor.  The bottom two rows are seeded with
 * random heat each frame.  A palette maps heat [0,255] to colours from
 * black → dark red → red → orange → yellow → white.
 */
static uint8_t  fire_buf[242][240]; /* [0..239]=display, [240..241]=heat source */
static uint16_t fire_pal[256];

static void fire_init_palette(void)
{
	int i;
	for (i = 0; i < 256; i++) {
		uint8_t r, g, b;
		if (i < 64) {
			r = i * 4; g = 0; b = 0;
		} else if (i < 128) {
			r = 255; g = (i - 64) * 4; b = 0;
		} else if (i < 192) {
			r = 255; g = 255; b = (i - 128) * 4;
		} else {
			r = 255; g = 255; b = 255;
		}
		fire_pal[i] = rgb565(r, g, b);
	}
}

static void fire_step(void)
{
	int x, y;
	/* seed bottom rows */
	for (x = 0; x < 240; x++) {
		fire_buf[240][x] = (uint8_t)(160 + rand() % 95);
		fire_buf[241][x] = (uint8_t)(160 + rand() % 95);
	}
	/* propagate upward with cooling */
	for (y = 0; y < 240; y++) {
		for (x = 0; x < 240; x++) {
			int v = ((int)fire_buf[y + 1][(x + 239) % 240] +
			         (int)fire_buf[y + 1][x] +
			         (int)fire_buf[y + 1][(x + 1)  % 240] +
			         (int)fire_buf[y + 2][x]) / 4;
			fire_buf[y][x] = (uint8_t)(v > 5 ? v - 5 : 0);
		}
	}
}

static void fire_draw(struct fb_ctx *ctx)
{
	uint32_t stride = ctx->finfo.line_length / 2;
	int x, y;
	/*
	 * fire_buf[0] is coolest (top); fire_buf[239] is hottest.
	 * Display upside-down so flames rise from the bottom of the screen.
	 */
	for (y = 0; y < 240; y++) {
		int dy = 239 - y;
		for (x = 0; x < 240; x++)
			ctx->buf[dy * stride + x] = fire_pal[fire_buf[y][x]];
	}
}

static void scene_fire(struct fb_ctx *ctx, int frames)
{
	int f;
	fire_init_palette();
	memset(fire_buf, 0, sizeof(fire_buf));

	/* warm up the simulation before displaying */
	for (f = 0; f < 40; f++)
		fire_step();

	for (f = 0; f < frames; f++) {
		fire_step();
		fire_draw(ctx);
		flush(ctx);
		usleep(50000); /* ~20 fps */
	}
}

/* ── Scene transition: fade to black ────────────────────────────────────── */

static void fade_to_black(struct fb_ctx *ctx)
{
	int i;
	for (i = 0; i < 8; i++) {
		fade(ctx, 1);
		flush(ctx);
		usleep(40000);
	}
	fill_rect(ctx, 0, 0, 239, 239, 0x0000u);
	flush(ctx);
}

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
	fill_rect(&ctx, 0, 0, 239, 239, 0x0000u);
	flush(&ctx);

	/* Loop indefinitely through all four scenes */
	while (1) {
		printf("Scene: plasma\n");
		scene_plasma(&ctx, 120);  /* 120 × 50 ms ≈ 6 s */
		fade_to_black(&ctx);

		printf("Scene: starfield\n");
		scene_stars(&ctx, 180);   /* 180 × 33 ms ≈ 6 s */
		fade_to_black(&ctx);

		printf("Scene: bouncing balls\n");
		scene_balls(&ctx, 150);   /* 150 × 40 ms ≈ 6 s */
		fade_to_black(&ctx);

		printf("Scene: fire\n");
		scene_fire(&ctx, 120);    /* 120 × 50 ms ≈ 6 s */
		fade_to_black(&ctx);
	}

	fb_close(&ctx); /* unreachable; here for completeness */
	return 0;
}
