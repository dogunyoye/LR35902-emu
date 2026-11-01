#include "LR35902.h"

#include <cassert>

static void test_internal_clock_serial_transfer()
{
    CPU_LR35902 cpu{};
    cpu_write(cpu, 0xFF01, 'A');
    cpu_write(cpu, 0xFF02, 0x81); // Start transfer with internal clock

    for (int cycle = 0; cycle < 2048; ++cycle)
    {
        assert(step_cpu(cpu) == 4);
    }

    assert(cpu_read(cpu, 0xFF01) == 0xFF);
    assert((cpu_read(cpu, 0xFF02) & 0x80) == 0);
    assert((cpu.memory[0xFF0F] & 0x08) != 0);
    assert(cpu.serial_output == "A");
}

static void test_external_clock_waits_for_peer()
{
    CPU_LR35902 cpu{};
    cpu_write(cpu, 0xFF01, 'B');
    cpu_write(cpu, 0xFF02, 0x80); // Start with external clock selected

    for (int cycle = 0; cycle < 2048; ++cycle)
    {
        assert(step_cpu(cpu) == 4);
    }

    assert(cpu.serial_output.empty());
    assert((cpu_read(cpu, 0xFF02) & 0x80) != 0);
    assert((cpu.memory[0xFF0F] & 0x08) == 0);
}

int main()
{
    test_internal_clock_serial_transfer();
    test_external_clock_waits_for_peer();
    return 0;
}