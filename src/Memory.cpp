#include "LR35902.h"

uint8_t cpu_read(CPU_LR35902 &cpu, uint16_t address)
{
    // TODO: Intercept for MBC (Memory Bank Controller) ROM switching
    // TODO: Intercept for VRAM/OAM accessibility checks during PPU drawing modes
    if (address >= 0xE000 && address < 0xFE00)
    {
        address -= 0x2000;
    }
    return cpu.memory[address];
}

void cpu_write(CPU_LR35902 &cpu, uint16_t address, uint8_t value)
{
    // Prevent writes to ROM area (0x0000 - 0x7FFF) unless sending bank commands
    if (address < 0x8000)
    {
        // Handle MBC banking triggers here later
        return;
    }

    // Echo RAM mapping (WRAM mirror from 0xC000-0xDFFF to 0xE000-0xFDFF)
    if (address >= 0xE000 && address < 0xFE00)
    {
        cpu.memory[address - 0x2000] = value;
        address -= 0x2000;
    }

    cpu.memory[address] = value;
}