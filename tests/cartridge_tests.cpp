#include "LR35902.h"

#include <cassert>
#include <filesystem>
#include <utility>

static std::vector<uint8_t> make_mbc1_ram_rom(uint8_t cartridge_type)
{
    std::vector<uint8_t> rom(0x8000, 0);
    rom[0x0147] = cartridge_type;
    rom[0x0148] = 0x00; // 2 ROM banks
    rom[0x0149] = 0x02; // 8 KiB RAM
    return rom;
}

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

static void test_battery_ram_persistence()
{
    const std::filesystem::path save_path =
        std::filesystem::temp_directory_path() / "lr35902-battery-ram-test.sav";
    const std::filesystem::path volatile_save_path =
        std::filesystem::temp_directory_path() / "lr35902-volatile-ram-test.sav";
    std::error_code filesystem_error;
    std::filesystem::remove(save_path, filesystem_error);
    std::filesystem::remove(volatile_save_path, filesystem_error);

    std::string error;
    CPU_LR35902 first_run{};
    assert(load_cartridge(first_run, make_mbc1_ram_rom(0x03), error));
    assert(first_run.cartridge_battery_backed);
    assert(load_cartridge_ram(first_run, save_path, error));
    assert(first_run.cartridge_ram[0] == 0xFF);

    cpu_write(first_run, 0x0000, 0x0A);
    cpu_write(first_run, 0xA000, 0x42);
    cpu_write(first_run, 0xA123, 0x9A);
    assert(save_cartridge_ram(first_run, save_path, error));

    CPU_LR35902 second_run{};
    assert(load_cartridge(second_run, make_mbc1_ram_rom(0x03), error));
    assert(load_cartridge_ram(second_run, save_path, error));
    cpu_write(second_run, 0x0000, 0x0A);
    assert(cpu_read(second_run, 0xA000) == 0x42);
    assert(cpu_read(second_run, 0xA123) == 0x9A);

    CPU_LR35902 volatile_ram{};
    assert(load_cartridge(volatile_ram, make_mbc1_ram_rom(0x02), error));
    assert(!volatile_ram.cartridge_battery_backed);
    cpu_write(volatile_ram, 0x0000, 0x0A);
    cpu_write(volatile_ram, 0xA000, 0x55);
    assert(save_cartridge_ram(volatile_ram, volatile_save_path, error));
    assert(!std::filesystem::exists(volatile_save_path));

    std::filesystem::remove(save_path, filesystem_error);
}

int main()
{
    test_rom_only_cartridge();
    test_mbc1_rom_banking();
    test_mbc1_ram_banking_and_enable();
    test_battery_ram_persistence();
    return 0;
}