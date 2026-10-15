# Copyright (c) 2026 The Zephyr Project Contributors
#
# SPDX-License-Identifier: Apache-2.0

import os
import pickle
import re
import selectors
import subprocess
import sys
import time
from pathlib import Path

import yaml
from elftools.elf.elffile import ELFFile
from twister_harness import DeviceAdapter

sys.path.insert(0, os.path.join(os.environ["ZEPHYR_BASE"], "scripts", "pylib", "twister"))
sys.path.insert(
    0, os.path.join(os.environ["ZEPHYR_BASE"], "scripts", "dts", "python-devicetree", "src")
)
from twisterlib.cmakecache import CMakeCache  # noqa: E402


def test_ram_load_layout(unlaunched_dut: DeviceAdapter) -> None:
    build_dir = Path(unlaunched_dut.device_config.build_dir)
    if (build_dir / "domains.yaml").exists():
        domains = yaml.safe_load((build_dir / "domains.yaml").read_text())
        build_dir = next(
            Path(domain["build_dir"])
            for domain in domains["domains"]
            if domain["name"] != "mcuboot"
        )

    configuration = (build_dir / "zephyr" / ".config").read_text()
    assert "# CONFIG_XIP is not set" in configuration
    chosen = "mcuboot,image-ram"
    if "CONFIG_MCUBOOT_BOOTLOADER_MODE_SINGLE_APP_RAM_LOAD=y" in configuration:
        chosen = "mcuboot,ram-load-dev"
    with (build_dir / "zephyr" / "edt.pickle").open("rb") as file:
        devicetree = pickle.load(file)
    assert ("CONFIG_ARCH_DATA_COPY_FOR_RAM_LOAD_SPLIT=y" in configuration) == (
        chosen in devicetree.chosen_nodes
    )
    runtime = devicetree.chosen_nodes["zephyr,sram"].regs[0]
    load = devicetree.chosen_nodes.get(chosen, devicetree.chosen_nodes["zephyr,sram"]).regs[0]

    with (build_dir / "zephyr" / "zephyr.elf").open("rb") as file:
        symbols = {
            symbol.name: symbol["st_value"]
            for symbol in ELFFile(file).get_section_by_name(".symtab").iter_symbols()
        }
    assert symbols["__rom_region_start"] == load.addr
    assert symbols["__data_region_start"] >= runtime.addr
    assert symbols["__bss_start"] >= runtime.addr
    assert load.addr <= symbols["__data_region_load_start"] < load.addr + load.size

    memory_map = (build_dir / "zephyr" / "zephyr.map").read_text()
    ram = re.search(r"^RAM\s+(0x[0-9a-fA-F]+)\s+(0x[0-9a-fA-F]+)", memory_map, re.MULTILINE)
    assert ram is not None
    runtime_size = min(load.size, runtime.size) if load.addr == runtime.addr else runtime.size
    assert (int(ram[1], 16), int(ram[2], 16)) == (runtime.addr, runtime_size)
    if load.addr == runtime.addr:
        assert symbols["__data_region_start"] == symbols["__data_region_load_start"]
    else:
        assert symbols["__data_region_start"] != symbols["__data_region_load_start"]
        load_size = load.size
        if load.addr < runtime.addr < load.addr + load.size:
            load_size = runtime.addr - load.addr
        rom_end_offset = re.search(r"^CONFIG_ROM_END_OFFSET=(.+)$", configuration, re.MULTILINE)
        assert rom_end_offset is not None
        flash = re.search(r"^FLASH\s+(0x[0-9a-fA-F]+)\s+(0x[0-9a-fA-F]+)", memory_map, re.MULTILINE)
        assert flash is not None
        assert (int(flash[1], 16), int(flash[2], 16)) == (
            load.addr,
            load_size - int(rom_end_offset[1], 0),
        )


def test_ram_load_split(unlaunched_dut: DeviceAdapter) -> None:
    build_dir = Path(unlaunched_dut.device_config.build_dir)
    domains = yaml.safe_load((build_dir / "domains.yaml").read_text())
    domain_dirs = {domain["name"]: Path(domain["build_dir"]) for domain in domains["domains"]}
    app_dir = next(path for name, path in domain_dirs.items() if name != "mcuboot")
    mcuboot_dir = domain_dirs["mcuboot"]

    qemu = CMakeCache.from_file(mcuboot_dir / "CMakeCache.txt").get("QEMU")
    assert qemu is not None
    command = (
        qemu,
        "-cpu",
        "cortex-m3",
        "-machine",
        "mps2-an385",
        "-nographic",
        "-device",
        f"loader,file={mcuboot_dir / 'zephyr' / 'zephyr.hex'}",
        "-device",
        f"loader,file={app_dir / 'zephyr' / 'zephyr.signed.bin'},addr=0x20050000",
    )
    expected = re.compile(r"PASS: split RAM-load initialized data")
    output = ""

    with subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT) as proc:
        assert proc.stdout is not None
        selector = selectors.DefaultSelector()
        selector.register(proc.stdout.fileno(), selectors.EVENT_READ)
        deadline = time.monotonic() + 60.0
        try:
            while not expected.search(output) and time.monotonic() < deadline:
                if not selector.select(deadline - time.monotonic()):
                    break
                chunk = os.read(proc.stdout.fileno(), 4096)
                if not chunk:
                    break
                output += chunk.decode(errors="replace")
        finally:
            selector.close()
            proc.terminate()
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill()

    assert expected.search(output), f"application did not initialize split data:\n{output}"
