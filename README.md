# LR35902 Emu

A small [Sharp LR35902](https://gbdev.io/pandocs/CPU.html) (Game Boy) CPU emulator written in C++20.

## Status

This project is still a work in progress, but the basic CPU core, memory routing, timer support, and CLI runner are in place.

| Component | State |
|---|---|
| CPU register model (AF/BC/DE/HL, SP, PC, IME) | :white_check_mark: done |
| 64 KB flat memory space + read/write routing | :white_check_mark: done |
| WRAM echo (0xE000–0xFDFF) | :white_check_mark: done |
| ROM write protection / cartridge RAM enable | :white_check_mark: done |
| VRAM/OAM access locking during PPU modes | :white_check_mark: done |
| Core instruction decode (loads, arithmetic, jumps, stack, CB-prefixed opcodes) | :white_check_mark: partial |
| Timer registers (DIV/TIMA/TMA/TAC) and update logic | :white_check_mark: done |
| Regression tests for core CPU/timer behavior | :white_check_mark: done |
| CLI runner with demo mode and ROM loading | :white_check_mark: done |
| Full LR35902 instruction coverage | :soon: planned |
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
- the test executable: `build/cpu_tests`
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

Run a ROM file:

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
│   ├── LR35902.cpp      # Instruction decoder, arithmetic, flags, CB ops
│   ├── Memory.cpp       # Memory routing and access rules
│   ├── Timer.cpp        # Divider/timer register updates
│   └── main.cpp         # CLI entry point and demo runner
├── tests/
│   └── cpu_tests.cpp    # Regression tests for CPU behavior
└── build/                # Generated CMake build output
```

## References

- [Pan Docs](https://gbdev.io/pandocs/) — the canonical Game Boy hardware reference
- [gbops](https://izik1.github.io/gbops/) — opcode table / timing reference

## License

[MIT](LICENSE) — Copyright © 2026 Dimeji
