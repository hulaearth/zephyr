#!/usr/bin/env python3

# Copyright (c) 2026 Hula Firmware
#
# SPDX-License-Identifier: Apache-2.0

import pathlib
import sys


FUNCTIONS = (
	("static inline bool arrm_state_get", "arrm_state_get"),
	("static inline uint32_t z_clock_lptim_getcounter", "z_clock_lptim_getcounter"),
	("static void lptim_irq_handler", "lptim_irq_handler"),
	("static void lptim_set_autoreload", "lptim_set_autoreload"),
	("void sys_clock_set_timeout", "sys_clock_set_timeout"),
	("static uint32_t sys_clock_lp_time_get", "sys_clock_lp_time_get"),
	("uint64_t sys_clock_cycle_get_64", "sys_clock_cycle_get_64"),
	("uint32_t sys_clock_cycle_get_32", "sys_clock_cycle_get_32"),
	("void sys_clock_idle_exit", "sys_clock_idle_exit"),
)


def extract_function(source: str, signature: str) -> str:
	start = source.index(signature)
	open_paren = source.index("(", start + len(signature))
	depth = 0
	close_paren = open_paren
	while close_paren < len(source):
		if source[close_paren] == "(":
			depth += 1
		elif source[close_paren] == ")":
			depth -= 1
			if depth == 0:
				break
		close_paren += 1
	terminator = close_paren + 1
	while source[terminator].isspace():
		terminator += 1
	if source[terminator] != "{":
		raise ValueError(f"unexpected token after function signature: {signature}")

	depth = 0
	state = "code"
	i = terminator
	while i < len(source):
		char = source[i]
		next_char = source[i + 1] if i + 1 < len(source) else ""
		if state == "code":
			if char == "/" and next_char == "*":
				state = "comment"
				i += 1
			elif char == '"':
				state = "string"
			elif char == "'":
				state = "character"
			elif char == "{":
				depth += 1
			elif char == "}":
				depth -= 1
				if depth == 0:
					return source[start : i + 1]
		elif state == "comment" and char == "*" and next_char == "/":
			state = "code"
			i += 1
		elif state == "string" or state == "character":
			if char == "\\":
				i += 1
			elif (state == "string" and char == '"') or (
				state == "character" and char == "'"
			):
				state = "code"
		i += 1
	raise ValueError(f"unterminated function: {signature}")


def main() -> None:
	if len(sys.argv) != 3:
		raise SystemExit("usage: generate_driver_fixture.py DRIVER_SOURCE OUTPUT")
	source = pathlib.Path(sys.argv[1]).read_text(encoding="utf-8")
	functions = []
	for signature, _ in FUNCTIONS:
		if signature == "void sys_clock_idle_exit":
			functions.append("#define CONFIG_STM32_LPTIM_STDBY_TIMER 1")
		functions.append(extract_function(source, signature))
	output = pathlib.Path(sys.argv[2])
	output.write_text(
		'#include "driver_fixture_support.h"\n\n'
		+ "\n\n".join(functions)
		+ '\n\n#include "driver_fixture_tests.h"\n',
		encoding="utf-8",
	)


if __name__ == "__main__":
	main()
