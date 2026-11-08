#include "LR35902.h"

#include <cassert>

static void test_timer_registers()
{
    CPU_LR35902 cpu{};
    cpu.memory[0xFF05] = 0xFF;
    cpu.memory[0xFF06] = 0x10;
    cpu.memory[0xFF07] = 0x04;

    update_timers(cpu, 1024);

    assert(cpu.memory[0xFF05] == 0x00);
    assert(cpu.memory[0xFF0F] == 0x00);

    update_timers(cpu, 4);
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

static void test_div_write_resets_tima_phase()
{
    CPU_LR35902 cpu{};
    cpu.memory[0xFF07] = 0x05; // Enable TIMA at 262144 Hz (16 T-cycles)

    update_timers(cpu, 8);
    assert(cpu.memory[0xFF05] == 0);
    assert(cpu.div_cycle_accumulator == 8);

    cpu_write(cpu, 0xFF04, 0x00);
    assert(cpu.div_cycle_accumulator == 0);
    assert(cpu.memory[0xFF05] == 1); // Resetting DIV creates a falling edge
    update_timers(cpu, 8);
    assert(cpu.memory[0xFF05] == 1);
    update_timers(cpu, 8);
    assert(cpu.memory[0xFF05] == 2);
}

static void test_tima_overflow_reload_delay()
{
    CPU_LR35902 cpu{};
    cpu.memory[0xFF05] = 0xFF;
    cpu.memory[0xFF06] = 0x42;
    cpu.memory[0xFF07] = 0x05; // Enable TIMA at 262144 Hz (16 T-cycles)

    update_timers(cpu, 16);
    assert(cpu.memory[0xFF05] == 0x00);
    assert((cpu.memory[0xFF0F] & 0x04) == 0);

    update_timers(cpu, 3);
    assert(cpu.memory[0xFF05] == 0x00);
    assert((cpu.memory[0xFF0F] & 0x04) == 0);

    update_timers(cpu, 1);
    assert(cpu.memory[0xFF05] == 0x42);
    assert((cpu.memory[0xFF0F] & 0x04) != 0);
}

static void test_tima_write_cancels_pending_reload()
{
    CPU_LR35902 cpu{};
    cpu.memory[0xFF05] = 0xFF;
    cpu.memory[0xFF06] = 0x42;
    cpu.memory[0xFF07] = 0x05;
    update_timers(cpu, 16);
    assert(cpu.memory[0xFF05] == 0x00);

    cpu_write(cpu, 0xFF05, 0x77);
    update_timers(cpu, 4);
    assert(cpu.memory[0xFF05] == 0x77);
    assert((cpu.memory[0xFF0F] & 0x04) == 0);
}

static void test_tima_write_is_ignored_on_reload_cycle()
{
    CPU_LR35902 cpu{};
    cpu.memory[0xFF05] = 0xFF;
    cpu.memory[0xFF06] = 0x42;
    cpu.memory[0xFF07] = 0x05;
    update_timers(cpu, 16);
    update_timers(cpu, 3);

    cpu_write(cpu, 0xFF05, 0x77);
    update_timers(cpu, 1);
    assert(cpu.memory[0xFF05] == 0x42);
    assert((cpu.memory[0xFF0F] & 0x04) != 0);
}

static void test_tma_write_is_used_by_pending_reload()
{
    CPU_LR35902 cpu{};
    cpu.memory[0xFF05] = 0xFF;
    cpu.memory[0xFF06] = 0x42;
    cpu.memory[0xFF07] = 0x05;
    update_timers(cpu, 16);
    update_timers(cpu, 3);

    cpu_write(cpu, 0xFF06, 0x55);
    update_timers(cpu, 1);
    assert(cpu.memory[0xFF05] == 0x55);
    assert((cpu.memory[0xFF0F] & 0x04) != 0);
}

static void test_reload_cycle_register_writes()
{
    CPU_LR35902 tima_cpu{};
    tima_cpu.memory[0xFF05] = 0xFF;
    tima_cpu.memory[0xFF06] = 0x42;
    tima_cpu.memory[0xFF07] = 0x05;
    update_timers(tima_cpu, 20);
    assert(tima_cpu.memory[0xFF05] == 0x42);
    cpu_write(tima_cpu, 0xFF05, 0x77);
    assert(tima_cpu.memory[0xFF05] == 0x42);

    CPU_LR35902 tma_cpu{};
    tma_cpu.memory[0xFF05] = 0xFF;
    tma_cpu.memory[0xFF06] = 0x42;
    tma_cpu.memory[0xFF07] = 0x05;
    update_timers(tma_cpu, 20);
    assert(tma_cpu.memory[0xFF05] == 0x42);
    cpu_write(tma_cpu, 0xFF06, 0x55);
    assert(tma_cpu.memory[0xFF06] == 0x55);
    assert(tma_cpu.memory[0xFF05] == 0x55);
}

static void test_stop_resets_divider()
{
    CPU_LR35902 cpu{};
    cpu.memory[0x0000] = 0x10; // STOP
    cpu.pc = 0x0000;
    update_timers(cpu, 0x1234);
    assert(cpu.memory[0xFF04] != 0);
    execute_instruction(cpu);
    assert(cpu.stopped);
    assert(cpu.memory[0xFF04] == 0 && cpu.div_cycle_accumulator == 0);
}

int main()
{
    test_timer_registers();
    test_div_write_resets_divider_phase();
    test_div_write_resets_tima_phase();
    test_tima_overflow_reload_delay();
    test_tima_write_cancels_pending_reload();
    test_tima_write_is_ignored_on_reload_cycle();
    test_tma_write_is_used_by_pending_reload();
    test_reload_cycle_register_writes();
    test_stop_resets_divider();
    return 0;
}