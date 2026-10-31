#include "LR35902.h"

#include <cassert>
#include <utility>

static void test_rom_only_cartridge()
{
    CPU_LR35902 cpu{};
    std::vector<uint8_t> rom(0x8000, 0);
    rom[0x0100] = 0x3E; // LD A, d8
    rom[0x0101] = 0x42;
    rom[0x0147] = 0x00; // ROM ONLY
    rom[0x0148] = 0x00; // 32 KiB
    std::string error;

    assert(load_rom_only_cartridge(cpu, std::move(rom), error));
    assert(error.empty());
    assert(cpu.pc == 0x0100);
    assert(cpu.af.reg16 == 0x01B0);
    assert(cpu.sp == 0xFFFE);
    assert(cpu.memory[0xFF40] == 0x91);
    assert(cpu.memory[0xFF48] == 0xFF);
    assert(cpu.memory[0xFF49] == 0xFF);
    assert(cpu_read(cpu, 0x0100) == 0x3E);
    assert(execute_instruction(cpu) == 8);
    assert(cpu.af.reg8.high == 0x42);

    cpu_write(cpu, 0x0100, 0x00);
    assert(cpu_read(cpu, 0x0100) == 0x3E);

    std::vector<uint8_t> unsupported_type(0x8000, 0);
    unsupported_type[0x0147] = 0x01;
    assert(!load_rom_only_cartridge(cpu, std::move(unsupported_type), error));
    assert(!error.empty());
    assert(cpu_read(cpu, 0x0100) == 0x3E);

    std::vector<uint8_t> wrong_size(0x4000, 0);
    assert(!load_rom_only_cartridge(cpu, std::move(wrong_size), error));
    assert(!error.empty());
}

static void test_mbc1_rom_banking()
{
    constexpr std::size_t ROM_BANK_SIZE = 0x4000;
    constexpr std::size_t ROM_BANK_COUNT = 64;
    std::vector<uint8_t> rom(ROM_BANK_SIZE * ROM_BANK_COUNT, 0);
    for (std::size_t bank = 0; bank < ROM_BANK_COUNT; ++bank)
    {
        rom[bank * ROM_BANK_SIZE] = static_cast<uint8_t>(bank);
    }
    rom[0x0147] = 0x01; // MBC1
    rom[0x0148] = 0x05; // 64 ROM banks
    rom[0x0149] = 0x00; // No cartridge RAM
    std::string error;
    CPU_LR35902 cpu{};

    assert(load_cartridge(cpu, std::move(rom), error));
    assert(cpu.cartridge_mbc1);
    assert(cpu_read(cpu, 0x0000) == 0);
    assert(cpu_read(cpu, 0x4000) == 1);

    cpu_write(cpu, 0x2000, 2);
    assert(cpu_read(cpu, 0x4000) == 2);
    cpu_write(cpu, 0x2000, 0); // Bank zero aliases bank one in the switchable window
    assert(cpu_read(cpu, 0x4000) == 1);

    cpu_write(cpu, 0x4000, 1);
    assert(cpu_read(cpu, 0x0000) == 0);
    assert(cpu_read(cpu, 0x4000) == 33);

    cpu_write(cpu, 0x6000, 1); // Mode 1 maps high bits into the lower window
    assert(cpu_read(cpu, 0x0000) == 32);
    assert(cpu_read(cpu, 0x4000) == 33);
    cpu_write(cpu, 0x6000, 0);
    assert(cpu_read(cpu, 0x0000) == 0);
}

static void test_mbc1_ram_banking_and_enable()
{
    std::vector<uint8_t> rom(0x8000, 0);
    rom[0x0147] = 0x03; // MBC1 + RAM + battery
    rom[0x0148] = 0x00; // 2 ROM banks
    rom[0x0149] = 0x03; // 32 KiB RAM
    std::string error;
    CPU_LR35902 cpu{};

    assert(load_cartridge(cpu, std::move(rom), error));
    assert(cpu_read(cpu, 0xA000) == 0xFF);
    cpu_write(cpu, 0xA000, 0x12);
    assert(cpu_read(cpu, 0xA000) == 0xFF);

    cpu_write(cpu, 0x0000, 0x0A); // Enable cartridge RAM
    cpu_write(cpu, 0xA000, 0x12);
    assert(cpu_read(cpu, 0xA000) == 0x12);

    cpu_write(cpu, 0x4000, 2);
    assert(cpu_read(cpu, 0xA000) == 0x12); // Mode 0 keeps RAM bank 0 selected

    cpu_write(cpu, 0x6000, 1);
    assert(cpu_read(cpu, 0xA000) == 0xFF);
    cpu_write(cpu, 0xA000, 0x34);
    assert(cpu_read(cpu, 0xA000) == 0x34);

    cpu_write(cpu, 0x4000, 1);
    assert(cpu_read(cpu, 0xA000) == 0xFF);
    cpu_write(cpu, 0x4000, 0);
    assert(cpu_read(cpu, 0xA000) == 0x12);
    cpu_write(cpu, 0x4000, 2);
    assert(cpu_read(cpu, 0xA000) == 0x34);

    cpu_write(cpu, 0x0000, 0x00); // Disable cartridge RAM
    cpu_write(cpu, 0xA000, 0x56);
    assert(cpu_read(cpu, 0xA000) == 0xFF);
    cpu_write(cpu, 0x0000, 0x0A);
    assert(cpu_read(cpu, 0xA000) == 0x34);
}

int main()
{
    test_rom_only_cartridge();
    test_mbc1_rom_banking();
    test_mbc1_ram_banking_and_enable();
    return 0;
}