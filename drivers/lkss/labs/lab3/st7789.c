// SPDX-License-Identifier: GPL-2.0
/*
 * st7789.c - LKSS Lab 3: Minimal SPI driver for the ST7789 240x240 TFT display
 *
 * This driver is deliberately simple and educational.  It does not use the
 * fbtft staging framework.  Every primitive is written from scratch so that
 * students understand exactly what goes over the SPI bus.
 *
 * Hardware connections on the i.MX93 FRDM EXT2 header (J601):
 *
 *   Display pin  J601 pin  SoC pad      Notes
 *   -----------  --------  -----------  ------------------------------------
 *   SDA (MOSI)   19        GPIO_IO10    LPSPI3_SOUT
 *   SCL (SCK)    23        GPIO_IO11    LPSPI3_SCK
 *   CS           GND       --           Module CS tied low (always selected)
 *   RES (RST)    32        GPIO_IO12    Active-low hardware reset
 *   DC  (D/C)    7         GPIO_IO04    Low = command, High = data
 *   VCC          1         3.3 V        Logic and panel supply
 *   GND          6         GND
 *   BLK          1         3.3 V        Backlight (tie high for always-on)
 *
 * Device Tree compatible string: "lkss,st7789"
 *
 * Cross-compile:
 *   make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- \
 *        M=drivers/lkss/labs/lab3 -j$(nproc)
 *
 * Lab structure (10 TODOs):
 *   TODO 1  - Low-level SPI primitives: write_cmd and write_data
 *   TODO 2  - Hardware reset
 *   TODO 3  - Initialization sequence
 *   TODO 4  - Address window (CASET/RASET/RAMWR) and full-screen fill
 *   TODO 5  - Filled rectangle
 *   TODO 6  - Single pixel write
 *   TODO 7  - Bresenham line drawing
 *   TODO 8  - Midpoint circle outline
 *   TODO 9  - Filled circle
 *   TODO 10 - Demo pattern (ties all primitives together)
 */

#include <linux/module.h>
#include <linux/spi/spi.h>
#include <linux/gpio/consumer.h>
#include <linux/delay.h>
#include <linux/kernel.h>   /* abs(), int_sqrt() */
#include <linux/slab.h>     /* kmalloc(), kfree() */
#include <linux/of.h>

/* ------------------------------------------------------------------
 * ST7789 command opcodes (ST7789VW datasheet, chapter 9)
 * ------------------------------------------------------------------ */
#define ST7789_NOP       0x00  /* no-operation                        */
#define ST7789_SWRESET   0x01  /* software reset                      */
#define ST7789_SLPOUT    0x11  /* sleep out                           */
#define ST7789_NORON     0x13  /* normal display mode on              */
#define ST7789_INVOFF    0x20  /* display inversion off               */
#define ST7789_INVON     0x21  /* display inversion on                */
#define ST7789_DISPOFF   0x28  /* display off                         */
#define ST7789_DISPON    0x29  /* display on                          */
#define ST7789_CASET     0x2A  /* column address set                  */
#define ST7789_RASET     0x2B  /* row address set                     */
#define ST7789_RAMWR     0x2C  /* memory write                        */
#define ST7789_MADCTL    0x36  /* memory data access control          */
#define ST7789_COLMOD    0x3A  /* interface pixel format              */

/* COLMOD parameter: 16 bits per pixel, RGB 5-6-5 encoding */
#define ST7789_COLMOD_RGB565  0x55

/*
 * MADCTL parameter: normal scan order, RGB (not BGR) color channel order,
 * no mirroring.  Change bits 6-7 to rotate the display:
 *   0x00 = 0 deg, 0x60 = 90 deg, 0xC0 = 180 deg, 0xA0 = 270 deg
 */
#define ST7789_MADCTL_NORMAL  0x00

/* Panel dimensions for the 240x240 module used in this lab */
#define ST7789_WIDTH   240
#define ST7789_HEIGHT  240

/* ------------------------------------------------------------------
 * Driver private state
 *
 * One instance is allocated per bound device in probe() and stored via
 * spi_set_drvdata() so that all functions can retrieve it.
 * ------------------------------------------------------------------ */
struct st7789_priv {
	struct spi_device  *spi;    /* back-pointer to the SPI device  */
	struct gpio_desc   *dc;     /* D/C (data/command select) GPIO  */
	struct gpio_desc   *reset;  /* hardware reset GPIO             */
	u16                 width;  /* panel width  in pixels          */
	u16                 height; /* panel height in pixels          */
};

/* ==================================================================
 * TODO 1 - Low-level SPI primitives
 *
 * Implement the two building-block functions that all higher-level
 * drawing code relies on:
 *
 *   st7789_write_cmd()       - set D/C LOW then clock one command byte
 *   st7789_write_data()      - set D/C HIGH then clock N data bytes
 *   st7789_write_data_byte() - convenience wrapper for a single byte
 *
 * Key rules:
 *   - D/C must be stable for the ENTIRE byte transfer (ST7789 samples
 *     D/C at the start of each byte).
 *   - Use spi_write(priv->spi, buf, len) for the actual transfer.
 *   - Return the error code from spi_write() unchanged.
 * ================================================================== */

/**
 * st7789_write_cmd - send a single command byte to the ST7789
 * @priv: driver private data
 * @cmd:  command opcode (one of the ST7789_* defines above)
 *
 * Drive D/C LOW before clocking the byte so the controller treats it
 * as a command opcode, not a data parameter.
 *
 * Returns 0 on success or a negative errno on failure.
 */
static int st7789_write_cmd(struct st7789_priv *priv, u8 cmd)
{
	/* TODO 1a: drive D/C low (command mode) then call spi_write().
	 *
	 *   gpiod_set_value(priv->dc, 0);
	 *   return spi_write(priv->spi, &cmd, 1);
	 */
	return 0; /* remove this line when TODO 1a is implemented */
}

/**
 * st7789_write_data - send one or more data bytes to the ST7789
 * @priv: driver private data
 * @buf:  byte buffer to transmit
 * @len:  number of bytes
 *
 * Drive D/C HIGH before clocking the bytes so the controller treats
 * them as data parameters or pixel data.
 *
 * Returns 0 on success or a negative errno on failure.
 */
static int st7789_write_data(struct st7789_priv *priv,
			     const u8 *buf, size_t len)
{
	/* TODO 1b: drive D/C high (data mode) then call spi_write().
	 *
	 *   gpiod_set_value(priv->dc, 1);
	 *   return spi_write(priv->spi, buf, len);
	 */
	return 0; /* remove this line when TODO 1b is implemented */
}

/**
 * st7789_write_data_byte - send a single data byte (convenience wrapper)
 * @priv: driver private data
 * @byte: the data byte to send
 */
static inline int st7789_write_data_byte(struct st7789_priv *priv, u8 byte)
{
	/* TODO 1c: call st7789_write_data() with a pointer to 'byte' and
	 * length 1.
	 *
	 *   return st7789_write_data(priv, &byte, 1);
	 */
	return 0; /* remove this line when TODO 1c is implemented */
}

/* ==================================================================
 * TODO 2 - Hardware reset
 *
 * The ST7789 must be reset before any command is accepted.
 * Reset sequence (datasheet section 8.16):
 *   1. Assert RESX LOW for at least 15 ms.
 *   2. Deassert RESX HIGH.
 *   3. Wait at least 120 ms before the first command.
 *
 * Note: priv->reset was obtained with GPIOD_OUT_HIGH so logical value 1
 * means "assert the active-low reset line" (the gpiod layer inverts).
 * ================================================================== */

/**
 * st7789_hw_reset - perform a hardware reset of the ST7789
 * @priv: driver private data
 *
 * After returning the controller is in its power-on default state and
 * ready to accept the initialization command sequence.
 */
static void st7789_hw_reset(struct st7789_priv *priv)
{
	/* TODO 2: assert reset, wait 15 ms, deassert, wait 120 ms.
	 *
	 *   gpiod_set_value(priv->reset, 1);   // assert (active-low)
	 *   msleep(15);
	 *   gpiod_set_value(priv->reset, 0);   // deassert
	 *   msleep(120);
	 */
}

/* ==================================================================
 * TODO 3 - Initialization sequence
 *
 * After hardware reset the controller is in sleep mode with an
 * undefined pixel format.  Send the sequence below to bring it up:
 *
 *   SWRESET (0x01)           -- software reset; wait >= 150 ms
 *   SLPOUT  (0x11)           -- exit sleep mode; wait >= 500 ms
 *   COLMOD  (0x3A) + 0x55    -- RGB565 pixel format (16 bpp)
 *   MADCTL  (0x36) + 0x00    -- normal scan, RGB color order
 *   INVON   (0x21)           -- inversion on (required for most modules)
 *   NORON   (0x13)           -- normal display mode (no partial)
 *   DISPON  (0x29)           -- display on; wait >= 100 ms
 *
 * Check every return value and propagate errors.
 * ================================================================== */

/**
 * st7789_init_display - send the full initialization command sequence
 * @priv: driver private data
 *
 * Returns 0 on success, negative errno on the first SPI error.
 */
static int st7789_init_display(struct st7789_priv *priv)
{
	int ret;

	/* TODO 3: implement the initialization sequence described above.
	 *
	 * Template:
	 *
	 *   ret = st7789_write_cmd(priv, ST7789_SWRESET);
	 *   if (ret) return ret;
	 *   msleep(150);
	 *
	 *   ret = st7789_write_cmd(priv, ST7789_SLPOUT);
	 *   if (ret) return ret;
	 *   msleep(500);
	 *
	 *   ret = st7789_write_cmd(priv, ST7789_COLMOD);
	 *   if (ret) return ret;
	 *   ret = st7789_write_data_byte(priv, ST7789_COLMOD_RGB565);
	 *   if (ret) return ret;
	 *
	 *   ret = st7789_write_cmd(priv, ST7789_MADCTL);
	 *   if (ret) return ret;
	 *   ret = st7789_write_data_byte(priv, ST7789_MADCTL_NORMAL);
	 *   if (ret) return ret;
	 *
	 *   ret = st7789_write_cmd(priv, ST7789_INVON);
	 *   if (ret) return ret;
	 *
	 *   ret = st7789_write_cmd(priv, ST7789_NORON);
	 *   if (ret) return ret;
	 *
	 *   ret = st7789_write_cmd(priv, ST7789_DISPON);
	 *   if (ret) return ret;
	 *   msleep(100);
	 *
	 *   return 0;
	 */

	/* Silence the unused-variable warning until TODO 3 is done. */
	(void)ret;
	return 0;
}

/* ==================================================================
 * TODO 4 - Address window and full-screen fill
 *
 * To write pixels the controller needs an active drawing window.
 * Set it with CASET (column start/end) and RASET (row start/end),
 * then open the pixel stream with RAMWR.  The controller advances
 * its internal write pointer automatically after each pixel.
 *
 * CASET / RASET encoding: four bytes, each pair is a 16-bit big-endian
 * value.  Example for x0=0, x1=239:
 *   col[4] = { 0x00, 0x00, 0x00, 0xEF }
 *
 * st7789_fill() should:
 *   - call st7789_set_addr_win() for the full panel
 *   - allocate one scanline buffer (width * 2 bytes, RGB565 big-endian)
 *   - fill the buffer with the repeated color
 *   - loop over all height rows, sending the buffer each time
 *   - free the buffer and return
 * ================================================================== */

/**
 * st7789_set_addr_win - define the active rectangular drawing window
 * @priv:     driver private data
 * @x0, y0:  top-left corner (inclusive, 0-based)
 * @x1, y1:  bottom-right corner (inclusive)
 *
 * Sends CASET, RASET, then RAMWR to open the pixel data stream.
 * Returns 0 on success or a negative errno on failure.
 */
static int st7789_set_addr_win(struct st7789_priv *priv,
			       u16 x0, u16 y0, u16 x1, u16 y1)
{
	/* TODO 4a: build col[] and row[] arrays, send CASET + col,
	 * RASET + row, then RAMWR.
	 *
	 *   u8 col[4] = { x0 >> 8, x0 & 0xff, x1 >> 8, x1 & 0xff };
	 *   u8 row[4] = { y0 >> 8, y0 & 0xff, y1 >> 8, y1 & 0xff };
	 *   int ret;
	 *
	 *   ret = st7789_write_cmd(priv, ST7789_CASET);
	 *   if (ret) return ret;
	 *   ret = st7789_write_data(priv, col, 4);
	 *   if (ret) return ret;
	 *
	 *   ret = st7789_write_cmd(priv, ST7789_RASET);
	 *   if (ret) return ret;
	 *   ret = st7789_write_data(priv, row, 4);
	 *   if (ret) return ret;
	 *
	 *   return st7789_write_cmd(priv, ST7789_RAMWR);
	 */
	return 0; /* remove when TODO 4a is done */
}

/**
 * st7789_fill - fill the entire display with one RGB565 color
 * @priv:  driver private data
 * @color: RGB565 value (e.g. 0xF800=red, 0x07E0=green, 0x001F=blue,
 *         0xFFFF=white, 0x0000=black)
 *
 * Allocates a single scanline buffer and sends it once per row.
 * Returns 0 on success or a negative errno on failure.
 */
static int st7789_fill(struct st7789_priv *priv, u16 color)
{
	/* TODO 4b: implement full-screen fill.
	 *
	 * Key points:
	 *   - RGB565 on the wire is big-endian: byte0 = color >> 8,
	 *     byte1 = color & 0xff.
	 *   - Reuse one scanline buffer (priv->width * 2 bytes) for all rows.
	 *   - GFP_KERNEL is valid here because probe() runs in process context.
	 *
	 *   u8 color_hi = color >> 8;
	 *   u8 color_lo = color & 0xff;
	 *   u8 *line;
	 *   int ret, x, y;
	 *
	 *   ret = st7789_set_addr_win(priv, 0, 0,
	 *                             priv->width - 1, priv->height - 1);
	 *   if (ret) return ret;
	 *
	 *   line = kmalloc(priv->width * 2, GFP_KERNEL);
	 *   if (!line) return -ENOMEM;
	 *
	 *   for (x = 0; x < priv->width; x++) {
	 *       line[x * 2]     = color_hi;
	 *       line[x * 2 + 1] = color_lo;
	 *   }
	 *
	 *   for (y = 0; y < priv->height; y++) {
	 *       ret = st7789_write_data(priv, line, priv->width * 2);
	 *       if (ret) break;
	 *   }
	 *
	 *   kfree(line);
	 *   return ret;
	 */
	return 0; /* remove when TODO 4b is done */
}

/* ==================================================================
 * TODO 5 - Filled rectangle
 *
 * st7789_fill_rect() is the fundamental drawing primitive.  All shapes
 * (borders, bars, backgrounds) are built from it.
 *
 * Steps:
 *   1. Clamp (x, y, w, h) to the panel boundaries so callers don't
 *      need to worry about out-of-bounds coordinates.
 *   2. Call st7789_set_addr_win(x, y, x+w-1, y+h-1).
 *   3. Allocate one row buffer (w * 2 bytes) and fill it with the color.
 *   4. Send the buffer h times with st7789_write_data().
 *   5. Free the buffer.
 * ================================================================== */

/**
 * st7789_fill_rect - draw a filled rectangle
 * @priv:  driver private data
 * @x:     left column (0-based)
 * @y:     top row (0-based)
 * @w:     width in pixels
 * @h:     height in pixels
 * @color: RGB565 fill color
 *
 * Returns 0 on success or a negative errno on failure.
 */
static int st7789_fill_rect(struct st7789_priv *priv,
			    u16 x, u16 y, u16 w, u16 h, u16 color)
{
	/* TODO 5: implement the filled rectangle.
	 *
	 *   u8 color_hi = color >> 8;
	 *   u8 color_lo = color & 0xff;
	 *   u8 *line;
	 *   int ret = 0;
	 *   u16 i, row;
	 *
	 *   // Clamp to panel size
	 *   if (x >= priv->width || y >= priv->height) return 0;
	 *   if (x + w > priv->width)  w = priv->width  - x;
	 *   if (y + h > priv->height) h = priv->height - y;
	 *
	 *   ret = st7789_set_addr_win(priv, x, y, x + w - 1, y + h - 1);
	 *   if (ret) return ret;
	 *
	 *   line = kmalloc(w * 2, GFP_KERNEL);
	 *   if (!line) return -ENOMEM;
	 *
	 *   for (i = 0; i < w; i++) {
	 *       line[i * 2]     = color_hi;
	 *       line[i * 2 + 1] = color_lo;
	 *   }
	 *
	 *   for (row = 0; row < h; row++) {
	 *       ret = st7789_write_data(priv, line, w * 2);
	 *       if (ret) break;
	 *   }
	 *
	 *   kfree(line);
	 *   return ret;
	 */
	return 0; /* remove when TODO 5 is done */
}

/* ==================================================================
 * TODO 6 - Single pixel write
 *
 * A pixel is a 1x1 window.  Set the address window to (x,y)-(x,y)
 * and send the two RGB565 bytes.
 *
 * Hint: check bounds first (return 0 silently if out of range).
 * ================================================================== */

/**
 * st7789_draw_pixel - set one pixel to the given color
 * @priv:  driver private data
 * @x:     column (0-based)
 * @y:     row (0-based)
 * @color: RGB565 color
 *
 * Returns 0 on success or a negative errno on failure.
 */
static int st7789_draw_pixel(struct st7789_priv *priv,
			     u16 x, u16 y, u16 color)
{
	/* TODO 6: implement single-pixel write.
	 *
	 *   u8 pixel[2] = { color >> 8, color & 0xff };
	 *   int ret;
	 *
	 *   if (x >= priv->width || y >= priv->height) return 0;
	 *
	 *   ret = st7789_set_addr_win(priv, x, y, x, y);
	 *   if (ret) return ret;
	 *
	 *   return st7789_write_data(priv, pixel, 2);
	 */
	return 0; /* remove when TODO 6 is done */
}

/* ==================================================================
 * TODO 7 - Bresenham line drawing
 *
 * The Bresenham algorithm draws a straight line between two arbitrary
 * endpoints using only integer addition and comparison -- no division,
 * no floating point.
 *
 * Algorithm state:
 *   dx  = abs(x1 - x0)          (horizontal span, always positive)
 *   dy  = -abs(y1 - y0)         (vertical span, negated for the error term)
 *   sx  = (x0 < x1) ? 1 : -1   (x step direction)
 *   sy  = (y0 < y1) ? 1 : -1   (y step direction)
 *   err = dx + dy               (error accumulator)
 *
 * Each iteration:
 *   1. Plot (x0, y0).
 *   2. If (x0 == x1 && y0 == y1) break.
 *   3. e2 = 2 * err
 *   4. If e2 >= dy: err += dy; x0 += sx
 *   5. If e2 <= dx: err += dx; y0 += sy
 *
 * Reference: Bresenham, J.E. (1965). IBM Systems Journal 4(1): 25-30.
 * ================================================================== */

/**
 * st7789_draw_line - draw a line between two arbitrary points
 * @priv:       driver private data
 * @x0, y0:    start point
 * @x1, y1:    end point
 * @color:      RGB565 color
 *
 * Returns 0 on success or a negative errno from st7789_draw_pixel().
 */
static int st7789_draw_line(struct st7789_priv *priv,
			    int x0, int y0, int x1, int y1, u16 color)
{
	/* TODO 7: implement Bresenham's line algorithm.
	 *
	 *   int dx  =  abs(x1 - x0);
	 *   int dy  = -abs(y1 - y0);
	 *   int sx  = (x0 < x1) ? 1 : -1;
	 *   int sy  = (y0 < y1) ? 1 : -1;
	 *   int err = dx + dy;
	 *   int e2, ret;
	 *
	 *   for (;;) {
	 *       ret = st7789_draw_pixel(priv, (u16)x0, (u16)y0, color);
	 *       if (ret) return ret;
	 *
	 *       if (x0 == x1 && y0 == y1) break;
	 *
	 *       e2 = 2 * err;
	 *       if (e2 >= dy) { if (x0 == x1) break; err += dy; x0 += sx; }
	 *       if (e2 <= dx) { if (y0 == y1) break; err += dx; y0 += sy; }
	 *   }
	 *   return 0;
	 */
	return 0; /* remove when TODO 7 is done */
}

/* ==================================================================
 * TODO 8 - Midpoint circle outline
 *
 * The midpoint (Bresenham) circle algorithm exploits 8-fold symmetry:
 * for each step (x, y) it plots 8 symmetric pixels around the center.
 *
 * Algorithm state:
 *   x = 0, y = r, d = 1 - r
 *
 * Each iteration while x <= y:
 *   Plot the 8 symmetric points.
 *   if d < 0:  d += 2*x + 3
 *   else:      d += 2*(x - y) + 5; y--
 *   x++
 * ================================================================== */

/**
 * st7789_draw_circle - draw the outline of a circle
 * @priv:    driver private data
 * @cx, cy:  center coordinates
 * @r:       radius in pixels
 * @color:   RGB565 color
 *
 * Returns 0 on success or a negative errno on failure.
 */
static int st7789_draw_circle(struct st7789_priv *priv,
			      int cx, int cy, int r, u16 color)
{
	/* TODO 8: implement the midpoint circle algorithm.
	 *
	 *   int x = 0, y = r, d = 1 - r, ret;
	 *
	 *   #define PLOT(px, py) do { \
	 *       ret = st7789_draw_pixel(priv, (u16)(px), (u16)(py), color); \
	 *       if (ret) return ret; \
	 *   } while (0)
	 *
	 *   while (x <= y) {
	 *       PLOT(cx + x, cy + y); PLOT(cx - x, cy + y);
	 *       PLOT(cx + x, cy - y); PLOT(cx - x, cy - y);
	 *       PLOT(cx + y, cy + x); PLOT(cx - y, cy + x);
	 *       PLOT(cx + y, cy - x); PLOT(cx - y, cy - x);
	 *
	 *       if (d < 0) {
	 *           d += 2 * x + 3;
	 *       } else {
	 *           d += 2 * (x - y) + 5;
	 *           y--;
	 *       }
	 *       x++;
	 *   }
	 *   #undef PLOT
	 *   return 0;
	 */
	return 0; /* remove when TODO 8 is done */
}

/* ==================================================================
 * TODO 9 - Filled circle
 *
 * Fill a circle by iterating over every row within the bounding box
 * and drawing a horizontal filled rectangle (chord) for each row.
 *
 * For a circle of radius r centered at (cx, cy):
 *   for dy in [-r, r]:
 *     dx = (int)int_sqrt(r*r - dy*dy)
 *     fill_rect(cx - dx, cy + dy, 2*dx + 1, 1, color)
 *
 * int_sqrt() is the kernel integer square root from <linux/kernel.h>.
 * ================================================================== */

/**
 * st7789_fill_circle - draw a filled circle
 * @priv:    driver private data
 * @cx, cy:  center coordinates
 * @r:       radius in pixels
 * @color:   RGB565 fill color
 *
 * Returns 0 on success or a negative errno on failure.
 */
static int st7789_fill_circle(struct st7789_priv *priv,
			      int cx, int cy, int r, u16 color)
{
	/* TODO 9: implement the filled circle using horizontal chords.
	 *
	 *   int dy, dx, ret;
	 *
	 *   for (dy = -r; dy <= r; dy++) {
	 *       dx = (int)int_sqrt((u32)(r * r - dy * dy));
	 *       ret = st7789_fill_rect(priv,
	 *                               (u16)(cx - dx), (u16)(cy + dy),
	 *                               (u16)(2 * dx + 1), 1, color);
	 *       if (ret) return ret;
	 *   }
	 *   return 0;
	 */
	return 0; /* remove when TODO 9 is done */
}

/* ==================================================================
 * TODO 10 - Demo pattern
 *
 * Draw a composite test image that exercises every primitive:
 *
 *   Step 1: st7789_fill()        - black background
 *   Step 2: st7789_fill_rect()   - red 4-pixel border (top/bottom/left/right)
 *   Step 3: st7789_fill_circle() - green filled circle, center (60,60), r=50
 *   Step 4: st7789_fill_rect()   - blue rectangle (130,130), 100x100
 *   Step 5: st7789_draw_line()   - white diagonal from (5,5) to (234,234)
 *   Step 6: st7789_draw_circle() - yellow circle outline, center (120,120), r=40
 *
 * Return the error code from the first failing call, 0 on success.
 * ================================================================== */

/**
 * st7789_demo - draw the test pattern that exercises all primitives
 * @priv: driver private data
 *
 * Returns 0 on success or a negative errno on failure.
 */
static int st7789_demo(struct st7789_priv *priv)
{
	int ret;

	/* TODO 10: implement the six-step demo pattern described above.
	 *
	 *   // Step 1: black background
	 *   ret = st7789_fill(priv, 0x0000);
	 *   if (ret) return ret;
	 *
	 *   // Step 2: red border, 4 pixels thick on all four sides
	 *   st7789_fill_rect(priv,   0,   0, 240,   4, 0xF800);
	 *   st7789_fill_rect(priv,   0, 236, 240,   4, 0xF800);
	 *   st7789_fill_rect(priv,   0,   0,   4, 240, 0xF800);
	 *   st7789_fill_rect(priv, 236,   0,   4, 240, 0xF800);
	 *
	 *   // Step 3: green filled circle
	 *   ret = st7789_fill_circle(priv, 60, 60, 50, 0x07E0);
	 *   if (ret) return ret;
	 *
	 *   // Step 4: blue rectangle
	 *   ret = st7789_fill_rect(priv, 130, 130, 100, 100, 0x001F);
	 *   if (ret) return ret;
	 *
	 *   // Step 5: white diagonal
	 *   ret = st7789_draw_line(priv, 5, 5, 234, 234, 0xFFFF);
	 *   if (ret) return ret;
	 *
	 *   // Step 6: yellow circle outline
	 *   ret = st7789_draw_circle(priv, 120, 120, 40, 0xFFE0);
	 *   if (ret) return ret;
	 *
	 *   return 0;
	 */

	(void)ret;
	return 0; /* remove when TODO 10 is done */
}

/* ------------------------------------------------------------------
 * SPI driver probe and remove
 * ------------------------------------------------------------------ */

/**
 * st7789_probe - called by the SPI core when the DT compatible matches
 * @spi: SPI device instance created from the device tree node
 *
 * Sequence:
 *   1. Configure SPI mode and speed; call spi_setup().
 *   2. Allocate and initialise driver private state.
 *   3. Obtain GPIO descriptors for RST and D/C from the DT.
 *   4. Hardware-reset the panel.
 *   5. Send the initialization command sequence.
 *   6. Draw the demo pattern.
 *
 * Returns 0 on success or a negative errno on failure.
 * All devm_* resources are freed automatically on removal.
 */
static int st7789_probe(struct spi_device *spi)
{
	struct st7789_priv *priv;
	int ret;

	/*
	 * Step 1: configure SPI controller parameters.
	 *
	 * ST7789 uses Mode 0 (CPOL=0, CPHA=0): clock idles low, data
	 * sampled on the rising edge.  We set it explicitly even though the
	 * device tree does not carry spi-cpol/spi-cpha properties.
	 */
	spi->mode = SPI_MODE_0;
	ret = spi_setup(spi);
	if (ret < 0) {
		dev_err(&spi->dev, "spi_setup() failed: %d\n", ret);
		return ret;
	}

	dev_info(&spi->dev, "ST7789 probe: speed=%u Hz mode=0x%02x\n",
		 spi->max_speed_hz, spi->mode);

	/*
	 * Step 2: allocate driver private state.
	 *
	 * devm_kzalloc() ties the allocation to the device lifetime; it is
	 * freed automatically when the device is unbound -- no need to call
	 * kfree() in remove().
	 */
	priv = devm_kzalloc(&spi->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->spi    = spi;
	priv->width  = ST7789_WIDTH;
	priv->height = ST7789_HEIGHT;
	spi_set_drvdata(spi, priv);

	/*
	 * Step 3: obtain GPIO descriptors.
	 *
	 * devm_gpiod_get() looks up the property "<con_id>-gpios" in the
	 * device tree node.  For "reset" it reads "reset-gpios"; for "dc"
	 * it reads "dc-gpios".
	 *
	 * GPIOD_OUT_HIGH: initialise the output HIGH.
	 *   - For RST (active-low): HIGH = deasserted = not in reset.
	 *   - For D/C: we will set the correct level before each transfer.
	 */
	priv->reset = devm_gpiod_get(&spi->dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(priv->reset)) {
		dev_err(&spi->dev, "failed to get reset GPIO: %ld\n",
			PTR_ERR(priv->reset));
		return PTR_ERR(priv->reset);
	}

	priv->dc = devm_gpiod_get(&spi->dev, "dc", GPIOD_OUT_LOW);
	if (IS_ERR(priv->dc)) {
		dev_err(&spi->dev, "failed to get D/C GPIO: %ld\n",
			PTR_ERR(priv->dc));
		return PTR_ERR(priv->dc);
	}

	/* Step 4: hardware reset */
	st7789_hw_reset(priv);

	/* Step 5: initialization sequence */
	ret = st7789_init_display(priv);
	if (ret) {
		dev_err(&spi->dev, "display init failed: %d\n", ret);
		return ret;
	}

	/* Step 6: demo pattern */
	ret = st7789_demo(priv);
	if (ret) {
		dev_err(&spi->dev, "demo pattern failed: %d\n", ret);
		return ret;
	}

	dev_info(&spi->dev, "ST7789 240x240 initialized successfully\n");
	return 0;
}

/**
 * st7789_remove - called when the driver is unloaded or the device removed
 * @spi: the SPI device instance
 *
 * Turns off the display before releasing resources.  All devm_ resources
 * (GPIO descriptors, private memory) are freed by the device framework
 * after this function returns.
 */
static void st7789_remove(struct spi_device *spi)
{
	struct st7789_priv *priv = spi_get_drvdata(spi);

	/* Blank the display before shutting down */
	st7789_write_cmd(priv, ST7789_DISPOFF);

	dev_info(&spi->dev, "ST7789 removed\n");
}

/* ------------------------------------------------------------------
 * Device-tree and SPI matching tables
 * ------------------------------------------------------------------ */

/*
 * of_device_id table: the SPI core compares the "compatible" property of
 * each unbound SPI device against every entry here.  A match triggers
 * probe().
 */
static const struct of_device_id st7789_of_match[] = {
	{ .compatible = "lkss,st7789" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, st7789_of_match);

/*
 * spi_device_id table: used for non-DT (legacy board-file) matching and
 * required by module_spi_driver() even when DT is the primary mechanism.
 */
static const struct spi_device_id st7789_spi_ids[] = {
	{ "st7789", 0 },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(spi, st7789_spi_ids);

/* spi_driver registration record */
static struct spi_driver st7789_driver = {
	.driver = {
		.name           = "st7789",
		.of_match_table = st7789_of_match,
	},
	.probe    = st7789_probe,
	.remove   = st7789_remove,
	.id_table = st7789_spi_ids,
};

/*
 * module_spi_driver() generates the module_init() and module_exit()
 * functions that call spi_register_driver() and spi_unregister_driver()
 * respectively.  It replaces the boilerplate init/exit pair.
 */
module_spi_driver(st7789_driver);

MODULE_AUTHOR("LKSS Lab Team");
MODULE_DESCRIPTION("LKSS Lab 3: Minimal ST7789 SPI display driver");
MODULE_LICENSE("GPL v2");
