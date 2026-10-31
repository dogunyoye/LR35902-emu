#include "LR35902.h"

#include <utility>

bool load_rom_only_cartridge(CPU_LR35902 &cpu, std::vector<uint8_t> rom, std::string &error)
{
    constexpr std::size_t ROM_SIZE = 0x8000;
    constexpr std::size_t CARTRIDGE_TYPE_ADDRESS = 0x0147;
    constexpr std::size_t ROM_SIZE_ADDRESS = 0x0148;

    if (rom.size() != ROM_SIZE)
    {
        error = "ROM-only cartridges must be exactly 32 KiB.";
        return false;
    }

    if (rom[CARTRIDGE_TYPE_ADDRESS] != 0x00)
    {
        error = "Unsupported cartridge type; only ROM ONLY (0x00) is supported.";
        return false;
    }

    if (rom[ROM_SIZE_ADDRESS] != 0x00)
    {
        error = "Unsupported ROM size; only 32 KiB (size code 0x00) is supported.";
        return false;
    }

    cpu = CPU_LR35902{};
    cpu.cartridge_rom = std::move(rom);
    cpu.af.reg16 = 0x01B0;
    cpu.bc.reg16 = 0x0013;
    cpu.de.reg16 = 0x00D8;
    cpu.hl.reg16 = 0x014D;
    cpu.sp = 0xFFFE;
    cpu.pc = 0x0100;
    error.clear();
    return true;
}
