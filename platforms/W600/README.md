# W600 standalone RD32 stub

This platform preserves the W600 target-side implementation imported from
`OpenIOTflasher/TEST/W600 R&D/loader/`. It uses its existing RD32 version 2
protocol, independently of the shared `0xA5` engine and generic HAL.

## Build and artifacts

With an ARM bare-metal GCC toolchain on PATH, run from the repository root:

```sh
make PLATFORM=W600
```

Alternatively set `TOOLCHAIN=/path/to/bin/` (including the trailing slash).
The platform retains the original `build-loader.ps1` Cortex-M3/Thumb, `-Os`,
freestanding, warning and section-GC flags. No SDK or vendor library is linked.
The repository's full/minimal build flags do not select different W600 features.

Outputs stay under `build/W600/`:

- `W600_stub.elf`, `W600_stub.map` and `W600_stub.asm`: reader build outputs.
- `W600_stub_code.bin`: raw reader before the protocol trailer.
- `secboot-copy-stub.o` and `secboot-copy-stub.bin`: separate bootstrap helper.
- `W600_Stub.bin`: raw reader followed by the eight ASCII bytes `W600RD32`.

`make_w600_image.py` checks that the bootstrap helper is exactly 80 bytes and
that the reader including its trailer fits the existing 3624-byte host budget
(`0x1000 - 56 - 0x1A0`). The final artifact is not compressed and is not a
complete flash image. Packaging always starts from the raw reader, so repeated
packaging does not accumulate trailers. It does not install or modify the host
resource.

## Runtime and host boundary

The linker places `_start` at SRAM `0x20030000`, reserves a 32 KiB RAM region,
and constrains allocated sections to end at or below `0x20037000`. Startup sets
the stack to `0x20038000` and disables interrupts before entering `loader_main`.

The existing protocol exposes information, flash reads, JEDEC identification,
and return-to-ROM operations. Read requests are bounded to 16384 bytes. It
supports the primary 1 MiB flash and an optional secondary SPI flash according
to the existing implementation. Return-to-ROM erases the temporary secboot
sector and resets through the watchdog; this stub must not be described as
read-only. These behaviors are preserved, not newly enabled by the migration.

OpenIOTflasher owns upload/bootstrap, temporary secboot handling, recovery and
host protocol orchestration. Its existing `BK7231Flasher/Flashers/W600Flasher.cs`
and embedded `BK7231Flasher/Floaders/W600_Stub.bin` remain unchanged by this
platform addition. The standalone copy helper is not concatenated into the
reader package.

## Import provenance and validation boundary

The canonical production source is now `bkflasher_stub/platforms/W600/`.
The retained OpenIOTflasher R&D sources and build script are historical and
are not updated by this migration.

The four files `w600_uart_flash_reader.c`, `startup.S`, `loader.ld`, and
`secboot_copy_stub.S` were copied byte-for-byte from the source directory above.
The platform Makefile and packaging helper replace that directory's
`build-loader.ps1` build/package orchestration; its host installation option is
not carried over.

The original build script names an ARM GCC 12.2 toolchain. Reproducing an
existing image requires the same compiler and flags.
