.. _day4:
.. _i2c-bus-and-the-bmp280-sensor-driver:

Day 4 – I2C Bus and the BMP280 Sensor Driver
=============================================

Slides: `Day 4 – I2C Bus and the BMP280 Sensor Driver`_

**Topics**

- I2C protocol: open-drain bus, 7-bit addressing, START/STOP conditions, ACK/NACK
- Linux I2C subsystem: ``i2c_adapter``, ``i2c_client``, ``i2c_driver``
- Writing an I2C driver from scratch: ``probe``, ``remove``, register read/write helpers
- Device tree: I2C controller nodes, sensor child nodes, ``reg`` property
- BMP280 sensor architecture: chip ID, calibration registers, ADC output registers
- BMP280 compensation formulas: converting raw ADC values to temperature (°C) and
  pressure (hPa)
- Exposing sensor data to userspace via ``sysfs`` attributes
- Introduction to the IIO (Industrial I/O) subsystem

**Goal**

By the end of this lab you will be able to:

- Scan an I2C bus and identify connected devices using ``i2cdetect``
- Write an I2C driver that reads registers from the BMP280 sensor
- Parse the BMP280 calibration data and apply the compensation formulas
- Expose temperature and pressure readings via ``sysfs`` attributes
- Understand how the IIO subsystem provides a standardized sensor interface

----

Theory
------

I2C Protocol Fundamentals
~~~~~~~~~~~~~~~~~~~~~~~~~~

TODO: Explain the I2C physical layer: two open-drain wires (SDA, SCL), pull-up
resistors, multi-master capability, and clock stretching.  Cover the transaction
structure: START → device address + R/W bit → ACK → data bytes → STOP.

Key concepts to cover:

- 7-bit address space (0x00–0x7F); BMP280 default address is ``0x76`` (SDO to GND) or
  ``0x77`` (SDO to VDDIO)
- Write transaction: master sends address + W bit, then register address, then data
- Read transaction (register read): write register address (dummy write), repeated
  START, address + R bit, read data bytes, NAK + STOP
- ACK (pulled low by receiver) vs NACK (bus released, stays high)
- I2C speeds: Standard (100 kHz), Fast (400 kHz), Fast+ (1 MHz)
- The i.MX93 LPI2C controller and which I2C buses are routed to the EXT2 header

The Linux I2C Subsystem
~~~~~~~~~~~~~~~~~~~~~~~

TODO: Explain the three layers: I2C controller driver (``i2c_adapter``), I2C core, and
I2C device driver (``i2c_driver`` / ``i2c_client``).

Key structs and APIs to cover:

.. list-table::
   :header-rows: 1
   :widths: 35 65

   * - API
     - Purpose
   * - ``struct i2c_client``
     - Represents one I2C device; holds ``addr``, ``adapter``, ``dev``
   * - ``i2c_smbus_read_byte_data(client, reg)``
     - Read one byte from register ``reg``
   * - ``i2c_smbus_write_byte_data(client, reg, val)``
     - Write one byte to register ``reg``
   * - ``i2c_smbus_read_i2c_block_data(client, reg, len, buf)``
     - Burst-read ``len`` bytes starting at ``reg``
   * - ``i2c_master_send(client, buf, count)``
     - Raw write of ``count`` bytes (no SMBus framing)
   * - ``i2c_master_recv(client, buf, count)``
     - Raw read of ``count`` bytes

Device Tree: I2C Nodes and Sensor Bindings
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

TODO: Show a real i.MX93 I2C controller node and a child node for the BMP280.  Explain
the ``reg`` property (7-bit I2C address without the R/W bit), ``clock-frequency``, and
``status``.

Example DTS skeleton:

.. code-block:: devicetree

   &lpi2cX {      /* TODO: identify the correct I2C bus on the EXT2 header */
       #address-cells = <1>;
       #size-cells = <0>;
       clock-frequency = <400000>;    /* Fast mode: 400 kHz */
       pinctrl-0 = <&pinctrl_lpi2cX>;
       pinctrl-names = "default";
       status = "okay";

       bmp280: pressure@76 {
           compatible = "lkss,bmp280";
           reg = <0x76>;              /* I2C address: SDO tied to GND */
           /* vddd-supply = <&reg_3v3>; optional regulator reference */
       };
   };

   &iomuxc {
       pinctrl_lpi2cX: lpi2cX-grp {
           fsl,pins = <
               /* TODO: real iomux entries for SCL and SDA */
           >;
       };
   };

The BMP280 Sensor
~~~~~~~~~~~~~~~~~

TODO: Introduce the BMP280: a Bosch digital pressure and temperature sensor.  Explain
the register map at a high level.

Key register addresses (from the BMP280 datasheet):

.. list-table::
   :header-rows: 1
   :widths: 15 15 70

   * - Register
     - Address
     - Purpose
   * - ``id``
     - 0xD0
     - Chip ID — always reads ``0x58`` for BMP280
   * - ``reset``
     - 0xE0
     - Write ``0xB6`` to trigger a soft reset
   * - ``status``
     - 0xF3
     - Bit 3: ``measuring``; bit 0: ``im_update``
   * - ``ctrl_meas``
     - 0xF4
     - Oversampling for temperature/pressure, power mode
   * - ``config``
     - 0xF5
     - Standby time, IIR filter coefficient, SPI 3-wire mode
   * - ``press_msb/lsb/xlsb``
     - 0xF7–0xF9
     - Raw 20-bit ADC pressure value
   * - ``temp_msb/lsb/xlsb``
     - 0xFA–0xFC
     - Raw 20-bit ADC temperature value
   * - ``calib00–calib25``
     - 0x88–0x9F
     - 26 bytes of factory calibration coefficients

Calibration and Compensation
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

TODO: Explain why raw ADC values are not directly usable and how the BMP280 stores
per-chip factory calibration data.  Walk through the Bosch-provided compensation
formulas (from datasheet section 4.2.3).

The calibration registers define 12 unsigned/signed 16-bit coefficients:
``dig_T1..T3`` (temperature) and ``dig_P1..P9`` (pressure).

Temperature compensation (produces ``t_fine`` needed by pressure):

.. code-block:: c

   /* All variables are s32 (signed 32-bit) unless noted */
   s32 var1, var2, t_fine;

   var1 = ((((adc_T >> 3) - ((s32)dig_T1 << 1))) * ((s32)dig_T2)) >> 11;
   var2 = (((((adc_T >> 4) - (s32)dig_T1) *
             ((adc_T >> 4) - (s32)dig_T1)) >> 12) * (s32)dig_T3) >> 14;
   t_fine = var1 + var2;
   /* Temperature in 0.01 °C units: */
   s32 temperature = (t_fine * 5 + 128) >> 8;

Pressure compensation (requires ``t_fine`` from above):

.. code-block:: c

   /* Uses s64 (signed 64-bit) for intermediate products */
   s64 var1p, var2p, pressure;

   var1p = ((s64)t_fine) - 128000;
   var2p = var1p * var1p * (s64)dig_P6;
   /* ... (full formula in datasheet section 4.2.3 – TODO: paste here) ... */
   /* Pressure in Pa units (divide by 256 to get integer Pa) */

Sysfs Attributes
~~~~~~~~~~~~~~~~~

TODO: Explain how to expose sensor readings to userspace by creating read-only
``sysfs`` attributes with ``DEVICE_ATTR_RO`` and ``sysfs_create_group()``.

Introduction to the IIO Subsystem
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

TODO: Give a brief overview of the IIO (Industrial I/O) subsystem: the standardized
kernel interface for sensors (ADCs, IMUs, pressure sensors, etc.).  Contrast writing a
plain sysfs driver (what we do in this lab) with a proper IIO driver (the approach used
by the upstream ``bmp280`` driver in ``drivers/iio/pressure/bmp280-i2c.c``).

Key concepts to cover:

- ``/sys/bus/iio/devices/iio:deviceX/`` — the standard IIO sysfs path
- ``in_pressure_input``, ``in_temp_input`` — standard IIO channel names
- Why the upstream driver is not used in this lab: writing our own teaches the I2C
  stack without the IIO abstraction getting in the way; Day 5 hackathon students may
  choose to use the upstream driver as a shortcut

----

Lab Exercises
-------------

.. note::

   Lab source code is under ``repos/lkss-linux/drivers/lkss/labs/lab4/``.
   Enable ``CONFIG_LKSS_LAB4`` in menuconfig before building.

   Hardware setup for today:

   - Wire the **BMP280 module** to the EXT2 expansion header via I2C:
     VCC → 3.3 V, GND → GND, SCL → the I2C clock pin, SDA → the I2C data pin,
     SDO → GND (sets I2C address to 0x76).
   - Keep all previous hardware (LEDs, buttons, ST7789) connected.
   - See :ref:`imx93-frdm-ext2-header` for the exact pin assignments.

Exercise 1 – Scan the I2C Bus
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

**Objective**: Verify the BMP280 is electrically connected and determine its I2C
address before writing any driver code.

Sub-tasks:

1. Identify the I2C bus number that corresponds to the EXT2 header pins.

   .. code-block:: bash

      ls /sys/bus/i2c/devices/

2. Scan the bus with ``i2cdetect``:

   .. code-block:: bash

      i2cdetect -y <bus_number>

   You should see a device at address ``0x76`` (or ``0x77`` if SDO is tied high).

3. Manually read the chip ID register (``0xD0``) with ``i2cget``:

   .. code-block:: bash

      i2cget -y <bus_number> 0x76 0xD0

   Confirm the returned value is ``0x58``.

4. Dump all BMP280 registers:

   .. code-block:: bash

      i2cdump -y <bus_number> 0x76

**Questions to answer:**

1. Why does ``i2cdetect`` use write-probes rather than read-probes?
2. What does the ``0x58`` chip ID tell you?

----

Exercise 2 – Add the BMP280 Device Tree Node
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

**Objective**: Describe the BMP280 to the kernel via the device tree so the driver can
bind to it.

Sub-tasks:

1. Open the FRDM board DTS file:
   ``repos/lkss-linux/arch/arm64/boot/dts/freescale/imx93-11x11-frdm.dts``.
2. Add the ``&lpi2cX`` controller node with ``status = "okay"`` and the pinctrl
   reference.
3. Add the ``bmp280`` child node (use the DTS skeleton from the Theory section).
4. Fill in the correct I2C bus number and GPIO iomux entries.
5. Rebuild the DTB and boot:

   .. code-block:: bash

      cd repos/lkss-linux
      make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- \
          freescale/imx93-11x11-frdm.dtb
      python3 scripts/lkss.py boot

6. On the board, verify the device node appears:

   .. code-block:: bash

      ls /sys/bus/i2c/devices/

----

Exercise 3 – Minimal I2C Driver: probe and remove
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

**Objective**: Write a driver skeleton that binds to the BMP280 device tree node.

Sub-tasks:

1. Open ``repos/lkss-linux/drivers/lkss/labs/lab4/bmp280.c``.
2. Fill in the ``of_match_table`` with the ``"lkss,bmp280"`` compatible string and an
   ``i2c_device_id`` table entry for ``"bmp280"``.
3. In ``probe``, print:

   .. code-block:: c

      dev_info(&client->dev, "BMP280 driver bound at address 0x%02x\n",
               client->addr);

4. In ``remove``, print a goodbye message.
5. Enable the driver in menuconfig, build, install, and boot.
6. Verify on the board:

   .. code-block:: bash

      dmesg | grep bmp280

----

Exercise 4 – Read the Chip ID Register
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

**Objective**: Verify SDA/SCL wiring is correct by reading the known chip ID.

Sub-tasks:

1. In ``probe``, read register ``0xD0`` with:

   .. code-block:: c

      int id = i2c_smbus_read_byte_data(client, 0xD0);
      if (id < 0) {
          dev_err(&client->dev, "Failed to read chip ID: %d\n", id);
          return id;
      }
      dev_info(&client->dev, "Chip ID: 0x%02x (expected 0x58)\n", id);
      if (id != 0x58)
          return -ENODEV;

2. Rebuild, boot, and verify the correct chip ID appears in ``dmesg``.

----

Exercise 5 – Configure the Sensor and Read Raw ADC Values
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

**Objective**: Configure the BMP280 for forced-mode measurement and read the raw ADC
output registers.

Sub-tasks:

1. Implement ``bmp280_configure(client)``:
   - Write ``ctrl_meas`` (0xF4) = ``0x27``:
     temperature oversampling ×1 (bits 7:5 = 001), pressure oversampling ×1
     (bits 4:2 = 001), forced mode (bits 1:0 = 01)
   - Forced mode triggers one measurement and then returns to sleep
2. Implement ``bmp280_read_raw(client, adc_t, adc_p)``:
   - Burst-read 6 bytes starting at ``0xF7`` (``press_msb``):

     .. code-block:: c

        u8 buf[6];
        i2c_smbus_read_i2c_block_data(client, 0xF7, 6, buf);
        *adc_p = ((s32)buf[0] << 12) | ((s32)buf[1] << 4) | (buf[2] >> 4);
        *adc_t = ((s32)buf[3] << 12) | ((s32)buf[4] << 4) | (buf[5] >> 4);

3. Call these functions from ``probe`` and print the raw values.
4. Trigger a second forced-mode read by writing ``ctrl_meas`` again (the mode bits
   revert to ``00`` after the measurement completes).

----

Exercise 6 – Read Calibration Data and Apply Compensation
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

**Objective**: Parse the factory calibration registers and compute temperature (°C) and
pressure (hPa) from the raw ADC values.

Sub-tasks:

1. Define a ``struct bmp280_calib`` with fields ``dig_T1``…``dig_T3`` (u16, s16, s16)
   and ``dig_P1``…``dig_P9`` (u16, s16×8).
2. Implement ``bmp280_read_calib(client, calib)``:
   - Burst-read 26 bytes starting at ``0x88``
   - Parse little-endian 16-bit values (``get_unaligned_le16()``)
3. Implement ``bmp280_compensate_temp(calib, adc_t, t_fine)`` using the formula from
   the Theory section above; return temperature in 0.01 °C units.
4. Implement ``bmp280_compensate_press(calib, adc_p, t_fine)`` using the 64-bit
   formula from the BMP280 datasheet; return pressure in Pa × 256 units, convert to
   hPa (divide by 25 600).
5. Call both functions in ``probe`` and print:

   .. code-block:: text

      BMP280: temperature = 25.34 °C, pressure = 1013.25 hPa

----

Exercise 7 – Expose Readings via sysfs
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

**Objective**: Make temperature and pressure readable from userspace through sysfs
attributes that trigger a fresh measurement on each read.

Sub-tasks:

1. Allocate a ``struct bmp280_data`` (containing ``i2c_client *``, calibration, and
   ``t_fine``) in ``probe`` and store it with ``i2c_set_clientdata()``.
2. Define two ``DEVICE_ATTR_RO`` attributes: ``temperature`` and ``pressure``.
3. In each ``show`` callback:
   a. Trigger a forced-mode measurement (write ``ctrl_meas``)
   b. Wait for measurement completion — poll ``status`` register (0xF3) bit 3 or
      simply ``msleep(10)``
   c. Read raw ADC values and apply compensation
   d. Print the result in human-readable format (e.g. ``"25.34\n"``)
4. Register the attributes with ``sysfs_create_group()`` in ``probe``; remove with
   ``sysfs_remove_group()`` in ``remove``.
5. On the board, test:

   .. code-block:: bash

      cat /sys/bus/i2c/devices/<busnum>-0076/temperature
      cat /sys/bus/i2c/devices/<busnum>-0076/pressure

----

Exercise 8 – Periodic Sampling with a Kernel Timer (Bonus)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

**Objective** (stretch goal): Use the kernel timer API (from Day 1, Exercise 5) to
sample the BMP280 every second and log the readings to the kernel log.

Sub-tasks:

1. Add a ``struct timer_list poll_timer`` to ``struct bmp280_data``.
2. Initialize and arm the timer in ``probe`` with a 1-second period.
3. In the timer callback, schedule a ``work_struct`` (use ``schedule_work()``) because
   I2C transfers cannot be done from interrupt/timer context.
4. In the work handler, trigger a measurement and log the result with ``dev_info()``.
5. In ``remove``, cancel the timer with ``timer_delete_sync()`` and flush the
   workqueue with ``cancel_work_sync()``.

**Questions to answer:**

1. Why can you not call ``i2c_smbus_read_byte_data()`` directly from a timer callback?
2. What is the correct cancellation order for the timer and the work item?  Why does
   order matter?

----

Cheatsheet: I2C and BMP280 APIs
---------------------------------

.. list-table::
   :header-rows: 1
   :widths: 45 55

   * - Function / Macro
     - Purpose
   * - ``i2c_smbus_read_byte_data(client, reg)``
     - Read one byte from register ``reg``; returns the byte or ``< 0`` on error
   * - ``i2c_smbus_write_byte_data(client, reg, val)``
     - Write one byte to register ``reg``
   * - ``i2c_smbus_read_i2c_block_data(client, reg, len, buf)``
     - Burst-read ``len`` bytes starting at ``reg`` into ``buf``
   * - ``i2c_set_clientdata(client, data)``
     - Store driver private data pointer in the ``i2c_client``
   * - ``i2c_get_clientdata(client)``
     - Retrieve driver private data pointer
   * - ``get_unaligned_le16(ptr)``
     - Read a little-endian 16-bit value from an unaligned byte pointer
   * - ``DEVICE_ATTR_RO(name)``
     - Declare a read-only sysfs attribute; implement ``name_show()``
   * - ``sysfs_create_group(kobj, grp)``
     - Register a group of sysfs attributes
   * - ``module_i2c_driver(drv)``
     - Register + unregister an ``i2c_driver`` in one macro

----

Resources
---------

- `Linux kernel I2C documentation <https://docs.kernel.org/i2c/index.html>`_
- `BMP280 datasheet <https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bmp280-ds001.pdf>`_ –
  register map (section 4), compensation formulas (section 4.2.3)
- `Upstream BMP280 IIO driver <https://elixir.bootlin.com/linux/latest/source/drivers/iio/pressure/bmp280-core.c>`_ –
  reference implementation (study after completing the exercises)
- `Linux IIO subsystem documentation <https://docs.kernel.org/driver-api/iio/index.html>`_
- `Linux Kernel Labs – I2C <https://linux-kernel-labs.github.io/refs/heads/master/labs/i2c.html>`_
- :ref:`imx93-frdm-ext2-header` – expansion header pin map
- :ref:`development_board` – FRDM-IMX93 board overview
