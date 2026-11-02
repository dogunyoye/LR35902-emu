#include "LR35902.h"

#include <algorithm>
#include <array>
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
    const bool mbc5 = cartridge_type >= 0x19 && cartridge_type <= 0x1E;
    if (!rom_only && !mbc1 && !mbc2 && !mbc5)
    {
        error = "Unsupported cartridge type; supported types are ROM ONLY, MBC1, MBC2, and MBC5.";
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
    else if (mbc1 || mbc5)
    {
        const uint8_t ram_size_code = rom[RAM_SIZE_ADDRESS];
        if (mbc1 && cartridge_type == 0x01)
        {
            if (ram_size_code != 0)
            {
                error = "MBC1 type 0x01 does not support cartridge RAM.";
                return false;
            }
        }
        else if (!ram_size_from_code(ram_size_code, ram_size))
        {
            error = "Unsupported cartridge RAM size code.";
            return false;
        }
    }

    const bool mbc1_multicart = mbc1 && is_mbc1_multicart(rom);
    cpu = CPU_LR35902{};
    cpu.cartridge_rom = std::move(rom);
    cpu.cartridge_mbc1 = mbc1;
    cpu.cartridge_mbc1_multicart = mbc1_multicart;
    cpu.cartridge_mbc2 = mbc2;
    cpu.cartridge_mbc5 = mbc5;
    cpu.cartridge_battery_backed = cartridge_type == 0x03 || cartridge_type == 0x06 ||
                                   cartridge_type == 0x1B || cartridge_type == 0x1E;
    cpu.cartridge_ram.assign(ram_size, 0xFF);
    cpu.af.reg16 = 0x01B0;
    cpu.bc.reg16 = 0x0013;
    cpu.de.reg16 = 0x00D8;
    cpu.hl.reg16 = 0x014D;
    cpu.sp = 0xFFFE;
    cpu.pc = 0x0100;
    cpu.div_cycle_accumulator = 0xABCC;
    cpu.memory[0xFF04] = 0xAB;
    cpu.serial_divider_accumulator = 460; // DMG ABC/MGB post-boot serial clock phase.
    cpu.memory[0xFF00] = 0xCF;
    cpu.memory[0xFF0F] = 0x01;
    cpu.memory[0xFF11] = 0xBF;
    cpu.memory[0xFF12] = 0xF3;
    cpu.memory[0xFF13] = 0xFF;
    cpu.memory[0xFF14] = 0xBF;
    cpu.memory[0xFF16] = 0x3F;
    cpu.memory[0xFF18] = 0xFF;
    cpu.memory[0xFF19] = 0xBF;
    cpu.memory[0xFF1B] = 0xFF;
    cpu.memory[0xFF1D] = 0xFF;
    cpu.memory[0xFF1E] = 0xBF;
    cpu.memory[0xFF20] = 0x3F;
    cpu.memory[0xFF23] = 0x80;
    cpu.memory[0xFF24] = 0x77;
    cpu.memory[0xFF25] = 0xF3;
    cpu.memory[0xFF26] = 0xF1;
    cpu.memory[0xFF2A] = 0xFF;
    cpu.memory[0xFF2B] = 0xFF;
    cpu.memory[0xFF2C] = 0xFF;
    cpu.memory[0xFF2D] = 0xFF;
    cpu.memory[0xFF2E] = 0xFF;
    cpu.memory[0xFF2F] = 0xFF;
    cpu.memory[0xFF46] = 0xFF;
    cpu.memory[0xFF40] = 0x91;
    cpu.memory[0xFF47] = 0xFC;
    cpu.memory[0xFF48] = 0xFF;
    cpu.memory[0xFF49] = 0xFF;
    cpu.memory[0xFF4A] = 0x00;
    cpu.memory[0xFF4B] = 0x00;
    cpu.memory[0xFF50] = 0xFF;
    switch (model)
    {
    case GameBoyModel::Dmg0:
        cpu.af.reg16 = 0x0100;
        cpu.bc.reg16 = 0xFF13;
        cpu.de.reg16 = 0x00C1;
        cpu.hl.reg16 = 0x8403;
        cpu.div_cycle_accumulator = 0x1830;
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
        cpu.memory[0xFF26] = 0xF0;
        cpu.div_cycle_accumulator = global_checksum == 0x96A7 ? 0xD850 : 0xD860;
        cpu.memory[0xFF04] = 0xD8;
        break;
    case GameBoyModel::Sgb2:
        cpu.af.reg16 = 0xFF00;
        cpu.bc.reg16 = 0x0014;
        cpu.de.reg16 = 0x0000;
        cpu.hl.reg16 = 0xC060;
        cpu.memory[0xFF00] = 0xFF;
        cpu.memory[0xFF26] = 0xF0;
        cpu.div_cycle_accumulator = global_checksum == 0x96A7 ? 0xD850 : 0xD860;
        cpu.memory[0xFF04] = 0xD8;
        break;
    case GameBoyModel::DmgABC:
        break;
    }
    error.clear();
    return true;
}

bool load_rom_only_cartridge(CPU_LR35902 &cpu, std::vector<uint8_t> rom, std::string &error,
                             GameBoyModel model)
{
    if (rom.size() != 0x8000 || rom[CARTRIDGE_TYPE_ADDRESS] != 0x00 || rom[ROM_SIZE_ADDRESS] != 0x00)
    {
        error = "ROM-only cartridges must be exactly 32 KiB with header type and size code 0x00.";
        return false;
    }
    return load_cartridge(cpu, std::move(rom), error, model);
}

bool load_cartridge_ram(CPU_LR35902 &cpu, const std::filesystem::path &path, std::string &error)
{
    if (!cpu.cartridge_battery_backed || cpu.cartridge_ram.empty())
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
    if (save_data.size() != cpu.cartridge_ram.size())
    {
        error = "Cartridge save size does not match the RAM size in the ROM header.";
        return false;
    }

    cpu.cartridge_ram = std::move(save_data);
    error.clear();
    return true;
}

bool save_cartridge_ram(const CPU_LR35902 &cpu, const std::filesystem::path &path, std::string &error)
{
    if (!cpu.cartridge_battery_backed || cpu.cartridge_ram.empty())
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

    file.write(reinterpret_cast<const char *>(cpu.cartridge_ram.data()),
               static_cast<std::streamsize>(cpu.cartridge_ram.size()));
    file.close();
    if (!file)
    {
        error = "Unable to write cartridge save: " + path.string();
        return false;
    }

    error.clear();
    return true;
}
