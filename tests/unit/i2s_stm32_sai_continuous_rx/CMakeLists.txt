# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
find_package(Python3 REQUIRED COMPONENTS Interpreter)

project(i2s_stm32_sai_continuous_rx)

get_filename_component(driver_root ${CMAKE_CURRENT_SOURCE_DIR}/../../.. ABSOLUTE)

set(driver_fixture ${CMAKE_CURRENT_BINARY_DIR}/i2s_stm32_sai_driver_fixture.c)
execute_process(
	COMMAND ${Python3_EXECUTABLE}
		${CMAKE_CURRENT_SOURCE_DIR}/generate_driver_fixture.py
		${driver_root}/drivers/i2s/i2s_stm32_sai.c
		${driver_fixture}
	RESULT_VARIABLE fixture_result
)
if(NOT fixture_result EQUAL 0)
	message(FATAL_ERROR "Failed to generate the STM32 SAI production-function fixture")
endif()

target_include_directories(app BEFORE PRIVATE
	${driver_root}/include
	${driver_root}/drivers/i2s
	${CMAKE_CURRENT_SOURCE_DIR}
)
target_sources(app PRIVATE main.c ${driver_fixture})
