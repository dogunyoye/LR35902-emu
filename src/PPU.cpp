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

    // Dot within a line at which STAT reports mode 3. Lines are timed from the
    // dot LY changes, so these include the four dots before mode 2 is reported.
    constexpr int MODE3_START = 81;
    constexpr int STARTUP_MODE3_START = 83;

    std::uint32_t shade_color(std::uint8_t shade)
    {
        // Shade 0 is the lightest colour on the DMG LCD.
        switch (shade)
        {
        case 0:
            return 0xFFE0F8D0;
        case 1:
            return 0xFF88C070;
        case 2:
            return 0xFF346856;
        default:
            return 0xFF081820;
        }
    }

    void update_status(CPU_LR35902 &cpu, PPU_LR35902 &ppu, std::uint8_t mode)
    {
        ppu.mode = mode;
        std::uint8_t stat = cpu.memory[STAT];
        stat = static_cast<std::uint8_t>((stat & 0xF8) | (mode & 0x03));
        if (ppu.ly_for_comparison >= 0 && ppu.ly_for_comparison == cpu.memory[LYC])
        {
            stat |= 0x04;
        }
        else
        {
            stat &= static_cast<std::uint8_t>(~0x04);
        }
        cpu.memory[STAT] = stat;

        // VRAM reads lock at dot 80, just before STAT reports mode 3. Writes are
        // checked separately by ppu_blocks_vram_write and ppu_blocks_oam_write.
        const bool late_mode2 = mode == 2 && ppu.cycles_into_line >= 80;
        cpu.vram_locked = ppu.lcd_enabled && (mode == 3 || late_mode2);
        cpu.oam_locked = ppu.lcd_enabled &&
                         (mode == 2 || mode == 3 || ppu.line_start_hold);

        // The mode 2 interrupt is raised as LY changes, before STAT reports
        // mode 2, and the mode 0 interrupt one dot before STAT reports mode 0.
        const bool mode0_line = mode == 0 ||
                                (mode == 3 && ppu.cycles_into_line >= ppu.mode3_end - 1);
        const bool mode2_line = mode == 2 || (ppu.line_start_hold && cpu.memory[LY] != 0);
        const bool stat_line = ppu.lcd_enabled &&
                               ((mode0_line && (stat & 0x08) != 0) ||
                                ((mode == 1) && (stat & 0x10) != 0 && (cpu.memory[LY] != 144 || ppu.cycles_into_line >= 4)) ||
                                (mode2_line && (stat & 0x20) != 0) ||
                                ((stat & 0x44) == 0x44));
        if (stat_line && !ppu.stat_interrupt_line)
        {
            cpu.memory[0xFF0F] |= 0x02;
        }
        ppu.stat_interrupt_line = stat_line;
    }

    std::uint32_t palette_color(const CPU_LR35902 &cpu, std::uint16_t palette_address, std::uint8_t color_index)
    {
        const std::uint8_t palette = cpu.memory[palette_address];
        const std::uint8_t shade = static_cast<std::uint8_t>((palette >> (color_index * 2)) & 0x03);
        return shade_color(shade);
    }

    // The window is fetched once LY has matched WY during the frame, while it
    // is enabled and WX places it on screen. LCDC bit 0 only blanks its pixels.
    bool window_visible(const CPU_LR35902 &cpu, const PPU_LR35902 &ppu)
    {
        return (cpu.memory[LCDC] & 0x20) != 0 && ppu.window_y_triggered && cpu.memory[WX] <= 166;
    }

    // OAM scan: the first ten objects overlapping this line, in OAM order.
    void scan_line_objects(const CPU_LR35902 &cpu, PPU_LR35902 &ppu, int line)
    {
        const int object_height = (cpu.memory[LCDC] & 0x04) != 0 ? 16 : 8;
        ppu.line_object_count = 0;
        for (std::uint8_t index = 0; index < 40 && ppu.line_object_count < 10; ++index)
        {
            const std::uint16_t address = static_cast<std::uint16_t>(OAM_START + index * 4);
            const int top = static_cast<int>(cpu.memory[address]) - 16;
            if (line >= top && line < top + object_height)
            {
                ppu.line_objects[static_cast<std::size_t>(ppu.line_object_count++)] = {
                    index, cpu.memory[address + 1], cpu.memory[address], false};
            }
        }
    }

    // Dots an object fetch stalls mode 3. The first object in a background tile
    // also waits for that tile's fetch to finish; X=0 objects sit in the tile
    // left of the screen.
    int object_penalty(const CPU_LR35902 &cpu, std::array<bool, 32> &penalised_tiles, std::uint8_t object_x)
    {
        const std::uint8_t world_x = static_cast<std::uint8_t>(object_x - 8 + cpu.memory[SCX]);
        const std::size_t tile = world_x / 8;
        if (penalised_tiles[tile])
        {
            return 6;
        }
        penalised_tiles[tile] = true;
        return 6 + std::max(0, 5 - (world_x & 0x07));
    }

    // Objects are fetched from left to right, ties in OAM order.
    int next_object(const std::array<PPU_LR35902::LineObject, 10> &objects, int count)
    {
        int next = -1;
        for (int index = 0; index < count; ++index)
        {
            const PPU_LR35902::LineObject &object = objects[static_cast<std::size_t>(index)];
            if (!object.fetched && object.x < 168 &&
                (next < 0 || object.x < objects[static_cast<std::size_t>(next)].x))
            {
                next = index;
            }
        }
        return next;
    }

    int calculate_mode3_duration(const CPU_LR35902 &cpu, const PPU_LR35902 &ppu)
    {
        int duration = 172 + (cpu.memory[SCX] & 0x07);
        if (window_visible(cpu, ppu))
        {
            duration += 6;
        }

        if ((cpu.memory[LCDC] & 0x02) != 0)
        {
            std::array<PPU_LR35902::LineObject, 10> objects = ppu.line_objects;
            std::array<bool, 32> penalised_tiles{};
            for (int index = next_object(objects, ppu.line_object_count); index >= 0;
                 index = next_object(objects, ppu.line_object_count))
            {
                PPU_LR35902::LineObject &object = objects[static_cast<std::size_t>(index)];
                object.fetched = true;
                duration += object_penalty(cpu, penalised_tiles, object.x);
            }
        }
        return duration;
    }

    void start_pixel_pipeline(const CPU_LR35902 &cpu, PPU_LR35902 &ppu)
    {
        ppu.fetch_step = 0;
        ppu.fetch_tile_x = 0;
        ppu.fetch_window = false;
        ppu.fetch_first_tile = true;
        ppu.background_count = 0;
        ppu.object_colors.fill(0);
        ppu.pixel_x = 0;
        ppu.discard_pixels = cpu.memory[SCX] & 0x07; // Only read as the line starts.
        ppu.stall_dots = 0;
        ppu.stalled_object = -1;
        ppu.window_active = false;
        ppu.window_drawn = false;
        ppu.penalised_tiles.fill(false);
        ppu.pending_count = 0;
        ppu.first_pixel_delayed = false;
        ppu.drawing = true;
        ppu.bgp_write_pending = false;
    }

    std::uint16_t tile_row_address(const CPU_LR35902 &cpu, std::uint8_t tile_id, int row)
    {
        if ((cpu.memory[LCDC] & 0x10) != 0)
        {
            return static_cast<std::uint16_t>(VRAM_START + tile_id * 16 + row * 2);
        }
        return static_cast<std::uint16_t>(0x9000 + static_cast<std::int8_t>(tile_id) * 16 + row * 2);
    }

    int fetch_row(const CPU_LR35902 &cpu, const PPU_LR35902 &ppu)
    {
        return ppu.fetch_window ? ppu.window_line & 0x07 : (cpu.memory[LY] + cpu.memory[SCY]) & 0x07;
    }

    // One dot of the background fetcher. Registers are read at the stage that
    // uses them, so mid-line writes affect the tiles fetched afterwards.
    void advance_fetcher(const CPU_LR35902 &cpu, PPU_LR35902 &ppu)
    {
        const std::uint8_t lcdc = cpu.memory[LCDC];
        switch (ppu.fetch_step)
        {
        case 1:
            if (ppu.fetch_window)
            {
                const std::uint16_t map = (lcdc & 0x40) != 0 ? BG_MAP_1 : BG_MAP_0;
                ppu.fetch_tile_id = cpu.memory[map + (ppu.window_line / 8) * 32 + (ppu.fetch_tile_x & 0x1F)];
            }
            else
            {
                const std::uint16_t map = (lcdc & 0x08) != 0 ? BG_MAP_1 : BG_MAP_0;
                const int column = ((cpu.memory[SCX] >> 3) + ppu.fetch_tile_x) & 0x1F;
                const int row = ((cpu.memory[LY] + cpu.memory[SCY]) & 0xFF) / 8;
                ppu.fetch_tile_id = cpu.memory[map + row * 32 + column];
            }
            break;
        case 3:
            ppu.fetch_low = cpu.memory[tile_row_address(cpu, ppu.fetch_tile_id, fetch_row(cpu, ppu))];
            break;
        case 5:
            ppu.fetch_high = cpu.memory[tile_row_address(cpu, ppu.fetch_tile_id, fetch_row(cpu, ppu)) + 1];
            if (ppu.fetch_first_tile)
            {
                // The first fetch of a line is thrown away and repeated.
                ppu.fetch_first_tile = false;
                ppu.fetch_step = 0;
                return;
            }
            break;
        case 6:
            if (ppu.background_count == 0)
            {
                ppu.background_low = ppu.fetch_low;
                ppu.background_high = ppu.fetch_high;
                ppu.background_count = 8;
                ++ppu.fetch_tile_x;
                ppu.fetch_step = 0;
            }
            return;
        default:
            break;
        }
        ++ppu.fetch_step;
    }

    void load_object(const CPU_LR35902 &cpu, PPU_LR35902 &ppu, int index)
    {
        PPU_LR35902::LineObject &object = ppu.line_objects[static_cast<std::size_t>(index)];
        object.fetched = true;
        const std::uint16_t address = static_cast<std::uint16_t>(OAM_START + object.oam_index * 4);
        const std::uint8_t attributes = cpu.memory[address + 3];
        const int object_height = (cpu.memory[LCDC] & 0x04) != 0 ? 16 : 8;
        int row = cpu.memory[LY] - (object.y - 16);
        if ((attributes & 0x40) != 0)
        {
            row = object_height - 1 - row;
        }
        int tile = cpu.memory[address + 2];
        if (object_height == 16)
        {
            tile = (tile & 0xFE) + row / 8;
        }
        row &= 0x07;
        const std::uint16_t row_address = static_cast<std::uint16_t>(VRAM_START + tile * 16 + row * 2);
        const std::uint8_t low = cpu.memory[row_address];
        const std::uint8_t high = cpu.memory[row_address + 1];

        for (int column = 0; column < 8; ++column)
        {
            const int slot = object.x - 8 + column - ppu.pixel_x;
            if (slot < 0 || slot >= 8 || ppu.object_colors[static_cast<std::size_t>(slot)] != 0)
            {
                continue; // Earlier objects keep their opaque pixels.
            }
            const int bit = (attributes & 0x20) != 0 ? column : 7 - column;
            const std::uint8_t color = static_cast<std::uint8_t>(((low >> bit) & 1) | (((high >> bit) & 1) << 1));
            ppu.object_colors[static_cast<std::size_t>(slot)] = color;
            ppu.object_attributes[static_cast<std::size_t>(slot)] = attributes;
        }
    }

    // Pops the next pixel from the shifters. It is coloured two dots later.
    PPU_LR35902::PendingPixel output_pixel(PPU_LR35902 &ppu)
    {
        const std::uint8_t background_color = static_cast<std::uint8_t>(
            ((ppu.background_high >> 7) << 1) | (ppu.background_low >> 7));
        ppu.background_low = static_cast<std::uint8_t>(ppu.background_low << 1);
        ppu.background_high = static_cast<std::uint8_t>(ppu.background_high << 1);
        --ppu.background_count;
        if (ppu.discard_pixels > 0)
        {
            --ppu.discard_pixels;
            return {};
        }

        const std::uint8_t delay = ppu.first_pixel_delayed ? 3 : 2;
        ppu.first_pixel_delayed = false;
        const PPU_LR35902::PendingPixel pixel{true, delay, static_cast<std::uint8_t>(ppu.pixel_x), background_color,
                                              ppu.object_colors[0], ppu.object_attributes[0]};
        std::copy(ppu.object_colors.begin() + 1, ppu.object_colors.end(), ppu.object_colors.begin());
        std::copy(ppu.object_attributes.begin() + 1, ppu.object_attributes.end(), ppu.object_attributes.begin());
        ppu.object_colors[7] = 0;
        ++ppu.pixel_x;
        return pixel;
    }

    // Palettes and the enable bits are read as each pixel is coloured.
    void resolve_pixel(const CPU_LR35902 &cpu, PPU_LR35902 &ppu, const PPU_LR35902::PendingPixel &pixel)
    {
        const std::uint8_t lcdc = ppu.resolve_lcdc;
        const std::uint8_t visible_background = (lcdc & 0x01) != 0 ? pixel.background_color : 0;
        const std::uint8_t bgp = ppu.bgp_write_pending ? ppu.bgp_write_blend : cpu.memory[BGP];
        std::uint32_t color = shade_color(static_cast<std::uint8_t>((bgp >> (visible_background * 2)) & 0x03));
        if (pixel.object_color != 0 && (lcdc & 0x02) != 0 &&
            ((pixel.object_attributes & 0x80) == 0 || visible_background == 0))
        {
            color = palette_color(cpu, (pixel.object_attributes & 0x10) != 0 ? OBP1 : OBP0, pixel.object_color);
        }
        ppu.framebuffer[static_cast<std::size_t>(cpu.memory[LY]) * PPU_LR35902::WIDTH + pixel.x] = color;
    }

    void advance_pixel_pipeline(const CPU_LR35902 &cpu, PPU_LR35902 &ppu)
    {
        if (!ppu.drawing)
        {
            return;
        }
        int still_pending = 0;
        for (int index = 0; index < ppu.pending_count; ++index)
        {
            PPU_LR35902::PendingPixel &pixel = ppu.pending_pixels[static_cast<std::size_t>(index)];
            if (--pixel.delay == 0)
            {
                resolve_pixel(cpu, ppu, pixel);
            }
            else
            {
                ppu.pending_pixels[static_cast<std::size_t>(still_pending++)] = pixel;
            }
        }
        ppu.pending_count = still_pending;
        if (ppu.pixel_x >= PPU_LR35902::WIDTH)
        {
            if (ppu.pending_count == 0)
            {
                ppu.drawing = false;
                if (ppu.window_drawn)
                {
                    ++ppu.window_line; // Only lines that draw the window advance its line counter.
                }
            }
            return;
        }

        const std::uint8_t lcdc = cpu.memory[LCDC];

        // Objects left of the screen (X < 8) are hit while the pixels left of
        // the screen are shifted out, before the first tile is pushed. The
        // fetcher holds that tile until they have been fetched.
        if (ppu.stall_dots == 0 && ppu.pixel_x == 0 && ppu.background_count == 0 && ppu.fetch_step == 6 &&
            (lcdc & 0x02) != 0)
        {
            const int index = next_object(ppu.line_objects, ppu.line_object_count);
            if (index >= 0 && ppu.line_objects[static_cast<std::size_t>(index)].x < 8)
            {
                ppu.stalled_object = index;
                ppu.stall_dots = object_penalty(cpu, ppu.penalised_tiles,
                                                ppu.line_objects[static_cast<std::size_t>(index)].x);
            }
        }
        if (ppu.stall_dots > 0 && ppu.background_count == 0)
        {
            if (--ppu.stall_dots == 0)
            {
                load_object(cpu, ppu, ppu.stalled_object);
                ppu.stalled_object = -1;
                ppu.first_pixel_delayed = true;
            }
            return;
        }

        advance_fetcher(cpu, ppu);

        if (!ppu.window_active && ppu.stall_dots == 0 && ppu.background_count > 0 && ppu.discard_pixels == 0 &&
            (lcdc & 0x20) != 0 && ppu.window_y_triggered && cpu.memory[WX] <= 166 &&
            ppu.pixel_x + 7 >= cpu.memory[WX])
        {
            // Starting the window clears the background shifter and restarts
            // the fetcher on the window's first tile, beginning this dot.
            ppu.window_active = true;
            ppu.window_drawn = true;
            ppu.fetch_window = true;
            ppu.fetch_tile_x = 0;
            ppu.fetch_step = 1;
            ppu.background_count = 0;
            return;
        }

        if (ppu.stall_dots == 0 && ppu.discard_pixels == 0 && ppu.background_count > 0 && (lcdc & 0x02) != 0)
        {
            const int index = next_object(ppu.line_objects, ppu.line_object_count);
            if (index >= 0 && ppu.line_objects[static_cast<std::size_t>(index)].x <= ppu.pixel_x + 8)
            {
                ppu.stalled_object = index;
                ppu.stall_dots = object_penalty(cpu, ppu.penalised_tiles,
                                                ppu.line_objects[static_cast<std::size_t>(index)].x);
            }
        }

        if (ppu.stall_dots > 0)
        {
            if (--ppu.stall_dots == 0)
            {
                load_object(cpu, ppu, ppu.stalled_object);
                ppu.stalled_object = -1;
            }
            return;
        }

        if (ppu.background_count > 0)
        {
            const PPU_LR35902::PendingPixel pixel = output_pixel(ppu);
            if (pixel.valid)
            {
                ppu.pending_pixels[static_cast<std::size_t>(ppu.pending_count++)] = pixel;
            }
        }
    }

    // Draws any pixels the pipeline has not reached by the end of the line,
    // which can happen when registers change mid-line.
    void finish_pixel_pipeline(const CPU_LR35902 &cpu, PPU_LR35902 &ppu)
    {
        while (ppu.drawing)
        {
            ppu.stall_dots = std::min(ppu.stall_dots, 1);
            advance_pixel_pipeline(cpu, ppu);
        }
    }

    void advance_scanline(CPU_LR35902 &cpu, PPU_LR35902 &ppu)
    {
        finish_pixel_pipeline(cpu, ppu);
        std::uint8_t ly = cpu.memory[LY];
        ly = ly == 153 ? 0 : static_cast<std::uint8_t>(ly + 1);
        cpu.memory[LY] = ly;
        ppu.cycles_into_line = 0;

        if (ly == 144)
        {
            cpu.memory[0xFF0F] |= 0x01;
            if ((cpu.memory[STAT] & 0x20) != 0)
            {
                cpu.memory[0xFF0F] |= 0x02;
            }
            ppu.line_start_hold = false;
            ppu.ly_for_comparison = ly;
            update_status(cpu, ppu, 1);
        }
        else if (ly < 144)
        {
            // The first four dots of a visible line keep the previous mode 0,
            // while the LY=LYC comparison is disabled. Mode 2 starts at dot 4.
            ppu.line_start_hold = true;
            ppu.ly_for_comparison = ly == 0 ? 0 : -1;
            if (ly == 0)
            {
                ppu.window_y_triggered = false;
                ppu.window_line = 0;
            }
            update_status(cpu, ppu, 0);
        }
        else
        {
            ppu.line_start_hold = false;
            ppu.ly_for_comparison = ly;
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
        ppu.drawing = false;
        ppu.startup_mode0 = false;
        ppu.line_start_hold = false;
        ppu.ly_for_comparison = 0;
        ppu.cycles_into_line = 0;
        ppu.mode = 0;
        cpu.memory[LY] = 0;
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
        ppu.ly_for_comparison = 0;
        ppu.window_y_triggered = false;
        ppu.window_line = 0;
        ppu.line_start_hold = false;
        if (ppu.has_started)
        {
            // Enabling the LCD starts a line that is still in mode 0 and skips
            // the OAM search phase. Mode 3 on that line starts two dots late.
            ppu.startup_mode0 = true;
            update_status(cpu, ppu, 0);
        }
        else
        {
            ppu.has_started = true;
            ppu.startup_mode0 = false;
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
            mode_boundary = ppu.cycles_into_line < 80 ? 80 : MODE3_START;
        }
        else if (ppu.mode == 3)
        {
            mode_boundary = ppu.cycles_into_line < ppu.mode3_end - 1 ? ppu.mode3_end - 1 : ppu.mode3_end;
        }
        else if (ppu.mode == 0)
        {
            if (ppu.startup_mode0)
            {
                mode_boundary = STARTUP_MODE3_START;
            }
            else if (ppu.line_start_hold)
            {
                mode_boundary = 4;
            }
            else
            {
                mode_boundary = 456;
            }
        }
        else
        {
            // The mode 1 interrupt is raised four dots into line 144.
            mode_boundary = cpu.memory[LY] == 144 && ppu.cycles_into_line < 4 ? 4 : 456;
        }

        const int step = std::min(cycles, mode_boundary - ppu.cycles_into_line);
        for (int dot = 0; dot < step; ++dot)
        {
            advance_pixel_pipeline(cpu, ppu);
            ppu.resolve_lcdc = cpu.memory[LCDC];
            ppu.bgp_write_pending = false; // The blended BGP lasts only for the dot after the write.
            ++ppu.cycles_into_line;
        }
        cycles -= step;

        if (ppu.cycles_into_line != mode_boundary)
        {
            continue;
        }

        if (ppu.mode == 2 && ppu.cycles_into_line < MODE3_START)
        {
            update_status(cpu, ppu, 2);
        }
        else if (ppu.mode == 2)
        {
            ppu.window_y_triggered = ppu.window_y_triggered || cpu.memory[LY] == cpu.memory[WY];
            scan_line_objects(cpu, ppu, cpu.memory[LY]);
            ppu.mode3_end = MODE3_START + calculate_mode3_duration(cpu, ppu);
            start_pixel_pipeline(cpu, ppu);
            update_status(cpu, ppu, 3);
        }
        else if (ppu.mode == 3 && ppu.cycles_into_line < ppu.mode3_end)
        {
            update_status(cpu, ppu, 3);
        }
        else if (ppu.mode == 3)
        {
            update_status(cpu, ppu, 0);
        }
        else if (ppu.mode == 0)
        {
            if (ppu.startup_mode0)
            {
                ppu.startup_mode0 = false;
                ppu.window_y_triggered = cpu.memory[LY] == cpu.memory[WY];
                scan_line_objects(cpu, ppu, cpu.memory[LY]);
                ppu.mode3_end = STARTUP_MODE3_START + calculate_mode3_duration(cpu, ppu);
                start_pixel_pipeline(cpu, ppu);
                update_status(cpu, ppu, 3);
            }
            else if (ppu.line_start_hold)
            {
                ppu.line_start_hold = false;
                ppu.ly_for_comparison = cpu.memory[LY];
                update_status(cpu, ppu, 2);
            }
            else
            {
                advance_scanline(cpu, ppu);
            }
        }
        else if (ppu.cycles_into_line < 456)
        {
            update_status(cpu, ppu, 1);
        }
        else
        {
            advance_scanline(cpu, ppu);
        }
    }
}

// CPU writes land at the start of their M-cycle, four dots before the PPU state
// that blocks them. VRAM writes are blocked while mode 3 is drawing. OAM writes
// are blocked during mode 2 and mode 3, except for the four dots before mode 3
// starts and the first four dots of each line.
bool ppu_blocks_vram_write(const PPU_LR35902 &ppu)
{
    constexpr int WRITE_LEAD = 4;
    const int dot = ppu.cycles_into_line + WRITE_LEAD;
    if (!ppu.lcd_enabled)
    {
        return false;
    }
    switch (ppu.mode)
    {
    case 3:
        return dot < ppu.mode3_end;
    case 2:
        return dot >= MODE3_START;
    case 0:
        return ppu.startup_mode0 && dot >= STARTUP_MODE3_START;
    default:
        return false;
    }
}

bool ppu_blocks_oam_write(const PPU_LR35902 &ppu)
{
    constexpr int WRITE_LEAD = 4;
    const int dot = ppu.cycles_into_line + WRITE_LEAD;
    if (!ppu.lcd_enabled)
    {
        return false;
    }
    switch (ppu.mode)
    {
    case 3:
        return dot < ppu.mode3_end;
    case 2:
        return dot < 80 || dot >= MODE3_START;
    case 0:
        return ppu.line_start_hold || (ppu.startup_mode0 && dot >= STARTUP_MODE3_START);
    default:
        return false;
    }
}
