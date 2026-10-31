#include "LR35902.h"

#include <cassert>

static void test_timer_registers()
{
    CPU_LR35902 cpu{};
    cpu.memory[0xFF05] = 0xFF;
    cpu.memory[0xFF06] = 0x10;
    cpu.memory[0xFF07] = 0x04;

    update_timers(cpu, 1024);

    assert(cpu.memory[0xFF05] == 0x10);
    assert(cpu.memory[0xFF0F] == 0x04);
}

static void test_div_write_resets_divider_phase()
{
    CPU_LR35902 cpu{};
    update_timers(cpu, 255);
    assert(cpu.memory[0xFF04] == 0);

    cpu_write(cpu, 0xFF04, 0xAB);
    assert(cpu.memory[0xFF04] == 0);
    assert(cpu.div_cycle_accumulator == 0);

    update_timers(cpu, 255);
    assert(cpu.memory[0xFF04] == 0);
    update_timers(cpu, 1);
    assert(cpu.memory[0xFF04] == 1);
}

int main()
{
    test_timer_registers();
    test_div_write_resets_divider_phase();
    return 0;
}