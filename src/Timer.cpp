#include "LR35902.h"

#include <algorithm>

namespace
{
    constexpr uint16_t DIV = 0xFF04;
    constexpr uint16_t TIMA = 0xFF05;
    constexpr uint16_t TMA = 0xFF06;
    constexpr uint16_t TAC = 0xFF07;
    constexpr uint16_t IF = 0xFF0F;
    // The APU frame sequencer steps when bit 4 of DIV (bit 12 of the internal
    // counter) falls, at 512 Hz.
    constexpr uint16_t FRAME_SEQUENCER_BIT = 0x1000;

    uint8_t timer_divider_bit(uint8_t tac)
    {
        switch (tac & 0x03)
        {
        case 0:
            return 9;
        case 1:
            return 3;
        case 2:
            return 5;
        default:
            return 7;
        }
    }

    bool timer_input(const CPU_LR35902 &cpu)
    {
        const uint8_t tac = cpu.memory[TAC];
        if ((tac & 0x04) == 0)
        {
            return false;
        }
        return (cpu.div_cycle_accumulator & (1u << timer_divider_bit(tac))) != 0;
    }

    void increment_tima(CPU_LR35902 &cpu)
    {
        if (cpu.tima_reload_delay != 0)
        {
            return;
        }

        if (cpu.memory[TIMA] == 0xFF)
        {
            cpu.memory[TIMA] = 0;
            cpu.tima_reload_delay = 4;
            return;
        }

        ++cpu.memory[TIMA];
    }

    void apply_timer_falling_edge(CPU_LR35902 &cpu, bool was_high)
    {
        if (was_high && !timer_input(cpu))
        {
            increment_tima(cpu);
        }
    }
}

void reset_divider(CPU_LR35902 &cpu)
{
    if ((cpu.div_cycle_accumulator & FRAME_SEQUENCER_BIT) != 0)
    {
        apu_frame_sequencer_step(cpu.apu); // Resetting DIV can make bit 4 fall early.
    }
    const bool old_timer_input = timer_input(cpu);
    cpu.div_cycle_accumulator = 0;
    cpu.memory[DIV] = 0;
    apply_timer_falling_edge(cpu, old_timer_input);
}

void write_timer_control(CPU_LR35902 &cpu, uint8_t value)
{
    const bool old_timer_input = timer_input(cpu);
    cpu.memory[TAC] = value & 0x07;
    apply_timer_falling_edge(cpu, old_timer_input);
}

void update_timers(CPU_LR35902 &cpu, int cycles)
{
    if (cycles <= 0)
    {
        return;
    }

    for (int cycle = 0; cycle < cycles; ++cycle)
    {
        cpu.tima_reloaded_this_cycle = false;
        const bool reload_was_pending = cpu.tima_reload_delay != 0;
        const bool old_timer_input = timer_input(cpu);
        const uint16_t old_divider = cpu.div_cycle_accumulator;
        cpu.div_cycle_accumulator = static_cast<uint16_t>(cpu.div_cycle_accumulator + 1);
        if ((old_divider & ~cpu.div_cycle_accumulator & FRAME_SEQUENCER_BIT) != 0)
        {
            apu_frame_sequencer_step(cpu.apu);
        }
        cpu.memory[DIV] = static_cast<uint8_t>(cpu.div_cycle_accumulator >> 8);
        apply_timer_falling_edge(cpu, old_timer_input);

        if (reload_was_pending && cpu.tima_reload_delay > 0)
        {
            --cpu.tima_reload_delay;
            if (cpu.tima_reload_delay == 0)
            {
                cpu.memory[TIMA] = cpu.memory[TMA];
                cpu.memory[IF] |= 0x04;
                cpu.tima_reloaded_this_cycle = true;
            }
        }
    }
}
