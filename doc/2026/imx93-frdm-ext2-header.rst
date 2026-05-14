i.MX93 FRDM – EXT2 Connector (J601) Pin Allocation
====================================================

The EXT2 connector (J601) is a 2×20 pin header on the NXP i.MX93 11×11 FRDM
board. It exposes a set of i.MX93 GPIO_IO pads (all in the **GPIO2** bank) plus
power and ground rails. The layout follows the Raspberry Pi 40-pin header
convention (odd pins on the left, even pins on the right).

LPI2C4 Pin Allocation
---------------------

The ``lpi2c4`` controller is routed through the EXT2 header and pinmuxed in
``pinctrl_lpi2c4`` (pad config ``0x40000b9e`` — open-drain with pull-up for I2C).

.. list-table::
   :header-rows: 1
   :widths: 10 15 20 20 35

   * - Pin
     - Signal
     - SoC Pad
     - GPIO
     - Function
   * - 3
     - SDA
     - GPIO_IO02
     - GPIO2_IO02
     - LPI2C4_SDA
   * - 5
     - SCL
     - GPIO_IO03
     - GPIO2_IO03
     - LPI2C4_SCL

LPSPI3 Pin Allocation
---------------------

The ``lpspi3`` controller is pinmuxed in ``pinctrl_lpspi3``.

.. list-table::
   :header-rows: 1
   :widths: 10 15 20 20 35

   * - Pin
     - Signal
     - SoC Pad
     - GPIO
     - Function
   * - 23
     - SCK
     - GPIO_IO11
     - GPIO2_IO11
     - LPSPI3_SCK
   * - 19
     - MOSI
     - GPIO_IO10
     - GPIO2_IO10
     - LPSPI3_SOUT
   * - 21
     - MISO
     - GPIO_IO09
     - GPIO2_IO09
     - LPSPI3_SIN (not connected)
   * - 24
     - CS0
     - GPIO_IO08
     - GPIO2_IO08
     - LPSPI3_PCS0

GPIO LEDs
---------

Three GPIO-controlled LEDs are allocated on the EXT2 header. Active-high: each
GPIO pad drives the LED anode through a 220 Ω series resistor; cathode to GND.

.. list-table::
   :header-rows: 1
   :widths: 10 15 20 20 35

   * - Pin
     - Label
     - SoC Pad
     - GPIO
     - Function
   * - 26
     - LED0
     - GPIO_IO07
     - GPIO2_IO07
     - Red LED (active-high)
   * - 7
     - LED1
     - GPIO_IO04
     - GPIO2_IO04
     - Green LED (active-high)
   * - 12
     - LED2
     - GPIO_IO18
     - GPIO2_IO18
     - Blue LED (active-high)

GPIO Buttons
------------

Four GPIO push-buttons are allocated on the EXT2 header. Active-low: pressing
pulls the line to GND. **External pull-ups to 3V3 are required.**

.. list-table::
   :header-rows: 1
   :widths: 10 15 20 20 35

   * - Pin
     - Label
     - SoC Pad
     - GPIO
     - Key
   * - 29
     - BTN1
     - GPIO_IO05
     - GPIO2_IO05
     - Button 1
   * - 31
     - BTN2
     - GPIO_IO06
     - GPIO2_IO06
     - Button 2
   * - 27
     - BTN3
     - GPIO_IO00
     - GPIO2_IO00
     - Button 3
   * - 28
     - BTN4
     - GPIO_IO01
     - GPIO2_IO01
     - Button 4

Full 2×20 Header Map
--------------------

Odd pins are on the left column, even pins on the right.

.. list-table::
   :header-rows: 1
   :widths: 8 35 8 35

   * - Pin
     - Signal
     - Pin
     - Signal
   * - 1
     - VRPi_3V3
     - 2
     - VRPi_5V
   * - 3
     - GPIO_IO02 — LPI2C4_SDA
     - 4
     - VRPi_5V
   * - 5
     - GPIO_IO03 — LPI2C4_SCL
     - 6
     - GND
   * - 7
     - GPIO_IO04 — LED1 green
     - 8
     - GPIO_IO14 (free)
   * - 9
     - GND
     - 10
     - GPIO_IO15 (free)
   * - 11
     - GPIO_IO17 (ST7789 D/C)
     - 12
     - GPIO_IO18 — LED2 blue
   * - 13
     - GPIO_IO27 (free)
     - 14
     - GND
   * - 15
     - GPIO_IO22 (free)
     - 16
     - GPIO_IO23 (free)
   * - 17
     - VRPi_3V3
     - 18
     - GPIO_IO24 (free)
   * - 19
     - GPIO_IO10 — LPSPI3_MOSI
     - 20
     - GND
   * - 21
     - GPIO_IO09 — LPSPI3_MISO (nc)
     - 22
     - GPIO_IO25 (free)
   * - 23
     - GPIO_IO11 — LPSPI3_SCK
     - 24
     - GPIO_IO08 — LPSPI3_CS0
   * - 25
     - GND
     - 26
     - GPIO_IO07 — LED0 red
   * - 27
     - GPIO_IO00 — BTN3
     - 28
     - GPIO_IO01 — BTN4
   * - 29
     - GPIO_IO05 — BTN1
     - 30
     - GND
   * - 31
     - GPIO_IO06 — BTN2
     - 32
     - GPIO_IO12 (free)
   * - 33
     - GPIO_IO13 — SPI8_SIN
     - 34
     - GND
   * - 35
     - GPIO_IO19 (free)
     - 36
     - GPIO_IO16 (free)
   * - 37
     - GPIO_IO26 (free)
     - 38
     - GPIO_IO20 (free)
   * - 39
     - GND
     - 40
     - GPIO_IO21 (free)

Device Tree References
----------------------

All pinctrl groups and device nodes are defined in
``arch/arm64/boot/dts/freescale/imx93-11x11-frdm.dts``:

- ``pinctrl_lpi2c4`` — LPI2C4 SDA/SCL (``0x40000b9e``)
- ``pinctrl_lpspi3`` — LPSPI3 bus (``0x31e``)
- ``pinctrl_lkss_gpio`` — LED0/1/2 outputs + BTN1/2/3/4 inputs (``0x31e``)
