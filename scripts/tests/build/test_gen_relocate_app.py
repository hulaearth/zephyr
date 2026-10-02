# Copyright (c) 2026 The Zephyr Project Contributors
#
# SPDX-License-Identifier: Apache-2.0

from collections import defaultdict
import importlib.util
from pathlib import Path

import pytest


SCRIPT = Path(__file__).parents[2] / "build" / "gen_relocate_app.py"
SPEC = importlib.util.spec_from_file_location("gen_relocate_app", SCRIPT)
assert SPEC is not None and SPEC.loader is not None
gen_relocate_app = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(gen_relocate_app)


@pytest.mark.parametrize(
    "location, region, alignment, keep",
    [
        ("SRAM_FAST_TEXT_32|COPY|NOKEEP", "SRAM_FAST|COPY", {"SRAM_FAST": 32}, False),
        ("SRAM_TEXT_32|COPY", "SRAM|COPY", {"SRAM": 32}, True),
        ("SRAM_FAST_TEXT_32|NOCOPY|NOKEEP", "SRAM_FAST|NOCOPY", {"SRAM_FAST": 32}, False),
        ("AXISRAM_SAFE_TEXT|COPY", "AXISRAM_SAFE|COPY", {}, True),
        ("AXISRAM_SAFE_TEXT|NOCOPY", "AXISRAM_SAFE|NOCOPY", {}, True),
        ("ext_ram_seg|COPY", "ext_ram_seg|COPY", {}, True),
        ("SRAM_FAST_TEXT_32", "SRAM_FAST", {"SRAM_FAST": 32}, True),
    ],
)
def test_memory_region_name_alignment_and_flags(location, region, alignment, keep):
    gen_relocate_app.mpu_align = {}
    sections = defaultdict(list)
    sections[gen_relocate_app.SectionKind.TEXT].append(
        gen_relocate_app.OutputSection("file.c.obj", ".text.function")
    )

    result = gen_relocate_app.assign_to_correct_mem_region(location, sections)

    assert list(result) == [region]
    assert gen_relocate_app.mpu_align == alignment
    assert result[region][gen_relocate_app.SectionKind.TEXT] == [
        gen_relocate_app.OutputSection("file.c.obj", ".text.function", keep=keep)
    ]


@pytest.mark.parametrize(
    "location, expected",
    [
        ("SRAM", ("SRAM", "")),
        ("SRAM_FAST", ("SRAM_FAST", "")),
        ("SRAM_FAST_32", ("SRAM_FAST", "32")),
        ("SRAM2_256", ("SRAM2", "256")),
    ],
)
def test_only_final_numeric_alignment_suffix_is_split(location, expected):
    assert gen_relocate_app.split_alignment_suffix(location) == expected


def test_relocation_metadata_is_not_classified_as_code():
    for name in (".rel.text.function", ".rela.text.function"):
        assert gen_relocate_app.SectionKind.for_section_named(name) is None


def test_arm_unwind_metadata_is_not_classified_as_code():
    for name in (".ARM.exidx.text.function", ".ARM.extab.text.function"):
        assert gen_relocate_app.SectionKind.for_section_named(name) is None


def test_regular_code_section_is_still_classified():
    assert (
        gen_relocate_app.SectionKind.for_section_named(".text.function")
        is gen_relocate_app.SectionKind.TEXT
    )
