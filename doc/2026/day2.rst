.. _day2:
.. _gpio-led-control-and-button-interrupts:

Day 2 – Misc Devices, GPIO and Button Interrupts
=================================================

Slides: `Day 2 – Misc Devices, GPIO and Button Interrupts`_

**Topics**

- The misc device interface: ``misc_register()``, ``struct miscdevice``,
  ``struct file_operations``
- Userspace ↔ kernel communication: ``copy_to_user()`` / ``copy_from_user()``
- Character device file operations: ``open``, ``release``, ``read``, ``write``
- Custom commands with ``ioctl``: ``_IO``, ``_IOR``, ``_IOW``, ``_IOWR`` macros
- GPIO subsystem overview: descriptor-based API (``gpiod_*``)
- Platform driver model: ``probe`` / ``remove``, ``platform_driver``,
  ``of_match_table``
- Device tree: GPIO controllers, ``pinctrl`` groups, GPIO properties
- Controlling LEDs by writing ``0`` / ``1`` to a misc device file
- Hardware interrupts: IRQ lines, ``devm_request_irq()``, ``IRQF_TRIGGER_*``
- Interrupt context constraints: what you can and cannot do in an ISR
- Software debouncing

**Goal**

By the end of this lab you will be able to:

- Register a misc character device and implement ``read`` / ``write`` / ``ioctl``
  file operations
- Transfer data safely between userspace and kernel using ``copy_to/from_user()``
- Wire three LEDs to GPIO pins and control them by writing to a device file
- Register a GPIO interrupt for a push-button and handle it in an ISR
- Report button press events to userspace through the same device file
- Apply simple software debouncing inside a kernel driver

----

Theory
------

The Misc Device Interface
~~~~~~~~~~~~~~~~~~~~~~~~~~

The **misc** (miscellaneous) subsystem is the simplest way to expose a kernel driver
to userspace as a character device.  Compared to registering a full character device
(``alloc_chrdev_region``, ``cdev_add``, ``class_create`` …), ``misc_register()`` does
all of that in a single call and automatically assigns a dynamic minor number under the
fixed major number ``10``.

.. code-block:: c

   #include <linux/miscdevice.h>
   #include <linux/fs.h>

   static const struct file_operations lkss_fops = {
       .owner   = THIS_MODULE,
       .open    = lkss_open,
       .release = lkss_release,
       .read    = lkss_read,
       .write   = lkss_write,
       .unlocked_ioctl = lkss_ioctl,
   };

   static struct miscdevice lkss_misc = {
       .minor = MISC_DYNAMIC_MINOR,
       .name  = "lkss_gpio",          /* creates /dev/lkss_gpio */
       .fops  = &lkss_fops,
   };

   /* In probe or module_init: */
   misc_register(&lkss_misc);

   /* In remove or module_exit: */
   misc_deregister(&lkss_misc);

After ``misc_register()`` the device node ``/dev/lkss_gpio`` appears automatically
(no ``mknod`` needed if ``udev`` is running).

Userspace ↔ Kernel Communication
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Userspace and kernel live in separate virtual address spaces.  Directly dereferencing
a userspace pointer in kernel code causes a fault.  The kernel provides safe copy
helpers that validate the pointer and handle page faults gracefully:

.. list-table::
   :header-rows: 1
   :widths: 40 60

   * - Function
     - Purpose
   * - ``copy_to_user(to, from, n)``
     - Copy ``n`` bytes from kernel buffer ``from`` to userspace ``to``; returns the
       number of bytes **not** copied (0 on full success)
   * - ``copy_from_user(to, from, n)``
     - Copy ``n`` bytes from userspace ``from`` to kernel buffer ``to``; returns bytes
       not copied
   * - ``put_user(val, ptr)``
     - Write a single scalar value to a userspace pointer
   * - ``get_user(val, ptr)``
     - Read a single scalar value from a userspace pointer

Always check the return value.  Return ``-EFAULT`` to the caller if the copy fails:

.. code-block:: c

   if (copy_from_user(kbuf, ubuf, count))
       return -EFAULT;

The ``read`` and ``write`` File Operations
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

``read`` and ``write`` are the primary data-transfer operations.  Their signatures
mirror the POSIX system calls:

.. code-block:: c

   /* Called when userspace calls read(fd, buf, count) */
   static ssize_t lkss_read(struct file *filp, char __user *buf,
                            size_t count, loff_t *ppos)
   {
       const char *msg = "LED is ON\n";
       size_t len = strlen(msg);
       if (copy_to_user(buf, msg, len))
           return -EFAULT;
       return len;
   }

   /* Called when userspace calls write(fd, buf, count) */
   static ssize_t lkss_write(struct file *filp, const char __user *buf,
                             size_t count, loff_t *ppos)
   {
       char kbuf[16] = {};
       if (count >= sizeof(kbuf))
           return -EINVAL;
       if (copy_from_user(kbuf, buf, count))
           return -EFAULT;
       /* parse kbuf and act … */
       return count;
   }

The ``ioctl`` Interface
~~~~~~~~~~~~~~~~~~~~~~~~

``ioctl`` is used for out-of-band control commands that do not map cleanly to
``read`` / ``write`` (e.g., "select which LED", "query driver version", "set blink
rate").  The ``cmd`` argument encodes the direction, type, number, and size of the
payload using four macros:

.. list-table::
   :header-rows: 1
   :widths: 25 75

   * - Macro
     - Meaning
   * - ``_IO(type, nr)``
     - Command with no data transfer
   * - ``_IOR(type, nr, datatype)``
     - Command that **reads** data from the kernel (kernel → user)
   * - ``_IOW(type, nr, datatype)``
     - Command that **writes** data to the kernel (user → kernel)
   * - ``_IOWR(type, nr, datatype)``
     - Bidirectional transfer

Define your commands in a shared header so both the kernel driver and userspace
programs include the same definitions:

.. code-block:: c

   /* include/uapi/lkss_gpio.h  (or a local header for this lab) */
   #define LKSS_IOC_MAGIC  'L'

   #define LKSS_LED_ON     _IOW(LKSS_IOC_MAGIC, 0, int)   /* arg = LED index */
   #define LKSS_LED_OFF    _IOW(LKSS_IOC_MAGIC, 1, int)   /* arg = LED index */
   #define LKSS_BTN_COUNT  _IOR(LKSS_IOC_MAGIC, 2, int)   /* arg = button index */

.. code-block:: c

   /* In the driver: */
   static long lkss_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
   {
       int idx;
       switch (cmd) {
       case LKSS_LED_ON:
           if (get_user(idx, (int __user *)arg))
               return -EFAULT;
           /* gpiod_set_value(led[idx], 1); */
           break;
       /* … */
       default:
           return -ENOTTY;
       }
       return 0;
   }

The GPIO Subsystem
~~~~~~~~~~~~~~~~~~

TODO: Explain the Linux GPIO descriptor API (``gpiod_get``, ``gpiod_set_value``,
``gpiod_direction_output``, ``gpiod_free``).  Contrast with the deprecated integer-
based API.  Explain ``devm_*`` variants.

Key concepts to cover:

- GPIO banks and pin numbering on the i.MX93 (``GPIO1`` … ``GPIO4``)
- Active-high vs active-low polarity — the descriptor API handles it transparently
- ``gpioinfo`` / ``gpioset`` / ``gpiomon`` userspace tools for quick experimentation

Platform Drivers and Device Tree Matching
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

TODO: Explain ``struct platform_driver``, ``module_platform_driver()``, and
``of_match_table``.  Cover ``probe`` / ``remove`` lifecycle and ``devm_*`` resource
management.

Device Tree: GPIO Nodes and pinctrl
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

TODO: Walk through a minimal DTS snippet adding LED and button nodes.  Explain
``pinctrl-0``, ``pinctrl-names``, and GPIO property naming conventions.

Example DTS skeleton (fill in real i.MX93 iomux offsets and GPIO numbers):

.. code-block:: devicetree

   &iomuxc {
       pinctrl_lkss_leds: lkss-leds-grp {
           fsl,pins = <
               /* TODO: MX93_PAD_GPIO_IO?? … */
           >;
       };

       pinctrl_lkss_buttons: lkss-buttons-grp {
           fsl,pins = <
               /* TODO: MX93_PAD_GPIO_IO?? … */
           >;
       };
   };

   lkss-leds {
       compatible = "lkss,gpio-leds";
       pinctrl-0 = <&pinctrl_lkss_leds>;
       pinctrl-names = "default";
       led-gpios = <&gpio1 TODO GPIO_ACTIVE_HIGH>,
                   <&gpio1 TODO GPIO_ACTIVE_HIGH>,
                   <&gpio1 TODO GPIO_ACTIVE_HIGH>;
   };

   lkss-buttons {
       compatible = "lkss,gpio-buttons";
       pinctrl-0 = <&pinctrl_lkss_buttons>;
       pinctrl-names = "default";
       button-gpios = <&gpio2 TODO GPIO_ACTIVE_LOW>,
                      <&gpio2 TODO GPIO_ACTIVE_LOW>,
                      <&gpio2 TODO GPIO_ACTIVE_LOW>,
                      <&gpio2 TODO GPIO_ACTIVE_LOW>;
       interrupt-parent = <&gpio2>;
       interrupts = <TODO IRQ_TYPE_EDGE_FALLING>,
                    <TODO IRQ_TYPE_EDGE_FALLING>,
                    <TODO IRQ_TYPE_EDGE_FALLING>,
                    <TODO IRQ_TYPE_EDGE_FALLING>;
   };

Hardware Interrupts
~~~~~~~~~~~~~~~~~~~

TODO: Explain the interrupt path on ARM64: peripheral → GIC-400 → CPU.  Cover
``devm_request_irq()``, ``irqreturn_t``, ``IRQ_HANDLED`` / ``IRQ_NONE``, and
threaded IRQs (``IRQF_ONESHOT``).

Key concepts:

- ``gpiod_to_irq()`` — converts a GPIO descriptor to a Linux IRQ number
- ``IRQF_TRIGGER_RISING``, ``IRQF_TRIGGER_FALLING``, ``IRQF_TRIGGER_BOTH``
- ``disable_irq()`` / ``enable_irq()`` for runtime masking

Interrupt Context vs Process Context
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

TODO: Explain what "interrupt context" means and its constraints.

Rules:

- Cannot sleep (``msleep``, ``wait_event``, ``mutex_lock``) in an ISR
- Use spinlocks (``spin_lock_irqsave``), not mutexes, when sharing data with an ISR
- Threaded IRQs run in process context and may sleep

Software Debouncing
~~~~~~~~~~~~~~~~~~~

TODO: Explain mechanical switch bounce and two common mitigation strategies:

1. **Timestamp gating** — record ``ktime_get()`` at the last accepted event; discard
   events within a 50 ms debounce window.
2. **Delayed work** — reschedule a ``delayed_work`` item on each edge; the action only
   fires after the signal stabilises.

----

Lab Exercises
-------------

.. note::

   Lab source code is under ``repos/lkss-linux/drivers/lkss/labs/lab2/``.
   Enable ``CONFIG_LKSS_LAB2`` in menuconfig before building.

   Hardware setup for today:

   - Wire **3 LEDs** (with 330 Ω series resistors) to GPIO pins on the EXT2 expansion
     header.  See :ref:`imx93-frdm-ext2-header` for the pin map.
   - Wire **4 push-buttons** (normally-open, with pull-up) to four other GPIO pins.

Exercise 1 – A Minimal Misc Device
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

**Objective**: Register a misc character device and verify it appears in ``/dev``.

Sub-tasks:

1. Open ``repos/lkss-linux/drivers/lkss/labs/lab2/lkss_misc.c``.
2. Define a ``struct file_operations`` with only ``.owner = THIS_MODULE``.
3. Register a ``struct miscdevice`` with ``MISC_DYNAMIC_MINOR`` and name ``"lkss_gpio"``.
4. In ``module_init`` call ``misc_register()``; in ``module_exit`` call
   ``misc_deregister()``.
5. Build and load on the board.  Verify:

   .. code-block:: bash

      ls -la /dev/lkss_gpio
      cat /proc/misc | grep lkss

**Questions to answer:**

1. What major number does the misc subsystem use?  Check with ``ls -l /dev/lkss_gpio``.
2. Why is ``MISC_DYNAMIC_MINOR`` preferable to picking a fixed minor number?

----

Exercise 2 – Implement ``open``, ``release``, and ``write``
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

**Objective**: Add ``open``, ``release``, and a ``write`` handler that echoes the
received string back to the kernel log.

Sub-tasks:

1. Implement ``lkss_open``: print ``"device opened\n"`` with ``pr_info()``, return 0.
2. Implement ``lkss_release``: print ``"device closed\n"``, return 0.
3. Implement ``lkss_write``:
   - Reject writes larger than 15 bytes (return ``-EINVAL``).
   - Copy the data from userspace with ``copy_from_user()``.
   - Null-terminate the buffer and print it with ``pr_info()``.
   - Return ``count``.
4. Register both callbacks in ``file_operations``.
5. On the board:

   .. code-block:: bash

      echo "hello kernel" > /dev/lkss_gpio
      dmesg | tail -3

**Questions to answer:**

1. What happens if ``copy_from_user()`` returns a non-zero value?  Why must you check it?
2. What does ``echo`` actually write to the file descriptor?  Include the newline in
   your answer.

----

Exercise 3 – Implement ``read``
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

**Objective**: Return a status string to userspace when the device is read.

Sub-tasks:

1. Keep a ``static int led_state = 0`` variable in the driver.
2. Implement ``lkss_read``:
   - Format a message: ``"LED state: %d\n"`` using ``snprintf()`` into a kernel buffer.
   - Copy it to userspace with ``copy_to_user()``.
   - Advance ``*ppos`` by the number of bytes copied (so a second read returns EOF).
   - Return the number of bytes copied, or 0 if ``*ppos >= len``.
3. On the board:

   .. code-block:: bash

      cat /dev/lkss_gpio

**Questions to answer:**

1. What happens if you do not advance ``*ppos``?  Try it and observe the behaviour of
   ``cat``.
2. Why should ``read`` return 0 (EOF) after returning all data, rather than looping
   forever?

----

Exercise 4 – Add an ``ioctl`` Interface
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

**Objective**: Implement two ioctl commands: ``LKSS_LED_ON`` and ``LKSS_LED_OFF`` that
accept a LED index.

Sub-tasks:

1. Define the ioctl commands in ``lkss_gpio.h`` (see the Theory section above for the
   macro usage).
2. Implement ``lkss_ioctl``:
   - For ``LKSS_LED_ON``: read the LED index with ``get_user()``, bounds-check it,
     set ``led_state`` accordingly, print a message.
   - For ``LKSS_LED_OFF``: same, clear the bit.
   - Return ``-ENOTTY`` for unknown commands.
3. Register ``lkss_ioctl`` as ``.unlocked_ioctl`` in ``file_operations``.
4. Write a small userspace test program ``test_ioctl.c``:

   .. code-block:: c

      int fd = open("/dev/lkss_gpio", O_RDWR);
      int led = 0;
      ioctl(fd, LKSS_LED_ON,  &led);   /* turn on LED 0 */
      sleep(1);
      ioctl(fd, LKSS_LED_OFF, &led);   /* turn off LED 0 */
      close(fd);

5. Cross-compile and run it on the board.

**Questions to answer:**

1. What does the kernel return if userspace calls ``ioctl()`` with an unknown command
   and the driver returns ``-ENOTTY``?
2. Why is ``unlocked_ioctl`` used instead of the old ``ioctl``?

----

Exercise 5 – Wire GPIO: Control LEDs via ``write``
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

**Objective**: Connect the misc device to real GPIO hardware.  Writing ``"1\n"`` to the
device file turns on LED 0; writing ``"0\n"`` turns it off.

Sub-tasks:

1. Convert the driver to a **platform driver** that binds to the ``lkss-leds`` device
   tree node (add the DTS node from the Theory section; fill in real GPIO numbers).
2. In ``probe``: obtain all three LED GPIO descriptors with ``devm_gpiod_get_index()``,
   configure them as outputs, store them in driver private data attached to the misc
   device via ``miscdevice.parent`` or a global (acceptable for a lab).
3. Update ``lkss_write``:
   - Parse the first character of the received buffer: ``'1'`` → ``gpiod_set_value(led[0], 1)``;
     ``'0'`` → ``gpiod_set_value(led[0], 0)``.
4. On the board:

   .. code-block:: bash

      echo 1 > /dev/lkss_gpio   # LED on
      echo 0 > /dev/lkss_gpio   # LED off

5. Extend the format to ``"<index> <value>\n"`` (e.g. ``"2 1\n"`` turns on LED 2) and
   update the parser.

----

Exercise 6 – Button Interrupt: count and expose via ``read``
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

**Objective**: Register a GPIO interrupt for button 0 and expose the press counter via
``read``.

Sub-tasks:

1. In ``probe``: obtain the button 0 GPIO descriptor, convert to IRQ with
   ``gpiod_to_irq()``, register the handler with ``devm_request_irq()`` using
   ``IRQF_TRIGGER_FALLING``.
2. Declare an ``atomic_t btn_count`` in driver private data; increment it in the ISR
   with ``atomic_inc()``.
3. Update ``lkss_read`` to format ``"presses: %d\n"`` using ``atomic_read(&btn_count)``.
4. On the board:

   .. code-block:: bash

      # Press the button a few times, then:
      cat /dev/lkss_gpio

**Questions to answer:**

1. Why use ``atomic_t`` instead of a plain ``int`` for the counter?
2. What is the risk of calling ``pr_info()`` inside the ISR on every button press?

----

Exercise 7 – Software Debouncing
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

**Objective**: Eliminate spurious button-press events with a timestamp gate.

Sub-tasks:

1. Add a ``ktime_t last_press`` field to the driver private data.
2. At the top of the ISR, compute the delta since ``last_press`` using ``ktime_get()``
   and ``ktime_ms_delta()``.
3. If the delta is less than 50 ms, return ``IRQ_HANDLED`` immediately.
4. Otherwise update ``last_press`` and increment ``btn_count``.
5. Rebuild, boot, and verify a single physical press now produces exactly one count
   increment.

**Bonus**: Repeat with ``delayed_work`` instead of timestamp gating.  Compare the
approaches: which is simpler? Which has lower latency?

----

Exercise 8 – All Four Buttons and Three LEDs (Stretch Goal)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

**Objective**: Wire all four buttons and all three LEDs.  Extend the ioctl interface
with ``LKSS_BTN_COUNT`` that returns the press count for any button index.

Design choices are yours — focus on clean resource management: all GPIOs and IRQs must
be released correctly when the module is unloaded.

----

Cheatsheet: Key APIs for Today
--------------------------------

.. list-table::
   :header-rows: 1
   :widths: 45 55

   * - Function / Macro
     - Purpose
   * - ``misc_register(&miscdev)``
     - Create ``/dev/<name>`` with automatic minor number
   * - ``misc_deregister(&miscdev)``
     - Remove the device node
   * - ``copy_from_user(kbuf, ubuf, n)``
     - Safe copy from userspace; returns bytes NOT copied
   * - ``copy_to_user(ubuf, kbuf, n)``
     - Safe copy to userspace; returns bytes NOT copied
   * - ``_IOW(magic, nr, type)``
     - Define an ioctl command that receives data from userspace
   * - ``_IOR(magic, nr, type)``
     - Define an ioctl command that returns data to userspace
   * - ``devm_gpiod_get_index(dev, id, idx, flags)``
     - Get the *n*-th GPIO from a DT multi-GPIO property
   * - ``gpiod_set_value(desc, val)``
     - Drive GPIO output; respects active-low polarity
   * - ``gpiod_to_irq(desc)``
     - Convert GPIO descriptor to Linux IRQ number
   * - ``devm_request_irq(dev, irq, handler, flags, name, data)``
     - Register ISR; auto-released on driver detach
   * - ``atomic_t`` / ``atomic_inc()`` / ``atomic_read()``
     - Lock-free integer counter safe for ISR ↔ process sharing
   * - ``ktime_get()`` / ``ktime_ms_delta()``
     - Monotonic clock read and millisecond delta

----

Resources
---------

- `Linux miscdevice API <https://docs.kernel.org/driver-api/misc_devices.html>`_
- `Linux kernel GPIO documentation <https://docs.kernel.org/driver-api/gpio/index.html>`_
- `Linux kernel interrupt documentation <https://docs.kernel.org/core-api/genericirq.html>`_
- `Linux Kernel Labs – Character device drivers <https://linux-kernel-labs.github.io/refs/heads/master/labs/device_drivers.html>`_
- `i.MX93 Technical Reference Manual – GPIO chapter <https://www.nxp.com/docs/en/reference-manual/IMX93RM.pdf>`_
- :ref:`imx93-frdm-ext2-header` – expansion header pin map
- :ref:`development_board` – FRDM-IMX93 board overview
