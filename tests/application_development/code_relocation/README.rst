.. _code_relocation:

Code relocation
#################

Overview
********
A simple example that demonstrates how relocation of code, data or bss sections
using a custom linker script.

Split RAM-load startup
**********************
The ``buildsystem.app_dev.code_relocation.split_ram_load`` scenario runs on
``mps2/an385`` with XIP disabled and generated code/data relocation enabled.
Its overlay places the image in AN385's writable ZBT SSRAM1 at the reset-vector
address, allowing QEMU to boot it without a bootloader. The existing linker
script places the generated SRAM2 runtime sections in the upper half of that
memory, with their load contents in the lower half.

Linker assertions require distinct load and runtime addresses for generated
text and data. The test calls a non-inlined function in the generated SRAM2
text section; it reads the initialized value from the generated SRAM2 data
section using a volatile access. Cortex-M startup reaches these sections via
``arch_data_copy()`` and ``data_copy_xip_relocation()``, without test setup
copying the contents. This validates generic startup in the emulator, without
requiring MCUboot policy or board-specific RAM-load integration.
