# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(i2s_stm32_sai_continuous_rx)

target_include_directories(app PRIVATE ${ZEPHYR_BASE}/drivers/i2s)
target_sources(app PRIVATE main.c)
