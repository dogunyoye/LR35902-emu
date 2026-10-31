#include "PPU.h"

#include <algorithm>

namespace
{
    constexpr std::uint16_t LCDC = 0xFF40;
    constexpr std::uint16_t STAT = 0xFF41;
    constexpr std::uint16_t SCY = 0xFF42;
    constexpr std::uint16_t SCX = 0xFF43;
    constexpr std::uint16_t LY = 0xFF44;
    constexpr std::uint16_t LYC = 0xFF45;
    constexpr std::uint16_t BGP = 0xFF47;
    constexpr std::uint16_t VRAM_START = 0x8000;
    constexpr std::uint16_t BG_MAP_0 = 0x9800;
    constexpr std::uint16_t BG_MAP_1 = 0x9C00;

    std::uint32_t shade_color(std::uint8_t shade)
    {
        switch (shade)
        {
        case 0:
            return 0xFF081820;
        case 1:
            return 0xFF3A5F40;
        case 2:
            return 0xFF9BBC0F;
        default:
            return 0xFFFFFFFF;
        }
    }

    void update_status(CPU_LR35902 &cpu, PPU_LR35902 &ppu, std::uint8_t mode)
    {
        ppu.mode = mode;
        std::uint8_t stat = cpu.memory[STAT];
        stat = static_cast<std::uint8_t>((stat & 0xF8) | (mode & 0x03));
        if (cpu.memory[LY] == cpu.memory[LYC])
        {
            stat |= 0x04;
        }
        else
        {
            stat &= static_cast<std::uint8_t>(~0x04);
        }
        cpu.memory[STAT] = stat;

        cpu.vram_locked = ppu.lcd_enabled && mode == 3;
        cpu.oam_locked = ppu.lcd_enabled && (mode == 2 || mode == 3);
    }

    void render_scanline(const CPU_LR35902 &cpu, PPU_LR35902 &ppu)
    {
        const int y = cpu.memory[LY];
        if (y >= PPU_LR35902::HEIGHT)
        {
            return;
        }

        const std::uint8_t lcdc = cpu.memory[LCDC];
        const std::uint8_t palette = cpu.memory[BGP];
        for (int x = 0; x < PPU_LR35902::WIDTH; ++x)
        {
            std::uint8_t color_index = 0;
            if ((lcdc & 0x01) != 0)
            {
                const std::uint8_t world_x = static_cast<std::uint8_t>(x + cpu.memory[SCX]);
                const std::uint8_t world_y = static_cast<std::uint8_t>(y + cpu.memory[SCY]);
                const std::uint16_t map_base = (lcdc & 0x08) != 0 ? BG_MAP_1 : BG_MAP_0;
                const std::uint16_t map_offset = static_cast<std::uint16_t>((world_y / 8) * 32 + world_x / 8);
                const std::uint8_t tile_id = cpu.memory[map_base + map_offset];

                std::uint16_t tile_base;
                if ((lcdc & 0x10) != 0)
                {
                    tile_base = static_cast<std::uint16_t>(VRAM_START + tile_id * 16);
                }
                else
                {
                    const int signed_tile_id = tile_id < 0x80 ? tile_id : static_cast<int>(tile_id) - 0x100;
                    tile_base = static_cast<std::uint16_t>(0x9000 + signed_tile_id * 16);
                }

                const std::uint16_t row_address = static_cast<std::uint16_t>(tile_base + (world_y & 0x07) * 2);
                const std::uint8_t bit = static_cast<std::uint8_t>(1u << (7 - (world_x & 0x07)));
                color_index = static_cast<std::uint8_t>(
                    ((cpu.memory[row_address] & bit) != 0 ? 1 : 0) |
                    ((cpu.memory[row_address + 1] & bit) != 0 ? 2 : 0));
            }

            const std::uint8_t shade = static_cast<std::uint8_t>((palette >> (color_index * 2)) & 0x03);
            ppu.framebuffer[static_cast<std::size_t>(y) * PPU_LR35902::WIDTH + x] = shade_color(shade);
        }
    }

    void advance_scanline(CPU_LR35902 &cpu, PPU_LR35902 &ppu)
    {
        std::uint8_t ly = cpu.memory[LY];
        ly = ly == 153 ? 0 : static_cast<std::uint8_t>(ly + 1);
        cpu.memory[LY] = ly;

        if (ly == 144)
        {
            cpu.memory[0xFF0F] |= 0x01;
            update_status(cpu, ppu, 1);
        }
        else if (ly < 144)
        {
            update_status(cpu, ppu, 2);
        }
        else
        {
            update_status(cpu, ppu, 1);
        }
    }
}

void update_ppu(CPU_LR35902 &cpu, PPU_LR35902 &ppu, int cycles)
{
    if (!ppu.framebuffer_initialized)
    {
        std::fill(ppu.framebuffer.begin(), ppu.framebuffer.end(), shade_color(0));
        ppu.framebuffer_initialized = true;
    }

    if ((cpu.memory[LCDC] & 0x80) == 0)
    {
        if (ppu.lcd_enabled)
        {
            std::fill(ppu.framebuffer.begin(), ppu.framebuffer.end(), shade_color(0));
        }
        ppu.lcd_enabled = false;
        ppu.cycles_into_line = 0;
        cpu.memory[LY] = 0;
        update_status(cpu, ppu, 0);
        return;
    }

    if (!ppu.lcd_enabled)
    {
        ppu.lcd_enabled = true;
        ppu.cycles_into_line = 0;
        cpu.memory[LY] = 0;
        update_status(cpu, ppu, 2);
    }

    while (cycles > 0)
    {
        int mode_boundary;
        if (ppu.mode == 2)
        {
            mode_boundary = 80;
        }
        else if (ppu.mode == 3)
        {
            mode_boundary = 252;
        }
        else
        {
            mode_boundary = 456;
        }

        const int step = std::min(cycles, mode_boundary - ppu.cycles_into_line);
        ppu.cycles_into_line += step;
        cycles -= step;

        if (ppu.cycles_into_line != mode_boundary)
        {
            continue;
        }

        if (ppu.mode == 2)
        {
            update_status(cpu, ppu, 3);
        }
        else if (ppu.mode == 3)
        {
            render_scanline(cpu, ppu);
            update_status(cpu, ppu, 0);
        }
        else
        {
            ppu.cycles_into_line = 0;
            advance_scanline(cpu, ppu);
        }
    }
}