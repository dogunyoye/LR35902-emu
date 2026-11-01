#include "LR35902.h"

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

static void test_oam_dma_transfer_and_bus_lock()
{
    CPU_LR35902 cpu{};
    for (std::uint16_t index = 0; index < 0xA0; ++index)
    {
        cpu.memory[0xC000 + index] = static_cast<std::uint8_t>(index ^ 0x5A);
    }

    cpu_write(cpu, 0xFF46, 0xC0);
    assert(cpu.oam_dma_active);
    assert(cpu_read(cpu, 0xC000) == 0x5A);
    update_oam_dma(cpu, 3);
    assert(cpu.memory[0xFE00] == 0);
    assert(cpu_read(cpu, 0xC000) == 0x5A);

    update_oam_dma(cpu, 1);
    assert(cpu.memory[0xFE00] == 0x5A);
    assert(cpu_read(cpu, 0xC000) == 0xFF);
    assert(cpu_read(cpu, 0xFF80) == 0);

    update_oam_dma(cpu, 636);
    assert(!cpu.oam_dma_active);
    assert(cpu.oam_dma_index == 0xA0);
    assert(!cpu.oam_dma_bus_blocked);
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
    update_oam_dma(cpu, 12);
    assert(cpu.memory[0xFE00] == 0xC2);
    assert(cpu.memory[0xFE01] == 0xD5);
}

int main()
{
    test_unusable_oam_region();
    test_interrupt_flags_unused_bits();
    test_oam_dma_transfer_and_bus_lock();
    test_oam_dma_fe_source_uses_echo_bus();
    return 0;
}
