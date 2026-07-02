.. _hackathon:

Hackathon: Build an Embedded Gadget
===================================

Welcome to the final day of the summer school! Today there are no guided
exercises. Theme of the hackathon is **Retro Style**.

Put everything you build so far together:

- a new **hackpad driver** (provided) exposes the daughter board's 4 push
  buttons and 3 LEDs to userspace,
- the **ST7789 display driver** (Day 3) grows a standard Linux framebuffer
  interface, so any graphics library can draw on it,
- the **BMP280 sensor driver** (copy it from Day 4) provides live temperature and pressure,
- you can use any other **sensor** in the toolbox! 
- the **LVGL** graphics library runs on top of ``/dev/fb0`` and gives you
  widgets, fonts, and animations in userspace.

Pick **one of project ideas** below (or use your own idea ), form a team of 2 and build it. At the end of the
day every team demos their app on real hardware. Retro style is encouraged; blinking LEDs and buttons
are mandatory

How the day is structured:
  * Read the samples and learn how to use LVGL
  * Pick an idea and work on it


The Framework
-------------

Your gadget is a normal Linux **userspace application**. The kernel work of
the previous days is what makes that possible:

.. code-block:: text

   +--------------------------------------------------+
   |            your app (LVGL, C)                    |   userspace
   +-----------+---------------+----------------------+
   | /dev/fb0  | /dev/hackpad  | /sys/.../temperature |
   +-----------+---------------+----------------------+
   | st7789fb  |    hackpad    |   bmp280 (Day 4)     |   kernel
   +-----------+---------------+----------------------+
   |  SPI      |  GPIOs + IRQs |   I2C                |   hardware
   +--------------------------------------------------+

The framework code lives in two places:

- **kernel**: ``repos/lkss-linux/drivers/lkss/lab5/`` — the ``hackpad`` and
  ``st7789fb`` drivers (enable ``CONFIG_LKSS_DRIVERS_LAB5`` in menuconfig)
- **userspace**: ``repos/lkss-linux/drivers/lkss/lab5/demos/`` — the LVGL
  build setup, a small HAL, a dozen complete warm-up samples and a skeleton for
  every project idea (you fill in the ``TODO``\ s)

.. _hackpad-driver:

The hackpad Driver: Buttons and LEDs
------------------------------------

Reading 4 buttons and driving 3 LEDs from every app would mean duplicating
the Day 2 GPIO/IRQ code seven times. Instead, the provided ``hackpad`` driver
(``drivers/lkss/lab5/hackpad.c``) wraps all of it behind **one misc character
device**, ``/dev/hackpad``. Read through it, it is Day 2 condensed into one
file: ``devm_gpiod_get_index()``, ``gpiod_to_irq()``, ``devm_request_irq()``,
jiffies-based debouncing, a kfifo, and a wait queue.

First enable its device tree node in
``arch/arm64/boot/dts/freescale/imx93-11x11-frdm.dts`` (it is already there,
inside ``lkss-bus``, but shipped ``disabled``):

.. code-block:: devicetree

   hackpad {
       compatible = "lkss,hackpad";
       button-gpios = <&gpio2 17 GPIO_ACTIVE_LOW>, // SW1
               <&gpio2 18 GPIO_ACTIVE_LOW>, // SW2
               <&gpio2 27 GPIO_ACTIVE_LOW>, // SW3
               <&gpio2 22 GPIO_ACTIVE_LOW>; // SW4
       led-gpios = <&gpio2 4 GPIO_ACTIVE_HIGH>,  // Red
               <&gpio2 14 GPIO_ACTIVE_HIGH>, // Green
               <&gpio2 15 GPIO_ACTIVE_HIGH>; // Blue
       status = "okay";                      // <- change from "disabled"
   };

.. warning::

   The Day 2 ``button-led`` node uses some of the same GPIOs. Make sure it is
   ``disabled`` (and the Day 2 modules are not loaded), otherwise the hackpad
   probe fails with ``-EBUSY``.

The userspace API (``drivers/lkss/lab5/demos/common/hackpad.h``):

+-----------------------------------+---------------------------------------------------------------+
| Operation                         | Behaviour                                                     |
+===================================+===============================================================+
| ``read()``                        | Returns one or more ``struct hackpad_event`` records          |
|                                   | (button index, pressed/released, timestamp). Blocks until an  |
|                                   | event arrives. Supports ``O_NONBLOCK``.                       |
+-----------------------------------+---------------------------------------------------------------+
| ``poll()`` / ``select()``         | Wakes up when an event is pending, making it suitable for     |
|                                   | integration with event loops.                                 |
+-----------------------------------+---------------------------------------------------------------+
| ``ioctl(HACKPAD_IOC_GET_BTNS)``   | Returns the current button state as a bitmask. Bit *N* is set |
|                                   | when button *N* is currently held down. Useful for polling    |
|                                   | the current state in games and interactive applications.      |
+-----------------------------------+---------------------------------------------------------------+
| ``ioctl(HACKPAD_IOC_SET_LED)``    | Sets the state of a single LED (on or off).                   |
+-----------------------------------+---------------------------------------------------------------+


Quick test from the shell:

.. code-block:: bash

   modprobe hackpad
   echo 111 > /dev/hackpad     # all LEDs on
   echo 000 > /dev/hackpad     # all LEDs off
   hexdump -C /dev/hackpad     # now press some buttons (Ctrl+C to stop)

And from C:

.. code-block:: c

   #include "hackpad.h"

   int fd = open("/dev/hackpad", O_RDWR);

   /* Wait for a button event */
   struct hackpad_event ev;
   read(fd, &ev, sizeof(ev));
   printf("button %d %s\n", ev.button, ev.pressed ? "pressed" : "released");

   /* Is SW1 held down right now? */
   __u32 state;
   ioctl(fd, HACKPAD_IOC_GET_BTNS, &state);
   if (state & (1 << HACKPAD_BTN_SW1)) { /* ... */ }

.. _st7789-fbdev:

Adding fbdev Support to the ST7789 Driver
------------------------------------------

Your Day 3 driver can fill rectangles — but only from *kernel* code. To let
userspace draw, Linux has a standard answer: the **framebuffer device**
(fbdev). A driver that registers a framebuffer gets a ``/dev/fbN`` node, and
any application can then ``mmap()`` it and write pixels — no custom ioctls, no
copying code into the kernel. This is exactly the interface LVGL's Linux
backend expects.

The full driver is provided in ``drivers/lkss/lab5/st7789fb.c`` (it is Day 3's
``st7789.c`` plus fbdev interface). Here is how fbdev is implemented.

**1. A shadow framebuffer in RAM.** The panel's GRAM is not memory-mapped,
it sits behind an SPI link. So we allocate a normal RAM buffer that userspace
draws into, and we copy it to the panel over SPI when it changes:

.. code-block:: c

   vmem = vzalloc(ST7789_FB_SIZE);     /* 240 * 240 * 2 = 115200 bytes */
   info->screen_buffer = vmem;

**2. Deferred I/O.** How do we know *when* userspace touched the buffer?
The ``fb_deferred_io`` framework write-protects the buffer's pages; the first
write to a page faults, the fb core notes the page as dirty and schedules a
worker. Our callback then runs, at most once per ``delay``, and flushes the
frame to the panel:

.. code-block:: c

   static void st7789fb_deferred_io(struct fb_info *info,
                                    struct list_head *pagereflist)
   {
       st7789fb_update_display(info->par);   /* full frame over SPI */
   }

   static struct fb_deferred_io st7789fb_defio = {
       .delay       = HZ / 30,               /* max 30 refreshes/second */
       .deferred_io = st7789fb_deferred_io,
   };

This batching is the whole trick: userspace can scribble thousands of pixels,
and the SPI bus sees at most 30 full-frame transfers per second (one 240x240
RGB565 frame is 115200 bytes ≈ 15 ms at 62.5 MHz, comfortably fast enough).

**3. The flush function** reuses your Day 3 primitives. One detail:
userspace writes native little-endian RGB565, but the ST7789 wants the high
byte of each pixel first, so we byte-swap into a transmit buffer:

.. code-block:: c

   static void st7789fb_update_display(struct st7789_priv *priv)
   {
       const u16 *fb = (const u16 *)priv->info->screen_buffer;
       u16 *tx = (u16 *)priv->txbuf;

       for (i = 0; i < priv->width * priv->height; i++)
           tx[i] = swab16(fb[i]); /* userspace (native) little-endian but ST7789 uses big-endian */

       st7789_set_addr_win(priv, 0, 0, priv->width - 1, priv->height - 1);
       st7789_write_data(priv, priv->txbuf, ST7789_FB_SIZE);
   }

**4. Describe the framebuffer** so userspace knows what it is looking at:
``fix`` holds what never changes (memory layout), ``var`` what could
(resolution, pixel format):

.. code-block:: c

   info->fix.line_length    = 240 * 2;           /* bytes per scanline   */
   info->fix.visual         = FB_VISUAL_TRUECOLOR;
   info->var.xres           = 240;
   info->var.yres           = 240;
   info->var.bits_per_pixel = 16;
   info->var.red.offset     = 11;  info->var.red.length   = 5;
   info->var.green.offset   = 5;   info->var.green.length = 6;
   info->var.blue.offset    = 0;   info->var.blue.length  = 5;

**5. Generate the file operations and register.** The fb core provides
generic implementations of ``read``/``write``/``fillrect`` for system-memory
framebuffers; one macro instantiates them with our damage callbacks so that
the write() path also triggers a panel update:

.. code-block:: c

   FB_GEN_DEFAULT_DEFERRED_SYSMEM_OPS(st7789fb,
                                      st7789fb_defio_damage_range,
                                      st7789fb_defio_damage_area)

   static const struct fb_ops st7789fb_ops = {
       .owner = THIS_MODULE,
       FB_DEFAULT_DEFERRED_OPS(st7789fb),
   };

   info->fbdefio = &st7789fb_defio;
   fb_deferred_io_init(info);
   register_framebuffer(info);          /* creates /dev/fb0 */

**6. The device tree node.** ``st7789fb`` binds to the same SPI device node
as the Day 3 driver (``compatible = "lkss,st7789"``) on the ``lpspi4`` bus.
The shipped ``arch/arm64/boot/dts/freescale/imx93-11x11-frdm.dts`` already
contains it; if your tree does not (or you want to double-check the wiring),
here it is, ready to copy-paste:

.. code-block:: dts

   &lpspi4 {
       #address-cells = <1>;
       #size-cells    = <0>;
       status         = "okay";

       pinctrl-0     = <&pinctrl_lpspi4_st7789>;
       pinctrl-names = "default";

       lkss_st7789: st7789@0 {
           compatible        = "lkss,st7789";
           reg               = <0>;            /* chip-select index 0   */
           spi-max-frequency = <62500000>;

           /* D/C:   GPIO2_IO13 (J601 pin 22), active-high = data */
           dc-gpios    = <&gpio2 13 GPIO_ACTIVE_HIGH>;

           /* Reset: GPIO2_IO19 (J601 pin 15), active-low */
           reset-gpios = <&gpio2 19 GPIO_ACTIVE_LOW>;
       };
   };

   &iomuxc {
       pinctrl_lpspi4_st7789: lpspi4-st7789grp {
           fsl,pins = <
               MX93_PAD_GPIO_IO21__LPSPI4_SCK   0x31e   /* J601 pin 16, SCK  */
               MX93_PAD_GPIO_IO20__LPSPI4_SOUT  0x31e   /* MOSI              */
               MX93_PAD_GPIO_IO13__GPIO2_IO13   0x31e   /* J601 pin 22, D/C  */
               MX93_PAD_GPIO_IO19__GPIO2_IO19   0x31e   /* J601 pin 15, RST  */
           >;
       };
   };

After any device tree change, rebuild the DTB and redeploy it to the board.

Build it (``CONFIG_LKSS_DRIVERS_LAB5_ST7789FB=m`` in menuconfig), then test:

.. code-block:: bash

   # the Day 3 driver binds to the same device! (make sure you dont load both)
   modprobe st7789fb
   dmesg | tail -2
   # st7789fb spi0.0: fb0: 240x240 RGB565 framebuffer on SPI

   # White noise - the "is it alive?" test
   cat /dev/urandom > /dev/fb0

   # All-red screen from the shell (RGB565 red = 0xF800, little-endian)
   for i in $(seq 57600); do printf '\x00\xf8'; done > /dev/fb0

.. note::

   Both ``st7789`` and ``st7789fb`` match ``compatible = "lkss,st7789"``, so
   use only ``st7789fb`` today.

LVGL Quick Guide
----------------

`LVGL <https://lvgl.io>`_ (Light and Versatile Graphics Library) is an
open-source embedded GUI library written in C. It gives you widgets (labels,
bars, charts, arcs...), styles, fonts and animations, renders them into a
buffer, and hands that buffer to a *display backend* , for us, the Linux
fbdev backend writing to ``/dev/fb0``.

The mental model:

- Everything on screen is an **object** (``lv_obj_t``), living in a tree
  rooted at the active **screen** (``lv_screen_active()``). A plain object is
  just a rectangle; ``lv_label_create()``, ``lv_bar_create()`` etc.
- Appearance is controlled by **styles** set directly on objects:
  ``lv_obj_set_style_bg_color(obj, lv_color_hex(0xFF0000), 0)``.
- Periodic work is done in **timers** (``lv_timer_create()``), this is where
  your game loop lives.
- ``lv_timer_handler()`` must be called repeatedly from ``main()``; it runs
  the timers and redraws whatever became dirty. Only the small dirty area is
  re-rendered, LVGL is efficient by design.

A complete minimal application:

.. code-block:: c

   #include "lvgl.h"

   int main(void)
   {
       lv_init();
       lv_tick_set_cb(my_ms_counter);                /* time source */

       lv_display_t *disp = lv_linux_fbdev_create(); /* fbdev backend */
       lv_linux_fbdev_set_file(disp, "/dev/fb0");

       lv_obj_t *label = lv_label_create(lv_screen_active());
       lv_label_set_text(label, "Hello LKSS!");
       lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);

       while (1) {
           uint32_t d = lv_timer_handler();          /* render + timers */
           usleep(d * 1000);
       }
   }

In the hackathon you do not even write this much, the provided HAL
(``drivers/lkss/lab5/demos/common/hal.c``) does the display setup, wires the hackpad
buttons in, and adds sensor helpers. Your ``main()`` becomes:

.. code-block:: c

   #include "hal.h"

   int main(void)
   {
       hal_init();          /* LVGL + /dev/fb0 + /dev/hackpad + LEDs   */
       create_my_ui();      /* your objects and timers                 */
       hal_run();           /* lv_timer_handler() loop, never returns  */
   }

Every sample and skeleton talks to the hardware through this one small API,
so it is worth understanding exactly what each call does.

A design note first: **no HAL function takes a handle or context argument**.
The board has exactly one display, one hackpad and one sensor, so the HAL
keeps the file descriptors and LVGL objects it manages in private
(``static``) variables inside ``hal.c``. ``hal_init()`` fills them in once;
every later call just uses them. That is also why ``hal_run()`` needs no
parameters: by the time you call it, your whole application - objects,
styles, timers - has already been registered *inside* LVGL, and
``hal_run()`` only has to keep LVGL's engine turning.

``void hal_init(void)``
   Call this once, first thing in ``main()``, before touching any LVGL API.
   It performs the whole setup dance from the previous section for you:

   - initializes LVGL (``lv_init()``) and gives it a monotonic millisecond
     time source;
   - creates the LVGL display bound to ``/dev/fb0`` (the ``st7789fb.ko``
     driver). If the framebuffer is missing, the program prints an error
     and exits - nothing works without a display;
   - opens ``/dev/hackpad`` (the ``hackpad.ko`` driver) in non-blocking
     mode. The hackpad is *optional*: if the module is not loaded you only
     get a warning on stderr, and every button/LED helper silently turns
     into a no-op, so display-only apps keep working;
   - registers the buttons as an LVGL *keypad* input device on the default
     widget group, and switches all LEDs off.

``void hal_run(void)``
   The application main loop; **it never returns**, so it must be the last
   line of ``main()``. It repeatedly calls ``lv_timer_handler()``, which
   redraws the dirtied screen areas and runs every due ``lv_timer``
   callback, then sleeps up to 20 ms until the next round. There are no
   parameters because there is nothing left to tell it - all your objects
   and timers already live inside LVGL. The flip side: once ``hal_run()``
   is called, *all* of your application logic executes inside callbacks
   (``lv_timer_create()``, button callbacks); there is no code of yours
   "after the loop".

``uint32_t hal_buttons(void)``
   *Level* input: which buttons are held down **right now**? Returns a
   bitmask with bit *N* set while button *N* is held, so
   ``hal_buttons() & (1 << HACKPAD_BTN_SW2)`` stays true for as long as a
   finger rests on SW2, and 0 means "nothing pressed" (also the answer when
   the hackpad is missing). Use it for continuous actions such as sliding
   the pong paddle.

``bool hal_button_pressed(int button)``
   *Edge* input: has this button been pressed since you last asked?
   ``button`` is one of ``HACKPAD_BTN_SW1`` .. ``HACKPAD_BTN_SW4``. Returns
   ``true`` **exactly once** per physical press, no matter how long the
   button stays held or how often you poll; presses are latched
   internally, so a short press cannot slip between two polls of a slow
   game timer. Use it for discrete actions: turning in snake, rotating a
   tetris piece, restarting after game over.

``void hal_set_button_cb(hal_btn_cb_t cb)``
   The third input style: registers ``cb(button, pressed)`` to be called on
   *every* edge - once when the button goes down (``pressed = true``) and
   once when it is released (``pressed = false``). The callback runs from
   the LVGL loop inside ``hal_run()``, never from a second thread, so it
   may safely touch LVGL objects. Pass ``NULL`` to unregister. Most demos
   simply poll; reach for the callback when the release matters too
   (e.g. "charge while held, fire on release").

``void hal_led(int led, bool on)``
   Switches a single LED on or off; ``led`` is one of ``HACKPAD_LED_RED``,
   ``HACKPAD_LED_GREEN`` or ``HACKPAD_LED_BLUE``. A no-op when the hackpad
   is missing.

``void hal_leds(uint32_t mask)``
   Sets all three LEDs at once from a bitmask: bit 0 = red, bit 1 = green,
   bit 2 = blue, so ``hal_leds(0b101)`` lights red and blue and switches
   green off. Handy for score milestones and level meters.

``lv_indev_t *hal_keypad(void)``
   Returns the LVGL keypad input device that ``hal_init()`` created
   (SW1..SW4 arrive as ``LV_KEY_PREV`` / ``NEXT`` / ``ENTER`` / ``ESC`` on
   the default widget group). Only needed for advanced widget navigation -
   none of the samples call it.

``int hal_bmp280_read(double *temp_c, double *press_hpa)``
   Reads the Day 4 BMP280 driver's sysfs attributes and hands the results
   back through the two *output* pointers: temperature in °C and pressure
   in hPa. Either pointer may be ``NULL`` if you only want the other
   value. Returns ``0`` on success and ``-1`` when the sensor is not
   available (``bmp280.ko`` not loaded, sensor not wired) - in that case
   the output variables are left untouched, so check the return value.
   Each call triggers a fresh measurement over I2C and is comparatively
   slow: call it from a dedicated slow timer (the demos use 5 s) and cache
   the values; never call it from a fast game/clock tick.

LVGL API cheatsheet, enough for everything in the project ideas:

+-----------------------------------------------------------------+------------------------------------------------------------+
| Function                                                        | Purpose                                                    |
+=================================================================+============================================================+
| ``lv_obj_create(parent)``                                       | Creates a basic rectangular object.                        |
+-----------------------------------------------------------------+------------------------------------------------------------+
| ``lv_obj_set_size(obj, w, h)``                                  | Sets the object's size in pixels.                          |
+-----------------------------------------------------------------+------------------------------------------------------------+
| ``lv_obj_set_pos(obj, x, y)``                                   | Positions the object relative to its parent.               |
+-----------------------------------------------------------------+------------------------------------------------------------+
| ``lv_obj_align(obj, LV_ALIGN_CENTER, dx, dy)``                  | Aligns an object relative to an anchor point of its        |
|                                                                 | parent, with optional X/Y offsets.                         |
+-----------------------------------------------------------------+------------------------------------------------------------+
| ``lv_obj_set_style_bg_color(obj, lv_color_hex(0xRRGGBB), 0)``   | Sets the object's background (fill) color.                 |
+-----------------------------------------------------------------+------------------------------------------------------------+
| ``lv_obj_set_style_text_font(obj, &lv_font_montserrat_48, 0)``  | Sets the font used for text. Ensure the desired font size  |
|                                                                 | is enabled in ``lv_conf.h``.                               |
+-----------------------------------------------------------------+------------------------------------------------------------+
| ``lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN)``                    | Hides an object.                                           |
+-----------------------------------------------------------------+------------------------------------------------------------+
| ``lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN)``                 | Makes a previously hidden object visible again.            |
+-----------------------------------------------------------------+------------------------------------------------------------+
| ``lv_label_create(parent)``                                     | Creates a text label.                                      |
+-----------------------------------------------------------------+------------------------------------------------------------+
| ``lv_label_set_text_fmt(label, "%d", value)``                   | Updates the label using ``printf``-style formatting.       |
+-----------------------------------------------------------------+------------------------------------------------------------+
| ``lv_bar_create()``                                             | Creates a progress bar.                                    |
+-----------------------------------------------------------------+------------------------------------------------------------+
| ``lv_bar_set_value(bar, value, LV_ANIM_OFF)``                   | Updates the progress value, optionally with animation.     |
+-----------------------------------------------------------------+------------------------------------------------------------+
| ``lv_arc_create()``                                             | Creates a circular gauge or dial.                          |
+-----------------------------------------------------------------+------------------------------------------------------------+
| ``lv_arc_set_value()``                                          | Updates the displayed value.                               |
+-----------------------------------------------------------------+------------------------------------------------------------+
| ``lv_chart_create()``                                           | Creates a scrolling chart for plotting data.               |
+-----------------------------------------------------------------+------------------------------------------------------------+
| ``lv_chart_set_next_value(chart, series, value)``               | Appends a new data point to the chart.                     |
+-----------------------------------------------------------------+------------------------------------------------------------+
| ``lv_timer_create(cb, period_ms, user_data)``                   | Creates a periodic timer callback, commonly used as a game |
|                                                                 | loop or update task.                                       |
+-----------------------------------------------------------------+------------------------------------------------------------+
| ``lv_timer_set_period()``                                       | Changes a timer's interval.                                |
+-----------------------------------------------------------------+------------------------------------------------------------+
| ``lv_timer_pause()``                                            | Pauses a timer.                                            |
+-----------------------------------------------------------------+------------------------------------------------------------+
| ``lv_timer_resume()``                                           | Resumes a paused timer.                                    |
+-----------------------------------------------------------------+------------------------------------------------------------+

Button input, three ways (all provided by the HAL):

.. code-block:: c

   /* 1. Level: is the button held down now? (movement in games) */
   if (hal_buttons() & (1 << HACKPAD_BTN_SW1)) { /* move up */ }

   /* 2. Edge: pressed since last check? (menus, turns, restart) */
   if (hal_button_pressed(HACKPAD_BTN_SW3)) { /* restart game */ }

   /* 3. LVGL keypad: SW1..SW4 arrive as LV_KEY_PREV / NEXT / ENTER / ESC
    *    on the default group - lets you navigate real widgets. */
   lv_group_add_obj(lv_group_get_default(), my_button_widget);

And the LEDs / sensor:

.. code-block:: c

   hal_led(HACKPAD_LED_GREEN, true);         /* one LED   */
   hal_leds(0b101);                          /* red+blue  */

   double t, p;
   hal_bmp280_read(&t, &p);                  /* degC, hPa (Day 4 driver!) */

Building LVGL for the Board
---------------------------

LVGL is cross-compiled on your **host** together with your application and
copied to the board. Everything is already set up in
``repos/lkss-linux/drivers/lkss/lab5/demos``; here is the complete,
reproducible procedure.

**1. Host prerequisites** (Ubuntu/Debian) — the same cross-compiler you
already build the kernel with:

.. code-block:: bash

   sudo apt install gcc-aarch64-linux-gnu

**2. Get LVGL** (pinned to v9.3.0, the version the demos are written for):

.. code-block:: bash

   cd repos/lkss-linux/drivers/lkss/lab5/demos
   git clone --depth 1 --branch v9.3.0 https://github.com/lvgl/lvgl.git

**3. Configuration** — ``lv_conf.h`` (provided) selects the LVGL features;
anything not set falls back to a sane default. Ours enables the fbdev
backend and RGB565:

.. code-block:: c

   #define LV_COLOR_DEPTH 16          /* RGB565, matches st7789fb   */
   #define LV_USE_LINUX_FBDEV 1       /* the /dev/fb0 backend       */
   #define LV_DEF_REFR_PERIOD 33      /* 30 fps, matches deferred io */
   #define LV_FONT_MONTSERRAT_48 1    /* + the other fonts we use    */

**4. Build** — a plain Makefile compiles all LVGL C sources into a static
library and links one binary per demo (disabled LVGL features compile to
empty objects, so building everything is fine):

.. code-block:: bash

   make -j$(nproc)          # everything, into build/
   make pong                # or just one demo

   file build/pong
   # build/pong: ELF 64-bit LSB pie executable, ARM aarch64 ...

**5. Deploy and run.** Copy the binaries into the rootfs

.. code-block:: bash

   # on the host
   python3 scripts/lkss.py boot

   # copy binary to rootfs
   python3 scripts/lkss.py copy repos/lkss-linux/drivers/lkss/lab5/demos/jump root/

   # on the board
   modprobe st7789fb
   modprobe hackpad
   modprobe bmp280
   ./jump

Samples
-------

Before jumping into your project, run and *read* the small samples in
``drivers/lkss/lab5/demos``, to demonstrates one building block and how API works,

**Display only** (need just ``st7789fb``):

+--------------+----------------------------------------------------------------+
| Sample       | What it shows                                                  |
+==============+================================================================+
| ``hello``    | Displays text on the screen by creating a label, selecting a   |
|              | font and color, aligning it, and updating its contents from a  |
|              | timer using ``printf``-style formatting.                       |
+--------------+----------------------------------------------------------------+
| ``colors``   | Fills the entire screen with a different color every second.   |
|              | This is the smallest possible LVGL application and a useful    |
|              | sanity check that the display is working.                      |
+--------------+----------------------------------------------------------------+
| ``flag``     | Draws a grid of small rectangles in a six-color diagonal flag  |
|              | pattern, demonstrating how to create and position many         |
|              | objects efficiently.                                           |
+--------------+----------------------------------------------------------------+
| ``bounce``   | Animates a ball bouncing off the screen edges using position   |
|              | and velocity. When a collision occurs, the velocity is         |
|              | reversed, illustrating the core mechanics behind games such as |
|              | Pong and Breakout.                                             |
+--------------+----------------------------------------------------------------+
| ``chart``    | Displays a live scrolling chart updated by a periodic timer.   |
|              | It uses a sine wave as the data source, demonstrating the same |
|              | techniques used for weather station or sensor history graphs.  |
+--------------+----------------------------------------------------------------+


**Buttons and LEDs** (need ``st7789fb`` + ``hackpad``):

+--------------+----------------------------------------------------------------+
| Sample       | What it shows                                                  |
+==============+================================================================+
| ``dot``      | Moves a dot one grid cell for each button press in any of the  |
|              | four directions, wrapping around the screen edges.             |
|              | Demonstrates edge-triggered input, the same movement model     |
|              | used in Snake.                                                 |
+--------------+----------------------------------------------------------------+
| ``paddle``   | Moves a paddle left or right while SW1 or SW2 is held, using   |
|              | level-polled input at 60 FPS. This is the same control scheme  |
|              | used in Pong and Breakout.                                     |
+--------------+----------------------------------------------------------------+
| ``jump``     | Displays four colored balls, each controlled by a different    |
|              | button. Pressing a button launches its ball upward while       |
|              | gravity brings it back down, demonstrating a simple game loop  |
|              | with independent physics for multiple objects.                 |
+--------------+----------------------------------------------------------------+
| ``blink``    | Changes the screen color on a button press and blinks the      |
|              | corresponding LED three times. Demonstrates coordinated        |
|              | visual and hardware feedback using timers instead of           |
|              | ``sleep()``.                                                   |
+--------------+----------------------------------------------------------------+
| ``leds``     | Implements an LED control panel. SW1, SW2, and SW3 toggle      |
|              | individual LEDs, SW4 turns them all off, and on-screen         |
|              | indicators mirror the current LED state.                       |
+--------------+----------------------------------------------------------------+
| ``bar``      | Controls a progress bar by holding buttons to increase or      |
|              | decrease its value. The three LEDs act as a simple level       |
|              | meter, illustrating the gauge pattern used in system           |
|              | monitoring applications.                                       |
+--------------+----------------------------------------------------------------+
| ``menu``     | Demonstrates a simple multi-screen interface with three pages, |
|              | navigated using SW1 and SW2. Screens are shown and hidden      |
|              | using LVGL's hidden-object flag, illustrating the navigation   |
|              | pattern used in watch faces and weather station interfaces.    |
+--------------+----------------------------------------------------------------+

----

Project Ideas
-------------

Pick one. Each idea lists the **base goal** (what a demo must show) and
**stretch goals** if you finish early.

Every idea comes with a **skeleton** in ``drivers/lkss/lab5/demos/<name>/``:
the UI is already built and a timer already ticks — the game/application
logic is a series of numbered ``TODO``\ s for you to fill in. The header
comment of each skeleton is an **API quick reference** for exactly the HAL
and LVGL calls that project needs, so read it first. Stuck? The supervisors
have seen every one of these bugs before — ask.

1. Retro Pong
~~~~~~~~~~~~~

The 1972 classic. A ball bounces between two paddles; you control the left
one with SW1/SW2, a software-controlled player runs the right one.

- **Base**: moving paddles, bouncing ball, score display, win at 5 points.
- **Stretch**: ball speeds up during rallies; spin depending on where the
  paddle is hit; green LED flash when you score, red when the software player does;
  two-player mode (SW3/SW4 for the right paddle).
- **Framework used**: held-button state (``hal_buttons()``), ``lv_timer`` at
  16 ms as game loop, plain ``lv_obj`` rectangles.
- **Code**: ``pong/pong.c``

2. Snake
~~~~~~~~

Steer a growing snake to the food without biting yourself. With only relative
turns (SW1 = turn left, SW4 = turn right) it is surprisingly tricky.

- **Base**: 15x15 grid, food, growth, self/wall collision, score.
- **Stretch**: speed increases with length; LED "belt rank" milestones at
  length 5/10/15; high-score kept across games; walls that wrap around.
- **Framework used**: edge-triggered buttons (``hal_button_pressed()``), a
  pool of hidden ``lv_obj`` cells, dynamic ``lv_timer_set_period()``.
- **Code**: ``snake/snake.c``

3. Breakout
~~~~~~~~~~~

Clear a colorful wall of bricks with ball and paddle. The 3 LEDs *are* your
life counter — lose a ball, lose a light.

- **Base**: 8x5 brick wall, paddle (SW1/SW2), ball physics, 3 lives on LEDs,
  win/lose screens.
- **Stretch**: bounce angle depends on paddle hit position; per-row scores;
  a second, faster level; brick that drops a power-up.
- **Framework used**: ``hal_buttons()``, ``hal_leds()``, hidden-flag tricks
  for destroyed bricks.
- **Code**: ``breakout/breakout.c``

4. Tetris
~~~~~~~~~

The 1984 falling-blocks classic. Seven pieces drop into a 10x15 well;
SW1/SW2 move the piece, SW3 rotates it, SW4 hard-drops. Complete rows
disappear and score points, and the pieces fall faster as you clear lines.

- **Base**: falling piece, collision with walls/floor/settled blocks,
  move/rotate/drop on buttons, row clearing with score and a "next piece"
  preview.
- **Stretch**: fall speed increases with cleared lines; LED milestones every
  10 lines; a game-over screen with restart; wall kicks when rotating next
  to a wall.
- **Framework used**: edge-triggered buttons (``hal_button_pressed()``), a
  pool of hidden ``lv_obj`` cells for the well, ``lv_timer_set_period()``
  for the fall speed, ``hal_leds()`` for milestones.
- **Code**: ``tetris/tetris.c``

5. Wristwatch
~~~~~~~~~~~~~

A watch face collection for a 240x240 "smartwatch": big digital time, a
seconds ring rendered with ``lv_arc``, and a weather complication fed by
*your* BMP280 driver. SW1/SW2 flip between faces.

- **Base**: correct time (``localtime``), date, at least two faces,
  temperature shown from the BMP280.
- **Stretch**: blue LED "tick" every second; a stopwatch face driven by the
  buttons; day/night color themes; set-time UI using the LVGL group/keypad
  navigation.
- **Framework used**: ``hal_bmp280_read()``, ``lv_arc``, multiple screens via
  hidden containers.
- **Code**: ``watch/watch.c``

6. Weather Station
~~~~~~~~~~~~~~~~~~

A multi-screen weather station: live readings, temperature history and
pressure history charts (``lv_chart``), navigated like a menu with SW1/SW2.
Falling pressure predicts bad weather — show a trend arrow and light the
matching LED (green rising, red falling, blue steady).

- **Base**: live temperature + pressure screen, one history chart, screen
  navigation.
- **Stretch**: pressure-trend forecast ("rain coming"); min/max since boot;
  configurable sample rate through the ENTER key + group navigation; log
  samples to a CSV file on the rootfs.
- **Framework used**: ``hal_bmp280_read()`` (the Day 4 sysfs attributes!),
  ``lv_chart`` in shift mode, LED trend indicator.
- **Code**: ``weather/weather.c``

7. Retro System Monitor
~~~~~~~~~~~~~~~~~~~~~~~

A green-on-black CRT-style dashboard for the board itself: kernel version,
uptime, load average, live CPU and memory bars — all parsed from ``/proc``,
the same interface ``top`` uses. The LED shows system load at a glance.

- **Base**: uptime, load average, CPU % (delta of ``/proc/stat``), memory
  from ``/proc/meminfo``, updating every second.
- **Stretch**: scrolling ``dmesg`` ticker; per-core CPU bars; network RX/TX
  from ``/proc/net/dev``; a "Matrix rain" screensaver after 30 s idle.
- **Framework used**: ``lv_bar``, the UNSCII retro fonts, procfs parsing —
  and a healthy respect for how much the kernel tells you for free.
- **Code**: ``sysmon/sysmon.c``

----

Checklist Before You Start Hacking
----------------------------------

#. Kernel: ``CONFIG_FB=y`` (Device Drivers → Graphics support → Frame
   buffer devices — without it the ``st7789fb`` option stays hidden!), then
   ``CONFIG_LKSS_DRIVERS_LAB5`` + both lab 5 drivers as ``m``, Day 4
   ``bmp280`` still enabled.
#. Device tree: ``hackpad`` node ``okay``, Day 2 ``button-led`` node
   ``disabled``, Day 3/4 SPI + I2C nodes still in place.
#. ``python3 scripts/lkss.py compile --install-modules`` and boot.
#. On the board: ``modprobe st7789fb hackpad bmp280`` — check ``/dev/fb0``
   and ``/dev/hackpad`` exist and ``cat /dev/urandom > /dev/fb0`` snows.
#. On the host: demos build (previous section) — run one on the board.
#. Pick an idea, split the work (one person on game logic, one on UI, one on
   glue/testing), and go build something you'll want to show off.

Useful resources
----------------

- `LVGL documentation <https://docs.lvgl.io/9.3/>`_ — widgets, styles, examples
- `LVGL API reference <https://docs.lvgl.io/9.3/API/index.html>`_
- `Linux fbdev API <https://docs.kernel.org/fb/api.html>`_
- `Deferred I/O <https://docs.kernel.org/fb/deferred_io.html>`_
- Day 3 (:ref:`SPI + ST7789 <spi-bus-st7789-display>`) and
  Day 4 (:ref:`I2C + BMP280 <i2c-bus-and-the-bmp280-sensor-driver>`) labs
- ``drivers/video/fbdev/ssd1307fb.c`` — an upstream SPI/I2C display driver
  using the exact same deferred-io pattern
