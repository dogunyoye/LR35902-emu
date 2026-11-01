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
    constexpr std::uint16_t OBP0 = 0xFF48;
    constexpr std::uint16_t OBP1 = 0xFF49;
    constexpr std::uint16_t WY = 0xFF4A;
    constexpr std::uint16_t WX = 0xFF4B;
    constexpr std::uint16_t VRAM_START = 0x8000;
    constexpr std::uint16_t BG_MAP_0 = 0x9800;
    constexpr std::uint16_t BG_MAP_1 = 0x9C00;
    constexpr std::uint16_t OAM_START = 0xFE00;

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

        const bool stat_line = ppu.lcd_enabled &&
                               (((mode == 0) && (stat & 0x08) != 0) ||
                                ((mode == 1) && (stat & 0x10) != 0) ||
                                ((mode == 2) && (stat & 0x20) != 0) ||
                                ((stat & 0x44) == 0x44));
        if (stat_line && !ppu.stat_interrupt_line)
        {
            cpu.memory[0xFF0F] |= 0x02;
        }
        ppu.stat_interrupt_line = stat_line;
    }

    std::uint8_t read_tile_pixel(const CPU_LR35902 &cpu, std::uint8_t lcdc,
                                 std::uint16_t map_base, int pixel_x, int pixel_y)
    {
        const std::uint8_t world_x = static_cast<std::uint8_t>(pixel_x);
        const std::uint8_t world_y = static_cast<std::uint8_t>(pixel_y);
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
        return static_cast<std::uint8_t>(
            ((cpu.memory[row_address] & bit) != 0 ? 1 : 0) |
            ((cpu.memory[row_address + 1] & bit) != 0 ? 2 : 0));
    }

    std::uint32_t palette_color(const CPU_LR35902 &cpu, std::uint16_t palette_address, std::uint8_t color_index)
    {
        const std::uint8_t palette = cpu.memory[palette_address];
        const std::uint8_t shade = static_cast<std::uint8_t>((palette >> (color_index * 2)) & 0x03);
        return shade_color(shade);
    }

    int calculate_mode3_duration(const CPU_LR35902 &cpu, int line)
    {
        const std::uint8_t lcdc = cpu.memory[LCDC];
        int duration = 172 + (cpu.memory[SCX] & 0x07);

        if ((lcdc & 0x01) != 0 && (lcdc & 0x20) != 0 &&
            line >= cpu.memory[WY] && cpu.memory[WX] <= 166)
        {
            duration += 6;
        }

        if ((lcdc & 0x02) != 0)
        {
            const int sprite_height = (lcdc & 0x04) != 0 ? 16 : 8;
            std::array<std::uint8_t, 10> selected_sprites{};
            std::size_t selected_count = 0;
            for (std::uint8_t index = 0; index < 40 && selected_count < selected_sprites.size(); ++index)
            {
                const std::uint16_t address = static_cast<std::uint16_t>(OAM_START + index * 4);
                const int top = static_cast<int>(cpu.memory[address]) - 16;
                if (line < top || line >= top + sprite_height)
                {
                    continue;
                }

                selected_sprites[selected_count++] = cpu.memory[address + 1];
            }

            std::sort(selected_sprites.begin(), selected_sprites.begin() + selected_count);
            std::array<bool, 32> fetched_background_tiles{};
            for (std::size_t index = 0; index < selected_count; ++index)
            {
                const int sprite_x = selected_sprites[index];
                if (sprite_x >= 168)
                {
                    continue;
                }

                if (sprite_x == 0)
                {
                    duration += 11;
                    continue;
                }

                const int world_x = sprite_x - 8 + cpu.memory[SCX];
                const int alignment = world_x & 0x07;
                const std::size_t tile = static_cast<std::size_t>((world_x / 8) & 0x1F);
                if (fetched_background_tiles[tile])
                {
                    duration += 6;
                    continue;
                }

                fetched_background_tiles[tile] = true;
                duration += 6 + std::max(0, 5 - alignment);
            }
        }

        return duration;
    }

    void render_scanline(const CPU_LR35902 &cpu, PPU_LR35902 &ppu)
    {
        const int y = cpu.memory[LY];
        if (y >= PPU_LR35902::HEIGHT)
        {
            return;
        }

        const std::uint8_t lcdc = cpu.memory[LCDC];
        const bool background_enabled = (lcdc & 0x01) != 0;
        const bool window_enabled = background_enabled && (lcdc & 0x20) != 0 &&
                                    y >= cpu.memory[WY] && cpu.memory[WX] <= 166;
        const int window_left = static_cast<int>(cpu.memory[WX]) - 7;
        const std::uint16_t background_map = (lcdc & 0x08) != 0 ? BG_MAP_1 : BG_MAP_0;
        const std::uint16_t window_map = (lcdc & 0x40) != 0 ? BG_MAP_1 : BG_MAP_0;
        std::array<std::uint8_t, PPU_LR35902::WIDTH> background_pixels{};

        for (int x = 0; x < PPU_LR35902::WIDTH; ++x)
        {
            std::uint8_t color_index = 0;
            if (background_enabled)
            {
                if (window_enabled && x >= window_left)
                {
                    color_index = read_tile_pixel(cpu, lcdc, window_map,
                                                  x - window_left, y - cpu.memory[WY]);
                }
                else
                {
                    color_index = read_tile_pixel(cpu, lcdc, background_map,
                                                  x + cpu.memory[SCX], y + cpu.memory[SCY]);
                }
            }

            background_pixels[static_cast<std::size_t>(x)] = color_index;
            ppu.framebuffer[static_cast<std::size_t>(y) * PPU_LR35902::WIDTH + x] =
                palette_color(cpu, BGP, color_index);
        }

        if ((lcdc & 0x02) == 0)
        {
            return;
        }

        struct Sprite
        {
            int left;
            int top;
            std::uint8_t tile;
            std::uint8_t attributes;
            std::uint8_t oam_index;
        };

        std::array<Sprite, 10> selected_sprites{};
        std::size_t selected_count = 0;
        const int sprite_height = (lcdc & 0x04) != 0 ? 16 : 8;
        for (std::uint8_t index = 0; index < 40 && selected_count < selected_sprites.size(); ++index)
        {
            const std::uint16_t address = static_cast<std::uint16_t>(OAM_START + index * 4);
            const int top = static_cast<int>(cpu.memory[address]) - 16;
            if (y < top || y >= top + sprite_height)
            {
                continue;
            }

            selected_sprites[selected_count++] = {
                static_cast<int>(cpu.memory[address + 1]) - 8,
                top,
                cpu.memory[address + 2],
                cpu.memory[address + 3],
                index};
        }

        std::sort(selected_sprites.begin(), selected_sprites.begin() + selected_count,
                  [](const Sprite &left, const Sprite &right)
                  {
                      return left.left != right.left ? left.left < right.left
                                                     : left.oam_index < right.oam_index;
                  });

        std::array<bool, PPU_LR35902::WIDTH> sprite_pixels_claimed{};
        for (std::size_t sprite_index = 0; sprite_index < selected_count; ++sprite_index)
        {
            const Sprite &sprite = selected_sprites[sprite_index];

            int tile_row = y - sprite.top;
            if ((sprite.attributes & 0x40) != 0)
            {
                tile_row = sprite_height - 1 - tile_row;
            }

            int tile_index = sprite.tile;
            if (sprite_height == 16)
            {
                tile_index &= 0xFE;
                tile_index += tile_row / 8;
                tile_row %= 8;
            }

            const std::uint16_t row_address = static_cast<std::uint16_t>(
                VRAM_START + tile_index * 16 + tile_row * 2);
            for (int pixel_x = 0; pixel_x < 8; ++pixel_x)
            {
                const int screen_x = sprite.left + pixel_x;
                if (screen_x < 0 || screen_x >= PPU_LR35902::WIDTH ||
                    sprite_pixels_claimed[static_cast<std::size_t>(screen_x)])
                {
                    continue;
                }

                const int bit_index = (sprite.attributes & 0x20) != 0 ? pixel_x : 7 - pixel_x;
                const std::uint8_t bit = static_cast<std::uint8_t>(1u << bit_index);
                const std::uint8_t color_index = static_cast<std::uint8_t>(
                    ((cpu.memory[row_address] & bit) != 0 ? 1 : 0) |
                    ((cpu.memory[row_address + 1] & bit) != 0 ? 2 : 0));
                if (color_index == 0)
                {
                    continue;
                }

                sprite_pixels_claimed[static_cast<std::size_t>(screen_x)] = true;
                const bool behind_background = (sprite.attributes & 0x80) != 0;
                if (behind_background && background_pixels[static_cast<std::size_t>(screen_x)] != 0)
                {
                    continue;
                }

                const std::uint16_t palette_address = (sprite.attributes & 0x10) != 0 ? OBP1 : OBP0;
                ppu.framebuffer[static_cast<std::size_t>(y) * PPU_LR35902::WIDTH + screen_x] =
                    palette_color(cpu, palette_address, color_index);
            }
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
            if ((cpu.memory[STAT] & 0x20) != 0)
            {
                cpu.memory[0xFF0F] |= 0x02;
            }
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
        ppu.startup_mode0 = false;
        ppu.startup_first_line = false;
        ppu.startup_first_enable = false;
        ppu.cycles_into_line = 0;
        ppu.mode = 0;
        cpu.memory[STAT] &= 0xFC; // Freeze LY=LYC while the LCD clock is stopped.
        cpu.vram_locked = false;
        cpu.oam_locked = false;
        return;
    }

    if (!ppu.lcd_enabled)
    {
        ppu.lcd_enabled = true;
        ppu.cycles_into_line = 0;
        cpu.memory[LY] = 0;
        if (ppu.has_started)
        {
            ppu.startup_mode0 = true;
            ppu.startup_first_line = true;
            ppu.startup_first_enable = !ppu.has_reenabled;
            ppu.has_reenabled = true;
            update_status(cpu, ppu, 0);
        }
        else
        {
            ppu.has_started = true;
            update_status(cpu, ppu, 2);
        }
    }
    else
    {
        update_status(cpu, ppu, ppu.mode);
    }

    while (cycles > 0)
    {
        int mode_boundary;
        if (ppu.mode == 2)
        {
            mode_boundary = 80;
        }
        else if (ppu.mode == 0 && ppu.startup_mode0)
        {
            mode_boundary = 84;
        }
        else if (ppu.mode == 0 && ppu.startup_first_line)
        {
            mode_boundary = 458;
        }
        else if (ppu.mode == 3)
        {
            mode_boundary = 80 + ppu.mode3_duration;
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
            ppu.mode3_duration = calculate_mode3_duration(cpu, cpu.memory[LY]);
            update_status(cpu, ppu, 3);
        }
        else if (ppu.mode == 0 && ppu.startup_mode0)
        {
            ppu.startup_mode0 = false;
            ppu.mode3_duration = calculate_mode3_duration(cpu, cpu.memory[LY]);
            ppu.mode3_duration += 2;
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
            if (ppu.startup_first_line)
            {
                ppu.startup_first_line = false;
                ppu.startup_first_enable = false;
            }
            advance_scanline(cpu, ppu);
        }
    }

    if (ppu.mode == 2 && ppu.cycles_into_line >= 76)
    {
        cpu.oam_locked = false;
    }
}
