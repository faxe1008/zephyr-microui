MicroUI Watch Sample
####################

Overview
********

This sample demonstrates how far the MicroUI framework can be pushed beyond its
typical use case. While MicroUI is designed as a lightweight immediate-mode GUI
library not typically intended for high-complexity applications, this watch demo
showcases advanced capabilities including complex rendering extensions like
drawing images and arcs.

The sample renders a watch face interface, demonstrating that even with MicroUI's
minimalist design philosophy, visually rich applications are achievable on
resource-constrained embedded devices.

Requirements
************

* Display device (round displays work best)
* Touch input device (optional)

Building and Running
********************

For native simulation:

.. zephyr-app-commands::
   :zephyr-app: samples/watch
   :host-os: unix
   :board: native_sim/native/64
   :goals: run
   :compact:

For ESP32-S3 Touch LCD 1.28:

.. zephyr-app-commands::
   :zephyr-app: samples/watch
   :host-os: unix
   :board: esp32s3_touch_lcd_1_28/esp32s3/procpu
   :goals: flash
   :compact:

