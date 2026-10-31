# LR35902 Emu

A small [Sharp LR35902](https://gbdev.io/pandocs/CPU.html) (Game Boy) CPU emulator written in C++20.

## Status

This project is still a work in progress, but the basic CPU core, memory routing, timer support, and CLI runner are in place.

| Component | State |
|---|---|
| CPU register model (AF/BC/DE/HL, SP, PC, IME) | :white_check_mark: done |
| 64 KB flat memory space + read/write routing | :white_check_mark: done |
| WRAM echo (0xE000–0xFDFF) | :white_check_mark: done |
| ROM-only cartridge (32 KiB, header validated, write protected) | :white_check_mark: done |
| VRAM/OAM access locking during PPU modes | :white_check_mark: done |
| Legal base and CB-prefixed opcode implementations | :white_check_mark: done |
| Timer registers (DIV/TIMA/TMA/TAC) and update logic | :white_check_mark: done |
| Interrupt dispatch, priority, delayed EI, and HALT wake-up | :white_check_mark: done |
| Cycle-driven CPU and timer stepping | :white_check_mark: done |
| Approximate variable mode-3 timing, BG/window/sprite rendering, VBlank and STAT interrupts | :white_check_mark: done |
| Regression tests for CPU, timer, interrupt, cartridge, memory, and PPU behavior | :white_check_mark: done |
| CLI runner with cycle-driven frames and ROM-only loading | :white_check_mark: done |
| Cycle-exact CPU and peripheral conformance | :soon: planned |
| MBC emulation / cartridge banking | :soon: planned |
| Full PPU and hardware fidelity | :soon: planned |

## Building

Requires [CMake](https://cmake.org) ≥ 3.16 and a C++20 compiler (GCC 10+, Clang 10+, or MSVC 2019+).

```sh
cmake -B build
cmake --build build
```

This generates:

- the static library: `build/liblr35902.a`
- test executables: `build/cpu_tests`, `build/timer_tests`, `build/cartridge_tests`, `build/memory_tests`, and `build/ppu_tests`
- the emulator CLI: `build/lr35902-emu`

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

Run a 32 KiB ROM-only cartridge image (header type 0x00, ROM size code 0x00):

```sh
./build/lr35902-emu /path/to/game.gb
```

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
│   ├── Cartridge.cpp    # ROM-only cartridge validation and setup
│   ├── PPU.cpp          # LCD modes, scanlines, and BG/window/sprite composition
│   ├── Timer.cpp        # Divider/timer register updates
│   └── main.cpp         # CLI entry point and demo runner
├── tests/
│   ├── cpu_tests.cpp        # CPU instructions, flags, and interrupts
│   ├── timer_tests.cpp      # Timer register behavior
│   ├── cartridge_tests.cpp  # ROM-only cartridge loading and mapping
│   ├── memory_tests.cpp     # Memory-bus address behavior
│   └── ppu_tests.cpp        # PPU modes, scanlines, window, and sprites
└── build/               # Generated CMake build output
```

## References

- [Pan Docs](https://gbdev.io/pandocs/) — the canonical Game Boy hardware reference
- [gbops](https://izik1.github.io/gbops/) — opcode table / timing reference

## License

[MIT](LICENSE) — Copyright © 2026 Dimeji
