.. _day3_human:

Day 3: SPI Bus and the ST7789 display
======================================

**Topics**

- SPI bus fundamentals: signals, clock polarity/phase (CPOL/CPHA), chip-select, SPI modes
- SPI busses on the i.MX93 FRDM board
- ST7789 TFT controller: architecture, pin description, command/data protocol
- ST7789 initialization sequence and essential commands
- Writing a minimal SPI driver from scratch
- Drawing primitives: fill, fill_rect, draw_pixel, draw_line, draw_circle
- Device tree for SPI node and the ST7789 child node

**Goal**

By the end of this lab you will be able to:

- Explain the SPI protocol and [identify the mode used by the ST7789 display]
- Wire the ST7789 display module to the i.MX93 FRDM EXT connector
- Write and build a complete SPI driver that initializes the display
- Implement graphical primitives (fill, rectangle, pixel, line, circle) from scratch
- Write the device tree node that binds the driver to the hardware
