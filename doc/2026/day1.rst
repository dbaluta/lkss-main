.. ============================================================================
.. Day 1 — Introduction to Linux Kernel
.. ============================================================================

Day 1 — Introduction to Linux Kernel
======================================

Slides: :download:`day1_slides <./slides/slides_day1.pdf>`

Description
-----------

In this lab session we will explore the basic elements of Linux Kernel loadable
modules by starting with the usual ``Hello World`` module, then expand into topics
including printing information, the kernel timer API, and observing kernel-level
errors.  We will also learn how to create a minimal embedded Linux image using
Buildroot and how to use it to boot the i.MX93 FRDM board.

By the end of the day, everyone should be able to write, build, and interact with
kernel modules, and have a working custom root filesystem image on the board.

----

Topics Covered
--------------

- Anatomy of a simple Linux kernel module
- Kernel module **compilation and loading/unloading** (``insmod``, ``rmmod``)
- Using ``printk`` and examining kernel logs via the serial console and ``dmesg``
- Exploring the Linux kernel timer API for delayed and periodic execution
- Observing **errors** at kernel level (``oops`` vs ``panic``)
- Using **Buildroot** to create a minimal embedded Linux root filesystem image

----

Before You Start
----------------

.. note::

   Before starting these exercises, make sure your **exercises repository** is
   up to date.  All lab source files are located under
   ``drivers/lkss/labs/lab1/``.

   **If you already have the repository**

   If you are using the virtual machine described in the
   :doc:`Infrastructure page <../infrastructure>`, the repository is already
   cloned.

   .. code-block:: bash

      $ cd ~/linux
      $ git pull origin main

   Make sure you are on the ``main`` branch.

   **If you do not have the repository yet**

   .. code-block:: bash

      $ git clone https://github.com/Linux-Kernel-Summer-School/linux.git
      $ cd linux

   Navigate to ``drivers/lkss/labs/lab1/`` to begin your work.

----

Environment Setup
-----------------

Source the cross-compilation environment in every terminal you open for kernel
or module compilation.

.. code-block:: bash

   $ cat ~/setenv.sh
   export ARCH=arm64
   export CROSS_COMPILE=aarch64-linux-gnu-

   $ source ~/setenv.sh

----

Kernel Image Compilation
-------------------------

.. note::
   This step only needs to be done **once** per lab day.  If you already have
   an ``Image`` and ``dtb`` from a previous session you can skip straight to
   `Kernel Module Compilation`_.

1. **Load the default board configuration**

   .. code-block:: bash

      $ cd ~/linux
      $ make imx93frdm_defconfig

2. **Compile the kernel Image and Device Tree Blob**

   .. code-block:: bash

      $ make -j$(nproc)

   The build produces:

   - Kernel image: ``arch/arm64/boot/Image``
   - Device Tree: ``arch/arm64/boot/dts/freescale/imx93-11x11-frdm.dtb``

----

Kernel Module Compilation
--------------------------

1. **Select the lab modules in Kconfig**

   .. code-block:: bash

      $ cd ~/linux
      $ make menuconfig

   Navigate to **Device Drivers → Linux Kernel Summer School Drivers** and
   select all ``lab1`` entries with ``M`` (build as module).

2. **Compile the selected modules**

   .. code-block:: bash

      $ make M=drivers/lkss/labs/lab1 modules

   This command only re-compiles the files under ``lab1/``, making the
   edit-compile-test cycle fast.

----

Building a Root Filesystem Image with Buildroot
------------------------------------------------

Buildroot is a tool that automates building a complete embedded Linux system:
cross-toolchain, libraries, BusyBox userland, and a flashable root filesystem
image — all configured through a single ``make menuconfig`` interface.

1. **Download and extract Buildroot**

   .. code-block:: bash

      $ cd ~
      $ wget https://buildroot.org/downloads/buildroot-2024.02.tar.gz
      $ tar xf buildroot-2024.02.tar.gz
      $ cd buildroot-2024.02

2. **Load the i.MX93 FRDM board configuration**

   .. code-block:: bash

      $ make imx93_11x11_lpddr4x_evk_defconfig

   If a school-provided configuration is available, use it instead:

   .. code-block:: bash

      $ cp ~/lkss-utils/configs/imx93_frdm_defconfig configs/
      $ make imx93_frdm_defconfig

3. **Browse the configuration (do not change anything yet)**

   .. code-block:: bash

      $ make menuconfig

   Explore the following sections to understand what Buildroot manages:

   - **Target options** — architecture and ABI
   - **Toolchain** — GCC version, C library
   - **Kernel** — which kernel version and defconfig Buildroot will build
   - **Target packages** — userspace programs compiled for the target
   - **Filesystem images** — output image format (``ext4``, ``sdcard.img``, …)

   Exit without saving (``Esc Esc`` → *No*).

4. **Add the** ``i2c-tools`` **package**

   We will need this package on Day 3.  Enable it now:

   .. code-block:: bash

      $ make menuconfig

   Navigate to **Target packages → Hardware handling → i2c-tools**, press
   ``Space`` to select, then save and exit.

5. **Build the image**

   .. code-block:: bash

      $ make -j$(nproc) 2>&1 | tee build.log

   The first build takes 30–90 minutes.  Subsequent builds are much faster
   because Buildroot caches downloads in ``dl/`` and build objects in
   ``output/``.

6. **Inspect the output artefacts**

   .. code-block:: bash

      $ ls -lh output/images/
      # Image  imx93-11x11-evk.dtb  rootfs.ext4  u-boot.bin  sdcard.img

7. **Flash the image to the SD card**

   .. warning::
      Use ``lsblk`` to identify your SD card device **before** running ``dd``.
      Writing to the wrong device destroys data permanently.

   .. code-block:: bash

      $ lsblk          # identify the SD card, e.g. /dev/sdb or /dev/mmcblk0

      $ sudo dd if=output/images/sdcard.img of=/dev/sdX \
                bs=4M conv=fsync status=progress
      $ sync

----

Preparing the Board Boot
-------------------------

At this point you have a kernel ``Image``, a ``dtb``, and a ``rootfs``.
Before booting we install the freshly compiled modules into the rootfs so the
board will find them at runtime.

1. **Clone the lkss-utils repository** (if not done already)

   .. code-block:: bash

      $ git clone https://github.com/Linux-Kernel-Summer-School/lkss-utils.git

2. **Install the compiled modules into the rootfs**

   .. code-block:: bash

      $ cd ~/lkss-utils/2025
      $ ./rootfs_util modules_install ./rootfs.ext2 ~/linux/

   This copies every ``.ko`` file (with correct directory structure and
   ``modules.dep``) into the ``rootfs.ext2`` image.

3. **Open the serial console**

   .. code-block:: bash

      $ minicom -D /dev/ttyACM0

4. **Boot the board**

   .. code-block:: bash

      $ cd ~/lkss-utils/2025
      $ ./boot_imx93.sh \
            ~/linux/arch/arm64/boot/Image \
            ~/linux/arch/arm64/boot/dts/freescale/imx93-11x11-frdm.dtb \
            ./rootfs.ext2

   Watch the boot log on the serial console and log in as ``root`` when the
   prompt appears.

----

Exercise 1: A Simple Hello World Kernel Module
------------------------------------------------

In this exercise you will load your first kernel module and observe the two
lifecycle functions that every module must implement.

1. **Explore the source code**

   Open ``drivers/lkss/labs/lab1/hello.c`` and identify:

   - The ``module_init`` and ``module_exit`` macros
   - The function called when the module is loaded
   - The function called when the module is unloaded
   - Which kernel function is used to print the message

2. **Build the module**

   .. code-block:: bash

      $ cd ~/linux
      $ make M=drivers/lkss/labs/lab1 modules

3. **Update the rootfs and reboot**

   .. code-block:: bash

      $ cd ~/lkss-utils/2025
      $ ./rootfs_util modules_install ./rootfs.ext2 ~/linux/

   Reboot the board with the updated rootfs.

4. **Load the module on the target**

   .. code-block:: bash

      # insmod hello.ko

   Observe which function is called and what appears on the serial console.

5. **Unload the module**

   .. code-block:: bash

      # rmmod hello

   Notice which function is called at unload time.

6. **Inspect the kernel log**

   .. code-block:: bash

      # dmesg | tail

   Confirm both the load and unload messages are present.

----

Exercise 2: Kernel Oops and Fault Handling
-------------------------------------------

In this exercise you will explore what happens when something goes wrong
inside kernel space — such as dereferencing a NULL pointer.  You will trigger
a kernel oops and learn to read the resulting output.

1. **Explore the source code**

   Open ``drivers/lkss/labs/lab1/oops.c``.  Read through the code to
   understand what it does to deliberately trigger an error in kernel space.

2. **Build, install, and load the module**

   .. code-block:: bash

      $ make M=drivers/lkss/labs/lab1 modules

   Install the updated modules, reboot, then on the target:

   .. code-block:: bash

      # insmod oops.ko

3. **Analyse the oops output**

   Examine the output from ``dmesg`` or the serial console and identify:

   - The **stack trace** showing where the error occurred
   - The **register dump** showing the CPU state at the time of the fault
   - The name of the **function** that caused the crash
   - Whether the system recovered (oops) or halted (panic)

----

Exercise 3: Working with Kernel Timers
----------------------------------------

In this exercise you will use the Linux kernel timer API to schedule deferred
work, and then observe what happens when a timer callback crashes.

1. **Explore the source code**

   Open ``drivers/lkss/labs/lab1/timer.c`` and identify:

   - How the ``timer_list`` structure is initialised with ``timer_setup``
   - How the timer expiry time is set using ``jiffies`` and ``HZ``
   - Where the timer is armed with ``mod_timer``
   - How the timer is safely cancelled on module unload with ``del_timer_sync``

2. **Modify the callback to repeat**

   Locate the ``my_timer_callback`` function.  Extend it to **re-arm the
   timer** at the end of every callback so it fires at a regular interval.

   .. code-block:: c

      static void my_timer_callback(struct timer_list *t)
      {
          pr_info("timer: fired!\n");
          /* Add the line below to make it periodic */
          mod_timer(&my_timer, jiffies + HZ);
      }

   Build, install, and load the module.  Run ``dmesg -w`` on the target to
   confirm the timer fires once per second.

3. **Trigger a kernel error inside the callback**

   Add a deliberate NULL pointer dereference inside ``my_timer_callback``
   **after** verifying the periodic behaviour above:

   .. code-block:: c

      int *bad_ptr = NULL;
      *bad_ptr = 42;   /* this will crash the kernel */

   Observe whether the system produces an oops, a panic, or something else.
   Note how the crash output differs from Exercise 2 — specifically, look at
   the interrupt context shown in the stack trace.

   .. warning::
      This step **will** crash or hang the board.  You will need to reboot and
      restore a working version of the module before continuing.

----

Resources
---------

- `Linux Kernel Labs — Introduction <https://linux-kernel-labs.github.io/refs/heads/master/labs/introduction.html>`_
- `Linux Kernel Labs — Basics of writing a kernel module <https://linux-kernel-labs.github.io/refs/heads/master/labs/kernel_modules.html>`_
- `Linux Kernel Labs — Kernel API <https://linux-kernel-labs.github.io/refs/heads/master/labs/kernel_api.html>`_
- `Buildroot user manual <https://buildroot.org/downloads/manual/manual.html>`_
