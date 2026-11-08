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
    // How far ahead the end of mode 3 must be known: writes are checked four
    // dots ahead and the STAT interrupt one dot beyond that.
    constexpr int MODE3_LOOKAHEAD = 6;
    // Pixels leave the pipeline at most one a dot, plus at most one window
    // reactivation pixel, so before this pixel mode 3 cannot end within the
    // lookahead.
    constexpr int MODE3_LOOKAHEAD_FROM_PIXEL = 160 - MODE3_LOOKAHEAD - 1;

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

    void start_pixel_pipeline(const CPU_LR35902 &cpu, PPU_State &ppu)
    {
        // The first fetch of a line is a dot shorter, and the fetcher waits a
        // dot before pushing the first tile.
        ppu.fetch_step = 1;
        ppu.fetch_tile_x = 0;
        ppu.fetch_window = false;
        ppu.fetch_first_tile = true;
        ppu.first_push_wait = false;
        ppu.background_count = 0;
        ppu.object_colors.fill(0);
        ppu.pixel_x = 0;
        ppu.pipeline_dot = 0;
        ppu.discard_pixels = cpu.memory[SCX] & 0x07; // Only read as the line starts.
        ppu.stall_dots = 0;
        ppu.stalled_object = -1;
        ppu.window_active = false;
        ppu.window_drawn = false;
        ppu.window_index_late = false;
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

    int fetch_row(const CPU_LR35902 &cpu, const PPU_State &ppu)
    {
        return ppu.fetch_window ? ppu.window_line & 0x07 : (cpu.memory[LY] + cpu.memory[SCY]) & 0x07;
    }

    // One dot of the background fetcher. Registers are read at the stage that
    // uses them, so mid-line writes affect the tiles fetched afterwards.
    void fetch_tile_index(const CPU_LR35902 &cpu, PPU_State &ppu, std::uint8_t lcdc)
    {
        if (ppu.fetch_window && (lcdc & 0x20) == 0)
        {
            // Disabling the window takes effect at the next tile index fetch,
            // which then fetches the background again.
            ppu.fetch_window = false;
            ppu.window_active = false;
        }
        if (ppu.fetch_window)
        {
            const std::uint16_t map = (lcdc & 0x40) != 0 ? BG_MAP_1 : BG_MAP_0;
            ppu.fetch_tile_id = cpu.memory[map + ((ppu.window_line / 8) & 0x1F) * 32 + (ppu.fetch_tile_x & 0x1F)];
        }
        else
        {
            const std::uint16_t map = (lcdc & 0x08) != 0 ? BG_MAP_1 : BG_MAP_0;
            const int column = ((cpu.memory[SCX] >> 3) + ppu.fetch_tile_x) & 0x1F;
            const int row = ((cpu.memory[LY] + cpu.memory[SCY]) & 0xFF) / 8;
            ppu.fetch_tile_id = cpu.memory[map + row * 32 + column];
        }
    }

    void advance_fetcher(const CPU_LR35902 &cpu, PPU_State &ppu)
    {
        const std::uint8_t lcdc = cpu.memory[LCDC];
        switch (ppu.fetch_step)
        {
        case 1:
            if (!ppu.window_index_late)
            {
                fetch_tile_index(cpu, ppu, lcdc);
            }
            break;
        case 2:
            if (ppu.window_index_late)
            {
                // The window's first tile index and low bitplane are read a
                // dot later.
                fetch_tile_index(cpu, ppu, lcdc);
            }
            break;
        case 3:
            if (!ppu.window_index_late)
            {
                ppu.fetch_low = cpu.memory[tile_row_address(cpu, ppu.fetch_tile_id, fetch_row(cpu, ppu))];
            }
            break;
        case 4:
            if (ppu.window_index_late)
            {
                ppu.window_index_late = false;
                ppu.fetch_low = cpu.memory[tile_row_address(cpu, ppu.fetch_tile_id, fetch_row(cpu, ppu))];
            }
            break;
        case 5:
            ppu.fetch_high = cpu.memory[tile_row_address(cpu, ppu.fetch_tile_id, fetch_row(cpu, ppu)) + 1];
            if (ppu.fetch_first_tile)
            {
                // The first fetch of a line is thrown away and repeated.
                ppu.fetch_first_tile = false;
                ppu.first_push_wait = true;
                ppu.fetch_step = 0;
                return;
            }
            break;
        case 6:
            if (ppu.first_push_wait)
            {
                ppu.first_push_wait = false;
                return;
            }
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

    void load_object(const CPU_LR35902 &cpu, PPU_State &ppu, int index)
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
    PPU_LR35902::PendingPixel output_pixel(PPU_State &ppu)
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
    void resolve_pixel(const CPU_LR35902 &cpu, PPU_State &ppu, const PPU_LR35902::PendingPixel &pixel,
                       std::uint32_t *framebuffer)
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
        if (framebuffer != nullptr)
        {
            framebuffer[static_cast<std::size_t>(cpu.memory[LY]) * PPU_LR35902::WIDTH + pixel.x] = color;
        }
    }

    // One dot of the pixel pipeline. Without a framebuffer it runs without
    // drawing, to look ahead.
    void advance_pixel_pipeline(const CPU_LR35902 &cpu, PPU_State &ppu, std::uint32_t *framebuffer)
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
                resolve_pixel(cpu, ppu, pixel, framebuffer);
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
        const int pipeline_dot = ppu.pipeline_dot++;
        const int fetch_step = ppu.fetch_step;

        // With WX below 7 the window starts left of the screen, before the
        // first background tile is pushed, so that its first 7 - WX pixels are
        // discarded by the time screen pixel 0 is reached.
        if (!ppu.window_active && ppu.pixel_x == 0 && (lcdc & 0x20) != 0 && ppu.window_y_triggered &&
            cpu.memory[WX] < 7 && pipeline_dot == 5 + cpu.memory[WX])
        {
            ppu.window_active = true;
            ppu.window_drawn = true;
            ppu.window_start_x = ppu.pixel_x;
            ppu.fetch_window = true;
            ppu.fetch_first_tile = false;
            ppu.first_push_wait = false;
            ppu.fetch_tile_x = 0;
            ppu.fetch_step = 1;
            ppu.background_count = 0;
            ppu.discard_pixels = 7 - cpu.memory[WX];
            return;
        }

        // Objects left of the screen (X < 8) are hit while the pixels left of
        // the screen are shifted out, before the first tile is pushed. The
        // fetcher holds that tile until they have been fetched.
        if (ppu.stall_dots == 0 && ppu.pixel_x == 0 && ppu.background_count == 0 && ppu.fetch_step == 6 &&
            !ppu.first_push_wait &&
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
            ppu.pixel_x + 7 == cpu.memory[WX])
        {
            // Each time the window starts again on a line, it moves on to the
            // next window row.
            if (ppu.window_drawn)
            {
                ++ppu.window_line;
            }
            // Starting the window clears the background shifter and restarts
            // the fetcher on the window's first tile, beginning this dot.
            ppu.window_active = true;
            ppu.window_drawn = true;
            ppu.window_start_x = ppu.pixel_x;
            ppu.fetch_window = true;
            ppu.window_index_late = true;
            ppu.fetch_tile_x = 0;
            ppu.fetch_step = 1;
            ppu.background_count = 0;
            return;
        }

        // If WX matches the last pixel to leave the pipeline while the window
        // is drawing, as a window tile fetch starts, the DMG inserts an extra
        // colour 0 pixel there, pushing the background one pixel right.
        if (ppu.window_active && ppu.fetch_window && fetch_step == 0 && ppu.stall_dots == 0 &&
            ppu.pending_count > 0 && (lcdc & 0x20) != 0 && ppu.pixel_x + 6 == cpu.memory[WX] &&
            ppu.pixel_x - 1 != ppu.window_start_x)
        {
            PPU_LR35902::PendingPixel &last = ppu.pending_pixels[static_cast<std::size_t>(ppu.pending_count - 1)];
            PPU_LR35902::PendingPixel inserted = last;
            inserted.background_color = 0;
            // Object pixels stay where they are on screen.
            ++last.x;
            last.object_color = ppu.object_colors[0];
            last.object_attributes = ppu.object_attributes[0];
            std::copy(ppu.object_colors.begin() + 1, ppu.object_colors.end(), ppu.object_colors.begin());
            std::copy(ppu.object_attributes.begin() + 1, ppu.object_attributes.end(), ppu.object_attributes.begin());
            ppu.object_colors[7] = 0;
            ppu.pending_pixels[static_cast<std::size_t>(ppu.pending_count)] = last;
            last = inserted;
            ++ppu.pending_count;
            ++ppu.pixel_x;
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
            advance_pixel_pipeline(cpu, ppu, ppu.framebuffer.data());
        }
    }

    // Mode 3 ends a dot after the pipeline outputs its last pixel. The CPU sees
    // mode 0 coming early, through the STAT interrupt and write blocking, so
    // near the end of the line a copy of the pipeline is run ahead to find the
    // dot, assuming no more register writes.
    void update_mode3_end(const CPU_LR35902 &cpu, PPU_LR35902 &ppu)
    {
        if (ppu.pixel_x >= PPU_LR35902::WIDTH)
        {
            ppu.mode3_end = std::min(ppu.mode3_end, ppu.cycles_into_line);
            return;
        }
        ppu.mode3_end = 456;
        if (ppu.pixel_x < MODE3_LOOKAHEAD_FROM_PIXEL)
        {
            return;
        }
        PPU_State ahead = ppu;
        for (int dot = 0; dot < MODE3_LOOKAHEAD; ++dot)
        {
            advance_pixel_pipeline(cpu, ahead, nullptr);
            if (ahead.pixel_x >= PPU_LR35902::WIDTH)
            {
                ppu.mode3_end = ppu.cycles_into_line + dot + 1;
                return;
            }
        }
    }

    void start_mode3(CPU_LR35902 &cpu, PPU_LR35902 &ppu)
    {
        scan_line_objects(cpu, ppu, cpu.memory[LY]);
        start_pixel_pipeline(cpu, ppu);
        ppu.mode3_end = 456;
        update_status(cpu, ppu, 3);
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
            // Near the end, the end of mode 3 is found a dot at a time.
            mode_boundary = ppu.cycles_into_line + std::max(1, MODE3_LOOKAHEAD_FROM_PIXEL - 1 - ppu.pixel_x);
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
            advance_pixel_pipeline(cpu, ppu, ppu.framebuffer.data());
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
            start_mode3(cpu, ppu);
        }
        else if (ppu.mode == 3)
        {
            update_mode3_end(cpu, ppu);
            update_status(cpu, ppu, ppu.cycles_into_line < ppu.mode3_end ? 3 : 0);
        }
        else if (ppu.mode == 0)
        {
            if (ppu.startup_mode0)
            {
                ppu.startup_mode0 = false;
                ppu.window_y_triggered = cpu.memory[LY] == cpu.memory[WY];
                start_mode3(cpu, ppu);
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

// The OAM scan reads one row per M-cycle from the start of each visible line.
// The CPU's accesses are checked at the start of its M-cycle, by which time the
// scan has moved on to the next row.
int ppu_oam_scan_row(const CPU_LR35902 &cpu, const PPU_LR35902 &ppu)
{
    if (!ppu.lcd_enabled || ppu.startup_mode0 || cpu.memory[LY] >= 144 ||
        !(ppu.line_start_hold || (ppu.mode == 2 && ppu.cycles_into_line < 80)))
    {
        return -1;
    }
    const int row = ppu.cycles_into_line / 4 + 1;
    return row < 20 ? row : -1;
}
