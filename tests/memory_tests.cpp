#include "LR35902.h"
#include "PPU.h"

#include <cassert>

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

int main()
{
    test_unusable_oam_region();
    test_interrupt_flags_unused_bits();
    test_ly_is_read_only();
    test_oam_dma_transfer_and_bus_lock();
    test_oam_dma_fe_source_uses_echo_bus();
    test_oam_corruption();
    return 0;
}
