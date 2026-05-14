5-Day Summer School Plan
========================

.. note::

   This file is for organizer reference only. It is not linked from the main toctree.

Overview
--------

.. list-table::
   :header-rows: 1
   :widths: 8 28 64

   * - Day
     - Title
     - Topics

   * - **1**
     - Introduction to the Linux Kernel
     - Linux kernel architecture, buildroot/rootfs, cross-compilation, kernel Image + DTB,
       ``lkss.py`` tooling, kernel modules (``insmod``, ``rmmod``, ``dmesg``),
       ``printk`` / ``pr_*`` log levels, Kconfig (``=y`` vs ``=m``),
       kernel oops vs panic, kernel timers (one-shot and periodic),
       brief device tree introduction (covered in depth on Day 2).

   * - **2**
     - Character Device Drivers and GPIO
     - Character device driver model (``open``, ``read``, ``write``, ``release``),
       user-space / kernel-space data transfer (``copy_to_user`` / ``copy_from_user``),
       static and dynamic device registration, ``mknod`` and ``udev``,
       ``ioctl`` for custom commands,
       GPIO consumer API (``gpiod_get``, ``gpiod_set_value``),
       LED control from a kernel driver,
       platform drivers and device tree matching (``compatible`` strings, OF APIs).

   * - **3**
     - Interrupts, Buttons, PWM, and the Expansion Header
     - Hardware interrupts and ``devm_request_irq()``,
       GPIO interrupt handling (rising/falling edge, IRQF_TRIGGER_*),
       software debouncing in the kernel,
       PWM subsystem (``pwm_request``, ``pwm_config``, ``pwm_enable``),
       connecting buttons and LEDs to the 40-pin EXPI expansion header of the FRDM board,
       device tree bindings for GPIO and PWM nodes,
       deeper device tree study: ``pinctrl``, ``clocks``, ``regulators``.

   * - **4**
     - SPI Displays and the ST7789 TFT LCD
     - SPI subsystem overview (master, device, transfer),
       SPI device tree bindings (``spidev``, panel nodes),
       Linux DRM/KMS subsystem vs. legacy framebuffer (``fbdev``),
       the ``panel-mipi-dbi`` driver family and the ST7789 controller,
       writing pixels and basic 2-D graphics from userspace (``/dev/fb0``),
       integrating the ST7789 TFT display with the FRDM board via the EXPI header,
       introduction to ``libdrm`` / ``fbdev`` userspace APIs,
       preparation for the hackathon: input event devices (``/dev/input/eventX``).

   * - **5** *(Hackathon)*
     - Build Your Own Embedded Linux Application
     - Full-day open hackathon. Students use the hardware and knowledge from Days 1–4
       to build one of the projects below (or propose their own).
       Teams present a 5-minute demo at the end of the day.

Hackathon Project Ideas
-----------------------

Retro Gaming Console
~~~~~~~~~~~~~~~~~~~~

Implement a classic game (Pong, Snake, or Breakout) on the i.MX93 FRDM board:

- **Input**: push-buttons wired to GPIO pins on the EXPI header
- **Display**: ST7789 TFT LCD driven via SPI
- **Kernel side**: a character device driver (written in Day 2/3) delivers button
  press events to userspace
- **Userspace side**: a C program reads button events, updates game state, and renders
  frames to ``/dev/fb0``

Suggested milestones:

1. Draw a static frame (background + paddle) on the LCD
2. Move a paddle left/right using two buttons
3. Add the ball with basic collision detection
4. Add score display using a simple bitmap font

Weather Station
~~~~~~~~~~~~~~~

Read environmental data from a sensor on the EXPI header and display it live:

- **Sensor**: BME280 or BMP280 (temperature, pressure, humidity) connected via SPI or I2C
- **Display**: ST7789 TFT LCD
- **Kernel side**: enable the ``bmp280`` or ``bme280`` IIO driver in Kconfig; expose data
  via ``/sys/bus/iio/devices/iio:device0/``
- **Userspace side**: poll sensor readings, format them, and render to ``/dev/fb0``

Suggested milestones:

1. Verify the sensor is detected (``dmesg | grep bme280``)
2. Read raw values from sysfs
3. Display temperature and pressure on the LCD
4. Add a simple bar-chart history (last N readings)

Grading Rubric (suggested)
---------------------------

.. list-table::
   :header-rows: 1
   :widths: 20 80

   * - Criteria
     - Description
   * - Functionality (40%)
     - Does the project work as demonstrated? Does it handle edge cases gracefully?
   * - Code quality (20%)
     - Is the kernel code clean, safe, and free of obvious memory issues?
   * - Understanding (20%)
     - Can the team explain their kernel/userspace split and why they made those choices?
   * - Presentation (20%)
     - Clear 5-minute demo; team can answer questions from the audience.

Daily Time Budget (4 hours each)
---------------------------------

.. list-table::
   :header-rows: 1
   :widths: 20 80

   * - Block
     - Content
   * - 0:00 – 0:30
     - Theory lecture (slides + whiteboard)
   * - 0:30 – 0:45
     - Live demo by instructor
   * - 0:45 – 3:00
     - Guided lab exercises (students work, instructors circulate)
   * - 3:00 – 3:30
     - Stretch / bonus exercises for fast finishers
   * - 3:30 – 4:00
     - Q&A, debrief, preview of next day
