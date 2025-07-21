# Copyright (c) 2025 Fabian Blatz <fabianblatz@gmail.com>
# SPDX-License-Identifier: Apache-2.0

if(CONFIG_MICROUI)

zephyr_library()
zephyr_library_sources(microui.c event_loop.c)
zephyr_library_sources_ifdef(CONFIG_MICROUI_INPUT input.c)

endif()
