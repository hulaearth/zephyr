# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
find_package(Python3 REQUIRED COMPONENTS Interpreter)

project(stm32_lptim_timer)

set(driver_fixture ${CMAKE_CURRENT_BINARY_DIR}/stm32_lptim_timer_fixture.c)
execute_process(
	COMMAND ${Python3_EXECUTABLE}
		${CMAKE_CURRENT_SOURCE_DIR}/generate_driver_fixture.py
		${ZEPHYR_BASE}/drivers/timer/stm32_lptim_timer.c
		${driver_fixture}
	RESULT_VARIABLE fixture_result
)
if(NOT fixture_result EQUAL 0)
	message(FATAL_ERROR "Failed to generate the STM32 LPTIM production-function fixture")
endif()

target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_sources(app PRIVATE ${driver_fixture})
