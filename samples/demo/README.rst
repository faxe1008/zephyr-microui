MicroUI Demo Sample
###################

Overview
********

This sample demonstrates the MicroUI library with a simple GUI interface.

Requirements
************

* Display device
* Input device (optional)

Building and Running
********************

The demo can be built as follows:

.. zephyr-app-commands::
   :zephyr-app: samples/demo
   :host-os: unix
   :board: native_sim
   :goals: run
   :compact:

The demo will display various UI elements and respond to input events.

Configuration
*************

Key configuration options:

* ``CONFIG_DISPLAY=y`` - Enable display support
* ``CONFIG_INPUT=y`` - Enable input support (optional)
