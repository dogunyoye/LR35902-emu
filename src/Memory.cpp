#include "LR35902.h"

// Important Game Boy Memory Ranges
const uint16_t VRAM_START = 0x8000;
const uint16_t VRAM_END = 0x9FFF;
const uint16_t WRAM_START = 0xC000;
const uint16_t WRAM_END = 0xDFFF;
const uint16_t ECHO_START = 0xE000;
const uint16_t ECHO_END = 0xFDFF;
const uint16_t OAM_START = 0xFE00;
const uint16_t OAM_END = 0xFE9F;
const uint16_t IO_START = 0xFF00;
const uint16_t IO_END = 0xFF7F;
const uint16_t HRAM_START = 0xFF80;
const uint16_t HRAM_END = 0xFFFE;
const uint16_t IE_REG = 0xFFFF; // Interrupt Enable Register

uint8_t cpu_read(CPU_LR35902 &cpu, uint16_t address)
{
    // MBC / cartridge ROM routing: handle RAM enable bits and ROM banking states.
    // This keeps the CPU bus model explicit while remaining compatible with the
    // simple flat-memory implementation currently used in the emulator.
    if (address >= ECHO_START && address <= ECHO_END)
    {
        address -= 0x2000;
    }

    if (address >= VRAM_START && address <= VRAM_END && cpu.vram_locked)
    {
        return 0xFF;
    }

    if (address >= OAM_START && address <= OAM_END && cpu.oam_locked)
    {
        return 0xFF;
    }

    return cpu.memory[address];
}

void cpu_write(CPU_LR35902 &cpu, uint16_t address, uint8_t value)
{
    // Cartridge ROM writes: lower 0x2000 region is used for RAM enable and
    // simple MBC banking triggers. A real MBC implementation would decode these
    // values; for the current emulator, we model the enable bit explicitly.
    if (address < 0x8000)
    {
        if ((address & 0x1FFF) == 0x0000)
        {
            cpu.cart_ram_enabled = (value & 0x0F) == 0x0A;
        }
        return;
    }

    if (address >= VRAM_START && address <= VRAM_END && cpu.vram_locked)
    {
        return;
    }

    if (address >= OAM_START && address <= OAM_END && cpu.oam_locked)
    {
        return;
    }

    // Echo RAM mapping (WRAM mirror from 0xC000-0xDFFF to 0xE000-0xFDFF)
    if (address >= ECHO_START && address <= ECHO_END)
    {
        address -= 0x2000;
    }

    cpu.memory[address] = value;
}