.. _day3:
.. _spi-and-the-st7789-display-driver:

Day 3 – SPI Bus and the ST7789 Display Driver
==============================================

Slides: `Day 3 – SPI Bus and the ST7789 Display Driver`_

**Topics**

- SPI protocol: clock polarity/phase (CPOL/CPHA), chip-select, SPI modes 0–3
- Linux SPI subsystem: ``spi_controller``, ``spi_device``, ``spi_transfer``,
  ``spi_message``
- The staging ``fbtft`` framework: what it provides and why we use it as a reference
- The ``fb_st7789v`` staging driver: architecture, init sequence, pixel write path
- Writing the ST7789 primitive layer from scratch: reset, command/data, fill, rect
- Device tree deep-dive: SPI controller nodes, device child nodes, ``pinctrl``
- ST7789 TFT controller: command/data (D/C) pin protocol, initialization sequence
- Pixel formats: RGB565 on the wire, RGB888 in memory
- Linux framebuffer (``fbdev``): ``/dev/fb0``, ``mmap``, drawing from userspace

**Goal**

By the end of this lab you will be able to:

- Describe the SPI electrical protocol and identify the SPI mode the ST7789 uses
- Navigate and explain the staging ``fb_st7789v`` driver source code
- Write ST7789 primitives (reset, write-cmd, write-data, fill, draw-rect) from scratch
- Add an ST7789 device tree node and bind your driver to it
- Display a solid color and a rectangle on the physical panel

----

Theory
------

SPI Protocol Fundamentals
~~~~~~~~~~~~~~~~~~~~~~~~~~

TODO: Explain the SPI bus: full-duplex, synchronous, master-slave.  Cover the four
signals (SCLK, MOSI, MISO, CS#) and the four modes defined by CPOL and CPHA.

Key concepts to cover:

- CPOL = 0: clock idles low; CPOL = 1: clock idles high
- CPHA = 0: data sampled on the leading edge; CPHA = 1: on the trailing edge
- ST7789 uses **SPI Mode 0** (CPOL=0, CPHA=0) for writes; read timing differs (we
  will only write)
- Half-duplex write-only path: MISO is not connected to the display module
- Typical operating speed: up to 80 MHz write; we use a conservative 40 MHz

The Linux SPI Subsystem
~~~~~~~~~~~~~~~~~~~~~~~

TODO: Explain the three-layer stack: controller driver (``spi_controller``), SPI core,
and device driver (``spi_driver`` / ``spi_device``).

Key structs and APIs to cover:

.. list-table::
   :header-rows: 1
   :widths: 35 65

   * - API
     - Purpose
   * - ``struct spi_device``
     - One device on the bus; carries ``max_speed_hz``, ``mode``, ``chip_select``
   * - ``struct spi_transfer``
     - One segment: ``tx_buf``, ``rx_buf``, ``len``, ``speed_hz``
   * - ``struct spi_message``
     - Ordered list of ``spi_transfer`` segments treated atomically
   * - ``spi_sync(spi, &msg)``
     - Submit a message and block until complete
   * - ``spi_write(spi, buf, len)``
     - Convenience wrapper: single write transfer
   * - ``module_spi_driver(drv)``
     - Register + unregister an ``spi_driver`` in one macro

The Staging ``fbtft`` Framework and ``fb_st7789v``
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The Linux kernel ``drivers/staging/fbtft/`` directory contains a small framework for
SPI/parallel TFT LCD panels.  The framework (``fbtft-core.c``, ``fbtft-bus.c``) handles
framebuffer registration, DMA buffers, and the dirty-region flush loop.
Individual panel drivers (``fb_st7789v.c``, ``fb_ili9341.c``, etc.) only supply:

- The panel's ``init_sequence`` array (the initialization commands to send on boot)
- The ``set_addr_win()`` callback (set the CASET/RASET window for a write operation)
- Optionally, a custom ``write_vmem()`` to push pixels to the panel

We study ``fb_st7789v.c`` as a **reference** and **starting point** — not as the driver
we ship.  The reasons for writing our own rather than using ``fb_st7789v`` directly:

1. The staging driver depends on the entire ``fbtft`` framework and its abstractions,
   hiding the raw SPI and D/C-pin protocol behind helpers.
2. Writing the primitives from scratch forces you to read the ST7789 datasheet and
   understand what each command does — which is the point of the lab.
3. The staging driver will eventually be removed in favour of the DRM ``panel-mipi-dbi``
   driver; our minimal driver is more representative of how a real out-of-tree driver
   looks today.

Reading the staging driver source (``drivers/staging/fbtft/fb_st7789v.c``) is Exercise 1.

Device Tree: SPI Controller and Device Nodes
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

TODO: Show a real i.MX93 SPI controller node (``&lpspi3`` or the bus routed to EXT2)
and an ST7789 child node.  Explain mandatory SPI DT properties.

Example DTS skeleton (fill in real pad/GPIO values):

.. code-block:: devicetree

   &lpspiX {         /* TODO: identify the correct SPI bus on the EXT2 header */
       #address-cells = <1>;
       #size-cells = <0>;
       status = "okay";
       pinctrl-0 = <&pinctrl_lpspiX>;
       pinctrl-names = "default";

       st7789: display@0 {
           compatible = "lkss,st7789";
           reg = <0>;                          /* chip-select 0 */
           spi-max-frequency = <40000000>;     /* 40 MHz */
           /* ST7789 uses Mode 0 – no spi-cpol / spi-cpha needed */

           dc-gpios    = <&gpio TODO GPIO_ACTIVE_HIGH>;   /* D/C pin  */
           reset-gpios = <&gpio TODO GPIO_ACTIVE_LOW>;    /* RESX pin */

           width  = <240>;
           height = <240>;
       };
   };

   &iomuxc {
       pinctrl_lpspiX: lpspiX-grp {
           fsl,pins = <
               /* TODO: CLK, MOSI, CS, and GPIO iomux entries */
           >;
       };
   };

The ST7789 Command/Data Protocol
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The ST7789 uses a standard SPI bus with one extra signal: **D/C** (data/command,
sometimes called RS or D/CX).

- D/C **low** before clocking a byte → the byte is a **command opcode**
- D/C **high** before clocking a byte → the byte is a **data / parameter** byte

Every interaction with the panel follows this pattern:

.. code-block:: text

   1. Assert CS# (SPI core does this automatically)
   2. Set D/C LOW
   3. Clock the command byte over MOSI
   4. Set D/C HIGH
   5. Clock 0 or more parameter/data bytes
   6. Deassert CS#

Key commands used in the initialization sequence:

.. list-table::
   :header-rows: 1
   :widths: 15 12 73

   * - Command
     - Opcode
     - Purpose
   * - ``SWRESET``
     - 0x01
     - Software reset; wait ≥ 150 ms after
   * - ``SLPOUT``
     - 0x11
     - Exit sleep mode; wait ≥ 500 ms after
   * - ``COLMOD``
     - 0x3A
     - Set pixel format; 0x55 = RGB565 (16 bits per pixel)
   * - ``MADCTL``
     - 0x36
     - Memory access control (rotation, BGR/RGB order)
   * - ``CASET``
     - 0x2A
     - Column address set (4 bytes: XS_H, XS_L, XE_H, XE_L)
   * - ``RASET``
     - 0x2B
     - Row address set (4 bytes: YS_H, YS_L, YE_H, YE_L)
   * - ``RAMWR``
     - 0x2C
     - Begin pixel data write; subsequent bytes fill the window
   * - ``DISPON``
     - 0x29
     - Turn on the display; wait ≥ 100 ms

Framebuffer Basics
~~~~~~~~~~~~~~~~~~~

TODO: Explain ``/dev/fb0``: a linear byte array that maps directly to the panel's
pixel memory.  Cover ``fb_var_screeninfo`` / ``fb_fix_screeninfo``, ``mmap()``, and
how a simple C program writes pixels from userspace.

In our driver we keep a **shadow buffer** in RAM, draw into it from the kernel (or via
userspace writes to ``/dev/fb0``), and flush dirty regions to the ST7789 over SPI
using a ``delayed_work`` item or on explicit ``ioctl``.

----

Lab Exercises
-------------

.. note::

   Lab source code is under ``repos/lkss-linux/drivers/lkss/labs/lab3/``.
   Enable ``CONFIG_LKSS_LAB3`` in menuconfig before building.

   Hardware setup for today:

   - Wire the **ST7789 display module** to the EXT2 expansion header:
     CLK, MOSI, CS, D/C, RESET, 3.3 V, GND.
   - Keep the LEDs and buttons from Day 2 connected — they will be used on Day 5.
   - See :ref:`imx93-frdm-ext2-header` for exact pin assignments.

Exercise 1 – Read the Staging Driver
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

**Objective**: Understand the structure of the existing ``fb_st7789v`` staging driver
before writing any code.  This is a reading and analysis exercise.

Sub-tasks:

1. Open the staging driver:

   .. code-block:: bash

      less repos/lkss-linux/drivers/staging/fbtft/fb_st7789v.c

2. Identify and annotate the following in the source:

   a. The ``init_sequence`` array — what commands are sent at boot and in what order?
      List the first five commands and their parameter bytes.
   b. The ``set_addr_win()`` callback — which four commands does it use to define the
      write window?
   c. The ``fbtft_display`` struct at the bottom — what panel dimensions and pixel
      format does it declare?
   d. How does ``fbtft-bus.c`` (``fbtft_write_spi()``) assert the D/C line?  Find the
      GPIO call.

3. Open ``repos/lkss-linux/drivers/staging/fbtft/fbtft.h`` and identify:

   - ``struct fbtft_par`` — the main driver state struct; find the ``gpio.dc`` field
   - ``write_register()`` macro — how does it differ from sending raw SPI bytes?

4. Answer in your lab notes:

   - What is the COLMOD byte used by ``fb_st7789v``?  What pixel format does that
     select?
   - How does the staging framework flush the framebuffer to the panel?  Which function
     is responsible?

**Questions to answer:**

1. Why is ``fb_st7789v.c`` in ``drivers/staging/`` rather than ``drivers/video/``?
   What is the criteria for graduation out of staging?
2. What advantage does the ``fbtft`` framework give panel driver authors?  What does
   it hide from them?

----

Exercise 2 – Add the ST7789 Device Tree Node
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

**Objective**: Describe the ST7789 hardware to the kernel before writing the driver.

Sub-tasks:

1. Identify which SPI bus (``lpspiX``) is routed to the EXT2 expansion header.
   Check the board DTS and schematic.
2. Open:
   ``repos/lkss-linux/arch/arm64/boot/dts/freescale/imx93-11x11-frdm.dts``
3. Add the ``&lpspiX`` node and the ``st7789`` child node using the skeleton from the
   Theory section.  Fill in real GPIO numbers for D/C and RESET from your wiring.
4. Add the ``pinctrl`` iomux entries for CLK, MOSI, and CS.
5. Rebuild only the DTB and boot:

   .. code-block:: bash

      cd repos/lkss-linux
      make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- \
          freescale/imx93-11x11-frdm.dtb
      python3 scripts/lkss.py boot

6. On the board, confirm the SPI device appears:

   .. code-block:: bash

      ls /sys/bus/spi/devices/

----

Exercise 3 – Write a Minimal SPI Driver (probe / remove)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

**Objective**: Write the ``spi_driver`` skeleton that binds to the ``"lkss,st7789"``
compatible string and prints the negotiated SPI parameters in ``probe``.

Sub-tasks:

1. Open ``repos/lkss-linux/drivers/lkss/labs/lab3/st7789.c``.
2. Define the ``of_device_id`` table with ``"lkss,st7789"`` and the ``spi_device_id``
   table with ``"st7789"``.
3. Implement ``st7789_probe``:

   .. code-block:: c

      static int st7789_probe(struct spi_device *spi)
      {
          dev_info(&spi->dev,
                   "ST7789 probe: max_speed=%u Hz, mode=0x%02x\n",
                   spi->max_speed_hz, spi->mode);
          return 0;
      }

4. Implement a trivial ``st7789_remove``.
5. Use ``module_spi_driver()`` to register the driver.
6. Enable in menuconfig, build, install, boot, and check ``dmesg``.

----

Exercise 4 – Implement ``write_cmd`` and ``write_data``
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

**Objective**: Build the two lowest-level primitives that all higher-level functions
will call.  Study how the staging driver does the same thing in ``fbtft-bus.c``, then
implement your own version.

Sub-tasks:

1. In ``st7789_probe``, obtain the D/C and RESET GPIO descriptors:

   .. code-block:: c

      priv->dc    = devm_gpiod_get(&spi->dev, "dc",    GPIOD_OUT_LOW);
      priv->reset = devm_gpiod_get(&spi->dev, "reset", GPIOD_OUT_HIGH);

2. Implement ``st7789_write_cmd(priv, cmd)``:

   .. code-block:: c

      static int st7789_write_cmd(struct st7789_priv *priv, u8 cmd)
      {
          gpiod_set_value(priv->dc, 0);           /* command mode */
          return spi_write(priv->spi, &cmd, 1);
      }

3. Implement ``st7789_write_data(priv, buf, len)``:

   .. code-block:: c

      static int st7789_write_data(struct st7789_priv *priv,
                                   const u8 *buf, size_t len)
      {
          gpiod_set_value(priv->dc, 1);           /* data mode */
          return spi_write(priv->spi, buf, len);
      }

4. Add a convenience macro ``st7789_write_data_byte(priv, byte)`` that passes a
   single-element array — this mirrors the ``write_reg()`` macro used in the
   staging driver.

Compare your implementation to ``fbtft_write_spi()`` in ``fbtft-bus.c``.  Note what
the staging framework additionally handles (DMA alignment, byte-swapping) that our
minimal version skips.

----

Exercise 5 – Hardware Reset and Init Sequence
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

**Objective**: Reset the ST7789 and send the initialization sequence derived from the
staging driver's ``init_sequence`` array.

Sub-tasks:

1. Implement ``st7789_hw_reset(priv)``:

   .. code-block:: c

      gpiod_set_value(priv->reset, 0);   /* assert reset */
      msleep(15);
      gpiod_set_value(priv->reset, 1);   /* release */
      msleep(120);

2. Implement ``st7789_init_display(priv)`` sending the following sequence
   (derived from ``fb_st7789v.c``'s ``init_sequence``):

   .. code-block:: text

      SWRESET  (0x01) — wait 150 ms
      SLPOUT   (0x11) — wait 500 ms
      COLMOD   (0x3A) + data 0x55   (RGB565)
      MADCTL   (0x36) + data 0x00   (normal orientation)
      INVON    (0x21)               (inversion on — required for correct colors on most
                                     ST7789 modules; check your panel datasheet)
      DISPON   (0x29) — wait 100 ms

   .. note::

      The ``INVON`` command (0x21) is present in the staging driver because many
      ST7789 modules have the color inversion bit set by default.  If your panel shows
      inverted colors, this is the fix.  Compare with ``fb_st7789v.c`` line by line.

3. Call ``st7789_hw_reset()`` then ``st7789_init_display()`` from ``probe``.
4. Build, boot, and verify the display backlight comes on (if wired).

----

Exercise 6 – Fill the Display with a Solid Color
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

**Objective**: Implement ``set_addr_win`` (the equivalent of the staging driver's
``set_addr_win()`` callback) and use it to fill the display with a single RGB565 color.

Sub-tasks:

1. Implement ``st7789_set_addr_win(priv, x0, y0, x1, y1)``:

   .. code-block:: c

      u8 col[4] = { x0 >> 8, x0, x1 >> 8, x1 };
      u8 row[4] = { y0 >> 8, y0, y1 >> 8, y1 };
      st7789_write_cmd(priv,  0x2A);          /* CASET */
      st7789_write_data(priv, col, 4);
      st7789_write_cmd(priv,  0x2B);          /* RASET */
      st7789_write_data(priv, row, 4);
      st7789_write_cmd(priv,  0x2C);          /* RAMWR */

   This is a direct reimplementation of ``set_addr_win()`` in ``fb_st7789v.c`` —
   compare the two side by side.

2. Implement ``st7789_fill(priv, color)``:
   - Call ``set_addr_win(0, 0, width-1, height-1)``
   - Allocate a scanline buffer of ``width × 2`` bytes (RGB565, big-endian on the wire)
   - Fill the buffer with the color bytes; send ``height`` scanlines over SPI
   - Free the buffer

3. Call ``st7789_fill(priv, 0x001F)`` (blue) from ``probe`` after init.
4. Build, boot, and observe the blue display.  Experiment: red = ``0xF800``,
   green = ``0x07E0``, white = ``0xFFFF``.

----

Exercise 7 – Draw a Filled Rectangle
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

**Objective**: Implement ``st7789_fill_rect()`` — the building block for all game
graphics on Day 5.

Sub-tasks:

1. Implement ``st7789_fill_rect(priv, x, y, w, h, color)``:
   - Call ``set_addr_win(x, y, x+w-1, y+h-1)``
   - Write ``w × h × 2`` bytes of the color value
2. Draw a test pattern from ``probe``:

   .. code-block:: c

      st7789_fill(priv, 0x0000);                     /* black background */
      /* Red border, 4 px thick */
      st7789_fill_rect(priv,   0,   0, 240,   4, 0xF800);
      st7789_fill_rect(priv,   0, 236, 240,   4, 0xF800);
      st7789_fill_rect(priv,   0,   0,   4, 240, 0xF800);
      st7789_fill_rect(priv, 236,   0,   4, 240, 0xF800);

3. Build, boot, and verify the red border appears.

----

Exercise 8 – Framebuffer Registration (Stretch Goal)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

**Objective**: Register a Linux ``fbdev`` framebuffer device so that userspace can
write pixels to ``/dev/fb0`` using ``mmap()``, and the driver flushes dirty regions
to the panel over SPI.

Sub-tasks:

1. Allocate a shadow buffer in RAM (``devm_kzalloc()``, ``width × height × 2`` bytes).
2. Register a ``struct fb_info`` with ``register_framebuffer()``.  Fill in:
   - ``var.xres`` / ``var.yres`` = 240, 240
   - ``var.bits_per_pixel`` = 16
   - RGB565 color bitfield offsets (red: 11 bits at offset 11; green: 6 at 5; blue:
     5 at 0)
   - ``fix.smem_start`` / ``fix.smem_len`` pointing at the shadow buffer
3. Implement minimal ``fb_ops``: ``fb_fillrect``, ``fb_copyarea``,
   ``fb_imageblit`` as cfb stubs.
4. Implement a ``delayed_work`` flush: every 33 ms (≈ 30 fps) call ``set_addr_win()``
   for the dirty region and push the shadow buffer over SPI.
5. On the board:

   .. code-block:: bash

      # Verify the framebuffer device appeared
      fbset -i -fb /dev/fb0
      # Fill with solid red using dd
      python3 -c "import sys; sys.stdout.buffer.write(b'\xf8\x00' * 240 * 240)" \
          > /dev/fb0

----

Cheatsheet: SPI and ST7789 APIs
---------------------------------

.. list-table::
   :header-rows: 1
   :widths: 45 55

   * - Function / Macro
     - Purpose
   * - ``spi_write(spi, buf, len)``
     - Synchronous write; blocks until complete
   * - ``spi_sync(spi, &msg)``
     - Submit a full ``spi_message`` synchronously
   * - ``devm_gpiod_get(dev, con_id, flags)``
     - Obtain D/C or RESET GPIO descriptor from DT
   * - ``gpiod_set_value(desc, val)``
     - Drive the GPIO (0 = command/assert-reset, 1 = data/deassert)
   * - ``msleep(ms)``
     - Sleep for ``ms`` milliseconds (valid in ``probe`` — process context)
   * - ``module_spi_driver(drv)``
     - Register + unregister ``spi_driver`` in one macro

----

Resources
---------

- `Linux kernel SPI documentation <https://docs.kernel.org/driver-api/spi.html>`_
- `ST7789V datasheet <https://www.waveshare.com/w/upload/a/ad/ST7789VW.pdf>`_ –
  command reference, initialization, CASET/RASET/RAMWR
- Staging driver source:
  ``repos/lkss-linux/drivers/staging/fbtft/fb_st7789v.c`` and ``fbtft-bus.c``
- `Linux framebuffer API <https://docs.kernel.org/fb/api.html>`_
- `Linux Kernel Labs – SPI <https://linux-kernel-labs.github.io/refs/heads/master/labs/spi.html>`_
- :ref:`imx93-frdm-ext2-header` – expansion header pin map
- :ref:`development_board` – FRDM-IMX93 board overview
