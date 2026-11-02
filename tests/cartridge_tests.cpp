#include "LR35902.h"

#include <algorithm>
#include <cassert>
#include <iterator>
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

static void test_mbc1_multicart_rom_banking()
{
    constexpr std::size_t ROM_BANK_SIZE = 0x4000;
    constexpr std::size_t ROM_BANK_COUNT = 64;
    constexpr uint8_t LOGO[] = {
        0xCE, 0xED, 0x66, 0x66, 0xCC, 0x0D, 0x00, 0x0B, 0x03, 0x73, 0x00, 0x83,
        0x00, 0x0C, 0x00, 0x0D, 0x00, 0x08, 0x11, 0x1F, 0x88, 0x89, 0x00, 0x0E,
        0xDC, 0xCC, 0x6E, 0xE6, 0xDD, 0xDD, 0xD9, 0x99, 0xBB, 0xBB, 0x67, 0x63,
        0x6E, 0x0E, 0xEC, 0xCC, 0xDD, 0xDC, 0x99, 0x9F, 0xBB, 0xB9, 0x33, 0x3E};
    std::vector<uint8_t> rom(ROM_BANK_SIZE * ROM_BANK_COUNT, 0);
    for (std::size_t bank = 0; bank < ROM_BANK_COUNT; ++bank)
    {
        rom[bank * ROM_BANK_SIZE] = static_cast<uint8_t>(bank);
    }
    std::copy(std::begin(LOGO), std::end(LOGO), rom.begin() + 0x10 * ROM_BANK_SIZE + 0x0104);
    rom[0x0147] = 0x01; // MBC1
    rom[0x0148] = 0x05; // 64 ROM banks
    rom[0x0149] = 0x00;
    std::string error;
    CPU_LR35902 cpu{};

    assert(load_cartridge(cpu, std::move(rom), error));
    assert(cpu.cartridge_mbc1_multicart);

    cpu_write(cpu, 0x2000, 0x12); // BANK1 bit 4 is not connected
    assert(cpu_read(cpu, 0x4000) == 0x02);
    cpu_write(cpu, 0x4000, 1); // BANK2 selects the 2 Mbit game
    assert(cpu_read(cpu, 0x4000) == 0x12);
    cpu_write(cpu, 0x6000, 1);
    assert(cpu_read(cpu, 0x0000) == 0x10);
}

static void test_mbc2_banking_and_ram()
{
    constexpr std::size_t ROM_BANK_SIZE = 0x4000;
    constexpr std::size_t ROM_BANK_COUNT = 16;
    std::vector<uint8_t> rom(ROM_BANK_SIZE * ROM_BANK_COUNT, 0);
    for (std::size_t bank = 0; bank < ROM_BANK_COUNT; ++bank)
    {
        rom[bank * ROM_BANK_SIZE] = static_cast<uint8_t>(bank);
    }
    rom[0x0147] = 0x06; // MBC2 + battery
    rom[0x0148] = 0x03; // 16 ROM banks
    rom[0x0149] = 0x00; // Built-in RAM is not described by the header
    std::string error;
    CPU_LR35902 cpu{};

    assert(load_cartridge(cpu, std::move(rom), error));
    assert(cpu.cartridge_mbc2);
    assert(cpu_read(cpu, 0x4000) == 1);

    cpu_write(cpu, 0x2100, 0x05); // Address bit 8 set selects ROMB
    assert(cpu_read(cpu, 0x4000) == 5);
    cpu_write(cpu, 0x0100, 0x00);
    assert(cpu_read(cpu, 0x4000) == 1); // Bank zero aliases bank one
    cpu_write(cpu, 0x4000, 0x03);       // Not decoded
    assert(cpu_read(cpu, 0x4000) == 1);

    cpu_write(cpu, 0xA000, 0x05);
    assert(cpu_read(cpu, 0xA000) == 0xFF);
    cpu_write(cpu, 0x2000, 0x0A); // Address bit 8 clear selects RAMG
    cpu_write(cpu, 0xA000, 0x35);
    assert(cpu_read(cpu, 0xA000) == 0xF5); // Only the low nibble exists
    assert(cpu_read(cpu, 0xA200) == 0xF5); // 512 addresses repeat
    cpu_write(cpu, 0x0000, 0x00);
    assert(cpu_read(cpu, 0xA000) == 0xFF);
}

static void test_mbc5_banking_and_ram()
{
    std::vector<uint8_t> rom(0x8000, 0);
    rom[0x0147] = 0x1B; // MBC5 + RAM + battery
    rom[0x0148] = 0x00;
    rom[0x0149] = 0x02;
    rom[0x4000] = 0x51;
    std::string error;
    CPU_LR35902 cpu{};

    assert(load_cartridge(cpu, std::move(rom), error));
    assert(cpu.cartridge_mbc5);
    assert(cpu.cartridge_battery_backed);
    assert(cpu_read(cpu, 0x4000) == 0x51);
    cpu_write(cpu, 0x2000, 0);
    assert(cpu_read(cpu, 0x4000) == cpu_read(cpu, 0x0000)); // MBC5 permits bank zero.

    cpu_write(cpu, 0x0000, 0x0A);
    cpu_write(cpu, 0xA000, 0x37);
    assert(cpu_read(cpu, 0xA000) == 0x37);
    cpu_write(cpu, 0x0000, 0x00);
    assert(cpu_read(cpu, 0xA000) == 0xFF);
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

static std::vector<uint8_t> make_mbc3_rom(uint8_t cartridge_type, uint8_t ram_size_code)
{
    constexpr std::size_t ROM_BANK_SIZE = 0x4000;
    constexpr std::size_t ROM_BANK_COUNT = 128;
    std::vector<uint8_t> rom(ROM_BANK_SIZE * ROM_BANK_COUNT, 0);
    for (std::size_t bank = 0; bank < ROM_BANK_COUNT; ++bank)
    {
        rom[bank * ROM_BANK_SIZE] = static_cast<uint8_t>(bank);
    }
    rom[0x0147] = cartridge_type;
    rom[0x0148] = 0x06; // 128 ROM banks
    rom[0x0149] = ram_size_code;
    return rom;
}

static void test_mbc3_banking()
{
    std::string error;
    CPU_LR35902 cpu{};
    assert(load_cartridge(cpu, make_mbc3_rom(0x13, 0x03), error)); // MBC3 + RAM + battery, 32 KiB
    assert(cpu.cartridge_mbc3);
    assert(!cpu.cartridge_has_rtc);
    assert(cpu_read(cpu, 0x4000) == 1);

    cpu_write(cpu, 0x2000, 0x45);
    assert(cpu_read(cpu, 0x4000) == 0x45);
    cpu_write(cpu, 0x2000, 0xFF); // Seven bank bits
    assert(cpu_read(cpu, 0x4000) == 0x7F);
    cpu_write(cpu, 0x2000, 0x00); // Bank zero aliases bank one
    assert(cpu_read(cpu, 0x4000) == 1);

    cpu_write(cpu, 0x0000, 0x0A);
    for (uint8_t bank = 0; bank < 4; ++bank)
    {
        cpu_write(cpu, 0x4000, bank);
        cpu_write(cpu, 0xA000, static_cast<uint8_t>(0x10 + bank));
    }
    for (uint8_t bank = 0; bank < 4; ++bank)
    {
        cpu_write(cpu, 0x4000, bank);
        assert(cpu_read(cpu, 0xA000) == 0x10 + bank);
    }
    cpu_write(cpu, 0x4000, 0x08); // No clock on this cartridge
    assert(cpu_read(cpu, 0xA000) == 0xFF);
}

static uint8_t read_latched_rtc(CPU_LR35902 &cpu, uint8_t reg)
{
    cpu_write(cpu, 0x4000, reg);
    return cpu_read(cpu, 0xA000);
}

static void write_rtc(CPU_LR35902 &cpu, uint8_t reg, uint8_t value)
{
    cpu_write(cpu, 0x4000, reg);
    cpu_write(cpu, 0xA000, value);
}

static void latch_rtc(CPU_LR35902 &cpu)
{
    cpu_write(cpu, 0x6000, 0x00);
    cpu_write(cpu, 0x6000, 0x01);
}

static void test_mbc3_rtc()
{
    constexpr int CYCLES_PER_SECOND = 4194304;
    std::string error;
    CPU_LR35902 cpu{};
    assert(load_cartridge(cpu, make_mbc3_rom(0x10, 0x03), error)); // MBC3 + timer + RAM + battery
    assert(cpu.cartridge_has_rtc);
    cpu_write(cpu, 0x0000, 0x0A);

    write_rtc(cpu, 0x08, 58);
    write_rtc(cpu, 0x09, 59);
    write_rtc(cpu, 0x0A, 23);
    write_rtc(cpu, 0x0B, 0xFF);
    write_rtc(cpu, 0x0C, 0x01); // Day 511
    update_cartridge_rtc(cpu, CYCLES_PER_SECOND);
    assert(read_latched_rtc(cpu, 0x08) == 58); // Reads return the latched copy
    latch_rtc(cpu);
    assert(read_latched_rtc(cpu, 0x08) == 59);

    update_cartridge_rtc(cpu, CYCLES_PER_SECOND); // 23:59:59 on day 511 rolls over
    latch_rtc(cpu);
    assert(read_latched_rtc(cpu, 0x08) == 0);
    assert(read_latched_rtc(cpu, 0x09) == 0);
    assert(read_latched_rtc(cpu, 0x0A) == 0);
    assert(read_latched_rtc(cpu, 0x0B) == 0);
    assert(read_latched_rtc(cpu, 0x0C) == 0x80); // Day carry set, day bit 8 clear

    write_rtc(cpu, 0x0C, 0x40); // Halt
    update_cartridge_rtc(cpu, CYCLES_PER_SECOND * 5);
    latch_rtc(cpu);
    assert(read_latched_rtc(cpu, 0x08) == 0);

    write_rtc(cpu, 0x0C, 0x00);
    write_rtc(cpu, 0x08, 0xFF); // Out-of-range seconds count to 63, then wrap without a carry
    latch_rtc(cpu);
    assert(read_latched_rtc(cpu, 0x08) == 0x3F);
    update_cartridge_rtc(cpu, CYCLES_PER_SECOND);
    latch_rtc(cpu);
    assert(read_latched_rtc(cpu, 0x08) == 0);
    assert(read_latched_rtc(cpu, 0x09) == 0);

    advance_cartridge_rtc_seconds(cpu, 3 * 86400 + 3661);
    latch_rtc(cpu);
    assert(read_latched_rtc(cpu, 0x0B) == 3);
    assert(read_latched_rtc(cpu, 0x0A) == 1);
    assert(read_latched_rtc(cpu, 0x09) == 1);
    assert(read_latched_rtc(cpu, 0x08) == 1);
}

static void test_mbc3_rtc_persistence()
{
    const std::filesystem::path save_path = std::filesystem::temp_directory_path() / "lr35902-rtc-test.sav";
    std::error_code filesystem_error;
    std::filesystem::remove(save_path, filesystem_error);

    std::string error;
    CPU_LR35902 first_run{};
    assert(load_cartridge(first_run, make_mbc3_rom(0x0F, 0x00), error)); // Timer + battery, no RAM
    assert(first_run.cartridge_ram.empty());
    cpu_write(first_run, 0x0000, 0x0A);
    write_rtc(first_run, 0x0C, 0x40); // Halted, so no time passes between save and load
    write_rtc(first_run, 0x09, 42);
    write_rtc(first_run, 0x0B, 7);
    assert(save_cartridge_ram(first_run, save_path, error));
    assert(std::filesystem::file_size(save_path) == 48);

    CPU_LR35902 second_run{};
    assert(load_cartridge(second_run, make_mbc3_rom(0x0F, 0x00), error));
    assert(load_cartridge_ram(second_run, save_path, error));
    cpu_write(second_run, 0x0000, 0x0A);
    latch_rtc(second_run);
    assert(read_latched_rtc(second_run, 0x09) == 42);
    assert(read_latched_rtc(second_run, 0x0B) == 7);
    assert(read_latched_rtc(second_run, 0x0C) == 0x40);

    std::filesystem::remove(save_path, filesystem_error);
}

int main()
{
    test_rom_only_cartridge();
    test_mbc1_rom_banking();
    test_mbc1_ram_banking_and_enable();
    test_mbc1_multicart_rom_banking();
    test_mbc2_banking_and_ram();
    test_mbc5_banking_and_ram();
    test_battery_ram_persistence();
    test_mbc3_banking();
    test_mbc3_rtc();
    test_mbc3_rtc_persistence();
    return 0;
}
