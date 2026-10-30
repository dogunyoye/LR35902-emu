# LR35902 Emu

A cycle-accurate [Sharp LR35902](https://gbdev.io/pandocs/CPU.html) (Game Boy) CPU emulator written in C++20.

## Status

:construction: **Work in progress** — the CPU/memory core is being built out.

| Component | State |
|---|---|
| CPU register model (AF/BC/DE/HL, SP, PC, IME) | :white_check_mark: done |
| 64 KB flat memory space + basic read/write routing | :white_check_mark: done |
| WRAM echo (0xE000–0xFDFF) | :white_check_mark: done |
| MBC (memory bank controller) banking | :soon: planned |
| VRAM/OAM access locking during PPU modes | :soon: planned |
| Instruction set / cycle decoder | :soon: planned |

## Building

Requires [CMake](https://cmake.org) ≥ 3.16 and a C++20 compiler (GCC 10+, Clang 10+, or MSVC 2019+).

```sh
cmake -B build
cmake --build build
```

This produces the static library `build/liblr35902.a`. Once `src/main.cpp` exists, a `lr35902-emu` executable target builds automatically.

### Debug builds

Debug configurations compile with Address- and UndefinedBehaviorSanitizer enabled — handy for catching the out-of-bounds/hardware-register-class bugs emulators love to attract:

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

## Project layout

```
├── include/
│   └── LR35902.h   # CPU state model (registers, PC/SP, memory bus)
├── src/
│   └── Memory.cpp  # Memory bus: read routing, WRAM echo, ROM write guard
└── CMakeLists.txt
```

## References

- [Pan Docs](https://gbdev.io/pandocs/) — the canonical Game Boy hardware reference
- [gbops](https://izik1.github.io/gbops/) — opcode table / timing reference

## License

[MIT](LICENSE) — Copyright © 2026 Dimeji
