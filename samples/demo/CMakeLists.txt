# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(microui_demo)

FILE(GLOB app_sources src/*.c)
target_sources(app PRIVATE ${app_sources})
