#include "LR35902.h"
#include "PPU.h"

#include <cassert>

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
    assert(ppu.framebuffer[0] == 0xFF3A5F40);
    assert(ppu.framebuffer[1] == 0xFF081820);

    update_ppu(cpu, ppu, 203);
    assert(cpu.memory[0xFF44] == 1);
    assert((cpu.memory[0xFF41] & 0x03) == 0); // Mode 2 is reported four dots after LY changes
    assert(cpu.oam_locked);
    assert(!cpu.oam_write_locked);

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
    assert(ppu.framebuffer[79] == 0xFF3A5F40);
    assert(ppu.framebuffer[80] == 0xFF9BBC0F);
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
    assert(priority_ppu.framebuffer[4] == 0xFFFFFFFF);

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
    assert(flip_ppu.framebuffer[0] == 0xFF081820);
    assert(flip_ppu.framebuffer[7] == 0xFFFFFFFF);

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
    assert(tall_ppu.framebuffer[0] == 0xFF9BBC0F);

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
    assert(hidden_ppu.framebuffer[0] == 0xFF3A5F40);
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

int main()
{
    test_ppu_modes_scanlines_and_background();
    test_ppu_window_composition();
    test_ppu_sprite_priority_palettes_flips_and_8x16();
    test_stat_mode_interrupt_edges();
    test_stat_lyc_interrupt_edges();
    test_variable_mode3_timing();
    return 0;
}