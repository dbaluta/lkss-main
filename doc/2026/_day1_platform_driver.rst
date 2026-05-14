Day 1 – Bonus Exercise: Platform Drivers and Device Tree Matching
=================================================================

.. note::

   This exercise was removed from the main Day 1 lab to keep the session within
   4 hours. It is a good fit for Day 2 (after the device tree theory block) or
   as a stretch exercise for fast finishers on Day 1.

   Copy-paste into ``day2.rst`` or offer it as an optional exercise.

----

Exercise – Platform Drivers and Device Tree Matching
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

**Objective**: Understand how the kernel's platform driver model works, and how a driver
binds to a hardware node described in the Device Tree. Read custom properties using the
OF (Open Firmware) API.

Background
^^^^^^^^^^

The **platform driver** model is the standard way to write drivers for on-SoC peripherals
that are not self-discoverable (unlike PCI or USB). The kernel learns about these
peripherals from the **Device Tree**. When the kernel boots, it walks the device tree,
finds nodes whose ``compatible`` property matches a registered driver's ``of_match_table``,
and calls that driver's ``probe()`` function.

.. code-block:: text

   Device Tree node                    Kernel driver
   ─────────────────────               ─────────────────────────────────
   lkss-device {                       static const struct of_device_id lkss_ids[] = {
       compatible = "lkss,foo";    ←──     { .compatible = "lkss,foo" },
       mystring = "hello";                 {}
       myint = <42>;               };
       status = "okay";
   };                              static int sample_probe(struct platform_device *pdev)
                                   {
                                       /* called automatically at boot or module load */
                                   }

Steps
^^^^^

**1 – Explore the driver source**

Open ``repos/lkss-linux/drivers/lkss/labs/lab1/lkss_platform_driver.c``. Identify:

- The ``of_device_id`` table and the ``compatible`` string it declares
- The ``probe()`` function – called when the driver matches a device tree node
- The ``remove()`` function – called when the module is unloaded or the device goes away
- The ``platform_driver_register()`` / ``platform_driver_unregister()`` calls
- How ``of_property_read_string()`` and ``of_property_read_u32()`` are used to read
  device tree properties

**2 – Inspect the Device Tree node**

Open the board's device tree source:

.. code-block:: bash

   cat repos/lkss-linux/arch/arm64/boot/dts/freescale/imx93-11x11-frdm.dts

Search for a node named ``lkss-device`` under the ``simple_bus`` node:

.. code-block:: text

   lkss-device {
       compatible = "lkss,my-lkss-device";
       mystring = "hello";
       myint = <42>;
       status = "okay";
   };

Confirm that the ``compatible`` string in the DTS matches the one in the driver's
``of_device_id`` table. Both must be ``"lkss,my-lkss-device"``. If they differ, update
one to match the other and recompile.

**3 – Enable and build the module**

In ``menuconfig``, enable the platform driver module under the LKSS Lab 1 section:

.. code-block:: bash

   python3 scripts/lkss.py menuconfig

Then build and install:

.. code-block:: bash

   python3 scripts/lkss.py compile -j$(nproc) --install-modules
   python3 scripts/lkss.py boot

**4 – Load the module and observe probe()**

On the board's serial console:

.. code-block:: bash

   insmod /root/lkss_platform_driver.ko
   dmesg | tail -10

You should see the ``probe()`` function printing the values of ``mystring`` and ``myint``
read from the device tree node, e.g.:

.. code-block:: text

   [   18.234567] lkss_platform: probed device lkss-device
   [   18.234590] lkss_platform: mystring = "hello"
   [   18.234601] lkss_platform: myint    = 42

**5 – Unload and verify remove()**

.. code-block:: bash

   rmmod lkss_platform_driver
   dmesg | tail -5

**6 – Add a second device tree node**

Without modifying the driver, add a second ``lkss-device`` node in the DTS with different
property values. Recompile the DTB only:

.. code-block:: bash

   cd repos/lkss-linux
   make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- dtbs

Copy the new DTB to ``output/``:

.. code-block:: bash

   cp arch/arm64/boot/dts/freescale/imx93-11x11-frdm.dtb \
       ../../output/

Boot and load the module. Verify that ``probe()`` is called **twice**.

Questions to answer
^^^^^^^^^^^^^^^^^^^

1. What would happen if the ``compatible`` strings in the driver and DTS do not match?
   How would you diagnose the mismatch?
2. What is the purpose of ``status = "okay"`` vs ``status = "disabled"`` in a DTS node?
3. Why does the platform driver use ``devm_`` prefixed helpers (e.g. ``devm_kzalloc``)?
   What problem do they solve?

Resources
^^^^^^^^^

- `Kernel docs – Platform devices and drivers <https://docs.kernel.org/driver-api/driver-model/platform.html>`_
- `OF API reference <https://elixir.bootlin.com/linux/latest/source/include/linux/of.h>`_
- `Device Tree specification <https://www.devicetree.org/specifications/>`_
