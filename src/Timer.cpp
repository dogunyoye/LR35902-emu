#include "LR35902.h"

void update_timers(CPU_LR35902 &cpu, int cycles)
{
    // 1. Update DIV Register (Increments every 256 T-cycles)
    cpu.div_cycle_accumulator += cycles;
    while (cpu.div_cycle_accumulator >= 256)
    {
        cpu.div_cycle_accumulator -= 256;
        cpu.memory[0xFF04]++; // Increment DIV (rolls over automatically from 0xFF to 0)
    }

    // 2. Check if Timer (TIMA) is enabled via TAC (bit 2 of 0xFF07)
    uint8_t tac = cpu.memory[0xFF07];
    bool timer_enabled = (tac & 0x04) != 0;

    if (timer_enabled)
    {
        cpu.tima_cycle_accumulator += cycles;

        // Determine threshold based on TAC frequency bits (bits 0-1)
        // 00: 4096 Hz   (every 1024 cycles)
        // 01: 262144 Hz (every 16 cycles)
        // 10: 65536 Hz  (every 64 cycles)
        // 11: 16384 Hz  (every 256 cycles)
        int threshold = 1024;
        switch (tac & 0x03)
        {
        case 0:
            threshold = 1024;
            break;
        case 1:
            threshold = 16;
            break;
        case 2:
            threshold = 64;
            break;
        case 3:
            threshold = 256;
            break;
        }

        while (cpu.tima_cycle_accumulator >= threshold)
        {
            cpu.tima_cycle_accumulator -= threshold;

            // If TIMA overflows (> 0xFF), reload from TMA and request interrupt
            if (cpu.memory[0xFF05] == 0xFF)
            {
                cpu.memory[0xFF05] = cpu.memory[0xFF06]; // Load TMA

                // Request Timer Interrupt (Bit 2 of IF register 0xFF0F)
                cpu.memory[0xFF0F] |= 0x04;
            }
            else
            {
                cpu.memory[0xFF05]++;
            }
        }
    }
}