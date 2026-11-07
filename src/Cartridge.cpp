#include "LR35902.h"

#include <algorithm>
#include <array>
#include <ctime>
#include <fstream>
#include <iterator>
#include <utility>

namespace
{
    constexpr std::size_t ROM_BANK_SIZE = 0x4000;
    constexpr std::size_t CARTRIDGE_TYPE_ADDRESS = 0x0147;
    constexpr std::size_t ROM_SIZE_ADDRESS = 0x0148;
    constexpr std::size_t RAM_SIZE_ADDRESS = 0x0149;
    constexpr std::size_t LOGO_ADDRESS = 0x0104;
    constexpr std::array<uint8_t, 0x30> NINTENDO_LOGO = {
        0xCE, 0xED, 0x66, 0x66, 0xCC, 0x0D, 0x00, 0x0B, 0x03, 0x73, 0x00, 0x83,
        0x00, 0x0C, 0x00, 0x0D, 0x00, 0x08, 0x11, 0x1F, 0x88, 0x89, 0x00, 0x0E,
        0xDC, 0xCC, 0x6E, 0xE6, 0xDD, 0xDD, 0xD9, 0x99, 0xBB, 0xBB, 0x67, 0x63,
        0x6E, 0x0E, 0xEC, 0xCC, 0xDD, 0xDC, 0x99, 0x9F, 0xBB, 0xB9, 0x33, 0x3E,
    };
    constexpr std::size_t MBC2_RAM_SIZE = 0x0200;
    constexpr uint32_t CYCLES_PER_SECOND = 4194304;
    // Clock state appended to the RAM in battery saves, in the layout BGB and
    // VBA-M use: five 32-bit live registers, five latched, a 64-bit Unix time.
    constexpr std::size_t RTC_SAVE_SIZE = 48;
    constexpr std::size_t RTC_SAVE_SIZE_32BIT_TIME = 44;
    constexpr std::array<uint8_t, 5> RTC_MASKS = {0x3F, 0x3F, 0x1F, 0xFF, 0xC1};

    bool has_battery_save(const CPU_LR35902 &cpu)
    {
        return cpu.cartridge_battery_backed && (!cpu.cartridge_ram.empty() || cpu.cartridge_has_rtc);
    }

    void tick_rtc_second(CPU_LR35902 &cpu)
    {
        std::array<uint8_t, 5> &rtc = cpu.rtc_registers;
        // Each counter wraps at its register width, so out-of-range values
        // written by software roll over to zero without carrying.
        rtc[0] = static_cast<uint8_t>((rtc[0] + 1) & 0x3F);
        if (rtc[0] != 60)
        {
            return;
        }
        rtc[0] = 0;
        rtc[1] = static_cast<uint8_t>((rtc[1] + 1) & 0x3F);
        if (rtc[1] != 60)
        {
            return;
        }
        rtc[1] = 0;
        rtc[2] = static_cast<uint8_t>((rtc[2] + 1) & 0x1F);
        if (rtc[2] != 24)
        {
            return;
        }
        rtc[2] = 0;
        const uint16_t day = static_cast<uint16_t>(((rtc[4] & 0x01) << 8 | rtc[3]) + 1);
        rtc[3] = static_cast<uint8_t>(day);
        rtc[4] = static_cast<uint8_t>((rtc[4] & 0xFE) | ((day >> 8) & 0x01));
        if (day > 0x1FF)
        {
            rtc[4] |= 0x80; // Day counter carry
        }
    }

    void put_le(std::vector<uint8_t> &data, uint64_t value, int bytes)
    {
        for (int index = 0; index < bytes; ++index)
        {
            data.push_back(static_cast<uint8_t>(value >> (index * 8)));
        }
    }

    uint64_t get_le(const std::vector<uint8_t> &data, std::size_t offset, int bytes)
    {
        uint64_t value = 0;
        for (int index = 0; index < bytes; ++index)
        {
            value |= static_cast<uint64_t>(data[offset + static_cast<std::size_t>(index)]) << (index * 8);
        }
        return value;
    }

    std::size_t rom_size_from_code(uint8_t code)
    {
        if (code > 8)
        {
            return 0;
        }
        return static_cast<std::size_t>(0x8000u) << code;
    }

    bool ram_size_from_code(uint8_t code, std::size_t &size)
    {
        switch (code)
        {
        case 0:
            size = 0;
            return true;
        case 1:
            size = 0x0800;
            return true;
        case 2:
            size = 0x2000;
            return true;
        case 3:
            size = 0x8000;
            return true;
        default:
            return false;
        }
    }

    // The DMG and MGB boot ROMs leave the Nintendo logo from the cartridge
    // header and a registered-trademark tile in VRAM, and the logo in the
    // background map. Each logo bit is doubled horizontally and vertically.
    void load_boot_logo(CPU_LR35902 &cpu)
    {
        constexpr std::array<uint8_t, 8> TRADEMARK_TILE = {0x3C, 0x42, 0xB9, 0xA5, 0xB9, 0xA5, 0x42, 0x3C};
        std::size_t address = 0x8010;
        for (std::size_t index = 0; index < 0x30; ++index)
        {
            const uint8_t logo_byte = cpu.cartridge_rom[LOGO_ADDRESS + index];
            for (const int shift : {4, 0})
            {
                uint8_t doubled = 0;
                for (int bit = 3; bit >= 0; --bit)
                {
                    const uint8_t pixel = (logo_byte >> (shift + bit)) & 0x01;
                    doubled = static_cast<uint8_t>((doubled << 2) | (pixel * 0x03));
                }
                cpu.memory[address] = doubled;
                cpu.memory[address + 2] = doubled;
                address += 4;
            }
        }
        for (const uint8_t row : TRADEMARK_TILE)
        {
            cpu.memory[address] = row;
            address += 2;
        }

        cpu.memory[0x9910] = 0x19;
        for (uint8_t tile = 1; tile <= 0x0C; ++tile)
        {
            cpu.memory[0x9903 + tile] = tile;
            cpu.memory[0x9923 + tile] = static_cast<uint8_t>(tile + 0x0C);
        }
    }

    // MBC1 multicarts are wired differently but share the normal MBC1 header.
    // Like other emulators, detect them as 8 Mbit ROMs whose second 2 Mbit
    // game also starts with a cartridge header logo.
    bool is_mbc1_multicart(const std::vector<uint8_t> &rom)
    {
        constexpr std::size_t SECOND_GAME_LOGO = 0x10 * ROM_BANK_SIZE + LOGO_ADDRESS;
        return rom.size() == 0x100000 &&
               std::equal(NINTENDO_LOGO.begin(), NINTENDO_LOGO.end(), rom.begin() + SECOND_GAME_LOGO);
    }
}

bool load_cartridge(CPU_LR35902 &cpu, std::vector<uint8_t> rom, std::string &error,
                    GameBoyModel model)
{
    if (rom.size() < 0x150)
    {
        error = "ROM image is too small to contain a cartridge header.";
        return false;
    }

    const uint8_t cartridge_type = rom[CARTRIDGE_TYPE_ADDRESS];
    const uint16_t global_checksum = static_cast<uint16_t>(
        (static_cast<uint16_t>(rom[0x014E]) << 8) | rom[0x014F]);
    const bool rom_only = cartridge_type == 0x00;
    const bool mbc1 = cartridge_type >= 0x01 && cartridge_type <= 0x03;
    const bool mbc2 = cartridge_type == 0x05 || cartridge_type == 0x06;
    const bool mbc3 = cartridge_type >= 0x0F && cartridge_type <= 0x13;
    const bool mbc5 = cartridge_type >= 0x19 && cartridge_type <= 0x1E;
    if (!rom_only && !mbc1 && !mbc2 && !mbc3 && !mbc5)
    {
        error = "Unsupported cartridge type; supported types are ROM ONLY, MBC1, MBC2, MBC3, and MBC5.";
        return false;
    }

    const uint8_t rom_size_code = rom[ROM_SIZE_ADDRESS];
    const std::size_t expected_rom_size = rom_size_from_code(rom_size_code);
    if (expected_rom_size == 0)
    {
        error = "Unsupported ROM size code.";
        return false;
    }
    if (rom.size() != expected_rom_size)
    {
        error = "ROM file size does not match the cartridge header.";
        return false;
    }

    if (rom_only && rom_size_code != 0)
    {
        error = "ROM ONLY cartridges must be exactly 32 KiB.";
        return false;
    }

    if (mbc2 && rom_size_code > 3)
    {
        error = "MBC2 cartridges support at most 256 KiB of ROM.";
        return false;
    }

    std::size_t ram_size = 0;
    if (mbc2)
    {
        ram_size = MBC2_RAM_SIZE; // Built-in 512x4-bit RAM; the header RAM size is unused.
    }
    else if (mbc1 || mbc3 || mbc5)
    {
        const uint8_t ram_size_code = rom[RAM_SIZE_ADDRESS];
        if ((mbc1 && cartridge_type == 0x01) || cartridge_type == 0x0F || cartridge_type == 0x11)
        {
            if (ram_size_code != 0)
            {
                error = "This cartridge type does not support cartridge RAM.";
                return false;
            }
        }
        else if (!ram_size_from_code(ram_size_code, ram_size))
        {
            error = "Unsupported cartridge RAM size code.";
            return false;
        }
        else if (ram_size == 0 && cartridge_type != 0x19 && cartridge_type != 0x1C)
        {
            // Some homebrew and test ROMs declare a RAM cartridge type with no
            // RAM size; give them a single 8 KiB bank as other emulators do.
            ram_size = 0x2000;
        }
    }

    const bool mbc1_multicart = mbc1 && is_mbc1_multicart(rom);
    cpu = CPU_LR35902{};
    cpu.cartridge_rom = std::move(rom);
    cpu.cartridge_mbc1 = mbc1;
    cpu.cartridge_mbc1_multicart = mbc1_multicart;
    cpu.cartridge_mbc2 = mbc2;
    cpu.cartridge_mbc3 = mbc3;
    cpu.cartridge_has_rtc = cartridge_type == 0x0F || cartridge_type == 0x10;
    cpu.cartridge_mbc5 = mbc5;
    cpu.cartridge_battery_backed = cartridge_type == 0x03 || cartridge_type == 0x06 || cartridge_type == 0x0F ||
                                   cartridge_type == 0x10 || cartridge_type == 0x13 ||
                                   cartridge_type == 0x1B || cartridge_type == 0x1E;
    cpu.cartridge_ram.assign(ram_size, 0xFF);
    cpu.af.reg16 = 0x01B0;
    cpu.bc.reg16 = 0x0013;
    cpu.de.reg16 = 0x00D8;
    cpu.hl.reg16 = 0x014D;
    cpu.sp = 0xFFFE;
    cpu.pc = 0x0100;
    cpu.div_cycle_accumulator = 0xABC8;
    cpu.memory[0xFF04] = 0xAB;
    cpu.serial_divider_accumulator = 460; // DMG ABC/MGB post-boot serial clock phase.
    cpu.memory[0xFF00] = 0xCF;
    cpu.memory[0xFF0F] = 0x01;
    cpu.memory[0xFF46] = 0xFF;
    cpu.memory[0xFF40] = 0x91;
    cpu.memory[0xFF47] = 0xFC;
    cpu.memory[0xFF48] = 0xFF;
    cpu.memory[0xFF49] = 0xFF;
    cpu.memory[0xFF4A] = 0x00;
    cpu.memory[0xFF4B] = 0x00;
    cpu.memory[0xFF50] = 0xFF;
    const bool sgb = model == GameBoyModel::Sgb || model == GameBoyModel::Sgb2;
    apu_power_on_after_boot(cpu.apu, !sgb); // The SGB boot ROM plays no chime.
    if (!sgb)
    {
        load_boot_logo(cpu);
    }
    switch (model)
    {
    case GameBoyModel::Dmg0:
        cpu.af.reg16 = 0x0100;
        cpu.bc.reg16 = 0xFF13;
        cpu.de.reg16 = 0x00C1;
        cpu.hl.reg16 = 0x8403;
        cpu.div_cycle_accumulator = 0x182C;
        cpu.memory[0xFF04] = 0x18;
        break;
    case GameBoyModel::Mgb:
        cpu.af.reg16 = 0xFFB0;
        break;
    case GameBoyModel::Sgb:
        cpu.af.reg16 = 0x0100;
        cpu.bc.reg16 = 0x0014;
        cpu.de.reg16 = 0x0000;
        cpu.hl.reg16 = 0xC060;
        cpu.memory[0xFF00] = 0xFF;
        cpu.div_cycle_accumulator = global_checksum == 0x96A7 ? 0xD84C : 0xD85C;
        cpu.memory[0xFF04] = 0xD8;
        break;
    case GameBoyModel::Sgb2:
        cpu.af.reg16 = 0xFF00;
        cpu.bc.reg16 = 0x0014;
        cpu.de.reg16 = 0x0000;
        cpu.hl.reg16 = 0xC060;
        cpu.memory[0xFF00] = 0xFF;
        cpu.div_cycle_accumulator = global_checksum == 0x96A7 ? 0xD84C : 0xD85C;
        cpu.memory[0xFF04] = 0xD8;
        break;
    case GameBoyModel::DmgABC:
        break;
    }
    error.clear();
    return true;
}

void update_cartridge_rtc(CPU_LR35902 &cpu, int cycles)
{
    if (!cpu.cartridge_has_rtc || (cpu.rtc_registers[4] & 0x40) != 0)
    {
        return;
    }
    cpu.rtc_cycles += static_cast<uint32_t>(cycles);
    while (cpu.rtc_cycles >= CYCLES_PER_SECOND)
    {
        cpu.rtc_cycles -= CYCLES_PER_SECOND;
        tick_rtc_second(cpu);
    }
}

void advance_cartridge_rtc_seconds(CPU_LR35902 &cpu, uint64_t seconds)
{
    if (!cpu.cartridge_has_rtc || (cpu.rtc_registers[4] & 0x40) != 0)
    {
        return;
    }
    // Whole days are added directly so long gaps between sessions stay cheap.
    const uint64_t days = seconds / 86400;
    for (uint64_t second = 0; second < seconds % 86400; ++second)
    {
        tick_rtc_second(cpu);
    }
    const uint64_t day = ((cpu.rtc_registers[4] & 0x01) << 8 | cpu.rtc_registers[3]) + days;
    cpu.rtc_registers[3] = static_cast<uint8_t>(day);
    cpu.rtc_registers[4] = static_cast<uint8_t>((cpu.rtc_registers[4] & 0xFE) | ((day >> 8) & 0x01));
    if (day > 0x1FF)
    {
        cpu.rtc_registers[4] |= 0x80;
    }
}

uint8_t read_cartridge_rtc(const CPU_LR35902 &cpu)
{
    const std::size_t index = static_cast<std::size_t>(cpu.mbc3_ram_select - 0x08);
    return static_cast<uint8_t>(cpu.rtc_latched[index] & RTC_MASKS[index]);
}

void write_cartridge_rtc(CPU_LR35902 &cpu, uint8_t value)
{
    const std::size_t index = static_cast<std::size_t>(cpu.mbc3_ram_select - 0x08);
    cpu.rtc_registers[index] = static_cast<uint8_t>(value & RTC_MASKS[index]);
    cpu.rtc_latched[index] = cpu.rtc_registers[index];
    if (index == 0)
    {
        cpu.rtc_cycles = 0; // Writing the seconds restarts the current second.
    }
}

bool load_cartridge_ram(CPU_LR35902 &cpu, const std::filesystem::path &path, std::string &error)
{
    if (!has_battery_save(cpu))
    {
        error.clear();
        return true;
    }

    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        std::error_code filesystem_error;
        if (!std::filesystem::exists(path, filesystem_error) && !filesystem_error)
        {
            error.clear();
            return true;
        }
        error = "Unable to open cartridge save: " + path.string();
        return false;
    }

    std::vector<uint8_t> save_data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    const std::size_t ram_size = cpu.cartridge_ram.size();
    const std::size_t rtc_size = save_data.size() - std::min(save_data.size(), ram_size);
    const bool valid_rtc_size = cpu.cartridge_has_rtc &&
                                (rtc_size == 0 || rtc_size == RTC_SAVE_SIZE || rtc_size == RTC_SAVE_SIZE_32BIT_TIME);
    if (save_data.size() < ram_size || (rtc_size != 0 && !valid_rtc_size))
    {
        error = "Cartridge save size does not match the RAM size in the ROM header.";
        return false;
    }

    std::copy(save_data.begin(), save_data.begin() + static_cast<std::ptrdiff_t>(ram_size), cpu.cartridge_ram.begin());
    if (rtc_size != 0)
    {
        for (std::size_t index = 0; index < 5; ++index)
        {
            cpu.rtc_registers[index] = static_cast<uint8_t>(get_le(save_data, ram_size + index * 4, 4) & RTC_MASKS[index]);
            cpu.rtc_latched[index] = static_cast<uint8_t>(get_le(save_data, ram_size + 20 + index * 4, 4) & RTC_MASKS[index]);
        }
        const int time_bytes = rtc_size == RTC_SAVE_SIZE ? 8 : 4;
        const uint64_t saved_time = get_le(save_data, ram_size + 40, time_bytes);
        const uint64_t now = static_cast<uint64_t>(std::time(nullptr));
        if (now > saved_time)
        {
            advance_cartridge_rtc_seconds(cpu, now - saved_time); // Time passed while the game was off.
        }
    }
    error.clear();
    return true;
}

bool save_cartridge_ram(const CPU_LR35902 &cpu, const std::filesystem::path &path, std::string &error)
{
    if (!has_battery_save(cpu))
    {
        error.clear();
        return true;
    }

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file)
    {
        error = "Unable to create cartridge save: " + path.string();
        return false;
    }

    std::vector<uint8_t> save_data = cpu.cartridge_ram;
    if (cpu.cartridge_has_rtc)
    {
        for (const uint8_t value : cpu.rtc_registers)
        {
            put_le(save_data, value, 4);
        }
        for (const uint8_t value : cpu.rtc_latched)
        {
            put_le(save_data, value, 4);
        }
        put_le(save_data, static_cast<uint64_t>(std::time(nullptr)), 8);
    }
    file.write(reinterpret_cast<const char *>(save_data.data()), static_cast<std::streamsize>(save_data.size()));
    file.close();
    if (!file)
    {
        error = "Unable to write cartridge save: " + path.string();
        return false;
    }

    error.clear();
    return true;
}
