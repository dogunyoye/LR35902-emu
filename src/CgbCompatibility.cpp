// The CGB boot ROM's colourisation of games made for the original Game Boy.
//
// The tables below are taken from SameBoy's free reimplementation of the CGB
// boot ROM (BootROMs/cgb_boot.asm, https://github.com/LIJI32/SameBoy), which
// reproduces the official boot ROM's choices, under the Expat License:
//
// :
//
// Expat License
//
// Copyright (c) 2015-2026 Lior Halphon
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include "LR35902.h"

#include <array>
#include <cstddef>
#include <string>
#include <vector>

namespace
{
    // Title checksums of Nintendo-published games. From FIRST_DUPLICATE on, a
    // checksum is shared and the title's fourth letter (DUPLICATE_LETTERS)
    // tells the games apart.
    constexpr std::array<std::uint8_t, 94> TITLE_CHECKSUMS = {{
        0x00, 0x88, 0x16, 0x36, 0xD1, 0xDB, 0xF2, 0x3C, 0x8C, 0x92, 0x3D, 0x5C,
        0x58, 0xC9, 0x3E, 0x70, 0x1D, 0x59, 0x69, 0x19, 0x35, 0xA8, 0x14, 0xAA,
        0x75, 0x95, 0x99, 0x34, 0x6F, 0x15, 0xFF, 0x97, 0x4B, 0x90, 0x17, 0x10,
        0x39, 0xF7, 0xF6, 0xA2, 0x49, 0x4E, 0x43, 0x68, 0xE0, 0x8B, 0xF0, 0xCE,
        0x0C, 0x29, 0xE8, 0xB7, 0x86, 0x9A, 0x52, 0x01, 0x9D, 0x71, 0x9C, 0xBD,
        0x5D, 0x6D, 0x67, 0x3F, 0x6B, 0xB3, 0x46, 0x28, 0xA5, 0xC6, 0xD3, 0x27,
        0x61, 0x18, 0x66, 0x6A, 0xBF, 0x0D, 0xF4, 0xB3, 0x46, 0x28, 0xA5, 0xC6,
        0xD3, 0x27, 0x61, 0x18, 0x66, 0x6A, 0xBF, 0x0D, 0xF4, 0xB3,
    }};
    constexpr std::size_t FIRST_DUPLICATE = 65;
    constexpr char DUPLICATE_LETTERS[] = "BEFAARBEKEK R-URAR INAILICE R";

    // The palette combination for each checksum. Bit 7 asks for the DMG boot
    // ROM's logo tilemap to be left in VRAM.
    constexpr std::array<std::uint8_t, 94> COMBINATION_PER_CHECKSUM = {{
        0x00, 0x04, 0x05, 0x23, 0x22, 0x03, 0x1F, 0x0F, 0x0A, 0x05, 0x13, 0x24,
        0x87, 0x25, 0x1E, 0x2C, 0x15, 0x20, 0x1F, 0x14, 0x05, 0x21, 0x0D, 0x0E,
        0x05, 0x1D, 0x05, 0x12, 0x09, 0x03, 0x02, 0x1A, 0x19, 0x19, 0x29, 0x2A,
        0x1A, 0x2D, 0x2A, 0x2D, 0x24, 0x26, 0x9A, 0x2A, 0x1E, 0x29, 0x22, 0x22,
        0x05, 0x2A, 0x06, 0x05, 0x21, 0x19, 0x2A, 0x2A, 0x28, 0x02, 0x10, 0x19,
        0x2A, 0x2A, 0x05, 0x00, 0x27, 0x24, 0x16, 0x19, 0x06, 0x20, 0x0C, 0x24,
        0x0B, 0x27, 0x12, 0x27, 0x18, 0x1F, 0x32, 0x11, 0x2E, 0x06, 0x1B, 0x00,
        0x2F, 0x29, 0x29, 0x00, 0x00, 0x13, 0x22, 0x17, 0x12, 0x1D,
    }};

    // Byte offsets into PALETTES of the colours for OBJ palette 0, OBJ palette
    // 1, and BG palette 0. A few combinations start part way through a palette.
    struct Combination
    {
        std::uint8_t obj0;
        std::uint8_t obj1;
        std::uint8_t bg;
    };
    constexpr std::array<Combination, 51> COMBINATIONS = {{
        {32, 32, 232}, // 0
        {144, 144, 144}, // 1
        {160, 160, 160}, // 2
        {192, 192, 192}, // 3
        {72, 72, 72}, // 4
        {0, 0, 0}, // 5
        {216, 216, 216}, // 6
        {40, 40, 40}, // 7
        {96, 96, 96}, // 8
        {208, 208, 208}, // 9
        {128, 64, 64}, // 10
        {32, 224, 224}, // 11
        {32, 16, 16}, // 12
        {24, 32, 32}, // 13
        {32, 232, 232}, // 14
        {224, 32, 224}, // 15
        {16, 136, 16}, // 16
        {128, 128, 64}, // 17
        {32, 32, 56}, // 18
        {32, 32, 144}, // 19
        {32, 32, 160}, // 20
        {152, 152, 72}, // 21
        {30, 30, 88}, // 22
        {136, 136, 16}, // 23
        {32, 32, 16}, // 24
        {32, 32, 24}, // 25
        {224, 224, 0}, // 26
        {24, 24, 0}, // 27
        {0, 0, 8}, // 28
        {144, 176, 144}, // 29
        {160, 176, 160}, // 30
        {192, 176, 192}, // 31
        {128, 176, 64}, // 32
        {136, 32, 104}, // 33
        {222, 0, 112}, // 34
        {222, 32, 120}, // 35
        {152, 182, 72}, // 36
        {128, 224, 80}, // 37
        {32, 184, 224}, // 38
        {136, 176, 16}, // 39
        {32, 0, 16}, // 40
        {32, 224, 24}, // 41
        {224, 24, 0}, // 42
        {24, 224, 32}, // 43
        {168, 224, 32}, // 44
        {24, 224, 0}, // 45
        {200, 24, 224}, // 46
        {0, 224, 64}, // 47
        {32, 24, 224}, // 48
        {224, 24, 48}, // 49
        {32, 224, 232}, // 50
    }};

    // RGB555 colours, four to a palette.
    constexpr std::array<std::uint16_t, 120> PALETTES = {{
        0x7FFF, 0x32BF, 0x00D0, 0x0000, // 0
        0x639F, 0x4279, 0x15B0, 0x04CB, // 1
        0x7FFF, 0x6E31, 0x454A, 0x0000, // 2
        0x7FFF, 0x1BEF, 0x0200, 0x0000, // 3
        0x7FFF, 0x421F, 0x1CF2, 0x0000, // 4
        0x7FFF, 0x5294, 0x294A, 0x0000, // 5
        0x7FFF, 0x03FF, 0x012F, 0x0000, // 6
        0x7FFF, 0x03EF, 0x01D6, 0x0000, // 7
        0x7FFF, 0x42B5, 0x3DC8, 0x0000, // 8
        0x7E74, 0x03FF, 0x0180, 0x0000, // 9
        0x67FF, 0x77AC, 0x1A13, 0x2D6B, // 10
        0x7ED6, 0x4BFF, 0x2175, 0x0000, // 11
        0x53FF, 0x4A5F, 0x7E52, 0x0000, // 12
        0x4FFF, 0x7ED2, 0x3A4C, 0x1CE0, // 13
        0x03ED, 0x7FFF, 0x255F, 0x0000, // 14
        0x036A, 0x021F, 0x03FF, 0x7FFF, // 15
        0x7FFF, 0x01DF, 0x0112, 0x0000, // 16
        0x231F, 0x035F, 0x00F2, 0x0009, // 17
        0x7FFF, 0x03EA, 0x011F, 0x0000, // 18
        0x299F, 0x001A, 0x000C, 0x0000, // 19
        0x7FFF, 0x027F, 0x001F, 0x0000, // 20
        0x7FFF, 0x03E0, 0x0206, 0x0120, // 21
        0x7FFF, 0x7EEB, 0x001F, 0x7C00, // 22
        0x7FFF, 0x3FFF, 0x7E00, 0x001F, // 23
        0x7FFF, 0x03FF, 0x001F, 0x0000, // 24
        0x03FF, 0x001F, 0x000C, 0x0000, // 25
        0x7FFF, 0x033F, 0x0193, 0x0000, // 26
        0x0000, 0x4200, 0x037F, 0x7FFF, // 27
        0x7FFF, 0x7E8C, 0x7C00, 0x0000, // 28
        0x7FFF, 0x1BEF, 0x6180, 0x0000, // 29
    }};

    // Combinations the player could pick by holding buttons during the logo.
    struct ManualChoice
    {
        const char *buttons;
        std::uint8_t combination;
    };
    constexpr std::array<ManualChoice, 12> MANUAL_CHOICES = {{
        {"right", 1},
        {"left", 48},
        {"up", 5},
        {"down", 8},
        {"right+a", 0},
        {"left+a", 40},
        {"up+a", 43},
        {"down+a", 3},
        {"right+b", 6},
        {"left+b", 7},
        {"up+b", 28},
        {"down+b", 49},
    }};
}

std::size_t dmg_compatibility_combination(const std::vector<uint8_t> &rom, bool &wants_dmg_logo)
{
    wants_dmg_logo = false;
    // Only Nintendo's own games are recognised; everything else gets combination 0.
    const bool nintendo = rom[0x014B] == 0x33 ? rom[0x0144] == '0' && rom[0x0145] == '1' : rom[0x014B] == 0x01;
    if (!nintendo)
    {
        return COMBINATION_PER_CHECKSUM[0];
    }
    std::uint8_t checksum = 0;
    for (std::size_t address = 0x0134; address <= 0x0143; ++address)
    {
        checksum = static_cast<std::uint8_t>(checksum + rom[address]);
    }
    for (std::size_t index = 0; index < TITLE_CHECKSUMS.size(); ++index)
    {
        if (TITLE_CHECKSUMS[index] == checksum &&
            (index < FIRST_DUPLICATE || rom[0x0137] == DUPLICATE_LETTERS[index - FIRST_DUPLICATE]))
        {
            wants_dmg_logo = (COMBINATION_PER_CHECKSUM[index] & 0x80) != 0;
            return COMBINATION_PER_CHECKSUM[index] & 0x7F;
        }
    }
    return COMBINATION_PER_CHECKSUM[0];
}

namespace
{
    void write_palette(std::array<std::uint8_t, 64> &palette_ram, int palette, std::size_t byte_offset)
    {
        for (std::size_t color = 0; color < 4; ++color)
        {
            const std::uint16_t value = PALETTES[byte_offset / 2 + color];
            palette_ram[static_cast<std::size_t>(palette) * 8 + color * 2] = static_cast<std::uint8_t>(value);
            palette_ram[static_cast<std::size_t>(palette) * 8 + color * 2 + 1] = static_cast<std::uint8_t>(value >> 8);
        }
    }
}

bool load_dmg_compatibility_palettes(CPU_LR35902 &cpu, const std::string &manual_choice)
{
    bool wants_dmg_logo = false;
    std::size_t combination = dmg_compatibility_combination(cpu.cartridge_rom, wants_dmg_logo);
    if (!manual_choice.empty())
    {
        bool found = false;
        for (const ManualChoice &choice : MANUAL_CHOICES)
        {
            if (manual_choice == choice.buttons)
            {
                combination = choice.combination;
                found = true;
            }
        }
        if (!found)
        {
            return false;
        }
    }
    const Combination &palettes = COMBINATIONS[combination];
    write_palette(cpu.obj_palette_ram, 0, palettes.obj0);
    write_palette(cpu.obj_palette_ram, 1, palettes.obj1);
    write_palette(cpu.bg_palette_ram, 0, palettes.bg);
    if (wants_dmg_logo)
    {
        load_dmg_boot_logo(cpu);
    }
    return true;
}

std::vector<std::string> dmg_compatibility_palette_names()
{
    std::vector<std::string> names;
    for (const ManualChoice &choice : MANUAL_CHOICES)
    {
        names.emplace_back(choice.buttons);
    }
    return names;
}
