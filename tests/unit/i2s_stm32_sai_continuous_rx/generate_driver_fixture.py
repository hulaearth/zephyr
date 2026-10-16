#!/usr/bin/env python3

# Copyright (c) 2026 Hula Firmware
#
# SPDX-License-Identifier: Apache-2.0

import pathlib
import sys


FUNCTIONS = (
	("static void continuous_rx_latch_fault", "continuous_rx_latch_fault"),
	("static void dma_callback", "dma_callback"),
	("static int continuous_rx_start", "continuous_rx_start"),
	("static int continuous_rx_quiesce", "continuous_rx_quiesce"),
	("static uint8_t continuous_rx_stop_target", "continuous_rx_stop_target"),
	("static int sai_sub_dma_init", "sai_sub_dma_init"),
	("static int stream_start", "stream_start"),
	("static void queue_drop", "queue_drop"),
	("static int stm32_sai_sub_conf", "stm32_sai_sub_conf"),
	("static int stm32_sai_sub_read", "stm32_sai_sub_read"),
	("int stm32_sai_rx_release", "stm32_sai_rx_release"),
	("int stm32_sai_rx_get_evidence", "stm32_sai_rx_get_evidence"),
	("static int stm32_sai_sub_trigger", "stm32_sai_sub_trigger"),
)


def extract_function(source: str, signature: str) -> str:
	search = 0
	while True:
		start = source.index(signature, search)
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
		if source[terminator] == "{":
			body = terminator
			break
		if source[terminator] != ";":
			raise ValueError(f"unexpected token after function signature: {signature}")
		search = terminator + 1
	depth = 0
	state = "code"
	i = body
	while i < len(source):
		char = source[i]
		next_char = source[i + 1] if i + 1 < len(source) else ""
		if state == "code":
			if char == "/" and next_char == "*":
				state = "comment"
				i += 1
			elif char == "/" and next_char == "/":
				state = "line_comment"
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
		elif state == "line_comment" and char == "\n":
			state = "code"
		elif state in ("string", "character"):
			if char == "\\":
				i += 1
			elif (state == "string" and char == '"') or (state == "character" and char == "'"):
				state = "code"
		i += 1
	raise ValueError(f"unterminated function: {signature}")


def main() -> None:
	if len(sys.argv) != 3:
		raise SystemExit("usage: generate_driver_fixture.py DRIVER_SOURCE OUTPUT")
	source = pathlib.Path(sys.argv[1]).read_text(encoding="utf-8")
	functions = [extract_function(source, signature) for signature, _ in FUNCTIONS]
	output = pathlib.Path(sys.argv[2])
	output.write_text(
		'#include "driver_fixture_support.h"\n\n'
		+ "\n\n".join(functions)
		+ '\n\n#include "driver_fixture_tests.h"\n',
		encoding="utf-8",
	)


if __name__ == "__main__":
	main()
