#include "LR35902.h"
#include "PPU.h"

#include <cassert>
#include <vector>
#include <string>
#include <algorithm>

static void test_ppu_modes_scanlines_and_background()
{
    CPU_LR35902 cpu{};
    PPU_LR35902 ppu{};
    cpu.memory[0xFF40] = 0x91; // LCD on, unsigned tile data, background on
    cpu.memory[0xFF47] = 0xE4;
    cpu.memory[0x9800] = 0;
    cpu.memory[0x8000] = 0x80; // Pixel 0 of tile row 0 uses color index 1

    update_ppu(cpu, ppu, 81);
    assert((cpu.memory[0xFF41] & 0x03) == 3);
    assert(cpu.vram_locked);
    assert(cpu.oam_locked);
    assert(cpu_read(cpu, 0x8000) == 0xFF);

    update_ppu(cpu, ppu, 172);
    assert((cpu.memory[0xFF41] & 0x03) == 0);
    assert(!cpu.vram_locked);
    assert(!cpu.oam_locked);
    assert(ppu.framebuffer[0] == 0xFF88C070);
    assert(ppu.framebuffer[1] == 0xFFE0F8D0);

    update_ppu(cpu, ppu, 199);
    assert(!ppu_blocks_oam_write(ppu)); // Writes land four dots ahead, still in the next line's first dots

    update_ppu(cpu, ppu, 4);
    assert(cpu.memory[0xFF44] == 1);
    assert((cpu.memory[0xFF41] & 0x03) == 0); // Mode 2 is reported four dots after LY changes
    assert(cpu.oam_locked);
    assert(ppu_blocks_oam_write(ppu));

    update_ppu(cpu, ppu, 4);
    assert((cpu.memory[0xFF41] & 0x03) == 2);
    assert(cpu.oam_locked);

    update_ppu(cpu, ppu, 143 * 456);
    assert(cpu.memory[0xFF44] == 144);
    assert((cpu.memory[0xFF41] & 0x03) == 1);
    assert((cpu.memory[0xFF0F] & 0x01) != 0);
    assert(!cpu.vram_locked);
    assert(!cpu.oam_locked);

    update_ppu(cpu, ppu, 10 * 456);
    assert(cpu.memory[0xFF44] == 0);
    assert((cpu.memory[0xFF41] & 0x03) == 2);

    cpu.memory[0xFF40] = 0;
    update_ppu(cpu, ppu, 4);
    assert(cpu.memory[0xFF44] == 0);
    assert((cpu.memory[0xFF41] & 0x03) == 0);
    assert(!cpu.vram_locked);
    assert(!cpu.oam_locked);
}

static void test_lcd_off_resets_ly()
{
    CPU_LR35902 cpu{};
    PPU_LR35902 ppu{};
    cpu.memory[0xFF40] = 0x91;
    update_ppu(cpu, ppu, 5 * 456 + 100);
    assert(cpu.memory[0xFF44] == 5);

    cpu.memory[0xFF40] = 0x11;
    update_ppu(cpu, ppu, 4);
    assert(cpu.memory[0xFF44] == 0);
}

static void test_ly_reads_zero_on_line_153()
{
    CPU_LR35902 cpu{};
    PPU_LR35902 ppu{};
    cpu.memory[0xFF40] = 0x91;
    update_ppu(cpu, ppu, 153 * 456 + 2);
    assert(cpu.memory[0xFF44] == 153);
    update_ppu(cpu, ppu, 4);
    assert(cpu.memory[0xFF44] == 0); // For the rest of line 153
    assert((cpu.memory[0xFF41] & 0x03) == 1);
    update_ppu(cpu, ppu, 456 - 6);
    assert(cpu.memory[0xFF44] == 0); // Line 0
    assert((cpu.memory[0xFF41] & 0x03) != 1);
    update_ppu(cpu, ppu, 456);
    assert(cpu.memory[0xFF44] == 1);
}

static void test_ppu_window_composition()
{
    CPU_LR35902 cpu{};
    PPU_LR35902 ppu{};
    cpu.memory[0xFF40] = 0xF1; // LCD, window, unsigned tiles, and background enabled
    cpu.memory[0xFF47] = 0xE4;
    cpu.memory[0xFF4A] = 0;
    cpu.memory[0xFF4B] = 87; // Window begins at screen x=80
    cpu.memory[0x9800] = 0;
    cpu.memory[0x9C00] = 1;
    cpu.memory[0x8000] = 0xFF; // Background tile color index 1
    cpu.memory[0x8011] = 0xFF; // Window tile color index 2

    update_ppu(cpu, ppu, 259);
    assert(ppu.framebuffer[79] == 0xFF88C070);
    assert(ppu.framebuffer[80] == 0xFF346856);
}

static void test_ppu_sprite_priority_palettes_flips_and_8x16()
{
    CPU_LR35902 priority_cpu{};
    PPU_LR35902 priority_ppu{};
    priority_cpu.memory[0xFF40] = 0x93; // LCD, objects, unsigned tiles, and background
    priority_cpu.memory[0xFF47] = 0xE4;
    priority_cpu.memory[0xFF49] = 0x30; // OBJ palette 1 maps color 2 to shade 3
    priority_cpu.memory[0x8010] = 0xFF; // Tile 1 color index 1
    priority_cpu.memory[0x8021] = 0xFF; // Tile 2 color index 2
    priority_cpu.memory[0xFE00] = 16;   // OAM entry 0: farther right, lower index
    priority_cpu.memory[0xFE01] = 12;
    priority_cpu.memory[0xFE02] = 1;
    priority_cpu.memory[0xFE04] = 16; // OAM entry 1: smaller X, higher priority
    priority_cpu.memory[0xFE05] = 10;
    priority_cpu.memory[0xFE06] = 2;
    priority_cpu.memory[0xFE07] = 0x10;

    update_ppu(priority_cpu, priority_ppu, 268);
    assert(priority_ppu.framebuffer[4] == 0xFF081820);

    CPU_LR35902 flip_cpu{};
    PPU_LR35902 flip_ppu{};
    flip_cpu.memory[0xFF40] = 0x93;
    flip_cpu.memory[0xFF47] = 0xE4;
    flip_cpu.memory[0xFF49] = 0x0C; // OBJ palette 1 maps color 1 to shade 3
    flip_cpu.memory[0x8010] = 0x80;
    flip_cpu.memory[0xFE00] = 16;
    flip_cpu.memory[0xFE01] = 8;
    flip_cpu.memory[0xFE02] = 1;
    flip_cpu.memory[0xFE03] = 0x30; // OBJ palette 1 and horizontal flip

    update_ppu(flip_cpu, flip_ppu, 264);
    assert(flip_ppu.framebuffer[0] == 0xFFE0F8D0);
    assert(flip_ppu.framebuffer[7] == 0xFF081820);

    CPU_LR35902 tall_cpu{};
    PPU_LR35902 tall_ppu{};
    tall_cpu.memory[0xFF40] = 0x97; // LCD, objects, 8x16 sprites, unsigned tiles, background
    tall_cpu.memory[0xFF47] = 0xE4;
    tall_cpu.memory[0xFF48] = 0xE4;
    tall_cpu.memory[0x8000] = 0xFF; // Tile 0 row 0 color index 1
    tall_cpu.memory[0x801F] = 0xFF; // Tile 1 row 7 color index 2
    tall_cpu.memory[0xFE00] = 16;
    tall_cpu.memory[0xFE01] = 8;
    tall_cpu.memory[0xFE02] = 1;    // 8x16 mode ignores tile ID bit 0
    tall_cpu.memory[0xFE03] = 0x40; // Vertical flip

    update_ppu(tall_cpu, tall_ppu, 264);
    assert(tall_ppu.framebuffer[0] == 0xFF346856);

    CPU_LR35902 hidden_cpu{};
    PPU_LR35902 hidden_ppu{};
    hidden_cpu.memory[0xFF40] = 0x93;
    hidden_cpu.memory[0xFF47] = 0xE4;
    hidden_cpu.memory[0x8010] = 0xFF; // Background color index 1
    hidden_cpu.memory[0x8021] = 0xFF; // Sprite color index 2
    hidden_cpu.memory[0x9800] = 1;
    hidden_cpu.memory[0xFE00] = 16;
    hidden_cpu.memory[0xFE01] = 8;
    hidden_cpu.memory[0xFE02] = 2;
    hidden_cpu.memory[0xFE03] = 0x80; // Behind nonzero background pixels

    update_ppu(hidden_cpu, hidden_ppu, 264);
    assert(hidden_ppu.framebuffer[0] == 0xFF88C070);
}

static void test_stat_mode_interrupt_edges()
{
    CPU_LR35902 cpu{};
    PPU_LR35902 ppu{};
    cpu.memory[0xFF40] = 0x80;
    cpu.memory[0xFF41] = 0x20; // Mode 2 STAT interrupt enable

    update_ppu(cpu, ppu, 0);
    assert((cpu.memory[0xFF0F] & 0x02) != 0);

    cpu.memory[0xFF0F] &= static_cast<std::uint8_t>(~0x02);
    update_ppu(cpu, ppu, 4);
    assert((cpu.memory[0xFF0F] & 0x02) == 0);

    cpu.memory[0xFF41] = 0x28; // Keep mode 2 enabled and enable mode 0
    update_ppu(cpu, ppu, 4);
    assert((cpu.memory[0xFF0F] & 0x02) == 0);

    update_ppu(cpu, ppu, 73);
    assert((cpu.memory[0xFF41] & 0x03) == 3);
    assert((cpu.memory[0xFF0F] & 0x02) == 0);

    update_ppu(cpu, ppu, 172);
    assert((cpu.memory[0xFF41] & 0x03) == 0);
    assert((cpu.memory[0xFF0F] & 0x02) != 0);
}

static void test_stat_lyc_interrupt_edges()
{
    CPU_LR35902 cpu{};
    PPU_LR35902 ppu{};
    cpu.memory[0xFF40] = 0x80;
    cpu.memory[0xFF41] = 0x40; // LYC coincidence STAT interrupt enable
    cpu.memory[0xFF45] = 1;

    update_ppu(cpu, ppu, 456);
    assert(cpu.memory[0xFF44] == 1);
    assert((cpu.memory[0xFF41] & 0x04) == 0); // LY=LYC is suppressed while LY changes
    assert((cpu.memory[0xFF0F] & 0x02) == 0);

    update_ppu(cpu, ppu, 4);
    assert((cpu.memory[0xFF41] & 0x04) != 0);
    assert((cpu.memory[0xFF0F] & 0x02) != 0);

    cpu.memory[0xFF0F] &= static_cast<std::uint8_t>(~0x02);
    update_ppu(cpu, ppu, 4);
    assert((cpu.memory[0xFF0F] & 0x02) == 0);

    cpu.memory[0xFF45] = 2;
    update_ppu(cpu, ppu, 4);
    assert((cpu.memory[0xFF41] & 0x04) == 0);
    assert((cpu.memory[0xFF0F] & 0x02) == 0);

    cpu.memory[0xFF45] = 1;
    update_ppu(cpu, ppu, 4);
    assert((cpu.memory[0xFF41] & 0x04) != 0);
    assert((cpu.memory[0xFF0F] & 0x02) != 0);
}

static void test_variable_mode3_timing()
{
    CPU_LR35902 scroll_cpu{};
    PPU_LR35902 scroll_ppu{};
    scroll_cpu.memory[0xFF40] = 0x91;
    scroll_cpu.memory[0xFF43] = 3;
    update_ppu(scroll_cpu, scroll_ppu, 81);
    update_ppu(scroll_cpu, scroll_ppu, 174);
    assert((scroll_cpu.memory[0xFF41] & 0x03) == 3);
    update_ppu(scroll_cpu, scroll_ppu, 1);
    assert((scroll_cpu.memory[0xFF41] & 0x03) == 0);
    update_ppu(scroll_cpu, scroll_ppu, 200);
    assert(scroll_cpu.memory[0xFF44] == 1);

    CPU_LR35902 window_cpu{};
    PPU_LR35902 window_ppu{};
    window_cpu.memory[0xFF40] = 0xF1;
    window_cpu.memory[0xFF4A] = 0;
    window_cpu.memory[0xFF4B] = 7;
    update_ppu(window_cpu, window_ppu, 81);
    update_ppu(window_cpu, window_ppu, 172);
    assert((window_cpu.memory[0xFF41] & 0x03) == 3);
    update_ppu(window_cpu, window_ppu, 6);
    assert((window_cpu.memory[0xFF41] & 0x03) == 0);
    update_ppu(window_cpu, window_ppu, 197);
    assert(window_cpu.memory[0xFF44] == 1);

    // With LCDC bit 0 clear the window is blank but still fetched.
    CPU_LR35902 blank_window_cpu{};
    PPU_LR35902 blank_window_ppu{};
    blank_window_cpu.memory[0xFF40] = 0xF0;
    blank_window_cpu.memory[0xFF4A] = 0;
    blank_window_cpu.memory[0xFF4B] = 7;
    update_ppu(blank_window_cpu, blank_window_ppu, 81);
    update_ppu(blank_window_cpu, blank_window_ppu, 172);
    assert((blank_window_cpu.memory[0xFF41] & 0x03) == 3);
    update_ppu(blank_window_cpu, blank_window_ppu, 6);
    assert((blank_window_cpu.memory[0xFF41] & 0x03) == 0);

    CPU_LR35902 sprite_cpu{};
    PPU_LR35902 sprite_ppu{};
    sprite_cpu.memory[0xFF40] = 0x93;
    sprite_cpu.memory[0xFE00] = 16;
    sprite_cpu.memory[0xFE01] = 8;
    update_ppu(sprite_cpu, sprite_ppu, 81);
    update_ppu(sprite_cpu, sprite_ppu, 172);
    assert((sprite_cpu.memory[0xFF41] & 0x03) == 3);
    update_ppu(sprite_cpu, sprite_ppu, 10);
    assert((sprite_cpu.memory[0xFF41] & 0x03) == 3);
    update_ppu(sprite_cpu, sprite_ppu, 1);
    assert((sprite_cpu.memory[0xFF41] & 0x03) == 0);
    update_ppu(sprite_cpu, sprite_ppu, 192);
    assert(sprite_cpu.memory[0xFF44] == 1);
}

static void test_mid_line_palette_change()
{
    CPU_LR35902 cpu{};
    PPU_LR35902 ppu{};
    cpu.memory[0xFF40] = 0x91;
    cpu.memory[0xFF47] = 0xE4;
    cpu.memory[0x8000] = 0xFF; // Every background pixel on line 0 is color index 1

    // Pixels are coloured from dot 95 onwards, one per dot, so this colours
    // pixels 0-79 before the palette changes.
    update_ppu(cpu, ppu, 175);
    cpu.memory[0xFF47] = 0xE0; // Color index 1 becomes shade 0
    update_ppu(cpu, ppu, 456 - 175);
    assert(ppu.framebuffer[79] == 0xFF88C070);
    assert(ppu.framebuffer[81] == 0xFFE0F8D0);
    assert(ppu.framebuffer[159] == 0xFFE0F8D0);
}

static void set_cgb_color(std::array<uint8_t, 64> &palette_ram, int palette, int index, uint16_t color)
{
    palette_ram[static_cast<std::size_t>(palette * 8 + index * 2)] = static_cast<uint8_t>(color);
    palette_ram[static_cast<std::size_t>(palette * 8 + index * 2 + 1)] = static_cast<uint8_t>(color >> 8);
}

static void test_cgb_tile_attributes_objects_and_priority()
{
    std::vector<uint8_t> rom(0x8000, 0);
    rom[0x0143] = 0xC0;
    std::string error;
    CPU_LR35902 cpu{};
    assert(load_cartridge(cpu, std::move(rom), error, GameBoyModel::Cgb));
    PPU_LR35902 ppu{};
    cpu.ppu = &ppu;
    std::fill(cpu.memory.begin() + 0x8000, cpu.memory.begin() + 0xA000, 0);
    cpu.memory[0xFF40] = 0x93; // LCD, BG, and objects on; tile data at $8000
    cpu.memory[0xFF42] = 0;
    cpu.memory[0xFF43] = 0;

    // Tile 1 in bank 1: row 0 has only its leftmost pixel set, in colour 1.
    cpu.vram_bank1[0x10] = 0x80;
    cpu.memory[0x9800] = 1;
    cpu.vram_bank1[0x1800] = 0x08 | 0x20 | 0x02; // Bank 1, horizontal flip, palette 2
    set_cgb_color(cpu.bg_palette_ram, 2, 0, 0x7FFF);  // White
    set_cgb_color(cpu.bg_palette_ram, 2, 1, 0x001F);  // Red

    // Tile 2 in bank 0 with background priority, colour 3 across the row.
    cpu.memory[0x8020] = 0xFF;
    cpu.memory[0x8021] = 0xFF;
    cpu.memory[0x9801] = 2;
    cpu.vram_bank1[0x1801] = 0x80 | 0x03; // Priority over objects, palette 3
    set_cgb_color(cpu.bg_palette_ram, 3, 3, 0x03E0); // Green

    // An object over both tiles from bank 1 with palette 5, colour 2.
    cpu.vram_bank1[0x30] = 0x00;
    cpu.vram_bank1[0x31] = 0xFF;
    cpu.memory[0xFE00] = 16;
    cpu.memory[0xFE01] = 8 + 4; // X 4-11
    cpu.memory[0xFE02] = 3;
    cpu.memory[0xFE03] = 0x08 | 0x05;
    set_cgb_color(cpu.obj_palette_ram, 5, 2, 0x7C00); // Blue

    update_ppu(cpu, ppu, 456);
    assert(ppu.framebuffer[0] == 0xFFFFFFFF); // Flipped, so the set pixel is on the right
    assert(ppu.framebuffer[3] == 0xFFFFFFFF);
    assert(ppu.framebuffer[4] == 0xFF0000FF); // The object over colour 0
    assert(ppu.framebuffer[7] == 0xFF0000FF); // The object over the flipped red pixel
    assert(ppu.framebuffer[8] == 0xFF00FF00); // Background priority keeps the green tile in front

    cpu.memory[0xFF40] = 0x92; // LCDC bit 0 clear: objects in front of everything
    update_ppu(cpu, ppu, 154 * 456);
    assert(ppu.framebuffer[8] == 0xFF0000FF);
    assert(ppu.framebuffer[12] == 0xFF00FF00); // The background is still drawn
}

// In DMG compatibility mode BGP's shades pick colours from BG palette 0.
// Moving WX under the window's last pixel inserts a pixel there; the line
// still ends at the screen's edge.
static void test_window_pixel_insertion_at_line_end()
{
    CPU_LR35902 cpu{};
    PPU_LR35902 ppu{};
    cpu.memory[0xFF40] = 0xB1; // LCD, window, unsigned tiles, and background enabled
    cpu.memory[0xFF47] = 0xE4;
    std::fill(cpu.memory.begin() + 0x8000, cpu.memory.begin() + 0x8010, 0xFF);
    for (int dot = 0; dot < 2 * 154 * 456; ++dot)
    {
        if (ppu.mode == 3 && ppu.pixel_x == 40)
        {
            cpu.memory[0xFF4B] = 165;
        }
        else if (ppu.mode != 3)
        {
            cpu.memory[0xFF4B] = 13;
        }
        update_ppu(cpu, ppu, 1);
        assert(ppu.pixel_x <= 160);
    }
}

// Line 0 shows mode 0 for its first dots after VBlank, but only HBlank raises
// the mode 0 STAT interrupt.
static void test_no_mode0_interrupt_as_line_0_starts()
{
    CPU_LR35902 cpu{};
    PPU_LR35902 ppu{};
    cpu.memory[0xFF40] = 0x91;
    cpu.memory[0xFF41] = 0x08; // Mode 0 interrupt
    update_ppu(cpu, ppu, 154 * 456 + 144 * 456); // Past the first frame, into VBlank
    assert(ppu.mode == 1);
    cpu.memory[0xFF0F] = 0;
    while (ppu.mode != 2)
    {
        update_ppu(cpu, ppu, 1);
    }
    assert(cpu.memory[0xFF44] == 0 && (cpu.memory[0xFF0F] & 0x02) == 0);
    while (ppu.mode != 0)
    {
        update_ppu(cpu, ppu, 1);
    }
    assert((cpu.memory[0xFF0F] & 0x02) != 0);
}

static void test_dmg_compatibility_colours()
{
    std::vector<uint8_t> rom(0x8000, 0);
    std::string error;
    CPU_LR35902 cpu{};
    assert(load_cartridge(cpu, std::move(rom), error, GameBoyModel::Cgb));
    PPU_LR35902 ppu{};
    cpu.ppu = &ppu;
    std::fill(cpu.memory.begin() + 0x8000, cpu.memory.begin() + 0xA000, 0);
    cpu.memory[0xFF40] = 0x91;
    cpu.memory[0xFF47] = 0x1B; // Colour 0 shows shade 3
    set_cgb_color(cpu.bg_palette_ram, 0, 3, 0x7C00);
    update_ppu(cpu, ppu, 456);
    assert(ppu.framebuffer[0] == 0xFF0000FF);
}

int main()
{
    test_ppu_modes_scanlines_and_background();
    test_lcd_off_resets_ly();
    test_ly_reads_zero_on_line_153();
    test_cgb_tile_attributes_objects_and_priority();
    test_dmg_compatibility_colours();
    test_window_pixel_insertion_at_line_end();
    test_no_mode0_interrupt_as_line_0_starts();
    test_ppu_window_composition();
    test_ppu_sprite_priority_palettes_flips_and_8x16();
    test_stat_mode_interrupt_edges();
    test_stat_lyc_interrupt_edges();
    test_variable_mode3_timing();
    test_mid_line_palette_change();
    return 0;
}