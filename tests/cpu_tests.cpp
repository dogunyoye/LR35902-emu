#include "LR35902.h"

#include <cassert>

static void test_load_and_add()
{
    CPU_LR35902 cpu{};
    cpu.memory[0x0000] = 0x3E; // LD A, d8
    cpu.memory[0x0001] = 0x42;
    cpu.memory[0x0002] = 0x06; // LD B, d8
    cpu.memory[0x0003] = 0x05;
    cpu.memory[0x0004] = 0x80; // ADD A, B
    cpu.memory[0x0005] = 0x47; // LD B, A
    cpu.pc = 0x0000;

    int cycles = execute_instruction(cpu);
    assert(cycles == 8);
    assert(cpu.af.reg8.high == 0x42);
    assert(cpu.pc == 0x0002);

    cycles = execute_instruction(cpu);
    assert(cycles == 8);
    assert(cpu.bc.reg8.high == 0x05);
    assert(cpu.pc == 0x0004);

    cycles = execute_instruction(cpu);
    assert(cycles == 4);
    assert(cpu.af.reg8.high == 0x47);
    assert(cpu.pc == 0x0005);

    cycles = execute_instruction(cpu);
    assert(cycles == 4);
    assert(cpu.bc.reg8.high == 0x47);
}

static void test_stack_and_loads()
{
    CPU_LR35902 cpu{};
    cpu.memory[0x0000] = 0x01; // LD BC, d16
    cpu.memory[0x0001] = 0x34;
    cpu.memory[0x0002] = 0x12;
    cpu.memory[0x0003] = 0x21; // LD HL, d16
    cpu.memory[0x0004] = 0x00;
    cpu.memory[0x0005] = 0x10;
    cpu.memory[0x0006] = 0xC5; // PUSH BC
    cpu.pc = 0x0000;

    execute_instruction(cpu);
    assert(cpu.bc.reg16 == 0x1234);
    assert(cpu.pc == 0x0003);

    execute_instruction(cpu);
    assert(cpu.hl.reg16 == 0x1000);
    assert(cpu.pc == 0x0006);

    execute_instruction(cpu);
    assert(cpu.sp == 0xFFFE);
    assert(cpu.memory[0xFFFE] == 0x34);
    assert(cpu.memory[0xFFFF] == 0x12);
}

static void test_inc_dec_registers()
{
    CPU_LR35902 cpu{};
    cpu.memory[0x0000] = 0x04; // INC B
    cpu.memory[0x0001] = 0x05; // DEC B
    cpu.memory[0x0002] = 0x00; // NOP
    cpu.pc = 0x0000;

    execute_instruction(cpu);
    assert(cpu.bc.reg8.high == 0x01);
    assert(cpu.pc == 0x0001);

    execute_instruction(cpu);
    assert(cpu.bc.reg8.high == 0x00);
    assert(cpu.pc == 0x0002);

    execute_instruction(cpu);
    assert(cpu.pc == 0x0003);
}

static void test_add_hl_registers()
{
    CPU_LR35902 cpu{};
    cpu.memory[0x0000] = 0x21; // LD HL, d16
    cpu.memory[0x0001] = 0x00;
    cpu.memory[0x0002] = 0x10;
    cpu.memory[0x0003] = 0x01; // LD BC, d16
    cpu.memory[0x0004] = 0x34;
    cpu.memory[0x0005] = 0x12;
    cpu.memory[0x0006] = 0x09; // ADD HL, BC
    cpu.memory[0x0007] = 0x31; // LD SP, d16
    cpu.memory[0x0008] = 0x05;
    cpu.memory[0x0009] = 0x00;
    cpu.memory[0x000A] = 0x39; // ADD HL, SP
    cpu.pc = 0x0000;

    execute_instruction(cpu);
    assert(cpu.hl.reg16 == 0x1000);

    execute_instruction(cpu);
    assert(cpu.bc.reg16 == 0x1234);

    execute_instruction(cpu);
    assert(cpu.hl.reg16 == 0x2234);

    execute_instruction(cpu);
    assert(cpu.sp == 0x0005);

    execute_instruction(cpu);
    assert(cpu.hl.reg16 == 0x2239);
}

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

int main()
{
    test_load_and_add();
    test_stack_and_loads();
    test_inc_dec_registers();
    test_add_hl_registers();
    test_timer_registers();
    return 0;
}
