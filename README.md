# LR35902 Emu

A small [Sharp LR35902](https://gbdev.io/pandocs/CPU.html) (Game Boy and Game Boy Color) emulator written in C++20.

## Status

This project is still a work in progress, but the basic CPU core, memory routing, timer support, and CLI runner are in place.

| Component | State |
|---|---|
| CPU register model (AF/BC/DE/HL, SP, PC, IME) | :white_check_mark: done |
| 64 KB flat memory space + read/write routing | :white_check_mark: done |
| WRAM echo (0xE000–0xFDFF) | :white_check_mark: done |
| ROM-only, MBC1 (including multicarts), MBC2, MBC3 with real-time clock, MBC5, MBC7 (accelerometer and EEPROM), and HuC1 cartridges (banked ROM/RAM, battery saves) | :white_check_mark: done |
| VRAM/OAM access locking during PPU modes | :white_check_mark: done |
| Legal base and CB-prefixed opcode coverage, including STOP | :white_check_mark: done |
| Timer registers (DIV/TIMA/TMA/TAC) and update logic | :white_check_mark: done |
| Interrupt dispatch, priority, delayed EI, and HALT wake-up | :white_check_mark: done |
| JOYP register, keyboard input, and joypad interrupt | :white_check_mark: done |
| SB/SC serial transfer and serial interrupt | :white_check_mark: done |
| Serial-output ROM test runner | :white_check_mark: done |
| M-cycle bus timing: timers, OAM DMA, serial, and PPU advance with each CPU memory access | :white_check_mark: done |
| Per-dot pixel FIFO rendering (mid-line register changes), variable mode-3 timing, VBlank and STAT interrupts | :white_check_mark: done |
| Sound: two square channels (one with sweep), wave and noise channels, stereo mixing to SDL audio | :white_check_mark: done |
| Regression tests for CPU, timer, serial, interrupts, joypad, cartridge, memory, and PPU behavior | :white_check_mark: done |
| CLI runner with cycle-driven cartridge support, configurable keyboard controls, and controller support | :white_check_mark: done |
| Save states and fast-forward | :white_check_mark: done |
| DMG OAM corruption bug | :white_check_mark: done |
| Sub-M-cycle peripheral conformance (DMA bus conflicts) | :soon: planned |
| Other mappers (MBC6, HuC3, etc.) | :soon: planned |
| Game Boy Color: CGB mode (VRAM and WRAM banks, double speed, colour palettes, tile attributes, VRAM DMA) and DMG compatibility mode | :white_check_mark: done |
| Game Boy Color boot colourisation of DMG games, and screen colour correction | :white_check_mark: done |
| Full PPU and hardware fidelity | :soon: planned |

## Building

Requires [CMake](https://cmake.org) ≥ 3.16 and a C++20 compiler (GCC 10+, Clang 10+, or MSVC 2019+).

```sh
cmake -B build
cmake --build build
```

Builds are optimised (`Release`) unless another build type is given.

This generates:

- the static library: `build/liblr35902.a`
- test executables: `build/cpu_tests`, `build/timer_tests`, `build/cartridge_tests`, `build/memory_tests`, `build/joypad_tests`, `build/serial_tests`, `build/ppu_tests`, `build/apu_tests`, `build/save_state_tests`, and `build/controls_tests`
- the emulator CLI: `build/lr35902-emu`
- the serial ROM test runner: `build/rom_test_runner`

Run the test suite with:

```sh
ctest --test-dir build --output-on-failure
```

Run a serial-reporting ROM test (for example, a CPU conformance ROM):

```sh
./build/rom_test_runner /path/to/test.gb [MAX_CYCLES] [MODEL]
```

MODEL is `dmg0`, `dmgABC`, `mgb`, `sgb`, `sgb2`, `cgb0`, `cgb`, or `agb`. By default (`auto`) cartridges that only run on the Game Boy Color run on a CGB and everything else on a DMG. The runner prints serial output and returns 0 when it sees `Passed`, 1 for `Failed`, or 2 if neither marker appears before the cycle limit (default: 50,000,000). It also recognises Mooneye's `LD B,B` register signatures and blargg's cartridge-RAM status at `$A000`. Pass `--screenshot out.ppm` before the ROM path to save the frame for screenshot tests such as dmg-acid2, cgb-acid2, and Mealybug Tearoom.

Run the Mooneye, blargg, dmg-acid2, cgb-acid2, Mealybug Tearoom, and SameSuite suites together. The ROMs are not included in this repository; `tests/fetch_test_roms.sh` downloads pinned versions of them (about 100 MB) into `build/test-roms`. SameSuite is published as source, so on Linux x86-64 the script also downloads a pinned [RGBDS](https://rgbds.gbdev.io) release to build it; elsewhere it skips SameSuite:

```sh
tests/fetch_test_roms.sh
tests/run_rom_suites.sh --roms build/test-roms
```

Each suite can also be given on its own with `--mooneye`, `--blargg`, `--acid2`, `--cgb-acid2`, `--mealybug`, or `--samesuite`. Mooneye's tests run on the model their file names name; blargg's CPU and timing tests also run a second time on a CGB.

This writes per-ROM logs and `results.tsv` to `build/rom-results`. The script exits 1 if any ROM fails that is not listed in `tests/rom_known_failures.tsv`, and 2 if a suite or the test runner is missing. The screenshot comparisons need Python with Pillow.

### Debug builds

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

This enables AddressSanitizer and UndefinedBehaviorSanitizer instrumentation useful for emulator debugging. Debug builds run much slower, so use the default build for playing games.

## Usage

Run the demo program:

```sh
./build/lr35902-emu
```

Run a supported ROM-only (32 KiB), MBC1, MBC2, MBC3, MBC5, MBC7, or HuC1 cartridge image (`.gb` or `.gbc`):

```sh
./build/lr35902-emu /path/to/game.gb
```

Cartridges made for the Game Boy Color (including those that also run on the original Game Boy) run on an emulated CGB in colour; other cartridges run on an emulated DMG. `--model cgb` runs games made for the original Game Boy on a CGB instead, coloured the way its boot ROM colours them: Nintendo's own games get palettes chosen for them, others a default. `--dmg-palette` picks one of the boot ROM's manual choices instead (`right`, `left`, `up`, `down`, or one of those with `+a` or `+b`, the buttons held during the logo on a real CGB). `--model dmg` runs games that also work on the original Game Boy on a DMG.

Game Boy Color colours are adjusted to look like they did on its screen, which was darker and less saturated than a modern display; F9 or `--no-color-correction` turns this off. Screenshots taken by the test runner always keep the exact colours.

Battery-backed cartridge RAM is loaded from and saved to a same-basename `.sav` file beside the ROM. MBC3 saves also store the real-time clock in the format BGB and VBA-M use, and the clock catches up on time that passed while the emulator was closed.

### Controls

| Action | Key | Controller |
|---|---|---|
| D-pad | Arrow keys | D-pad or left stick |
| A / B | Z / X | A / B |
| Select / Start | Right Shift / Enter | Back / Start |
| Fast-forward (hold; four times speed, sound muted) | Tab | Right trigger |
| Pause | P | |
| Save state / load state | F5 / F8 | |
| Fullscreen | F11 | |
| Colour correction on or off (Game Boy Color) | F9 | |
| Tilt (MBC7 cartridges) | I, K, J, L | Right stick |
| Quit | Escape | |

Controllers are picked up when connected. Keys can be rebound in a `controls.cfg` file in the emulator's settings folder (`~/.local/share/lr35902-emu/` on Linux, `%APPDATA%\lr35902-emu\` on Windows, `~/Library/Application Support/lr35902-emu/` on macOS), or in a file given with `--controls FILE`. Each line binds an action to one or more keys by their SDL names; actions not listed keep their default keys:

```text
# controls.cfg
a = Space
b = Left Ctrl
start = Return, Keypad Enter
fast_forward = Backspace
```

The actions are `up`, `down`, `left`, `right`, `a`, `b`, `select`, `start`, `tilt_up`, `tilt_down`, `tilt_left`, `tilt_right`, `fast_forward`, `pause`, `save_state`, `load_state`, `fullscreen`, `color_correction`, and `quit`. The emulator stops with a message if the file has a mistake or binds one key to two actions.

The window can be resized and scales the picture by whole numbers. `--scale N` sets its starting size (1–10 times the Game Boy's 160×144, default 4) and `--fullscreen` starts in fullscreen.

### Save states

F5 saves the whole machine state to a same-basename `.state` file beside the ROM, and F8 loads it back. A state only loads into a version of the emulator with the same save-state format, running the same ROM. Loading a state also restores the cartridge RAM it was saved with, which is then written to the `.sav` file on exit as usual.

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
│   ├── LR35902.h        # CPU state model, flags, public API
│   ├── PPU.h            # PPU state and pixel pipeline
│   ├── APU.h            # Sound channel state and sample output
│   ├── SaveState.h      # Save state serialisation
│   └── Controls.h       # Player actions and their bindings
├── src/
│   ├── LR35902.cpp      # Instruction decoder, flags, interrupt stepping
│   ├── Memory.cpp       # Memory routing and access rules
│   ├── Cartridge.cpp    # Cartridge validation, banking, and save RAM
│   ├── PPU.cpp          # LCD modes, scanlines, and the per-dot pixel FIFO
│   ├── APU.cpp          # Sound channels, frame sequencer, and mixing
│   ├── SaveState.cpp    # Save state format, loading, and saving
│   ├── Controls.cpp     # Key bindings, controls file, and controller layout
│   ├── CgbCompatibility.cpp # CGB boot ROM palettes for games made for the original Game Boy
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
│   ├── apu_tests.cpp        # Sound registers, length counters, and output
│   ├── save_state_tests.cpp # Save state round trips and rejected states
│   ├── controls_tests.cpp   # Key binding defaults and controls file parsing
│   ├── rom_test_runner.cpp  # ROM conformance runner (serial, signatures, screenshots)
│   ├── fetch_test_roms.sh   # Downloads the ROM test suites at pinned versions
│   ├── run_rom_suites.sh    # Runs the Mooneye, blargg, acid2, Mealybug, and SameSuite suites
│   ├── rom_known_failures.tsv # ROM test failures expected for now
│   └── compare_screenshot.py # Compares screenshots with DMG reference images
└── build/               # Generated CMake build output
```

## References

- [Pan Docs](https://gbdev.io/pandocs/) — the canonical Game Boy hardware reference
- [gbops](https://izik1.github.io/gbops/) — opcode table / timing reference
- [SameBoy](https://github.com/LIJI32/SameBoy) — the CGB boot ROM palette tables in `src/CgbCompatibility.cpp` come from its free boot ROM reimplementation (Expat License; the notice is in that file)

## License

[MIT](LICENSE) — Copyright © 2026 Dimeji
