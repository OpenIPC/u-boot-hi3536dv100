# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Repository Overview

OpenIPC fork of HiSilicon's vendor U-Boot 2010.06 for Hi3536Dv100 (DVR/NVR Cortex-A7 SoC). Imported from Hi3536D SDK V2.0.2.0 (V100R001C02SPC020). Builds via the OpenIPC HiSi U-Boot fleet's standard CI shape: build → 256 KiB partition-fit → qemu_smoke against widgetii/qemu-hisilicon's `hi3536dv100` machine → publish to `OpenIPC/firmware/latest`.

## Vendor source quirks

- **Single-target tree.** Top-level Makefile has `hi3536dv100_config`. CPU dir `arch/arm/cpu/hi3536dv100/`. Board dir `board/hi3536dv100/`. No per-SoC variant fan-out.
- **No `compressed/` subdir** — no mini-boot LZMA self-extractor; raw `u-boot.bin` runs directly. Same shape as `u-boot-hi3536cv100`.
- **Single 8 KiB regfile slot.** `mkboot.sh` does `dd bs=8192 conv=sync` after a 64-byte u-boot.bin head, then u-boot.bin from offset 8256 onward (vs hi3536cv100's 5 KiB). CI inlines that — see `.github/workflows/build.yml`'s `Build` step.
- **Toolchain.** `arm-hisiv610-linux` (gcc 6.2.1, glibc 2.24), available in `OpenIPC/toolchains` v1 as a split asset (`arm-hisiv610-linux.tgz.part00` + `.part01`, 2 GB GitHub release limit). The workflow reassembles via `cat parts | tar xzf`.
- **TEXT_BASE.** `0x80800000`, set by `board/hi3536dv100/config.mk`.

## OpenIPC patches (deviations from verbatim vendor source)

- **`include/configs/hi-common.h`** (new file): cloned verbatim from `u-boot-hi3519v101` / `u-boot-hi3536cv100`. OpenIPC fleet env block (mtdparts, OpenIPC # prompt, env helpers, etc.).
- **`include/configs/hi3536dv100.h`**: `#include <configs/hi-common.h>` at the end, then `#undef` the heavy commands the SPI-NOR-squashfs flow doesn't need (CMD_UBI/UBIFS/USB/PCMCIA/FAT/EXT2/FS_GENERIC) — same trim as `u-boot-hi3536cv100`. Without it the binary exceeds the 256 KiB partition-fit gate. Vendor's `CONFIG_PRODUCTNAME` is already `"hi3536dv100"`, no override needed.
- **`common/cmd_nand.c`**: inline `str2long` (verbatim from `u-boot-hi3519v101`). Vendor's cmd_nand.c calls it inside `#ifdef CONFIG_CMD_MTDPARTS` but only declares it as `static inline` in cmd_onenand.c — link fails when both CMD_NAND and CMD_MTDPARTS are enabled (the OpenIPC default once hi-common.h enables MTDPARTS).
- **`reg_info_hi3536dv100.bin`** (new file): 8192-byte DDR/PLL register-init blob. Extracted byte-verbatim from `Hi3536D_SDK_V2.0.2.0`'s prebuilt `u-boot-hi3536dv100.bin` via `dd skip=64 count=8192` — the inverse of `mkboot.sh`'s embed.
