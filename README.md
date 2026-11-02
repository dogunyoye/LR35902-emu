# LR35902 Emu

A small [Sharp LR35902](https://gbdev.io/pandocs/CPU.html) (Game Boy) CPU emulator written in C++20.

## Status

This project is still a work in progress, but the basic CPU core, memory routing, timer support, and CLI runner are in place.

| Component | State |
|---|---|
| CPU register model (AF/BC/DE/HL, SP, PC, IME) | :white_check_mark: done |
| 64 KB flat memory space + read/write routing | :white_check_mark: done |
| WRAM echo (0xE000–0xFDFF) | :white_check_mark: done |
| ROM-only, MBC1 (including multicarts), MBC2, and MBC5 cartridges (banked ROM/RAM, battery saves) | :white_check_mark: done |
| VRAM/OAM access locking during PPU modes | :white_check_mark: done |
| Legal base and CB-prefixed opcode coverage, including STOP | :white_check_mark: done |
| Timer registers (DIV/TIMA/TMA/TAC) and update logic | :white_check_mark: done |
| Interrupt dispatch, priority, delayed EI, and HALT wake-up | :white_check_mark: done |
| JOYP register, keyboard input, and joypad interrupt | :white_check_mark: done |
| SB/SC serial transfer and serial interrupt | :white_check_mark: done |
| Serial-output ROM test runner | :white_check_mark: done |
| Cycle-driven CPU and timer stepping | :white_check_mark: done |
| Approximate variable mode-3 timing, BG/window/sprite rendering, VBlank and STAT interrupts | :white_check_mark: done |
| Regression tests for CPU, timer, serial, interrupts, joypad, cartridge, memory, and PPU behavior | :white_check_mark: done |
| CLI runner with cycle-driven cartridge support and keyboard controls | :white_check_mark: done |
| Cycle-exact CPU and peripheral conformance | :soon: planned |
| Other mappers (MBC3, RTC, etc.) | :soon: planned |
| Full PPU and hardware fidelity | :soon: planned |

## Building

Requires [CMake](https://cmake.org) ≥ 3.16 and a C++20 compiler (GCC 10+, Clang 10+, or MSVC 2019+).

```sh
cmake -B build
cmake --build build
```

This generates:

- the static library: `build/liblr35902.a`
- test executables: `build/cpu_tests`, `build/timer_tests`, `build/cartridge_tests`, `build/memory_tests`, `build/joypad_tests`, `build/serial_tests`, and `build/ppu_tests`
- the emulator CLI: `build/lr35902-emu`
- the serial ROM test runner: `build/rom_test_runner`

Run the test suite with:

```sh
ctest --test-dir build --output-on-failure
```

Run a serial-reporting ROM test (for example, a CPU conformance ROM):

```sh
./build/rom_test_runner /path/to/test.gb [MAX_CYCLES]
```

The runner prints serial output and returns 0 when it sees `Passed`, 1 for `Failed`, or 2 if neither marker appears before the cycle limit (default: 50,000,000). It also recognises Mooneye's `LD B,B` register signatures and blargg's cartridge-RAM status at `$A000`. Pass `--screenshot out.ppm` before the ROM path to save the frame for screenshot tests such as dmg-acid2.

Run the Mooneye, blargg, and dmg-acid2 suites together (the ROMs are not included in this repository):

```sh
tests/run_rom_suites.sh --mooneye /path/to/mooneye-test-suite --blargg /path/to/gb-test-roms --acid2 /path/to/dmg-acid2
```

This writes per-ROM logs and `results.tsv` to `build/rom-results`. The dmg-acid2 comparison needs Python with Pillow.

### Debug builds

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

This enables sanitizer instrumentation useful for emulator debugging.

## Usage

Run the demo program:

```sh
./build/lr35902-emu
```

Run a supported ROM-only (32 KiB), MBC1, MBC2, or MBC5 cartridge image:

```sh
./build/lr35902-emu /path/to/game.gb
```

Battery-backed cartridge RAM is loaded from and saved to a same-basename `.sav` file beside the ROM.

Controls: arrow keys move, Z is A, X is B, Right Shift is Select, and Enter is Start.

Show usage/help:

```sh
./build/lr35902-emu --help
```

## Project layout

```text
├── CMakeLists.txt
├── LICENSE
├── README.md
├── include/
│   └── LR35902.h        # CPU state model, flags, public API
├── src/
│   ├── LR35902.cpp      # Instruction decoder, flags, interrupt stepping
│   ├── Memory.cpp       # Memory routing and access rules
│   ├── Cartridge.cpp    # Cartridge validation, banking, and save RAM
│   ├── PPU.cpp          # LCD modes, scanlines, and BG/window/sprite composition
│   ├── Serial.cpp       # Internal-clock serial transfers and test output
│   ├── Timer.cpp        # Divider/timer register updates
│   └── main.cpp         # CLI entry point and demo runner
├── tests/
│   ├── cpu_tests.cpp        # CPU instructions, flags, and interrupts
│   ├── timer_tests.cpp      # Timer register behavior
│   ├── cartridge_tests.cpp  # Cartridge loading, banking, and save persistence
│   ├── memory_tests.cpp     # Memory-bus address behavior
│   ├── joypad_tests.cpp     # JOYP selection and interrupt behavior
│   ├── serial_tests.cpp     # Serial transfer and interrupt behavior
│   ├── ppu_tests.cpp        # PPU modes, scanlines, window, and sprites
│   └── rom_test_runner.cpp  # Serial-output ROM conformance runner
└── build/               # Generated CMake build output
```

## References

- [Pan Docs](https://gbdev.io/pandocs/) — the canonical Game Boy hardware reference
- [gbops](https://izik1.github.io/gbops/) — opcode table / timing reference

## License

[MIT](LICENSE) — Copyright © 2026 Dimeji
