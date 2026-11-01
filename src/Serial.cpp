#include "LR35902.h"

void update_serial(CPU_LR35902 &cpu, int cycles)
{
    if (cycles <= 0)
    {
        return;
    }

    cpu.serial_divider_accumulator = static_cast<uint16_t>(cpu.serial_divider_accumulator + cycles);
    if (!cpu.serial_transfer_active || !cpu.serial_internal_clock)
    {
        return;
    }

    if (cycles < cpu.serial_cycles_remaining)
    {
        cpu.serial_cycles_remaining = static_cast<uint16_t>(cpu.serial_cycles_remaining - cycles);
        return;
    }

    cpu.serial_output.push_back(static_cast<char>(cpu.serial_shift_register));
    cpu.memory[0xFF01] = 0xFF;
    cpu.memory[0xFF02] &= 0x01;
    cpu.memory[0xFF0F] |= 0x08;
    cpu.serial_transfer_active = false;
    cpu.serial_cycles_remaining = 0;
}
