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

int main()
{
    test_rom_only_cartridge();
    return 0;
}