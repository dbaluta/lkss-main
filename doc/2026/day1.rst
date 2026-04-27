.. _day1:
.. _introduction-to-the-linux-kernel:

Day 1 – Introduction to the Linux Kernel
=========================================

Slides: `Day 1 – Introduction to the Linux Kernel`_

**Topics**

- Linux kernel overview and source tree layout
- Buildroot and the root filesystem
- Cross-compiling the kernel Image and DTB
- Kernel modules: ``insmod``, ``rmmod``, ``lsmod``
- Kernel logging: ``printk``, ``pr_*`` log levels, ``dmesg``
- Kconfig: built-in (``=y``) vs loadable module (``=m``)
- Kernel oops vs panic
- Kernel timers: one-shot and periodic

**Goal**

By the end of this lab you will be able to:

- Build and boot a Linux kernel on the i.MX93 FRDM board using the ``lkss.py`` tooling
- Write, compile, load and unload a kernel module
- Read and interpret kernel log output
- Configure the kernel build system with ``menuconfig``
- Trigger and analyse a kernel oops report
- Schedule deferred work using the kernel timer API

----

Theory
------

What is the Linux Kernel?
~~~~~~~~~~~~~~~~~~~~~~~~~~

The **Linux kernel** is the core of the operating system. It sits between the hardware
and the user-space applications, providing:

- **Process management** – scheduling, creation, termination of processes
- **Memory management** – virtual memory, page allocation
- **Device drivers** – abstractions for hardware peripherals
- **Filesystem support** – VFS layer, ext4, FAT, etc.
- **Networking stack** – TCP/IP and everything above it
- **System calls** – the API that user-space programs use to interact with the kernel

The kernel runs in a privileged CPU mode (EL1 on ARM64) while user applications run in
unprivileged mode (EL0). This separation protects the system: a buggy application cannot
crash the kernel, but a buggy **kernel module** can.

.. figure:: ../_static/figures/linux_kernel_overview.png
   :align: center
   :alt: Linux kernel overview

   Linux kernel architecture overview

TODO: Add ``_static/figures/linux_kernel_overview.png`` diagram.

The Linux source tree is organized into several top-level directories:

.. list-table::
   :header-rows: 1
   :widths: 20 80

   * - Directory
     - Purpose
   * - ``arch/``
     - Architecture-specific code (``arch/arm64/`` for our board)
   * - ``drivers/``
     - Device drivers (there are tousands of them)
   * - ``fs/``
     - Filesystem implementations
   * - ``include/``
     - Kernel header files
   * - ``mm/``
     - Memory management
   * - ``net/``
     - Networking stack
   * - ``kernel/``
     - Core kernel subsystems (scheduler, timers, IRQs…)
   * - ``Documentation/``
     - In-tree documentation

Root Filesystem and Buildroot
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The Linux kernel it is not useful running standalone, it needs a **root filesystem** (rootfs) to mount
after boot. The rootfs provides:

- The init system (``/sbin/init`` or ``busybox init``)
- System libraries (``libc``, ``libm``, ...)
- Userspace tools (``ls``, ``cat``, ``insmod``, ...)
- Device nodes under ``/dev``

For embedded systems, `Buildroot <https://buildroot.org>`_ is a popular tool to generate a
minimal, cross-compiled rootfs together with a toolchain. In this summer school the rootfs
is pre-built for you and is provided as a ``rootfs.ext2`` image, a raw ext2 filesystem image
that the board mounts over USB using the NXP ``uuu`` flashing tool.

The Kernel Image
~~~~~~~~~~~~~~~~

When the kernel source is compiled for an ARM64 target, the build system produces a
file called ``Image`` located at ``arch/arm64/boot/Image``. This is a raw, uncompressed
binary of the kernel, the single executable that the bootloader (U-Boot in our case)
loads into RAM and jumps to at boot time.

A few things worth knowing about ``Image``:

- It is **architecture-specific**: the ``Image`` built for ARM64 will not run on x86.
  This is why we need a cross-compiler (``aarch64-linux-gnu-gcc``) on the host.
- It is **self-contained**: the kernel code, built-in drivers (``=y``), and the
  decompression stub are all packed into this single file.
- Loadable modules (``.ko`` files) are **not** part of ``Image``; they live on the
  rootfs and are loaded at runtime with ``insmod`` or ``modprobe`` commands.
- The bootloader also needs to know where in RAM to place ``Image`` and what address
  to jump to; this is handled by the U-Boot script included in ``flash.bin``.

The Device Tree (DTB)
~~~~~~~~~~~~~~~~~~~~~

ARM-based SoCs (like the i.MX93) describe their hardware in a **Device Tree**. The Device
Tree Source (``*.dts``) is compiled into a Device Tree Blob (``*.dtb``) that the kernel
reads at boot time to discover the hardware topology: memory ranges, peripherals, interrupts,
clocks, GPIO controllers, etc.

The kernel needs both the ``Image`` binary **and** the ``*.dtb``
file to boot correctly.

Kernel Modules
~~~~~~~~~~~~~~

A **kernel module** (``*.ko`` file) is a piece of kernel code that can be loaded and
unloaded at runtime without rebooting. This makes development much faster: you write your
driver, compile it as a module, copy it to the board, and load it with ``insmod``, no
reflashing needed.

Every kernel module must define at minimum:

- An **init function** called when the module is loaded (``module_init()``)
- An **exit function** called when the module is unloaded (``module_exit()``)
- License, author, and description metadata (``MODULE_LICENSE``, etc.)

.. code-block:: c

   #include <linux/module.h>
   #include <linux/init.h>

   static int __init hello_init(void)
   {
       pr_info("Hello, kernel!\n");
       return 0;
   }

   static void __exit hello_exit(void)
   {
       pr_info("Goodbye, kernel!\n");
   }

   module_init(hello_init);
   module_exit(hello_exit);

   MODULE_LICENSE("GPL");
   MODULE_AUTHOR("LKSS Student");
   MODULE_DESCRIPTION("Hello World kernel module");

Key points:

- ``__init`` and ``__exit`` are compiler hints that place the functions in special memory
  sections; the kernel frees the ``__init`` section after boot.
- ``pr_info()`` is a shorthand for ``printk(KERN_INFO ...)``. Output goes to the kernel
  log, visible via ``dmesg`` or the serial console.
- A module that returns a non-zero value from its init function fails to load.

----

Environment Setup
-----------------

This section walks you through initializing the development environment, building the
kernel, and booting the board. Each step shows both the ``lkss.py`` helper command and
the equivalent raw shell commands so you understand what happens under the hood.

.. note::

   All ``lkss.py`` commands must be run from the **root of the lkss-main repository**::

      cd ~/work/repos/lkss-main

Step 0 – Prerequisites
~~~~~~~~~~~~~~~~~~~~~~~

Make sure the following packages are installed on your host machine:

.. code-block:: bash

   sudo apt update
   sudo apt install -y \
       git build-essential libncurses-dev bc flex bison \
       libssl-dev libelf-dev gcc-aarch64-linux-gnu \
       minicom python3 python3-pip e2tools

Install Python dependencies for the ``lkss.py`` tool:

.. code-block:: bash

   pip3 install -r requirements.txt

Step 1 – Initialize the Environment
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The ``init`` command clones the Linux kernel repository, downloads pre-built binaries
(rootfs, boot container, ``uuu`` flashing tool), and caches the configuration.

**Using lkss.py (recommended):**

.. code-block:: bash

   python3 scripts/lkss.py init

.. note::

   The first run downloads several hundred megabytes. You need to be patient.

**What it does under the hood:**

.. code-block:: bash

   # Clone the kernel source
   git clone --depth=10 -b lkss-6.19.y \
       https://github.com/NXP-Research/lkss-linux \
       repos/lkss-linux

   # Download rootfs, boot container, and uuu from GitHub releases
   # (the lkss.yaml manifest specifies the exact URLs and versions)

After ``init`` completes, you should see:

.. code-block:: text

   repos/
   └── lkss-linux/          # Linux kernel source
   bin/
   ├── rootfs               # ext2 rootfs image
   ├── flash.bin            # boot container (U-Boot + ATF)
   └── uuu                  # NXP Universal Update Utility

Step 2 – Build the Kernel
~~~~~~~~~~~~~~~~~~~~~~~~~~

**Using lkss.py (recommended):**

.. code-block:: bash

   # Build with N parallel jobs (replace N with your CPU count, e.g. 8)
   python3 scripts/lkss.py compile -j$(nproc)

The first build applies the ``imx93frdm_lkss_defconfig`` configuration automatically.
Compiled artifacts (``Image`` and ``imx93-11x11-frdm.dtb``) are copied to the ``output/``
directory.

**Equivalent raw commands:**

.. code-block:: bash

   cd repos/lkss-linux

   # Set up the cross-compilation environment
   export ARCH=arm64
   export CROSS_COMPILE=aarch64-linux-gnu-

   # Apply the board defconfig
   make imx93frdm_lkss_defconfig

   # Build kernel Image and device tree blobs
   make -j$(nproc)

   # Artifacts are at:
   #   arch/arm64/boot/Image
   #   arch/arm64/boot/dts/freescale/imx93-11x11-frdm.dtb

.. note::

   A full kernel build takes around **5–10 minutes** depending on your machine.
   Subsequent incremental builds (after changing a driver) are much faster.

Step 3 – Boot the Board
~~~~~~~~~~~~~~~~~~~~~~~~

Connect the three USB-C cables to your board:

1. **POWER USB** – supplies power
2. **BOOT USB** – used by ``uuu`` to transfer the images
3. **DEBUG USB** – gives you a serial console (shows up as ``/dev/ttyACM0``)

Make sure the boot switch is set to **USB boot** mode (``1000`` – only switch 1 is ON).
Refer to :ref:`development_board` for the exact switch positions.

**Open the serial console first** (in a separate terminal):

.. code-block:: bash

   minicom -D /dev/ttyACM0

**Then boot the board:**

.. code-block:: bash

   # Using lkss.py (recommended)
   python3 scripts/lkss.py boot

**Equivalent raw command:**

.. code-block:: bash

   ./bin/uuu -b scripts/boot/uuu_script \
       bin/flash.bin \
       bin/rootfs \
       output/Image \
       output/imx93-11x11-frdm.dtb

Watch the serial console – you should see U-Boot messages followed by the Linux kernel
boot log, ending with a shell prompt:

.. code-block:: text

   Welcome to NXP's LKSS
   nxp-lkss login: root
   root@nxp-lkss:~#

.. tip::

   The default login is ``root`` with password: `root`.

----

Lab Exercises
-------------

.. note::

   The lab source code lives in the Linux kernel tree under
   ``repos/lkss-linux/drivers/lkss/labs/``.  All exercises for today are under
   ``lab1/``.

   Before each test cycle the workflow is:

   1. Edit/write your driver in ``repos/lkss-linux/drivers/lkss/labs/lab1/``
   2. Compile the module(s)
   3. Copy the ``.ko`` file(s) to the rootfs
   4. Boot the board
   5. Load the module and observe

Exercise 1 – Anatomy of a Hello World Module
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

**Objective**: Understand the structure of a kernel module, load and unload it, and
read kernel log output.

**1.1 – Explore the source**

Open ``repos/lkss-linux/drivers/lkss/labs/lab1/hello.c`` and read through it carefully.
Make sure you understand the following points before moving on:

- **The init function** is registered with ``module_init()`` and called by the kernel
  when you run ``insmod``. It must return ``0`` on success or a **negative errno** value
  on failure (e.g. ``-ENOMEM``, ``-ENODEV``). If it returns non-zero the module load
  is aborted and the module is not recorded as loaded.

- **The exit function** is registered with ``module_exit()`` and called when you run
  ``rmmod``. Its job is to **undo everything the init function did**: free memory,
  unregister devices, release resources. A missing or incomplete exit function is a
  resource leak.

- **``__init`` and ``__exit``** are compiler hints. The linker places ``__init``
  functions in a special section that the kernel frees from memory after boot, saving
  RAM. ``__exit`` functions are discarded entirely when the driver is compiled as
  built-in (``=y``), because they can never be called.

- **``MODULE_LICENSE("GPL")``** is mandatory. Without it the kernel marks itself as
  *tainted* and refuses to export GPL-only symbols to your module. Many core kernel
  APIs (including several we will use in later labs) are GPL-only.

**1.2 – Enable the module in Kconfig**

Before building, you need to tell the kernel build system to compile the ``lab1`` modules.
Open menuconfig:

.. code-block:: bash

   # Using lkss.py (recommended)
   python3 scripts/lkss.py menuconfig

   # Raw equivalent
   cd repos/lkss-linux
   make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- menuconfig

Navigate to:

.. code-block:: text

   Device Drivers  --->
     Linux Kernel Summer School Drivers  --->
       [M] LKSS Lab 1 drivers

Press ``M`` to select the module (``M`` = build as loadable module). Save and exit.

**1.3 – Compile the modules**

.. code-block:: bash

   # Using lkss.py – build kernel + all modules, then install modules into rootfs
   python3 scripts/lkss.py compile -j$(nproc) --install-modules

**What --install-modules does under the hood:**

.. code-block:: bash

   cd repos/lkss-linux
   make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- -j$(nproc)

   # Install modules into the rootfs image
   sudo INSTALL_MOD_PATH=<rootfs_mount> make modules_install

.. tip::

   If you only changed a module (not the core kernel), you can rebuild just the module
   to save time:

   .. code-block:: bash

      cd repos/lkss-linux
      make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- \
          M=drivers/lkss/labs/lab1 modules

   Then copy the ``.ko`` to the rootfs manually:

   .. code-block:: bash

      python3 scripts/lkss.py copy \
          repos/lkss-linux/drivers/lkss/labs/lab1/hello.ko \
          /root/

**1.4 Boot the board and load the module**

.. code-block:: bash

   python3 scripts/lkss.py boot

On the board's serial console:

.. code-block:: bash

   # Load the module
   insmod /root/hello.ko

   # Inspect the kernel log
   dmesg | tail -5

You should see something like:

.. code-block:: text

   [  12.345678] Hello, kernel!

**1.5 Unload the module**

.. code-block:: bash

   rmmod hello
   dmesg | tail -5

Verify the exit message appears.

**Questions to answer:**

1. Which function is called when you run ``insmod``? Which when you run ``rmmod``?
2. What happens if you try to ``rmmod`` a module that was never loaded?
3. What does the number in brackets (e.g. ``12.345678``) in ``dmesg`` output represent?

----

Exercise 2 – Printing from the Kernel
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

**Objective**: Understand ``printk`` log levels and how to filter kernel messages.

The kernel provides several logging macros, each corresponding to a severity level:

.. list-table::
   :header-rows: 1
   :widths: 20 20 60

   * - Macro
     - Level
     - When to use
   * - ``pr_emerg()``
     - ``KERN_EMERG``
     - System is unusable
   * - ``pr_alert()``
     - ``KERN_ALERT``
     - Action must be taken immediately
   * - ``pr_crit()``
     - ``KERN_CRIT``
     - Critical conditions
   * - ``pr_err()``
     - ``KERN_ERR``
     - Error conditions
   * - ``pr_warn()``
     - ``KERN_WARNING``
     - Warning conditions
   * - ``pr_notice()``
     - ``KERN_NOTICE``
     - Normal but significant condition
   * - ``pr_info()``
     - ``KERN_INFO``
     - Informational
   * - ``pr_debug()``
     - ``KERN_DEBUG``
     - Debug-level messages

**2.1 Modify hello.c**

Edit ``repos/lkss-linux/drivers/lkss/labs/lab1/hello.c``. Add one ``pr_*`` call at each
severity level inside ``hello_init()``. For example:

.. code-block:: c

   pr_emerg("This is EMERG level\n");
   pr_err("This is ERR level\n");
   pr_warn("This is WARNING level\n");
   pr_info("This is INFO level\n");
   pr_debug("This is DEBUG level\n");

Rebuild the module, copy it to the rootfs, boot, and load it:

.. code-block:: bash

   # On host – rebuild and install
   python3 scripts/lkss.py compile -j$(nproc) --install-modules
   python3 scripts/lkss.py boot

   # On board – load and inspect
   insmod /root/hello.ko

.. code-block:: bash

   # Show all messages with their numeric log level
   dmesg

   # Filter by log level (show only warnings and above)
   dmesg -l warn,err,crit,alert,emerg

**2.2 Dynamic debug**

``pr_debug()`` is silent by default. Enable it for your module at runtime:

.. code-block:: bash

   echo "module hello +p" > /sys/kernel/debug/dynamic_debug/control
   rmmod hello
   insmod /root/hello.ko
   dmesg | tail

**Questions to answer:**

1. Which log levels are shown by default in ``dmesg``?
2. What is the console loglevel and how do you change it?
   Hint: ``cat /proc/sys/kernel/printk``

----

Exercise 3 – Kconfig: Building Drivers into the Image
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

**Objective**: Understand how ``Kconfig`` controls what gets compiled into the kernel,
and the difference between built-in (``=y``) and modular (``=m``) drivers.

**3.1 – Explore the Kconfig file**

Open ``repos/lkss-linux/drivers/lkss/labs/lab1/Kconfig``. Note the structure:

.. code-block:: kconfig

   config LKSS_LAB1_HELLO
       tristate "Hello World module"
       help
         A simple Hello World kernel module for learning purposes.

The ``tristate`` keyword means the option can be:

- ``n`` – not compiled at all
- ``m`` – compiled as a loadable module (``.ko``)
- ``y`` – compiled directly into the kernel Image (built-in)

**3.2 – Build the module as built-in (=y)**

In ``menuconfig``, change the ``Hello World module`` option from ``M`` to ``Y``:

.. code-block:: bash

   python3 scripts/lkss.py menuconfig

Navigate to the LKSS Lab 1 section and press ``Y`` instead of ``M``.

Rebuild and boot:

.. code-block:: bash

   python3 scripts/lkss.py compile -j$(nproc)
   python3 scripts/lkss.py boot

On the board, observe the kernel log during boot:

.. code-block:: bash

   dmesg | grep -i hello

Notice that you do **not** need to run ``insmod`` – the module's ``init`` function ran
automatically as part of the kernel boot sequence. Also notice that ``insmod hello.ko``
will now fail with "File exists" because the module is already built in.

**3.3 – Add your own Kconfig entry**

Create a new file ``repos/lkss-linux/drivers/lkss/labs/lab1/mydriver.c`` with a minimal
module that prints your name. Add an entry for it in the ``Kconfig`` file. Rebuild and test.

**Questions to answer:**

1. What is the advantage of a loadable module over a built-in driver during development?
2. Where in the ``.config`` file is the result of your ``menuconfig`` selections stored?
   Hint: ``grep LKSS repos/lkss-linux/.config``

----

Exercise 4 – Kernel Oops and Fault Handling
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

**Objective**: Observe what happens when kernel code accesses invalid memory. Learn to
read a kernel oops report.

.. warning::

   This exercise deliberately crashes the kernel. This is safe – the board will reboot
   automatically (or you can power-cycle it). No permanent damage occurs.

**4.1 – Explore the source**

Open ``repos/lkss-linux/drivers/lkss/labs/lab1/oops.c``. The module dereferences a NULL
pointer inside its ``init`` function, which will trigger a kernel oops.

**4.2 – Enable and build**

Enable ``CONFIG_LKSS_LAB1_OOPS`` in menuconfig:

.. code-block:: bash

   python3 scripts/lkss.py menuconfig

Navigate to:

.. code-block:: text

   Device Drivers  --->
     Linux Kernel Summer School Drivers  --->
       LKSS Lab 1 drivers  --->
         [M] Exercise 4: Kernel Oops demonstration

Save and exit, then build and install:

.. code-block:: bash

   python3 scripts/lkss.py compile -j$(nproc) --install-modules
   python3 scripts/lkss.py boot

**4.3 – Load the module**

On the board's serial console:

.. code-block:: bash

   insmod /root/oops.ko

Watch the serial console. You will see output similar to:

.. code-block:: text

   [   15.123456] Unable to handle kernel NULL pointer dereference at virtual address 0000000000000000
   [   15.123456] Mem abort info:
   [   15.123456]   ESR = 0x0000000096000004
   [   15.123456]   EC = 0x25: DABT (current EL), IL = 32 bits
   ...
   [   15.123456] Call trace:
   [   15.123456]  oops_init+0x1c/0x30 [oops]
   [   15.123456]  do_one_initcall+0x54/0x1d0
   ...
   [   15.123456] ---[ end trace ]---

**4.4 – Analyze the oops**

Find and understand each part of the oops output:

1. **The faulting address** – the virtual address the kernel tried to access. For a
   NULL dereference it will be ``0x0000000000000000`` (or a small offset from it if
   the code accessed a struct member through a NULL pointer).

2. **The call trace** – the chain of function calls that led to the crash, innermost
   first. You will see ``oops_init`` near the top, followed by the kernel's own
   module-loading machinery (``do_one_initcall``, etc.). This is the primary tool for
   locating the faulty line of code.

3. **The register dump** – the CPU register state at the moment of the fault. The
   ``PC`` (program counter) tells you the exact instruction that faulted. You can
   resolve it to a source line using ``aarch64-linux-gnu-addr2line``.

4. **The ESR (Exception Syndrome Register)** – ARM64-specific field that encodes
   *why* the exception occurred (data abort, instruction abort, alignment fault, etc.).

**Oops vs Panic**:

- An **oops** is a recoverable fault – the kernel kills the offending process (or in our
  case, aborts the module load) and tries to continue.
- A **panic** is unrecoverable – the kernel halts or reboots. A panic is triggered when
  an oops occurs in a context where the kernel cannot safely continue (e.g., interrupt
  context, or when ``panic_on_oops=1``).

**Questions to answer:**

1. After the oops, is the board still functional? Can you still use the serial console?
2. What does the ``PC`` register value tell you? How would you find which line of source
   code corresponds to that address?

----

Exercise 5 – Kernel Timers
~~~~~~~~~~~~~~~~~~~~~~~~~~~

**Objective**: Use the kernel timer API to schedule a function to run after a delay.
Extend it to fire periodically. Observe what happens when a timer callback crashes.

Kernel timers allow you to schedule a callback to run at a future point in time, expressed
as a ``jiffies`` offset. The key functions are:

.. list-table::
   :header-rows: 1
   :widths: 35 65

   * - Function
     - Purpose
   * - ``timer_setup(&t, callback, flags)``
     - Initialize a timer and associate a callback
   * - ``mod_timer(&t, jiffies + msecs_to_jiffies(ms))``
     - Arm (or re-arm) the timer to fire in ``ms`` milliseconds
   * - ``timer_delete_sync(&t)``
     - Cancel the timer and wait for any running callback to finish

A minimal timer module looks like this:

.. code-block:: c

   #include <linux/module.h>
   #include <linux/timer.h>

   static struct timer_list my_timer;

   static void my_timer_callback(struct timer_list *t)
   {
       pr_info("Timer fired!\n");
   }

   static int __init timer_init(void)
   {
       timer_setup(&my_timer, my_timer_callback, 0);
       mod_timer(&my_timer, jiffies + msecs_to_jiffies(1000));
       pr_info("Timer armed for 1 second\n");
       return 0;
   }

   static void __exit timer_exit(void)
   {
       timer_delte_sync(&my_timer);
       pr_info("Timer cancelled\n");
   }

   module_init(timer_init);
   module_exit(timer_exit);
   MODULE_LICENSE("GPL");

**5.1 – Enable and build**

Enable ``CONFIG_LKSS_LAB1_TIMER`` in menuconfig:

.. code-block:: bash

   python3 scripts/lkss.py menuconfig

Navigate to:

.. code-block:: text

   Device Drivers  --->
     Linux Kernel Summer School Drivers  --->
       LKSS Lab 1 drivers  --->
         [M] Exercise 5: Kernel Timer demonstration

Save and exit, then build and install:

.. code-block:: bash

   python3 scripts/lkss.py compile -j$(nproc) --install-modules

**5.2 – Explore the source**

Open ``repos/lkss-linux/drivers/lkss/labs/lab1/timer.c``. Identify:

- How the timer is initialized with ``timer_setup()``
- The ``timeout`` value used in ``mod_timer()`` and how it is expressed in ``jiffies``
- What ``my_timer_callback()`` does

**5.3 – Load and observe**

Boot the board and load the module:

.. code-block:: bash

   python3 scripts/lkss.py boot

   # On board
   insmod /root/timer.ko
   dmesg | tail -5

Verify the "Timer armed" message appears. After one second you should see "Timer fired!".

**5.4 – Make the timer periodic**

The timer fires once and stops. Modify ``my_timer_callback()`` in ``timer.c`` to re-arm
the timer at the end of the callback so it fires every second indefinitely.
The line is already in the file as a comment — uncomment it:

.. code-block:: c

   static void my_timer_callback(struct timer_list *t)
   {
       pr_info("Timer fired!\n");
       mod_timer(&my_timer, jiffies + msecs_to_jiffies(1000));
   }

Rebuild, install, boot, and load:

.. code-block:: bash

   # On host
   python3 scripts/lkss.py compile -j$(nproc) --install-modules
   python3 scripts/lkss.py boot

   # On board – use dmesg -w (watch mode) to see messages appear every second
   insmod /root/timer.ko
   dmesg -w

Unload the module with ``rmmod timer`` and confirm the messages stop.

**5.5 – Crash inside a timer callback**

.. warning::

   This step deliberately crashes the kernel. Power-cycle the board to recover.

Add a NULL pointer dereference inside ``my_timer_callback()`` in ``timer.c``.
The lines are already present as a comment block — uncomment them:

.. code-block:: c

   static void my_timer_callback(struct timer_list *t)
   {
       int *p = NULL;
       pr_info("About to crash...\n");
       *p = 42;    /* NULL deref – triggers kernel oops/panic */
   }

Rebuild, install, and boot:

.. code-block:: bash

   # On host
   python3 scripts/lkss.py compile -j$(nproc) --install-modules
   python3 scripts/lkss.py boot

   # On board
   insmod /root/timer.ko

Load the module and wait for the timer to fire. Compare this oops with the one from
Exercise 4: notice that the call trace now shows the timer softirq context rather than
a direct call from ``insmod``.

**Questions to answer:**

1. What is a ``jiffy``? How many jiffies per second does the kernel use on this board?
   Hint: ``cat /boot/config-* | grep CONFIG_HZ`` or check ``/proc/interrupts``.
2. When the crash happened inside the timer callback, was the board still responsive
   on the serial console? Why might this differ from the Exercise 4 oops?

----

Cheatsheet: Build-Test Cycle
-----------------------------

During the labs, you will repeatedly go through this cycle. Here is a quick reference:

.. list-table::
   :header-rows: 1
   :widths: 40 60

   * - Action
     - Command
   * - Open menuconfig
     - ``python3 scripts/lkss.py menuconfig``
   * - Build kernel + all modules + install to rootfs
     - ``python3 scripts/lkss.py compile -j$(nproc) --install-modules``
   * - Build only one lab's modules (fast)
     - ``cd repos/lkss-linux && make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- M=drivers/lkss/labs/lab1 modules``
   * - Copy a single ``.ko`` to rootfs
     - ``python3 scripts/lkss.py copy <path/to/module.ko> /root/``
   * - Boot the board
     - ``python3 scripts/lkss.py boot``
   * - Load module on board
     - ``insmod /root/<module>.ko``
   * - Unload module on board
     - ``rmmod <module>``
   * - View kernel log
     - ``dmesg | tail -20``
   * - List loaded modules
     - ``lsmod``

----

Resources
---------

- `Linux Kernel Labs – Introduction <https://linux-kernel-labs.github.io/refs/heads/master/labs/introduction.html>`_
- `Linux Kernel Labs – Kernel Modules <https://linux-kernel-labs.github.io/refs/heads/master/labs/kernel_modules.html>`_
- `Linux Kernel Labs – Kernel API <https://linux-kernel-labs.github.io/refs/heads/master/labs/kernel_api.html>`_
- `Elixir Cross-referencer (browse Linux source online) <https://elixir.bootlin.com/linux/latest/source>`_
- `Kernel documentation – Writing kernel modules <https://docs.kernel.org/driver-api/>`_
- `i.MX93 Technical Reference Manual <https://www.nxp.com/docs/en/reference-manual/IMX93RM.pdf>`_
- :ref:`development_board` – FRDM-IMX93 board overview
