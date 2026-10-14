# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
find_package(Python3 REQUIRED COMPONENTS Interpreter)

project(stm32n6_clock)

set(driver_fixture ${CMAKE_CURRENT_BINARY_DIR}/clock_stm32_ll_n6_fixture.c)
execute_process(
	COMMAND ${Python3_EXECUTABLE}
		${CMAKE_CURRENT_SOURCE_DIR}/generate_driver_fixture.py
		${ZEPHYR_BASE}/drivers/clock_control/clock_stm32_ll_n6.c
		${driver_fixture}
	RESULT_VARIABLE fixture_result
)
if(NOT fixture_result EQUAL 0)
	message(FATAL_ERROR "Failed to generate the STM32N6 clock driver fixture")
endif()

target_sources(app PRIVATE main.c ${driver_fixture})
