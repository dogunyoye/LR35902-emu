#include "LR35902.h"

#include <cassert>
#include <utility>

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

static void test_flag_behavior_add_sub()
{
    CPU_LR35902 cpu{};
    cpu.memory[0x0000] = 0x3E; // LD A, d8
    cpu.memory[0x0001] = 0x0F;
    cpu.memory[0x0002] = 0x06; // LD B, d8
    cpu.memory[0x0003] = 0x01;
    cpu.memory[0x0004] = 0x80; // ADD A, B
    cpu.memory[0x0005] = 0xD6; // SUB A, d8
    cpu.memory[0x0006] = 0x01;
    cpu.pc = 0x0000;

    execute_instruction(cpu); // LD A, 0x0F
    assert(cpu.af.reg8.high == 0x0F);

    execute_instruction(cpu); // LD B, 0x01
    assert(cpu.bc.reg8.high == 0x01);

    execute_instruction(cpu); // ADD A, B => 0x10
    assert(cpu.af.reg8.high == 0x10);
    assert(get_flag(cpu, FLAG_Z) == false);
    assert(get_flag(cpu, FLAG_N) == false);
    assert(get_flag(cpu, FLAG_H) == true);
    assert(get_flag(cpu, FLAG_C) == false);

    execute_instruction(cpu); // SUB A, 0x01 => 0x0F
    assert(cpu.af.reg8.high == 0x0F);
    assert(get_flag(cpu, FLAG_Z) == false);
    assert(get_flag(cpu, FLAG_N) == true);
    assert(get_flag(cpu, FLAG_H) == true);
    assert(get_flag(cpu, FLAG_C) == false);
}

static void test_cb_prefixed_bit_and_rotate()
{
    CPU_LR35902 cpu{};
    cpu.bc.reg8.high = 0x80; // B = 0x80
    cpu.af.reg8.high = 0x80; // A = 0x80
    cpu.memory[0x0000] = 0xCB;
    cpu.memory[0x0001] = 0x00; // RLC B
    cpu.memory[0x0002] = 0xCB;
    cpu.memory[0x0003] = 0x7F; // BIT 7, A
    cpu.pc = 0x0000;

    execute_instruction(cpu);
    assert(cpu.bc.reg8.high == 0x01);
    assert(get_flag(cpu, FLAG_C) == true);
    assert(get_flag(cpu, FLAG_Z) == false);

    execute_instruction(cpu);
    assert(get_flag(cpu, FLAG_Z) == false);
    assert(get_flag(cpu, FLAG_N) == false);
    assert(get_flag(cpu, FLAG_H) == true);
    assert(get_flag(cpu, FLAG_C) == true);
}

static void test_instruction_cycle_counts()
{
    CPU_LR35902 cpu{};
    cpu.hl.reg16 = 0xC000;
    cpu.af.reg8.high = 0x5A;
    cpu.memory[0xC000] = 0x80;
    cpu.memory[0x0000] = 0x46; // LD B, (HL)
    cpu.memory[0x0001] = 0x70; // LD (HL), B
    cpu.memory[0x0002] = 0x40; // LD B, B
    cpu.memory[0x0003] = 0xCB;
    cpu.memory[0x0004] = 0x00; // RLC B
    cpu.memory[0x0005] = 0xCB;
    cpu.memory[0x0006] = 0x46; // BIT 0, (HL)
    cpu.memory[0x0007] = 0xCB;
    cpu.memory[0x0008] = 0x06; // RLC (HL)

    assert(execute_instruction(cpu) == 8);
    assert(cpu.bc.reg8.high == 0x80);
    assert(execute_instruction(cpu) == 8);
    assert(cpu.memory[0xC000] == 0x80);
    assert(execute_instruction(cpu) == 4);
    assert(execute_instruction(cpu) == 8);
    assert(get_flag(cpu, FLAG_C));
    assert(cpu.af.reg8.high == 0x5A);
    assert(execute_instruction(cpu) == 12);
    assert(get_flag(cpu, FLAG_Z));
    assert(execute_instruction(cpu) == 16);
    assert(cpu.memory[0xC000] == 0x01);
    assert(get_flag(cpu, FLAG_C));
}

static void test_control_flow_cycle_counts()
{
    CPU_LR35902 cpu{};
    cpu.memory[0x0000] = 0xC2; // JP NZ, a16
    cpu.memory[0x0001] = 0x06;
    cpu.memory[0x0002] = 0x00;
    cpu.memory[0x0006] = 0xCA; // JP Z, a16
    cpu.memory[0x0007] = 0x09;
    cpu.memory[0x0008] = 0x00;
    cpu.memory[0x0009] = 0x76; // HALT

    assert(execute_instruction(cpu) == 16);
    assert(cpu.pc == 0x0006);

    set_flag(cpu, FLAG_Z, false);
    assert(execute_instruction(cpu) == 12);
    assert(cpu.pc == 0x0009);

    assert(execute_instruction(cpu) == 4);
    assert(cpu.halted);
    assert(cpu.pc == 0x000A);
}

static void test_rom_only_cartridge()
{
    CPU_LR35902 cpu{};
    std::vector<uint8_t> rom(0x8000, 0);
    rom[0x0100] = 0x3E; // LD A, d8
    rom[0x0101] = 0x42;
    rom[0x0147] = 0x00; // ROM ONLY
    rom[0x0148] = 0x00; // 32 KiB
    std::string error;

    assert(load_rom_only_cartridge(cpu, std::move(rom), error));
    assert(error.empty());
    assert(cpu.pc == 0x0100);
    assert(cpu.af.reg16 == 0x01B0);
    assert(cpu.sp == 0xFFFE);
    assert(cpu_read(cpu, 0x0100) == 0x3E);
    assert(execute_instruction(cpu) == 8);
    assert(cpu.af.reg8.high == 0x42);

    cpu_write(cpu, 0x0100, 0x00);
    assert(cpu_read(cpu, 0x0100) == 0x3E);

    std::vector<uint8_t> unsupported_type(0x8000, 0);
    unsupported_type[0x0147] = 0x01;
    assert(!load_rom_only_cartridge(cpu, std::move(unsupported_type), error));
    assert(!error.empty());
    assert(cpu_read(cpu, 0x0100) == 0x3E);

    std::vector<uint8_t> wrong_size(0x4000, 0);
    assert(!load_rom_only_cartridge(cpu, std::move(wrong_size), error));
    assert(!error.empty());
}

int main()
{
    test_load_and_add();
    test_stack_and_loads();
    test_inc_dec_registers();
    test_add_hl_registers();
    test_timer_registers();
    test_flag_behavior_add_sub();
    test_cb_prefixed_bit_and_rotate();
    test_instruction_cycle_counts();
    test_control_flow_cycle_counts();
    test_rom_only_cartridge();
    return 0;
}
