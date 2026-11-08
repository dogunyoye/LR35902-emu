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

    assert(load_cartridge(cpu, std::move(rom), error));
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
    unsupported_type[0x0147] = 0xFC; // Pocket Camera
    assert(!load_cartridge(cpu, std::move(unsupported_type), error));
    assert(!error.empty());
    assert(cpu_read(cpu, 0x0100) == 0x3E);

    std::vector<uint8_t> wrong_size(0x4000, 0);
    assert(!load_cartridge(cpu, std::move(wrong_size), error));
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

static void test_mbc3_rtc_runs_during_stop()
{
    constexpr int CYCLES_PER_SECOND = 4194304;
    std::string error;
    CPU_LR35902 cpu{};
    assert(load_cartridge(cpu, make_mbc3_rom(0x10, 0x03), error));
    cpu_write(cpu, 0x0000, 0x0A);
    write_rtc(cpu, 0x08, 0);

    cpu.stopped = true;
    for (int cycle = 0; cycle < CYCLES_PER_SECOND; cycle += step_cpu(cpu))
    {
    }
    latch_rtc(cpu);
    assert(read_latched_rtc(cpu, 0x08) == 1);
}

// A cartridge whose first byte of each 16 KiB bank is the bank number.
static std::vector<uint8_t> make_banked_rom(uint8_t cartridge_type, uint8_t rom_size_code, uint8_t ram_size_code)
{
    std::vector<uint8_t> rom(static_cast<std::size_t>(0x8000) << rom_size_code, 0);
    for (std::size_t bank = 0; bank < rom.size() / 0x4000; ++bank)
    {
        rom[bank * 0x4000] = static_cast<uint8_t>(bank);
    }
    rom[0x0147] = cartridge_type;
    rom[0x0148] = rom_size_code;
    rom[0x0149] = ram_size_code;
    return rom;
}

static void test_huc1_banking_ram_and_infrared()
{
    std::string error;
    CPU_LR35902 cpu{};
    assert(load_cartridge(cpu, make_banked_rom(0xFF, 0x05, 0x03), error)); // 1 MiB ROM, 32 KiB RAM
    assert(cpu.cartridge_battery_backed);

    cpu_write(cpu, 0x2000, 0x25);
    assert(cpu_read(cpu, 0x4000) == 0x25);
    cpu_write(cpu, 0x2000, 0x00);
    assert(cpu_read(cpu, 0x4000) == 0x01); // Bank 0 maps to bank 1

    cpu_write(cpu, 0xA000, 0x11); // RAM is usable without enabling it
    cpu_write(cpu, 0x4000, 0x02);
    cpu_write(cpu, 0xA000, 0x22);
    assert(cpu_read(cpu, 0xA000) == 0x22);
    cpu_write(cpu, 0x4000, 0x00);
    assert(cpu_read(cpu, 0xA000) == 0x11);

    cpu_write(cpu, 0x0000, 0x0E); // Infrared port
    assert(cpu_read(cpu, 0xA000) == 0xC0);
    cpu_write(cpu, 0xA000, 0x01);
    cpu_write(cpu, 0x0000, 0x00);
    assert(cpu_read(cpu, 0xA000) == 0x11);
}

// Clocks one bit into the MBC7 EEPROM with chip select high and returns DO.
static bool clock_mbc7_eeprom(CPU_LR35902 &cpu, bool bit)
{
    const uint8_t data = bit ? 0x02 : 0x00;
    cpu_write(cpu, 0xA080, static_cast<uint8_t>(0x80 | data));
    cpu_write(cpu, 0xA080, static_cast<uint8_t>(0xC0 | data));
    return (cpu_read(cpu, 0xA080) & 0x01) != 0;
}

static void send_mbc7_command(CPU_LR35902 &cpu, uint8_t opcode, uint8_t address)
{
    cpu_write(cpu, 0xA080, 0x00);
    cpu_write(cpu, 0xA080, 0x80); // Select the chip
    clock_mbc7_eeprom(cpu, true); // Start bit
    for (int bit = 1; bit >= 0; --bit)
    {
        clock_mbc7_eeprom(cpu, ((opcode >> bit) & 1) != 0);
    }
    for (int bit = 7; bit >= 0; --bit)
    {
        clock_mbc7_eeprom(cpu, ((address >> bit) & 1) != 0);
    }
}

static void test_mbc7_eeprom_and_accelerometer()
{
    std::string error;
    CPU_LR35902 cpu{};
    assert(load_cartridge(cpu, make_banked_rom(0x22, 0x04, 0x00), error)); // 512 KiB ROM
    assert(cpu.cartridge_battery_backed);
    assert(cpu.cartridge_ram.size() == 256);

    cpu_write(cpu, 0x2000, 0x1F);
    assert(cpu_read(cpu, 0x4000) == 0x1F);

    assert(cpu_read(cpu, 0xA020) == 0xFF); // Registers need both enables
    cpu_write(cpu, 0x0000, 0x0A);
    assert(cpu_read(cpu, 0xA020) == 0xFF);
    cpu_write(cpu, 0x4000, 0x40);

    cpu.mbc7_tilt_x = 1;
    cpu.mbc7_tilt_y = -1;
    cpu_write(cpu, 0xA000, 0x55);
    assert(cpu_read(cpu, 0xA020) == 0x00 && cpu_read(cpu, 0xA030) == 0x80);
    cpu_write(cpu, 0xA010, 0xAA);
    assert(((cpu_read(cpu, 0xA030) << 8) | cpu_read(cpu, 0xA020)) == 0x81D0 + 0x70);
    assert(((cpu_read(cpu, 0xA050) << 8) | cpu_read(cpu, 0xA040)) == 0x81D0 - 0x70);

    // Writes are ignored until enabled with EWEN.
    send_mbc7_command(cpu, 0x01, 0x05);
    for (int bit = 15; bit >= 0; --bit)
    {
        clock_mbc7_eeprom(cpu, ((0x1234 >> bit) & 1) != 0);
    }
    assert(cpu.cartridge_ram[10] == 0xFF);

    send_mbc7_command(cpu, 0x00, 0xC0); // EWEN
    send_mbc7_command(cpu, 0x01, 0x05); // WRITE word 5
    for (int bit = 15; bit >= 0; --bit)
    {
        clock_mbc7_eeprom(cpu, ((0xBEEF >> bit) & 1) != 0);
    }
    assert(cpu.cartridge_ram[10] == 0xBE && cpu.cartridge_ram[11] == 0xEF);

    send_mbc7_command(cpu, 0x02, 0x05); // READ word 5
    assert((cpu_read(cpu, 0xA080) & 0x01) == 0); // Dummy zero bit
    uint16_t word = 0;
    for (int bit = 0; bit < 16; ++bit)
    {
        word = static_cast<uint16_t>((word << 1) | (clock_mbc7_eeprom(cpu, false) ? 1 : 0));
    }
    assert(word == 0xBEEF);

    send_mbc7_command(cpu, 0x03, 0x05); // ERASE word 5
    assert(cpu.cartridge_ram[10] == 0xFF && cpu.cartridge_ram[11] == 0xFF);
}

static std::vector<uint8_t> make_titled_rom(const char *title, uint8_t old_licensee)
{
    std::vector<uint8_t> rom(0x8000, 0);
    for (std::size_t index = 0; title[index] != '\0'; ++index)
    {
        rom[0x0134 + index] = static_cast<uint8_t>(title[index]);
    }
    rom[0x014B] = old_licensee;
    return rom;
}

static void test_dmg_compatibility_palettes()
{
    bool wants_logo = true;
    // Nintendo's games are recognised by title checksum.
    assert(dmg_compatibility_combination(make_titled_rom("SUPERMARIOLAND3", 0x01), wants_logo) == 32);
    assert(!wants_logo);
    // POKEMON BLUE shares its checksum; its fourth letter picks the entry.
    assert(dmg_compatibility_combination(make_titled_rom("POKEMON BLUE", 0x01), wants_logo) == 11);
    assert(dmg_compatibility_combination(make_titled_rom("POKXMON BLUE", 0x01), wants_logo) == 0);
    // Other publishers' games get the default.
    assert(dmg_compatibility_combination(make_titled_rom("SUPERMARIOLAND3", 0x08), wants_logo) == 0);
    std::vector<uint8_t> new_licensee = make_titled_rom("SUPERMARIOLAND3", 0x33);
    new_licensee[0x0144] = '0';
    new_licensee[0x0145] = '1';
    assert(dmg_compatibility_combination(new_licensee, wants_logo) == 32);

    std::string error;
    CPU_LR35902 cpu{};
    assert(load_cartridge(cpu, make_titled_rom("SUPERMARIOLAND3", 0x01), error, GameBoyModel::Cgb));
    assert(cpu.cgb_hardware && !cpu.cgb_mode);
    assert(cpu.object_priority_mode == 1);
    // Combination 32: BG palette 8 ($7FFF, $42B5, $3DC8, $0000).
    assert(cpu.bg_palette_ram[2] == 0xB5 && cpu.bg_palette_ram[3] == 0x42);
    assert(load_dmg_compatibility_palettes(cpu, "up")); // Combination 5: palette 0 throughout
    assert(cpu.bg_palette_ram[2] == 0xBF && cpu.bg_palette_ram[3] == 0x32);
    assert(cpu.obj_palette_ram[10] == 0xBF && cpu.obj_palette_ram[11] == 0x32);
    assert(!load_dmg_compatibility_palettes(cpu, "sideways"));
    assert(dmg_compatibility_palette_names().size() == 12);
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
    test_mbc3_rtc_runs_during_stop();
    test_mbc3_rtc_persistence();
    test_huc1_banking_ram_and_infrared();
    test_mbc7_eeprom_and_accelerometer();
    test_dmg_compatibility_palettes();
    return 0;
}
