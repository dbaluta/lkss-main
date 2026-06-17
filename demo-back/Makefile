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

PROGS   := fb_test snake_auto pong weather fb_demo watch

all: $(PROGS)

%: %.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f $(PROGS)

.PHONY: all clean
