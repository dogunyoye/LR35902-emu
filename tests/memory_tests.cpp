#include "LR35902.h"

#include <cassert>

static void test_unusable_oam_region()
{
    CPU_LR35902 cpu{};
    cpu.memory[0xFE9F] = 0x55;
    cpu.memory[0xFEA0] = 0xAA;
    cpu.memory[0xFEFF] = 0xBB;

    assert(cpu_read(cpu, 0xFE9F) == 0x55);
    assert(cpu_read(cpu, 0xFEA0) == 0xFF);
    assert(cpu_read(cpu, 0xFEFF) == 0xFF);

    cpu_write(cpu, 0xFEA0, 0x11);
    cpu_write(cpu, 0xFEFF, 0x22);
    assert(cpu.memory[0xFEA0] == 0xAA);
    assert(cpu.memory[0xFEFF] == 0xBB);
}

int main()
{
    test_unusable_oam_region();
    return 0;
}