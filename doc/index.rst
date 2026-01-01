.. _microui-home:

MicroUI for Zephyr
##################

Welcome to the MicroUI for Zephyr documentation!

MicroUI is a lightweight immediate-mode GUI library designed for resource-constrained
embedded systems running Zephyr RTOS. This port is based on `rxi/microui`_ and extends
it with Zephyr-specific integrations and additional features.

.. _rxi/microui: https://github.com/rxi/microui

Demo
****

Check out this demo video showcasing the capabilities of MicroUI:

.. raw:: html

   <video width="100%" height="auto" controls style="max-width: 600px;">
     <source src="_static/demo.mp4" type="video/mp4">
     Your browser does not support the video tag.
   </video>

The demo application source code is located at `samples/demo`.

Features
********

- **Lightweight immediate-mode GUI** - No widget state management needed
- **Minimal memory footprint** - No dynamic memory allocation
- **Simple API** - Easy to create user interfaces
- **Zephyr integration** - Works with Zephyr's display and input subsystems
- **Extended drawing primitives** - Circles, arcs, lines, triangles, and images
- **Animation support** - Built-in animation framework
- **Flex layout system** - Proportional/weighted layouts

Getting Started
***************

To add MicroUI to your Zephyr project, add it as a module in your ``west.yml``:

.. code-block:: yaml

   manifest:
     projects:
       - name: zephyr-microui
         url: https://github.com/faxe1008/zephyr-microui
         revision: main
         path: modules/lib/microui

Then enable it in your ``prj.conf``:

.. code-block:: kconfig

   CONFIG_MICROUI=y

.. toctree::
   :maxdepth: 2
   :caption: Contents

   api/index

Indices and Tables
******************

* :ref:`genindex`
* :ref:`search`
