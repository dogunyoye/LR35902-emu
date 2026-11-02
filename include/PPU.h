#pragma once

#include "LR35902.h"

#include <array>
#include <cstddef>
#include <cstdint>

struct PPU_LR35902
{
    static constexpr int WIDTH = 160;
    static constexpr int HEIGHT = 144;
    static constexpr std::size_t PIXEL_COUNT = static_cast<std::size_t>(WIDTH) * HEIGHT;

    std::array<std::uint32_t, PIXEL_COUNT> framebuffer{};
    int cycles_into_line = 0;
    // Dot within the line at which mode 3 ends.
    int mode3_end = 253;
    // Line used for the LY=LYC comparison. -1 disables the comparison, which
    // happens for a few dots after LY changes.
    int ly_for_comparison = 0;
    // Window line counter, and whether LY has matched WY this frame.
    int window_line = 0;
    bool window_y_triggered = false;

    // Mode 3 pixel pipeline. Objects found by the OAM scan for this line, in
    // OAM order, with whether each has been fetched yet.
    struct LineObject
    {
        std::uint8_t oam_index;
        std::uint8_t x;
        std::uint8_t y;
        bool fetched;
    };
    std::array<LineObject, 10> line_objects{};
    int line_object_count = 0;
    // Background fetcher: step 0-5 fetches the tile index and bitplanes over
    // two dots each, step 6 waits to push into the empty background shifter.
    int fetch_step = 0;
    int fetch_tile_x = 0;
    bool fetch_window = false;
    bool fetch_first_tile = false;
    std::uint8_t fetch_tile_id = 0;
    std::uint8_t fetch_low = 0;
    std::uint8_t fetch_high = 0;
    std::uint8_t background_low = 0;
    std::uint8_t background_high = 0;
    int background_count = 0;
    // Object pixels waiting to be mixed, slot 0 being the next pixel.
    std::array<std::uint8_t, 8> object_colors{};
    std::array<std::uint8_t, 8> object_attributes{};
    int pixel_x = 0;
    int discard_pixels = 0;
    // Pixels leave the pipeline two dots before their colour is resolved, so
    // the last pixels are coloured just after STAT reports mode 0.
    struct PendingPixel
    {
        bool valid;
        std::uint8_t x;
        std::uint8_t background_color;
        std::uint8_t object_color;
        std::uint8_t object_attributes;
    };
    std::array<PendingPixel, 2> pending_pixels{};
    bool drawing = false;
    // On the DMG, the pixel output as BGP is written uses the old and new
    // values ORed together.
    bool bgp_write_pending = false;
    std::uint8_t bgp_write_blend = 0;
    int stall_dots = 0;
    int stalled_object = -1;
    bool window_active = false;
    bool window_drawn = false;
    std::array<bool, 32> penalised_tiles{};
    std::uint8_t mode = 0;
    bool lcd_enabled = false;
    bool has_started = false;
    bool startup_mode0 = false;
    bool line_start_hold = false;
    bool framebuffer_initialized = false;
    bool stat_interrupt_line = false;
};

void update_ppu(CPU_LR35902 &cpu, PPU_LR35902 &ppu, int cycles);
// Whether a CPU write landing now is blocked by the PPU.
bool ppu_blocks_vram_write(const PPU_LR35902 &ppu);
bool ppu_blocks_oam_write(const PPU_LR35902 &ppu);
