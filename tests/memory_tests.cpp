#include "LR35902.h"
#include "PPU.h"

#include <cassert>
#include <string>
#include <vector>

static void test_unusable_oam_region()
{
    CPU_LR35902 cpu{};
    cpu.memory[0xFE9F] = 0x55;
    cpu.memory[0xFEA0] = 0xAA;
    cpu.memory[0xFEFF] = 0xBB;

    assert(cpu_read(cpu, 0xFE9F) == 0x55);
    assert(cpu_read(cpu, 0xFEA0) == 0xFF);
    assert(cpu_read(cpu, 0xFEFF) == 0xFF);

    cpu_write(cpu, 0xFEA0, 0x11);
    cpu_write(cpu, 0xFEFF, 0x22);
    assert(cpu.memory[0xFEA0] == 0xAA);
    assert(cpu.memory[0xFEFF] == 0xBB);
}

static void test_interrupt_flags_unused_bits()
{
    CPU_LR35902 cpu{};
    cpu_write(cpu, 0xFF0F, 0x08);
    assert(cpu.memory[0xFF0F] == 0x08);
    assert(cpu_read(cpu, 0xFF0F) == 0xE8);
}

static void test_ly_is_read_only()
{
    CPU_LR35902 cpu{};
    cpu.memory[0xFF44] = 0x12;
    cpu_write(cpu, 0xFF44, 0xC8);
    assert(cpu.memory[0xFF44] == 0x12);
}

static void test_oam_dma_transfer_and_bus_lock()
{
    CPU_LR35902 cpu{};
    for (std::uint16_t index = 0; index < 0xA0; ++index)
    {
        cpu.memory[0xC000 + index] = static_cast<std::uint8_t>(index ^ 0x5A);
    }

    cpu_write(cpu, 0xFF46, 0xC0);
    assert(!cpu.oam_dma_active);
    update_oam_dma(cpu, 4);
    assert(cpu_read(cpu, 0xC000) == 0x5A);

    update_oam_dma(cpu, 4); // The transfer starts two M-cycles after the write
    assert(cpu.oam_dma_active);
    assert(cpu.memory[0xFE00] == 0);
    assert(cpu_read(cpu, 0xC000) == 0xFF);
    assert(cpu_read(cpu, 0xFF80) == 0);

    update_oam_dma(cpu, 4);
    assert(cpu.memory[0xFE00] == 0x5A);

    update_oam_dma(cpu, 159 * 4);
    assert(!cpu.oam_dma_active);
    assert(cpu.oam_dma_index == 0xA0);
    for (std::uint16_t index = 0; index < 0xA0; ++index)
    {
        assert(cpu.memory[0xFE00 + index] == static_cast<std::uint8_t>(index ^ 0x5A));
    }
    assert(cpu_read(cpu, 0xC000) == 0x5A);
}

static void test_oam_dma_fe_source_uses_echo_bus()
{
    CPU_LR35902 cpu{};
    cpu.memory[0xDE00] = 0xC2;
    cpu.memory[0xDE01] = 0xD5;
    cpu_write(cpu, 0xFF46, 0xFE);
    update_oam_dma(cpu, 16); // Two M-cycles to start, then one byte per M-cycle
    assert(cpu.memory[0xFE00] == 0xC2);
    assert(cpu.memory[0xFE01] == 0xD5);
}

static void fill_oam_pattern(CPU_LR35902 &cpu)
{
    for (int index = 0; index < 0xA0; ++index)
    {
        cpu.memory[0xFE00 + index] = static_cast<uint8_t>(index * 7 + 3);
    }
}

static void test_oam_corruption()
{
    CPU_LR35902 cpu{};
    PPU_LR35902 ppu{};
    cpu.ppu = &ppu;
    cpu.memory[0xFF40] = 0x91;
    update_ppu(cpu, ppu, 456 + 8); // Line 1, scanning OAM row 3
    assert(ppu_oam_scan_row(cpu, ppu) == 3);
    cpu.bus_ticking = true;

    fill_oam_pattern(cpu);
    const uint16_t a = static_cast<uint16_t>(cpu.memory[0xFE18] | (cpu.memory[0xFE19] << 8));
    const uint16_t b = static_cast<uint16_t>(cpu.memory[0xFE10] | (cpu.memory[0xFE11] << 8));
    const uint16_t c = static_cast<uint16_t>(cpu.memory[0xFE14] | (cpu.memory[0xFE15] << 8));
    cpu_corrupt_oam(cpu, 0xFE40, OamCorruption::Write);
    const uint16_t written = static_cast<uint16_t>(((a ^ c) & (b ^ c)) ^ c);
    assert(cpu.memory[0xFE18] == static_cast<uint8_t>(written));
    assert(cpu.memory[0xFE19] == static_cast<uint8_t>(written >> 8));
    for (int index = 2; index < 8; ++index)
    {
        assert(cpu.memory[0xFE18 + index] == cpu.memory[0xFE10 + index]); // Copied from the previous row
    }

    fill_oam_pattern(cpu);
    cpu_corrupt_oam(cpu, 0xFDFF, OamCorruption::Write); // Address outside OAM
    cpu_corrupt_oam(cpu, 0xFF00, OamCorruption::Read);
    assert(cpu.memory[0xFE18] == static_cast<uint8_t>(0x18 * 7 + 3));

    update_ppu(cpu, ppu, 100); // Mode 3: no OAM scan
    assert(ppu_oam_scan_row(cpu, ppu) == -1);
    cpu_corrupt_oam(cpu, 0xFE00, OamCorruption::Read);
    assert(cpu.memory[0xFE18] == static_cast<uint8_t>(0x18 * 7 + 3));
}

static std::vector<uint8_t> make_cgb_rom()
{
    std::vector<uint8_t> rom(0x8000, 0);
    rom[0x0143] = 0xC0; // Game Boy Color only
    return rom;
}

static void test_cgb_banks_and_palettes()
{
    std::string error;
    CPU_LR35902 cpu{};
    assert(load_cartridge(cpu, make_cgb_rom(), error, GameBoyModel::Cgb));
    assert(cpu.cgb_hardware && cpu.cgb_mode);
    assert(cpu.af.reg8.high == 0x11);

    cpu_write(cpu, 0x8000, 0x11);
    cpu_write(cpu, 0xFF4F, 0x01);
    assert(cpu_read(cpu, 0xFF4F) == 0xFF);
    cpu_write(cpu, 0x8000, 0x22);
    assert(cpu.vram_bank1[0] == 0x22 && cpu.memory[0x8000] == 0x11);
    cpu_write(cpu, 0xFF4F, 0x00);
    assert(cpu_read(cpu, 0x8000) == 0x11);

    cpu_write(cpu, 0xD000, 0x01); // Bank 1 (SVBK 0 also selects bank 1)
    cpu_write(cpu, 0xFF70, 0x05);
    assert(cpu_read(cpu, 0xFF70) == 0xFD);
    cpu_write(cpu, 0xD000, 0x05);
    assert(cpu_read(cpu, 0xF000) == 0x05); // Echo RAM follows the bank
    cpu_write(cpu, 0xFF70, 0x00);
    assert(cpu_read(cpu, 0xD000) == 0x01);
    assert(cpu_read(cpu, 0xC000) == cpu.memory[0xC000]);

    cpu_write(cpu, 0xFF68, 0x80 | 0x3F); // Auto-increment wraps after the last byte
    cpu_write(cpu, 0xFF69, 0xAB);
    cpu_write(cpu, 0xFF69, 0xCD);
    assert(cpu.bg_palette_ram[0x3F] == 0xAB && cpu.bg_palette_ram[0x00] == 0xCD);
    assert(cpu_read(cpu, 0xFF68) == 0xC1);
    cpu_write(cpu, 0xFF68, 0x00);
    assert(cpu_read(cpu, 0xFF69) == 0xCD);
}

static void test_cgb_speed_switch_and_dma()
{
    std::string error;
    CPU_LR35902 cpu{};
    std::vector<uint8_t> rom = make_cgb_rom();
    rom[0x0100] = 0x10; // STOP
    rom[0x0101] = 0x00;
    for (int index = 0; index < 0x20; ++index)
    {
        rom[0x4000 + index] = static_cast<uint8_t>(index + 1);
    }
    assert(load_cartridge(cpu, std::move(rom), error, GameBoyModel::Cgb));

    assert(cpu_read(cpu, 0xFF4D) == 0x7E);
    cpu_write(cpu, 0xFF4D, 0x01);
    execute_instruction(cpu);
    assert(cpu.double_speed && !cpu.stopped);
    assert(cpu_read(cpu, 0xFF4D) == 0xFE);

    cpu_write(cpu, 0xFF51, 0x40); // From $4000
    cpu_write(cpu, 0xFF52, 0x00);
    cpu_write(cpu, 0xFF53, 0x01); // To $8100
    cpu_write(cpu, 0xFF54, 0x00);
    cpu_write(cpu, 0xFF55, 0x01); // Two blocks, straight away
    assert(cpu.memory[0x8100] == 0x01 && cpu.memory[0x811F] == 0x20);
    assert(cpu_read(cpu, 0xFF55) == 0xFF);
    assert(cpu.hdma_stall_cycles == 32); // Eight M-cycles per block, doubled in double speed

    cpu_write(cpu, 0xFF55, 0x80); // One block per HBlank
    assert(cpu_read(cpu, 0xFF55) == 0x00);
    cpu_hblank_started(cpu);
    assert(cpu_read(cpu, 0xFF55) == 0xFF && !cpu.hdma_hblank_active);
}

// STAT shows mode 0 with the LCD off, so an HBlank DMA started then copies its
// first block straight away.
static void test_hblank_dma_started_in_mode_0()
{
    std::string error;
    CPU_LR35902 cpu{};
    PPU_LR35902 ppu{};
    std::vector<uint8_t> rom = make_cgb_rom();
    for (int index = 0; index < 0x20; ++index)
    {
        rom[0x4000 + index] = static_cast<uint8_t>(index + 1);
    }
    assert(load_cartridge(cpu, std::move(rom), error, GameBoyModel::Cgb));
    cpu.ppu = &ppu;
    cpu.memory[0xFF40] = 0x00;
    update_ppu(cpu, ppu, 4);

    cpu_write(cpu, 0xFF51, 0x40); // From $4000
    cpu_write(cpu, 0xFF52, 0x00);
    cpu_write(cpu, 0xFF53, 0x01); // To $8100
    cpu_write(cpu, 0xFF54, 0x00);
    cpu_write(cpu, 0xFF55, 0x81); // Two blocks, one per HBlank
    assert(cpu.memory[0x8100] == 0x01 && cpu.memory[0x810F] == 0x10);
    assert(cpu.memory[0x8110] == 0x00);
    assert(cpu_read(cpu, 0xFF55) == 0x00); // One block left
}

// The CGB has separate cartridge, WRAM, and VRAM buses, so OAM DMA only
// blocks the bus it copies from; the DMG's cartridge and WRAM share one.
static void test_oam_dma_bus_conflicts_by_model()
{
    for (const bool cgb : {false, true})
    {
        std::vector<uint8_t> rom(0x8000, 0);
        rom[0x0143] = cgb ? 0xC0 : 0x00;
        rom[0x1234] = 0x5A;
        std::string error;
        CPU_LR35902 cpu{};
        assert(load_cartridge(cpu, std::move(rom), error, cgb ? GameBoyModel::Cgb : GameBoyModel::DmgABC));
        cpu.memory[0xC100] = 0xA5;
        cpu.bus_ticking = true;
        cpu_write(cpu, 0xFF46, 0xC1); // DMA from WRAM
        cpu_read(cpu, 0xFF80);
        cpu_read(cpu, 0xFF80);
        assert(cpu.oam_dma_active);
        assert(cpu_read(cpu, 0x1234) == (cgb ? 0x5A : 0xFF)); // ROM
        assert(cpu_read(cpu, 0xC100) == 0xFF);               // WRAM, being copied from
        assert(cpu_read(cpu, 0xFE00) == 0xFF);               // OAM, being copied to
        cpu_write(cpu, 0xFF06, 0x42);                        // I/O and IE are inside the CPU
        assert(cpu_read(cpu, 0xFF06) == 0x42);
        cpu_write(cpu, 0xFFFF, 0x15);
        assert(cpu_read(cpu, 0xFFFF) == 0x15);
        assert(cpu.oam_dma_active);
    }
}

static void test_cgb_registers_in_dmg_mode()
{
    std::string error;
    CPU_LR35902 cpu{};
    std::vector<uint8_t> rom(0x8000, 0); // Not made for the Game Boy Color
    assert(load_cartridge(cpu, std::move(rom), error, GameBoyModel::Cgb));
    assert(cpu.cgb_hardware && !cpu.cgb_mode);
    cpu_write(cpu, 0xFF4F, 0x01);
    cpu_write(cpu, 0xFF70, 0x03);
    assert(cpu_read(cpu, 0xFF4F) == 0xFE); // VBK stays bank 0
    assert(cpu_read(cpu, 0xFF70) == 0xFF);
    assert(cpu_read(cpu, 0xFF4D) == 0xFF);
    assert(cpu_read(cpu, 0xFF68) == 0xC8);
}

int main()
{
    test_unusable_oam_region();
    test_interrupt_flags_unused_bits();
    test_ly_is_read_only();
    test_oam_dma_transfer_and_bus_lock();
    test_oam_dma_fe_source_uses_echo_bus();
    test_oam_corruption();
    test_cgb_banks_and_palettes();
    test_cgb_speed_switch_and_dma();
    test_cgb_registers_in_dmg_mode();
    test_hblank_dma_started_in_mode_0();
    test_oam_dma_bus_conflicts_by_model();
    return 0;
}
