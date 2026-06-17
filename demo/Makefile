# SPDX-License-Identifier: GPL-2.0
# Demo programs for the ST7789 240×240 framebuffer
#
# Cross-compile for the target board:
#   make
#
# Native compile (for testing on x86 with a framebuffer):
#   make CC=gcc
#
# Copy all binaries to the board:
#   python3 ../scripts/lkss.py copy fb_test snake_auto pong weather fb_demo /root/

CC      := aarch64-linux-gnu-gcc
CFLAGS  := -O2 -Wall -Wextra

PROGS      := fb_test snake_auto pong weather fb_demo watch sensors
PROGS_ST7789 := fb_test_st7789 fb_test_st7789_skel \
                snake_auto_st7789 pong_st7789 weather_st7789 \
                fb_demo_st7789 watch_st7789 sensors_st7789

all: $(PROGS) $(PROGS_ST7789)

%: %.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f $(PROGS) $(PROGS_ST7789)

.PHONY: all clean
