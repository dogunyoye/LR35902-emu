#pragma once

#include "LR35902.h"

#include <array>
#include <cstddef>
#include <cstdint>

// Everything the PPU tracks apart from the picture, so it is cheap to copy.
struct PPU_State
{
    static constexpr int WIDTH = 160;
    static constexpr int HEIGHT = 144;
    static constexpr std::size_t PIXEL_COUNT = static_cast<std::size_t>(WIDTH) * HEIGHT;

    int cycles_into_line = 0;
    // Dot within the line at which mode 3 ends: one dot after the pixel
    // pipeline outputs its last pixel.
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
    // The first tile fetched on a line is shifted out left of the screen and
    // then fetched again.
    bool fetch_first_tile = false;
    std::uint8_t fetch_tile_id = 0;
    // CGB mode: the tile's attributes from VRAM bank 1 (palette, bank, flips,
    // and priority over objects), kept with its pixels in the shifter.
    std::uint8_t fetch_attributes = 0;
    std::uint8_t fetch_low = 0;
    std::uint8_t fetch_high = 0;
    std::uint8_t background_low = 0;
    std::uint8_t background_high = 0;
    std::uint8_t background_attributes = 0;
    int background_count = 0;
    // Object pixels waiting to be mixed, slot 0 being the next pixel, with
    // the OAM index of the object each came from.
    std::array<std::uint8_t, 8> object_colors{};
    std::array<std::uint8_t, 8> object_attributes{};
    std::array<std::uint8_t, 8> object_oam_indices{};
    int pixel_x = 0;
    // Pixels still to be shifted out left of the screen. Objects and the
    // window are matched against a position that counts these 8 pixels.
    int invisible_pixels = 0;
    // Pixels discarded at the start of the line for SCX's low bits.
    int discard_pixels = 0;
    // Pixels leave the pipeline two dots before their colour is resolved, so
    // the last pixels are coloured just after STAT reports mode 0. After an
    // object left of the screen is fetched, the first pixel on screen waits a
    // dot longer and is coloured with the pixel behind it.
    struct PendingPixel
    {
        bool valid;
        std::uint8_t delay;
        std::uint8_t x;
        std::uint8_t background_color;
        std::uint8_t object_color;
        std::uint8_t object_attributes;
        std::uint8_t background_attributes;
    };
    std::array<PendingPixel, 4> pending_pixels{};
    int pending_count = 0;
    bool first_pixel_delayed = false;
    bool drawing = false;
    // On the DMG, the pixel output as BGP is written uses the old and new
    // values ORed together.
    bool bgp_write_pending = false;
    std::uint8_t bgp_write_blend = 0;
    // LCDC as of the previous dot. Pixel colouring sees LCDC writes a dot late.
    std::uint8_t resolve_lcdc = 0;
    int stall_dots = 0;
    int stalled_object = -1;
    bool window_active = false;
    bool window_drawn = false;
    bool window_index_late = false;
    // Pixel at which the window last started on this line.
    int window_start_x = 0;
    std::array<bool, 32> penalised_tiles{};
    std::uint8_t mode = 0;
    bool lcd_enabled = false;
    bool has_started = false;
    bool startup_mode0 = false;
    bool line_start_hold = false;
    bool framebuffer_initialized = false;
    bool stat_interrupt_line = false;
};

struct PPU_LR35902 : PPU_State
{
    std::array<std::uint32_t, PIXEL_COUNT> framebuffer{};
};

void update_ppu(CPU_LR35902 &cpu, PPU_LR35902 &ppu, int cycles);
// Whether a CPU write landing now is blocked by the PPU. Writes land at the
// start of their M-cycle, this many dots (4, or 2 in double speed) before the
// PPU state that blocks them.
bool ppu_blocks_vram_write(const PPU_LR35902 &ppu, int write_lead = 4);
bool ppu_blocks_oam_write(const PPU_LR35902 &ppu, int write_lead = 4);
// The 8-byte OAM row the OAM scan is reading, or -1 outside the scan.
int ppu_oam_scan_row(const CPU_LR35902 &cpu, const PPU_LR35902 &ppu);
